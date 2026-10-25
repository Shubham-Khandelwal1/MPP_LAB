#include "main.h"
#include "uart.h"
#include "waveform.h"
#include <string.h>

/* Board clock and short waits stay with the application entry point. */
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

/* The Cortex-M startup owns reset, data, BSS and constructor initialization.
   These hooks satisfy newlib's init-array support with -nostartfiles, without
   linking the generic CRT startup or its unused OS/file syscall stubs. */
void _init(void) {}
void _fini(void) {}

static wave_config_t config = {WAVE_SINE, 120000U, 80U};
static wave_timing_t timing;
static uint32_t uart_frequency_millihz = 120000U;
static uint32_t uart_amplitude_percent = 80U;

typedef enum { CONTROL_UNSET, CONTROL_UART, CONTROL_MANUAL } control_mode_t;
static control_mode_t control_mode = CONTROL_UNSET;

#define USER_BUTTON_DEBOUNCE_US 20000U
#define USER_BUTTON_STOP_US 2000000U
#define POT_POLL_US 50000U
#define POT_CHANGE_CODES 4U
typedef struct {
    bool sample;
    bool stable;
    uint32_t changed_at;
} button_t;
static button_t shape_button, mode_switch;
static uint32_t shape_press_started;
static bool shape_long_press_handled;
static bool pot_ready;
static uint16_t applied_frequency_code, applied_amplitude_code;
static bool applied_pot_valid, pot_fault_reported;
static uint32_t last_pot_cycles;

static void button_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;
    /* B1 is active high on PC13 with the board's default solder bridges. */
    GPIOC->MODER &= ~GPIO_MODER_MODE13;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~GPIO_PUPDR_PUPD13) | GPIO_PUPDR_PUPD13_1;
    /* PA1 floats high with the pull-up; closing it to GND selects UART. */
    GPIOA->MODER &= ~GPIO_MODER_MODE1;
    GPIOA->PUPDR = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD1) | GPIO_PUPDR_PUPD1_0;
    board_delay_us(100U); /* Let the pull-up settle before reading the boot mode. */
    shape_button.sample = shape_button.stable = (GPIOC->IDR & GPIO_IDR_ID13) != 0U;
    mode_switch.sample = mode_switch.stable = (GPIOA->IDR & GPIO_IDR_ID1) != 0U;
    shape_button.changed_at = mode_switch.changed_at = DWT->CYCCNT;
    shape_press_started = shape_button.changed_at;
}

static bool button_level_changed(button_t *button, bool sample)
{
    const uint32_t now = DWT->CYCCNT;
    if (sample != button->sample) {
        button->sample = sample;
        button->changed_at = now;
    }
    /* Accept a press or release only after the level stays steady for 20 ms. */
    const uint32_t debounce_cycles = (SystemCoreClock / 1000000U) * USER_BUTTON_DEBOUNCE_US;
    if (sample != button->stable &&
        (uint32_t)(now - button->changed_at) >= debounce_cycles) {
        button->stable = sample;
        return true;
    }
    return false;
}

static bool pot_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_ADC12EN;
    (void)RCC->AHB2ENR;
    GPIOA->MODER |= GPIO_MODER_MODE0; /* PA0: analog input, ADC1 channel 1. */
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD0;
    GPIOB->MODER |= GPIO_MODER_MODE0; /* PB0: analog input, ADC1 channel 15. */
    GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD0;
    /* Synchronous HCLK/4 keeps the ADC clock within its limit at 144 MHz. */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~(ADC_CCR_CKMODE | ADC_CCR_PRESC)) |
                        ADC_CCR_CKMODE_0 | ADC_CCR_CKMODE_1 | ADC_CCR_VREFEN;
    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;
    board_delay_us(20U);
    ADC1->CFGR = 0U; /* One software-triggered, right-aligned 12-bit conversion. */
    ADC1->SQR1 = ADC_SQR1_SQ1_0;
    ADC1->SMPR1 = (ADC1->SMPR1 & ~ADC_SMPR1_SMP1) | ADC_SMPR1_SMP1;
    ADC1->SMPR2 = (ADC1->SMPR2 & ~(ADC_SMPR2_SMP15 | ADC_SMPR2_SMP18)) |
                  ADC_SMPR2_SMP15 | ADC_SMPR2_SMP18;
    ADC1->DIFSEL &= ~((1U << 1) | (1U << 15) | (1U << 18));
    /* ADC1 channel 18 measures VREFINT for the OLED's voltage estimate. */
    ADC1->CR &= ~ADC_CR_ADCALDIF;
    ADC1->CR |= ADC_CR_ADCAL;
    if (!board_wait(&ADC1->CR, ADC_CR_ADCAL, 0U, 10000U)) { return false; }
    board_delay_us(1U);
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    return board_wait(&ADC1->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY, 1000U);
}

static bool pot_read(uint32_t channel, uint16_t *code)
{
    /* SQR1 may change only between regular conversions (ADSTART clear). */
    if (!board_wait(&ADC1->CR, ADC_CR_ADSTART, 0U, 1000U)) { return false; }
    ADC1->SQR1 = channel << ADC_SQR1_SQ1_Pos;
    uint32_t sum = 0U;
    for (unsigned i = 0U; i < 4U; ++i) {
        ADC1->ISR = ADC_ISR_EOC | ADC_ISR_EOS;
        ADC1->CR |= ADC_CR_ADSTART;
        if (!board_wait(&ADC1->ISR, ADC_ISR_EOC, ADC_ISR_EOC, 1000U)) { return false; }
        sum += ADC1->DR & 4095U;
    }
    *code = (uint16_t)((sum + 2U) / 4U);
    return true;
}

static bool manual_controls_read(uint16_t *frequency_code, uint16_t *amplitude_code)
{
    return pot_read(1U, frequency_code) && pot_read(15U, amplitude_code);
}

/* GM009605 v4.3: 128x64 SSD1306-compatible OLED on I2C1 PB8/PB9. */
#define OLED_WIDTH 128U
#define OLED_REFRESH_US 10000U
#define OLED_REFERENCE_US 500000U
static bool oled_ready;
static uint8_t oled_address, oled_drawn_pages, oled_next_page;
static uint32_t oled_refresh_cycles, oled_reference_cycles, oled_reference_mv;
static uint32_t oled_shown_frequency, oled_shown_peak_centivolts;
static uint32_t oled_shown_amp_ref_centivolts, oled_shown_ref_centivolts;
static wave_shape_t oled_shown_shape;
static bool oled_shown_running;
static control_mode_t oled_shown_mode;

static bool oled_wait_i2c(uint32_t flag)
{
    const uint32_t start = DWT->CYCCNT;
    const uint32_t timeout = (SystemCoreClock / 1000000U) * 2000U;
    for (;;) {
        const uint32_t status = I2C1->ISR;
        if (status & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)) { return false; }
        if (status & flag) { return true; }
        if ((uint32_t)(DWT->CYCCNT - start) >= timeout) { return false; }
    }
}

static bool oled_write(uint8_t control, const uint8_t *bytes, uint16_t count)
{
    if (!board_wait(&I2C1->ISR, I2C_ISR_BUSY, 0U, 2000U)) { return false; }
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    I2C1->CR2 = ((uint32_t)oled_address << 1) |
                ((uint32_t)(count + 1U) << I2C_CR2_NBYTES_Pos) |
                I2C_CR2_AUTOEND | I2C_CR2_START;
    if (!oled_wait_i2c(I2C_ISR_TXIS)) { goto failed; }
    I2C1->TXDR = control; /* 0x00 selects commands; 0x40 selects pixels. */
    for (uint16_t i = 0U; i < count; ++i) {
        if (!oled_wait_i2c(I2C_ISR_TXIS)) { goto failed; }
        I2C1->TXDR = bytes[i];
    }
    if (!oled_wait_i2c(I2C_ISR_STOPF)) { goto failed; }
    I2C1->ICR = I2C_ICR_STOPCF;
    return true;

failed:
    I2C1->CR2 |= I2C_CR2_STOP;
    (void)board_wait(&I2C1->ISR, I2C_ISR_STOPF, I2C_ISR_STOPF, 2000U);
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    return false;
}

static bool oled_page(uint8_t page, const uint8_t pixels[OLED_WIDTH])
{
    const uint8_t position[] = {(uint8_t)(0xB0U | page), 0x00U, 0x10U};
    return oled_write(0x00U, position, sizeof(position)) &&
           oled_write(0x40U, pixels, OLED_WIDTH);
}

static bool oled_init(void)
{
    RCC->CR |= RCC_CR_HSION;
    if (!board_wait(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY, 1000U)) { return false; }
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    (void)RCC->APB1ENR1;
    GPIOB->MODER = (GPIOB->MODER & ~((3U << 16) | (3U << 18))) |
                   (2U << 16) | (2U << 18);
    GPIOB->AFR[1] = (GPIOB->AFR[1] & ~0xFFU) | 0x44U; /* PB8/9: I2C1 AF4. */
    GPIOB->OTYPER |= (1U << 8) | (1U << 9); /* I2C needs open-drain outputs. */
    GPIOB->OSPEEDR |= (3U << 16) | (3U << 18);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~((3U << 16) | (3U << 18))) |
                   (1U << 16) | (1U << 18);
    I2C1->CR1 = 0U;
    RCC->CCIPR = (RCC->CCIPR & ~RCC_CCIPR_I2C1SEL) | RCC_CCIPR_I2C1SEL_1;
    /* HSI16, ~93 kHz standard-mode I2C: PRESC=1, SCLDEL=4,
       SDADEL=2, SCLH=38, SCLL=45. No HAL or LL clock helpers. */
    I2C1->TIMINGR = 0x1042262DU;
    I2C1->CR1 = I2C_CR1_PE;
    board_delay_us(20000U); /* Allow the OLED's charge pump supply to settle. */

    static const uint8_t setup[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0x7F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6
    };
    oled_address = 0x3CU;
    if (!oled_write(0x00U, setup, sizeof(setup))) {
        oled_address = 0x3DU;
        if (!oled_write(0x00U, setup, sizeof(setup))) { return false; }
    }
    const uint8_t blank[OLED_WIDTH] = {0U};
    for (uint8_t page = 0U; page < 8U; ++page) {
        if (!oled_page(page, blank)) { return false; }
    }
    const uint8_t on = 0xAFU;
    if (!oled_write(0x00U, &on, 1U)) { return false; }
    oled_refresh_cycles = DWT->CYCCNT -
                          (SystemCoreClock / 1000000U) * OLED_REFRESH_US;
    oled_reference_cycles = DWT->CYCCNT -
                            (SystemCoreClock / 1000000U) * OLED_REFERENCE_US;
    return true;
}

/* Only the glyphs used by the status screen are stored in flash. Each byte
   describes one 7-pixel-high column, with the least significant bit at top. */
static const uint8_t oled_font['Z' - ' ' + 1][5] = {
    ['-' - ' '] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['.' - ' '] = {0x00, 0x60, 0x60, 0x00, 0x00},
    ['0' - ' '] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1' - ' '] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2' - ' '] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3' - ' '] = {0x21, 0x41, 0x45, 0x4B, 0x31},
    ['4' - ' '] = {0x18, 0x14, 0x12, 0x7F, 0x10},
    ['5' - ' '] = {0x27, 0x45, 0x45, 0x45, 0x39},
    ['6' - ' '] = {0x3C, 0x4A, 0x49, 0x49, 0x30},
    ['7' - ' '] = {0x01, 0x71, 0x09, 0x05, 0x03},
    ['8' - ' '] = {0x36, 0x49, 0x49, 0x49, 0x36},
    ['9' - ' '] = {0x06, 0x49, 0x49, 0x29, 0x1E},
    ['A' - ' '] = {0x7E, 0x11, 0x11, 0x11, 0x7E},
    ['D' - ' '] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E' - ' '] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G' - ' '] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H' - ' '] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I' - ' '] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['L' - ' '] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M' - ' '] = {0x7F, 0x02, 0x0C, 0x02, 0x7F},
    ['N' - ' '] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O' - ' '] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P' - ' '] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['Q' - ' '] = {0x3E, 0x41, 0x51, 0x21, 0x5E},
    ['R' - ' '] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S' - ' '] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T' - ' '] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U' - ' '] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V' - ' '] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W' - ' '] = {0x3F, 0x40, 0x38, 0x40, 0x3F},
    ['Z' - ' '] = {0x61, 0x51, 0x49, 0x45, 0x43}
};

static bool oled_text(uint8_t page, const char *line)
{
    uint8_t pixels[OLED_WIDTH] = {0U};
    uint32_t x = 0U;
    while (*line && x + 5U < OLED_WIDTH) {
        const unsigned character = (unsigned char)*line++;
        if (character >= ' ' && character <= 'Z') {
            memcpy(&pixels[x], oled_font[character - ' '], 5U);
        }
        x += 6U;
    }
    return oled_page(page, pixels);
}

static char *oled_append(char *out, const char *text)
{
    while (*text) { *out++ = *text++; }
    return out;
}

static char *oled_decimal(char *out, uint32_t value)
{
    char reversed[10];
    unsigned count = 0U;
    do { reversed[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (count) { *out++ = reversed[--count]; }
    return out;
}

static char *oled_volts(char *out, uint32_t centivolts)
{
    out = oled_decimal(out, centivolts / 100U);
    *out++ = '.';
    *out++ = (char)('0' + (centivolts / 10U) % 10U);
    *out++ = (char)('0' + centivolts % 10U);
    return out;
}

static const char *oled_shape(wave_shape_t shape)
{
    switch (shape) {
    case WAVE_SINE: return "SINE";
    case WAVE_SQUARE: return "SQUARE";
    case WAVE_TRIANGLE: return "TRIANGLE";
    case WAVE_SAW_UP: return "SAW UP";
    case WAVE_SAW_DOWN: return "SAW DOWN";
    default: return "WAVE";
    }
}

static uint32_t oled_measure_reference(void)
{
    uint16_t raw;
    if (!pot_ready || !pot_read(18U, &raw) || raw == 0U) { return 0U; }
    /* Factory VREFINT calibration was taken at VREF+=3000 mV. Measuring
       the same bandgap against the board's reference reveals its voltage. */
    const uint32_t calibration = *(const uint16_t *)0x1FFF75AAUL;
    if (calibration < 1000U || calibration > 2200U) { return 0U; }
    const uint32_t millivolts = (3000U * calibration + raw / 2U) / raw;
    return (millivolts >= 1800U && millivolts <= 3600U) ? millivolts : 0U;
}

static bool oled_render(uint32_t peak_centivolts, uint32_t ref_centivolts,
                        bool running)
{
    char line[24];
    char *p;
    /* Transfer at most one 128-byte page per loop pass. This bounds the
       button and potentiometer polling delay on the 100 kHz I2C bus. */
    for (uint8_t offset = 0U; offset < 4U; ++offset) {
        const uint8_t row = (uint8_t)((oled_next_page + offset) & 3U);
        const bool missing = (oled_drawn_pages & (1U << row)) == 0U;
        switch (row) {
        case 0U:
            if (!missing && config.shape == oled_shown_shape &&
                running == oled_shown_running) { continue; }
            p = oled_append(line, oled_shape(config.shape));
            p = oled_append(p, running ? " RUN" : " STOP");
            *p = '\0';
            break;
        case 1U:
            if (!missing && timing.actual_millihz == oled_shown_frequency) { continue; }
            p = oled_append(line, "FREQ ");
            p = oled_decimal(p, timing.actual_millihz / 1000U);
            *p++ = '.';
            const uint32_t fraction = timing.actual_millihz % 1000U;
            *p++ = (char)('0' + fraction / 100U);
            *p++ = (char)('0' + (fraction / 10U) % 10U);
            *p++ = (char)('0' + fraction % 10U);
            p = oled_append(p, " HZ");
            *p = '\0';
            break;
        case 2U:
            if (!missing && peak_centivolts == oled_shown_peak_centivolts &&
                ref_centivolts == oled_shown_amp_ref_centivolts) { continue; }
            p = oled_append(line, "AMP 0-");
            p = oled_reference_mv ? oled_volts(p, peak_centivolts) : oled_append(p, "--.--");
            p = oled_append(p, " V");
            *p = '\0';
            break;
        default:
            if (!missing && control_mode == oled_shown_mode &&
                ref_centivolts == oled_shown_ref_centivolts) { continue; }
            p = oled_append(line, control_mode == CONTROL_MANUAL ? "MANUAL" : "UART");
            p = oled_append(p, " REF ");
            p = oled_reference_mv ? oled_volts(p, ref_centivolts) : oled_append(p, "--.--");
            *p++ = 'V';
            *p = '\0';
            break;
        }
        if (!oled_text((uint8_t)(row * 2U), line)) { return false; }
        if (row == 0U) {
            oled_shown_shape = config.shape;
            oled_shown_running = running;
        } else if (row == 1U) {
            oled_shown_frequency = timing.actual_millihz;
        } else if (row == 2U) {
            oled_shown_peak_centivolts = peak_centivolts;
            oled_shown_amp_ref_centivolts = ref_centivolts;
        } else {
            oled_shown_mode = control_mode;
            oled_shown_ref_centivolts = ref_centivolts;
        }
        oled_drawn_pages |= (uint8_t)(1U << row);
        oled_next_page = (uint8_t)((row + 1U) & 3U);
        return true;
    }
    return true;
}

static void oled_poll(void)
{
    if (!oled_ready) { return; }
    const uint32_t now = DWT->CYCCNT;
    if ((uint32_t)(now - oled_reference_cycles) >=
        (SystemCoreClock / 1000000U) * OLED_REFERENCE_US) {
        oled_reference_cycles = now;
        const uint32_t measured = oled_measure_reference();
        if (measured != 0U) { oled_reference_mv = measured; }
    }
    if ((uint32_t)(now - oled_refresh_cycles) <
        (SystemCoreClock / 1000000U) * OLED_REFRESH_US) { return; }
    oled_refresh_cycles = now;
    const uint32_t peak_mv = (oled_reference_mv * config.amplitude_percent + 50U) / 100U;
    const uint32_t peak_cv = (peak_mv + 5U) / 10U;
    const uint32_t ref_cv = (oled_reference_mv + 5U) / 10U;
    if (!oled_render(peak_cv, ref_cv, output_running())) {
        oled_ready = false; /* An absent OLED must never stop waveform generation. */
        uart_puts("\r\nWARN OLED update failed; waveform continues.\r\n> ");
    }
}

static void print_status(void)
{
    uart_puts(output_running() ? "RUN " : "STOP ");
    uart_puts(control_mode == CONTROL_UART ? "UART " : "MANUAL ");
    uart_puts(wave_name(config.shape));
    uart_puts(" requested="); uart_millihz(config.frequency_millihz);
    uart_puts(" Hz timer="); uart_millihz(timing.actual_millihz);
    uart_puts(" Hz amp="); uart_uint(config.amplitude_percent);
    uart_puts("% VREF+ peak from 0 V, samples="); uart_uint(timing.samples);
    uart_puts(" fs="); uart_uint(timing.sample_hz);
    uart_puts(" Hz clock="); uart_uint(SystemCoreClock);
    uart_puts(board_uses_hse() ? " Hz HSE\r\n" : " Hz HSI (RC accuracy)\r\n");
}

static void print_help(void)
{
    uart_puts(
        "sin|sine <Hz> [amp%]\r\n"
        "square|sq <Hz> [amp%]\r\n"
        "triangle|tri <Hz> [amp%]\r\n"
        "saw|sawtooth|sawup <Hz> [amp%]\r\n"
        "sawdown <Hz> [amp%]\r\n"
        "freq <Hz>   amp <0..100 integer %>\r\n"
        "start|on   stop|off   status   help|?\r\n"
        "Hz: 0.1..10000, up to 3 decimals. Example: sin 120\r\n"
        "Wave commands start output. freq/amp keep RUN/STOP state.\r\n"
        "B1 short press starts/cycles waves; hold 2 s for emergency stop.\r\n"
        "PA1 open or VDD=manual (PA0: 10..10000 Hz; PB0: 0..100% amp).\r\n"
        "PA1 to GND=UART; freq, amp and wave commands require PA1 low.\r\n"
        "Amplitude sets the peak above 0 V, as % of VREF+.\r\n"
        "PA4 DAC output. 115200 8N1. Enable terminal local echo.\r\n");
}

static bool select_controls(uint32_t frequency_millihz, uint32_t amplitude_percent)
{
    if (frequency_millihz == config.frequency_millihz &&
        amplitude_percent == config.amplitude_percent) { return true; }
    wave_config_t next = config;
    next.frequency_millihz = frequency_millihz;
    next.amplitude_percent = amplitude_percent;
    wave_timing_t next_timing;
    if (!wave_timing(SystemCoreClock, frequency_millihz, &next_timing)) { return false; }
    if (output_running() && !output_start(&next, &next_timing)) { return false; }
    config = next;
    timing = next_timing;
    return true;
}

static void set_control_mode(control_mode_t next, bool announce)
{
    if (next == control_mode) { return; }
    control_mode = next; /* The physical PA1 level determines the mode. */
    if (next == CONTROL_MANUAL) {
        uint16_t frequency_code, amplitude_code;
        applied_pot_valid = false;
        last_pot_cycles = DWT->CYCCNT;
        if (pot_ready && manual_controls_read(&frequency_code, &amplitude_code)) {
            if (select_controls(wave_pot_frequency(frequency_code),
                                wave_pot_amplitude(amplitude_code))) {
                applied_frequency_code = frequency_code;
                applied_amplitude_code = amplitude_code;
                applied_pot_valid = true;
            } else { uart_puts("\r\nERR timer configuration\r\n"); }
        } else if (pot_ready) {
            uart_puts("\r\nERR potentiometer ADC read failed\r\n");
            pot_fault_reported = true;
        }
    } else {
        if (!select_controls(uart_frequency_millihz, uart_amplitude_percent)) {
            uart_puts("\r\nERR timer configuration\r\n");
        }
    }
    if (announce) {
        uart_puts("\r\nOK ");
        print_status();
        uart_puts("> ");
    }
}

static void poll_pot(void)
{
    if (control_mode != CONTROL_MANUAL || !pot_ready) { return; }
    const uint32_t now = DWT->CYCCNT;
    if ((uint32_t)(now - last_pot_cycles) <
        (SystemCoreClock / 1000000U) * POT_POLL_US) { return; }
    last_pot_cycles = now;
    uint16_t frequency_code, amplitude_code;
    if (!manual_controls_read(&frequency_code, &amplitude_code)) {
        applied_pot_valid = false;
        if (!pot_fault_reported) {
            uart_puts("\r\nERR potentiometer ADC read failed; manual mode retained\r\n> ");
            pot_fault_reported = true;
        }
        return;
    }
    pot_fault_reported = false;
    const uint16_t frequency_difference = (frequency_code > applied_frequency_code) ?
        (frequency_code - applied_frequency_code) : (applied_frequency_code - frequency_code);
    const uint16_t amplitude_difference = (amplitude_code > applied_amplitude_code) ?
        (amplitude_code - applied_amplitude_code) : (applied_amplitude_code - amplitude_code);
    const bool update_frequency = !applied_pot_valid || frequency_difference >= POT_CHANGE_CODES;
    const bool update_amplitude = !applied_pot_valid || amplitude_difference >= POT_CHANGE_CODES;
    if ((update_frequency || update_amplitude) &&
        select_controls(update_frequency ? wave_pot_frequency(frequency_code) : config.frequency_millihz,
                        update_amplitude ? wave_pot_amplitude(amplitude_code) : config.amplitude_percent)) {
        if (update_frequency) { applied_frequency_code = frequency_code; }
        if (update_amplitude) { applied_amplitude_code = amplitude_code; }
        applied_pot_valid = true;
    }
}

static void handle_button_press(void)
{
    wave_config_t next = config;
    if (output_running()) {
        next.shape = (next.shape == WAVE_SAW_DOWN) ? WAVE_SINE : (wave_shape_t)(next.shape + 1);
    }
    wave_timing_t next_timing;
    if (!output_start(&next, &next_timing)) {
        uart_puts("\r\nERR timer configuration\r\n> ");
        return;
    }
    config = next;
    timing = next_timing;
    uart_puts("\r\nOK ");
    print_status();
    uart_puts("> ");
}

static void handle_emergency_stop(void)
{
    output_stop();
    uart_puts("\r\nEMERGENCY STOP ");
    print_status();
    uart_puts("> ");
}

static void poll_shape_button(void)
{
    const bool pressed = (GPIOC->IDR & GPIO_IDR_ID13) != 0U;
    if (button_level_changed(&shape_button, pressed)) {
        if (shape_button.stable) {
            /* Use the raw edge time so the 20 ms debounce does not lengthen
               the required hold or turn a just-short press into a stop. */
            shape_press_started = shape_button.changed_at;
            shape_long_press_handled = false;
        } else if (!shape_long_press_handled) {
            const uint32_t held_cycles = shape_button.changed_at - shape_press_started;
            if (held_cycles >= (SystemCoreClock / 1000000U) * USER_BUTTON_STOP_US) {
                handle_emergency_stop();
            } else {
                handle_button_press(); /* Short press acts on release. */
            }
        }
    }
    if (shape_button.stable && pressed && !shape_long_press_handled &&
        (uint32_t)(DWT->CYCCNT - shape_press_started) >=
        (SystemCoreClock / 1000000U) * USER_BUTTON_STOP_US) {
        shape_long_press_handled = true;
        handle_emergency_stop();
    }
}

static void execute(char *line)
{
    /* Parse into a copy so bad input cannot change the active waveform. */
    const command_t command = command_parse(line, &config);
    if (control_mode == CONTROL_MANUAL &&
        (command.kind == COMMAND_FREQUENCY || command.kind == COMMAND_AMPLITUDE ||
         command.kind == COMMAND_WAVE)) {
        uart_puts("ERR frequency and amplitude are set by PA0/PB0; connect PA1 to GND for UART mode.\r\n");
        return;
    }
    switch (command.kind) {
    case COMMAND_EMPTY: return;
    case COMMAND_HELP: print_help(); return;
    case COMMAND_STATUS: print_status(); return;
    case COMMAND_ERROR:
        uart_puts("ERR "); uart_puts(command.error); uart_puts("\r\n"); return;
    case COMMAND_STOP: output_stop(); break;
    case COMMAND_START:
        if (!output_start(&config, &timing)) { uart_puts("ERR timer configuration\r\n"); return; }
        break;
    default: {
        wave_timing_t next_timing;
        if (!wave_timing(SystemCoreClock, command.config.frequency_millihz, &next_timing)) {
            uart_puts("ERR timer configuration\r\n"); return;
        }
        /* Shape commands start output; freq and amp retain the current state. */
        const bool start = command.kind == COMMAND_WAVE || output_running();
        if (start && !output_start(&command.config, &next_timing)) {
            uart_puts("ERR timer configuration\r\n"); return;
        }
        config = command.config;
        timing = next_timing;
        if (command.kind == COMMAND_FREQUENCY || command.kind == COMMAND_WAVE) {
            uart_frequency_millihz = config.frequency_millihz;
        }
        if (command.kind == COMMAND_AMPLITUDE || command.kind == COMMAND_WAVE) {
            uart_amplitude_percent = config.amplitude_percent;
        }
        break;
    }
    }
    uart_puts("OK ");
    print_status();
}

int main(void)
{
    board_init();
    button_init();
    uart_init();
    uart_puts("\r\nNUCLEO-G474RE CMSIS function generator\r\n");
    if (!output_init()) {
        uart_puts("ERR DAC did not become ready. Reset the board.\r\n");
        while (1) { __WFI(); }
    }
    pot_ready = pot_init();
    if (!pot_ready) { uart_puts("ERR potentiometer ADC unavailable.\r\n"); }
    (void)wave_timing(SystemCoreClock, config.frequency_millihz, &timing);
    set_control_mode(mode_switch.stable ? CONTROL_MANUAL : CONTROL_UART, false);
    oled_ready = oled_init();
    if (!oled_ready) { uart_puts("WARN OLED not detected at I2C address 0x3C or 0x3D.\r\n"); }
    uart_puts("Output stopped. Tap B1 to start/cycle; hold 2 s to stop. PA1 open/VDD=manual, PA1 to GND=UART.\r\n");
    print_status();
    uart_puts("> ");
    char line[96];
    unsigned length = 0U;
    bool discard = false, previous_cr = false;
    for (;;) {
        const uint32_t fault = output_take_fault();
        if (fault) {
            uart_puts("\r\nERR output stopped: ");
            uart_puts((fault & 1U) ? "DMA transfer error" : "DAC DMA underrun");
            uart_puts(". Type start to retry.\r\n> ");
        }
        poll_shape_button();
        if (button_level_changed(&mode_switch, (GPIOA->IDR & GPIO_IDR_ID1) != 0U)) {
            set_control_mode(mode_switch.stable ? CONTROL_MANUAL : CONTROL_UART, true);
        }
        poll_pot();
        oled_poll();
        const int byte = uart_getc();
        if (byte == -1) { continue; }
        if (byte == -2) {
            discard = true; length = 0U; previous_cr = false;
            uart_puts("\r\nERR UART stream lost; press Enter, then resend command.\r\n");
            continue;
        }
        if (byte == '\n' && previous_cr) { previous_cr = false; continue; }
        previous_cr = byte == '\r';
        if (byte == '\r' || byte == '\n') {
            if (!discard) { line[length] = '\0'; execute(line); }
            else { uart_puts("ERR line discarded\r\n"); }
            discard = false; length = 0U;
            uart_puts("> ");
        } else if (!discard) {
            if (byte == '\b' || byte == 127) {
                if (length) { --length; }
            } else if ((byte >= 32 && byte <= 126) || byte == '\t') {
                if (length < sizeof(line) - 1U) { line[length++] = (char)byte; }
                else { discard = true; }
            } else {
                /* Do not execute a command containing damaged/control bytes. */
                discard = true;
            }
        }
    }
}
