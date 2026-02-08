// task.h - Task management and scheduler

#ifndef TASK_H
#define TASK_H

// Trapframe size: 34 unsigned longs (x0-x30, ELR, SPSR, padding)
#define TRAPFRAME_SIZE 34

// Task states
typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

// Task Control Block
typedef struct task {
    unsigned long sp;           // Saved stack pointer (points to trapframe)
    unsigned long stack[1024];  // 8KB stack per task
    task_state_t state;
    unsigned int id;
    char name[32];
    unsigned long sleep_until;
    struct task *next;
} task_t;

#define MAX_TASKS 8

// Initialise scheduler, adopt current context as task 0
void scheduler_init(void);

// Create a new task
void task_create(void (*entry_point)(void), const char *name);

// Pick next task — called from IRQ handler, returns new SP
// old_sp = interrupted task's SP (pointing to its trapframe)
// Returns the SP to restore (same or different task)
unsigned long schedule_irq(unsigned long old_sp);

// Voluntary yield (for cooperative use — wraps schedule_irq)
void task_yield(void);

// Sleep current task
void task_sleep(unsigned int ms);

// Exit current task
void task_exit(void);

// Accessors
task_t *get_current_task(void);
task_t *get_task_pool(void);

#endif // TASK_H
