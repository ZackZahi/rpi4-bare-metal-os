// task.c - Task scheduler implementation
//
// Cooperative round-robin scheduler. Tasks are switched either:
//   1. Voluntarily via task_yield()
//   2. Preemptively via schedule() called from the timer IRQ
//
// Stack layout for a new task must exactly match what switch_to
// in context.S expects to pop:
//
//   SP+80: x29, x30   <- x30 = task_wrapper (entry point on first run)
//   SP+64: x27, x28
//   SP+48: x25, x26
//   SP+32: x23, x24
//   SP+16: x21, x22
//   SP+0:  x19, x20   <- x19 = real entry_point (used by task_wrapper)

#include "task.h"
#include "uart.h"
#include "timer.h"

#define MAX_TASKS 8

// 6 register pairs saved by switch_to (x19-x30)
#define CONTEXT_FRAME_SIZE 12

// Task pool
static task_t task_pool[MAX_TASKS];
static task_t *current_task = 0;
static task_t *ready_queue_head = 0;
static unsigned int next_task_id = 0;

// Assembly context switch
extern void switch_to(unsigned long *old_sp, unsigned long new_sp);

// Forward declarations
static void idle_task(void);
static void task_wrapper(void);

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
        // Wake up sleeping tasks whose deadline has passed
        if (task->state == TASK_BLOCKED && timer_get_tick_count() >= task->sleep_until) {
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

// ---- Task wrapper ----
// switch_to restores x19 = entry_point, x30 = task_wrapper, then does `ret`
// so we land here with the real entry point sitting in x19.
static void task_wrapper(void) {
    void (*entry)(void);
    asm volatile("mov %0, x19" : "=r"(entry));

    entry();

    // If the task function returns, clean up
    task_exit();
}

// ---- Stack initialisation ----
// Build a fake stack frame that switch_to can pop on first run.
static void init_task_stack(task_t *task, void (*entry_point)(void)) {
    // Point to one-past-the-end of the stack array, then align down to 16 bytes
    unsigned long *sp = &task->stack[1024];
    sp = (unsigned long *)((unsigned long)sp & ~0xFUL);

    // Push register pairs in the same order switch_to saves them,
    // so that the restore (which pops in reverse) gets the right values.
    //
    // switch_to saves:  x19/x20, x21/x22, x23/x24, x25/x26, x27/x28, x29/x30
    // switch_to restores (pops): x29/x30, x27/x28, x25/x26, x23/x24, x21/x22, x19/x20
    //
    // So on the stack (low address = top):
    //   sp[0]  = x19  (entry_point, used by task_wrapper)
    //   sp[1]  = x20  (0)
    //   sp[2]  = x21  (0)
    //   sp[3]  = x22  (0)
    //   sp[4]  = x23  (0)
    //   sp[5]  = x24  (0)
    //   sp[6]  = x25  (0)
    //   sp[7]  = x26  (0)
    //   sp[8]  = x27  (0)
    //   sp[9]  = x28  (0)
    //   sp[10] = x29  (0, frame pointer)
    //   sp[11] = x30  (task_wrapper, return address)

    sp -= CONTEXT_FRAME_SIZE;

    sp[0]  = (unsigned long)entry_point;    // x19
    sp[1]  = 0;                             // x20
    sp[2]  = 0;                             // x21
    sp[3]  = 0;                             // x22
    sp[4]  = 0;                             // x23
    sp[5]  = 0;                             // x24
    sp[6]  = 0;                             // x25
    sp[7]  = 0;                             // x26
    sp[8]  = 0;                             // x27
    sp[9]  = 0;                             // x28
    sp[10] = 0;                             // x29 (FP)
    sp[11] = (unsigned long)task_wrapper;   // x30 (LR → ret target)

    task->sp = (unsigned long)sp;
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

void scheduler_init(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_pool[i].state = TASK_DEAD;
        task_pool[i].id = 0;
    }
    ready_queue_head = 0;
    current_task = 0;
    next_task_id = 0;

    // The idle task ensures there's always something to run
    task_create(idle_task, "idle");
}

void task_create(void (*entry_point)(void), const char *name) {
    // Find a free slot
    task_t *task = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (task_pool[i].state == TASK_DEAD) {
            task = &task_pool[i];
            break;
        }
    }
    if (!task) {
        uart_puts("[scheduler] ERROR: no free task slots\n");
        return;
    }

    task->id = next_task_id++;
    task->state = TASK_READY;
    task->sleep_until = 0;
    task->next = 0;
    strcpy_local(task->name, name);

    init_task_stack(task, entry_point);
    enqueue_task(task);
}

void schedule(void) {
    // Disable interrupts during scheduling to avoid re-entrancy
    asm volatile("msr daifset, #2");

    if (!current_task) {
        // First time: just pick a task and jump to it
        current_task = dequeue_ready_task();
        if (current_task) {
            current_task->state = TASK_RUNNING;
            asm volatile("msr daifclr, #2");  // Re-enable before switching
            switch_to(0, current_task->sp);
        }
        asm volatile("msr daifclr, #2");
        return;
    }

    task_t *prev = current_task;

    // Re-queue current task if it's still runnable
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
        enqueue_task(prev);
    }

    current_task = dequeue_ready_task();

    if (!current_task) {
        // Nothing else to run — keep running the previous task
        current_task = prev;
        current_task->state = TASK_RUNNING;
        asm volatile("msr daifclr, #2");
        return;
    }

    current_task->state = TASK_RUNNING;
    asm volatile("msr daifclr, #2");

    // Context switch (saves prev's SP, restores current's SP)
    switch_to(&prev->sp, current_task->sp);
}

void task_yield(void) {
    schedule();
}

void task_sleep(unsigned int ms) {
    if (!current_task) return;

    // Convert ms to tick count (each tick = 100ms)
    unsigned long ticks = (ms + 99) / 100;
    current_task->sleep_until = timer_get_tick_count() + ticks;
    current_task->state = TASK_BLOCKED;
    schedule();
}

void task_exit(void) {
    if (!current_task) return;

    current_task->state = TASK_DEAD;
    current_task = 0;
    schedule();

    // Should never reach here
    while (1)
        asm volatile("wfi");
}

// Idle task — lowest priority, just waits for interrupts
static void idle_task(void) {
    while (1) {
        asm volatile("wfi");
    }
}
