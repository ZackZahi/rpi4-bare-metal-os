// task.h - Task scheduler header with EL0 user-mode support

#ifndef TASK_H
#define TASK_H

#define MAX_TASKS 8
#define KSTACK_SIZE 4096  // 4KB kernel stack per task

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_DEAD
} task_state_t;

typedef struct task {
    unsigned long sp;              // Saved stack pointer (kernel SP for EL0 tasks)
    unsigned long stack[1024];     // 8KB user/task stack
    unsigned long kstack[KSTACK_SIZE / 8];  // 4KB kernel stack (for EL0 exception handling)
    task_state_t state;
    unsigned int id;
    char name[32];
    unsigned long sleep_until;
    int is_user;                   // 1 = EL0 user task, 0 = EL1 kernel task
    unsigned long user_sp;         // Saved SP_EL0 for user tasks
    struct task *next;
} task_t;

// Scheduler API
void scheduler_init(void);
void task_create(void (*entry_point)(void), const char *name);        // Creates EL0 (user) task
void task_create_kernel(void (*entry_point)(void), const char *name); // Creates EL1 (kernel) task
void schedule(void);
void task_yield(void);
void task_sleep(unsigned int ms);
void task_exit(void);
int task_kill(unsigned int task_id);

// IRQ-based scheduling
unsigned long schedule_irq(unsigned long current_sp);

// Task info
task_t *get_current_task(void);
task_t *get_task_pool(void);

// EL0 fault handler
unsigned long el0_sync_fault_handler(unsigned long sp);

#endif // TASK_H
