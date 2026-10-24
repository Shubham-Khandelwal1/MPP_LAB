#include "main.h"
#include "uart.h"
#include "waveform.h"

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

typedef enum { CONTROL_UNSET, CONTROL_UART, CONTROL_MANUAL } control_mode_t;
static control_mode_t control_mode = CONTROL_UNSET;

#define USER_BUTTON_DEBOUNCE_US 20000U
#define POT_POLL_US 50000U
#define POT_CHANGE_CODES 4U
typedef struct {
    bool sample;
    bool stable;
    uint32_t changed_at;
} button_t;
static button_t shape_button, mode_switch;
static bool pot_ready;
static uint16_t applied_pot_code;
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

static bool button_pressed_event(button_t *button, bool sample)
{
    return button_level_changed(button, sample) && button->stable;
}

static bool pot_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
    (void)RCC->AHB2ENR;
    GPIOA->MODER |= GPIO_MODER_MODE0; /* PA0: analog input, ADC1 channel 1. */
    GPIOA->PUPDR &= ~GPIO_PUPDR_PUPD0;
    /* Synchronous HCLK/4 keeps the ADC clock within its limit at 144 MHz. */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~(ADC_CCR_CKMODE | ADC_CCR_PRESC)) |
                        ADC_CCR_CKMODE_0 | ADC_CCR_CKMODE_1;
    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;
    board_delay_us(20U);
    ADC1->CFGR = 0U; /* One software-triggered, right-aligned 12-bit conversion. */
    ADC1->SQR1 = ADC_SQR1_SQ1_0;
    ADC1->SMPR1 = (ADC1->SMPR1 & ~ADC_SMPR1_SMP1) | ADC_SMPR1_SMP1;
    ADC1->DIFSEL &= ~(1U << 1); /* Channel 1 is single-ended. */
    ADC1->CR &= ~ADC_CR_ADCALDIF;
    ADC1->CR |= ADC_CR_ADCAL;
    if (!board_wait(&ADC1->CR, ADC_CR_ADCAL, 0U, 10000U)) { return false; }
    board_delay_us(1U);
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    return board_wait(&ADC1->ISR, ADC_ISR_ADRDY, ADC_ISR_ADRDY, 1000U);
}

static bool pot_read(uint16_t *code)
{
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

static void print_status(void)
{
    uart_puts(output_running() ? "RUN " : "STOP ");
    uart_puts(control_mode == CONTROL_UART ? "UART " : "MANUAL ");
    uart_puts(wave_name(config.shape));
    uart_puts(" requested="); uart_millihz(config.frequency_millihz);
    uart_puts(" Hz timer="); uart_millihz(timing.actual_millihz);
    uart_puts(" Hz amp="); uart_uint(config.amplitude_percent);
    uart_puts("% VREF+ p-p, offset=50% VREF+, samples="); uart_uint(timing.samples);
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
        "B1 USER starts the selected wave, then cycles shapes on each press.\r\n"
        "PA1 open or VDD=manual (PA0 pot: 10..10000 Hz).\r\n"
        "PA1 to GND=UART; freq and wave commands require PA1 low.\r\n"
        "Amplitude is % of VREF+ peak-to-peak, centered at VREF+/2.\r\n"
        "PA4 DAC output. 115200 8N1. Enable terminal local echo.\r\n");
}

static bool select_frequency(uint32_t frequency_millihz)
{
    if (frequency_millihz == config.frequency_millihz) { return true; }
    wave_config_t next = config;
    next.frequency_millihz = frequency_millihz;
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
        uint16_t code;
        applied_pot_valid = false;
        last_pot_cycles = DWT->CYCCNT;
        if (pot_ready && pot_read(&code)) {
            if (select_frequency(wave_pot_frequency(code))) {
                applied_pot_code = code;
                applied_pot_valid = true;
            } else { uart_puts("\r\nERR timer configuration\r\n"); }
        } else if (pot_ready) {
            uart_puts("\r\nERR potentiometer ADC read failed\r\n");
            pot_fault_reported = true;
        }
    } else {
        if (!select_frequency(uart_frequency_millihz)) {
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
    uint16_t code;
    if (!pot_read(&code)) {
        applied_pot_valid = false;
        if (!pot_fault_reported) {
            uart_puts("\r\nERR potentiometer ADC read failed; manual mode retained\r\n> ");
            pot_fault_reported = true;
        }
        return;
    }
    pot_fault_reported = false;
    const uint16_t difference = (code > applied_pot_code) ?
                                (code - applied_pot_code) : (applied_pot_code - code);
    if ((!applied_pot_valid || difference >= POT_CHANGE_CODES) &&
        select_frequency(wave_pot_frequency(code))) {
        applied_pot_code = code;
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

static void execute(char *line)
{
    /* Parse into a copy so bad input cannot change the active waveform. */
    const command_t command = command_parse(line, &config);
    if (control_mode == CONTROL_MANUAL &&
        (command.kind == COMMAND_FREQUENCY || command.kind == COMMAND_WAVE)) {
        uart_puts("ERR frequency is set by PA0; connect PA1 to GND for UART mode.\r\n");
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
    uart_puts("Output stopped. Press B1 to start; PA1 open/VDD=manual, PA1 to GND=UART.\r\n");
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
        if (button_level_changed(&mode_switch, (GPIOA->IDR & GPIO_IDR_ID1) != 0U)) {
            set_control_mode(mode_switch.stable ? CONTROL_MANUAL : CONTROL_UART, true);
        }
        if (button_pressed_event(&shape_button, (GPIOC->IDR & GPIO_IDR_ID13) != 0U)) {
            handle_button_press();
        }
        poll_pot();
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
