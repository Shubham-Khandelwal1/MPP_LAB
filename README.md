# Nucleo G474RE UART function generator

CMSIS register-level firmware for the STM32G474RET6 on the NUCLEO-G474RE. Send `sin 120` over the board's USB virtual COM port to generate a 120 Hz sine wave on its internal DAC. Sine, square, triangle, rising sawtooth and falling sawtooth share one analog output. Frequency and amplitude are controlled through UART.

LCD, seven-segment display, keypad, rotary encoder, watchdog and ADC controls are omitted. Peripheral initialization, interrupts and data transfers use CMSIS device registers and Cortex-M intrinsics; neither HAL nor LL drivers are built. Standard C functions are used for string comparisons and calculating the sine table.

## Connect the board

| Purpose | Connection |
| --- | --- |
| PC serial connection and programming | USB cable to ST-LINK connector CN1 |
| Waveform output | PA4 / DAC1_OUT1, Arduino A2 at CN8 pin 3; also CN7 pin 32 |
| Oscilloscope ground | Board GND, such as CN6 pin 6 or 7 |
| MCU UART transmit | PA2, LPUART1_TX, alternate function 12 |
| MCU UART receive | PA3, LPUART1_RX, alternate function 12 |

The default ST-LINK serial routing uses PA2/PA3: SB17 and SB23 closed; SB18, SB22, SB12 and SB20 open. Arduino D0/D1 route to PC5/PC4 by default, so use the USB virtual COM port for this firmware. These connections are documented in [ST UM2505, sections 7.6.5 and 8](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf). PA4's DAC function and UART alternate functions are in the [STM32G474 datasheet](https://www.st.com/resource/en/datasheet/stm32g474re.pdf).

Use a high-impedance oscilloscope input, preferably a 10x probe. The DAC buffer is intended for light loads; do not use a 50-ohm scope termination. Buffered output cannot accurately reach either supply rail. See the DAC electrical characteristics in the [datasheet](https://www.st.com/resource/en/datasheet/stm32g474re.pdf).

## Build and run

In STM32CubeIDE, refresh the MPP project, select Debug, then use **Project → Clean** and **Build Project**. Run/debug using the existing `MPP Debug.launch`. Press Resume if the debugger stops at `main`.

The project now targets **STM32G474RETx**, with 512 KB flash and a 96 KB SRAM linker region. The additional CCM RAM is unused so DMA buffers stay in accessible SRAM. The old CE-package linker scripts, CubeMX generator metadata, HAL application files and HAL/LL driver directory have been removed. The original project is archived at `/tmp/MPP-before-cmsis.tar.gz` on this machine.

Alternatively, from this directory:

```sh
make -j4
make test
```

The Makefile finds the installed CubeIDE compiler under `/opt/st`, or uses `arm-none-eabi-gcc` from PATH. Set `TOOLCHAIN_BIN=/path/to/bin` when needed. It produces `build/MPP.elf`, `build/MPP.bin` and `build/MPP.hex`. CubeIDE produces `Debug/MPP.elf` or `Release/MPP.elf`. Flash an ELF/HEX using ST-LINK; a raw BIN has flash address `0x08000000`.

Open the ST-LINK virtual COM port at **115200 baud, 8 data bits, no parity, 1 stop bit, no flow control**. On Linux it is usually `/dev/ttyACM0`; select the actual enumerated port. Enable terminal **local echo** if you want to see what you type. Press Enter after each command. CR, LF and CRLF are accepted.

Output starts stopped, with sine / 120 Hz / 80% amplitude selected. Reset the board after opening the terminal to see the startup banner, or send `help` at any time.

## UART commands

| Command | Effect |
| --- | --- |
| `sin 120` | Start a 120 Hz sine wave |
| `square 1000` | Start a 1 kHz square wave with 50% duty cycle |
| `triangle 50` | Start a 50 Hz triangle wave |
| `saw 200` | Start a 200 Hz rising sawtooth |
| `sawdown 200` | Start a 200 Hz falling sawtooth |
| `sin 123.456 50` | Set frequency and amplitude together, then start |
| `freq 500` | Change frequency, retaining waveform and RUN/STOP state |
| `amp 80` | Set amplitude to 80% of VREF+ peak-to-peak, retaining RUN/STOP state |
| `stop` | Stop timing and write DAC code zero; output stays buffered |
| `start` | Restart the selected waveform from its initial phase |
| `status` | Report configuration, nominal timer frequency, sample count and clock source |
| `help` | Print command help |

Aliases: `sine`, `sq`, `tri`, `sawtooth`, `sawup`, `on`, `off` and `?`. Commands are case-insensitive. Write bare numbers without units or percent signs. A waveform command without an amplitude retains the current amplitude.

Frequency accepts **0.1–10000 Hz**, with up to three decimal places. Amplitude accepts an integer **0–100**. Invalid commands leave the active configuration unchanged. Lines longer than 95 characters are discarded; receive errors/overflow also discard the damaged command. After a stream-error message, press Enter and resend. For scripts, wait for the `> ` prompt between commands.

## Amplitude and output voltage

All waveforms have a fixed DC center of approximately VREF+/2. `amp 80` means approximately `0.8 × VREF+` volts peak-to-peak, with low/high levels near `0.1 × VREF+` and `0.9 × VREF+`. `amp 0` produces a constant midpoint voltage while running. The DAC cannot produce negative voltages directly.

JP8 selects the board reference or VDD; the board reference can be 3.25 V or 2.5 V depending on its fitted components. Check or measure VREF+ when converting the percentage to volts. [ST UM2505, section 7.6.3](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf)

At VREF+=3.25 V, the default 80% setting is approximately **2.60 Vpp centered on 1.625 V**. `amp 100` requests the full digital code span; analog peaks may clip at the buffer limits. `stop` requests zero code, not a precision 0 V source or a high-impedance output.

## Timing and implementation

The normal clock is the board's 24 MHz HSE crystal multiplied to 144 MHz. If the crystal fails to start, the firmware uses HSI16 through the PLL and marks the source as HSI in `status`; absolute accuracy then depends on the internal RC oscillator. A PLL startup failure leaves it at HSI16 and recalculates timing accordingly. The stock crystal routing is described in [ST UM2505, section 7.5.1](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf).

TIM6 update events trigger DAC1 channel 1. The DAC requests the next table entry through DMAMUX1 channel 0, request 6, and circular DMA1 channel 1. Both memory and peripheral DMA transfers use 32-bit words. DAC trigger selection is configured while the channel is disabled, and its high-frequency interface is configured for the bus clock. A sample is preloaded before starting the timer. This follows the DAC sequencing described in [ST RM0440, chapter 22](https://www.st.com/resource/en/reference_manual/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf).

The sample rate is limited to 250 ksample/s. There are 240 samples per period at low frequencies, falling to 24 at 10 kHz. High-frequency sine/triangle output therefore has more visible steps. Square/sawtooth edges are limited by analog settling. Changing waveform, amplitude or frequency briefly stops and restarts the sample stream; phase continuity during reconfiguration is not promised.

```text
sample frequency = timer clock / ((PSC + 1) × (ARR + 1))
output frequency = sample frequency / samples per period
```

At 144 MHz, `sin 120` uses PSC=0, ARR=4999 and 240 samples: 28800 samples/s and exactly 120 Hz by nominal divider arithmetic. Other requests may round. The `timer=` field is calculated from registers, not measured feedback; it excludes oscillator error. UART reception uses an interrupt and a ring buffer, while waveform samples move autonomously in hardware. DMA errors stop output and are reported over UART.

| Source | Responsibility |
| --- | --- |
| `Core/Src/main.c` | Command loop and status replies |
| `Core/Src/board.c` | Clock, power, flash latency and short delays |
| `Core/Src/uart.c` | LPUART1 registers and interrupt receive buffer |
| `Core/Src/output.c` | GPIO, DAC1, TIM6, DMA and error interrupts |
| `Core/Src/waveform.c` | Waveform tables and timer calculations |
| `Core/Src/command.c` | Command and numeric validation |
| `Core/Src/runtime.c` | Minimal C runtime initialization hooks |
| `Core/Src/system_stm32g4xx.c` | ST CMSIS system initialization and clock calculation |

## Verification

The command-line firmware build and CubeIDE Debug and Release builds pass. Host tests cover command parsing, malformed values, all supported frequency settings at 144 MHz and the 16 MHz fallback, and every waveform across table sizes and amplitude settings. The worst nominal timer rounding error at 144 MHz is below 0.087%. ELF inspection confirms the application interrupt vectors, SRAM placement of the DMA buffer, and absence of HAL/LL symbols.

This firmware has **not been flashed or verified on a physical board** in this session. For the first hardware check, send `sin 120`, probe PA4 against GND, and expect a period near 8.333 ms. Then check `square 1000`, `triangle 50`, `saw 200`, `amp 50` and `stop`.
