// gic.c - GIC-400 + ARM Local Peripherals interrupt controller for RPi4
//
// On real RPi4 hardware, the GIC-400 handles interrupt routing.
// On QEMU's raspi4b, the ARM generic timer interrupt is routed via
// the BCM2836-style ARM Local Peripherals controller, NOT the GIC.
//
// The local peripherals are at physical 0x40000000 on RPi2/3, but
// on RPi4 they are remapped to 0xFF800000.
//
// To get timer interrupts working on QEMU raspi4b, we need to:
//   1. Enable the timer via ARM system registers (done in timer.c)
//   2. Unmask CNTP interrupt for core 0 at 0xFF800040
//   3. The IRQ will then fire through the vector table
//
// We still initialise the GIC for completeness (needed on real hw).

#include "gic.h"

// ---- GIC-400 registers ----
#define GIC_BASE        0xFF840000UL

#define GICD_BASE       (GIC_BASE + 0x1000)
#define GICD_CTLR       ((volatile unsigned int *)(GICD_BASE + 0x000))
#define GICD_ISENABLER  ((volatile unsigned int *)(GICD_BASE + 0x100))
#define GICD_IPRIORITYR ((volatile unsigned int *)(GICD_BASE + 0x400))
#define GICD_ITARGETSR  ((volatile unsigned int *)(GICD_BASE + 0x800))

#define GICC_BASE       (GIC_BASE + 0x2000)
#define GICC_CTLR       ((volatile unsigned int *)(GICC_BASE + 0x000))
#define GICC_PMR        ((volatile unsigned int *)(GICC_BASE + 0x004))
#define GICC_IAR        ((volatile unsigned int *)(GICC_BASE + 0x00C))
#define GICC_EOIR       ((volatile unsigned int *)(GICC_BASE + 0x010))

// ---- ARM Local Peripherals (BCM2836-style) ----
// On RPi4 these are at 0xFF800000 (remapped from 0x40000000)
#define ARM_LOCAL_BASE          0xFF800000UL
#define CORE0_TIMER_IRQ_CTRL    ((volatile unsigned int *)(ARM_LOCAL_BASE + 0x40))
#define CORE0_IRQ_SOURCE        ((volatile unsigned int *)(ARM_LOCAL_BASE + 0x60))

// Bit 1 in CORE0_TIMER_IRQ_CTRL = nCNTPNSIRQ (physical non-secure timer)
// Bit 0 = nCNTPSIRQ (physical secure timer)
// Bit 3 = nCNTVIRQ (virtual timer)
#define CNTP_IRQ_ENABLE         (1 << 1)   // Non-secure physical timer

// Bit in CORE0_IRQ_SOURCE that indicates CNTP fired
#define IRQ_SOURCE_CNTP         (1 << 1)

void gic_init(void) {
    // Disable distributor and CPU interface
    *GICD_CTLR = 0;
    *GICC_CTLR = 0;

    // Allow all priority levels
    *GICC_PMR = 0xFF;

    // Enable distributor and CPU interface
    *GICD_CTLR = 1;
    *GICC_CTLR = 1;
}

void gic_enable_interrupt(unsigned int int_id) {
    // GIC: set priority
    unsigned int reg = int_id / 4;
    unsigned int shift = (int_id % 4) * 8;
    volatile unsigned int *prio = GICD_IPRIORITYR + reg;
    unsigned int val = *prio;
    val &= ~(0xFF << shift);
    val |= (0xA0 << shift);
    *prio = val;

    // GIC: target CPU 0
    volatile unsigned int *tgt = GICD_ITARGETSR + reg;
    val = *tgt;
    val &= ~(0xFF << shift);
    val |= (0x01 << shift);
    *tgt = val;

    // GIC: enable the interrupt
    unsigned int en_reg = int_id / 32;
    unsigned int en_bit = int_id % 32;
    GICD_ISENABLER[en_reg] = (1 << en_bit);
}

// Enable the physical timer interrupt via ARM Local Peripherals
// This is what actually makes QEMU raspi4b deliver the IRQ
void gic_enable_timer_irq(void) {
    *CORE0_TIMER_IRQ_CTRL = CNTP_IRQ_ENABLE;
}

// Check if the timer IRQ is pending via the local interrupt source register
int gic_timer_irq_pending(void) {
    return (*CORE0_IRQ_SOURCE & IRQ_SOURCE_CNTP) != 0;
}

unsigned int gic_get_interrupt(void) {
    return *GICC_IAR & 0x3FF;
}

void gic_end_interrupt(unsigned int int_id) {
    *GICC_EOIR = int_id;
}
