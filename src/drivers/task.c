// task.c - Preemptive round-robin scheduler with EL0 user-mode support
//
// Tasks can run at EL0 (user mode) or EL1 (kernel mode).
// - The shell (task 0) runs at EL1 — it needs direct hardware access.
// - Spawned tasks run at EL0 — they must use syscalls for I/O.
//
// Each EL0 task has:
//   - A user stack (task->stack) — used for the task's own code at EL0
//   - A kernel stack (task->kstack) — used when handling exceptions from EL0
//
// When an IRQ or syscall occurs from EL0:
//   1. CPU switches to SP_EL1 automatically
//   2. vectors.S saves trapframe onto the kernel stack
//   3. C handler runs on the kernel stack
//   4. vectors.S restores trapframe and erets back to EL0

#include "task.h"
#include "uart.h"
#include "timer.h"
#include "smp.h"

#define TRAPFRAME_SIZE 34

// Provided by syscall_stubs.S — EL0 tasks return here on function exit
extern void user_task_exit_stub(void);

static task_t task_pool[MAX_TASKS];
static task_t *current_task = 0;
static task_t *ready_queue_head = 0;
static unsigned int next_task_id = 0;

// ---- Queue helpers ----

static void enqueue_task(task_t *task) {
    task->next = 0;
    if (!ready_queue_head) {
        ready_queue_head = task;
        return;
    }
    task_t *t = ready_queue_head;
    while (t->next)
        t = t->next;
    t->next = task;
}

static task_t *dequeue_ready_task(void) {
    task_t *task = ready_queue_head;
    task_t *prev = 0;

    while (task) {
        if (task->state == TASK_BLOCKED &&
            timer_get_tick_count() >= task->sleep_until) {
            task->state = TASK_READY;
        }

        if (task->state == TASK_READY) {
            if (prev)
                prev->next = task->next;
            else
                ready_queue_head = task->next;
            task->next = 0;
            return task;
        }

        prev = task;
        task = task->next;
    }
    return 0;
}

static void remove_from_queue(task_t *target) {
    task_t *task = ready_queue_head;
    task_t *prev = 0;

    while (task) {
        if (task == target) {
            if (prev)
                prev->next = task->next;
            else
                ready_queue_head = task->next;
            task->next = 0;
            return;
        }
        prev = task;
        task = task->next;
    }
}

// ---- Task exit trampoline (EL1 kernel tasks only) ----
static void task_exit_trampoline(void) {
    if (current_task) {
        current_task->state = TASK_DEAD;
    }
    while (1)
        asm volatile("wfi");
}

// ---- Build fake trapframe ----

static void init_task_trapframe(task_t *task, void (*entry_point)(void), int is_user) {
    if (is_user) {
        // EL0 task: trapframe on kernel stack, eret drops to EL0
        unsigned long *ktop = &task->kstack[KSTACK_SIZE / sizeof(unsigned long)];
        ktop = (unsigned long *)((unsigned long)ktop & ~0xFUL);

        unsigned long *tf = ktop - TRAPFRAME_SIZE;
        for (int i = 0; i < TRAPFRAME_SIZE; i++)
            tf[i] = 0;

        // User stack top (for SP_EL0)
        unsigned long *utop = &task->stack[sizeof(task->stack) / sizeof(unsigned long)];
        utop = (unsigned long *)((unsigned long)utop & ~0xFUL);
        task->user_sp = (unsigned long)utop;

        tf[30] = (unsigned long)user_task_exit_stub;    // x30 = exit stub (safety net)
        tf[31] = (unsigned long)entry_point;            // ELR_EL1 = entry point
        tf[32] = 0x0;                                  // SPSR = EL0t, all IRQs enabled

        task->sp = (unsigned long)tf;
    } else {
        // EL1 task: trapframe on task's own stack
        unsigned long *top = &task->stack[sizeof(task->stack) / sizeof(unsigned long)];
        top = (unsigned long *)((unsigned long)top & ~0xFUL);

        unsigned long *tf = top - TRAPFRAME_SIZE;
        for (int i = 0; i < TRAPFRAME_SIZE; i++)
            tf[i] = 0;

        tf[30] = (unsigned long)task_exit_trampoline;   // x30 (LR)
        tf[31] = (unsigned long)entry_point;            // ELR_EL1
        tf[32] = 0x5;                                  // SPSR = EL1h, IRQs enabled

        task->sp = (unsigned long)tf;
        task->user_sp = 0;
    }
}

static void strcpy_local(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

// ---- Public API ----

task_t *get_current_task(void) { return current_task; }
task_t *get_task_pool(void) { return task_pool; }

void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_DEAD;
        task_pool[i].id = 0;
        task_pool[i].next = 0;
        task_pool[i].name[0] = '\0';
        task_pool[i].is_user = 0;
        task_pool[i].user_sp = 0;
    }
    ready_queue_head = 0;
    next_task_id = 0;

    task_t *shell = &task_pool[0];
    shell->id = next_task_id++;
    shell->state = TASK_RUNNING;
    shell->sleep_until = 0;
    shell->next = 0;
    shell->is_user = 0;
    shell->user_sp = 0;
    strcpy_local(shell->name, "shell");
    shell->sp = 0;

    current_task = shell;
}

void task_create(void (*entry_point)(void), const char *name) {
    asm volatile("msr daifset, #2");
    spin_lock(&scheduler_lock);

    task_t *task = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_DEAD) {
            task = &task_pool[i];
            break;
        }
    }
    if (!task) {
        uart_puts("[sched] ERROR: no free task slots\n");
        spin_unlock(&scheduler_lock);
        asm volatile("msr daifclr, #2");
        return;
    }

    task->id = next_task_id++;
    task->state = TASK_READY;
    task->sleep_until = 0;
    task->next = 0;
    task->is_user = 1;  // EL0 user mode
    strcpy_local(task->name, name);

    init_task_trapframe(task, entry_point, 1);  // 1 = user mode
    enqueue_task(task);

    spin_unlock(&scheduler_lock);
    asm volatile("msr daifclr, #2");
}

void task_create_kernel(void (*entry_point)(void), const char *name) {
    asm volatile("msr daifset, #2");
    spin_lock(&scheduler_lock);

    task_t *task = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_DEAD) {
            task = &task_pool[i];
            break;
        }
    }
    if (!task) {
        uart_puts("[sched] ERROR: no free task slots\n");
        spin_unlock(&scheduler_lock);
        asm volatile("msr daifclr, #2");
        return;
    }

    task->id = next_task_id++;
    task->state = TASK_READY;
    task->sleep_until = 0;
    task->next = 0;
    task->is_user = 0;
    strcpy_local(task->name, name);

    init_task_trapframe(task, entry_point, 0);
    enqueue_task(task);

    spin_unlock(&scheduler_lock);
    asm volatile("msr daifclr, #2");
}

int task_kill(unsigned int task_id) {
    asm volatile("msr daifset, #2");
    spin_lock(&scheduler_lock);

    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].id == task_id && task_pool[i].state != TASK_DEAD) {
            if (&task_pool[i] == &task_pool[0]) {
                spin_unlock(&scheduler_lock);
                asm volatile("msr daifclr, #2");
                return -1;
            }
            if (&task_pool[i] == current_task) {
                spin_unlock(&scheduler_lock);
                asm volatile("msr daifclr, #2");
                return -1;
            }

            remove_from_queue(&task_pool[i]);
            task_pool[i].state = TASK_DEAD;
            task_pool[i].next = 0;

            spin_unlock(&scheduler_lock);
            asm volatile("msr daifclr, #2");
            return 0;
        }
    }

    spin_unlock(&scheduler_lock);
    asm volatile("msr daifclr, #2");
    return -1;
}

unsigned long schedule_irq(unsigned long old_sp) {
    if (!current_task) return old_sp;

    current_task->sp = old_sp;

    // Save SP_EL0 for user tasks
    if (current_task->is_user) {
        unsigned long usp;
        asm volatile("mrs %0, sp_el0" : "=r"(usp));
        current_task->user_sp = usp;
    }

    task_t *prev = current_task;

    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        enqueue_task(prev);
    }

    task_t *next = dequeue_ready_task();

    if (!next) {
        prev->state = TASK_RUNNING;
        current_task = prev;
        return prev->sp;
    }

    current_task = next;
    current_task->state = TASK_RUNNING;

    // Restore SP_EL0 for user tasks
    if (next->is_user && next->user_sp) {
        asm volatile("msr sp_el0, %0" :: "r"(next->user_sp));
    }

    return current_task->sp;
}

void task_yield(void) {
    asm volatile("nop");
}

void task_sleep(unsigned int ms) {
    if (!current_task) return;

    asm volatile("msr daifset, #2");
    spin_lock(&scheduler_lock);
    unsigned long ticks = (ms + 99) / 100;
    current_task->sleep_until = timer_get_tick_count() + ticks;
    current_task->state = TASK_BLOCKED;
    enqueue_task(current_task);
    spin_unlock(&scheduler_lock);
    asm volatile("msr daifclr, #2");

    while (current_task->state == TASK_BLOCKED)
        asm volatile("wfi");
}

void task_exit(void) {
    if (!current_task) return;
    asm volatile("msr daifset, #2");
    current_task->state = TASK_DEAD;
    asm volatile("msr daifclr, #2");
    while (1)
        asm volatile("wfi");
}

// ---- EL0 fault handler ----
unsigned long el0_sync_fault_handler(unsigned long sp) {
    unsigned long esr, elr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, elr_el1" : "=r"(elr));

    uart_puts("\n[FAULT] EL0 exception: ESR=");
    uart_put_hex(esr);
    uart_puts(" ELR=");
    uart_put_hex(elr);
    if (current_task) {
        uart_puts(" task=");
        uart_puts(current_task->name);
    }
    uart_puts(" — killed\n");

    if (current_task) {
        current_task->state = TASK_DEAD;
        spin_lock(&scheduler_lock);
        sp = schedule_irq(sp);
        spin_unlock(&scheduler_lock);
    }

    return sp;
}
