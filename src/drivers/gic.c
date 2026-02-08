// gic.c - GIC-400 (Generic Interrupt Controller) driver for Raspberry Pi 4

#include "gic.h"

// GIC-400 base addresses for Raspberry Pi 4
#define GIC_BASE        0xFF840000

// GIC Distributor registers
#define GICD_BASE       (GIC_BASE + 0x1000)
#define GICD_CTLR       ((volatile unsigned int*)(GICD_BASE + 0x000))
#define GICD_ISENABLER  ((volatile unsigned int*)(GICD_BASE + 0x100))
#define GICD_IPRIORITYR ((volatile unsigned int*)(GICD_BASE + 0x400))
#define GICD_ITARGETSR  ((volatile unsigned int*)(GICD_BASE + 0x800))
#define GICD_ICFGR      ((volatile unsigned int*)(GICD_BASE + 0xC00))

// GIC CPU Interface registers
#define GICC_BASE       (GIC_BASE + 0x2000)
#define GICC_CTLR       ((volatile unsigned int*)(GICC_BASE + 0x000))
#define GICC_PMR        ((volatile unsigned int*)(GICC_BASE + 0x004))
#define GICC_IAR        ((volatile unsigned int*)(GICC_BASE + 0x00C))
#define GICC_EOIR       ((volatile unsigned int*)(GICC_BASE + 0x010))

// ARM Generic Timer interrupt ID for physical timer
#define TIMER_IRQ       30

// Initialize the GIC
void gic_init(void) {
    // Disable distributor
    *GICD_CTLR = 0;
    
    // Disable CPU interface
    *GICC_CTLR = 0;
    
    // Set priority mask to lowest priority (allow all interrupts)
    *GICC_PMR = 0xFF;
    
    // Enable distributor
    *GICD_CTLR = 1;
    
    // Enable CPU interface
    *GICC_CTLR = 1;
}

// Enable a specific interrupt
void gic_enable_interrupt(unsigned int int_id) {
    // Set interrupt priority (lower number = higher priority)
    unsigned int priority_reg = int_id / 4;
    unsigned int priority_shift = (int_id % 4) * 8;
    volatile unsigned int* priority_ptr = GICD_IPRIORITYR + priority_reg;
    unsigned int val = *priority_ptr;
    val &= ~(0xFF << priority_shift);
    val |= (0xA0 << priority_shift);  // Priority 0xA0
    *priority_ptr = val;
    
    // Set interrupt target to CPU 0
    unsigned int target_reg = int_id / 4;
    unsigned int target_shift = (int_id % 4) * 8;
    volatile unsigned int* target_ptr = GICD_ITARGETSR + target_reg;
    val = *target_ptr;
    val &= ~(0xFF << target_shift);
    val |= (0x01 << target_shift);  // Target CPU 0
    *target_ptr = val;
    
    // Enable the interrupt
    unsigned int enable_reg = int_id / 32;
    unsigned int enable_bit = int_id % 32;
    GICD_ISENABLER[enable_reg] = (1 << enable_bit);
}

// Get the current interrupt ID
unsigned int gic_get_interrupt(void) {
    return *GICC_IAR & 0x3FF;
}

// Signal end of interrupt
void gic_end_interrupt(unsigned int int_id) {
    *GICC_EOIR = int_id;
}
