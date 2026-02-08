// gic.h - GIC-400 + ARM Local Peripherals interrupt controller

#ifndef GIC_H
#define GIC_H

// Initialize the GIC-400
void gic_init(void);

// Enable a specific interrupt in the GIC
void gic_enable_interrupt(unsigned int int_id);

// Enable the physical timer IRQ via ARM Local Peripherals
// (required for QEMU raspi4b - the GIC alone won't route it)
void gic_enable_timer_irq(void);

// Check if timer IRQ is pending (via local interrupt source)
int gic_timer_irq_pending(void);

// Read interrupt acknowledge register
unsigned int gic_get_interrupt(void);

// Signal end of interrupt
void gic_end_interrupt(unsigned int int_id);

#endif // GIC_H
