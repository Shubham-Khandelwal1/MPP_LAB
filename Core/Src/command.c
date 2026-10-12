#include "command.h"
#include <string.h>

static bool parse_millihz(const char *text, uint32_t *value)
{
    uint32_t integer = 0U, fraction = 0U, digits = 0U;
    if (*text < '0' || *text > '9') { return false; }
    while (*text >= '0' && *text <= '9') {
        integer = integer * 10U + (uint32_t)(*text++ - '0');
        if (integer > 10000U) { return false; }
    }
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 3U) { return false; }
            fraction = fraction * 10U + (uint32_t)(*text++ - '0');
        }
        if (digits == 0U) { return false; }
    }
    if (*text != '\0') { return false; }
    while (digits++ < 3U) { fraction *= 10U; }
    *value = integer * 1000U + fraction;
    return *value >= WAVE_MIN_MILLIHZ && *value <= WAVE_MAX_MILLIHZ;
}

static bool parse_percent(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    if (*text == '\0') { return false; }
    while (*text >= '0' && *text <= '9') {
        result = result * 10U + (uint32_t)(*text++ - '0');
        if (result > 100U) { return false; }
    }
    *value = result;
    return *text == '\0';
}

command_t command_parse(char *line, const wave_config_t *current)
{
    command_t result = {COMMAND_ERROR, *current, "Unknown command; type help."};
    char *args[4];
    unsigned argc = 0U;
    char *p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') { ++p; }
        if (*p == '\0') { break; }
        if (argc == 4U) { return result; }
        args[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            if (*p >= 'A' && *p <= 'Z') { *p += 'a' - 'A'; }
            ++p;
        }
        if (*p != '\0') { *p++ = '\0'; }
    }
    if (argc == 0U) { result.kind = COMMAND_EMPTY; return result; }
    if (argc == 1U) {
        if (!strcmp(args[0], "help") || !strcmp(args[0], "?")) { result.kind = COMMAND_HELP; }
        else if (!strcmp(args[0], "status")) { result.kind = COMMAND_STATUS; }
        else if (!strcmp(args[0], "start") || !strcmp(args[0], "on")) { result.kind = COMMAND_START; }
        else if (!strcmp(args[0], "stop") || !strcmp(args[0], "off")) { result.kind = COMMAND_STOP; }
        return result;
    }
    if (!strcmp(args[0], "amp")) {
        if (argc == 2U && parse_percent(args[1], &result.config.amplitude_percent)) {
            result.kind = COMMAND_AMPLITUDE;
        } else { result.error = "Use amp <0..100 integer percent of VREF+ peak-to-peak>."; }
        return result;
    }
    if (!strcmp(args[0], "freq")) {
        if (argc == 2U && parse_millihz(args[1], &result.config.frequency_millihz)) {
            result.kind = COMMAND_FREQUENCY;
        } else { result.error = "Use freq <0.1..10000 Hz, at most 3 decimal places>."; }
        return result;
    }
    wave_shape_t shape;
    if (!strcmp(args[0], "sin") || !strcmp(args[0], "sine")) { shape = WAVE_SINE; }
    else if (!strcmp(args[0], "square") || !strcmp(args[0], "sq")) { shape = WAVE_SQUARE; }
    else if (!strcmp(args[0], "triangle") || !strcmp(args[0], "tri")) { shape = WAVE_TRIANGLE; }
    else if (!strcmp(args[0], "saw") || !strcmp(args[0], "sawtooth") || !strcmp(args[0], "sawup")) { shape = WAVE_SAW_UP; }
    else if (!strcmp(args[0], "sawdown")) { shape = WAVE_SAW_DOWN; }
    else { return result; }

    if (argc > 3U || !parse_millihz(args[1], &result.config.frequency_millihz)) {
        result.error = "Use <wave> <0.1..10000 Hz> [amplitude percent].";
        return result;
    }
    if (argc == 3U && !parse_percent(args[2], &result.config.amplitude_percent)) {
        result.error = "Amplitude must be an integer from 0 to 100 percent.";
        return result;
    }
    result.config.shape = shape;
    result.kind = COMMAND_WAVE;
    return result;
}
