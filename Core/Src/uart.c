#include "uart.h"
#include "board.h"

#define RX_SIZE 512U
#define RX_MASK (RX_SIZE - 1U)
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
