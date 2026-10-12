#ifndef OUTPUT_H
#define OUTPUT_H
#include "waveform.h"
bool output_init(void);
bool output_start(const wave_config_t *config, wave_timing_t *timing);
void output_stop(void);
bool output_running(void);
uint32_t output_take_fault(void);
#endif
