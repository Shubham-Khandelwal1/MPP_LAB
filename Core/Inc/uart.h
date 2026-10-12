#ifndef UART_H
#define UART_H
#include <stdint.h>
void uart_init(void);
void uart_putc(char character);
void uart_puts(const char *text);
void uart_uint(uint32_t value);
void uart_millihz(uint32_t value);
/* -1: no data, -2: damaged/overflowed stream; discard through next newline. */
int uart_getc(void);
#endif
