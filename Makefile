# CMSIS-only build. CubeIDE also builds Core/ using .cproject.
TOOLCHAIN_BIN ?= $(firstword $(wildcard /opt/st/stm32cubeide_*/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin))
CROSS := $(if $(TOOLCHAIN_BIN),$(TOOLCHAIN_BIN)/)arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
HOST_CC ?= cc
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/MPP
CPU := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CPPFLAGS := -DSTM32G474xx -ICore/Inc -IDrivers/CMSIS/Include -IDrivers/CMSIS/Device/ST/STM32G4xx/Include
CFLAGS := $(CPU) -std=c11 -O2 -g3 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS := $(CPU) -nostartfiles -TSTM32G474RETX_FLASH.ld --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections,-Map=$(TARGET).map -Wl,--print-memory-usage
C_SOURCES := $(wildcard Core/Src/*.c)
ASM_SOURCE := Core/Startup/startup_stm32g474retx.s
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SOURCES)) $(BUILD_DIR)/$(ASM_SOURCE:.s=.o)

.PHONY: all test clean
all: $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(TARGET).elf: $(OBJECTS) STM32G474RETX_FLASH.ld
	$(CC) $(OBJECTS) $(LDFLAGS) -Wl,--start-group -lc -lm -Wl,--end-group -o $@
	$(SIZE) $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(@D)
	$(CC) $(CPU) -g3 -c $< -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

test: $(BUILD_DIR)/test_waveform
	./$(BUILD_DIR)/test_waveform

$(BUILD_DIR)/test_waveform: tests/test_waveform.c Core/Src/waveform.c Core/Src/command.c Core/Inc/waveform.h Core/Inc/command.h
	@mkdir -p $(@D)
	$(HOST_CC) -std=c11 -O2 -g -Wall -Wextra -Werror -ICore/Inc tests/test_waveform.c Core/Src/waveform.c Core/Src/command.c -lm -o $@

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) $(TARGET).elf $(TARGET).bin $(TARGET).hex $(TARGET).map $(BUILD_DIR)/test_waveform

-include $(OBJECTS:.o=.d)
