// kernel.c - Kernel with UART shell, timer interrupts, and preemptive scheduler

#include "uart.h"
#include "timer.h"
#include "gic.h"
#include "task.h"

static volatile int scheduler_enabled = 0;

// Called from vectors.S with the interrupted task's SP (pointing to trapframe).
// Returns the SP to restore (may be a different task's trapframe).
unsigned long irq_handler_c(unsigned long sp) {
    unsigned long ctl;
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));

    if (ctl & 0x4) {  // ISTATUS = timer expired
        timer_handle_irq();

        unsigned long ticks = timer_get_tick_count();
        if (ticks % 100 == 0 && ticks > 0) {
            uart_puts("\n[Timer: ");
            uart_put_dec(ticks / 10);
            uart_puts("s]\n");
        }

        // Preemptive scheduling
        if (scheduler_enabled) {
            sp = schedule_irq(sp);
        }
    }

    return sp;
}

// ---- Utility functions ----

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// ---- Demo tasks ----

static void task_counter(void) {
    for (int i = 1; i <= 5; i++) {
        uart_puts("[counter] ");
        uart_put_dec(i);
        uart_puts("/5\n");
        task_sleep(1000);
    }
    uart_puts("[counter] finished\n");
}

static void task_spinner(void) {
    const char spin[] = "|/-\\";
    for (int i = 0; i < 20; i++) {
        uart_puts("[spinner] ");
        uart_putc(spin[i % 4]);
        uart_puts("\n");
        task_sleep(500);
    }
    uart_puts("[spinner] finished\n");
}

// ---- Command processor ----

static const char *state_name(task_state_t s) {
    switch (s) {
        case TASK_READY:   return "READY";
        case TASK_RUNNING: return "RUNNING";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_DEAD:    return "DEAD";
        default:           return "?";
    }
}

static void process_command(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "help") == 0) {
        uart_puts("Available commands:\n");
        uart_puts("  help      - Show this help message\n");
        uart_puts("  echo      - Echo back what you type\n");
        uart_puts("  time      - Show current tick count\n");
        uart_puts("  info      - Show system information\n");
        uart_puts("  clear     - Clear screen\n");
        uart_puts("  hello     - Print a greeting\n");
        uart_puts("  ps        - List all tasks\n");
        uart_puts("  spawn     - Launch demo tasks\n");
        return;
    }

    if (strcmp(cmd, "echo") == 0) {
        uart_puts("Echo mode - type something and press Enter:\n> ");
        char buffer[128];
        uart_gets(buffer, sizeof(buffer));
        uart_puts("You typed: ");
        uart_puts(buffer);
        uart_puts("\n");
        return;
    }

    if (strcmp(cmd, "time") == 0) {
        unsigned long ticks = timer_get_tick_count();
        uart_puts("Uptime: ");
        uart_put_dec(ticks / 10);
        uart_puts(" seconds (");
        uart_put_dec(ticks);
        uart_puts(" ticks)\n");
        return;
    }

    if (strcmp(cmd, "info") == 0) {
        uart_puts("Raspberry Pi 4 Bare Metal OS\n");
        uart_puts("CPU: ARM Cortex-A72 (ARMv8-A)\n");
        uart_puts("Timer frequency: ");
        uart_put_dec(timer_get_frequency());
        uart_puts(" Hz\n");
        uart_puts("Scheduler: preemptive round-robin (100ms quantum)\n");
        uart_puts("Max tasks: ");
        uart_put_dec(MAX_TASKS);
        uart_puts("\n");
        return;
    }

    if (strcmp(cmd, "clear") == 0) {
        uart_puts("\033[2J\033[H");
        return;
    }

    if (strcmp(cmd, "hello") == 0) {
        uart_puts("Hello from bare metal!\n");
        uart_puts("Welcome to Raspberry Pi 4 OS\n");
        return;
    }

    if (strcmp(cmd, "ps") == 0) {
        task_t *pool = get_task_pool();
        uart_puts("ID  NAME            STATE\n");
        uart_puts("--  ----            -----\n");
        for (int i = 0; i < MAX_TASKS; i++) {
            if (pool[i].state == TASK_DEAD && i != 0)
                continue;
            if (pool[i].name[0] == '\0')
                continue;
            uart_put_dec(pool[i].id);
            if (pool[i].id < 10) uart_puts("   ");
            else uart_puts("  ");
            uart_puts(pool[i].name);
            int len = 0;
            const char *p = pool[i].name;
            while (*p++) len++;
            for (int j = len; j < 16; j++) uart_putc(' ');
            uart_puts(state_name(pool[i].state));
            if (&pool[i] == get_current_task())
                uart_puts(" <-- current");
            uart_puts("\n");
        }
        return;
    }

    if (strcmp(cmd, "spawn") == 0) {
        uart_puts("Spawning 'counter' and 'spinner' tasks...\n");
        task_create(task_counter, "counter");
        task_create(task_spinner, "spinner");
        return;
    }

    uart_puts("Unknown command: ");
    uart_puts(cmd);
    uart_puts("\nType 'help' for available commands.\n");
}

// ---- Kernel entry point ----

void kernel_main(void) {
    uart_init();

    uart_puts("\033[2J\033[H");
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Raspberry Pi 4 OS\n");
    uart_puts("========================================\n\n");
    uart_puts("Initializing system...\n");

    uart_puts("Setting up GIC interrupt controller...\n");
    gic_init();

    unsigned long freq = timer_get_frequency();
    uart_puts("Timer frequency: ");
    uart_put_dec(freq);
    uart_puts(" Hz\n");

    uart_puts("Setting up timer interrupts (100ms interval)...\n");
    timer_init(100);
    gic_enable_interrupt(30);
    gic_enable_timer_irq();

    uart_puts("Initializing task scheduler...\n");
    scheduler_init();
    scheduler_enabled = 1;

    uart_puts("System ready!\n");
    uart_puts("\nType 'help' for available commands.\n\n");

    char input_buffer[128];
    while (1) {
        uart_puts("rpi4> ");
        uart_gets(input_buffer, sizeof(input_buffer));
        process_command(input_buffer);
    }
}
