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

uint32_t wave_pot_frequency(uint16_t adc_code)
{
    /* Split the pot travel into 10-100, 100-1000, and 1000-10000 Hz. */
    static const uint32_t limits[] = {10000U, 100000U, 1000000U, 10000000U};
    if (adc_code >= 4095U) { return WAVE_MAX_MILLIHZ; }
    const uint32_t scaled = (uint32_t)adc_code * 3U;
    const uint32_t decade = scaled / 4095U;
    const uint32_t fraction = scaled % 4095U;
    const uint64_t span = (uint64_t)limits[decade + 1U] - limits[decade];
    return limits[decade] + (uint32_t)((span * fraction + 2047U) / 4095U);
}

/* Hardware output is excluded from the host-only waveform tests. */
#ifdef STM32G474xx
#include "main.h"

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
#endif
