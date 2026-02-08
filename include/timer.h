// timer.h - ARM Generic Timer driver header

#ifndef TIMER_H
#define TIMER_H

// Initialize the timer with given interval in milliseconds
void timer_init(unsigned int interval_ms);

// Handle timer interrupt
void timer_handle_irq(void);

// Get current tick count
unsigned long timer_get_tick_count(void);

// Get timer frequency
unsigned long timer_get_frequency(void);

// Get current timer ticks
unsigned long timer_get_ticks(void);

// Delay for specified milliseconds (polling-based)
void timer_delay_ms(unsigned int ms);

#endif // TIMER_H
