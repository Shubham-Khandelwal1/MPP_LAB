#include "uart.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static command_t parse(const char *text)
{
    char buffer[256];
    assert(strlen(text) < sizeof(buffer));
    strcpy(buffer, text);
    const wave_config_t current = {WAVE_TRIANGLE, 250000U, 80U};
    return command_parse(buffer, &current);
}

static void test_commands(void)
{
    command_t c = parse("sin 120");
    assert(c.kind == COMMAND_WAVE && c.config.shape == WAVE_SINE);
    assert(c.config.frequency_millihz == 120000U && c.config.amplitude_percent == 80U);
    c = parse("  SQUARE\t123.456 50 ");
    assert(c.kind == COMMAND_WAVE && c.config.shape == WAVE_SQUARE);
    assert(c.config.frequency_millihz == 123456U && c.config.amplitude_percent == 50U);
    const char *aliases[] = {"sine 1", "sq 1", "tri 1", "triangle 1", "saw 1", "sawtooth 1", "sawup 1", "sawdown 1"};
    for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        assert(parse(aliases[i]).kind == COMMAND_WAVE);
    }
    assert(parse("freq 0.1").config.frequency_millihz == 100U);
    assert(parse("freq 10000.000").config.frequency_millihz == 10000000U);
    assert(parse("amp 0").kind == COMMAND_AMPLITUDE);
    assert(parse("amp 100").config.amplitude_percent == 100U);
    assert(parse("stop").kind == COMMAND_STOP);
    assert(parse("start").kind == COMMAND_START);
    assert(parse("off").kind == COMMAND_STOP);
    assert(parse("on").kind == COMMAND_START);
    assert(parse("?").kind == COMMAND_HELP);
    assert(parse("status").kind == COMMAND_STATUS);
    assert(parse(" \t ").kind == COMMAND_EMPTY);
    const char *invalid[] = {"sin", "sin 0", "sin -1", "sin 0.099", "sin 10000.001",
        "sin 12.3456", "sin nan", "sin inf", "sin 120junk", "sin 1e3", "sin 120 101",
        "sin 120 80 extra", "sin 120 80 extra more", "sin 1.", "amp 1.5", "amp -1",
        "amp 9999999999999999999999999", "sin 99999999999999999999999999", "status 1",
        "amp 4294967296", "freq 10001", "sin +120", "sin .5", "unknown 120"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        c = parse(invalid[i]);
        if (c.kind != COMMAND_ERROR) { fprintf(stderr, "Accepted invalid: %s\n", invalid[i]); }
        assert(c.kind == COMMAND_ERROR);
    }
}

static void test_timing(void)
{
    wave_timing_t t;
    assert(wave_timing(144000000U, 120000U, &t));
    assert(t.samples == 240U && t.prescaler == 0U && t.reload == 4999U);
    assert(t.actual_millihz == 120000U && t.sample_hz == 28800U);
    assert(!wave_timing(144000000U, 99U, &t));
    assert(!wave_timing(144000000U, 10000001U, &t));
    assert(!wave_timing(0U, 1000U, &t));
    double worst_error = 0.0;
    const uint32_t clocks[] = {144000000U, 16000000U};
    /* Every supported 0.001 Hz setting, including all table-size boundaries. */
    for (unsigned c = 0; c < 2U; ++c) {
        for (uint32_t f = WAVE_MIN_MILLIHZ; f <= WAVE_MAX_MILLIHZ; ++f) {
            assert(wave_timing(clocks[c], f, &t));
            assert(t.samples >= 24U && t.samples <= WAVE_MAX_SAMPLES && t.samples % 4U == 0U);
            double fs = clocks[c] / (((double)t.prescaler + 1.0) * ((double)t.reload + 1.0));
            assert(fs <= WAVE_MAX_SAMPLE_HZ);
            double actual = fs / t.samples;
            double requested = f / 1000.0;
            double error = fabs(actual - requested) / requested;
            assert(error < (c == 0U ? 0.0009 : 0.008));
            assert(fabs((double)t.actual_millihz - actual * 1000.0) <= 0.50001);
            if (c == 0U && error > worst_error) { worst_error = error; }
        }
    }
    printf("Timer sweep: 19,999,802 cases; worst 144 MHz error %.5f%%\n", worst_error * 100.0);
}

static void test_pot_frequency(void)
{
    uint32_t previous = wave_pot_frequency(0U);
    assert(previous == 10000U); /* 10 Hz. UART still supports 0.1 Hz. */
    for (uint16_t code = 1U; code < 4096U; ++code) {
        const uint32_t frequency = wave_pot_frequency(code);
        assert(frequency >= previous && frequency >= 10000U &&
               frequency <= WAVE_MAX_MILLIHZ);
        previous = frequency;
    }
    assert(previous == WAVE_MAX_MILLIHZ);
    assert(wave_pot_frequency(1365U) == 100000U); /* 100 Hz boundary. */
    assert(wave_pot_frequency(2730U) == 1000000U); /* 1 kHz boundary. */
    assert(wave_pot_frequency(2048U) > 100000U);
    assert(wave_pot_frequency(2048U) < 1000000U);
}

static void test_shapes(void)
{
    uint32_t table[WAVE_MAX_SAMPLES];
    wave_config_t cfg = {WAVE_SINE, 120000U, 100U};
    for (unsigned s = 0; s <= WAVE_SAW_DOWN; ++s) {
        cfg.shape = (wave_shape_t)s;
        for (uint16_t n = 24U; n <= WAVE_MAX_SAMPLES; n += 4U) {
            for (unsigned amp = 0U; amp <= 100U; ++amp) {
                cfg.amplitude_percent = amp;
                wave_fill(table, n, &cfg);
                uint32_t lo = 4095U, hi = 0U;
                for (unsigned i = 0U; i < n; ++i) {
                    assert(table[i] <= 4095U);
                    if (table[i] < lo) { lo = table[i]; }
                    if (table[i] > hi) { hi = table[i]; }
                    if (s == WAVE_SAW_UP && i) { assert(table[i] >= table[i-1]); }
                    if (s == WAVE_SAW_DOWN && i) { assert(table[i] <= table[i-1]); }
                }
                assert(fabs((double)(hi - lo) - 4095.0 * amp / 100.0) <= 1.01);
                if (!amp) { assert(lo == 2048U && hi == 2048U); }
                if (amp == 100U) { assert(lo == 0U && hi == 4095U); }
                if (s == WAVE_SQUARE) {
                    for (unsigned i = 0; i < n; ++i) { assert(table[i] == (i < n/2U ? hi : lo)); }
                }
                if (s == WAVE_SINE) { assert(table[n/4U] == hi && table[3U*n/4U] == lo); }
                if (s == WAVE_TRIANGLE) {
                    assert(table[0] == lo && table[n/2U] == hi);
                    for (unsigned i = 1; i <= n/2U; ++i) { assert(table[i] >= table[i-1]); }
                    for (unsigned i = n/2U + 1U; i < n; ++i) { assert(table[i] <= table[i-1]); }
                }
            }
        }
    }
}

int main(void)
{
    test_commands(); test_timing(); test_pot_frequency(); test_shapes();
    puts("PASS: command validation, timer limits/accuracy, pot mapping, waveform shape/amplitude");
    return 0;
}
