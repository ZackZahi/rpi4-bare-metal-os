// kernel.c - Minimal kernel with UART output for Raspberry Pi 4

// UART0 memory-mapped registers for Raspberry Pi 4
// Pi 4 uses a different peripheral base address than Pi 3
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

// Main kernel entry point
void kernel_main(void) {
    // Initialize UART for output
    uart_init();
    
    // Print welcome message
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Minimal Raspberry Pi 4 OS - QEMU\n");
    uart_puts("========================================\n");
    uart_puts("\n");
    uart_puts("Hello from bare metal!\n");
    uart_puts("Kernel initialized successfully.\n");
    uart_puts("\n");
    
    // Infinite loop
    while(1) {
        uart_putc('.');
        delay(10000000);
    }
}
