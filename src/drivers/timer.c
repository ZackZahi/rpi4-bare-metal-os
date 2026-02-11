// timer.c - ARM Generic Timer driver for Raspberry Pi 4
//
// SMP-safe: timer_freq is read from the hardware register directly
// rather than relying on a static variable that only core 0 sets.
// Each core has its own physical timer (cntp_tval_el0, cntp_ctl_el0).

#include "timer.h"

// Global tick counter (incremented by core 0 only for system uptime)
static volatile unsigned long tick_count = 0;

// Timer interval in ticks (set once by core 0, read by all)
static volatile unsigned long timer_interval = 0;

unsigned long timer_get_frequency(void) {
    unsigned long freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

unsigned long timer_get_ticks(void) {
    unsigned long ticks;
    asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return ticks;
}

void timer_init(unsigned int interval_ms) {
    unsigned long freq = timer_get_frequency();

    // Calculate and store interval (all cores use the same value)
    unsigned long interval = (freq / 1000) * interval_ms;
    timer_interval = interval;

    // Set timer compare value
    asm volatile("msr cntp_tval_el0, %0" :: "r"(interval));

    // Enable timer, unmask interrupt
    unsigned long ctrl = 1;
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(ctrl));
}

void timer_handle_irq(void) {
    // Increment system tick count (only meaningful as aggregate)
    tick_count++;

    // Re-arm this core's timer
    // Read interval from the shared variable (set by timer_init)
    unsigned long interval = timer_interval;
    if (interval == 0) {
        // Fallback: compute from hardware frequency
        interval = (timer_get_frequency() / 1000) * 100;
    }
    asm volatile("msr cntp_tval_el0, %0" :: "r"(interval));
}

unsigned long timer_get_tick_count(void) {
    return tick_count;
}

void timer_delay_ms(unsigned int ms) {
    unsigned long freq = timer_get_frequency();
    unsigned long start = timer_get_ticks();
    unsigned long ticks_to_wait = (freq / 1000) * ms;

    while ((timer_get_ticks() - start) < ticks_to_wait) {
        asm volatile("nop");
    }
}
