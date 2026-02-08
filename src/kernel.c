// kernel.c - Kernel with UART shell and timer interrupts

#include "uart.h"
#include "timer.h"
#include "gic.h"

// IRQ handler called from vectors.S irq_handler_stub
void irq_handler(void) {
    // Check if the physical timer expired
    unsigned long ctl;
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));

    if (ctl & 0x4) {  // ISTATUS bit set = timer expired
        timer_handle_irq();

        unsigned long ticks = timer_get_tick_count();

        // Print uptime every 10 seconds
        if (ticks % 100 == 0 && ticks > 0) {
            uart_puts("\n[Timer: ");
            uart_put_dec(ticks / 10);
            uart_puts("s]\nrpi4> ");
        }
    }
}

// ---- Utility functions ----

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

// ---- Command processor ----

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
        uart_puts("Features: UART I/O, Timer Interrupts, GIC-400\n");
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

    // Initialize GIC
    uart_puts("Setting up GIC interrupt controller...\n");
    gic_init();

    // Timer setup
    unsigned long freq = timer_get_frequency();
    uart_puts("Timer frequency: ");
    uart_put_dec(freq);
    uart_puts(" Hz\n");

    uart_puts("Setting up timer interrupts (100ms interval)...\n");
    timer_init(100);

    // Enable timer interrupt
    gic_enable_interrupt(30);   // GIC PPI 30 (for real hardware)
    gic_enable_timer_irq();     // ARM Local Peripherals (for QEMU)

    uart_puts("System ready!\n");
    uart_puts("\nType 'help' for available commands.\n\n");

    // Command loop
    char input_buffer[128];
    while (1) {
        uart_puts("rpi4> ");
        uart_gets(input_buffer, sizeof(input_buffer));
        process_command(input_buffer);
    }
}
