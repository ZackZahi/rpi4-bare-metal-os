// gic.c - GIC-400 + ARM Local Peripherals interrupt controller for RPi4
//
// On QEMU raspi4b, the ARM generic timer interrupt is routed via
// the BCM2836-style ARM Local Peripherals controller at 0xFF800000.
// Each core has its own timer IRQ control and IRQ source registers.

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

// Per-core timer IRQ control: 0x40 + core_id * 4
#define CORE_TIMER_IRQ_CTRL(n)  ((volatile unsigned int *)(ARM_LOCAL_BASE + 0x40 + (n) * 4))

// Per-core IRQ source: 0x60 + core_id * 4
#define CORE_IRQ_SOURCE(n)      ((volatile unsigned int *)(ARM_LOCAL_BASE + 0x60 + (n) * 4))

// Bit 1 = nCNTPNSIRQ (physical non-secure timer)
#define CNTP_IRQ_ENABLE         (1 << 1)
#define IRQ_SOURCE_CNTP         (1 << 1)

// ---- GIC init (core 0, full distributor + CPU interface) ----

void gic_init(void) {
    *GICD_CTLR = 0;
    *GICC_CTLR = 0;
    *GICC_PMR = 0xFF;
    *GICD_CTLR = 1;
    *GICC_CTLR = 1;
}

// ---- GIC CPU interface init for secondary cores ----
// Each core has its own banked GICC registers at the same address

void gic_init_core(void) {
    *GICC_CTLR = 0;
    *GICC_PMR = 0xFF;
    *GICC_CTLR = 1;
}

// ---- Enable interrupt in distributor ----

void gic_enable_interrupt(unsigned int int_id) {
    unsigned int reg = int_id / 4;
    unsigned int shift = (int_id % 4) * 8;

    volatile unsigned int *prio = GICD_IPRIORITYR + reg;
    unsigned int val = *prio;
    val &= ~(0xFF << shift);
    val |= (0xA0 << shift);
    *prio = val;

    volatile unsigned int *tgt = GICD_ITARGETSR + reg;
    val = *tgt;
    val &= ~(0xFF << shift);
    val |= (0x01 << shift);
    *tgt = val;

    unsigned int en_reg = int_id / 32;
    unsigned int en_bit = int_id % 32;
    GICD_ISENABLER[en_reg] = (1 << en_bit);
}

// ---- Per-core timer IRQ via ARM Local Peripherals ----

void gic_enable_timer_irq(void) {
    *CORE_TIMER_IRQ_CTRL(0) = CNTP_IRQ_ENABLE;
}

void gic_enable_timer_irq_core(unsigned int core_id) {
    if (core_id < 4)
        *CORE_TIMER_IRQ_CTRL(core_id) = CNTP_IRQ_ENABLE;
}

int gic_timer_irq_pending(void) {
    return (*CORE_IRQ_SOURCE(0) & IRQ_SOURCE_CNTP) != 0;
}

int gic_timer_irq_pending_core(unsigned int core_id) {
    if (core_id >= 4) return 0;
    return (*CORE_IRQ_SOURCE(core_id) & IRQ_SOURCE_CNTP) != 0;
}

// ---- GIC interrupt acknowledge / end ----

unsigned int gic_get_interrupt(void) {
    return *GICC_IAR & 0x3FF;
}

void gic_end_interrupt(unsigned int int_id) {
    *GICC_EOIR = int_id;
}
