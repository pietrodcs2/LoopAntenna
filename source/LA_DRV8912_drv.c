
/*
 * ============================================================================
 * DRV8912-Q1 DRIVER - USER GUIDE
 * ============================================================================
 *
 * PURPOSE
 * -------
 * Controls the twelve half-bridge outputs of a DRV8912-Q1 through SPI.
 * Each output can be HIGH, LOW or high impedance (Hi-Z).
 *
 * HIGH enables the high-side power branch, connecting the output to VM.
 * LOW enables the low-side power branch, connecting the output to ground.
 * Hi-Z disables both power branches.
 *
 * The driver provides blocking register/output operations and interrupt-driven
 * nonblocking operations. Output commands set electrical states; they do not
 * generate timed relay pulses or confirm mechanical relay positions.
 *
 *
 * DEVICE SUPPORT AND CHIP SELECTION
 * ---------------------------------
 * The default configuration contains two devices:
 *
 *   DRV8912_DefaultConfigs[0]: SPI0, PCS2, mapped to PTC2.
 *   DRV8912_DefaultConfigs[1]: SPI0, PCS3, mapped to PTC1.
 *
 * These assignments come from LA_config.h and the board pin configuration.
 * Each device requires its own persistent DRV8912_device_t context.
 *
 * The device has no SPI slave address. Its chip-select signal selects the
 * physical chip; the address in a transaction selects a register inside it.
 * This driver uses independent chip selects, not daisy-chain communication.
 *
 * The two-device count describes the supplied default configurations.
 * Additional contexts/configurations can be supplied if hardware permits.
 *
 *
 * INITIALIZATION
 * --------------
 * Perform initialization in this order:
 *
 *   1. Configure the MCU clocks.
 *   2. Configure SPI pins with SPIs_InitPins().
 *   3. Initialize the shared SPI peripheral with SPIs_Init(SPI0).
 *   4. Initialize each device context:
 *
 *        DRV8912_InitContext(&devices[i], &DRV8912_DefaultConfigs[i]);
 *
 *   5. Configure each device:
 *
 *        DRV8912_StartBlocking(&devices[i], coreClock_Hz, true);
 *
 * Check every returned status before continuing.
 * Contexts should initially be zero-initialized and must remain allocated
 * throughout device use. Do not copy or reinitialize a busy context.
 *
 * InitContext copies the SPI peripheral, transfer flags, optional enable
 * callback and callback user data into the context. The default configuration
 * table is used only when explicitly passed to InitContext.
 *
 * StartBlocking optionally calls config.setEnable(true, config.userData)
 * to drive nSLEEP high, then waits for the wake-up settling time.
 * Default configurations have no enable callback: nSLEEP must therefore
 * be held high externally.
 *
 * StartBlocking checks the device ID and saves the initial register contents.
 * It does not compare them with the documented reset/default values.
 * It leaves all outputs in Hi-Z, disables output PWM and the internal PWM
 * generators, configures diagnostics/protection, clears fault indications,
 * and reads the final diagnostic registers.
 *
 * Configuration writes use read-back verification of the relevant bits.
 * The fault-clear command is issued separately.
 * Startup does not force a hardware reset or generate relay test pulses.
 *
 * coreClock_Hz is the actual MCU core frequency used for software delays.
 * extOvp=true selects the extended voltage range, suitable for nominal 24 V
 * operation; false selects the standard range.
 *
 *
 * STARTUP LOG
 * -----------
 * Each device context contains its own startupLog:
 *
 *   startupLog.result:
 *       Result of the startup procedure.
 *
 *   startupLog.deviceId:
 *       Device identification read from CONFIG_CTRL.
 *       The expected DRV8912-Q1 identification is zero.
 *
 *   startupLog.registersBefore[address]:
 *       Register contents captured before startup configuration writes.
 *       The array index is the register address.
 *       Reserved addresses are skipped.
 *
 *   startupLog.faultsAfter:
 *       Diagnostic registers read after configuration and fault clearing.
 *
 * If startup fails, the log may be incomplete. Entries not reached by the
 * procedure are not evidence of actual register values.
 * In particular, fault data is meaningful only if its read completed.
 *
 *
 * BLOCKING OUTPUT COMMANDS
 * ------------------------
 * Blocking functions finish their SPI operations before returning.
 *
 * DRV8912_SetOutputBlocking(device, output, state):
 *     Changes one output, numbered 1..12.
 *     Reads its OP_CTRL register, modifies only that output's two-bit field,
 *     then writes and verifies the result. Other output fields are preserved.
 *
 * DRV8912_SetOutputsBlocking(device, highMask, lowMask):
 *     Sets all twelve outputs.
 *     Mask bit 0 corresponds to OUT1; bit 11 corresponds to OUT12.
 *     A bit in highMask selects HIGH.
 *     A bit in lowMask selects LOW.
 *     A bit in neither mask selects Hi-Z.
 *     Masks must not overlap or use bits outside the twelve-output range.
 *
 * DRV8912_ApplyRelayMaskBlocking(device, mask):
 *     Convenience wrapper for binary relay control.
 *     A set bit selects HIGH; a clear bit selects LOW.
 *     No output is left in Hi-Z.
 *
 * DRV8912_SetAllHighImpedanceBlocking(device):
 *     Sets all twelve outputs to Hi-Z.
 *
 * All-output updates write and verify OP_CTRL_1, OP_CTRL_2 and OP_CTRL_3.
 * Each register controls four outputs. The three groups do not change
 * simultaneously, and an error can leave a partially applied configuration.
 *
 * Public output APIs acquire and release the device context automatically.
 * Internal Locked functions require the caller to own that context already.
 *
 *
 * NONBLOCKING OUTPUT COMMANDS
 * ---------------------------
 * DRV8912_SetOutputsNonBlocking(device, highMask, lowMask)
 * DRV8912_ApplyRelayMaskNonBlocking(device, mask)
 * DRV8912_SetAllHighImpedanceNonBlocking(device)
 *
 * These functions copy the requested states into the device context and
 * start the first interrupt-driven SPI transaction.
 *
 * kStatus_Success means the operation was accepted, NOT completed.
 * On submission failure, no operation remains pending for that request.
 *
 * After successful submission, repeatedly call from the main loop:
 *
 *   DRV8912_GetTransferResult(device, &result, NULL);
 *
 * This call advances the operation as well as collecting its final result.
 * The sequence consists of six SPI transactions:
 *
 *   Write OP_CTRL_1 -> Read and verify OP_CTRL_1
 *   Write OP_CTRL_2 -> Read and verify OP_CTRL_2
 *   Write OP_CTRL_3 -> Read and verify OP_CTRL_3
 *
 * QueueFrame submits one transaction.
 * TransferCompleted records its completion in interrupt context.
 * GetTransferResult processes that completion in the foreground and submits
 * the next transaction. It retries intermediate submission if the bus is busy.
 *
 * GetTransferResult returns false while no final result is available.
 * It returns true once when the complete operation ends, successfully or
 * unsuccessfully, and places the final status in the result parameter.
 *
 * The context remains reserved until the final result is consumed.
 * Do not submit another command on that context before completion.
 * Keep contexts and their buffers valid throughout the operation.
 *
 *
 * REGISTER ACCESS AND OUTPUT READ-BACK
 * -----------------------------------
 * DRV8912_ReadRegisterBlocking(device, address, &response):
 *     Reads a register synchronously.
 *
 * DRV8912_WriteRegisterBlocking(device, address, data, &response):
 *     Writes a register synchronously.
 *     This basic function does not perform a separate read-back verification.
 *     The response pointer may be NULL when returned bytes are not needed.
 *
 * The response contains:
 *
 *   response.spiStatus.b8:
 *       SPI status byte returned during the transaction.
 *
 *   response.registerData:
 *       Addressed register value for reads.
 *       PREVIOUS register value for writes.
 *
 * To read the configured output states, read OP_CTRL_1..3.
 * Each register contains four two-bit fields, starting at bits 1:0:
 *
 *   00 = Hi-Z; 01 = LOW; 10 = HIGH.
 *
 * This reads the commanded configuration, not the measured output voltage
 * or the mechanical state of a relay.
 *
 * Nonblocking register access is available through:
 *
 *   DRV8912_ReadRegisterNonBlocking(device, address)
 *   DRV8912_WriteRegisterNonBlocking(device, address, data)
 *
 * After successful submission, poll GetTransferResult with a response pointer:
 *
 *   DRV8912_GetTransferResult(device, &result, &response);
 *
 * Use response contents only when the call returns true and result is
 * kStatus_Success. A nonblocking register write has no automatic read-back
 * verification; the nonblocking output-state service does perform it.
 *
 *
 * DIAGNOSTIC STATUS
 * -----------------
 * Every SPI read/write returns a summary status byte.
 * It contains NPOR, OVP, UVLO, OCP, OLD and the combined thermal flag OT.
 * Bits 7:6 are fixed at binary 11 and provide a basic response-format check.
 * Passing this check does not prove that communication is fault-free,
 * that no device faults exist, or that a write succeeded.
 *
 * DRV8912_ReadFaultsBlocking(device, &faults) reads seven diagnostic registers:
 *
 *   faults.icStatus:
 *       IC_STAT, with separate thermal warning and shutdown flags.
 *
 *   faults.overcurrent.b8[0..2]:
 *       OCP_STAT_1..3, with separate LS/HS flags for every output.
 *
 *   faults.openLoad.b8[0..2]:
 *       OLD_STAT_1..3, with separate LS/HS flags for every output.
 *
 * Named flags are accessible through the corresponding .bits members,
 * for example faults.overcurrent.bits.OUT5_HS.
 *
 * The destination is updated only if all seven reads succeed.
 * kStatus_Success means the diagnostic read succeeded, even if flags are set.
 * The registers are read sequentially, not as a simultaneous snapshot.
 *
 * DRV8912_ReadFaultsBlocking does not clear fault flags.
 * DRV8912_ClearFaultsBlocking issues the fault-clear command.
 * An ongoing fault condition may prevent clearing or cause flags to reassert.
 *
 * There is no combined seven-register nonblocking diagnostic reader here.
 * Individual diagnostic registers can be read with ReadRegisterNonBlocking.
 *
 * Startup disables open-load detection for the intended pulse/capacitive
 * loads. A clear OLD flag therefore does not prove that a relay is connected.
 *
 *
 * SHARED BUS AND EXECUTION CONTEXT
 * --------------------------------
 * Foreground code should call the public APIs and poll GetTransferResult.
 * Interrupt callbacks record completion; they do not perform the full
 * multi-register output update.
 *
 * The shared SPI layer arbitrates access and applies each device's SPI
 * settings before a transaction. The device context lock protects the
 * logical operation; it does not reserve the bus between transactions.
 *
 * The completion callback includes the short device-required inter-access
 * delay. Nonblocking operation avoids waiting for the SPI transfer itself.
 *
 * Completion requires working SPI interrupts and foreground polling.
 * The asynchronous sequence currently has no automatic timeout.
 * ============================================================================
 */


/* LA_DRV8912_drv.c - TI DRV8912-Q1, independent chip selects (no daisy chain). */
/* ------------------------ I N C L U D E    F I L E S ------------------------ */
#include "LA_DRV8912_drv.h"
#include "LA_config.h"
#include "fsl_clock.h"
#include <string.h>

#define DRV8912_READ_COMMAND 0x40U
#define DRV8912_WRITE_GAP_US 3U /* tSC_SPI >= 2.5 us. */
#define DRV8912_READY_US 2000U  /* tREADY = 1 ms; conservative board margin. */

/* ------------------- L O C A L     D E F I N I T I O N S -------------------- */


/* PWM_CTRL_1 (0x0B): PWM enable for OUT1..OUT8.
 * Each bit: 0 = static control; 1 = PWM enabled.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_PWM_CTRL_1_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t OUT1_PWM_EN : 1; /* Bit 0: 1 = enable PWM on OUT1. */
        uint8_t OUT2_PWM_EN : 1; /* Bit 1: 1 = enable PWM on OUT2. */
        uint8_t OUT3_PWM_EN : 1; /* Bit 2: 1 = enable PWM on OUT3. */
        uint8_t OUT4_PWM_EN : 1; /* Bit 3: 1 = enable PWM on OUT4. */
        uint8_t OUT5_PWM_EN : 1; /* Bit 4: 1 = enable PWM on OUT5. */
        uint8_t OUT6_PWM_EN : 1; /* Bit 5: 1 = enable PWM on OUT6. */
        uint8_t OUT7_PWM_EN : 1; /* Bit 6: 1 = enable PWM on OUT7. */
        uint8_t OUT8_PWM_EN : 1; /* Bit 7: 1 = enable PWM on OUT8. */
    } bits;
} DRV8912_PWM_CTRL_1_t;

/* OLD_CTRL_1 (0x19): open-load detection control for OUT1..OUT8.
 * Each bit: 0 = detection enabled; 1 = detection disabled.
 * Writing 0xFF disables detection on all eight outputs.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_OLD_CTRL_1_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t OUT1_OLD_DIS : 1; /* Bit 0. */
        uint8_t OUT2_OLD_DIS : 1; /* Bit 1. */
        uint8_t OUT3_OLD_DIS : 1; /* Bit 2. */
        uint8_t OUT4_OLD_DIS : 1; /* Bit 3. */
        uint8_t OUT5_OLD_DIS : 1; /* Bit 4. */
        uint8_t OUT6_OLD_DIS : 1; /* Bit 5. */
        uint8_t OUT7_OLD_DIS : 1; /* Bit 6. */
        uint8_t OUT8_OLD_DIS : 1; /* Bit 7. */
    } bits;
} DRV8912_OLD_CTRL_1_t;


/* PWM_CTRL_2 (0x0C):
 * Bits 0..3: 0 = static control; 1 = PWM enabled on OUT9..OUT12.
 * Bits 4..7: 0 = PWM generator enabled; 1 = generator disabled.
 * Writing 0xF0 selects static control and disables all four generators.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_PWM_CTRL_2_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t OUT9_PWM_EN  : 1; /* Bit 0. */
        uint8_t OUT10_PWM_EN : 1; /* Bit 1. */
        uint8_t OUT11_PWM_EN : 1; /* Bit 2. */
        uint8_t OUT12_PWM_EN : 1; /* Bit 3. */
        uint8_t PWM_CH1_DIS  : 1; /* Bit 4. */
        uint8_t PWM_CH2_DIS  : 1; /* Bit 5. */
        uint8_t PWM_CH3_DIS  : 1; /* Bit 6. */
        uint8_t PWM_CH4_DIS  : 1; /* Bit 7. */
    } bits;
} DRV8912_PWM_CTRL_2_t;

/* OLD_CTRL_2 (0x1A):
 * Bits 0..3: 0 = open-load detection enabled; 1 = disabled.
 * Bits 4..5: parallel-mode overcurrent turn-off configuration.
 * Bit 6: 0 = disable affected outputs after OLD; 1 = keep them active.
 * Bit 7: 0 = report OLD on nFAULT; 1 = suppress reporting on nFAULT.
 * Writing 0x0F disables open-load detection on OUT9..OUT12.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_OLD_CTRL_2_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t OUT9_OLD_DIS  : 1; /* Bit 0. */
        uint8_t OUT10_OLD_DIS : 1; /* Bit 1. */
        uint8_t OUT11_OLD_DIS : 1; /* Bit 2. */
        uint8_t OUT12_OLD_DIS : 1; /* Bit 3. */
        uint8_t PL_MODE_EN   : 2; /* Bits 5:4: 00 = fast turn-off;
                                  *           01 = slow turn-off. */
        uint8_t OLD_OP       : 1; /* Bit 6: output operation after OLD. */
        uint8_t OLD_REP      : 1; /* Bit 7: OLD reporting on nFAULT. */
    } bits;
} DRV8912_OLD_CTRL_2_t;

/* OLD_CTRL_3 (0x1B):
 * Bits 0..3: 0 = low-current OLD disabled; 1 = enabled on OUT9..OUT12.
 * Bit 4: 0 = negative-current OLD disabled; 1 = enabled.
 * Bits 5..7: overcurrent deglitch time:
 *   000 = 10 us; 001 = 5 us; 010 = 2.5 us; 011 = 1 us;
 *   100 = 60 us; 101 = 40 us; 110 = 30 us; 111 = 20 us.
 * Writing 0x00 disables these OLD modes and selects 10 us OCP filtering.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_OLD_CTRL_3_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t OUT9_LOLD_EN  : 1; /* Bit 0. */
        uint8_t OUT10_LOLD_EN : 1; /* Bit 1. */
        uint8_t OUT11_LOLD_EN : 1; /* Bit 2. */
        uint8_t OUT12_LOLD_EN : 1; /* Bit 3. */
        uint8_t OLD_NEG_EN    : 1; /* Bit 4. */
        uint8_t OCP_DEG       : 3; /* Bits 7:5. */
    } bits;
} DRV8912_OLD_CTRL_3_t;

/* OLD_CTRL_4 (0x24): low-current open-load detection for OUT1..OUT8.
 * Each bit: 0 = detection disabled; 1 = detection enabled.
 * Writing 0x00 disables detection on all eight outputs.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_OLD_CTRL_4_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t OUT1_LOLD_EN : 1; /* Bit 0. */
        uint8_t OUT2_LOLD_EN : 1; /* Bit 1. */
        uint8_t OUT3_LOLD_EN : 1; /* Bit 2. */
        uint8_t OUT4_LOLD_EN : 1; /* Bit 3. */
        uint8_t OUT5_LOLD_EN : 1; /* Bit 4. */
        uint8_t OUT6_LOLD_EN : 1; /* Bit 5. */
        uint8_t OUT7_LOLD_EN : 1; /* Bit 6. */
        uint8_t OUT8_LOLD_EN : 1; /* Bit 7. */
    } bits;
} DRV8912_OLD_CTRL_4_t;

/* CONFIG_CTRL (0x07):
 * Bit 0: write 1 to clear fault flags.
 * Bit 1: 0 = standard OVP range; 1 = extended range (up to 32 V operation).
 * Bit 2: 0 = no OTW report on nFAULT; 1 = reporting enabled.
 * Bit 3: 0 = OCP report on nFAULT enabled; 1 = reporting disabled.
 * Bits 4..6: read-only device ID; 000 = DRV8912-Q1.
 * Bit 7: reserved; write 0.
 * Bitfield layout assumes the project's ARM GCC toolchain.
 */
typedef union DRV8912_CONFIG_CTRL_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        uint8_t CLR_FLT : 1; /* Bit 0: fault-clear command. */
        uint8_t EXT_OVP : 1; /* Bit 1: extended overvoltage range. */
        uint8_t OTW_REP : 1; /* Bit 2: thermal-warning reporting. */
        uint8_t OCP_REP : 1; /* Bit 3: overcurrent reporting suppression. */
        uint8_t IC_ID   : 3; /* Bits 6:4: device identification. */
        uint8_t sp7     : 1; /* Bit 7: reserved. */
    } bits;
} DRV8912_CONFIG_CTRL_t;


/* ------------------------- G L O B A L     D A T A ------------------------- */
const DRV8912_deviceConfig_t DRV8912_DefaultConfigs[DRV8912_DEVICE_COUNT] =
{
    { DRV8912_SPI, DRV8912_0_SPI_TRANSFER_FLAGS, NULL, NULL },
    { DRV8912_SPI, DRV8912_1_SPI_TRANSFER_FLAGS, NULL, NULL }
};

/* -------------------------- L O C A L     D A T A -------------------------- */
static const SPI_busConfig_t DRV8912_busConfig =
{
    .baudrate_Hz = DRV8912_SPI_BAUDRATE_HZ,
    .bitsPerFrame = 8U,
    .cpol = kDSPI_ClockPolarityActiveHigh, /* SDK name means idle LOW. */
    .cpha = kDSPI_ClockPhaseSecondEdge,   /* Sample on falling edge: mode 1. */
    .direction = kDSPI_MsbFirst,
    .pcsToSckDelay_ns = DRV8912_SPI_DELAY_NS,
    .lastSckToPcsDelay_ns = DRV8912_SPI_DELAY_NS,
    .betweenTransferDelay_ns = DRV8912_SPI_DELAY_NS
};

/* ---------------------- I M P L E M E N T A T I O N ------------------------ */
static bool DRV8912_ConfigValid(const DRV8912_deviceConfig_t *config)
{
    uint32_t allowed = DSPI_MASTER_PCS_MASK | (uint32_t)kDSPI_MasterPcsContinuous;
    return config != NULL &&
        (config->base == SPI0 || config->base == SPI1 || config->base == SPI2) &&
        (config->transferFlags & ~allowed) == 0U &&
        (config->transferFlags & kDSPI_MasterPcsContinuous) != 0U &&
        ((config->transferFlags & DSPI_MASTER_PCS_MASK) >> DSPI_MASTER_PCS_SHIFT) <= 5U;
}

static status_t DRV8912_Acquire(DRV8912_device_t *device)
{
    uint32_t primask;
    status_t status = kStatus_DSPI_Busy;
    if (device == NULL || !DRV8912_ConfigValid(&device->config))
        return kStatus_InvalidArgument;
    primask = __get_PRIMASK();
    __disable_irq();
    if (!device->busy) { device->busy = true; status = kStatus_Success; }
    __DMB();
    __set_PRIMASK(primask);
    return status;
}

static void DRV8912_Release(DRV8912_device_t *device)
{
    __DMB();
    device->busy = false;
}

static bool DRV8912_AddressValid(DRV8912_register_t address, bool write)
{
    uint32_t a = (uint32_t)address;
    return (a <= 0x1BU || a == 0x24U) && (!write || a >= 0x07U);
}

static bool DRV8912_WriteValid(DRV8912_register_t address, uint8_t data)
{
    if (!DRV8912_AddressValid(address, true)) return false;
    if ((address == eDRV8912_CONFIG_CTRL || address == eDRV8912_FW_CTRL_2 ||
         address == eDRV8912_SR_CTRL_2) && (data & 0xF0U) != 0U) return false;
    if (address >= eDRV8912_OP_CTRL_1 && address <= eDRV8912_OP_CTRL_3)
    {
        /* Reject simultaneous LS/HS enable requests. */
        if ((data & (data >> 1U) & 0x55U) != 0U) return false;
    }
    return true;
}

/**
 * @brief Validates the fixed SPI status bits and optionally copies the response.
 *
 * A DRV8912 SPI transaction receives two bytes:
 *
 * rx[0] - SPI status byte:
 *         Bit 7: always 1.
 *         Bit 6: always 1.
 *         Bit 5: OT   - combined thermal status.
 *         Bit 4: OLD  - open-load detection status.
 *         Bit 3: OCP  - overcurrent protection status.
 *         Bit 2: UVLO - undervoltage lockout status.
 *         Bit 1: OVP  - overvoltage protection status.
 *         Bit 0: NPOR - power-on-reset status, with inverted polarity.
 *
 * rx[1] - Register response:
 *         For a read command, the value of the addressed register.
 *         For a write command, the PREVIOUS value of the addressed register,
 *         before the requested write is applied.
 *
 * The function checks that bits 7 and 6 of rx[0] are both set, as required
 * by the DRV8912 SPI response format. If either bit is clear, the function
 * returns kStatus_Fail without modifying the response structure.
 *
 * This is only a basic response-format check. It rejects, for example,
 * an all-zero response, but cannot detect every communication failure:
 * an all-one response also passes this check. It is not a CRC check.
 *
 * If the fixed-bit check passes and response is not NULL, both received
 * bytes are copied without further interpretation.
 * If response is NULL, only the fixed-bit check is performed.
 *
 * Fault flags are NOT evaluated here. A response containing active fault
 * indications can therefore still produce kStatus_Success.
 * Note that this SPI status byte has a different bit layout from IC_STAT;
 * do not apply DRV8912_IC_FAULT_MASK directly to rx[0].
 *
 * The function does NOT verify that a write has updated the register.
 * Write verification requires a separate read-back transaction and a
 * comparison with the requested value, as done by DRV8912_WriteVerified().
 *
 * This function performs no SPI transfer and does not modify device state.
 *
 * @param rx
 *        Pointer to at least two valid received bytes.
 *        Must not be NULL; this internal function does not check the pointer.
 *
 * @param response
 *        Optional destination for the SPI status byte and register data.
 *        May be NULL when the caller only needs the format check.
 *
 * @return kStatus_Success
 *         The two fixed status bits are correct. This does not imply that
 *         no faults exist or that a register write was successful.
 *
 * @return kStatus_Fail
 *         One or both fixed status bits are incorrect.
 */
static status_t DRV8912_Decode(const uint8_t rx[2], DRV8912_response_t *response)
{
    /* A low/floating MISO must not masquerade as device ID zero. */
    if ((rx[0] & 0xC0U) != 0xC0U) return kStatus_Fail;
    if (response != NULL)
    {
        response->spiStatus.b8 = rx[0];
        response->registerData = rx[1];
    }
    return kStatus_Success;
}

/* Caller owns the device context; SPIs owns bus acquisition/release. */
static status_t DRV8912_Transfer(DRV8912_device_t *device, bool read,
    DRV8912_register_t address, uint8_t data, DRV8912_response_t *response)
{
    uint8_t tx[2] = { (uint8_t)((uint8_t)address | (read ? DRV8912_READ_COMMAND : 0U)), data };
    uint8_t rx[2] = { 0U, 0U };
    SPI_transaction_t transfer = { 0 };
    status_t status;
    transfer.base = device->config.base;
    transfer.config = &DRV8912_busConfig;
    transfer.txData = tx;
    transfer.rxData = rx;
    transfer.dataSize = sizeof(tx);
    transfer.configFlags = device->config.transferFlags;
    status = SPIs_TransferBlocking(&transfer);
    /* Delay on failure too: a partial SDK write may have reached the device. */
    SDK_DelayAtLeastUs(DRV8912_WRITE_GAP_US, CLOCK_GetFreq(kCLOCK_CoreSysClk));
    if (status == kStatus_Success) status = DRV8912_Decode(rx, response);
    return status;
}

status_t DRV8912_InitContext(DRV8912_device_t *device,
                            const DRV8912_deviceConfig_t *config)
{
    if (device == NULL || !DRV8912_ConfigValid(config)) return kStatus_InvalidArgument;
    /* Preserve config even if a caller passes &device->config. */
    DRV8912_deviceConfig_t copy = *config;
    memset(device, 0, sizeof(*device));
    device->config = copy;
    device->startupLog.result = kStatus_Fail;
    device->startupLog.deviceId = 0xFFU;
    return kStatus_Success;
}

status_t DRV8912_ReadRegisterBlocking(DRV8912_device_t *device,
    DRV8912_register_t address, DRV8912_response_t *response)
{
    status_t status;
    if (response == NULL || !DRV8912_AddressValid(address, false)) return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    status = DRV8912_Transfer(device, true, address, 0U, response);
    DRV8912_Release(device);
    return status;
}

status_t DRV8912_WriteRegisterBlocking(DRV8912_device_t *device,
    DRV8912_register_t address, uint8_t data, DRV8912_response_t *response)
{
    status_t status;
    if (!DRV8912_WriteValid(address, data)) return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    status = DRV8912_Transfer(device, false, address, data, response);
    DRV8912_Release(device);
    return status;
}

/* Runs in IRQ context. No new operation on this context is allowed before
 * the result is consumed; the SDK bus lock has already been released. */
static void DRV8912_TransferCompleted(status_t status, void *userData)
{
    DRV8912_device_t *device = (DRV8912_device_t *)userData;
    SDK_DelayAtLeastUs(DRV8912_WRITE_GAP_US, CLOCK_GetFreq(kCLOCK_CoreSysClk));
    if (status == kStatus_Success) status = DRV8912_Decode(device->rxData, NULL);
    device->asyncStatus = status;
    __DMB();
    device->asyncCompleted = true;
}

/* Submit one frame while the caller retains the device context lock. */
static status_t DRV8912_QueueFrame(DRV8912_device_t *device, bool read,
                                 DRV8912_register_t address, uint8_t data)
{
    SPI_transaction_t transfer = { 0 };
    device->txData[0] = (uint8_t)((uint8_t)address | (read ? DRV8912_READ_COMMAND : 0U));
    device->txData[1] = data;
    device->rxData[0] = 0U; device->rxData[1] = 0U;
    device->asyncCompleted = false;
    transfer.base = device->config.base;
    transfer.config = &DRV8912_busConfig;
    transfer.txData = device->txData;
    transfer.rxData = device->rxData;
    transfer.dataSize = 2U;
    transfer.configFlags = device->config.transferFlags;
    transfer.callback = DRV8912_TransferCompleted;
    transfer.userData = device;
    return SPIs_TransferNonBlocking(&transfer);
}

static status_t DRV8912_Submit(DRV8912_device_t *device, bool read,
                               DRV8912_register_t address, uint8_t data)
{
    status_t status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    device->maskOperation = false;
    device->maskSubmitPending = false;
    device->asyncPending = true;
    status = DRV8912_QueueFrame(device, read, address, data);
    if (status != kStatus_Success)
    {
        device->asyncPending = false;
        DRV8912_Release(device);
    }
    return status;
}

status_t DRV8912_ReadRegisterNonBlocking(DRV8912_device_t *device, DRV8912_register_t address)
{
    if (!DRV8912_AddressValid(address, false)) return kStatus_InvalidArgument;
    return DRV8912_Submit(device, true, address, 0U);
}

status_t DRV8912_WriteRegisterNonBlocking(DRV8912_device_t *device,
                                         DRV8912_register_t address, uint8_t data)
{
    if (!DRV8912_WriteValid(address, data)) return kStatus_InvalidArgument;
    return DRV8912_Submit(device, false, address, data);
}

/**
 * @brief Starts a nonblocking update of all twelve output states.
 *
 * Mask bit 0 corresponds to OUT1; bit 11 corresponds to OUT12.
 * A bit set in highMask selects HIGH; a bit set in lowMask selects LOW.
 * A bit clear in both masks selects Hi-Z. Masks must not overlap or
 * contain bits outside the twelve-output range.
 *
 * Copies the requested states into the device context and starts the
 * first interrupt-driven SPI transfer.
 *
 * The caller must repeatedly call DRV8912_GetTransferResult() from the
 * main loop to advance the three register writes and read-back checks.
 * It returns true only when the complete operation has ended.
 *
 * Updates occur in groups of four outputs, not simultaneously.
 * Failure can leave preceding groups updated. No timed pulses are generated.
 *
 * The device remains reserved until the final result is consumed.
 *
 * @return kStatus_Success if the operation was accepted, not completed;
 *         otherwise a validation, device/bus-busy or submission error.
 */
status_t DRV8912_SetOutputsNonBlocking(DRV8912_device_t *device,
                                          uint16_t highMask, uint16_t lowMask)
{
    status_t status;
    if (((highMask | lowMask) & ~DRV8912_OUTPUT_MASK) != 0U ||
        (highMask & lowMask) != 0U) return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    for (unsigned group = 0U; group < 3U; ++group)
    {
        uint8_t value = 0U;
        for (unsigned bit = 0U; bit < 4U; ++bit)
        {
            uint16_t mask = (uint16_t)(1U << (group * 4U + bit));
            if ((lowMask & mask) != 0U) value |= (uint8_t)(1U << (bit * 2U));
            if ((highMask & mask) != 0U) value |= (uint8_t)(2U << (bit * 2U));
        }
        device->maskValues[group] = value;
    }
    device->maskOperation = true;
    device->maskStep = 0U;
    device->maskSubmitPending = false;
    device->asyncPending = true;
    status = DRV8912_QueueFrame(device, false, eDRV8912_OP_CTRL_1, device->maskValues[0]);
    if (status != kStatus_Success)
    {
        device->maskOperation = false;
        device->asyncPending = false;
        DRV8912_Release(device);
    }
    return status;
}


status_t DRV8912_ApplyRelayMaskNonBlocking(DRV8912_device_t *device, uint16_t mask)
{
    if ((mask & ~DRV8912_OUTPUT_MASK) != 0U) return kStatus_InvalidArgument;
    return DRV8912_SetOutputMasksNonBlocking(device, mask,
        (uint16_t)(~mask & DRV8912_OUTPUT_MASK));
}


status_t DRV8912_SetAllHighImpedanceNonBlocking(DRV8912_device_t *device)
{
    return DRV8912_SetOutputMasksNonBlocking(device, 0U, 0U);
}

/*
 * Nonblocking transfer flow:
 *
 * 1. The public NonBlocking function reserves the device context and calls
 *    DRV8912_QueueFrame() to start one interrupt-driven SPI transaction.
 *    kStatus_Success means submission succeeded, not operation completion.
 *
 * 2. When that transaction ends, DRV8912_TransferCompleted() stores its
 *    result and sets asyncCompleted. It does not start the next transaction.
 *
 * 3. The main loop repeatedly calls DRV8912_GetTransferResult().
 *    For a single-register operation, it collects the final result.
 *    For an output-state update, it advances this six-transaction sequence:
 *
 *      Write OP_CTRL_1 -> Read OP_CTRL_1 and verify
 *      Write OP_CTRL_2 -> Read OP_CTRL_2 and verify
 *      Write OP_CTRL_3 -> Read OP_CTRL_3 and verify
 *
 *    Each next transaction is started through DRV8912_QueueFrame().
 *    If the shared SPI bus is busy between transactions, submission is
 *    retried on a subsequent call without releasing the device context.
 *
 * 4. DRV8912_GetTransferResult() returns:
 *    false: no final result is available; keep calling it from the main loop.
 *    true:  the operation ended; the status parameter contains its result.
 *           The device context is released and can accept a new command.
 *
 * Transfer errors or read-back mismatches terminate the sequence.
 * Previously written output groups may already have changed.
 *
 * IMPORTANT: GetTransferResult() also advances multi-register operations;
 * polling it is required for progress, not merely for retrieving the result.
 */
bool DRV8912_GetTransferResult(DRV8912_device_t *device,
    status_t *status, DRV8912_response_t *response)
{
    status_t result;
    if (device == NULL || status == NULL || !device->asyncPending) return false;

    if (device->maskOperation)
    {
        if (!device->maskSubmitPending)
        {
            if (!device->asyncCompleted) return false;
            __DMB();
            result = device->asyncStatus;
            if (result != kStatus_Success) goto complete;
            /* Odd steps are read-back checks; even steps are writes. */
            if ((device->maskStep & 1U) != 0U &&
                device->rxData[1] != device->maskValues[device->maskStep / 2U])
            {
                result = kStatus_Fail;
                goto complete;
            }
            if (device->maskStep == 5U) goto complete;
            ++device->maskStep;
            device->asyncCompleted = false;
            device->maskSubmitPending = true;
        }
        /* Retry later if another device temporarily owns the shared bus. */
        result = DRV8912_QueueFrame(device, (device->maskStep & 1U) != 0U,
            (DRV8912_register_t)(eDRV8912_OP_CTRL_1 + device->maskStep / 2U),
            device->maskValues[device->maskStep / 2U]);
        if (result == kStatus_DSPI_Busy) return false;
        if (result != kStatus_Success) goto complete;
        device->maskSubmitPending = false;
        return false;
    }

    if (!device->asyncCompleted) return false;
    __DMB();
    result = device->asyncStatus;
complete:
    *status = result;
    if (result == kStatus_Success) (void)DRV8912_Decode(device->rxData, response);
    device->maskOperation = false;
    device->maskSubmitPending = false;
    device->asyncPending = false;
    device->asyncCompleted = false;
    DRV8912_Release(device);
    return true;
}

static status_t DRV8912_WriteVerified(DRV8912_device_t *device,
                                     DRV8912_register_t address, uint8_t data, uint8_t mask)
{
    DRV8912_response_t response;
    status_t status = DRV8912_Transfer(device, false, address, data, NULL);
    if (status == kStatus_Success)
        status = DRV8912_Transfer(device, true, address, 0U, &response);
    if (status == kStatus_Success && (response.registerData & mask) != (data & mask))
        status = kStatus_Fail;
    return status;
}

/**
 * @brief Sets the electrical states of all twelve outputs synchronously.
 *
 * Mask bit 0 corresponds to OUT1; bit 11 corresponds to OUT12.
 * A bit set in high selects HIGH; a bit set in low selects LOW.
 * A bit clear in both masks selects high impedance (Hi-Z).
 *
 * Preconditions: masks use only bits 0..11 and must not overlap.
 * The caller must already own the device context lock.
 *
 * Builds, writes and verifies OP_CTRL_1..3, one group of four outputs
 * at a time. Updates are not simultaneous across the three groups.
 * Stops at the first error; preceding groups may already be updated.
 *
 * This internal blocking function does not acquire or release the device
 * context lock and does not generate timed relay pulses.
 *
 * @return kStatus_Success if all three registers were written and verified;
 *         otherwise the first transfer or verification error.
 */
static status_t DRV8912_SetOutputsLockedBlocking( DRV8912_device_t *device, uint16_t high, uint16_t low )
{
    unsigned group, bit;
    for (group = 0U; group < 3U; ++group)
    {
        uint8_t value = 0U;
        for (bit = 0U; bit < 4U; ++bit)
        {
            uint16_t m = (uint16_t)(1U << (group * 4U + bit));
            if ((low & m) != 0U) value |= (uint8_t)(1U << (bit * 2U));
            if ((high & m) != 0U) value |= (uint8_t)(2U << (bit * 2U));
        }
        status_t status = DRV8912_WriteVerified(device,
            (DRV8912_register_t)(eDRV8912_OP_CTRL_1 + group), value, 0xFFU);
        if (status != kStatus_Success) return status;
    }
    return kStatus_Success;
}

/**
 * @brief Public user-facing version of DRV8912_SetOutStates().
 *
 * Validates the masks, acquires the device context lock, calls the
 * internal blocking output-state setter, and releases the lock.
 *
 * Returns kStatus_DSPI_Busy if the device is already reserved;
 * otherwise returns the validation, transfer or verification status.
 */
status_t DRV8912_SetOutputsBlocking(DRV8912_device_t *device,
                                       uint16_t highMask, uint16_t lowMask )
{
    status_t status;
    if (((highMask | lowMask) & ~DRV8912_OUTPUT_MASK) != 0U || (highMask & lowMask) != 0U)
        return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    status = DRV8912_SetOutputsLockedBlocking(device, highMask, lowMask);
    DRV8912_Release(device);
    return status;
}

/**
 * @brief Sets the state of one output while preserving all other outputs.
 *
 * Reads the current OP_CTRL register, modifies only the two-bit field
 * belonging to the selected output, then writes and verifies the result.
 *
 * Acquires and releases the device context lock. All transfers are blocking.
 *
 * @param device Initialized device context.
 * @param output Output number, from 1 to 12.
 * @param state  Requested state: HIGH, LOW or high impedance (Hi-Z).
 *
 * @return kStatus_Success on completion; otherwise a validation,
 *         device-busy, transfer or verification error.
 */

status_t DRV8912_SetOutputBlocking(DRV8912_device_t *device,
    uint8_t output, eDRV8912_outputState_t state)
{
    DRV8912_response_t response;
    DRV8912_register_t address;
    uint8_t shift, value;
    status_t status;
    if (output < 1U || output > 12U ||
        (state != eDRV8912_Output_HighImpedance && state != eDRV8912_Output_Low &&
         state != eDRV8912_Output_High)) return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    address = (DRV8912_register_t)(eDRV8912_OP_CTRL_1 + (output - 1U) / 4U);
    shift = (uint8_t)(((output - 1U) % 4U) * 2U);
    status = DRV8912_Transfer(device, true, address, 0U, &response);
    if (status == kStatus_Success)
    {
        value = (uint8_t)((response.registerData & ~(3U << shift)) | ((uint8_t)state << shift));
        if (!DRV8912_WriteValid(address, value)) status = kStatus_Fail;
        else status = DRV8912_WriteVerified(device, address, value, 0xFFU);
    }
    DRV8912_Release(device);
    return status;
}

status_t DRV8912_ApplyRelayMaskBlocking(DRV8912_device_t *device, uint16_t mask)
{
    if ((mask & ~DRV8912_OUTPUT_MASK) != 0U) return kStatus_InvalidArgument;
    return DRV8912_SetOutputsBlocking(device, mask, (uint16_t)(~mask & DRV8912_OUTPUT_MASK));
}

status_t DRV8912_AllHighImpedanceBlocking(DRV8912_device_t *device)
{
    return DRV8912_SetOutputsBlocking(device, 0U, 0U);
}



/**
 * @brief Reads the seven device diagnostic registers synchronously.
 *
 * Reads IC_STAT, followed by the OCP_STAT and OLD_STAT registers
 * for each group of four outputs.
 *
 * The returned structure contains:
 * - icStatus.b8: general device status from IC_STAT.
 * - overcurrent.b8[0..2]: low-side/high-side overcurrent flags
 *   for OUT1..OUT12, from OCP_STAT_1..3.
 * - openLoad.b8[0..2]: low-side/high-side open-load flags
 *   for OUT1..OUT12, from OLD_STAT_1..3.
 *
 * Each register requires one blocking 16-clock SPI transaction.
 * The function stops at the first transfer or response-format error.
 *
 * Register values are collected in a local structure. The destination
 * is updated only after all seven reads succeed; on failure, its previous
 * contents remain unchanged.
 *
 * Registers are read sequentially, so the result is not a simultaneous
 * snapshot. Diagnostic conditions may change between reads.
 *
 * This function does not clear fault flags, change output states, or
 * classify the returned diagnostic flags as success/failure.
 * Open-load flags must be interpreted according to the configured
 * detection mode; a clear flag does not prove that a load is connected.
 *
 * Internal function: the caller must already own the device context lock.
 * It does not acquire or release that lock. Each SPI transaction acquires
 * and releases the shared bus through DRV8912_Transfer().
 *
 * @param device
 *        Valid, initialized device context, already reserved by the caller.
 *
 * @param faults
 *        Non-NULL destination for the complete diagnostic result.
 *
 * @return kStatus_Success
 *         All seven registers were read successfully, even if fault flags
 *         are present in the returned data.
 *
 * @return Other status
 *         Error returned by the first unsuccessful transaction.
 */
static status_t DRV8912_ReadFaults(DRV8912_device_t *device, DRV8912_faults_t *faults)
{
    DRV8912_faults_t values;
    DRV8912_response_t response;
    status_t status = DRV8912_Transfer(device, true, eDRV8912_IC_STAT, 0U, &response);
    if (status != kStatus_Success) return status;
    values.icStatus.b8 = response.registerData;
    for (unsigned i = 0U; i < 3U; ++i)
    {
        status = DRV8912_Transfer(device, true, (DRV8912_register_t)(eDRV8912_OCP_STAT_1 + i), 0U, &response);
        if (status != kStatus_Success) return status;
        values.overcurrent.b8[i] = response.registerData;
        status = DRV8912_Transfer(device, true, (DRV8912_register_t)(eDRV8912_OLD_STAT_1 + i), 0U, &response);
        if (status != kStatus_Success) return status;
        values.openLoad.b8[i] = response.registerData;
    }
    *faults = values; /* Publish only a complete snapshot; not hardware-atomic. */
    return kStatus_Success;
}

/**
 * @brief Public user-facing version of DRV8912_ReadFaults().
 *
 * Validates the destination pointer, acquires the device context lock,
 * calls the internal blocking diagnostic reader, and releases the lock.
 *
 * Returns kStatus_DSPI_Busy if the device is already reserved;
 * otherwise returns the diagnostic reader's status.
 * kStatus_Success means the registers were read, not that no faults exist.
 */

status_t DRV8912_ReadFaultsBlocking(DRV8912_device_t *device, DRV8912_faults_t *faults)
{
    status_t status;
    if (faults == NULL) return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    status = DRV8912_ReadFaults(device, faults);
    DRV8912_Release(device);
    return status;
}

static status_t DRV8912_ClearFaults(DRV8912_device_t *device)
{
    DRV8912_response_t response;
    status_t status = DRV8912_Transfer(device, true, eDRV8912_CONFIG_CTRL, 0U, &response);
    if (status == kStatus_Success)
        status = DRV8912_Transfer(device, false, eDRV8912_CONFIG_CTRL,
            (uint8_t)((response.registerData & 0x0EU) | DRV8912_CONFIG_CLEAR_FAULTS), NULL);
    return status;
}

status_t DRV8912_ClearFaultsBlocking(DRV8912_device_t *device)
{
    status_t status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    status = DRV8912_ClearFaults(device);
    DRV8912_Release(device);
    return status;
}

/**
 * @brief Initializes the device synchronously for static output control.
 *
 * If configured, calls the enable callback to wake the device by driving
 * nSLEEP high, then waits for the wake-up settling time.
 *
 * Checks the device ID and saves a copy of the initial register contents
 * in device->startupLog.registersBefore.
 * Does not compare those contents with the documented reset/default values.
 *
 * Clears the output-enable bits, leaving all outputs in Hi-Z, disables PWM
 * on all outputs and stops the internal PWM generators.
 * Applies the diagnostic/protection configuration using register writes
 * followed by read-back verification of the relevant bits.
 * The fault-clear command is issued separately, followed by a diagnostic read.
 *
 * Saves the final diagnostics and result in device->startupLog.
 * Does not generate relay test pulses or force a hardware reset.
 *
 * @param device
 *        Initialized device context identifying the SPI peripheral,
 *        chip select and optional wake-up callback.
 *
 * @param coreClock_Hz
 *        Actual MCU core clock frequency in Hz, used for software delays.
 *
 * @param extOvp
 *        true selects the extended overvoltage range for operation up to
 *        32 V, suitable for a nominal 24 V supply; false selects the
 *        standard range.
 *
 * @return kStatus_Success if initialization and the final fault check succeed;
 *         otherwise the first detected error.
 */
status_t DRV8912_StartBlocking(DRV8912_device_t *device, uint32_t coreClock_Hz, bool extOvp)
{
    DRV8912_response_t response;
    status_t status;
    if (coreClock_Hz == 0U) return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    memset(&device->startupLog, 0, sizeof(device->startupLog));
    device->startupLog.deviceId = 0xFFU;
    if (device->config.setEnable != NULL)
    	device->config.setEnable(true, device->config.userData);
    SDK_DelayAtLeastUs(DRV8912_READY_US, coreClock_Hz);
    // reads the ID of the chip
    status = DRV8912_Transfer(device, true, eDRV8912_CONFIG_CTRL, 0U, &response);
    if (status != kStatus_Success) goto done;
    device->startupLog.deviceId = (uint8_t)((response.registerData >> 4U) & 7U);
    if (device->startupLog.deviceId != 0U) { status = kStatus_Fail; goto done; }
    /* Document the initial register image before changing the configuration. */
    for (unsigned a = 0U; a < DRV8912_REGISTER_COUNT; ++a)
    {
        if (!DRV8912_AddressValid((DRV8912_register_t)a, false)) continue;
        status = DRV8912_Transfer(device, true, (DRV8912_register_t)a, 0U, &response);
        if (status != kStatus_Success) goto done;
        device->startupLog.registersBefore[a] = response.registerData;
    }
    // Clear to HI Z all of the outpus
    status = DRV8912_SetOutputsLockedBlocking(device, 0U, 0U);
    if (status != kStatus_Success) goto done;

    /* Enable static drive and stop internal PWM generators. */
    status = DRV8912_WriteVerified(device, eDRV8912_PWM_CTRL_1, 0U, 0xFFU);
    if (status != kStatus_Success) goto done;
    // Disable internal PWM generator and static drive for the remaining
    status = DRV8912_WriteVerified(device, eDRV8912_PWM_CTRL_2, 0xF0U, 0xFFU);
    if (status != kStatus_Success) goto done;
    /* Disable active open-load detection for pulse/capacitive relay loads.
     * OLD_OP=0, OLD_REP=0, PL_MODE_EN=0: independent outputs. */
    status = DRV8912_WriteVerified(device, eDRV8912_OLD_CTRL_1, 0xFFU, 0xFFU);
    if (status != kStatus_Success) goto done;
    status = DRV8912_WriteVerified(device, eDRV8912_OLD_CTRL_2, 0x0FU, 0xFFU);
    if (status != kStatus_Success) goto done;

    /* OCP_DEG=000 (10 us), OLD_NEG_EN=0, low-current OLD disabled. */
    status = DRV8912_WriteVerified(device, eDRV8912_OLD_CTRL_3, 0U, 0xFFU);
    if (status != kStatus_Success) goto done;
    status = DRV8912_WriteVerified(device, eDRV8912_OLD_CTRL_4, 0U, 0xFFU);
    if (status != kStatus_Success) goto done;
    status = DRV8912_WriteVerified(device, eDRV8912_CONFIG_CTRL,
        (uint8_t)(DRV8912_CONFIG_OTW_REPORT | (extOvp ? DRV8912_CONFIG_EXT_OVP : 0U)), 0x0EU);
    if (status != kStatus_Success) goto done;
    status = DRV8912_ClearFaults(device);
    if (status != kStatus_Success) goto done;
    status = DRV8912_ReadFaults(device, &device->startupLog.faultsAfter);
    if (status == kStatus_Success &&
        (device->startupLog.faultsAfter.icStatus.b8 & DRV8912_IC_FAULT_MASK) != 0U)
        status = kStatus_Fail;
done:
    device->startupLog.result = status;
    DRV8912_Release(device);
    return status;
}

status_t DRV8912_SleepBlocking(DRV8912_device_t *device, uint32_t coreClock_Hz)
{
    status_t status;
    if (coreClock_Hz == 0U || device == NULL || device->config.setEnable == NULL)
        return kStatus_InvalidArgument;
    status = DRV8912_Acquire(device);
    if (status != kStatus_Success) return status;
    status = DRV8912_SetMasks(device, 0U, 0U);
    /* The pin is authoritative even if the preceding SPI transaction failed.
     * Hi-Z can itself generate a transition with capacitive relay circuitry. */
    device->config.setEnable(false, device->config.userData);
    SDK_DelayAtLeastUs(DRV8912_READY_US, coreClock_Hz);
    DRV8912_Release(device);
    return status;
}
