// gic.h - GIC-400 (Generic Interrupt Controller) driver header

#ifndef GIC_H
#define GIC_H

// Initialize the GIC
void gic_init(void);

// Enable a specific interrupt
void gic_enable_interrupt(unsigned int int_id);

#endif // GIC_H
