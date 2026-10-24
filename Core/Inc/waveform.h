#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdbool.h>
#include <stdint.h>

#define WAVE_MAX_SAMPLES 240U
#define WAVE_MAX_SAMPLE_HZ 250000U
#define WAVE_MIN_MILLIHZ 100U
#define WAVE_MAX_MILLIHZ 10000000U

typedef enum {
    WAVE_SINE, WAVE_SQUARE, WAVE_TRIANGLE, WAVE_SAW_UP, WAVE_SAW_DOWN
} wave_shape_t;

typedef struct {
    wave_shape_t shape;
    uint32_t frequency_millihz;
    uint32_t amplitude_percent; /* Peak-to-peak fraction of VREF+, centered at VREF+/2. */
} wave_config_t;

typedef struct {
    uint16_t prescaler;         /* Register values, not divisors. */
    uint16_t reload;
    uint16_t samples;
    uint32_t actual_millihz;
    uint32_t sample_hz;
} wave_timing_t;

bool wave_timing(uint32_t timer_hz, uint32_t frequency_millihz, wave_timing_t *timing);
void wave_fill(uint32_t *buffer, uint16_t count, const wave_config_t *config);
const char *wave_name(wave_shape_t shape);
uint32_t wave_pot_frequency(uint16_t adc_code);

/* DAC output driven by TIM6 and circular DMA. */
bool output_init(void);
bool output_start(const wave_config_t *config, wave_timing_t *timing);
void output_stop(void);
bool output_running(void);
uint32_t output_take_fault(void);

#endif
