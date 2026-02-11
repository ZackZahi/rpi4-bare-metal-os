// gic.h - GIC-400 + ARM Local Peripherals interrupt controller

#ifndef GIC_H
#define GIC_H

// Initialize the GIC-400 distributor + CPU interface (call once from core 0)
void gic_init(void);

// Initialize GIC CPU interface for a secondary core
void gic_init_core(void);

// Enable a specific interrupt in the GIC
void gic_enable_interrupt(unsigned int int_id);

// Enable the physical timer IRQ via ARM Local Peripherals
void gic_enable_timer_irq(void);              // Core 0 (backward compat)
void gic_enable_timer_irq_core(unsigned int core_id);  // Any core

// Check if timer IRQ is pending
int gic_timer_irq_pending(void);
int gic_timer_irq_pending_core(unsigned int core_id);

// Read interrupt acknowledge register
unsigned int gic_get_interrupt(void);

// Signal end of interrupt
void gic_end_interrupt(unsigned int int_id);

#endif // GIC_H
