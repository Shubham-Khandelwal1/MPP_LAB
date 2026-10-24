# Nucleo G474RE UART function generator

CMSIS register-level firmware for the STM32G474RET6 on the NUCLEO-G474RE. With PA1 connected to GND, send `sin 120` over the board's USB virtual COM port to generate a 120 Hz sine wave on its internal DAC. Sine, square, triangle, rising sawtooth and falling sawtooth share one analog output. Frequency can come from UART or a potentiometer; amplitude is set through UART.

LCD, seven-segment display, keypad, rotary encoder and watchdog controls are omitted. Peripheral initialization, interrupts and data transfers use CMSIS device registers and Cortex-M intrinsics; neither HAL nor LL drivers are built. Standard C functions are used for string comparisons and calculating the sine table.

## Connect the board

| Purpose | Connection |
| --- | --- |
| PC serial connection and programming | USB cable to ST-LINK connector CN1 |
| Waveform output | PA4 / DAC1_OUT1, CN7 pin 32 |
| Waveform select button | B1 USER on PC13 with the board's default SB16/SB21 routing |
| Potentiometer outer terminal, either side | VREF+ at CN10 pin 7 (also CN5 pin 8) |
| Potentiometer center terminal, wiper | PA0 / ADC1 channel 1 at CN7 pin 28 |
| Potentiometer remaining outer terminal | GND at CN7 pin 8 (also CN6 pin 6 or 7) |
| UART mode switch | Latching switch between PA1 at CN7 pin 30 and GND at CN7 pin 8 |
| Oscilloscope ground | Board GND, such as CN7 pin 8 |
| MCU UART transmit | PA2, LPUART1_TX, alternate function 12 |
| MCU UART receive | PA3, LPUART1_RX, alternate function 12 |

The default ST-LINK serial routing uses PA2/PA3: SB17 and SB23 closed; SB18, SB22, SB12 and SB20 open. Use the USB virtual COM port for this firmware. These connections are documented in [ST UM2505, sections 7.6.5 and 8](https://www.st.com/resource/en/user_manual/dm00556337-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf). PA4's DAC function and UART alternate functions are in the [STM32G474 datasheet](https://www.st.com/resource/en/datasheet/stm32g474re.pdf).

Use a roughly 10 kΩ potentiometer as a voltage divider. Swapping its two outer terminals reverses the knob direction. The ADC uses VREF+ as its full-scale reference, so supplying the potentiometer from VREF+ gives the full range. Do not connect it to 5 V. PA1 has an internal pull-up: with the switch open or disconnected, PA1 reads high and manual potentiometer control is selected. PA1 connected to VDD also selects manual control. Closing the switch to GND pulls PA1 low and selects UART control. The board pin assignments and VREF+ selection are documented in [ST UM2505, sections 7.6.3 and 8](https://www.st.com/resource/en/user_manual/dm00556337-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf).

Use a high-impedance oscilloscope input, preferably a 10x probe. The DAC buffer is intended for light loads; do not use a 50-ohm scope termination. Buffered output cannot accurately reach either supply rail. See the DAC electrical characteristics in the [datasheet](https://www.st.com/resource/en/datasheet/stm32g474re.pdf).

## Build and run

In STM32CubeIDE, refresh the MPP project, select Debug, then use **Project → Clean** and **Build Project**. Run/debug using the existing `MPP Debug.launch`. Press Resume if the debugger stops at `main`.

The project targets **STM32G474RETx**, with 512 KB flash and a 96 KB SRAM linker region. The additional CCM RAM is unused so DMA buffers stay in accessible SRAM. The firmware uses CMSIS headers and direct register access throughout; HAL and LL are not built.

Alternatively, from this directory:

```sh
make -j4
make test
```

The Makefile finds the installed CubeIDE compiler under `/opt/st`, or uses `arm-none-eabi-gcc` from PATH. Set `TOOLCHAIN_BIN=/path/to/bin` when needed. It produces `build/MPP.elf`, `build/MPP.bin` and `build/MPP.hex`. CubeIDE produces `Debug/MPP.elf` or `Release/MPP.elf`. Flash an ELF/HEX using ST-LINK; a raw BIN has flash address `0x08000000`.

Open the ST-LINK virtual COM port at **115200 baud, 8 data bits, no parity, 1 stop bit, no flow control**. On Linux it is usually `/dev/ttyACM0`; select the actual enumerated port. Enable terminal **local echo** if you want to see what you type. Every command must end with a CR or LF character; CRLF also works. In TinySerial, add an actual new line after the command in the Send box before clicking **Send**. Its **Display Send** option shows what you sent, even if no command has run. If you already sent several commands without a line ending, send a new line to clear that buffered line, then resend one command with its own new line.

Output starts stopped in the mode selected by PA1. With the switch open or PA1 at VDD, manual mode is selected and the potentiometer sets the initial frequency. With PA1 connected to GND, UART mode starts with sine / 120 Hz / 80% amplitude selected. Reset the board after opening the terminal to see the startup banner, or send `help` at any time.

Press **B1 USER** to start the selected waveform. Each later press while running advances through sine, square, triangle, rising sawtooth and falling sawtooth, then repeats. The button keeps the selected frequency and amplitude. After `stop`, the next press restarts the selected shape. Presses are debounced for 20 ms; holding the button changes shape only once. The default board connection is PC13 (SB16 on, SB21 off), as described in [ST UM2505, section 7.6.2](https://www.st.com/resource/en/user_manual/um2505-stm32g4-nucleo64-boards-mb1367-stmicroelectronics.pdf).

Leave the **PA1 switch open** (or connect PA1 to VDD) for manual frequency control, or close it to **GND** for UART frequency control. In manual mode, the PA0 potentiometer sets 10 Hz at one end to 10 kHz at the other, spread across three ranges (10–100 Hz, 100 Hz–1 kHz, and 1–10 kHz) for useful adjustment at lower frequencies. UART still accepts 0.1–10000 Hz. The firmware samples the knob every 50 ms and ignores tiny ADC changes to avoid needless waveform restarts. The PA1 level is debounced for 20 ms. Pulling PA1 low restores the last UART frequency; changing modes retains the selected waveform, amplitude, and RUN/STOP state. B1 works in either mode. In manual mode, `freq` and waveform commands are rejected; `amp`, `start`, `stop`, `status`, and `help` remain available. `status` reports the selected mode. An ADC failure does not select UART mode while PA1 remains high.

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
| `Core/Src/main.c` / `Core/Inc/main.h` | Clock setup, button and ADC handling, runtime hooks, command loop and status replies |
| `Core/Src/uart.c` / `Core/Inc/uart.h` | LPUART1, interrupt receive buffer and command validation |
| `Core/Src/waveform.c` / `Core/Inc/waveform.h` | Waveform tables, timer calculations, DAC1, TIM6, DMA and output error interrupts |
| `Core/Src/system_stm32g4xx.c` | Unmodified ST CMSIS system initialization and clock calculation |
| `Core/Startup/startup_stm32g474retx.s` | Cortex-M reset sequence and interrupt vector table |

Application implementation is organized in the three `.c` files above. The CMSIS system source and startup assembly remain separate because they provide the device runtime. The hardware sections of `uart.c` and `waveform.c` are excluded from the native host test build; they are included in the STM32 build.

## Verification

The command-line firmware build and CubeIDE Debug and Release builds pass. Host tests cover command parsing, malformed values, all supported frequency settings at 144 MHz and the 16 MHz fallback, the full ADC-code-to-frequency mapping, and every waveform across table sizes and amplitude settings. The worst nominal timer rounding error at 144 MHz is below 0.087%. ELF inspection confirms the application interrupt vectors, SRAM placement of the DMA buffer, and absence of HAL/LL symbols.

This firmware has **not been flashed or verified on a physical board** in this session. For the first hardware check, close the PA1-to-GND switch, send `sin 120`, probe PA4 against GND, and expect a period near 8.333 ms. Then press B1 to cycle shapes, open the PA1 switch to enter manual mode, and turn the PA0 potentiometer to check frequency changes. Close the PA1-to-GND switch again and confirm the UART frequency returns.
