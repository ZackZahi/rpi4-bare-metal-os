// timer.c - ARM Generic Timer driver for Raspberry Pi 4

#include "timer.h"

// Timer frequency (set by hardware/firmware)
static unsigned long timer_freq = 0;

// Global tick counter
static volatile unsigned long tick_count = 0;

// Get timer frequency from system register
unsigned long timer_get_frequency(void) {
    unsigned long freq;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

// Get current timer value
unsigned long timer_get_ticks(void) {
    unsigned long ticks;
    asm volatile("mrs %0, cntpct_el0" : "=r"(ticks));
    return ticks;
}

// Initialize the ARM Generic Timer
void timer_init(unsigned int interval_ms) {
    // Get timer frequency
    timer_freq = timer_get_frequency();
    
    // Calculate timer interval (convert ms to ticks)
    unsigned long interval = (timer_freq / 1000) * interval_ms;
    
    // Set timer compare value
    asm volatile("msr cntp_tval_el0, %0" :: "r"(interval));
    
    // Enable timer (bit 0 = enable, bit 1 = mask interrupt)
    // Set bit 0 to enable, keep bit 1 clear to unmask
    unsigned long ctrl = 1;
    asm volatile("msr cntp_ctl_el0, %0" :: "r"(ctrl));
}

// Timer interrupt handler
void timer_handle_irq(void) {
    // Increment tick counter
    tick_count++;
    
    // Reset timer for next interrupt
    unsigned long interval = (timer_freq / 1000) * 100; // 100ms
    asm volatile("msr cntp_tval_el0, %0" :: "r"(interval));
}

// Get tick count
unsigned long timer_get_tick_count(void) {
    return tick_count;
}

// Simple delay using timer (polling, not interrupt-based)
void timer_delay_ms(unsigned int ms) {
    unsigned long start = timer_get_ticks();
    unsigned long ticks_to_wait = (timer_freq / 1000) * ms;
    
    while((timer_get_ticks() - start) < ticks_to_wait) {
        asm volatile("nop");
    }
}
