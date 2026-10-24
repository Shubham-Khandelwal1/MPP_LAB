#include "uart.h"
#include <string.h>

/* The parser below also builds on the host for its unit tests. */
#ifdef STM32G474xx
#include "main.h"

#define RX_SIZE 512U
#define RX_MASK (RX_SIZE - 1U)
/* IRQ writes the ring buffer; the command loop consumes it outside the IRQ. */
static volatile uint8_t rx_buffer[RX_SIZE];
static volatile uint32_t rx_head, rx_tail;
static volatile uint32_t rx_fault;

void uart_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB1ENR2 |= RCC_APB1ENR2_LPUART1EN;
    (void)RCC->APB1ENR2;
    RCC->CCIPR &= ~RCC_CCIPR_LPUART1SEL; /* PCLK1 = HCLK, APB divider /1. */
    GPIOA->MODER = (GPIOA->MODER & ~((3U << 4) | (3U << 6))) | (2U << 4) | (2U << 6);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((15U << 8) | (15U << 12))) | (12U << 8) | (12U << 12);
    GPIOA->OTYPER &= ~((1U << 2) | (1U << 3));
    GPIOA->OSPEEDR = (GPIOA->OSPEEDR & ~((3U << 4) | (3U << 6))) | (1U << 4) | (1U << 6);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3U << 4) | (3U << 6))) | (1U << 6);
    LPUART1->CR1 = 0U;
    LPUART1->CR2 = 0U;
    LPUART1->CR3 = USART_CR3_EIE;
    LPUART1->PRESC = 0U;
    LPUART1->BRR = (uint32_t)(((uint64_t)SystemCoreClock * 256U + 57600U) / 115200U);
    LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    LPUART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                    USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE;
    NVIC_SetPriority(LPUART1_IRQn, 2U);
    NVIC_EnableIRQ(LPUART1_IRQn);
}

void LPUART1_IRQHandler(void)
{
    const uint32_t status = LPUART1->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) {
        LPUART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
        rx_fault = 1U;
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        const uint8_t byte = (uint8_t)LPUART1->RDR;
        const uint32_t next = (rx_head + 1U) & RX_MASK;
        if (next == rx_tail) { rx_fault = 1U; }
        if (!rx_fault) {
            rx_buffer[rx_head] = byte;
            __DMB();
            rx_head = next;
        }
    }
}

int uart_getc(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int result = -1;
    if (rx_fault) {
        rx_tail = rx_head;
        rx_fault = 0U;
        result = -2;
    } else if (rx_tail != rx_head) {
        result = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1U) & RX_MASK;
    }
    __set_PRIMASK(primask);
    return result;
}

void uart_putc(char character)
{
    while (!(LPUART1->ISR & USART_ISR_TXE_TXFNF)) { __NOP(); }
    LPUART1->TDR = (uint8_t)character;
}
void uart_puts(const char *text) { while (*text) { uart_putc(*text++); } }
void uart_uint(uint32_t value)
{
    char digits[10];
    unsigned count = 0U;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while (value);
    while (count) { uart_putc(digits[--count]); }
}
void uart_millihz(uint32_t value)
{
    uart_uint(value / 1000U);
    uart_putc('.');
    uart_putc((char)('0' + (value / 100U) % 10U));
    uart_putc((char)('0' + (value / 10U) % 10U));
    uart_putc((char)('0' + value % 10U));
}

#endif

/* UART command grammar and validation. */
static bool parse_millihz(const char *text, uint32_t *value)
{
    uint32_t integer = 0U, fraction = 0U, digits = 0U;
    if (*text < '0' || *text > '9') { return false; }
    while (*text >= '0' && *text <= '9') {
        integer = integer * 10U + (uint32_t)(*text++ - '0');
        if (integer > 10000U) { return false; }
    }
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 3U) { return false; }
            fraction = fraction * 10U + (uint32_t)(*text++ - '0');
        }
        if (digits == 0U) { return false; }
    }
    if (*text != '\0') { return false; }
    while (digits++ < 3U) { fraction *= 10U; }
    *value = integer * 1000U + fraction;
    return *value >= WAVE_MIN_MILLIHZ && *value <= WAVE_MAX_MILLIHZ;
}

static bool parse_percent(const char *text, uint32_t *value)
{
    uint32_t result = 0U;
    if (*text == '\0') { return false; }
    while (*text >= '0' && *text <= '9') {
        result = result * 10U + (uint32_t)(*text++ - '0');
        if (result > 100U) { return false; }
    }
    *value = result;
    return *text == '\0';
}

command_t command_parse(char *line, const wave_config_t *current)
{
    command_t result = {COMMAND_ERROR, *current, "Unknown command; type help."};
    char *args[4];
    unsigned argc = 0U;
    char *p = line;
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') { ++p; }
        if (*p == '\0') { break; }
        if (argc == 4U) { return result; }
        args[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            if (*p >= 'A' && *p <= 'Z') { *p += 'a' - 'A'; }
            ++p;
        }
        if (*p != '\0') { *p++ = '\0'; }
    }
    if (argc == 0U) { result.kind = COMMAND_EMPTY; return result; }
    if (argc == 1U) {
        if (!strcmp(args[0], "help") || !strcmp(args[0], "?")) { result.kind = COMMAND_HELP; }
        else if (!strcmp(args[0], "status")) { result.kind = COMMAND_STATUS; }
        else if (!strcmp(args[0], "start") || !strcmp(args[0], "on")) { result.kind = COMMAND_START; }
        else if (!strcmp(args[0], "stop") || !strcmp(args[0], "off")) { result.kind = COMMAND_STOP; }
        return result;
    }
    if (!strcmp(args[0], "amp")) {
        if (argc == 2U && parse_percent(args[1], &result.config.amplitude_percent)) {
            result.kind = COMMAND_AMPLITUDE;
        } else { result.error = "Use amp <0..100 integer percent of VREF+ peak-to-peak>."; }
        return result;
    }
    if (!strcmp(args[0], "freq")) {
        if (argc == 2U && parse_millihz(args[1], &result.config.frequency_millihz)) {
            result.kind = COMMAND_FREQUENCY;
        } else { result.error = "Use freq <0.1..10000 Hz, at most 3 decimal places>."; }
        return result;
    }
    wave_shape_t shape;
    if (!strcmp(args[0], "sin") || !strcmp(args[0], "sine")) { shape = WAVE_SINE; }
    else if (!strcmp(args[0], "square") || !strcmp(args[0], "sq")) { shape = WAVE_SQUARE; }
    else if (!strcmp(args[0], "triangle") || !strcmp(args[0], "tri")) { shape = WAVE_TRIANGLE; }
    else if (!strcmp(args[0], "saw") || !strcmp(args[0], "sawtooth") || !strcmp(args[0], "sawup")) { shape = WAVE_SAW_UP; }
    else if (!strcmp(args[0], "sawdown")) { shape = WAVE_SAW_DOWN; }
    else { return result; }

    if (argc > 3U || !parse_millihz(args[1], &result.config.frequency_millihz)) {
        result.error = "Use <wave> <0.1..10000 Hz> [amplitude percent].";
        return result;
    }
    if (argc == 3U && !parse_percent(args[2], &result.config.amplitude_percent)) {
        result.error = "Amplitude must be an integer from 0 to 100 percent.";
        return result;
    }
    result.config.shape = shape;
    result.kind = COMMAND_WAVE;
    return result;
}
