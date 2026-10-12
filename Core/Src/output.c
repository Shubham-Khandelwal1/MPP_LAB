#include "output.h"
#include "board.h"

/* SRAM1/2, never CCM. G4 DAC peripheral registers require 32-bit accesses. */
static uint32_t samples[WAVE_MAX_SAMPLES];
static volatile bool running;
static volatile uint32_t fault;
#define DAC_TIM6_TRIGGER (7U << DAC_CR_TSEL1_Pos)
#define DMAMUX_DAC1_CH1 6U

static void disarm(void)
{
    TIM6->CR1 &= ~TIM_CR1_CEN;
    DAC1->CR &= ~(DAC_CR_TEN1 | DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1);
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    while (DMA1_Channel1->CCR & DMA_CCR_EN) { __NOP(); }
    running = false;
}

void output_stop(void)
{
    disarm();
    DMA1->IFCR = DMA_IFCR_CGIF1;
    DAC1->SR = DAC_SR_DMAUDR1;
    DAC1->DHR12R1 = 0U; /* TEN1=0: the DAC updates without a trigger. */
}

bool output_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_DAC1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    (void)RCC->APB1ENR1;
    GPIOA->MODER |= 3U << 8;
    GPIOA->PUPDR &= ~(3U << 8);
    DAC1->CR = 0U;
    /* MODE1=000: external PA4, output buffer on; factory trim is retained. */
    DAC1->MCR = (SystemCoreClock > 80000000U) ? DAC_MCR_HFSEL_0 : 0U;
    DAC1->DHR12R1 = 0U;
    /* TSEL1 is writable only while EN1=0. Set it before enabling the channel. */
    DAC1->CR = DAC_TIM6_TRIGGER;
    DAC1->CR |= DAC_CR_EN1;
    if (!board_wait(&DAC1->SR, DAC_SR_DAC1RDY, DAC_SR_DAC1RDY, 1000U)) { return false; }
    board_delay_us(20U);
    TIM6->CR1 = TIM_CR1_ARPE;
    TIM6->CR2 = 0U;
    TIM6->DIER = 0U; /* No per-sample CPU interrupts or timer DMA requests. */
    DMAMUX1_Channel0->CCR = DMAMUX_DAC1_CH1;
    NVIC_SetPriority(DMA1_Channel1_IRQn, 0U);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 0U);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    return true;
}

bool output_start(const wave_config_t *config, wave_timing_t *timing)
{
    if (!wave_timing(SystemCoreClock, config->frequency_millihz, timing)) { return false; }
    disarm();
    wave_fill(samples, timing->samples, config);
    /* DHR -> DOR occurs before the DMA request. Preload the last table sample,
       then DMA cycles 0..N-1. No duplicated entry on subsequent periods. */
    DAC1->DHR12R1 = samples[timing->samples - 1U];
    DMA1->IFCR = DMA_IFCR_CGIF1;
    DAC1->SR = DAC_SR_DMAUDR1;
    NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    DMA1_Channel1->CPAR = (uint32_t)&DAC1->DHR12R1;
    DMA1_Channel1->CMAR = (uint32_t)samples;
    DMA1_Channel1->CNDTR = timing->samples;
    DMA1_Channel1->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_CIRC |
                          DMA_CCR_PSIZE_1 | DMA_CCR_MSIZE_1 | DMA_CCR_PL_1 | DMA_CCR_TEIE;
    TIM6->CR2 = 0U;
    TIM6->PSC = timing->prescaler;
    TIM6->ARR = timing->reload;
    TIM6->CNT = 0U;
    TIM6->EGR = TIM_EGR_UG; /* Load PSC/ARR while DAC triggering is disabled. */
    TIM6->SR = 0U;
    TIM6->CR2 = TIM_CR2_MMS_1;
    __DMB();
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    DAC1->CR = DAC_CR_EN1 | DAC_TIM6_TRIGGER | DAC_CR_TEN1 | DAC_CR_DMAEN1 | DAC_CR_DMAUDRIE1;
    running = true;
    TIM6->CR1 |= TIM_CR1_CEN;
    return true;
}

bool output_running(void) { return running; }
uint32_t output_take_fault(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t result = fault;
    fault = 0U;
    __set_PRIMASK(primask);
    return result;
}
void DMA1_Channel1_IRQHandler(void)
{
    if (DMA1->ISR & DMA_ISR_TEIF1) { output_stop(); fault |= 1U; }
    DMA1->IFCR = DMA_IFCR_CGIF1;
}
void TIM6_DAC_IRQHandler(void)
{
    if (DAC1->SR & DAC_SR_DMAUDR1) { output_stop(); fault |= 2U; }
}
