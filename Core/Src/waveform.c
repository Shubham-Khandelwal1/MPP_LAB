#include "waveform.h"
#include <math.h>

bool wave_timing(uint32_t timer_hz, uint32_t frequency_millihz, wave_timing_t *timing)
{
    if (timer_hz == 0U || frequency_millihz < WAVE_MIN_MILLIHZ ||
        frequency_millihz > WAVE_MAX_MILLIHZ || timing == 0) {
        return false;
    }

    /* Multiples of four put both sine extrema exactly on table entries. */
    uint32_t count = (WAVE_MAX_SAMPLE_HZ * 1000U / frequency_millihz) & ~3U;
    if (count > WAVE_MAX_SAMPLES) {
        count = WAVE_MAX_SAMPLES;
    }
    const uint64_t numerator = (uint64_t)timer_hz * 1000U;
    const uint64_t sample_millihz = (uint64_t)frequency_millihz * count;
    uint64_t prescale = (numerator + sample_millihz * 65536U - 1U) /
                        (sample_millihz * 65536U);
    if (prescale == 0U || prescale > 65536U) {
        return false;
    }
    uint64_t period = (numerator + sample_millihz * prescale / 2U) /
                      (sample_millihz * prescale);
    /* Never exceed the DAC sample-rate limit after integer timer rounding. */
    const uint64_t min_period = ((uint64_t)timer_hz + prescale * WAVE_MAX_SAMPLE_HZ - 1U) /
                                (prescale * WAVE_MAX_SAMPLE_HZ);
    if (period < min_period) {
        period = min_period;
    }
    if (period == 0U || period > 65536U) {
        return false;
    }
    const uint64_t divisor = prescale * period * count;
    timing->prescaler = (uint16_t)(prescale - 1U);
    timing->reload = (uint16_t)(period - 1U);
    timing->samples = (uint16_t)count;
    timing->actual_millihz = (uint32_t)((numerator + divisor / 2U) / divisor);
    timing->sample_hz = (uint32_t)((timer_hz + prescale * period / 2U) / (prescale * period));
    return true;
}

void wave_fill(uint32_t *buffer, uint16_t count, const wave_config_t *config)
{
    const float amplitude = 2047.5f * (float)config->amplitude_percent / 100.0f;
    for (uint16_t i = 0; i < count; ++i) {
        const float phase = (float)i / (float)count;
        float normalized;
        switch (config->shape) {
        case WAVE_SQUARE:
            normalized = (i < count / 2U) ? 1.0f : -1.0f;
            break;
        case WAVE_TRIANGLE:
            normalized = (i < count / 2U) ? -1.0f + 4.0f * phase : 3.0f - 4.0f * phase;
            break;
        case WAVE_SAW_UP:
            normalized = -1.0f + 2.0f * (float)i / (float)(count - 1U);
            break;
        case WAVE_SAW_DOWN:
            normalized = 1.0f - 2.0f * (float)i / (float)(count - 1U);
            break;
        default:
            normalized = sinf(6.283185307179586f * phase);
            break;
        }
        float code = 2047.5f + amplitude * normalized;
        if (code < 0.0f) { code = 0.0f; }
        if (code > 4095.0f) { code = 4095.0f; }
        buffer[i] = (uint32_t)(code + 0.5f);
    }
}

const char *wave_name(wave_shape_t shape)
{
    static const char *const names[] = {"sin", "square", "triangle", "saw", "sawdown"};
    return ((unsigned)shape < sizeof(names) / sizeof(names[0])) ? names[shape] : "unknown";
}
