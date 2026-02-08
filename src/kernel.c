// kernel.c - Kernel with UART input/output and timer interrupts

#include "uart.h"
#include "timer.h"
#include "gic.h"

// Timer interrupt ID
#define TIMER_IRQ       30

// IRQ handler called from assembly stub
void irq_handler(void) {
    // Get interrupt ID from GIC
    extern unsigned int gic_get_interrupt(void);
    extern void gic_end_interrupt(unsigned int);
    
    unsigned int int_id = gic_get_interrupt();
    
    // Check if it's the timer interrupt
    if(int_id == TIMER_IRQ) {
        // Handle timer interrupt
        timer_handle_irq();
        
        unsigned long ticks = timer_get_tick_count();
        
        // Print tick every 10 seconds (100 ticks at 100ms intervals)
        if(ticks % 100 == 0) {
            uart_puts("[Timer: ");
            uart_put_dec(ticks / 10);
            uart_puts("s]\n");
        }
    }
    
    // Signal end of interrupt to GIC
    gic_end_interrupt(int_id);
}

// Simple string comparison
int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Simple string length
int strlen(const char* s) {
    int len = 0;
    while(*s++) len++;
    return len;
}

// Process a command
void process_command(char* cmd) {
    // Skip leading whitespace
    while(*cmd == ' ') cmd++;
    
    // Empty command
    if(*cmd == '\0') {
        return;
    }
    
    // Help command
    if(strcmp(cmd, "help") == 0) {
        uart_puts("Available commands:\n");
        uart_puts("  help      - Show this help message\n");
        uart_puts("  echo      - Echo back what you type\n");
        uart_puts("  time      - Show current tick count\n");
        uart_puts("  info      - Show system information\n");
        uart_puts("  clear     - Clear screen\n");
        uart_puts("  hello     - Print a greeting\n");
        return;
    }
    
    // Echo command
    if(strcmp(cmd, "echo") == 0) {
        uart_puts("Echo mode - type something and press Enter:\n> ");
        char buffer[128];
        uart_gets(buffer, sizeof(buffer));
        uart_puts("You typed: ");
        uart_puts(buffer);
        uart_puts("\n");
        return;
    }
    
    // Time command
    if(strcmp(cmd, "time") == 0) {
        unsigned long ticks = timer_get_tick_count();
        uart_puts("Uptime: ");
        uart_put_dec(ticks / 10);
        uart_puts(" seconds (");
        uart_put_dec(ticks);
        uart_puts(" ticks)\n");
        return;
    }
    
    // Info command
    if(strcmp(cmd, "info") == 0) {
        uart_puts("Raspberry Pi 4 Bare Metal OS\n");
        uart_puts("CPU: ARM Cortex-A72 (ARMv8-A)\n");
        uart_puts("Timer frequency: ");
        uart_put_dec(timer_get_frequency());
        uart_puts(" Hz\n");
        uart_puts("Features: UART I/O, Timer Interrupts, GIC-400\n");
        return;
    }
    
    // Clear command
    if(strcmp(cmd, "clear") == 0) {
        uart_puts("\033[2J\033[H");  // ANSI escape codes
        return;
    }
    
    // Hello command
    if(strcmp(cmd, "hello") == 0) {
        uart_puts("Hello from bare metal!\n");
        uart_puts("Welcome to Raspberry Pi 4 OS\n");
        return;
    }
    
    // Unknown command
    uart_puts("Unknown command: ");
    uart_puts(cmd);
    uart_puts("\n");
    uart_puts("Type 'help' for available commands.\n");
}

// Main kernel entry point
void kernel_main(void) {
    // Initialize UART for output
    uart_init();
    
    // Print welcome message
    uart_puts("\033[2J\033[H");  // Clear screen
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Raspberry Pi 4 OS - UART Input\n");
    uart_puts("========================================\n");
    uart_puts("\n");
    uart_puts("Initializing system...\n");
    
    // Initialize GIC
    uart_puts("Setting up GIC interrupt controller...\n");
    gic_init();
    
    // Print timer information
    unsigned long freq = timer_get_frequency();
    uart_puts("Timer frequency: ");
    uart_put_dec(freq);
    uart_puts(" Hz\n");
    
    // Initialize timer with 100ms interval
    uart_puts("Setting up timer interrupts (100ms interval)...\n");
    timer_init(100);
    
    // Enable timer interrupt in GIC
    gic_enable_interrupt(TIMER_IRQ);
    
    uart_puts("System ready!\n");
    uart_puts("\nType 'help' for available commands.\n\n");
    
    // Command loop
    char input_buffer[128];
    while(1) {
        uart_puts("rpi4> ");
        uart_gets(input_buffer, sizeof(input_buffer));
        process_command(input_buffer);
    }
}
