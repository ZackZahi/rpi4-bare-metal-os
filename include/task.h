// task.h - Task management and scheduler

#ifndef TASK_H
#define TASK_H

// Task states
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

// Task Control Block
typedef struct task {
    unsigned long sp;           // Saved stack pointer
    unsigned long stack[1024];  // 8KB stack per task (1024 x 8 bytes)
    task_state_t state;
    unsigned int id;
    char name[32];
    unsigned long sleep_until;  // Tick count to wake at
    struct task *next;          // Ready-queue link
} task_t;

// Initialise the scheduler (creates idle task)
void scheduler_init(void);

// Create a task. entry_point must be a void(void) function.
void task_create(void (*entry_point)(void), const char *name);

// Pick the next task and context-switch to it.
// Called from timer IRQ for preemptive scheduling, or from task_yield().
void schedule(void);

// Voluntarily give up the CPU
void task_yield(void);

// Sleep the current task for approximately `ms` milliseconds
void task_sleep(unsigned int ms);

// Terminate the current task
void task_exit(void);

// Get pointer to the currently running task
task_t *get_current_task(void);

#endif // TASK_H
