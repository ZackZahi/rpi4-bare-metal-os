// smp.h - Multi-core (SMP) support
//
// Wakes secondary cores on RPi4 (Cortex-A72 x 4).
// Each core runs its own timer IRQ and pulls tasks from the shared scheduler.

#ifndef SMP_H
#define SMP_H

#define NUM_CORES 4

// ---- Spinlock ----

typedef struct {
    volatile unsigned int lock;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

void spin_lock(spinlock_t *lk);
void spin_unlock(spinlock_t *lk);

// ---- SMP init ----

// Initialize and wake secondary cores
void smp_init(void);

// Get current core ID (0-3)
static inline unsigned int smp_core_id(void) {
    unsigned long mpidr;
    asm volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (unsigned int)(mpidr & 0x3);
}

// ---- Per-core state ----

typedef struct {
    volatile unsigned int online;
    volatile unsigned long ticks;
    volatile unsigned long tasks_run;
} core_info_t;

core_info_t *smp_get_core_info(unsigned int core_id);

// Global scheduler lock
extern spinlock_t scheduler_lock;

#endif // SMP_H
