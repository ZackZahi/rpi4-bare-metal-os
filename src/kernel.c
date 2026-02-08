// kernel.c - Minimal kernel with UART output and timer interrupts for Raspberry Pi 4

#include "timer.h"
#include "gic.h"

// UART0 memory-mapped registers for Raspberry Pi 4
#define MMIO_BASE       0xFE000000  // Pi 4 peripheral base

#define UART0_DR        ((volatile unsigned int*)(MMIO_BASE + 0x00201000))
#define UART0_FR        ((volatile unsigned int*)(MMIO_BASE + 0x00201018))
#define UART0_IBRD      ((volatile unsigned int*)(MMIO_BASE + 0x00201024))
#define UART0_FBRD      ((volatile unsigned int*)(MMIO_BASE + 0x00201028))
#define UART0_LCRH      ((volatile unsigned int*)(MMIO_BASE + 0x0020102C))
#define UART0_CR        ((volatile unsigned int*)(MMIO_BASE + 0x00201030))
#define UART0_ICR       ((volatile unsigned int*)(MMIO_BASE + 0x00201044))

// GPIO registers
#define GPFSEL1         ((volatile unsigned int*)(MMIO_BASE + 0x00200004))
#define GPPUD           ((volatile unsigned int*)(MMIO_BASE + 0x00200094))
#define GPPUDCLK0       ((volatile unsigned int*)(MMIO_BASE + 0x00200098))

// Timer interrupt ID
#define TIMER_IRQ       30

// Simple delay function
void delay(unsigned int count) {
    while(count--) {
        asm volatile("nop");
    }
}

// Initialize UART
void uart_init(void) {
    // Disable UART0
    *UART0_CR = 0x00000000;
    
    // Setup GPIO pins 14 and 15 for UART
    unsigned int selector = *GPFSEL1;
    selector &= ~(7 << 12);  // Clear GPIO 14
    selector |= 4 << 12;      // Set to alt0
    selector &= ~(7 << 15);  // Clear GPIO 15
    selector |= 4 << 15;      // Set to alt0
    *GPFSEL1 = selector;
    
    // Disable pull up/down for pins 14,15
    *GPPUD = 0x00000000;
    delay(150);
    *GPPUDCLK0 = (1 << 14) | (1 << 15);
    delay(150);
    *GPPUDCLK0 = 0x00000000;
    
    // Clear pending interrupts
    *UART0_ICR = 0x7FF;
    
    // Set baud rate to 115200
    // UART clock = 48MHz on Pi 4, divider = 48000000 / (16 * 115200) = 26.04
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    
    // Enable FIFO, 8-bit data transmission
    *UART0_LCRH = (1 << 4) | (1 << 5) | (1 << 6);
    
    // Enable UART0, receive, and transmit
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

// Send a character via UART
void uart_putc(unsigned char c) {
    // Wait for UART to become ready to transmit
    while(*UART0_FR & (1 << 5)) { }
    *UART0_DR = c;
}

// Send a string via UART
void uart_puts(const char* str) {
    while(*str) {
        if(*str == '\n') {
            uart_putc('\r');  // Add carriage return for newlines
        }
        uart_putc(*str++);
    }
}

// Print a number in decimal
void uart_put_dec(unsigned long value) {
    if(value == 0) {
        uart_putc('0');
        return;
    }
    
    char buffer[20];
    int i = 0;
    
    while(value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while(i > 0) {
        uart_putc(buffer[--i]);
    }
}

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
        
        // Print tick every second (10 ticks at 100ms intervals)
        if(ticks % 10 == 0) {
            uart_puts("Timer tick: ");
            uart_put_dec(ticks);
            uart_puts("\n");
        }
    }
    
    // Signal end of interrupt to GIC
    gic_end_interrupt(int_id);
}

// Main kernel entry point
void kernel_main(void) {
    // Initialize UART for output
    uart_init();
    
    // Print welcome message
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Raspberry Pi 4 OS - Timer Interrupts\n");
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
    
    uart_puts("System ready! Timer interrupts enabled.\n");
    
    // Infinite loop - work is done by interrupts now!
    while(1) {
        // Put CPU to sleep until next interrupt
        asm volatile("wfi");  // Wait For Interrupt
    }
}
