################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../source/LA_AD9959_drv.c \
../source/LA_ADL5961_drv.c \
../source/LA_DATA_TUNING.c \
../source/LA_DRV8912_drv.c \
../source/LA_IS25LP064A_drv.c \
../source/LA_PIT_drv.c \
../source/LA_SYSTIMER_drv.c \
../source/LA_VNA_ADC_drv.c \
../source/MK64F12_Project.c \
../source/SPIS_drv.c \
../source/clocks.c \
../source/semihost_hardfault.c 

C_DEPS += \
./source/LA_AD9959_drv.d \
./source/LA_ADL5961_drv.d \
./source/LA_DATA_TUNING.d \
./source/LA_DRV8912_drv.d \
./source/LA_IS25LP064A_drv.d \
./source/LA_PIT_drv.d \
./source/LA_SYSTIMER_drv.d \
./source/LA_VNA_ADC_drv.d \
./source/MK64F12_Project.d \
./source/SPIS_drv.d \
./source/clocks.d \
./source/semihost_hardfault.d 

OBJS += \
./source/LA_AD9959_drv.o \
./source/LA_ADL5961_drv.o \
./source/LA_DATA_TUNING.o \
./source/LA_DRV8912_drv.o \
./source/LA_IS25LP064A_drv.o \
./source/LA_PIT_drv.o \
./source/LA_SYSTIMER_drv.o \
./source/LA_VNA_ADC_drv.o \
./source/MK64F12_Project.o \
./source/SPIS_drv.o \
./source/clocks.o \
./source/semihost_hardfault.o 


# Each subdirectory must supply rules for building sources it contributes
source/%.o: ../source/%.c source/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -D__REDLIB__ -DCPU_MK64FN1M0VLL12 -DCPU_MK64FN1M0VLL12_cm4 -DSDK_OS_BAREMETAL -DSDK_DEBUGCONSOLE=1 -DCR_INTEGER_PRINTF -DPRINTF_FLOAT_ENABLE=0 -DSERIAL_PORT_TYPE_UART=1 -D__MCUXPRESSO -D__USE_CMSIS -DDEBUG -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\board" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\source" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\drivers" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\device" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\CMSIS" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\utilities" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\component\serial_manager" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\component\uart" -I"C:\PietroWorks\LoopAntenna\MK64F12_Project\component\lists" -O0 -fno-common -g3 -Wall -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fmerge-constants -fmacro-prefix-map="$(<D)/"= -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean-source

clean-source:
	-$(RM) ./source/LA_AD9959_drv.d ./source/LA_AD9959_drv.o ./source/LA_ADL5961_drv.d ./source/LA_ADL5961_drv.o ./source/LA_DATA_TUNING.d ./source/LA_DATA_TUNING.o ./source/LA_DRV8912_drv.d ./source/LA_DRV8912_drv.o ./source/LA_IS25LP064A_drv.d ./source/LA_IS25LP064A_drv.o ./source/LA_PIT_drv.d ./source/LA_PIT_drv.o ./source/LA_SYSTIMER_drv.d ./source/LA_SYSTIMER_drv.o ./source/LA_VNA_ADC_drv.d ./source/LA_VNA_ADC_drv.o ./source/MK64F12_Project.d ./source/MK64F12_Project.o ./source/SPIS_drv.d ./source/SPIS_drv.o ./source/clocks.d ./source/clocks.o ./source/semihost_hardfault.d ./source/semihost_hardfault.o

.PHONY: clean-source

