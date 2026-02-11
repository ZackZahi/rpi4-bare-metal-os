// task.h - Task scheduler header

#ifndef TASK_H
#define TASK_H

#define MAX_TASKS 8

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

typedef struct task {
    unsigned long sp;
    unsigned long stack[1024];  // 8KB stack
    task_state_t state;
    unsigned int id;
    char name[32];
    unsigned long sleep_until;
    struct task *next;
} task_t;

// Scheduler API
void scheduler_init(void);
void task_create(void (*entry_point)(void), const char *name);
void schedule(void);
void task_yield(void);
void task_sleep(unsigned int ms);
void task_exit(void);
int task_kill(unsigned int task_id);  // Kill task by ID. Returns 0=success, -1=not found

// IRQ-based scheduling (called from vectors.S)
unsigned long schedule_irq(unsigned long current_sp);

// Task info
task_t *get_current_task(void);
task_t *get_task_pool(void);

#endif // TASK_H
