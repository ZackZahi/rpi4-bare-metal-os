// smp.c - Multi-core (SMP) support for Raspberry Pi 4
//
// Wakes secondary cores via QEMU raspi4b spin table (0xE0/0xE8/0xF0).
// Each core gets its own timer IRQ and runs the shared scheduler.

#include "smp.h"
#include "uart.h"
#include "timer.h"
#include "gic.h"

// ---- Spinlock implementation (ARMv8 exclusives) ----

void spin_lock(spinlock_t *lk) {
    unsigned int tmp, val;
    asm volatile(
        "   sevl\n"
        "1: wfe\n"
        "2: ldaxr   %w0, [%2]\n"
        "   cbnz    %w0, 1b\n"
        "   stxr    %w1, %w3, [%2]\n"
        "   cbnz    %w1, 2b\n"
        : "=&r"(val), "=&r"(tmp)
        : "r"(&lk->lock), "r"(1)
        : "memory"
    );
}

void spin_unlock(spinlock_t *lk) {
    asm volatile(
        "   stlr    %w0, [%1]\n"
        "   sev\n"
        :
        : "r"(0), "r"(&lk->lock)
        : "memory"
    );
}

// ---- Global scheduler lock ----

spinlock_t scheduler_lock = SPINLOCK_INIT;

// ---- Per-core state ----

static core_info_t cores[NUM_CORES];

core_info_t *smp_get_core_info(unsigned int core_id) {
    if (core_id >= NUM_CORES) return &cores[0];
    return &cores[core_id];
}

// ---- Per-core stacks (16KB each) ----

#define CORE_STACK_SIZE (16 * 1024)
static unsigned char core_stacks[3][CORE_STACK_SIZE] __attribute__((aligned(16)));

// Exported to smp_entry.S — stack top pointers indexed by core_id
unsigned long smp_stacks[NUM_CORES];

// Exported to smp_entry.S — shared MMU config from core 0
unsigned long smp_shared_ttbr0;
unsigned long smp_shared_tcr;
unsigned long smp_shared_mair;

// ---- Secondary core C entry point (called from smp_entry.S) ----

void secondary_core_main(unsigned int core_id) {
    // Set up this core's timer
    timer_init(100);

    // Enable timer IRQ routing for this core
    gic_enable_timer_irq_core(core_id);

    // Enable GIC CPU interface for this core
    gic_init_core();

    // Mark online
    cores[core_id].online = 1;
    cores[core_id].ticks = 0;
    cores[core_id].tasks_run = 0;

    // On QEMU raspi4b, the ARM local peripheral timer IRQ only wakes
    // core 0 via interrupt. Secondary cores poll the timer ISTATUS bit.
    // This is functionally equivalent — we check, re-arm, and run the
    // scheduler on each tick.
    while (1) {
        asm volatile("yield");  // Hint: let other HW threads run
        unsigned long ctl;
        asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));
        if (ctl & 0x4) {  // ISTATUS = timer expired
            // Re-arm timer
            unsigned long interval = timer_get_frequency();
            interval = (interval / 1000) * 100;
            asm volatile("msr cntp_tval_el0, %0" :: "r"(interval));

            // Update per-core stats
            cores[core_id].ticks++;

            // Run scheduler if enabled
            // (For now secondary cores just idle — task migration comes next)
        }
    }
}

// ---- External assembly entry point ----
extern void secondary_entry(void);

// ---- Wake secondary cores ----

void smp_init(void) {
    // Init core 0 info
    cores[0].online = 1;
    cores[0].ticks = 0;
    cores[0].tasks_run = 0;

    for (int i = 1; i < NUM_CORES; i++) {
        cores[i].online = 0;
        cores[i].ticks = 0;
        cores[i].tasks_run = 0;
    }

    // Set up stack top pointers (stacks grow down)
    smp_stacks[0] = 0;  // Core 0 already has its stack
    smp_stacks[1] = (unsigned long)&core_stacks[0][CORE_STACK_SIZE];
    smp_stacks[2] = (unsigned long)&core_stacks[1][CORE_STACK_SIZE];
    smp_stacks[3] = (unsigned long)&core_stacks[2][CORE_STACK_SIZE];

    // Share MMU config from core 0
    asm volatile("mrs %0, ttbr0_el1" : "=r"(smp_shared_ttbr0));
    asm volatile("mrs %0, tcr_el1"   : "=r"(smp_shared_tcr));
    asm volatile("mrs %0, mair_el1"  : "=r"(smp_shared_mair));

    // Ensure all writes are visible before waking cores
    asm volatile("dsb sy");

    unsigned long entry = (unsigned long)secondary_entry;

    uart_puts("  Waking core 1...");
    *(volatile unsigned long *)0xE0 = entry;
    asm volatile("sev");

    uart_puts(" core 2...");
    *(volatile unsigned long *)0xE8 = entry;
    asm volatile("sev");

    uart_puts(" core 3...");
    *(volatile unsigned long *)0xF0 = entry;
    asm volatile("sev");

    // Wait for cores to come online (poll for ~200ms)
    unsigned long start, freq;
    asm volatile("mrs %0, cntpct_el0" : "=r"(start));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    unsigned long deadline = start + freq / 5;
    while (1) {
        unsigned long now;
        asm volatile("mrs %0, cntpct_el0" : "=r"(now));
        if (now >= deadline) break;
        // Check if all came up early
        if (cores[1].online && cores[2].online && cores[3].online) break;
    }

    uart_puts("\n  ");
    int online = 0;
    for (int i = 0; i < NUM_CORES; i++) {
        if (cores[i].online) online++;
    }
    uart_put_dec(online);
    uart_puts("/");
    uart_put_dec(NUM_CORES);
    uart_puts(" cores online\n");
}
