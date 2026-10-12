#include "board.h"
#include "command.h"
#include "output.h"
#include "uart.h"

static wave_config_t config = {WAVE_SINE, 120000U, 80U};
static wave_timing_t timing;

static void print_status(void)
{
    uart_puts(output_running() ? "RUN " : "STOP ");
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
        "Amplitude is % of VREF+ peak-to-peak, centered at VREF+/2.\r\n"
        "PA4=A2 output. 115200 8N1. Enable terminal local echo.\r\n");
}

static void execute(char *line)
{
    const command_t command = command_parse(line, &config);
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
        const bool start = command.kind == COMMAND_WAVE || output_running();
        config = command.config;
        timing = next_timing;
        if (start && !output_start(&config, &timing)) {
            uart_puts("ERR timer configuration\r\n"); return;
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
    uart_init();
    uart_puts("\r\nNUCLEO-G474RE CMSIS function generator\r\n");
    if (!output_init()) {
        uart_puts("ERR DAC did not become ready. Reset the board.\r\n");
        while (1) { __WFI(); }
    }
    (void)wave_timing(SystemCoreClock, config.frequency_millihz, &timing);
    uart_puts("Output stopped. Type sin 120 to start; help for commands.\r\n");
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
