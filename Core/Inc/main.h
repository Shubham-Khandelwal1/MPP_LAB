#ifndef MAIN_H
#define MAIN_H

#include "stm32g4xx.h"
#include <stdbool.h>

/* Shared clock helpers used by the UART and DAC setup. */
void board_init(void);
void board_delay_us(uint32_t microseconds);
bool board_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_us);
bool board_uses_hse(void);

#endif
