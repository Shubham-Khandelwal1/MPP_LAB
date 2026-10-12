#include "board.h"

static bool uses_hse;

bool board_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value, uint32_t timeout_us)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = (SystemCoreClock / 1000000U) * timeout_us;
    while ((*reg & mask) != value) {
        if ((uint32_t)(DWT->CYCCNT - start) >= cycles) { return false; }
    }
    return true;
}

void board_delay_us(uint32_t microseconds)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t cycles = (SystemCoreClock / 1000000U) * microseconds;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) { __NOP(); }
}

void board_init(void)
{
    SCB->VTOR = FLASH_BASE;
    NVIC_SetPriorityGrouping(3U); /* Four preemption priority bits. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    SystemCoreClockUpdate();

    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | PWR_CR1_VOS_0;
    if (!board_wait(&PWR->SR2, PWR_SR2_VOSF, 0U, 10000U)) { return; }
    PWR->CR5 |= PWR_CR5_R1MODE; /* Range 1 normal mode, at most 150 MHz. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_4WS |
                  FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN;
    if (!board_wait(&FLASH->ACR, FLASH_ACR_LATENCY, FLASH_ACR_LATENCY_4WS, 1000U)) { return; }

    /* The stock NUCLEO-G474RE has a 24 MHz crystal. HSI16 is a fallback. */
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    uses_hse = board_wait(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, 100000U);
    const uint32_t source = uses_hse ? RCC_PLLCFGR_PLLSRC_HSE : RCC_PLLCFGR_PLLSRC_HSI;
    const uint32_t m = uses_hse ? 6U : 4U;
    if (!uses_hse) { RCC->CR &= ~RCC_CR_HSEON; }
    /* 24/6 * 72/2 or 16/4 * 72/2 = 144 MHz. PLL R encoding 0 means /2. */
    RCC->PLLCFGR = source | ((m - 1U) << RCC_PLLCFGR_PLLM_Pos) |
                    (72U << RCC_PLLCFGR_PLLN_Pos) | RCC_PLLCFGR_PLLREN;
    RCC->CR |= RCC_CR_PLLON;
    if (!board_wait(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY, 10000U)) {
        RCC->CR &= ~RCC_CR_PLLON;
        uses_hse = false;
        return; /* Continue at HSI16; timing uses SystemCoreClock. */
    }

    /* Step through AHB /2 before /1 for the frequency-transition sequence. */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
                  RCC_CFGR_HPRE_DIV2;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { __NOP(); }
    SystemCoreClockUpdate();
    board_delay_us(10U);
    RCC->CFGR &= ~RCC_CFGR_HPRE;
    SystemCoreClockUpdate();
}

bool board_uses_hse(void) { return uses_hse; }
