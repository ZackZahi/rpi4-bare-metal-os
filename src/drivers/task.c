// task.c - Preemptive round-robin scheduler (trapframe-based)

#include "task.h"
#include "uart.h"
#include "timer.h"

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

    tf[30] = (unsigned long)task_exit_trampoline;   // x30 (LR)
    tf[31] = (unsigned long)entry_point;             // ELR_EL1
    tf[32] = 0x5;                                    // SPSR_EL1 = EL1h, IRQs enabled

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
    // Next timer IRQ will do the switch
    asm volatile("nop");
}

void task_sleep(unsigned int ms) {
    if (!current_task) return;

    // Save pointer to THIS task before we get preempted
    // (current_task will change when the scheduler switches away)
    task_t *me = current_task;

    asm volatile("msr daifset, #2");
    unsigned long ticks = (ms + 99) / 100;
    me->sleep_until = timer_get_tick_count() + ticks;
    me->state = TASK_BLOCKED;
    asm volatile("msr daifclr, #2");

    // Wait until the scheduler wakes us up (sets state back to READY/RUNNING)
    while (me->state == TASK_BLOCKED)
        asm volatile("wfi");
}

void task_exit(void) {
    if (!current_task) return;

    task_t *me = current_task;

    asm volatile("msr daifset, #2");
    me->state = TASK_DEAD;
    asm volatile("msr daifclr, #2");

    while (1)
        asm volatile("wfi");
}
