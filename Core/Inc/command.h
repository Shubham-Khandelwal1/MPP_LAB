#ifndef COMMAND_H
#define COMMAND_H

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

#endif
