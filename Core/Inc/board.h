#ifndef BOARD_H
#define BOARD_H
#include "stm32g4xx.h"
#include <stdbool.h>
void board_init(void);
void board_delay_us(uint32_t microseconds);
bool board_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_us);
bool board_uses_hse(void);
#endif
