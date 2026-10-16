/* LA_DRV8912_drv.h - DRV8912-Q1 relay output driver. */
#ifndef LA_DRV8912_DRV_H_
#define LA_DRV8912_DRV_H_

/* ------------------------ I N C L U D E    F I L E S ------------------------ */
#include "SPIS_drv.h"
#include <stdbool.h>
#include <stdint.h>

/* ------------------- G L O B A L   D E F I N I T I O N S -------------------- */
#define DRV8912_OUTPUT_COUNT       12U
#define DRV8912_OUTPUT_MASK        0x0FFFU
#define DRV8912_DEVICE_COUNT       2U
#define DRV8912_REGISTER_COUNT     0x25U

/* TI SLVSEC9C, section 8.6.1: addresses 0x1C..0x23 are reserved. */
typedef enum DRV8912_register_t
{
    eDRV8912_IC_STAT = 0x00,
    eDRV8912_OCP_STAT_1 = 0x01, eDRV8912_OCP_STAT_2 = 0x02,
    eDRV8912_OCP_STAT_3 = 0x03,
    eDRV8912_OLD_STAT_1 = 0x04, eDRV8912_OLD_STAT_2 = 0x05,
    eDRV8912_OLD_STAT_3 = 0x06,
    eDRV8912_CONFIG_CTRL = 0x07,
    eDRV8912_OP_CTRL_1 = 0x08, eDRV8912_OP_CTRL_2 = 0x09,
    eDRV8912_OP_CTRL_3 = 0x0A,
    eDRV8912_PWM_CTRL_1 = 0x0B, eDRV8912_PWM_CTRL_2 = 0x0C,
    eDRV8912_FW_CTRL_1 = 0x0D, eDRV8912_FW_CTRL_2 = 0x0E,
    eDRV8912_PWM_MAP_CTRL_1 = 0x0F, eDRV8912_PWM_MAP_CTRL_2 = 0x10,
    eDRV8912_PWM_MAP_CTRL_3 = 0x11,
    eDRV8912_PWM_FREQ_CTRL = 0x12,
    eDRV8912_PWM_DUTY_CTRL_1 = 0x13, eDRV8912_PWM_DUTY_CTRL_2 = 0x14,
    eDRV8912_PWM_DUTY_CTRL_3 = 0x15, eDRV8912_PWM_DUTY_CTRL_4 = 0x16,
    eDRV8912_SR_CTRL_1 = 0x17, eDRV8912_SR_CTRL_2 = 0x18,
    eDRV8912_OLD_CTRL_1 = 0x19, eDRV8912_OLD_CTRL_2 = 0x1A,
    eDRV8912_OLD_CTRL_3 = 0x1B, eDRV8912_OLD_CTRL_4 = 0x24
} DRV8912_register_t;

/* Two bits per output: LS_EN at bit 0, HS_EN at bit 1. */
typedef enum eDRV8912_outputState_t
{
    eDRV8912_Output_HighImpedance = 0,
    eDRV8912_Output_Low = 1,
    eDRV8912_Output_High = 2
} eDRV8912_outputState_t;

/*
 * SPI status byte returned as the first byte of every read/write response.
 * Its bit layout differs from the IC_STAT register.
 *
 * Fault flags report device diagnostic status; they do not confirm that
 * a register write succeeded or that a relay changed state.
 *
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_SPI_STATUS_t
{
    uint8_t b8; /* Complete SPI status byte. */

    struct __attribute__((packed))
    {
        uint8_t NPOR  : 1; /* Bit 0: 0 = power-on reset reported;
                           *        1 = reset indication cleared. */
        uint8_t OVP   : 1; /* Bit 1: 1 = overvoltage reported. */
        uint8_t UVLO  : 1; /* Bit 2: 1 = undervoltage lockout reported. */
        uint8_t OCP   : 1; /* Bit 3: 1 = overcurrent reported;
                           *        see OCP_STAT_1..3 for branch details. */
        uint8_t OLD   : 1; /* Bit 4: 1 = open load reported;
                           *        see OLD_STAT_1..3 for branch details. */
        uint8_t OT    : 1; /* Bit 5: combined thermal indication:
                           *        overtemperature warning or shutdown. */
        uint8_t fixed : 2; /* Bits 7:6: fixed binary 11 (value 3);
                           * basic response-format check, not a CRC. */
    } bits;
} DRV8912_SPI_STATUS_t;

typedef char DRV8912_SPI_STATUS_SizeCheck[
    (sizeof(DRV8912_SPI_STATUS_t) == 1) ? 1 : -1
];


/*
 * IC_STAT register (address 0x00): general device diagnostic status.
 * Its bit layout differs from the SPI status byte.
 *
 * OCP and OLD summarize branch faults; use OCP_STAT_1..3 and
 * OLD_STAT_1..3 to identify the affected output and power branch.
 *
 * NPOR has inverted polarity and is not a fault flag.
 * DRV8912_IC_FAULT_MASK (0x7E) selects bits 1..6 only.
 *
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_IC_STATUS_t
{
    uint8_t b8; /* Complete IC_STAT register value. */

    struct __attribute__((packed))
    {
        uint8_t NPOR : 1; /* Bit 0: 0 = power-on reset reported;
                           *        1 = reset indication cleared. */
        uint8_t OVP  : 1; /* Bit 1: 1 = overvoltage reported. */
        uint8_t UVLO : 1; /* Bit 2: 1 = undervoltage lockout reported. */
        uint8_t OCP  : 1; /* Bit 3: 1 = overcurrent reported;
                           *        see OCP_STAT_1..3 for branch details. */
        uint8_t OLD  : 1; /* Bit 4: 1 = open load reported;
                           *        see OLD_STAT_1..3 for branch details. */
        uint8_t OTW  : 1; /* Bit 5: 1 = overtemperature warning reported. */
        uint8_t OTSD : 1; /* Bit 6: 1 = overtemperature shutdown reported. */
        uint8_t sp7  : 1; /* Bit 7: reserved; exclude from status checks. */
    } bits;
} DRV8912_IC_STATUS_t;

typedef char DRV8912_IC_STATUS_SizeCheck[
    (sizeof(DRV8912_IC_STATUS_t) == 1) ? 1 : -1
];


/*
 * Overcurrent status from three consecutive registers:
 * b8[0] = OCP_STAT_1 (0x01): OUT1..OUT4.
 * b8[1] = OCP_STAT_2 (0x02): OUT5..OUT8.
 * b8[2] = OCP_STAT_3 (0x03): OUT9..OUT12.
 *
 * Each output has two diagnostic flags:
 * LS = low-side power branch, connecting the output to ground.
 * HS = high-side power branch, connecting the output to VM.
 *
 * A flag set to 1 reports overcurrent on the corresponding branch.
 * These flags do not contain measured current values.
 *
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_OCP_STATUS_t
{
    uint8_t b8[3]; /* Raw values of OCP_STAT_1..3, in register order. */

    struct __attribute__((packed))
    {
        /* Byte 0: OCP_STAT_1. */
        uint8_t OUT1_LS  : 1; /* Bit 0: OUT1 low-side overcurrent. */
        uint8_t OUT1_HS  : 1; /* Bit 1: OUT1 high-side overcurrent. */
        uint8_t OUT2_LS  : 1; /* Bit 2: OUT2 low-side overcurrent. */
        uint8_t OUT2_HS  : 1; /* Bit 3: OUT2 high-side overcurrent. */
        uint8_t OUT3_LS  : 1; /* Bit 4: OUT3 low-side overcurrent. */
        uint8_t OUT3_HS  : 1; /* Bit 5: OUT3 high-side overcurrent. */
        uint8_t OUT4_LS  : 1; /* Bit 6: OUT4 low-side overcurrent. */
        uint8_t OUT4_HS  : 1; /* Bit 7: OUT4 high-side overcurrent. */

        /* Byte 1: OCP_STAT_2. */
        uint8_t OUT5_LS  : 1; /* Bit 0: OUT5 low-side overcurrent. */
        uint8_t OUT5_HS  : 1; /* Bit 1: OUT5 high-side overcurrent. */
        uint8_t OUT6_LS  : 1; /* Bit 2: OUT6 low-side overcurrent. */
        uint8_t OUT6_HS  : 1; /* Bit 3: OUT6 high-side overcurrent. */
        uint8_t OUT7_LS  : 1; /* Bit 4: OUT7 low-side overcurrent. */
        uint8_t OUT7_HS  : 1; /* Bit 5: OUT7 high-side overcurrent. */
        uint8_t OUT8_LS  : 1; /* Bit 6: OUT8 low-side overcurrent. */
        uint8_t OUT8_HS  : 1; /* Bit 7: OUT8 high-side overcurrent. */

        /* Byte 2: OCP_STAT_3. */
        uint8_t OUT9_LS  : 1; /* Bit 0: OUT9 low-side overcurrent. */
        uint8_t OUT9_HS  : 1; /* Bit 1: OUT9 high-side overcurrent. */
        uint8_t OUT10_LS : 1; /* Bit 2: OUT10 low-side overcurrent. */
        uint8_t OUT10_HS : 1; /* Bit 3: OUT10 high-side overcurrent. */
        uint8_t OUT11_LS : 1; /* Bit 4: OUT11 low-side overcurrent. */
        uint8_t OUT11_HS : 1; /* Bit 5: OUT11 high-side overcurrent. */
        uint8_t OUT12_LS : 1; /* Bit 6: OUT12 low-side overcurrent. */
        uint8_t OUT12_HS : 1; /* Bit 7: OUT12 high-side overcurrent. */
    } bits;
} DRV8912_OCP_STATUS_t;

typedef char DRV8912_OCP_STATUS_SizeCheck[
    (sizeof(DRV8912_OCP_STATUS_t) == 3U) ? 1 : -1
];
typedef char DRV8912_OCP_STATUS_SizeCheck[
    (sizeof(DRV8912_OCP_STATUS_t) == 3) ? 1 : -1
];

/* Open-load flags: 1 = open load reported on the corresponding branch. */
typedef union DRV8912_OLD_STATUS_t
{
    uint8_t b8[3];

    struct __attribute__((packed))
    {
        /* Byte 0: OLD_STAT_1. */
        uint8_t OUT1_LS  : 1;
        uint8_t OUT1_HS  : 1;
        uint8_t OUT2_LS  : 1;
        uint8_t OUT2_HS  : 1;
        uint8_t OUT3_LS  : 1;
        uint8_t OUT3_HS  : 1;
        uint8_t OUT4_LS  : 1;
        uint8_t OUT4_HS  : 1;

        /* Byte 1: OLD_STAT_2. */
        uint8_t OUT5_LS  : 1;
        uint8_t OUT5_HS  : 1;
        uint8_t OUT6_LS  : 1;
        uint8_t OUT6_HS  : 1;
        uint8_t OUT7_LS  : 1;
        uint8_t OUT7_HS  : 1;
        uint8_t OUT8_LS  : 1;
        uint8_t OUT8_HS  : 1;

        /* Byte 2: OLD_STAT_3. */
        uint8_t OUT9_LS  : 1;
        uint8_t OUT9_HS  : 1;
        uint8_t OUT10_LS : 1;
        uint8_t OUT10_HS : 1;
        uint8_t OUT11_LS : 1;
        uint8_t OUT11_HS : 1;
        uint8_t OUT12_LS : 1;
        uint8_t OUT12_HS : 1;
    } bits;
} DRV8912_OLD_STATUS_t;

typedef char DRV8912_OLD_STATUS_SizeCheck[
    (sizeof(DRV8912_OLD_STATUS_t) == 3U) ? 1 : -1
];


#define DRV8912_CONFIG_CLEAR_FAULTS  0x01U
#define DRV8912_CONFIG_EXT_OVP       0x02U
#define DRV8912_CONFIG_OTW_REPORT    0x04U
#define DRV8912_CONFIG_OCP_NO_REPORT 0x08U
#define DRV8912_IC_NPOR              0x01U /* 0 = POR occurred. */
#define DRV8912_IC_OVP               0x02U
#define DRV8912_IC_UVLO              0x04U
#define DRV8912_IC_OCP               0x08U
#define DRV8912_IC_OLD               0x10U
#define DRV8912_IC_OTW               0x20U
#define DRV8912_IC_OTSD              0x40U
#define DRV8912_IC_FAULT_MASK        0x7EU /* NPOR is not a fault flag. */

/* SPI status header differs from the IC_STAT register: bits [7:6] = 11,
 * bit 5 combines the thermal flags. Keep the two representations separate. */
typedef struct DRV8912_response_t
{
	DRV8912_SPI_STATUS_t spiStatus;
    uint8_t registerData; /* Writes return the PREVIOUS register value. */
} DRV8912_response_t;

typedef struct DRV8912_faults_t
{
	DRV8912_IC_STATUS_t icStatus;
	DRV8912_OCP_STATUS_t overcurrent;
	DRV8912_OLD_STATUS_t  openLoad;
} DRV8912_faults_t;

/* Optional board hook: high = awake; low = sleep. The pin is also called
 * ENABLE in portions of the TI documentation. Configure its GPIO first.
 * NULL means externally held high; true sleep is then unavailable. */
typedef void (*DRV8912_setEnable_t)(bool enabled, void *userData);

typedef struct DRV8912_deviceConfig_t
{
    SPI_Type *base;
    uint32_t transferFlags;
    DRV8912_setEnable_t setEnable;
    void *userData;
} DRV8912_deviceConfig_t;

typedef struct DRV8912_startupLog_t
{
    status_t result;
    uint8_t deviceId;
    uint8_t registersBefore[DRV8912_REGISTER_COUNT];
    DRV8912_faults_t faultsAfter;
} DRV8912_startupLog_t;

/* One persistent context per physical IC. Do not copy or reinitialize a
 * context while busy. All APIs are intended for foreground use. */
typedef struct DRV8912_device_t
{
    DRV8912_deviceConfig_t config;
    DRV8912_startupLog_t startupLog;
    volatile bool busy;
    volatile bool asyncPending;
    volatile bool asyncCompleted;
    status_t asyncStatus;
    uint8_t txData[2];
    uint8_t rxData[2];
    /* Foreground sequencer: write/read-back for each OP_CTRL register. */
    bool maskOperation;
    bool maskSubmitPending;
    uint8_t maskStep;
    uint8_t maskValues[3];
} DRV8912_device_t;

extern const DRV8912_deviceConfig_t DRV8912_DefaultConfigs[DRV8912_DEVICE_COUNT];

/* Call on a fresh zero-initialized context, once, before using the device. */
status_t DRV8912_InitContext(DRV8912_device_t *device,
                            const DRV8912_deviceConfig_t *config);

/* Initialize for static relay control: ID + register snapshot, Hi-Z outputs,
 * PWM off, active OLD off (appropriate for pulse/capacitive loads), fault
 * clear and final diagnostic check. Does not generate output test pulses.
 * extOvp must be true for a nominal 24 V supply. The enable hook, if present,
 * is driven high; this procedure does NOT force a hardware cold reset. */
status_t DRV8912_StartBlocking(DRV8912_device_t *device,
                              uint32_t coreClock_Hz, bool extOvp);
status_t DRV8912_ReadRegisterBlocking(DRV8912_device_t *device,
    DRV8912_register_t address, DRV8912_response_t *response);
status_t DRV8912_WriteRegisterBlocking(DRV8912_device_t *device,
    DRV8912_register_t address, uint8_t data, DRV8912_response_t *response);

/* One 16-clock message using two SDK 8-bit frames with CS held low.
 * Context and buffers remain owned until GetTransferResult consumes the
 * completion. kStatus_Success means submitted, not completed. */
status_t DRV8912_ReadRegisterNonBlocking(DRV8912_device_t *device,
                                        DRV8912_register_t address);
status_t DRV8912_WriteRegisterNonBlocking(DRV8912_device_t *device,
                                         DRV8912_register_t address, uint8_t data);
/* Nonblocking mask service. Success means accepted. Poll GetTransferResult
 * in the foreground: it advances three writes and three read-back checks,
 * returning true only when the whole operation ends. No new command on this
 * context is allowed until that result is consumed. Partial updates are
 * possible on failure; output groups are not simultaneous.
 */
status_t DRV8912_SetOutputMasksNonBlocking(DRV8912_device_t *device,
                                          uint16_t highMask, uint16_t lowMask);
status_t DRV8912_ApplyRelayMaskNonBlocking(DRV8912_device_t *device, uint16_t mask);
status_t DRV8912_AllHighImpedanceNonBlocking(DRV8912_device_t *device);

bool DRV8912_GetTransferResult(DRV8912_device_t *device,
    status_t *status, DRV8912_response_t *response);

/* OUT1..OUT12 use indices 1..12; masks use bit 0..11 respectively.
 * A bit in neither mask means Hi-Z; overlap is rejected. Each group of four
 * outputs changes in one transaction. Three groups are NOT simultaneous;
 * failure can leave a partially applied combination. */
status_t DRV8912_SetOutputBlocking(DRV8912_device_t *device,
    uint8_t output, eDRV8912_outputState_t state);
status_t DRV8912_SetOutputsBlocking(DRV8912_device_t *device,
                                       uint16_t highMask, uint16_t lowMask);
/* Drives selected bits high and the other 12 bits low. It generates no
 * timed pulse and does not define the relay-to-L/C mapping. */
status_t DRV8912_ApplyRelayMaskBlocking(DRV8912_device_t *device, uint16_t mask);
status_t DRV8912_AllHighImpedanceBlocking(DRV8912_device_t *device);
status_t DRV8912_ReadFaultsBlocking(DRV8912_device_t *device, DRV8912_faults_t *faults);
status_t DRV8912_ClearFaultsBlocking(DRV8912_device_t *device);
/* Sleep requires the board hook; wake/reconfigure with StartBlocking. */
status_t DRV8912_SleepBlocking(DRV8912_device_t *device, uint32_t coreClock_Hz);

#endif /* LA_DRV8912_DRV_H_ */
