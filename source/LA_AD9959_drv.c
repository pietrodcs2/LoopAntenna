/*
 * LA_AD9959_drv.c
 *
 *  Created on: 6. okt. 2026
 *      Author: pc
 */

/* ------------------------ I N C L U D E    F I L E S ------------------------ */
#include "LA_AD9959_drv.h"
#include <stdint.h>
#include "fsl_dspi.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "SPIS_drv.h"
#include "LA_config.h"
#include "board.h"

/* ------------------- L O C A L     D E F I N I T I O N S  ------------------- */


#define AD9959_SPI_FRAME_MAX_SIZE    5U
#define AD9959_FTW_BITS 			32U

/* MASTER_RESET is active high. */
#define AD9959_RESET_ASSERTED_LEVEL       1U
#define AD9959_RESET_DEASSERTED_LEVEL     0U

/* Conservative timings for the intended 20–30 MHz reference clock. */
#define AD9959_RESET_PULSE_US            10U
#define AD9959_RESET_RECOVERY_US         10U

/* Maximum value of the 10-bit amplitude scale factor. */
#define AD9959_AMPLITUDE_SCALE_MAX    1023U

/* DDS channel assignments. */
#define AD9959_CHANNEL_LOCAL_OSC  eAD9959_Channel0
#define AD9959_CHANNEL_RF_INP     eAD9959_Channel1

/*
 * Also define AD9959_RESET_GPIO and AD9959_RESET_PIN
 * according to the actual board connections.
 */




/* Channel selection for register accesses. */
typedef enum eChannelEnable_t
{
    eChannel_Disabled = 0,
    eChannel_Enabled  = 1

} eChannelEnable_t;

typedef enum eAD9959_controlBit_t
{
    eAD9959_Control_ReferenceClockPowerDown,
    eAD9959_Control_DacReferencePowerDown,
    eAD9959_Control_SyncClockDisable,
    eAD9959_Control_DacPowerDown,
    eAD9959_Control_DigitalPowerDown

} eAD9959_controlBit_t;

/* Serial I/O modes, named according to the datasheet. */
typedef enum eSerialIoMode_t
{
    /* SCLK and bidirectional SDIO_0; CS is not counted. */
    eSerialIoMode_SingleBit2Wire = 0,

    /* SCLK, SDIO_0 input and SDIO_2 output; CS is not counted. */
    eSerialIoMode_SingleBit3Wire = 1,

    /* Two data bits transferred per serial clock. */
    eSerialIoMode_TwoBit = 2,

    /* Four data bits transferred per serial clock. */
    eSerialIoMode_FourBit = 3

} eSerialIoMode_t;


/* Serial bit order. */
typedef enum eBitOrder_t
{
    eBitOrder_MsbFirst = 0,
    eBitOrder_LsbFirst = 1

} eBitOrder_t;


/* Read/write selection */
typedef enum eReadWrite_t
{
    eReadWrite_Write = 0,
    eReadWrite_Read  = 1
} eReadWrite_t;

typedef enum AD9959_channelOperation_t
{
    eAD9959_ChannelOperation_SingleTone,
    eAD9959_ChannelOperation_Amplitude

} AD9959_channelOperation_t;

/* Channel Select Register, address 0x00. */
typedef union AD9959_CSR_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bit 0: LSB first. */
        eBitOrder_t eBitOrder : 1;

        /* Bits [2:1]: Serial I/O mode select. */
        eSerialIoMode_t eSerialIoMode : 2;

        /* Bit 3: must be zero. */
        uint8_t sp : 1;

        /* Bits [7:4]: channel selection. */
        eChannelEnable_t eChannel0Enable : 1;
        eChannelEnable_t eChannel1Enable : 1;
        eChannelEnable_t eChannel2Enable : 1;
        eChannelEnable_t eChannel3Enable : 1;

    } bits;

} AD9959_CSR_t;


typedef char AD9959_CSR_SizeCheck[
    (sizeof(AD9959_CSR_t) == 1U) ? 1 : -1
];


/* VCO gain control. */
/*  If the value is outside of 4 and 20 (decimal), the PLL is disabled.  */
typedef enum eVcoGainControl_t
{
    eVcoGainControl_LowRange  = 0, /* VCO: 100 to 160 MHz. */
    eVcoGainControl_HighRange = 1  /* VCO: 255 to 500 MHz. */

} eVcoGainControl_t;


/* PLL divider ratio: determines REFCLK multiplication. */
typedef enum ePllDividerRatio_t
{
    ePllDividerRatio_Bypass     = 0,

    ePllDividerRatio_Multiply4  = 4,
    ePllDividerRatio_Multiply5  = 5,
    ePllDividerRatio_Multiply6  = 6,
    ePllDividerRatio_Multiply7  = 7,
    ePllDividerRatio_Multiply8  = 8,
    ePllDividerRatio_Multiply9  = 9,
    ePllDividerRatio_Multiply10 = 10,
    ePllDividerRatio_Multiply11 = 11,
    ePllDividerRatio_Multiply12 = 12,
    ePllDividerRatio_Multiply13 = 13,
    ePllDividerRatio_Multiply14 = 14,
    ePllDividerRatio_Multiply15 = 15,
    ePllDividerRatio_Multiply16 = 16,
    ePllDividerRatio_Multiply17 = 17,
    ePllDividerRatio_Multiply18 = 18,
    ePllDividerRatio_Multiply19 = 19,
    ePllDividerRatio_Multiply20 = 20

} ePllDividerRatio_t;


/* Charge pump control. */
typedef enum eChargePumpControl_t
{
    eChargePumpControl_75uA  = 0,	/* Default */
    eChargePumpControl_100uA = 1,
    eChargePumpControl_125uA = 2,
    eChargePumpControl_150uA = 3

} eChargePumpControl_t;


/*
 * Profile pin configuration.
 * Code interpretation depends on modulation and RU/RD settings.
 */
typedef enum eProfilePinConfiguration_t
{
    eProfilePinConfiguration_Code0 = 0,
    eProfilePinConfiguration_Code1 = 1,
    eProfilePinConfiguration_Code2 = 2,
    eProfilePinConfiguration_Code3 = 3,
    eProfilePinConfiguration_Code4 = 4,
    eProfilePinConfiguration_Code5 = 5,
    eProfilePinConfiguration_Code6 = 6,
    eProfilePinConfiguration_Code7 = 7

} eProfilePinConfiguration_t;


/* Ramp-up/ramp-down control-pin allocation. */
typedef enum eRampUpDown_t
{
    eRampUpDown_Disabled        = 0,
    eRampUpDown_ProfileP2P3     = 1,
    eRampUpDown_ProfileP3       = 2,
    eRampUpDown_Sdio1Sdio2Sdio3 = 3

} eRampUpDown_t;


/* Modulation level: does not enable modulation by itself. */
typedef enum eModulationLevel_t
{
    eModulationLevel_Two     = 0,
    eModulationLevel_Four    = 1,
    eModulationLevel_Eight   = 2,
    eModulationLevel_Sixteen = 3

} eModulationLevel_t;


/* Power-down mode control for the corresponding circuit. */
/*
 * 0 = the clock input circuitry is enabled for operation (default).
 * 1 = the clock input circuitry is disabled and is in a low power dissipation state.
 */
typedef enum ePowerDown_t
{
    ePowerDown_Disabled = 0, /* Circuit active. */
    ePowerDown_Enabled  = 1  /* Circuit powered down. */

} ePowerDown_t;


/* External power-down mode, selected for PWR_DWN_CTL. */
/*
  0 = the external power-down mode is in fast recovery power-down mode (default). In this mode,
	   when the PWR_DWN_CTL input pin is high, the digital logic and the DAC digital logic are
	   powered down. The DAC bias circuitry, PLL, oscillator, and clock input circuitry are not powered
	   down.
  1 = the external power-down mode is in full power-down mode. In this mode, when the
	   PWR_DWN_CTL input pin is high, all functions are powered down. This includes the DAC and PLL,
	   which take a significant amount of time to power up.
*/
typedef enum eExternalPowerDownMode_t
{
    eExternalPowerDownMode_FastRecovery = 0,
    eExternalPowerDownMode_Full         = 1

} eExternalPowerDownMode_t;


/* SYNC_CLK output disable control. */
typedef enum eSyncClkDisable_t
{
    eSyncClkDisable_DeAsserted = 0, /* Output active. */
    eSyncClkDisable_Asserted   = 1  /* Output held low. */

} eSyncClkDisable_t;


/* Manual synchronization feature enable. */
typedef enum eManualSync_t
{
    eManualSync_Disabled = 0,
    eManualSync_Enabled  = 1

} eManualSync_t;


/**
 * @brief Function Register 1, address 0x01.
 *
 * Common configuration register shared by all four DDS channels.
 * Reset value: 0x000000.
 *
 * Bytes are arranged in MSB-first SPI transmission order:
 *
 *     b8[0]: register bits [23:16]
 *     b8[1]: register bits [15:8]
 *     b8[2]: register bits [7:0]
 *
 * Within each byte, bit fields assume allocation from bit 0 upward
 * on the target GCC compiler.
 *
 * Reserved fields must remain zero.
 */
typedef union AD9959_FR1_t
{
    uint8_t b8[3];

    struct __attribute__((packed))
    {
        /* Byte 0: register bits [23:16]. */

        /* Bits [17:16]: Charge pump control. */
        eChargePumpControl_t eChargePumpControl : 2;

        /* Bits [22:18]: PLL divider ratio. */
        ePllDividerRatio_t ePllDividerRatio : 5;

        /* Bit 23: VCO gain control. */
        eVcoGainControl_t eVcoGainControl : 1;


        /* Byte 1: register bits [15:8]. */

        /* Bits [9:8]: Modulation level. */
        eModulationLevel_t eModulationLevel : 2;

        /* Bits [11:10]: Ramp-up/ramp-down. */
        eRampUpDown_t eRampUpDown : 2;

        /* Bits [14:12]: Profile pin configuration. */
        eProfilePinConfiguration_t eProfilePinConfiguration : 3;

        /* Bit 15: Open, keep zero. */
        uint8_t sp0 : 1;


        /* Byte 2: register bits [7:0]. */

        /* Bit 0: Manual software sync. */
        eManualSync_t eManualSoftwareSync : 1;

        /* Bit 1: Manual hardware sync. */
        eManualSync_t eManualHardwareSync : 1;

        /* Bits [3:2]: Open, keep zero. */
        uint8_t sp1 : 2;

        /* Bit 4: DAC reference power-down. */
        ePowerDown_t eDacReferencePowerDown : 1;

        /* Bit 5: SYNC_CLK disable. */
        eSyncClkDisable_t eSyncClkDisable : 1;

        /* Bit 6: External power-down mode. */
        eExternalPowerDownMode_t eExternalPowerDownMode : 1;

        /* Bit 7: Reference clock input power-down. */
        ePowerDown_t eReferenceClockInputPowerDown : 1;

    } bits;

} AD9959_FR1_t;


/* Verify the register occupies exactly three bytes. */
typedef char AD9959_FR1_SizeCheck[
    (sizeof(AD9959_FR1_t) == 3U) ? 1 : -1
];

/* Automatic accumulator clear on each I/O_UPDATE. */
typedef enum eAccumulatorAutoclear_t
{
    eAccumulatorAutoclear_Disabled = 0,
    eAccumulatorAutoclear_Enabled  = 1

} eAccumulatorAutoclear_t;


/* Continuous accumulator clear control. */
typedef enum eAccumulatorClear_t
{
    eAccumulatorClear_DeAsserted = 0,
    eAccumulatorClear_Asserted   = 1

} eAccumulatorClear_t;


/* Automatic synchronization of multiple DDS devices. */
typedef enum eAutoSyncEnable_t
{
    eAutoSync_Disabled = 0,
    eAutoSync_Enabled  = 1

} eAutoSyncEnable_t;


/* Master selection for automatic multidevice synchronization. */
typedef enum eMultideviceSyncMasterEnable_t
{
    eMultideviceSyncMaster_Disabled = 0, /* Slave role. */
    eMultideviceSyncMaster_Enabled  = 1  /* Master role. */

} eMultideviceSyncMasterEnable_t;


/*
 * Latched synchronization status.
 * Reading this status clears it.
 */
typedef enum eMultideviceSyncStatus_t
{
    eMultideviceSyncStatus_NoLossDetected = 0,
    eMultideviceSyncStatus_LossDetected   = 1

} eMultideviceSyncStatus_t;


/* Mask control for the synchronization status indication. */
typedef enum eMultideviceSyncMask_t
{
    eMultideviceSyncMask_Unmasked = 0,
    eMultideviceSyncMask_Masked   = 1

} eMultideviceSyncMask_t;


/*
 * Compensation for SYNC_OUT-to-SYNC_IN propagation delay.
 * Delay ranges are expressed in SYSCLK periods.
 */
typedef enum eSystemClockOffset_t
{
    eSystemClockOffset_Delay0To1 = 0,
    eSystemClockOffset_Delay1To2 = 1,
    eSystemClockOffset_Delay2To3 = 2,
    eSystemClockOffset_Delay3To4 = 3

} eSystemClockOffset_t;


/**
 * @brief Function Register 2, address 0x02.
 *
 * Common control register shared by all four DDS channels.
 * Reset value: 0x0000.
 *
 * Bytes are arranged in MSB-first SPI transmission order:
 *
 *     b8[0]: register bits [15:8]
 *     b8[1]: register bits [7:0]
 *
 * Within each byte, bit fields assume allocation from bit 0 upward
 * on the target GCC compiler.
 *
 * Autoclear controls request a synchronous, one-cycle accumulator
 * clear on each I/O_UPDATE.
 *
 * Clear controls hold the corresponding accumulators at zero
 * while asserted, using their asynchronous clear mechanism.
 *
 * The multidevice synchronization status is a read-only,
 * clear-on-read indication.
 *
 * Reserved fields must remain zero.
 */
typedef union AD9959_FR2_t
{
    uint8_t b8[2];

    struct __attribute__((packed))
    {
        /* Byte 0: register bits [15:8]. */

        /* Bits [11:8]: Open, keep zero. */
        uint8_t sp0 : 4;

        /* Bit 12: All channels clear phase accumulator. */
        eAccumulatorClear_t
            eAllChannelsClearPhaseAccumulator : 1;

        /* Bit 13: All channels autoclear phase accumulator. */
        eAccumulatorAutoclear_t
            eAllChannelsAutoclearPhaseAccumulator : 1;

        /* Bit 14: All channels clear sweep accumulator. */
        eAccumulatorClear_t
            eAllChannelsClearSweepAccumulator : 1;

        /* Bit 15: All channels autoclear sweep accumulator. */
        eAccumulatorAutoclear_t
            eAllChannelsAutoclearSweepAccumulator : 1;


        /* Byte 1: register bits [7:0]. */

        /* Bits [1:0]: System clock offset. */
        eSystemClockOffset_t eSystemClockOffset : 2;

        /* Bits [3:2]: Open, keep zero. */
        uint8_t sp1 : 2;

        /* Bit 4: Multidevice sync mask. */
        eMultideviceSyncMask_t eMultideviceSyncMask : 1;

        /* Bit 5: Multidevice sync status, read-only. */
        eMultideviceSyncStatus_t eMultideviceSyncStatus : 1;

        /* Bit 6: Multidevice sync master enable. */
        eMultideviceSyncMasterEnable_t
            eMultideviceSyncMasterEnable : 1;

        /* Bit 7: Auto sync enable. */
        eAutoSyncEnable_t eAutoSyncEnable : 1;

    } bits;

} AD9959_FR2_t;


/* Verify the register occupies exactly two bytes. */
typedef char AD9959_FR2_SizeCheck[
    (sizeof(AD9959_FR2_t) == 2U) ? 1 : -1
];

/* Amplitude frequency phase (AFP) select. */
typedef enum eAfpSelect_t
{
    eAfpSelect_Disabled  = 0,
    eAfpSelect_Amplitude = 1,
    eAfpSelect_Frequency = 2,
    eAfpSelect_Phase     = 3

} eAfpSelect_t;


/* Linear sweep no-dwell. */
typedef enum eLinearSweepNoDwell_t
{
    eLinearSweepNoDwell_Disabled = 0,
    eLinearSweepNoDwell_Enabled  = 1

} eLinearSweepNoDwell_t;


/* Linear sweep enable. */
typedef enum eLinearSweepEnable_t
{
    eLinearSweep_Disabled = 0,
    eLinearSweep_Enabled  = 1

} eLinearSweepEnable_t;


/* Load sweep ramp rate timer at I/O_UPDATE. */
typedef enum eLoadSrrAtIoUpdate_t
{
    eLoadSrrAtIoUpdate_Disabled = 0,
    eLoadSrrAtIoUpdate_Enabled  = 1

} eLoadSrrAtIoUpdate_t;


/*
 * DAC full-scale current control.
 * Scaling is relative to the full-scale current established by RSET.
 * Note the nonsequential encoding of half and quarter scale.
 */
typedef enum eDacFullScaleCurrentControl_t
{
    eDacFullScaleCurrent_Eighth  = 0,
    eDacFullScaleCurrent_Half    = 1,
    eDacFullScaleCurrent_Quarter = 2,
    eDacFullScaleCurrent_Full    = 3

} eDacFullScaleCurrentControl_t;


/* Matched pipe delays active. */
typedef enum eMatchedPipeDelays_t
{
    eMatchedPipeDelays_Disabled = 0,
    eMatchedPipeDelays_Enabled  = 1

} eMatchedPipeDelays_t;


/* Sine wave output enable. */
typedef enum eSineWaveOutputEnable_t
{
    eSineWaveOutput_Disabled = 0, /* Cosine conversion. */
    eSineWaveOutput_Enabled  = 1  /* Sine conversion. */

} eSineWaveOutputEnable_t;

/**
 * @brief Channel Function Register, address 0x03.
 *
 * Each DDS channel has an independent copy of this register.
 * CSR channel selection determines which copies receive a write.
 * Select exactly one channel for readback.
 *
 * Reset value: 0x000302.
 *
 * Bytes are arranged in MSB-first SPI transmission order:
 *
 *     b8[0]: register bits [23:16]
 *     b8[1]: register bits [15:8]
 *     b8[2]: register bits [7:0]
 *
 * Within each byte, bit fields assume allocation from bit 0 upward
 * on the target GCC compiler.
 *
 * Reserved fields must remain zero.
 */
typedef union AD9959_CFR_t
{
    uint8_t b8[3];

    struct __attribute__((packed))
    {
        /* Byte 0: register bits [23:16]. */

        /* Bits [21:16]: Open, keep zero. */
        uint8_t sp0 : 6;

        /* Bits [23:22]: Amplitude frequency phase select. */
        eAfpSelect_t eAfpSelect : 2;


        /* Byte 1: register bits [15:8]. */

        /* Bits [9:8]: DAC full-scale current control. */
        eDacFullScaleCurrentControl_t
            eDacFullScaleCurrentControl : 2;

        /* Bit 10: Must be zero. */
        uint8_t sp1 : 1;

        /* Bits [12:11]: Open, keep zero. */
        uint8_t sp2 : 2;

        /* Bit 13: Load SRR at I/O_UPDATE. */
        eLoadSrrAtIoUpdate_t eLoadSrrAtIoUpdate : 1;

        /* Bit 14: Linear sweep enable. */
        eLinearSweepEnable_t eLinearSweepEnable : 1;

        /* Bit 15: Linear sweep no-dwell. */
        eLinearSweepNoDwell_t eLinearSweepNoDwell : 1;


        /* Byte 2: register bits [7:0]. */

        /* Bit 0: Sine wave output enable. */
        eSineWaveOutputEnable_t eSineWaveOutputEnable : 1;

        /* Bit 1: Clear phase accumulator. */
        eAccumulatorClear_t eClearPhaseAccumulator : 1;

        /* Bit 2: Autoclear phase accumulator. */
        eAccumulatorAutoclear_t eAutoclearPhaseAccumulator : 1;

        /* Bit 3: Clear sweep accumulator. */
        eAccumulatorClear_t eClearSweepAccumulator : 1;

        /* Bit 4: Autoclear sweep accumulator. */
        eAccumulatorAutoclear_t eAutoclearSweepAccumulator : 1;

        /* Bit 5: Matched pipe delays active. */
        eMatchedPipeDelays_t eMatchedPipeDelays : 1;

        /* Bit 6: DAC power-down. */
        ePowerDown_t eDacPowerDown : 1;

        /* Bit 7: Digital power-down. */
        ePowerDown_t eDigitalPowerDown : 1;

    } bits;

} AD9959_CFR_t;


/* Verify the register occupies exactly three bytes. */
typedef char AD9959_CFR_SizeCheck[
    (sizeof(AD9959_CFR_t) == 3U) ? 1 : -1
];



/*
 * Channel Frequency Tuning Word 0 — Address 0x04.
 *
 * Bits [31:0] contain the frequency tuning word
 * for the channel selected through CSR.
 */
typedef union AD9959_CFTW0_t
{
    uint8_t b8[4];

    struct __attribute__((packed))
    {
        uint32_t frequencyTuningWord;

    } bits;

} AD9959_CFTW0_t;

typedef char AD9959_CFTW0_SizeCheck[
    (sizeof(AD9959_CFTW0_t) == 4U) ? 1 : -1
];



/*
 * Channel Phase Offset Word 0 — Address 0x05.
 *
 * Wire byte order: most significant byte first.
 * Bits [13:0] contain the phase offset word.
 * Unused bits [15:14] are kept at zero.
 */
typedef union AD9959_CPOW0_t
{
    uint8_t b8[2];

    struct __attribute__((packed))
    {
        /* Byte 0: register bits [15:8]. */
        uint8_t pow13_8 : 6;
        uint8_t sp0     : 2;

        /* Byte 1: register bits [7:0]. */
        uint8_t pow7_0;

    } bits;

} AD9959_CPOW0_t;

typedef char AD9959_CPOW0_SizeCheck[
    (sizeof(AD9959_CPOW0_t) == 2U) ? 1 : -1
];

/* Amplitude increment/decrement step size. */
typedef enum eAmplitudeStepSize_t
{
    eAmplitudeStepSize_1 = 0,
    eAmplitudeStepSize_2 = 1,
    eAmplitudeStepSize_4 = 2,
    eAmplitudeStepSize_8 = 3

} eAmplitudeStepSize_t;


/* Digital amplitude multiplier enable. */
typedef enum eAmplitudeMultiplierEnable_t
{
    eAmplitudeMultiplier_Disabled = 0,
    eAmplitudeMultiplier_Enabled  = 1

} eAmplitudeMultiplierEnable_t;


/* Amplitude ramp-up/ramp-down operating mode. */
typedef enum eRampUpDownMode_t
{
    eRampUpDownMode_Manual    = 0,
    eRampUpDownMode_Automatic = 1

} eRampUpDownMode_t;


/* Amplitude ramp timer reload on I/O_UPDATE. */
typedef enum eLoadArrAtIoUpdate_t
{
    eLoadArrAtIoUpdate_Disabled = 0,
    eLoadArrAtIoUpdate_Enabled  = 1

} eLoadArrAtIoUpdate_t;


/*
 * Amplitude Control Register — Address 0x06.
 *
 * Native register representation for the K64 GCC toolchain.
 * The SPI transfer function must arrange the three bytes
 * in the configured wire byte order.
 */
typedef union AD9959_ACR_t
{
    uint8_t b8[3];

    struct __attribute__((packed))
    {
        /* Bits [9:0]: manual amplitude or automatic ramp limit. */
        uint32_t amplitudeScaleFactor : 10;

        /* Bit 10: reload the amplitude ramp timer on I/O_UPDATE. */
        eLoadArrAtIoUpdate_t eLoadArrAtIoUpdate : 1;

        /* Bit 11: select manual or automatic amplitude control. */
        eRampUpDownMode_t eRampUpDownMode : 1;

        /* Bit 12: enable the digital amplitude multiplier. */
        eAmplitudeMultiplierEnable_t eAmplitudeMultiplierEnable : 1;

        /* Bit 13: unused. Keep at zero. */
        uint32_t sp0 : 1;

        /* Bits [15:14]: automatic amplitude ramp step size. */
        eAmplitudeStepSize_t eAmplitudeStepSize : 2;

        /* Bits [23:16]: automatic amplitude ramp rate value. */
        uint32_t amplitudeRampRate : 8;

    } bits;

} AD9959_ACR_t;


typedef char AD9959_ACR_SizeCheck[
    (sizeof(AD9959_ACR_t) == 3U) ? 1 : -1
];


/*
 * Linear Sweep Ramp Rate — Address 0x07.
 *
 * Each field specifies the interval between successive
 * steps of the corresponding hardware linear sweep ramp.
 *
 * The SPI transfer function must arrange the two bytes
 * in the configured wire byte order.
 */
typedef union AD9959_LSRR_t
{
    uint8_t b8[2];

    struct __attribute__((packed))
    {
        /* Bits [7:0]: rising sweep ramp rate. */
        uint8_t risingSweepRampRate;

        /* Bits [15:8]: falling sweep ramp rate. */
        uint8_t fallingSweepRampRate;

    } bits;

} AD9959_LSRR_t;


typedef char AD9959_LSRR_SizeCheck[
    (sizeof(AD9959_LSRR_t) == 2U) ? 1 : -1
];


/*
 * LSR Rising Delta Word — Address 0x08.
 *
 * Bits [31:0] contain the rising delta tuning word.
 * During hardware linear sweep operation, this value
 * is added to the sweep accumulator at each rising step.
 *
 * Its interpretation depends on the selected sweep
 * parameter: frequency, phase, or amplitude.
 *
 * The SPI transfer function must arrange the four bytes
 * in the configured wire byte order.
 */
typedef union AD9959_RDW_t
{
    uint8_t b8[4];

    struct __attribute__((packed))
    {
        uint32_t risingDeltaWord;

    } bits;

} AD9959_RDW_t;


typedef char AD9959_RDW_SizeCheck[
    (sizeof(AD9959_RDW_t) == 4U) ? 1 : -1
];



/*
 * LSR Falling Delta Word — Address 0x09.
 *
 * Bits [31:0] contain the falling delta tuning word.
 * During hardware linear sweep operation, this unsigned
 * value is subtracted from the sweep accumulator at
 * each falling step.
 *
 * Its interpretation depends on the selected sweep
 * parameter: frequency, phase, or amplitude.
 *
 * The SPI transfer function must arrange the four bytes
 * in the configured wire byte order.
 */
typedef union AD9959_FDW_t
{
    uint8_t b8[4];

    struct __attribute__((packed))
    {
        uint32_t fallingDeltaWord;

    } bits;

} AD9959_FDW_t;


typedef char AD9959_FDW_SizeCheck[
    (sizeof(AD9959_FDW_t) == 4U) ? 1 : -1
];

/*
 * SPI instruction byte.
 *
 * Bits [4:0]: register address.
 * Bits [6:5]: don't care; transmitted as zero.
 * Bit 7: read/write selection.
 */
typedef union AD9959_SPI_INSTRUCTION_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t address : 5;
        uint8_t sp0     : 2;
        eReadWrite_t eReadWrite : 1;

    } bits;

} AD9959_SPI_INSTRUCTION_t;


/*
 * Single-register SPI frame.
 *
 * Only the first frameSize bytes are transferred.
 * Data bytes are stored in wire order, MSB first.
 */
typedef union AD9959_SPI_FRAME_t
{
    uint8_t b8[5];

    struct __attribute__((packed))
    {
        AD9959_SPI_INSTRUCTION_t instruction;
        uint8_t data[4];

    } bytes;

} AD9959_SPI_FRAME_t;


typedef char AD9959_SPI_INSTRUCTION_SizeCheck[
    (sizeof(AD9959_SPI_INSTRUCTION_t) == 1U) ? 1 : -1
];

typedef char AD9959_SPI_FRAME_SizeCheck[
    (sizeof(AD9959_SPI_FRAME_t) == 5U) ? 1 : -1
];


/*
 * Channel profile word.
 *
 * Native K64 byte order, consistent with CFTW0, RDW and FDW.
 * The transfer functions convert it into MSB-first wire order.
 *
 * Frequency uses bits [31:0].
 * Phase uses bits [31:18].
 * Amplitude uses bits [31:22].
 */
typedef union AD9959_CW_t
{
    uint8_t b8[4];

    struct __attribute__((packed))
    {
        uint32_t channelWord;

    } bits;

} AD9959_CW_t;


/*
 * Register image containing common registers and one channel's registers.
 *
 * Comments specify SPI register addresses, not structure byte offsets.
 * Channel-specific access requires the appropriate CSR selection.
 */
typedef struct __attribute__((packed)) AD9959_regs_t
{
    /* Common registers. */
    AD9959_CSR_t   CSR;       /* 0x00 */
    AD9959_FR1_t   FR1;       /* 0x01 */
    AD9959_FR2_t   FR2;       /* 0x02 */

    /* Channel-specific registers. */
    AD9959_CFR_t   CFR;       /* 0x03 */
    AD9959_CFTW0_t CFTW0;     /* 0x04 */
    AD9959_CPOW0_t CPOW0;     /* 0x05 */
    AD9959_ACR_t   ACR;       /* 0x06 */
    AD9959_LSRR_t  LSRR;      /* 0x07 */
    AD9959_RDW_t   RDW;       /* 0x08 */
    AD9959_FDW_t   FDW;       /* 0x09 */

    /* Channel-specific profile registers. */
    AD9959_CW_t CW1;          /* 0x0A */
    AD9959_CW_t CW2;          /* 0x0B */
    AD9959_CW_t CW3;          /* 0x0C */
    AD9959_CW_t CW4;          /* 0x0D */
    AD9959_CW_t CW5;          /* 0x0E */
    AD9959_CW_t CW6;          /* 0x0F */
    AD9959_CW_t CW7;          /* 0x10 */
    AD9959_CW_t CW8;          /* 0x11 */
    AD9959_CW_t CW9;          /* 0x12 */
    AD9959_CW_t CW10;         /* 0x13 */
    AD9959_CW_t CW11;         /* 0x14 */
    AD9959_CW_t CW12;         /* 0x15 */
    AD9959_CW_t CW13;         /* 0x16 */
    AD9959_CW_t CW14;         /* 0x17 */
    AD9959_CW_t CW15;         /* 0x18 */

} AD9959_regs_t;


/* Verify packed register image sizes. */
typedef char AD9959_CW_SizeCheck[
    (sizeof(AD9959_CW_t) == 4U) ? 1 : -1
];

typedef char AD9959_regs_SizeCheck[
    (sizeof(AD9959_regs_t) == 88U) ? 1 : -1
];


typedef struct AD9959_registerEntry_t
{
    uint8_t address;
    size_t offset;

} AD9959_registerEntry_t;

#define AD9959_REGISTER_ENTRY(member, spiAddress) \
    { (spiAddress), offsetof(AD9959_regs_t, member) }


/*
 * Calculated PLL configuration.
 *
 * actualSysclk_Hz is the frequency to use when converting
 * output frequencies into DDS tuning words.
 */
typedef struct AD9959_clockConfig_t
{
    uint32_t crystalFrequency_Hz;
    uint32_t requestedSysclk_Hz;
    uint32_t actualSysclk_Hz;

    uint8_t pllMultiplier;

    eVcoGainControl_t eVcoGainControl;

} AD9959_clockConfig_t;


typedef uint8_t AD9959_SpiFrame_t[ AD9959_SPI_FRAME_MAX_SIZE ];

typedef struct AD9959_transferContext_t
{
    volatile bool pending;
    volatile bool completed;
    volatile status_t status;

    AD9959_SpiFrame_t txData;
    AD9959_SpiFrame_t rxData;

    uint8_t *rxDestination;
    /*
     * Destination for a register read.
     * Data is decoded when the completed result is collected.
     * Must remain valid until AD9959_GetTransferResult() consumes it.
     */
    uint8_t *registerDestination;
    uint8_t registerAddress;
    size_t frameSize;

    dspi_transfer_t transfer;

} AD9959_transferContext_t;

typedef enum eAD9959_registerIndex_t
{
    eAD9959_Register_CSR,
    eAD9959_Register_FR1,
    eAD9959_Register_FR2,
    eAD9959_Register_CFR,
	eAD9959_Register_CFTW0,
	eAD9959_Register_CPOW0,
	eAD9959_Register_ACR,
	eAD9959_Register_LSRR,
	eAD9959_Register_RDW,
	eAD9959_Register_FDW,
	eAD9959_Register_CW1,
	eAD9959_Register_CW2,
	eAD9959_Register_CW3,
	eAD9959_Register_CW4,
	eAD9959_Register_CW5,
	eAD9959_Register_CW6,
	eAD9959_Register_CW7,
	eAD9959_Register_CW8,
	eAD9959_Register_CW9,
	eAD9959_Register_CW10,
	eAD9959_Register_CW11,
	eAD9959_Register_CW12,
	eAD9959_Register_CW13,
	eAD9959_Register_CW14,
	eAD9959_Register_CW15,
	eAD9959_Register_Count

} eAD9959_registerIndex_t;


/* ------------------ G L O B A L           F U N C T I O N S   ---------------- */


// status_t AD9959_SetTwoFrequenciesBlocking( uint32_t frequency0_Hz, uint32_t frequency1_Hz );
status_t AD9959_SetTwoFrequenciesNonBlocking( uint32_t frequency0_Hz, uint32_t frequency1_Hz );
status_t AD9959_ReadRegistersBlocking(AD9959_regs_t *regs);
status_t AD9959_SetClockBlocking(uint32_t crystalFrequency_Hz, uint32_t requestedSysclk_Hz, AD9959_clockConfig_t *config );
status_t AD9959_SetReferenceClockEnabledBlocking( bool enabled );
status_t AD9959_SetDacReferenceEnabledBlocking( bool enabled );
status_t AD9959_SetSyncClockEnabledBlocking( bool enabled );
status_t AD9959_SetDacEnabledBlocking( uint8_t channel, bool enabled );

status_t AD9959_SetDigitalCoreEnabledBlocking( uint8_t channel, bool enabled );
// status_t AD9959_InitBlocking( uint32_t coreClock_Hz );
status_t AD9959_SetSingleToneBlocking( uint8_t channel );
status_t AD9959_SetAmplitudeBlocking( uint8_t channel, uint16_t amplitudeScaleFactor );
status_t AD9959_SetSingleToneBlocking( uint8_t channel );
status_t AD9959_SetAmplitudeBlocking( uint8_t channel, uint16_t amplitudeScaleFactor );

bool AD9959_GetTransferResult( status_t *status );

/* ------------------------- G L O B A L     D A T A   ------------------------ */


/* -------------------------- L O C A L     D A T A   ------------------------- */
// uint8_t frameSize;  /* Instruction byte + register data bytes. */
static AD9959_transferContext_t AD9959_transferContext = { 0 };
/* Last successfully programmed clock configuration. */
static AD9959_clockConfig_t AD9959_clockConfig = { 0 };

/* ------------------ E X T E R N A L      F U N C T I O N S   ---------------- */


/* ------------------ E X T E R N A L      D A T A ---------------------------- */
extern dspi_master_handle_t AD9959_SPI_HANDLE;

/* --------------------- L O C A L     F U N C T I O N S   -------------------- */
/*
 * Execute a complete SPI frame synchronously.
 *
 * rxData may be NULL when received data is not required.
 * Returns when the transfer has completed or failed.
 */
static status_t AD9959_TransferBlocking( const uint8_t *txData, uint8_t *rxData, size_t frameSize );

/*
 * Start a complete SPI frame asynchronously.
 *
 * Returns the transfer submission status.
 * Successful submission does not imply transfer completion.
 *
 * Transfer buffers must remain valid until completion.
 * rxData may be NULL when received data is not required.
 */
static status_t AD9959_TransferNonBlocking( const uint8_t *txData, uint8_t *rxData, size_t frameSize );

/*
 * registerData points to the native register image.
 * Its size is determined by the register address.
 */
static status_t AD9959_WriteRegisterBlocking(uint8_t address, const uint8_t *registerData );
static status_t AD9959_ReadRegisterBlocking(uint8_t address, uint8_t *registerData );
static status_t AD9959_WriteRegisterNonBlocking(uint8_t address, const uint8_t *registerData );
static status_t AD9959_ReadRegisterNonBlocking(uint8_t address, uint8_t *registerData );
static status_t AD9959_CalculateClockConfig(uint32_t crystalFrequency_Hz, uint32_t requestedSysclk_Hz, AD9959_clockConfig_t *config );
static status_t AD9959_MakeWriteFrame(uint8_t address, const uint8_t *registerData, AD9959_SpiFrame_t frame, uint8_t *frameSize);
static void AD9959_SPI_Callback( status_t status, void *userData );
static status_t AD9959_SetControlBitBlocking(eAD9959_controlBit_t control, bool asserted );
static status_t AD9959_CalculateFrequencyWord(uint32_t frequency_Hz, uint32_t sysclk_Hz, uint32_t *ftw );
static status_t AD9959_ConfigureChannelBlocking(uint8_t channel, AD9959_channelOperation_t operation, uint16_t amplitudeScaleFactor );
static status_t AD9959_PowerUpModeBlocking( bool autotest );
static void AD9959_StartupError(status_t status, uint8_t channel, eAD9959_registerIndex_t index, uint8_t byteIndex, uint8_t expected, uint8_t actual, uint8_t mask );
static void AD9959_StartupCompare(uint8_t channel, eAD9959_registerIndex_t index, const uint8_t *actual, const uint8_t *expected, const uint8_t *mask );
static status_t AD9959_StartupAccess(bool write, uint8_t channel, eAD9959_registerIndex_t index, uint8_t *data );
static AD9959_CSR_t AD9959_MakeStartupCsr(uint8_t channel);
static void AD9959_StartupResetMasks(AD9959_regs_t *expected, AD9959_regs_t *mask );


/* ----------------------  I M P L E M E N T A T I O N   ---------------------- */

SPI_busConfig_t AD9959_spiConfig =
{
    .baudrate_Hz = AD9959_SPI_BAUDRATE_HZ,
    .bitsPerFrame = 8U,

    .cpol = kDSPI_ClockPolarityActiveHigh,
    .cpha = kDSPI_ClockPhaseFirstEdge,
    .direction = kDSPI_MsbFirst,

    .pcsToSckDelay_ns = AD9959_SPI_DELAY_NS,
    .lastSckToPcsDelay_ns = AD9959_SPI_DELAY_NS,
    .betweenTransferDelay_ns = AD9959_SPI_DELAY_NS
};



static const AD9959_registerEntry_t AD9959_registerTable[] =
{
	[eAD9959_Register_CSR] =
			AD9959_REGISTER_ENTRY(CSR,   0x00U),
	[eAD9959_Register_FR1] =
			AD9959_REGISTER_ENTRY(FR1,   0x01U),
	[eAD9959_Register_FR2] =
			AD9959_REGISTER_ENTRY(FR2,   0x02U),
    [eAD9959_Register_CFR] =
    		AD9959_REGISTER_ENTRY(CFR,   0x03U),
    // AD9959_REGISTER_ENTRY(CFTW0, 0x04U),
	[eAD9959_Register_CFTW0] =
	    AD9959_REGISTER_ENTRY(CFTW0, 0x04U),
	[ eAD9959_Register_CPOW0 ] =
		AD9959_REGISTER_ENTRY(CPOW0, 0x05U),
    // AD9959_REGISTER_ENTRY(ACR,   0x06U),
	[eAD9959_Register_ACR] =
	    AD9959_REGISTER_ENTRY(ACR, 0x06U),
	[eAD9959_Register_LSRR] =
		AD9959_REGISTER_ENTRY(LSRR,  0x07U),
	[eAD9959_Register_RDW] =
		AD9959_REGISTER_ENTRY(RDW,  0x08U),
	[eAD9959_Register_FDW] =
		AD9959_REGISTER_ENTRY(FDW,  0x09U),
	[ eAD9959_Register_CW1 ] =
		AD9959_REGISTER_ENTRY(CW1,   0x0AU),
	[ eAD9959_Register_CW2 ] =
		AD9959_REGISTER_ENTRY(CW2,   0x0BU),
	[ eAD9959_Register_CW3 ] =
		AD9959_REGISTER_ENTRY(CW3,   0x0CU),
	[ eAD9959_Register_CW4 ] =
		AD9959_REGISTER_ENTRY(CW4,   0x0DU),
	[ eAD9959_Register_CW5 ] =
		AD9959_REGISTER_ENTRY(CW5,   0x0EU),
	[ eAD9959_Register_CW6 ] =
		AD9959_REGISTER_ENTRY(CW6,   0x0FU),
	[ eAD9959_Register_CW7 ] =
		AD9959_REGISTER_ENTRY(CW7,   0x10U),
	[ eAD9959_Register_CW8 ] =
		AD9959_REGISTER_ENTRY(CW8,   0x11U),
	[ eAD9959_Register_CW9 ] =
		AD9959_REGISTER_ENTRY(CW9,   0x12U),
	[ eAD9959_Register_CW10 ] =
		AD9959_REGISTER_ENTRY(CW10,  0x13U),
	[ eAD9959_Register_CW11 ] =
		AD9959_REGISTER_ENTRY(CW11,  0x14U),
	[ eAD9959_Register_CW12 ] =
		AD9959_REGISTER_ENTRY(CW12,  0x15U),
	[ eAD9959_Register_CW13 ] =
		AD9959_REGISTER_ENTRY(CW13,  0x16U),
	[ eAD9959_Register_CW14 ] =
		AD9959_REGISTER_ENTRY(CW14,  0x17U),
	[ eAD9959_Register_CW15 ] =
		AD9959_REGISTER_ENTRY(CW15,  0x18U)
};

/*
SPI_transaction_t spiTransaction =
{
    .base = AD9959_SPI,
    .config = &AD9959_spiConfig,

    .txData = frame,
    .rxData = NULL,
    .dataSize = frameSize,

    .configFlags = AD9959_SPI_TRANSFER_FLAGS
};
*/


/**
 * @brief Handles completion of an asynchronous SPI transfer.
 *
 * Copies received bytes to the caller's buffer, when supplied
 * and when the transfer succeeded.
 *
 * Stores the final status, releases the shared bus, and marks
 * the result as available.
 *
 * Called by the SDK in interrupt context.
 * Does not generate I/O_UPDATE or start another transfer.
 */
/* The shared SPI module releases the bus before this callback. */
static void AD9959_SPI_Callback( status_t status, void *userData )
{
    AD9959_transferContext_t *context =
        (AD9959_transferContext_t *)userData;

    if ((status == kStatus_Success) &&
        (context->rxDestination != NULL))
    {
        for (size_t i = 0U; i < context->frameSize; ++i)
        {
            context->rxDestination[i] = context->rxData[i];
        }
    }

    context->status = status;
    context->completed = true;
}


/**
 * @brief Encodes a frequency tuning word in MSB-first SPI byte order.
 */
static AD9959_CFTW0_t AD9959_MakeCftw0(uint32_t ftw)
{
    AD9959_CFTW0_t reg = { .b8 = { 0U } };

    reg.bits.frequencyTuningWord = ftw;

    return reg;
}

/**
 * @brief Returns the unsigned frequency tuning word.
 */
static uint32_t AD9959_GetCftw0Value(AD9959_CFTW0_t reg)
{
    return reg.bits.frequencyTuningWord;
}


/*
 * Build a phase offset register in MSB-first wire order.
 *
 * Only the least significant 14 bits of phaseOffsetWord
 * are retained. Unused register bits remain zero.
 */
static AD9959_CPOW0_t AD9959_MakeCpow0(uint16_t phaseOffsetWord)
{
	AD9959_CPOW0_t reg = { .b8 = { 0U } };

	reg.bits.pow13_8 = (uint8_t) ((phaseOffsetWord >> 8) & 0x3FU);

	reg.bits.pow7_0 = (uint8_t) phaseOffsetWord;

	return reg;
}


/*
 * Return the register data size in bytes.
 *
 * A return value of zero indicates an unsupported address.
 * The instruction byte is not included.
 */
static uint8_t AD9959_GetRegisterDataSize(uint8_t address)
{
	switch (address) {
		case eAD9959_Register_CSR: /* CSR */
			return 1U;

		case eAD9959_Register_FR2: /* FR2 */
		case eAD9959_Register_CPOW0: /* CPOW0 */
		case eAD9959_Register_LSRR: /* LSRR */
			return 2U;

		case eAD9959_Register_FR1: /* FR1 */
		case eAD9959_Register_CFR: /* CFR */
		case eAD9959_Register_ACR: /* ACR */
			return 3U;

		case eAD9959_Register_CFTW0: /* CFTW0 */
		case eAD9959_Register_RDW:/* RDW */
		case eAD9959_Register_FDW: /* FDW */
			return 4U;

		default:
			/* Channel profile registers CW1 through CW15. */
			if ((address >= 0x0AU) && (address <= 0x18U)) {
				return 4U;
			}

			return 0U;
	}
}


/*
 * Calculate the PLL configuration closest to the requested
 * system clock frequency.
 *
 * Only crystal operation with the PLL enabled is considered.
 * Supported crystal frequencies range from 20 MHz to 30 MHz.
 *
 * Candidates outside the guaranteed VCO frequency ranges
 * are discarded.
 *
 * If two candidates have the same frequency error, the
 * lower system clock frequency is selected.
 *
 * This function performs no hardware access.
 * The output configuration is modified only on success.
 */
static status_t AD9959_CalculateClockConfig( uint32_t crystalFrequency_Hz, uint32_t requestedSysclk_Hz, AD9959_clockConfig_t *config )
{
	AD9959_clockConfig_t candidate = { 0 };
	AD9959_clockConfig_t best = { 0 };

	uint32_t bestError_Hz = UINT32_MAX;

	if ((config == NULL) || (crystalFrequency_Hz < 20000000UL)
			|| (crystalFrequency_Hz > 30000000UL)
			|| (requestedSysclk_Hz < 100000000UL)
			|| (requestedSysclk_Hz > 500000000UL)) {
		return kStatus_InvalidArgument;
	}

	for (uint8_t multiplier = 4U; multiplier <= 20U; ++multiplier) {
		uint32_t frequency_Hz = crystalFrequency_Hz * multiplier;

		uint32_t error_Hz;

		if ((frequency_Hz >= 100000000UL) && (frequency_Hz <= 160000000UL)) {
			candidate.eVcoGainControl = eVcoGainControl_LowRange;
		} else if ((frequency_Hz >= 255000000UL)
				&& (frequency_Hz <= 500000000UL)) {
			candidate.eVcoGainControl = eVcoGainControl_HighRange;
		} else {
			continue;
		}

		error_Hz =
				(frequency_Hz >= requestedSysclk_Hz) ?
						(frequency_Hz - requestedSysclk_Hz) :
						(requestedSysclk_Hz - frequency_Hz);

		if (error_Hz < bestError_Hz) {
			bestError_Hz = error_Hz;

			candidate.crystalFrequency_Hz = crystalFrequency_Hz;

			candidate.requestedSysclk_Hz = requestedSysclk_Hz;

			candidate.actualSysclk_Hz = frequency_Hz;
			candidate.pllMultiplier = multiplier;

			best = candidate;
		}
	}

	if (best.pllMultiplier == 0U) {
		return kStatus_InvalidArgument;
	}

	*config = best;

	return kStatus_Success;
}


/*
 * Build a single-register SPI write frame.
 *
 * address:
 *     Device register address. Determines the data size.
 *
 * registerData:
 *     Register image in the addressed register type's byte order.
 *     Must contain at least the number of bytes required
 *     by the addressed register.
 *
 * frame:
 *     Destination buffer, sized for the largest SPI frame.
 *     Data is arranged in MSB-first wire order.
 *     Unused trailing bytes are cleared.
 *
 * frameSize:
 *     Receives the number of bytes to transmit, including
 *     the instruction byte.
 *     Set to zero on failure when the pointer is valid.
 *
 * This function does not access the SPI peripheral
 * or generate an I/O_UPDATE pulse.
 */
static status_t AD9959_MakeWriteFrame(uint8_t address, const uint8_t *registerData, AD9959_SpiFrame_t frame, uint8_t *frameSize)
{
	uint8_t dataSize;

	if (frameSize == NULL) {
		return kStatus_InvalidArgument;
	}

	*frameSize = 0U;

	if ((registerData == NULL) || (frame == NULL)) {
		return kStatus_InvalidArgument;
	}

	dataSize = AD9959_GetRegisterDataSize(address);

	if ((dataSize == 0U) || (dataSize >= AD9959_SPI_FRAME_MAX_SIZE)) {
		return kStatus_InvalidArgument;
	}

	/*
	 * Build locally so the source may overlap the
	 * destination without being overwritten prematurely.
	 */
	AD9959_SpiFrame_t result = { 0U };

	/* Write instruction: R/W = 0, unused bits = 0. */
	result[0] = address;

	/*
	for (uint8_t i = 0U; i < dataSize; ++i) {
		result[i + 1U] = registerData[dataSize - 1U - i];
	}
	*/

	/* These register types already contain MSB-first bytes. */
	bool alreadyMsbFirst =
	    (address == AD9959_registerTable[eAD9959_Register_FR1].address) ||
	    (address == AD9959_registerTable[eAD9959_Register_FR2].address) ||
	    (address == AD9959_registerTable[eAD9959_Register_CFR].address) ||
	    (address == AD9959_registerTable[eAD9959_Register_CPOW0].address);

	for (uint8_t i = 0U; i < dataSize; ++i)
	{
	    uint8_t sourceIndex = alreadyMsbFirst
	        ? i
	        : (uint8_t)(dataSize - 1U - i);

	    result[i + 1U] = registerData[sourceIndex];
	}

	for (uint8_t i = 0U; i < AD9959_SPI_FRAME_MAX_SIZE; ++i) {
		frame[i] = result[i];
	}

	*frameSize = dataSize + 1U;

	return kStatus_Success;
}


/**
 * @brief Writes one AD9959 register synchronously.
 *
 * Builds the instruction and data bytes using the representation
 * of the addressed register type, then transfers the complete frame
 * through the shared SPI bus.
 *
 * registerData must contain at least the register's required data size.
 * The caller selects the target channels through CSR when necessary.
 *
 * Returns kStatus_DSPI_Busy if the bus is unavailable; otherwise returns
 * the frame-building or transfer status.
 *
 * kStatus_Success confirms SPI completion, without readback verification.
 * Does not generate I/O_UPDATE.
 */
static status_t AD9959_WriteRegisterBlocking(uint8_t address, const uint8_t *registerData )
{
	AD9959_SpiFrame_t frame = { 0U };
	uint8_t frameSize = 0U;
	status_t status;

	status = AD9959_MakeWriteFrame(address, registerData, frame, &frameSize);

	if (status != kStatus_Success) {
		return status;
	}

	return AD9959_TransferBlocking(frame, NULL, frameSize );
}

/**
 * @brief Starts an interrupt-driven write of one AD9959 register.
 *
 * Builds the instruction and data bytes using the representation
 * of the addressed register type. The transfer routine copies the
 * frame into persistent storage before this function returns.
 *
 * registerData must contain at least the register's required data size
 * and may be reused after this call returns.
 * The caller selects the target channels through CSR when necessary.
 *
 * Returns kStatus_DSPI_Busy if the bus is unavailable or a previous
 * AD9959 transfer result has not yet been consumed.
 *
 * kStatus_Success confirms submission only. Collect the final status
 * through AD9959_GetTransferResult() before starting the next write.
 *
 * Does not generate I/O_UPDATE.
 */
static status_t AD9959_WriteRegisterNonBlocking(uint8_t address, const uint8_t *registerData )
{
	AD9959_SpiFrame_t frame = { 0U };
	uint8_t frameSize = 0U;
	status_t status;

	status = AD9959_MakeWriteFrame(address, registerData, frame, &frameSize);

	if (status != kStatus_Success) {
		return status;
	}

	return AD9959_TransferNonBlocking(frame, NULL, frameSize );
}

/**
 * @brief Executes a complete AD9959 SPI frame synchronously.
 *
 * Acquires the shared SPI bus, applies the AD9959 communication
 * settings, and performs a blocking full-duplex transfer.
 *
 * Chip select remains asserted between the bytes of the frame
 * and is released at the end of the transfer.
 *
 * @param txData
 *     Frame to transmit, already arranged in wire byte order.
 *
 * @param rxData
 *     Receive buffer containing at least frameSize bytes.
 *     May be NULL when received data is not required.
 *     For reads, the first received byte corresponds to the
 *     instruction and must be ignored by the caller.
 *
 * @param frameSize
 *     Number of bytes to transfer, including the instruction.
 *     Valid single-register frames contain two to five bytes.
 *
 * @return
 *     kStatus_InvalidArgument for invalid parameters.
 *     kStatus_DSPI_Busy if the shared bus cannot be acquired.
 *     Otherwise, the configuration or SDK transfer status.
 *
 * The bus is released before returning whenever acquisition
 * succeeded. This function does not generate I/O_UPDATE.
 */
static status_t AD9959_TransferBlocking( const uint8_t *txData, uint8_t *rxData, size_t frameSize )
{
	if ((txData == NULL) || (frameSize < 2U)
			|| (frameSize > AD9959_SPI_FRAME_MAX_SIZE))
	{
		return kStatus_InvalidArgument;
	}

	SPI_transaction_t transaction =
	{
		.base = AD9959_SPI,
		.config = &AD9959_spiConfig,
		.txData = txData,
		.rxData = rxData,
		.dataSize = frameSize,
		.configFlags = AD9959_SPI_TRANSFER_FLAGS
	};

	return SPIs_TransferBlocking(&transaction );
}


/**
 * @brief Starts an interrupt-driven SPI frame through the shared bus.
 *
 * Copies TX data into persistent storage before submitting the transfer.
 * rxData, when supplied, must remain valid until completion.
 *
 * Bus acquisition, configuration and release are handled by SPIS.
 */
static status_t AD9959_TransferNonBlocking( const uint8_t *txData, uint8_t *rxData, size_t frameSize )
{
    AD9959_transferContext_t *context = &AD9959_transferContext;

    status_t status;

    if ((txData == NULL) ||
        (frameSize < 2U) ||
        (frameSize > AD9959_SPI_FRAME_MAX_SIZE))
    {
        return kStatus_InvalidArgument;
    }

    if (context->pending)
    {
        return kStatus_DSPI_Busy;
    }

    for (size_t i = 0U; i < frameSize; ++i)
    {
        context->txData[i] = txData[i];
        context->rxData[i] = 0U;
    }

    context->rxDestination = rxData;
    context->frameSize = frameSize;

    context->registerDestination = NULL;
    context->registerAddress = 0U;

    context->completed = false;
    context->status = kStatus_DSPI_Busy;
    context->pending = true;

    SPI_transaction_t transaction =
    {
        .base = AD9959_SPI,
        .config = &AD9959_spiConfig,
        .txData = context->txData,
        .rxData = context->rxData,
        .dataSize = frameSize,
        .configFlags = AD9959_SPI_TRANSFER_FLAGS,
        .callback = AD9959_SPI_Callback,
        .userData = context
    };

    status = SPIs_TransferNonBlocking(&transaction);

    if (status != kStatus_Success)
    {
        context->status = status;
        context->completed = false;
        context->pending = false;
        context->rxDestination = NULL;
    }

    return status;
}


/**
 * @brief Retrieves and consumes a completed SPI transfer result.
 *
 * For a successful register read, skips the instruction byte and
 * converts the received data into the register type's representation.
 * The register destination remains unchanged if the transfer failed.
 *
 * Returns false if no completed result is available or status is NULL.
 * Returns true after storing the final status and releasing the context.
 *
 * Call from the same processing context used to submit transfers.
 */
bool AD9959_GetTransferResult(status_t *status)
{
    AD9959_transferContext_t *context =
        &AD9959_transferContext;

    if ((status == NULL) ||
        !context->pending ||
        !context->completed)
    {
        return false;
    }

    if ((context->status == kStatus_Success) &&
        (context->registerDestination != NULL))
    {
        uint8_t dataSize = (uint8_t)(context->frameSize - 1U);
        uint8_t address = context->registerAddress;


        bool alreadyMsbFirst =
            (address == AD9959_registerTable[eAD9959_Register_FR1].address) ||
            (address == AD9959_registerTable[eAD9959_Register_FR2].address) ||
            (address == AD9959_registerTable[eAD9959_Register_CFR].address) ||
            (address == AD9959_registerTable[eAD9959_Register_CPOW0].address);

        for (uint8_t i = 0U; i < dataSize; ++i)
        {
            uint8_t destinationIndex = alreadyMsbFirst
                ? i
                : (uint8_t)(dataSize - 1U - i);

            context->registerDestination[destinationIndex] =
                context->rxData[i + 1U];
        }
    }

    *status = context->status;

    context->rxDestination = NULL;
    context->registerDestination = NULL;
    context->registerAddress = 0U;
    context->completed = false;
    context->pending = false;

    return true;
}


/**
 * @brief Reads one AD9959 register synchronously.
 *
 * Sends a read instruction followed by dummy bytes, then converts
 * the received data into the addressed register type's representation.
 *
 * registerData must hold at least the register's required data size.
 * Its contents remain unchanged if the transfer fails.
 *
 * Select exactly one channel through CSR before reading a
 * channel-specific register.
 */
static status_t AD9959_ReadRegisterBlocking(uint8_t address, uint8_t *registerData )
{
	AD9959_SpiFrame_t txFrame = { 0U };
	AD9959_SpiFrame_t rxFrame = { 0U };
	uint8_t dataSize;
	bool alreadyMsbFirst;
	status_t status;

	if (registerData == NULL) {
		return kStatus_InvalidArgument;
	}

	dataSize = AD9959_GetRegisterDataSize(address);

	if ((dataSize == 0U) || (dataSize >= AD9959_SPI_FRAME_MAX_SIZE)) {
		return kStatus_InvalidArgument;
	}

	/* Read instruction: R/W = 1. */
	txFrame[0] = (uint8_t) (0x80U | address);

	status = AD9959_TransferBlocking(txFrame, rxFrame, (size_t) dataSize + 1U);

	if (status != kStatus_Success) {
		return status;
	}

	alreadyMsbFirst =
	    (address == AD9959_registerTable[eAD9959_Register_FR1].address) ||
	    (address == AD9959_registerTable[eAD9959_Register_FR2].address) ||
	    (address == AD9959_registerTable[eAD9959_Register_CFR].address) ||
	    (address == AD9959_registerTable[eAD9959_Register_CPOW0].address);

	/* Skip the byte received during instruction transmission. */
	for (uint8_t i = 0U; i < dataSize; ++i) {
		uint8_t destinationIndex =
				alreadyMsbFirst ? i : (uint8_t) (dataSize - 1U - i);

		registerData[destinationIndex] = rxFrame[i + 1U];
	}

	return kStatus_Success;
}

/**
 * @brief Starts an interrupt-driven read of one AD9959 register.
 *
 * registerData must hold the register's required data size and remain
 * valid until AD9959_GetTransferResult() consumes the completed result.
 *
 * Select exactly one channel through CSR before reading a
 * channel-specific register.
 *
 * kStatus_Success confirms submission only.
 * The destination is updated when the result is collected.
 */
static status_t AD9959_ReadRegisterNonBlocking( uint8_t address, uint8_t *registerData )
{
	AD9959_SpiFrame_t txFrame = { 0U };
	uint8_t dataSize;
	status_t status;

	if (registerData == NULL) {
		return kStatus_InvalidArgument;
	}

	dataSize = AD9959_GetRegisterDataSize(address);

	if ((dataSize == 0U) || (dataSize >= AD9959_SPI_FRAME_MAX_SIZE)) {
		return kStatus_InvalidArgument;
	}

	txFrame[0] = (uint8_t) (0x80U | address);

	/* Keep received bytes in the persistent transfer context. */
	status = AD9959_TransferNonBlocking(txFrame,
	NULL, (size_t) dataSize + 1U);

	if (status == kStatus_Success) {
		AD9959_transferContext.registerAddress = address;
		AD9959_transferContext.registerDestination = registerData;
	}

	return status;
}

/**
 * @brief Reads the common registers and the selected channel's registers.
 *
 * Exactly one channel must already be selected through CSR.
 * Each register is accessed through a separate SPI transaction.
 *
 * The destination is updated only after every read succeeds.
 * Register reads may have device-defined side effects.
 */
status_t AD9959_ReadRegistersBlocking(AD9959_regs_t *regs)
{
    AD9959_regs_t snapshot = { 0 };
    uint8_t *bytes = (uint8_t *)&snapshot;
    status_t status;

    if (regs == NULL)
    {
        return kStatus_InvalidArgument;
    }

    for (size_t i = 0U;
         i < sizeof(AD9959_registerTable) /
             sizeof(AD9959_registerTable[0]);
         ++i)
    {
        status = AD9959_ReadRegisterBlocking(
            AD9959_registerTable[i].address,
            &bytes[AD9959_registerTable[i].offset]);

        if (status != kStatus_Success)
        {
            return status;
        }
    }

    *regs = snapshot;

    return kStatus_Success;
}

/**
 * @brief Calculates and writes the DDS PLL configuration synchronously.
 *
 * Preserves all FR1 fields unrelated to the PLL configuration.
 * The output configuration is updated only after a successful write.
 *
 * Does not generate I/O_UPDATE or wait for PLL settling.
 */
status_t AD9959_SetClockBlocking(uint32_t crystalFrequency_Hz, uint32_t requestedSysclk_Hz, AD9959_clockConfig_t *config )
{
	AD9959_clockConfig_t calculated = { 0 };
	AD9959_FR1_t fr1 = { .b8 = { 0U } };
	status_t status;

	if (config == NULL) {
		return kStatus_InvalidArgument;
	}

	status = AD9959_CalculateClockConfig(crystalFrequency_Hz,
			requestedSysclk_Hz, &calculated);

	if (status != kStatus_Success) {
		return status;
	}


	/* Preserve the other FR1 settings. */
	status = AD9959_ReadRegisterBlocking( AD9959_registerTable[eAD9959_Register_FR1].address, fr1.b8);

	if (status != kStatus_Success) {
		return status;
	}

	fr1.bits.ePllDividerRatio = (ePllDividerRatio_t) calculated.pllMultiplier;

	fr1.bits.eVcoGainControl = calculated.eVcoGainControl;

	status = AD9959_WriteRegisterBlocking( AD9959_registerTable[eAD9959_Register_FR1].address, fr1.b8);

	if (status != kStatus_Success) {
		return status;
	}

	/* Retain the clock configuration for frequency calculations. */
	AD9959_clockConfig = calculated;

	*config = calculated;

	return kStatus_Success;
}




/**
 * @brief Selects exactly one channel for subsequent register accesses.
 *
 * Preserves the serial-interface settings.
 * Optionally saves the preceding CSR after a successful selection.
 *
 * Channel selection takes effect immediately, without I/O_UPDATE.
 * Does not change channel power-down settings.
 */
status_t AD9959_SelectChannelBlocking( uint8_t channel, AD9959_CSR_t *previousCsr )
{
	AD9959_CSR_t saved = { .b8 = 0U };
	AD9959_CSR_t selected;
	status_t status;

	const uint8_t address = AD9959_registerTable[eAD9959_Register_CSR].address;

	if (channel >= eAD9959_ChannelCount) {
		return kStatus_InvalidArgument;
	}

	status = AD9959_ReadRegisterBlocking(address, &saved.b8);

	if (status != kStatus_Success) {
		return status;
	}

	selected = saved;

	selected.bits.eChannel0Enable =
			(channel == eAD9959_Channel0) ?
					eChannel_Enabled : eChannel_Disabled;

	selected.bits.eChannel1Enable =
			(channel == eAD9959_Channel1) ?
					eChannel_Enabled : eChannel_Disabled;

	selected.bits.eChannel2Enable =
			(channel == eAD9959_Channel2) ?
					eChannel_Enabled : eChannel_Disabled;

	selected.bits.eChannel3Enable =
			(channel == eAD9959_Channel3) ?
					eChannel_Enabled : eChannel_Disabled;

	status = AD9959_WriteRegisterBlocking(address, &selected.b8);

	if ((status == kStatus_Success) && (previousCsr != NULL)) {
		*previousCsr = saved;
	}

	return status;
}

/**
 * @brief Changes a channel power-down control and restores the previous CSR.
 *
 * Attempts CSR restoration even if the CFR access fails.
 * Returns the CFR error when present; otherwise the restoration status.
 *
 * Call from a single driver processing context.
 * Does not generate I/O_UPDATE.
 */
static status_t AD9959_SetChannelControlBitBlocking( uint8_t channel, eAD9959_controlBit_t control, bool asserted )
{
    AD9959_CSR_t previousCsr = { .b8 = 0U };
    status_t status;
    status_t restoreStatus;

    if ((control != eAD9959_Control_DacPowerDown) &&
        (control != eAD9959_Control_DigitalPowerDown))
    {
        return kStatus_InvalidArgument;
    }

    status = AD9959_SelectChannelBlocking(channel, &previousCsr);

    if (status != kStatus_Success)
    {
        return status;
    }

    status = AD9959_SetControlBitBlocking(control, asserted);

    restoreStatus = AD9959_WriteRegisterBlocking(
        AD9959_registerTable[eAD9959_Register_CSR].address,
        &previousCsr.b8);

    return (status != kStatus_Success) ? status : restoreStatus;
}

/**
 * @brief Changes one named control bit through a blocking register access.
 *
 * Uses the register table for addressing and typed fields for bit changes.
 * Preserves the other register fields.
 *
 * For channel controls, exactly one channel must be selected through CSR.
 * Does not generate I/O_UPDATE.
 */
static status_t AD9959_SetControlBitBlocking(eAD9959_controlBit_t control, bool asserted )
{
	union {
		AD9959_FR1_t fr1;
		AD9959_CFR_t cfr;
	} reg = { 0 };

	eAD9959_registerIndex_t registerIndex;
	status_t status;

	switch (control) {
		case eAD9959_Control_ReferenceClockPowerDown:
		case eAD9959_Control_DacReferencePowerDown:
		case eAD9959_Control_SyncClockDisable:
			registerIndex = eAD9959_Register_FR1;
		break;

		case eAD9959_Control_DacPowerDown:
		case eAD9959_Control_DigitalPowerDown:
			registerIndex = eAD9959_Register_CFR;
		break;

		default:
			return kStatus_InvalidArgument;
	}

	const uint8_t address = AD9959_registerTable[registerIndex].address;

	uint8_t *registerData =
			(registerIndex == eAD9959_Register_FR1) ? reg.fr1.b8 : reg.cfr.b8;

	status = AD9959_ReadRegisterBlocking(address, registerData);

	if (status != kStatus_Success) {
		return status;
	}

	ePowerDown_t powerDown =
			asserted ? ePowerDown_Enabled : ePowerDown_Disabled;

	switch (control) {
	case eAD9959_Control_ReferenceClockPowerDown:
		reg.fr1.bits.eReferenceClockInputPowerDown = powerDown;
		break;

	case eAD9959_Control_DacReferencePowerDown:
		reg.fr1.bits.eDacReferencePowerDown = powerDown;
		break;

	case eAD9959_Control_SyncClockDisable:
		reg.fr1.bits.eSyncClkDisable =
				asserted ?
						eSyncClkDisable_Asserted : eSyncClkDisable_DeAsserted;
		break;

	case eAD9959_Control_DacPowerDown:
		reg.cfr.bits.eDacPowerDown = powerDown;
		break;

	case eAD9959_Control_DigitalPowerDown:
		reg.cfr.bits.eDigitalPowerDown = powerDown;
		break;

	default:
		return kStatus_InvalidArgument;
	}

	return AD9959_WriteRegisterBlocking(address, registerData);
}


/**
 * @brief Enables or powers down the common reference-clock input circuit.
 *
 * enabled = true enables the circuit; false requests power-down.
 * Preserves the other FR1 fields.
 *
 * PWR_DWN_CTL must be low for individual power-down control.
 * Returns the blocking register-access status.
 * Does not generate I/O_UPDATE or wait for clock settling.
 */
status_t AD9959_SetReferenceClockEnabledBlocking(bool enabled)
{
    return AD9959_SetControlBitBlocking(
        eAD9959_Control_ReferenceClockPowerDown, !enabled);
}

/**
 * @brief Enables or powers down the DAC reference shared by all channels.
 *
 * enabled = true enables the reference; false requests power-down.
 * Preserves the other FR1 fields.
 *
 * Returns the blocking register-access status.
 * Does not generate I/O_UPDATE or wait for analog settling.
 */
status_t AD9959_SetDacReferenceEnabledBlocking( bool enabled )
{
    return AD9959_SetControlBitBlocking( eAD9959_Control_DacReferencePowerDown, !enabled );
}

/**
 * @brief Enables or disables the SYNC_CLK output pin.
 *
 * enabled = true activates the output; false holds it at logic zero
 * and shuts down its output driver.
 * Internal synchronization remains active.
 * Preserves the other FR1 fields.
 *
 * Returns the blocking register-access status.
 * Does not generate I/O_UPDATE.
 */
status_t AD9959_SetSyncClockEnabledBlocking(bool enabled)
{
    return AD9959_SetControlBitBlocking( eAD9959_Control_SyncClockDisable, !enabled);
}


/**
 * @brief Enables or powers down one channel's DAC.
 *
 * Selects the requested channel and restores the previous CSR afterward.
 * Does not generate I/O_UPDATE.
 */
status_t AD9959_SetDacEnabledBlocking(uint8_t channel, bool enabled )
{
	return AD9959_SetChannelControlBitBlocking(channel,
			eAD9959_Control_DacPowerDown, !enabled);
}

/**
 * @brief Enables or powers down one channel's digital DDS core.
 *
 * Selects the requested channel and restores the previous CSR afterward.
 * Does not generate I/O_UPDATE.
 */
status_t AD9959_SetDigitalCoreEnabledBlocking(uint8_t channel, bool enabled )
{
	return AD9959_SetChannelControlBitBlocking(channel,
			eAD9959_Control_DigitalPowerDown, !enabled);
}


/**
 * @brief Calculates the nearest frequency tuning word.
 *
 * Supports DC and frequencies strictly below half the system clock.
 * The destination remains unchanged on failure.
 */
static status_t AD9959_CalculateFrequencyWord(uint32_t frequency_Hz, uint32_t sysclk_Hz, uint32_t *ftw )
{
	uint64_t numerator;

	if ((ftw == NULL) || (sysclk_Hz == 0U)) {
		return kStatus_InvalidArgument;
	}

	if (((uint64_t) frequency_Hz * 2U) >= sysclk_Hz) {
		return kStatus_InvalidArgument;
	}

	/* Round to the nearest tuning word using integer arithmetic. */
	numerator = ((uint64_t) frequency_Hz << AD9959_FTW_BITS) + (sysclk_Hz / 2U);

	*ftw = (uint32_t) (numerator / sysclk_Hz);

	return kStatus_Success;
}


/**
 * @brief Writes the frequency tuning word of one DDS channel.
 *
 * Uses the system clock retained by AD9959_SetClockBlocking().
 * Selects the requested channel and restores the preceding CSR,
 * including when the tuning-word write fails.
 *
 * The channel must be configured for single-tone operation.
 * Does not change channel power-down settings.
 * Does not generate I/O_UPDATE.
 *
 * Call from a single driver processing context.
 */
status_t AD9959_SetFrequencyBlocking( eAD9959_channel_t channel, uint32_t frequency_Hz)
{
	AD9959_CSR_t previousCsr = { .b8 = 0U };
	AD9959_CFTW0_t reg;
	uint32_t ftw;
	status_t status;
	status_t restoreStatus;

	if (channel >= eAD9959_ChannelCount) {
		return kStatus_InvalidArgument;
	}

	if (AD9959_clockConfig.actualSysclk_Hz == 0U) {
		return kStatus_Fail;
	}

	status = AD9959_CalculateFrequencyWord(frequency_Hz,
			AD9959_clockConfig.actualSysclk_Hz, &ftw);

	if (status != kStatus_Success) {
		return status;
	}

	reg = AD9959_MakeCftw0(ftw);

	status = AD9959_SelectChannelBlocking(channel, &previousCsr);

	if (status != kStatus_Success) {
		return status;
	}

	status = AD9959_WriteRegisterBlocking(
			AD9959_registerTable[eAD9959_Register_CFTW0].address, reg.b8);

	restoreStatus = AD9959_WriteRegisterBlocking(
			AD9959_registerTable[eAD9959_Register_CSR].address,
			&previousCsr.b8);

	return (status != kStatus_Success) ? status : restoreStatus;
}


/**
 * @brief Writes a tuning word to the currently selected DDS channel.
 *
 * Does not read, modify or restore CSR.
 * Select exactly one channel before starting the sequence.
 *
 * In non-blocking mode, success means submission succeeded.
 * Retrieve completion before submitting the next transfer.
 *
 * Does not generate I/O_UPDATE.
 */
status_t AD9959_SetFrequencySelected( uint32_t frequency_Hz, bool blocking )
{
	uint32_t ftw;
	status_t status;
	AD9959_CFTW0_t reg;

    if (AD9959_transferContext.pending)
    {
        return kStatus_DSPI_Busy;
    }


    status = AD9959_CalculateFrequencyWord(frequency_Hz,
    			AD9959_clockConfig.actualSysclk_Hz, &ftw );

    if (status != kStatus_Success) {
    	return status;
    }

    reg = AD9959_MakeCftw0(ftw);

    const uint8_t address =
        AD9959_registerTable[ eAD9959_Register_CFTW0 ].address;

    if (blocking)
    {
    	return AD9959_WriteRegisterBlocking( address, reg.b8);
    }

    return AD9959_WriteRegisterNonBlocking( address, reg.b8 );
}



static void AD9959_ResetAssert()
{

    GPIO_PinWrite(AD9959_RESET_GPIO,
                  AD9959_RESET_PIN,
                  AD9959_RESET_ASSERTED_LEVEL);


}

static void AD9959_ResetDeAssert()
{

    GPIO_PinWrite(AD9959_RESET_GPIO,
                  AD9959_RESET_PIN,
                  AD9959_RESET_DEASSERTED_LEVEL);

}

static void AD9959_IoUpdateAssert(void)
{

    GPIO_PinWrite(AD9959_IO_UPDATE_GPIO,
                  AD9959_IO_UPDATE_PIN,
                  1U);

}

static void AD9959_IoUpdateDeAssert(void)
{

    GPIO_PinWrite(AD9959_IO_UPDATE_GPIO,
                  AD9959_IO_UPDATE_PIN,
                  0U);
    }


#if 0

/**
 * @brief Resets the DDS and configures its serial interface.
 *
 * Configures single-bit 3-wire SPI with MSB-first transfers,
 * then reads back CSR and verifies its complete contents.
 *
 * Initially selects channel 0 for channel-register accesses.
 * Channel selection does not control channel power.
 *
 * Preconditions:
 * - Shared SPI bus initialized.
 * - MASTER_RESET pin configured as a GPIO output.
 * - Reference clock running and stable.
 * - Called during initialization, without concurrent DDS accesses.
 *
 * Does not generate I/O_UPDATE.
 * Clock and output-frequency configuration must follow this call.
 *
 * @param coreClock_Hz Actual MCU core clock frequency.
 *
 * @return SDK transfer status, kStatus_DSPI_Busy if an asynchronous
 *         DDS operation is pending, or kStatus_Fail on CSR mismatch.
 */
status_t AD9959_InitBlocking(uint32_t coreClock_Hz)
{
    AD9959_CSR_t csr = { .b8 = 0U };
    AD9959_CSR_t readback = { .b8 = 0U };

    const uint8_t address = AD9959_registerTable[eAD9959_Register_CSR].address;

    status_t status;

    if (coreClock_Hz == 0U)
    {
        return kStatus_InvalidArgument;
    }

    if (AD9959_transferContext.pending)
    {
        return kStatus_DSPI_Busy;
    }

    /* Restore the device registers and serial-port defaults. */
    AD9959_ResetAssert();

    SDK_DelayAtLeastUs( AD9959_RESET_PULSE_US, coreClock_Hz );

    AD9959_ResetDeAssert();

    /* Previously programmed PLL settings are no longer valid. */
    AD9959_clockConfig = (AD9959_clockConfig_t){ 0 };

    SDK_DelayAtLeastUs(AD9959_RESET_RECOVERY_US, coreClock_Hz);

    /* Configure the serial port using the existing field definitions. */
    csr.bits.eBitOrder = eBitOrder_MsbFirst;
    csr.bits.eSerialIoMode = eSerialIoMode_SingleBit3Wire;

    csr.bits.eChannel0Enable = eChannel_Enabled;
    csr.bits.eChannel1Enable = eChannel_Disabled;
    csr.bits.eChannel2Enable = eChannel_Disabled;
    csr.bits.eChannel3Enable = eChannel_Disabled;

    status = AD9959_WriteRegisterBlocking(address, &csr.b8);

    if (status != kStatus_Success)
    {
        return status;
    }

    /* Verify communication through the separate readback pin. */
    status = AD9959_ReadRegisterBlocking(address, &readback.b8);

    if (status != kStatus_Success)
    {
        return status;
    }

    return (readback.b8 == csr.b8)
        ? kStatus_Success
        : kStatus_Fail;
}
#endif

/**
 * @brief Writes a complete CFR or ACR configuration for one channel.
 *
 * Selects the requested channel and restores the previous CSR afterward.
 * Attempts CSR restoration even if the register write fails.
 *
 * Single-tone operation writes all CFR fields.
 * Amplitude operation writes all ACR fields for manual scaling.
 *
 * Call without concurrent DDS accesses.
 * Does not generate I/O_UPDATE.
 */
static status_t AD9959_ConfigureChannelBlocking(uint8_t channel, AD9959_channelOperation_t operation, uint16_t amplitudeScaleFactor )
{
	union {
		AD9959_CFR_t cfr;
		AD9959_ACR_t acr;
	} reg = { 0 };

	AD9959_CSR_t previousCsr;

	uint8_t address;
	uint8_t *registerData;

	status_t status;
	status_t restoreStatus;

	if (channel >= eAD9959_ChannelCount )
	{
		return kStatus_InvalidArgument;
	}

	switch (operation) {
		case eAD9959_ChannelOperation_SingleTone:
			address = AD9959_registerTable[eAD9959_Register_CFR].address;

			registerData = reg.cfr.b8;
			break;

		case eAD9959_ChannelOperation_Amplitude:
			if (amplitudeScaleFactor > AD9959_AMPLITUDE_SCALE_MAX) {
				return kStatus_InvalidArgument;
			}

			address = AD9959_registerTable[eAD9959_Register_ACR].address;

			registerData = reg.acr.b8;
			break;

		default:
			return kStatus_InvalidArgument;
	}

	status = AD9959_SelectChannelBlocking(channel, &previousCsr);

	if (status != kStatus_Success) {
		return status;
	}

	// status = AD9959_ReadRegisterBlocking(address, registerData);

	switch (operation)
	{
		case eAD9959_ChannelOperation_SingleTone:
		    /* Reserved fields. */
		    reg.cfr.bits.sp0 = 0U;
		    reg.cfr.bits.sp1 = 0U;
		    reg.cfr.bits.sp2 = 0U;

		    /* Single-tone operation without modulation or sweep. */
		    reg.cfr.bits.eAfpSelect =
		        eAfpSelect_Disabled;

		    reg.cfr.bits.eLinearSweepEnable =
		        eLinearSweep_Disabled;

		    reg.cfr.bits.eLinearSweepNoDwell =
		        eLinearSweepNoDwell_Disabled;

		    reg.cfr.bits.eLoadSrrAtIoUpdate =
		        eLoadSrrAtIoUpdate_Disabled;

		    /* DAC at full-scale current; channel powered on. */
		    reg.cfr.bits.eDacFullScaleCurrentControl =
		        eDacFullScaleCurrent_Full;

		    reg.cfr.bits.eDacPowerDown =
		        ePowerDown_Disabled;

		    reg.cfr.bits.eDigitalPowerDown =
		        ePowerDown_Disabled;

		    /* Accumulators running without automatic clearing. */
		    reg.cfr.bits.eClearPhaseAccumulator =
		        eAccumulatorClear_DeAsserted;

		    reg.cfr.bits.eAutoclearPhaseAccumulator =
		        eAccumulatorAutoclear_Disabled;

		    reg.cfr.bits.eClearSweepAccumulator =
		        eAccumulatorClear_DeAsserted;

		    reg.cfr.bits.eAutoclearSweepAccumulator =
		        eAccumulatorAutoclear_Disabled;

		    /* Explicit waveform and pipeline settings. */
		    reg.cfr.bits.eSineWaveOutputEnable =
		        eSineWaveOutput_Enabled;

		    reg.cfr.bits.eMatchedPipeDelays =
		        eMatchedPipeDelays_Disabled;
		break;


		case eAD9959_ChannelOperation_Amplitude:
		    /* Reserved field. */
		    reg.acr.bits.sp0 = 0U;

		    /* Direct software control of the amplitude. */
		    reg.acr.bits.eAmplitudeMultiplierEnable =
		        eAmplitudeMultiplier_Enabled;

		    reg.acr.bits.eRampUpDownMode =
		        eRampUpDownMode_Manual;

		    reg.acr.bits.amplitudeScaleFactor =
		        amplitudeScaleFactor;

		    /* Automatic ramp settings are unused in manual mode. */
		    reg.acr.bits.eLoadArrAtIoUpdate =
		        eLoadArrAtIoUpdate_Disabled;

		    reg.acr.bits.eAmplitudeStepSize =
		        eAmplitudeStepSize_1;

		    reg.acr.bits.amplitudeRampRate = 0U;
		break;
		default:
				/* Operation was validated before hardware access. */
		break;
	}

	status = AD9959_WriteRegisterBlocking(address, registerData);


	restoreStatus = AD9959_WriteRegisterBlocking(
			AD9959_registerTable[eAD9959_Register_CSR].address,
			&previousCsr.b8);

	return (status != kStatus_Success) ? status : restoreStatus;
}


/**
 * @brief Configures one channel for powered-on single-tone operation.
 *
 * Disables modulation, linear sweep and accumulator clear controls.
 * Enables the DAC and digital core, selects full-scale DAC current
 * and sine-wave output, and disables matched pipeline delays.
 *
 * Leaves frequency, phase offset and ACR unchanged.
 * Restores the previous channel selection.
 * Requires a subsequent I/O_UPDATE.
 */
status_t AD9959_SetSingleToneBlocking(uint8_t channel)
{
    return AD9959_ConfigureChannelBlocking(
        channel,
        eAD9959_ChannelOperation_SingleTone,
        0U);
}


/**
 * @brief Sets the digital amplitude scale factor for one channel.
 *
 * Enables the amplitude multiplier in manual mode.
 * Valid scale factors range from 0 to AD9959_AMPLITUDE_SCALE_MAX.
 *
 * Zero suppresses the ideal generated signal; it does not
 * power down the DAC or guarantee zero RF leakage.
 *
 * Preserves the DAC full-scale current setting.
 * Restores the previous channel selection.
 * Requires a subsequent I/O_UPDATE to apply the configuration.
 */
status_t AD9959_SetAmplitudeBlocking(uint8_t channel, uint16_t amplitudeScaleFactor )
{
	return AD9959_ConfigureChannelBlocking(channel,
			eAD9959_ChannelOperation_Amplitude, amplitudeScaleFactor);
}




















/*
 * Configure common resources and channel power states.
 * Active channels use an explicit single-tone CFR configuration.
 * Preserves frequency words, amplitude settings and PLL configuration.
 * Requires PWR_DWN_CTL low. Does not generate I/O_UPDATE.
 * autotest is true if I need two channels for carrier and lo.
 */
static status_t AD9959_PowerUpModeBlocking( bool autotest )
{
    AD9959_CSR_t savedCsr;
    AD9959_CSR_t csr;
    AD9959_FR1_t fr1;
    AD9959_CFR_t cfr = { 0 };

    status_t status;
    status_t restoreStatus;

    const uint8_t csrAddress =
        AD9959_registerTable[eAD9959_Register_CSR].address;

    const uint8_t fr1Address =
        AD9959_registerTable[eAD9959_Register_FR1].address;

    const uint8_t cfrAddress =
        AD9959_registerTable[eAD9959_Register_CFR].address;

    if (AD9959_transferContext.pending)
    {
        return kStatus_DSPI_Busy;
    }

    status = AD9959_ReadRegisterBlocking(csrAddress, &savedCsr.b8);
    if (status != kStatus_Success)
    {
        return status;
    }

    status = AD9959_ReadRegisterBlocking(fr1Address, fr1.b8);
    if (status != kStatus_Success)
    {
        return status;
    }

    /* Preserve PLL and other common settings. */
    fr1.bits.eReferenceClockInputPowerDown = ePowerDown_Disabled;
    fr1.bits.eDacReferencePowerDown = ePowerDown_Disabled;
    fr1.bits.eSyncClkDisable = eSyncClkDisable_DeAsserted;

    /* Explicit single-tone configuration for all channels. */
    cfr.bits.sp0 = 0U;
    cfr.bits.sp1 = 0U;
    cfr.bits.sp2 = 0U;

    cfr.bits.eAfpSelect = eAfpSelect_Disabled;
    cfr.bits.eLinearSweepEnable = eLinearSweep_Disabled;
    cfr.bits.eLinearSweepNoDwell = eLinearSweepNoDwell_Disabled;
    cfr.bits.eLoadSrrAtIoUpdate = eLoadSrrAtIoUpdate_Disabled;

    cfr.bits.eDacFullScaleCurrentControl = eDacFullScaleCurrent_Full;
    cfr.bits.eSineWaveOutputEnable = eSineWaveOutput_Enabled;
    cfr.bits.eMatchedPipeDelays = eMatchedPipeDelays_Disabled;

    cfr.bits.eClearPhaseAccumulator = eAccumulatorClear_DeAsserted;
    cfr.bits.eAutoclearPhaseAccumulator = eAccumulatorAutoclear_Disabled;
    cfr.bits.eClearSweepAccumulator = eAccumulatorClear_DeAsserted;
    cfr.bits.eAutoclearSweepAccumulator = eAccumulatorAutoclear_Disabled;

    cfr.bits.eDacPowerDown = ePowerDown_Enabled;
    cfr.bits.eDigitalPowerDown = ePowerDown_Enabled;

    /* Select all channels without changing the SPI configuration. */
    csr = savedCsr;
    csr.bits.eChannel0Enable = eChannel_Enabled;
    csr.bits.eChannel1Enable = eChannel_Enabled;
    csr.bits.eChannel2Enable = eChannel_Enabled;
    csr.bits.eChannel3Enable = eChannel_Enabled;

    status = AD9959_WriteRegisterBlocking(csrAddress, &csr.b8);
    if (status != kStatus_Success)
    {
        goto restore_csr;
    }

    /* Stage power-down for all channels in one write. */
    status = AD9959_WriteRegisterBlocking(cfrAddress, cfr.b8);
    if (status != kStatus_Success)
    {
        goto restore_csr;
    }

    /* Select LO, plus RF input when autotest is requested. */
    csr.bits.eChannel0Enable =
        ((AD9959_CHANNEL_LOCAL_OSC == eAD9959_Channel0) ||
         (autotest && (AD9959_CHANNEL_RF_INP == eAD9959_Channel0)))
        ? eChannel_Enabled : eChannel_Disabled;

    csr.bits.eChannel1Enable =
        ((AD9959_CHANNEL_LOCAL_OSC == eAD9959_Channel1) ||
         (autotest && (AD9959_CHANNEL_RF_INP == eAD9959_Channel1)))
        ? eChannel_Enabled : eChannel_Disabled;

    csr.bits.eChannel2Enable =
        ((AD9959_CHANNEL_LOCAL_OSC == eAD9959_Channel2) ||
         (autotest && (AD9959_CHANNEL_RF_INP == eAD9959_Channel2)))
        ? eChannel_Enabled : eChannel_Disabled;

    csr.bits.eChannel3Enable =
        ((AD9959_CHANNEL_LOCAL_OSC == eAD9959_Channel3) ||
         (autotest && (AD9959_CHANNEL_RF_INP == eAD9959_Channel3)))
        ? eChannel_Enabled : eChannel_Disabled;

    status = AD9959_WriteRegisterBlocking(csrAddress, &csr.b8);
    if (status != kStatus_Success)
    {
        goto restore_csr;
    }

    /* Stage power-up only for the selected channels. */
    cfr.bits.eDacPowerDown = ePowerDown_Disabled;
    cfr.bits.eDigitalPowerDown = ePowerDown_Disabled;

    status = AD9959_WriteRegisterBlocking(cfrAddress, cfr.b8);
    if (status != kStatus_Success)
    {
        goto restore_csr;
    }

    status = AD9959_WriteRegisterBlocking(fr1Address, fr1.b8);

restore_csr:
    restoreStatus =
        AD9959_WriteRegisterBlocking(csrAddress, &savedCsr.b8);

    return (status != kStatus_Success) ? status : restoreStatus;
}


/*
 * Stage power-up of the LO and RF input channels for autotest.
 * All other channels are powered down at the next I/O_UPDATE.
 * Requires PWR_DWN_CTL low. Does not generate I/O_UPDATE.
 */
status_t AD9959_PowerUpAutotestBlocking(void)
{
    return AD9959_PowerUpModeBlocking(true);
}


/*
 * Stage power-up of the LO channel only.
 * All other channels are powered down at the next I/O_UPDATE.
 * Requires PWR_DWN_CTL low. Does not generate I/O_UPDATE.
 */
status_t AD9959_PowerUpLoBlocking(void)
{
    return AD9959_PowerUpModeBlocking(false);
}


/*
 * Initialize I/O_UPDATE and MASTER_RESET as GPIO outputs.
 * Both signals start low: update inactive and reset deasserted.
 */
void AD9959_InitControlPins(void)
{
    const gpio_pin_config_t outputLow =
    {
        .pinDirection = kGPIO_DigitalOutput,
        .outputLogic = 0U
    };

    CLOCK_EnableClock(kCLOCK_PortC);

    /* Set output values and directions before connecting the GPIO mux. */
    GPIO_PinInit(AD9959_IO_UPDATE_GPIO,
                 AD9959_IO_UPDATE_PIN,
                 &outputLow);

    GPIO_PinInit(AD9959_RESET_GPIO,
                 AD9959_RESET_PIN,
                 &outputLow);

    PORT_SetPinMux(AD9959_IO_UPDATE_PORT,
                   AD9959_IO_UPDATE_PIN,
                   kPORT_MuxAsGpio);

    PORT_SetPinMux(AD9959_RESET_PORT,
                   AD9959_RESET_PIN,
                   kPORT_MuxAsGpio);
}
/*
 * Here is the support for a logged startup
 */

#define AD9959_STARTUP_ERROR_CAPACITY       32U
#define AD9959_STARTUP_CRYSTAL_WAIT_US   10000U
#define AD9959_STARTUP_PLL_WAIT_US        5000U
#define AD9959_STARTUP_IO_UPDATE_US          1U
#define AD9959_STARTUP_FULL_BYTE_MASK     UINT8_MAX
#define AD9959_STARTUP_NO_CHANNEL         UINT8_MAX
#define AD9959_STARTUP_NO_BYTE            UINT8_MAX

/* Distinct patterns also test whether channel selection works. */
static const uint32_t AD9959_startupTestWords[eAD9959_ChannelCount] =
{
    [eAD9959_Channel0] = UINT32_C(0x13579BDF),
    [eAD9959_Channel1] = UINT32_C(0x2468ACE0),
    [eAD9959_Channel2] = UINT32_C(0x55AA33CC),
    [eAD9959_Channel3] = UINT32_C(0xAA55CC33)
};

typedef enum eAD9959_StartupStep_t
{
    eAD9959_Startup_NotStarted,
    eAD9959_Startup_Reset,
    eAD9959_Startup_Spi,
    eAD9959_Startup_ResetRead,
    eAD9959_Startup_ResetCompare,
    eAD9959_Startup_WriteReadTest,
    eAD9959_Startup_TestRestore,
    eAD9959_Startup_Clock,
    eAD9959_Startup_ClockCompare,
    eAD9959_Startup_PowerDown,
    eAD9959_Startup_PowerDownCompare,
    eAD9959_Startup_FinalSelection,
    eAD9959_Startup_Done
} eAD9959_StartupStep_t;

typedef struct AD9959_startupError_t
{
    eAD9959_StartupStep_t step;
    status_t status;
    uint8_t channel; /* NO_CHANNEL means common register/no channel context. */
    eAD9959_registerIndex_t registerIndex; /* Count means unknown/not applicable. */
    uint8_t byteIndex; /* Index in the driver's decoded register image. */
    uint8_t expected;
    uint8_t actual;
    uint8_t compareMask;
} AD9959_startupError_t;

typedef struct AD9959_startupLog_t
{
    bool completed;
    bool successful;
    bool powerDownRequested;
    bool powerDownVerified;
    bool spiVerified;
    bool resetRegistersVerified;
    bool writeReadVerified;
    bool pllConfigurationVerified; /* Readback only; not physical PLL lock. */
    eAD9959_StartupStep_t step;
    eAD9959_StartupStep_t failedStep;
    status_t status;
    uint32_t errorCount; /* Total, including errors beyond storage capacity. */
    uint8_t storedErrorCount;
    bool errorOverflow;
    AD9959_startupError_t errors[AD9959_STARTUP_ERROR_CAPACITY];
    AD9959_clockConfig_t clockConfig;
    bool resetImageValid[eAD9959_ChannelCount];
    AD9959_regs_t resetImages[eAD9959_ChannelCount];
} AD9959_startupLog_t;

static AD9959_startupLog_t AD9959_startupLog;

const AD9959_startupLog_t *AD9959_GetStartupLog(void)
{
    return &AD9959_startupLog;
}



void AD9959_IoUpdate(uint32_t coreClock_Hz)
{
    AD9959_IoUpdateDeAssert();
    SDK_DelayAtLeastUs(AD9959_STARTUP_IO_UPDATE_US, coreClock_Hz);

    AD9959_IoUpdateAssert();
    SDK_DelayAtLeastUs(AD9959_STARTUP_IO_UPDATE_US, coreClock_Hz);

    AD9959_IoUpdateDeAssert();
}



static void AD9959_StartupError(status_t status, uint8_t channel, eAD9959_registerIndex_t index, uint8_t byteIndex, uint8_t expected, uint8_t actual, uint8_t mask )
{
    AD9959_startupLog_t *log = &AD9959_startupLog;
    AD9959_startupError_t error =
    {
        log->step, status, channel, index, byteIndex, expected, actual, mask
    };
    if (log->errorCount == 0U)
    {
        log->failedStep = log->step;
        log->status = status;
    }
    ++log->errorCount;
    if (log->storedErrorCount < AD9959_STARTUP_ERROR_CAPACITY)
    {
        log->errors[log->storedErrorCount++] = error;
    }
    else
    {
        log->errorOverflow = true;
    }
}

static void AD9959_StartupCompare(uint8_t channel, eAD9959_registerIndex_t index, const uint8_t *actual, const uint8_t *expected, const uint8_t *mask )
{
    const uint8_t size = AD9959_GetRegisterDataSize(
        AD9959_registerTable[index].address);
    for (uint8_t i = 0U; i < size; ++i)
    {
        if (((actual[i] ^ expected[i]) & mask[i]) != 0U)
        {
            AD9959_StartupError(kStatus_Fail, channel, index,
                i, expected[i], actual[i], mask[i]);
        }
    }
}

static status_t AD9959_StartupAccess(bool write, uint8_t channel, eAD9959_registerIndex_t index, uint8_t *data )
{
    const uint8_t address = AD9959_registerTable[index].address;
    status_t status = write ? AD9959_WriteRegisterBlocking(address, data)
                           : AD9959_ReadRegisterBlocking(address, data);
    if (status != kStatus_Success)
    {
        AD9959_StartupError(status, channel, index,
            AD9959_STARTUP_NO_BYTE, 0U, 0U, 0U);
    }
    return status;
}

/*
 * Builds the CSR on startup
 */
static AD9959_CSR_t AD9959_MakeStartupCsr(uint8_t channel)
{
    AD9959_CSR_t csr = { .b8 = 0U };
    csr.bits.eBitOrder = eBitOrder_MsbFirst;
    csr.bits.eSerialIoMode = eSerialIoMode_SingleBit3Wire;
    csr.bits.eChannel0Enable = (channel == eAD9959_Channel0)
        ? eChannel_Enabled : eChannel_Disabled;
    csr.bits.eChannel1Enable = (channel == eAD9959_Channel1)
        ? eChannel_Enabled : eChannel_Disabled;
    csr.bits.eChannel2Enable = (channel == eAD9959_Channel2)
        ? eChannel_Enabled : eChannel_Disabled;
    csr.bits.eChannel3Enable = (channel == eAD9959_Channel3)
        ? eChannel_Enabled : eChannel_Disabled;
    return csr;
}

static void AD9959_StartupResetMasks( AD9959_regs_t *expected, AD9959_regs_t *mask )
{
    memset(expected, 0, sizeof(*expected));
    memset(mask, 0, sizeof(*mask));
    mask->CSR.b8 = AD9959_STARTUP_FULL_BYTE_MASK;
    memset(mask->FR1.b8, AD9959_STARTUP_FULL_BYTE_MASK, sizeof(mask->FR1.b8));
    memset(mask->FR2.b8, AD9959_STARTUP_FULL_BYTE_MASK, sizeof(mask->FR2.b8));
    /* Clear-on-read multidevice sync status is not a configuration bit. */
    mask->FR2.bits.eMultideviceSyncStatus = eMultideviceSyncStatus_NoLossDetected;
    expected->CFR.bits.eDacFullScaleCurrentControl = eDacFullScaleCurrent_Full;
    /* Bootstrap I/O_UPDATE has already cleared the reset phase-clear bit. */
    expected->CFR.bits.eClearPhaseAccumulator = eAccumulatorClear_DeAsserted;
    memset(mask->CFR.b8, AD9959_STARTUP_FULL_BYTE_MASK, sizeof(mask->CFR.b8));
    memset(mask->CPOW0.b8, AD9959_STARTUP_FULL_BYTE_MASK, sizeof(mask->CPOW0.b8));
    /* Only these CFTW0/ACR bits have specified reset values. */
    mask->CFTW0.bits.frequencyTuningWord = UINT32_C(0xFF000000);
    mask->ACR.bits.amplitudeScaleFactor = AD9959_AMPLITUDE_SCALE_MAX;
    mask->ACR.bits.eLoadArrAtIoUpdate = eLoadArrAtIoUpdate_Enabled;
    mask->ACR.bits.eRampUpDownMode = eRampUpDownMode_Automatic;
    mask->ACR.bits.eAmplitudeMultiplierEnable = eAmplitudeMultiplier_Enabled;
    mask->ACR.bits.sp0 = 1U;
    mask->ACR.bits.eAmplitudeStepSize = eAmplitudeStepSize_8;
    /* ACR ramp rate, LSRR, RDW, FDW and CW1..CW15 remain unmasked (N/A). */
}

/*
 * Cold reset, register analysis, SPI write/read test and PLL configuration.
 * powerDownAtEnd=true: standby with crystal/PLL running, all channels off,
 * DAC reference off and SYNC_CLK output disabled.
 * powerDownAtEnd=false: retain reset channel power states and active SYNC_CLK.
 * On success all CFTW0 words are zero; no RF tone is configured.
 * Final CSR selects LOCAL_OSC for later writes.
 * The routine generates the I/O_UPDATE pulses required by startup.
 * On failure, state is partial: consult the log; do not start measurements.
 */
status_t AD9959_ColdStartBlocking( uint32_t coreClock_Hz, bool powerDownAtEnd )
{
    AD9959_startupLog_t *log = &AD9959_startupLog;
    AD9959_regs_t expected, mask;
    AD9959_CSR_t csr, readCsr;
    AD9959_FR1_t fr1, readFr1;
    AD9959_CFR_t standbyCfr;
    AD9959_CFTW0_t word, readWord;
    uint8_t fullMask[AD9959_SPI_FRAME_MAX_SIZE] =
    {
        AD9959_STARTUP_FULL_BYTE_MASK, AD9959_STARTUP_FULL_BYTE_MASK,
        AD9959_STARTUP_FULL_BYTE_MASK, AD9959_STARTUP_FULL_BYTE_MASK,
        AD9959_STARTUP_FULL_BYTE_MASK
    };

    AD9959_InitControlPins();

    /* Do not overwrite an existing log or touch hardware while DDS is busy. */
    if (AD9959_transferContext.pending)
    {
        return kStatus_DSPI_Busy;
    }
    memset(log, 0, sizeof(*log));
    log->powerDownRequested = powerDownAtEnd;
    log->step = eAD9959_Startup_Reset;
    if (coreClock_Hz == 0U)
    {
        AD9959_StartupError(kStatus_InvalidArgument, AD9959_STARTUP_NO_CHANNEL,
            eAD9959_Register_Count, AD9959_STARTUP_NO_BYTE, 0U, 0U, 0U);
        goto finish;
    }

    /* Hold I/O_UPDATE low while waiting for the reference clock. */
    AD9959_IoUpdateDeAssert();
    SDK_DelayAtLeastUs(AD9959_STARTUP_CRYSTAL_WAIT_US, coreClock_Hz);
    /* Restore hardware register defaults. */
    AD9959_ResetAssert();
    SDK_DelayAtLeastUs(AD9959_RESET_PULSE_US, coreClock_Hz);
    AD9959_ResetDeAssert();

    /* Invalidate the previous software clock configuration. */
    AD9959_clockConfig = (AD9959_clockConfig_t){ 0 };

    SDK_DelayAtLeastUs(AD9959_RESET_RECOVERY_US, coreClock_Hz);

    log->step = eAD9959_Startup_Spi;

    csr = AD9959_MakeStartupCsr(AD9959_CHANNEL_LOCAL_OSC);
    if ( AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
    		eAD9959_Register_CSR, &csr.b8 )
    	 != kStatus_Success
	   )
    	goto finish;
    AD9959_IoUpdate(coreClock_Hz);

    if (AD9959_StartupAccess(false, AD9959_STARTUP_NO_CHANNEL,
    		eAD9959_Register_CSR, &readCsr.b8) != kStatus_Success)
    	goto finish;
    AD9959_StartupCompare(AD9959_STARTUP_NO_CHANNEL, eAD9959_Register_CSR,
    		&readCsr.b8, &csr.b8, fullMask);
    if (log->errorCount != 0U)
    	goto finish;
    log->spiVerified = true;

    AD9959_StartupResetMasks(&expected, &mask);

    for (uint8_t ch = 0U; ch < eAD9959_ChannelCount; ++ch)
    {
        log->step = eAD9959_Startup_ResetRead;
        csr = AD9959_MakeStartupCsr(ch);
        if ( AD9959_StartupAccess(true, ch, eAD9959_Register_CSR,
        			&csr.b8)
        		!= kStatus_Success
		    )
        	goto finish;
        status_t status = AD9959_ReadRegistersBlocking(&log->resetImages[ch]);
        if (status != kStatus_Success)
        {
            /* Existing bulk reader does not expose the failing SPI address. */
            AD9959_StartupError(status, ch, eAD9959_Register_Count,
                AD9959_STARTUP_NO_BYTE, 0U, 0U, 0U);
            goto finish;
        }
        log->resetImageValid[ch] = true;
        expected.CSR = csr;
        log->step = eAD9959_Startup_ResetCompare;
        for (unsigned int i = 0U; i < eAD9959_Register_Count; ++i)
        {
            size_t offset = AD9959_registerTable[i].offset;
            AD9959_StartupCompare(ch, (eAD9959_registerIndex_t)i,
                (const uint8_t *)&log->resetImages[ch] + offset,
                (const uint8_t *)&expected + offset,
                (const uint8_t *)&mask + offset);
        }
    }
    if (log->errorCount != 0U) goto finish;
    log->resetRegistersVerified = true;

    log->step = eAD9959_Startup_WriteReadTest;
    /* No I/O_UPDATE while the test words are staged. */
    for (uint8_t ch = 0U; ch < eAD9959_ChannelCount; ++ch)
    {
        csr = AD9959_MakeStartupCsr(ch);
        if (AD9959_StartupAccess(true, ch, eAD9959_Register_CSR,
            &csr.b8) != kStatus_Success) goto finish;
        word = AD9959_MakeCftw0(AD9959_startupTestWords[ch]);
        if (AD9959_StartupAccess(true, ch, eAD9959_Register_CFTW0,
            word.b8) != kStatus_Success) goto finish;
    }
    for (uint8_t ch = 0U; ch < eAD9959_ChannelCount; ++ch)
    {
        csr = AD9959_MakeStartupCsr(ch);
        if (AD9959_StartupAccess(true, ch, eAD9959_Register_CSR,
            &csr.b8) != kStatus_Success) goto finish;
        if (AD9959_StartupAccess(false, ch, eAD9959_Register_CFTW0,
            readWord.b8) != kStatus_Success) goto finish;
        word = AD9959_MakeCftw0(AD9959_startupTestWords[ch]);
        AD9959_StartupCompare(ch, eAD9959_Register_CFTW0,
            readWord.b8, word.b8, fullMask);
    }
    log->writeReadVerified = (log->errorCount == 0U);
    log->step = eAD9959_Startup_TestRestore;
    /* Establish a deterministic zero frequency for all channels. */
    csr.bits.eChannel0Enable = eChannel_Enabled;
    csr.bits.eChannel1Enable = eChannel_Enabled;
    csr.bits.eChannel2Enable = eChannel_Enabled;
    csr.bits.eChannel3Enable = eChannel_Enabled;
    if (AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
        eAD9959_Register_CSR, &csr.b8) != kStatus_Success) goto finish;
    word = AD9959_MakeCftw0(0U);
    if (AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
        eAD9959_Register_CFTW0, word.b8) != kStatus_Success) goto finish;
    for (uint8_t ch = 0U; ch < eAD9959_ChannelCount; ++ch)
    {
        csr = AD9959_MakeStartupCsr(ch);
        if (AD9959_StartupAccess(true, ch, eAD9959_Register_CSR,
            &csr.b8) != kStatus_Success) goto finish;
        if (AD9959_StartupAccess(false, ch, eAD9959_Register_CFTW0,
            readWord.b8) != kStatus_Success) goto finish;
        AD9959_StartupCompare(ch, eAD9959_Register_CFTW0,
            readWord.b8, word.b8, fullMask);
    }
    if (log->errorCount != 0U) goto finish;
    AD9959_IoUpdate(coreClock_Hz);

    log->step = eAD9959_Startup_Clock;
    status_t status = AD9959_SetClockBlocking(AD9959_XTAL_FRQ_HZ,
        AD9959_SYSCLK_HZ, &log->clockConfig);
    if (status != kStatus_Success)
    {
        AD9959_StartupError(status, AD9959_STARTUP_NO_CHANNEL,
            eAD9959_Register_FR1, AD9959_STARTUP_NO_BYTE, 0U, 0U, 0U);
        goto finish;
    }
    AD9959_IoUpdate(coreClock_Hz);
    SDK_DelayAtLeastUs(AD9959_STARTUP_PLL_WAIT_US, coreClock_Hz);
    AD9959_clockConfig = log->clockConfig;
    fr1 = expected.FR1;
    fr1.bits.ePllDividerRatio = (ePllDividerRatio_t)log->clockConfig.pllMultiplier;
    fr1.bits.eVcoGainControl = log->clockConfig.eVcoGainControl;
    log->step = eAD9959_Startup_ClockCompare;
    if (AD9959_StartupAccess(false, AD9959_STARTUP_NO_CHANNEL,
        eAD9959_Register_FR1, readFr1.b8) != kStatus_Success) goto finish;
    AD9959_StartupCompare(AD9959_STARTUP_NO_CHANNEL, eAD9959_Register_FR1,
        readFr1.b8, fr1.b8, fullMask);
    if (log->errorCount != 0U) goto finish;
    log->pllConfigurationVerified = true;

    if (powerDownAtEnd)
    {
        log->step = eAD9959_Startup_PowerDown;
        /* Known reset CFR, with phase clear released and both power-downs set. */
        standbyCfr = expected.CFR;
        standbyCfr.bits.eDacPowerDown = ePowerDown_Enabled;
        standbyCfr.bits.eDigitalPowerDown = ePowerDown_Enabled;
        csr = AD9959_MakeStartupCsr(AD9959_CHANNEL_LOCAL_OSC);
        csr.bits.eChannel0Enable = eChannel_Enabled;
        csr.bits.eChannel1Enable = eChannel_Enabled;
        csr.bits.eChannel2Enable = eChannel_Enabled;
        csr.bits.eChannel3Enable = eChannel_Enabled;
        if (AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
            eAD9959_Register_CSR, &csr.b8) != kStatus_Success) goto finish;
        if (AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
            eAD9959_Register_CFR, standbyCfr.b8) != kStatus_Success) goto finish;
        /* Leave the reference input and PLL enabled. */
        fr1.bits.eDacReferencePowerDown = ePowerDown_Enabled;
        fr1.bits.eSyncClkDisable = eSyncClkDisable_Asserted;
        if (AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
            eAD9959_Register_FR1, fr1.b8) != kStatus_Success) goto finish;
        AD9959_IoUpdate(coreClock_Hz);
        log->step = eAD9959_Startup_PowerDownCompare;
        if (AD9959_StartupAccess(false, AD9959_STARTUP_NO_CHANNEL,
            eAD9959_Register_FR1, readFr1.b8) != kStatus_Success) goto finish;
        AD9959_StartupCompare(AD9959_STARTUP_NO_CHANNEL, eAD9959_Register_FR1,
            readFr1.b8, fr1.b8, fullMask);
        for (uint8_t ch = 0U; ch < eAD9959_ChannelCount; ++ch)
        {
            AD9959_CFR_t readCfr;
            csr = AD9959_MakeStartupCsr(ch);
            if (AD9959_StartupAccess(true, ch, eAD9959_Register_CSR,
                &csr.b8) != kStatus_Success) goto finish;
            if (AD9959_StartupAccess(false, ch, eAD9959_Register_CFR,
                readCfr.b8) != kStatus_Success) goto finish;
            AD9959_StartupCompare(ch, eAD9959_Register_CFR,
                readCfr.b8, standbyCfr.b8, fullMask);
        }
        if (log->errorCount != 0U) goto finish;
        log->powerDownVerified = true;
    }

    log->step = eAD9959_Startup_FinalSelection;
    csr = AD9959_MakeStartupCsr(AD9959_CHANNEL_LOCAL_OSC);
    if (AD9959_StartupAccess(true, AD9959_STARTUP_NO_CHANNEL,
        eAD9959_Register_CSR, &csr.b8) != kStatus_Success) goto finish;
    if (AD9959_StartupAccess(false, AD9959_STARTUP_NO_CHANNEL,
        eAD9959_Register_CSR, &readCsr.b8) != kStatus_Success) goto finish;
    AD9959_StartupCompare(AD9959_STARTUP_NO_CHANNEL, eAD9959_Register_CSR,
        &readCsr.b8, &csr.b8, fullMask);
    if (log->errorCount == 0U) log->step = eAD9959_Startup_Done;

finish:
    log->completed = true; /* Attempt ended; successful distinguishes failure. */
    log->successful = (log->errorCount == 0U);
    return log->status;
}










/*
 * Prepare channel power states and frequencies, then apply them together.
 * Leaves only LOCAL_OSC selected for subsequent frequency updates.
 * Requires a successfully configured DDS clock and PWR_DWN_CTL low.
 * On failure, does not generate I/O_UPDATE; staged writes are not rolled back.
 */
static status_t AD9959_StartModeBlocking(uint32_t coreClock_Hz, uint32_t loFrequency_Hz, uint32_t rfFrequency_Hz,
		bool autotest)
{
	status_t status;
	uint32_t validationWord;

	if (coreClock_Hz == 0U) {
		return kStatus_InvalidArgument;
	}

	if (AD9959_transferContext.pending) {
		return kStatus_DSPI_Busy;
	}

	/* Validate all requested frequencies before modifying registers. */
	status = AD9959_CalculateFrequencyWord(loFrequency_Hz,
			AD9959_clockConfig.actualSysclk_Hz, &validationWord);

	if (status != kStatus_Success) {
		return status;
	}

	if (autotest) {
		status = AD9959_CalculateFrequencyWord(rfFrequency_Hz,
				AD9959_clockConfig.actualSysclk_Hz, &validationWord);

		if (status != kStatus_Success) {
			return status;
		}
	}

	/* Stage the required channel power states. */
	status = autotest ?
					AD9959_PowerUpAutotestBlocking() :
					AD9959_PowerUpLoBlocking();

	if (status != kStatus_Success) {
		return status;
	}

	if (autotest) {
		/* Prepare RF input first. */
		status = AD9959_SelectChannelBlocking( AD9959_CHANNEL_RF_INP, NULL );

		if (status != kStatus_Success) {
			return status;
		}

		status = AD9959_SetFrequencySelected( rfFrequency_Hz, true );

		if (status != kStatus_Success) {
			return status;
		}
	}

	/* Prepare LO last and leave it selected. */
	status = AD9959_SelectChannelBlocking( AD9959_CHANNEL_LOCAL_OSC, NULL );

	if (status != kStatus_Success) {
		return status;
	}

	status = AD9959_SetFrequencySelected(loFrequency_Hz, true);

	if (status != kStatus_Success) {
		return status;
	}

	/* Apply power states and frequencies with one update pulse. */
	AD9959_IoUpdate(coreClock_Hz);

	return kStatus_Success;
}

/*
 * Start LO and RF input tones.
 * Leaves only LOCAL_OSC selected.
 */
status_t AD9959_StartAutotestBlocking(uint32_t coreClock_Hz, uint32_t loFrequency_Hz, uint32_t rfFrequency_Hz )
{
	return AD9959_StartModeBlocking(coreClock_Hz, loFrequency_Hz, rfFrequency_Hz, true ) ;
}

/*
 * Start the LO tone and power down all other channels.
 * Leaves only LOCAL_OSC selected.
 */
status_t AD9959_StartLoBlocking(uint32_t coreClock_Hz, uint32_t loFrequency_Hz)
{
	return AD9959_StartModeBlocking(coreClock_Hz, loFrequency_Hz, 0U, false );
}

/*
 * Put all channels into standby and apply the configuration.
 * Keeps the reference clock and PLL running.
 * Disables all DACs, digital cores, DAC reference and SYNC_CLK output.
 * Uses the common single-tone CFR configuration.
 * Preserves frequency words, amplitude settings and original CSR selection.
 * Requires PWR_DWN_CTL low and no concurrent DDS accesses.
 */
status_t AD9959_PowerDownBlocking(void)
{
    AD9959_CSR_t savedCsr;
    AD9959_CSR_t csr;
    AD9959_FR1_t fr1;
    AD9959_CFR_t cfr = { 0 };

    status_t status;
    status_t restoreStatus;

    const uint8_t csrAddress =
        AD9959_registerTable[eAD9959_Register_CSR].address;

    const uint8_t fr1Address =
        AD9959_registerTable[eAD9959_Register_FR1].address;

    const uint8_t cfrAddress =
        AD9959_registerTable[eAD9959_Register_CFR].address;

    const uint32_t coreClock_Hz =
        CLOCK_GetFreq(kCLOCK_CoreSysClk);

    if (AD9959_transferContext.pending)
    {
        return kStatus_DSPI_Busy;
    }

    if (coreClock_Hz == 0U)
    {
        return kStatus_InvalidArgument;
    }

    status = AD9959_ReadRegisterBlocking(csrAddress, &savedCsr.b8);
    if (status != kStatus_Success)
    {
        return status;
    }

    status = AD9959_ReadRegisterBlocking(fr1Address, fr1.b8);
    if (status != kStatus_Success)
    {
        return status;
    }

    /* Common single-tone configuration with both power-downs asserted. */
    cfr.bits.sp0 = 0U;
    cfr.bits.sp1 = 0U;
    cfr.bits.sp2 = 0U;

    cfr.bits.eAfpSelect = eAfpSelect_Disabled;
    cfr.bits.eLinearSweepEnable = eLinearSweep_Disabled;
    cfr.bits.eLinearSweepNoDwell = eLinearSweepNoDwell_Disabled;
    cfr.bits.eLoadSrrAtIoUpdate = eLoadSrrAtIoUpdate_Disabled;

    cfr.bits.eDacFullScaleCurrentControl = eDacFullScaleCurrent_Full;
    cfr.bits.eSineWaveOutputEnable = eSineWaveOutput_Enabled;
    cfr.bits.eMatchedPipeDelays = eMatchedPipeDelays_Disabled;

    cfr.bits.eClearPhaseAccumulator = eAccumulatorClear_DeAsserted;
    cfr.bits.eAutoclearPhaseAccumulator = eAccumulatorAutoclear_Disabled;
    cfr.bits.eClearSweepAccumulator = eAccumulatorClear_DeAsserted;
    cfr.bits.eAutoclearSweepAccumulator = eAccumulatorAutoclear_Disabled;

    cfr.bits.eDacPowerDown = ePowerDown_Enabled;
    cfr.bits.eDigitalPowerDown = ePowerDown_Enabled;

    /* Keep the reference clock active and preserve PLL settings. */
    fr1.bits.eReferenceClockInputPowerDown = ePowerDown_Disabled;
    fr1.bits.eDacReferencePowerDown = ePowerDown_Enabled;
    fr1.bits.eSyncClkDisable = eSyncClkDisable_Asserted;

    /* Select all channels while preserving the serial-port settings. */
    csr = savedCsr;
    csr.bits.eChannel0Enable = eChannel_Enabled;
    csr.bits.eChannel1Enable = eChannel_Enabled;
    csr.bits.eChannel2Enable = eChannel_Enabled;
    csr.bits.eChannel3Enable = eChannel_Enabled;

    status = AD9959_WriteRegisterBlocking(csrAddress, &csr.b8);
    if (status != kStatus_Success)
    {
        goto restore_csr;
    }

    status = AD9959_WriteRegisterBlocking(cfrAddress, cfr.b8);
    if (status != kStatus_Success)
    {
        goto restore_csr;
    }

    status = AD9959_WriteRegisterBlocking(fr1Address, fr1.b8);

restore_csr:
    restoreStatus =
        AD9959_WriteRegisterBlocking( csrAddress, &savedCsr.b8 );

    if (status != kStatus_Success)
    {
        return status;
    }

    if (restoreStatus != kStatus_Success)
    {
        return restoreStatus;
    }

    /* Apply standby only after all writes have succeeded. */
    AD9959_IoUpdate(coreClock_Hz);

    return kStatus_Success;
}


