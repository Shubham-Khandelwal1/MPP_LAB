#ifndef UART_H
#define UART_H
#include <stdint.h>
#include "waveform.h"

typedef enum {
    COMMAND_EMPTY, COMMAND_WAVE, COMMAND_FREQUENCY, COMMAND_AMPLITUDE,
    COMMAND_START, COMMAND_STOP, COMMAND_STATUS, COMMAND_HELP, COMMAND_ERROR
} command_kind_t;

/* Parsing is strict; invalid input never modifies the current configuration. */
typedef struct {
    command_kind_t kind;
    wave_config_t config;
    const char *error;
} command_t;

command_t command_parse(char *line, const wave_config_t *current);

void uart_init(void);
void uart_putc(char character);
void uart_puts(const char *text);
void uart_uint(uint32_t value);
void uart_millihz(uint32_t value);
/* -1: no data, -2: damaged/overflowed stream; discard through next newline. */
int uart_getc(void);
#endif
