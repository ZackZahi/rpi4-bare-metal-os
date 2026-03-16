// syscall.c - Kernel-side syscall dispatcher
//
// Called from vectors.S when an SVC is trapped from EL0 (or EL1).
// The trapframe is on the stack: sp[0]=x0 ... sp[8]=x8 ... sp[31]=ELR sp[32]=SPSR
// We read x8 for the syscall number, x0/x1 for arguments, and write x0 for return.

#include "syscall.h"
#include "uart.h"
#include "task.h"
#include "smp.h"

// Trapframe offsets (unsigned long index)
#define TF_X0   0
#define TF_X1   1
#define TF_X8   8

unsigned long syscall_dispatch(unsigned long sp) {
    unsigned long *tf = (unsigned long *)sp;
    unsigned long syscall_nr = tf[TF_X8];
    unsigned long arg0 = tf[TF_X0];
    unsigned long arg1 = tf[TF_X1];

    switch (syscall_nr) {

    case SYS_WRITE: {
        // sys_write(buf, len) — write to UART
        const char *buf = (const char *)arg0;
        unsigned long len = arg1;
        for (unsigned long i = 0; i < len; i++)
            uart_putc(buf[i]);
        tf[TF_X0] = (unsigned long)len;
        break;
    }

    case SYS_READ: {
        // sys_read(buf, len) — read from UART (blocking, one char at a time)
        char *buf = (char *)arg0;
        unsigned long len = arg1;
        unsigned long i = 0;
        if (len > 0) {
            buf[i] = uart_getc();
            i = 1;
        }
        tf[TF_X0] = i;
        break;
    }

    case SYS_EXIT: {
        // sys_exit() — terminate current task, schedule next
        task_t *cur = get_current_task();
        if (cur) {
            cur->state = TASK_DEAD;
        }
        spin_lock(&scheduler_lock);
        sp = schedule_irq(sp);
        spin_unlock(&scheduler_lock);
        break;
    }

    case SYS_YIELD: {
        // sys_yield() — voluntarily give up CPU
        spin_lock(&scheduler_lock);
        sp = schedule_irq(sp);
        spin_unlock(&scheduler_lock);
        break;
    }

    case SYS_SLEEP: {
        // sys_sleep(ms) — block current task for ms milliseconds
        task_t *cur = get_current_task();
        if (cur) {
            unsigned long ticks = (arg0 + 99) / 100;
            // We need timer_get_tick_count but avoid circular include
            extern unsigned long timer_get_tick_count(void);
            cur->sleep_until = timer_get_tick_count() + ticks;
            cur->state = TASK_BLOCKED;
        }
        spin_lock(&scheduler_lock);
        sp = schedule_irq(sp);
        spin_unlock(&scheduler_lock);
        break;
    }

    case SYS_GETPID: {
        task_t *cur = get_current_task();
        tf[TF_X0] = cur ? cur->id : 0;
        break;
    }

    default:
        uart_puts("[syscall] unknown syscall ");
        uart_put_dec(syscall_nr);
        uart_puts("\n");
        tf[TF_X0] = (unsigned long)-1;
        break;
    }

    return sp;
}
