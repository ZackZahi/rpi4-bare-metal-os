// task.c - Preemptive round-robin scheduler (trapframe-based)
//
// How it works:
//   - Timer IRQ fires, vectors.S saves full register state onto the
//     interrupted task's stack (trapframe), then calls irq_handler_c(sp).
//   - irq_handler_c calls schedule_irq(sp) which saves the SP into the
//     current task's TCB, picks the next task, and returns that task's SP.
//   - vectors.S restores registers from the returned SP and does eret.
//
// For NEW tasks, we build a fake trapframe on their stack so that when
// vectors.S restores from it and does eret, execution starts at the
// task's entry point.

#include "task.h"
#include "uart.h"
#include "timer.h"

// Trapframe size: 34 unsigned longs (x0-x30, ELR, SPSR, padding)
#define TRAPFRAME_SIZE 34

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

// Remove a specific task from the ready queue (used by task_kill)
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

// ---- Task exit trampoline ----
static void task_exit_trampoline(void) {
    if (current_task) {
        current_task->state = TASK_DEAD;
    }
    while (1)
        asm volatile("wfi");
}

// ---- Build fake trapframe for a new task ----
static void init_task_trapframe(task_t *task, void (*entry_point)(void)) {
    unsigned long *top = &task->stack[1024];
    top = (unsigned long *)((unsigned long)top & ~0xFUL);

    unsigned long *tf = top - TRAPFRAME_SIZE;

    for (int i = 0; i < TRAPFRAME_SIZE; i++)
        tf[i] = 0;

    tf[30] = (unsigned long)task_exit_trampoline;  // x30 (LR)
    tf[31] = (unsigned long)entry_point;           // ELR_EL1
    tf[32] = 0x5;                                  // SPSR_EL1 = EL1h, IRQs enabled

    task->sp = (unsigned long)tf;
}

// ---- Simple string copy ----
static void strcpy_local(char *dst, const char *src) {
    while (*src)
        *dst++ = *src++;
    *dst = '\0';
}

// ---- Public API ----

task_t *get_current_task(void) {
    return current_task;
}

task_t *get_task_pool(void) {
    return task_pool;
}

void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_DEAD;
        task_pool[i].id = 0;
        task_pool[i].next = 0;
        task_pool[i].name[0] = '\0';
    }
    ready_queue_head = 0;
    next_task_id = 0;

    // Adopt current context as task 0 ("shell")
    task_t *shell = &task_pool[0];
    shell->id = next_task_id++;
    shell->state = TASK_RUNNING;
    shell->sleep_until = 0;
    shell->next = 0;
    strcpy_local(shell->name, "shell");
    shell->sp = 0;

    current_task = shell;
}

void task_create(void (*entry_point)(void), const char *name) {
    asm volatile("msr daifset, #2");

    task_t *task = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_DEAD) {
            task = &task_pool[i];
            break;
        }
    }
    if (!task) {
        uart_puts("[sched] ERROR: no free task slots\n");
        asm volatile("msr daifclr, #2");
        return;
    }

    task->id = next_task_id++;
    task->state = TASK_READY;
    task->sleep_until = 0;
    task->next = 0;
    strcpy_local(task->name, name);

    init_task_trapframe(task, entry_point);
    enqueue_task(task);

    asm volatile("msr daifclr, #2");
}

// Kill a task by ID. Returns 0 on success, -1 if not found.
// Cannot kill the shell (task 0) or the currently running task via this API.
int task_kill(unsigned int task_id) {
    asm volatile("msr daifset, #2");

    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].id == task_id && task_pool[i].state != TASK_DEAD) {
            // Don't kill the shell
            if (&task_pool[i] == &task_pool[0]) {
                asm volatile("msr daifclr, #2");
                return -1;
            }
            // Don't kill self (use task_exit for that)
            if (&task_pool[i] == current_task) {
                asm volatile("msr daifclr, #2");
                return -1;
            }

            // Remove from ready queue if queued
            remove_from_queue(&task_pool[i]);

            // Mark dead
            task_pool[i].state = TASK_DEAD;
            task_pool[i].next = 0;

            asm volatile("msr daifclr, #2");
            return 0;
        }
    }

    asm volatile("msr daifclr, #2");
    return -1;
}

unsigned long schedule_irq(unsigned long old_sp) {
    if (!current_task) return old_sp;

    current_task->sp = old_sp;

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
    return current_task->sp;
}

void task_yield(void) {
    asm volatile("nop");
}

void task_sleep(unsigned int ms) {
    if (!current_task) return;

    asm volatile("msr daifset, #2");
    unsigned long ticks = (ms + 99) / 100;
    current_task->sleep_until = timer_get_tick_count() + ticks;
    current_task->state = TASK_BLOCKED;
    // Re-enqueue so dequeue_ready_task can check the sleep timer
    enqueue_task(current_task);
    asm volatile("msr daifclr, #2");

    // Spin until the scheduler wakes us (sets state back to RUNNING)
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
