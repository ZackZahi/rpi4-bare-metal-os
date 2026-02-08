// uart.c - UART driver for Raspberry Pi 4

#include "uart.h"

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

// UART Flag Register bits
#define UART_FR_RXFE    (1 << 4)    // Receive FIFO empty
#define UART_FR_TXFF    (1 << 5)    // Transmit FIFO full

// Simple delay function
static void delay(unsigned int count) {
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
    while(*UART0_FR & UART_FR_TXFF) { }
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

// Print a number in hexadecimal
void uart_put_hex(unsigned long value) {
    const char hex_chars[] = "0123456789ABCDEF";
    uart_puts("0x");
    for(int i = 60; i >= 0; i -= 4) {
        uart_putc(hex_chars[(value >> i) & 0xF]);
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

// Check if data is available to read
int uart_has_data(void) {
    return !(*UART0_FR & UART_FR_RXFE);
}

// Read a character (blocking)
unsigned char uart_getc(void) {
    // Wait until data is available
    while(*UART0_FR & UART_FR_RXFE) { }
    return (unsigned char)(*UART0_DR & 0xFF);
}

// Read a character (non-blocking)
// Returns -1 if no data available
int uart_getc_nonblock(void) {
    if(*UART0_FR & UART_FR_RXFE) {
        return -1;
    }
    return (int)(*UART0_DR & 0xFF);
}

// Read a line of input with echo
// Supports backspace and basic line editing
void uart_gets(char* buffer, int max_len) {
    int pos = 0;
    
    while(1) {
        unsigned char c = uart_getc();
        
        // Handle Enter/Return
        if(c == '\r' || c == '\n') {
            buffer[pos] = '\0';
            uart_puts("\n");
            return;
        }
        
        // Handle Backspace (0x7F or 0x08)
        if(c == 0x7F || c == 0x08) {
            if(pos > 0) {
                pos--;
                uart_puts("\b \b");  // Erase character on screen
            }
            continue;
        }
        
        // Handle Ctrl+C
        if(c == 0x03) {
            uart_puts("^C\n");
            buffer[0] = '\0';
            return;
        }
        
        // Handle Ctrl+U (clear line)
        if(c == 0x15) {
            while(pos > 0) {
                uart_puts("\b \b");
                pos--;
            }
            continue;
        }
        
        // Handle printable characters
        if(c >= 32 && c < 127 && pos < max_len - 1) {
            buffer[pos++] = c;
            uart_putc(c);  // Echo character
        }
    }
}
