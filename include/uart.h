// uart.h - UART driver header

#ifndef UART_H
#define UART_H

// Initialize UART
void uart_init(void);

// Output functions
void uart_putc(unsigned char c);
void uart_puts(const char* str);
void uart_put_hex(unsigned long value);
void uart_put_dec(unsigned long value);

// Input functions
unsigned char uart_getc(void);      // Blocking read
int uart_getc_nonblock(void);       // Non-blocking read (-1 if no data)
int uart_has_data(void);            // Check if data available

// Line input with echo
void uart_gets(char* buffer, int max_len);

#endif // UART_H
