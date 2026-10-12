################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/board.c \
../Core/Src/command.c \
../Core/Src/main.c \
../Core/Src/output.c \
../Core/Src/runtime.c \
../Core/Src/system_stm32g4xx.c \
../Core/Src/uart.c \
../Core/Src/waveform.c 

OBJS += \
./Core/Src/board.o \
./Core/Src/command.o \
./Core/Src/main.o \
./Core/Src/output.o \
./Core/Src/runtime.o \
./Core/Src/system_stm32g4xx.o \
./Core/Src/uart.o \
./Core/Src/waveform.o 

C_DEPS += \
./Core/Src/board.d \
./Core/Src/command.d \
./Core/Src/main.d \
./Core/Src/output.d \
./Core/Src/runtime.d \
./Core/Src/system_stm32g4xx.d \
./Core/Src/uart.d \
./Core/Src/waveform.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DSTM32G474xx -c -I../Core/Inc -I../Drivers/CMSIS/Device/ST/STM32G4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/board.cyclo ./Core/Src/board.d ./Core/Src/board.o ./Core/Src/board.su ./Core/Src/command.cyclo ./Core/Src/command.d ./Core/Src/command.o ./Core/Src/command.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/output.cyclo ./Core/Src/output.d ./Core/Src/output.o ./Core/Src/output.su ./Core/Src/runtime.cyclo ./Core/Src/runtime.d ./Core/Src/runtime.o ./Core/Src/runtime.su ./Core/Src/system_stm32g4xx.cyclo ./Core/Src/system_stm32g4xx.d ./Core/Src/system_stm32g4xx.o ./Core/Src/system_stm32g4xx.su ./Core/Src/uart.cyclo ./Core/Src/uart.d ./Core/Src/uart.o ./Core/Src/uart.su ./Core/Src/waveform.cyclo ./Core/Src/waveform.d ./Core/Src/waveform.o ./Core/Src/waveform.su

.PHONY: clean-Core-2f-Src

