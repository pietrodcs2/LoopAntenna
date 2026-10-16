/*
 * IS25LP064A-JBLE PURE SPI FLASH DRIVER
 * ==================================
 * Target: ISSI IS25LP064A, JEDEC 9D 60 17, 8 MiB, three address bytes.
 * Standard 1-bit SPI only, mode 0, MSB first. No Quad/QPI, filesystem, database,
 * heap allocation, automatic formatting or automatic erasure during writes.
 * Commands/data are sent in one SDK transaction, keeping CS low throughout.
 * Every standalone instruction ends with CS high.
 *
 * INITIALIZATION
 * Configure MCU clocks/pin mux, initialize SPIs once and set the chosen PCS
 * active LOW. The proposed default is SPI0/PCS4 (PTC0 in your board mapping);
 * change the macros/config to match actual wiring. This driver configures no pins.
 * GetDefaultConfig(), assign getTimeMs/userData, InitContext(), StartBlocking().
 * Start wakes from deep power-down, checks the ID and reads diagnostic registers.
 * It does not erase data, force a reset, change QE or clear write protection.
 * startupLog records ID, status/function bytes and the initialization result.
 *
 * REQUIRED CLOCK
 * getTimeMs must return a genuinely advancing uint32_t millisecond uptime.
 * Unsigned subtraction supports wraparound. Timeouts must be < 2^31 ms.
 * Call from foreground with interrupts enabled; waits require the timebase.
 * Optional idle hook can service a watchdog; it must not reenter this device.
 * If absent, a short SDK delay limits polling traffic. Blocking calls occupy
 * the caller until completion; this first version has no asynchronous API.
 *
 * TIMEOUT SCOPE
 * Timeouts bound internal WIP polling and retries on a busy shared SPI bus.
 * They assume SPIs_TransferBlocking/SDK returns. The existing shared SPI layer
 * itself has no abort/timeout API, so this driver cannot bound a hung SDK call.
 * An operation which times out may STILL be running inside the FLASH. Later
 * accesses must wait for readiness; do not erase/reset to recover blindly.
 *
 * DATA OPERATIONS
 * Read accepts any valid byte range, using bounded 256-byte chunks.
 * Program checks the entire input range and block protection, then pre-reads it
 * to reject any required zero-to-one bit transition BEFORE programming starts.
 * It splits at actual page boundaries, confirms WEL after each WREN, waits for
 * WIP to clear, checks WEL clearing and verifies each page by read-back.
 * The caller must explicitly erase when RequiresErase is returned.
 * Erase APIs require exact alignment and verify all requested bytes as 0xFF.
 * Range erase processes 4 KiB sectors. UpdateBlocking preserves surrounding
 * data through read/modify/erase/program using a caller-owned sector buffer.
 * Earlier chunks can be completed when a later transfer/verification fails.
 * operationLog.completedBytes identifies the completed/verified prefix.
 *
 * STATE AND OWNERSHIP
 * One persistent zero-initialized context per chip. Do not copy/reinitialize it
 * while busy. Public calls reserve the context; private Locked helpers reuse
 * that reservation. SPIs arbitrates the bus separately for each transaction,
 * allowing other devices to use it while FLASH program/erase progresses.
 * Caller data/output buffers must not alias this context or its SPI buffers.
 * Do not drive the same chip through another context/controller concurrently.
 * Status/function/ID can be queried before successful Start for diagnosis.
 * PSUS/ESUS are checked: suspended operations are rejected, never mistaken for
 * readiness. This driver neither suspends nor resumes operations.
 *
 * LOGS AND ELECTRICAL CONDITIONS
 * All results are IS25LP064A_status_t; operationLog.spiStatus retains SDK errors.
 * output buffers of a failed multi-chunk read may contain a partial prefix.
 * WP#/HOLD# must be inactive for normal access. Pull them up, keep firmware in
 * standard SPI, and avoid direct rail ties incompatible with QE=1 (datasheet).
 * GPIO controls and status bitfield unions assume the project ARM GCC ABI.
 * Decisions in the driver use masks, not compiler-dependent bitfield ordering.
 *
 * No operation here supplies wear leveling or power-loss-safe record updates.
 * Reliance Edge/storage policy will be integrated above this layer later.
 * Reference: ISSI IS25LP064A datasheet Rev. A18, 2024-06-27.
 */

/* ------------------------ I N C L U D E   F I L E S ------------------------ */
#include "LA_IS25LP064A_drv.h"
#include "LA_SYSTIMER_drv.h"
#include "fsl_clock.h"
#include <string.h>

/* --------------------- L O C A L   D E F I N I T I O N S -------------------- */
#define IS25_CMD_READ 0x03U
#define IS25_CMD_JEDEC 0x9FU
#define IS25_CMD_STATUS 0x05U
#define IS25_CMD_FUNCTION 0x48U
#define IS25_CMD_WREN 0x06U
#define IS25_CMD_PROGRAM 0x02U
#define IS25_CMD_ERASE4 0x20U
#define IS25_CMD_ERASE32 0x52U
#define IS25_CMD_ERASE64 0xD8U
#define IS25_CMD_ERASECHIP 0xC7U
#define IS25_CMD_WRSR 0x01U
#define IS25_CMD_WAKE 0xABU
#define IS25_STATUS_WIP 0x01U
#define IS25_STATUS_WEL 0x02U
#define IS25_STATUS_BP 0x3CU
#define IS25_FUNCTION_TBS 0x02U
#define IS25_FUNCTION_SUSPEND 0x0CU
#define IS25_ADDRESS_PREFIX_BYTES 4U
#define IS25_MAX_NORMAL_READ_HZ 50000000UL
#define IS25_POLL_DELAY_US 50U
#define IS25_WAKE_DELAY_US 5U

/* INPUT: candidate user config. RETURN: true if usable by shared CTAR0 SPI.
 * The required time callback is checked here; its actual progress is a board
 * responsibility. Mode 0 is supplied by this driver, not by user flags.
 */
static bool IS25LP064A_ConfigValid(const IS25LP064A_config_t *config)
{
    uint32_t allowed = DSPI_MASTER_PCS_MASK | (uint32_t)kDSPI_MasterPcsContinuous;
    return config != NULL && config->getTimeMs != NULL && config->baudrate_Hz != 0U &&
        config->baudrate_Hz <= IS25_MAX_NORMAL_READ_HZ &&
        (config->base == SPI0 || config->base == SPI1 || config->base == SPI2) &&
        (config->transferFlags & ~allowed) == 0U &&
        (config->transferFlags & kDSPI_MasterPcsContinuous) != 0U &&
        ((config->transferFlags & DSPI_MASTER_PCS_MASK) >> DSPI_MASTER_PCS_SHIFT) <= 5U;
}

/* INPUT: destination. OUTPUT: suggested bus config, with no time callback.
 * Assign getTimeMs before InitContext. PCS4 is a suggestion, not configured GPIO.
 */
void IS25LP064A_GetDefaultConfig(IS25LP064A_config_t *config)
{
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->base = IS25LP064A_SPI_BASE;
    config->transferFlags = IS25LP064A_SPI_TRANSFER_FLAGS;
    config->baudrate_Hz = IS25LP064A_SPI_BAUDRATE_HZ;
    config->getTimeMs = LA_SYSTIMER_GetTimeMs;
}

/* INPUT: fresh context, config. OUTPUT: cleared context with config copied.
 * Does not initialize SPI, send commands or create nonvolatile data.
 * Call only once on a fresh/not-in-use context; config may alias device->config.
 */
IS25LP064A_status_t IS25LP064A_InitContext(IS25LP064A_device_t *device,
    const IS25LP064A_config_t *config)
{
    IS25LP064A_config_t copy;
    if (device == NULL || !IS25LP064A_ConfigValid(config)) return eIS25LP064A_InvalidArgument;
    copy = *config;
    memset(device, 0, sizeof(*device));
    device->config = copy;
    device->startupLog.result = eIS25LP064A_NotInitialized;
    device->operationLog.result = eIS25LP064A_NotInitialized;
    return eIS25LP064A_Success;
}

/* INPUT: context and whether array access needs a confirmed ID.
 * OUTPUT: sets context busy and resets last-operation log on acquisition.
 * RETURN: Busy, InvalidExecutionContext, NotInitialized, InvalidArgument or Success.
 */
static IS25LP064A_status_t IS25LP064A_Acquire(IS25LP064A_device_t *device, bool requireInitialized)
{
    uint32_t primask;
    if (device == NULL || !IS25LP064A_ConfigValid(&device->config)) return eIS25LP064A_InvalidArgument;
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U) return eIS25LP064A_InvalidExecutionContext;
    primask = __get_PRIMASK();
    __disable_irq();
    if (device->busy)
    {
        __set_PRIMASK(primask);
        return eIS25LP064A_Busy;
    }
    if (requireInitialized && !device->initialized)
    {
        __set_PRIMASK(primask);
        return eIS25LP064A_NotInitialized;
    }
    device->busy = true;
    __DMB();
    __set_PRIMASK(primask);
    memset(&device->operationLog, 0, sizeof(device->operationLog));
    return eIS25LP064A_Success;
}

/* INPUT: owned context/result. OUTPUT: publishes log result, releases ownership. */
static IS25LP064A_status_t IS25LP064A_Finish(IS25LP064A_device_t *device, IS25LP064A_status_t result)
{
    device->operationLog.result = result;
    __DMB();
    device->busy = false;
    return result;
}

/* INPUT: address/length. RETURN: true if every byte fits; never permits wrapping. */
static bool IS25LP064A_RangeValid(uint32_t address, uint32_t length)
{
    return length != 0U && address < IS25LP064A_CAPACITY_BYTES &&
        length <= IS25LP064A_CAPACITY_BYTES - address;
}

/* INPUT: owned context, prepared tx buffer, wire-byte count.
 * OUTPUT: rx buffer and last SDK status. Single complete CS-low transaction.
 */
static IS25LP064A_status_t IS25LP064A_TransferLocked(IS25LP064A_device_t *device, size_t count)
{
    SPI_busConfig_t config = {0};
    SPI_transaction_t transfer = {0};
    status_t status;
    config.baudrate_Hz = device->config.baudrate_Hz;
    config.bitsPerFrame = 8U;
    config.cpol = kDSPI_ClockPolarityActiveHigh;
    config.cpha = kDSPI_ClockPhaseFirstEdge;
    config.direction = kDSPI_MsbFirst;
    config.pcsToSckDelay_ns = IS25LP064A_SPI_DELAY_NS;
    config.lastSckToPcsDelay_ns = IS25LP064A_SPI_DELAY_NS;
    config.betweenTransferDelay_ns = IS25LP064A_SPI_DELAY_NS;
    transfer.base = device->config.base;
    transfer.config = &config;
    transfer.txData = device->txData;
    transfer.rxData = device->rxData;
    transfer.dataSize = count;
    transfer.configFlags = device->config.transferFlags;
    memset(device->rxData, 0, count);
    status = SPIs_TransferBlocking(&transfer);
    device->operationLog.spiStatus = status;
    if (status == kStatus_DSPI_Busy) return eIS25LP064A_Busy;
    return status == kStatus_Success ? eIS25LP064A_Success : eIS25LP064A_SPIError;
}

/* Short polling yield, with no device reentry or SPI work in this hook. */
static void IS25LP064A_IdleLocked(IS25LP064A_device_t *device)
{
    if (device->config.idle != NULL) device->config.idle(device->config.userData);
    else SDK_DelayAtLeastUs(IS25_POLL_DELAY_US, CLOCK_GetFreq(kCLOCK_CoreSysClk));
}

/* INPUT: one-byte command. OUTPUT: diagnostic byte only if SPI succeeds. */
static IS25LP064A_status_t IS25LP064A_ReadRegisterLocked(IS25LP064A_device_t *device,
    uint8_t command, uint8_t *value)
{
    IS25LP064A_status_t status;
    device->txData[0] = command;
    device->txData[1] = 0xFFU;
    status = IS25LP064A_TransferLocked(device, 2U);
    if (status == eIS25LP064A_Success) *value = device->rxData[1];
    return status;
}

/* INPUT: timeout in ms (0 < value < 2^31). OUTPUT: no data; polls WIP and
 * function suspension. SPI read errors propagate, never mean "not busy".
 * Busy shared bus is retried within the same elapsed-time budget.
 */
static IS25LP064A_status_t IS25LP064A_WaitReadyLocked(IS25LP064A_device_t *device, uint32_t timeout_ms)
{
    uint32_t start;
    if (timeout_ms == 0U || timeout_ms >= UINT32_C(0x80000000)) return eIS25LP064A_InvalidArgument;
    start = device->config.getTimeMs(device->config.userData);
    for (;;)
    {
        uint8_t sr = 0U, fr = 0U;
        IS25LP064A_status_t status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &sr);
        if (status != eIS25LP064A_Success && status != eIS25LP064A_Busy) return status;
        if (status == eIS25LP064A_Success && (sr & IS25_STATUS_WIP) == 0U)
        {
            status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_FUNCTION, &fr);
            if (status == eIS25LP064A_Success)
                return (fr & IS25_FUNCTION_SUSPEND) != 0U ? eIS25LP064A_Suspended : eIS25LP064A_Success;
            if (status != eIS25LP064A_Busy) return status;
        }
        if ((uint32_t)(device->config.getTimeMs(device->config.userData) - start) >= timeout_ms)
            return eIS25LP064A_Timeout;
        IS25LP064A_IdleLocked(device);
    }
}

/* INPUT: opcode/address. OUTPUT: command prefix with exactly THREE address bytes. */
static void IS25LP064A_MakeAddressLocked(IS25LP064A_device_t *device, uint8_t command, uint32_t address)
{
    device->txData[0] = command;
    device->txData[1] = (uint8_t)(address >> 16U);
    device->txData[2] = (uint8_t)(address >> 8U);
    device->txData[3] = (uint8_t)address;
}

/* INPUT: owned context. OUTPUT: JEDEC ID only on success (no dummy address). */
static IS25LP064A_status_t IS25LP064A_ReadIdLocked(IS25LP064A_device_t *device, uint32_t *id)
{
    IS25LP064A_status_t status;
    memset(device->txData, 0xFF, 4U);
    device->txData[0] = IS25_CMD_JEDEC;
    status = IS25LP064A_TransferLocked(device, 4U);
    if (status == eIS25LP064A_Success)
        *id = ((uint32_t)device->rxData[1] << 16U) | ((uint32_t)device->rxData[2] << 8U) | device->rxData[3];
    return status;
}

/* INPUT: owned context/address/count<=256. OUTPUT: data in rxData[4..]. */
static IS25LP064A_status_t IS25LP064A_ReadChunkLocked(IS25LP064A_device_t *device,
    uint32_t address, uint32_t count)
{
    IS25LP064A_MakeAddressLocked(device, IS25_CMD_READ, address);
    memset(device->txData + IS25_ADDRESS_PREFIX_BYTES, 0xFF, count);
    return IS25LP064A_TransferLocked(device, IS25_ADDRESS_PREFIX_BYTES + count);
}

/* INPUT: write/erase target interval. OUTPUT: none. Uses BP and OTP TBS only
 * for reading protection geometry. Never writes Function Register/OTP bits.
 */
static IS25LP064A_status_t IS25LP064A_CheckProtectionLocked(IS25LP064A_device_t *device,
    uint32_t address, uint32_t length)
{
    uint8_t sr, fr;
    uint32_t bp, protectedBytes, first;
    IS25LP064A_status_t status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &sr);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_FUNCTION, &fr);
    if (status != eIS25LP064A_Success) return status;
    if ((fr & IS25_FUNCTION_SUSPEND) != 0U) return eIS25LP064A_Suspended;
    if ((sr & IS25_STATUS_WIP) != 0U) return eIS25LP064A_Busy;
    bp = (sr & IS25_STATUS_BP) >> 2U;
    if (bp == 0U) return eIS25LP064A_Success;
    protectedBytes = bp >= 8U ? (uint32_t)IS25LP064A_CAPACITY_BYTES : UINT32_C(65536) << (bp - 1U);
    first = (fr & IS25_FUNCTION_TBS) != 0U ? 0U : IS25LP064A_CAPACITY_BYTES - protectedBytes;
    return address < first + protectedBytes && first < address + length ?
        eIS25LP064A_WriteProtected : eIS25LP064A_Success;
}

/* INPUT: context ready for a modifying command. OUTPUT: enables and confirms WEL.
 * SPI errors and latch rejection are distinguished. This alone programs no data.
 */
static IS25LP064A_status_t IS25LP064A_WriteEnableLocked(IS25LP064A_device_t *device)
{
    uint8_t sr;
    IS25LP064A_status_t status;
    device->txData[0] = IS25_CMD_WREN;
    status = IS25LP064A_TransferLocked(device, 1U);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &sr);
    if (status != eIS25LP064A_Success) return status;
    if ((sr & IS25_STATUS_WIP) != 0U) return eIS25LP064A_Busy;
    return (sr & IS25_STATUS_WEL) != 0U ? eIS25LP064A_Success : eIS25LP064A_WriteEnableFailed;
}

/* INPUT: timeout for a just-issued modifying command. OUTPUT: none.
 * Waits for WIP to clear and checks automatic WEL clearing. Read-back by the
 * caller remains essential: idle status alone is not proof the data changed.
 */
static IS25LP064A_status_t IS25LP064A_WaitModificationLocked(IS25LP064A_device_t *device, uint32_t timeout_ms)
{
    uint8_t sr;
    IS25LP064A_status_t status = IS25LP064A_WaitReadyLocked(device, timeout_ms);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &sr);
    if (status != eIS25LP064A_Success) return status;
    return (sr & IS25_STATUS_WEL) == 0U ? eIS25LP064A_Success : eIS25LP064A_VerificationFailed;
}

/* INPUT: context. OUTPUT: startup log/descriptor; initialized=true only on success.
 * Wakes with ABh, checks JEDEC and waits out a previous internal operation.
 * No reset, protection changes, program or erase is performed.
 */
IS25LP064A_status_t IS25LP064A_StartBlocking(IS25LP064A_device_t *device)
{
    IS25LP064A_status_t status = IS25LP064A_Acquire(device, false);
    if (status != eIS25LP064A_Success) return status;
    device->initialized = false;
    memset(&device->startupLog, 0, sizeof(device->startupLog));
    memset(&device->descriptor, 0, sizeof(device->descriptor));
    device->txData[0] = IS25_CMD_WAKE;
    status = IS25LP064A_TransferLocked(device, 1U);
    if (status != eIS25LP064A_Success) goto done;
    SDK_DelayAtLeastUs(IS25_WAKE_DELAY_US, CLOCK_GetFreq(kCLOCK_CoreSysClk));
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    if (status != eIS25LP064A_Success) goto done;
    status = IS25LP064A_ReadIdLocked(device, &device->startupLog.jedecId);
    if (status != eIS25LP064A_Success) goto done;
    if (device->startupLog.jedecId != IS25LP064A_JEDEC_ID) { status = eIS25LP064A_WrongDevice; goto done; }
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &device->startupLog.status.b8);
    if (status != eIS25LP064A_Success) goto done;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_FUNCTION, &device->startupLog.function.b8);
    if (status != eIS25LP064A_Success) goto done;
    device->descriptor.jedecId = device->startupLog.jedecId;
    device->descriptor.capacity_bytes = IS25LP064A_CAPACITY_BYTES;
    device->descriptor.pageSize_bytes = IS25LP064A_PAGE_SIZE_BYTES;
    device->descriptor.sectorSize_bytes = IS25LP064A_SECTOR_SIZE_BYTES;
    device->descriptor.sectorCount = IS25LP064A_SECTOR_COUNT;
    device->descriptor.addressByteCount = 3U;
    device->initialized = true;
done:
    device->startupLog.result = status;
    return IS25LP064A_Finish(device, status);
}

/* INPUT: context/output. OUTPUT: raw ID even before Start, only on SPI success. */
IS25LP064A_status_t IS25LP064A_ReadIdBlocking(IS25LP064A_device_t *device, uint32_t *jedecId)
{
    IS25LP064A_status_t status;
    if (jedecId == NULL) return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, false);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_ReadIdLocked(device, jedecId);
    return IS25LP064A_Finish(device, status);
}

/* INPUT: context/output. OUTPUT: raw STATUS register, also valid during WIP. */
IS25LP064A_status_t IS25LP064A_ReadStatusBlocking(IS25LP064A_device_t *device, IS25LP064A_STATUS_t *value)
{
    IS25LP064A_status_t status;
    if (value == NULL) return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, false);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &value->b8);
    return IS25LP064A_Finish(device, status);
}

/* INPUT: context/output. OUTPUT: raw FUNCTION register; no OTP writes. */
IS25LP064A_status_t IS25LP064A_ReadFunctionBlocking(IS25LP064A_device_t *device, IS25LP064A_FUNCTION_t *value)
{
    IS25LP064A_status_t status;
    if (value == NULL) return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, false);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_FUNCTION, &value->b8);
    return IS25LP064A_Finish(device, status);
}

/* Public readiness wait, also usable after an operation timed out. */
IS25LP064A_status_t IS25LP064A_WaitReadyBlocking(IS25LP064A_device_t *device, uint32_t timeout_ms)
{
    IS25LP064A_status_t status = IS25LP064A_Acquire(device, false);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_WaitReadyLocked(device, timeout_ms);
    return IS25LP064A_Finish(device, status);
}

/* INPUT: initialized context/address/output/length. OUTPUT: exact byte range.
 * Range crossing pages/sectors is permitted. Failure may leave a partial prefix.
 */
IS25LP064A_status_t IS25LP064A_ReadBlocking(IS25LP064A_device_t *device,
    uint32_t address, void *buffer, uint32_t length)
{
    uint8_t *out = buffer;
    uint32_t done = 0U;
    IS25LP064A_status_t status;
    if (buffer == NULL || !IS25LP064A_RangeValid(address, length)) return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, true);
    if (status != eIS25LP064A_Success) return status;
    device->operationLog.address = address; device->operationLog.requestedBytes = length;
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    while (status == eIS25LP064A_Success && done < length)
    {
        uint32_t count = length - done;
        if (count > IS25LP064A_PAGE_SIZE_BYTES) count = IS25LP064A_PAGE_SIZE_BYTES;
        status = IS25LP064A_ReadChunkLocked(device, address + done, count);
        if (status != eIS25LP064A_Success) break;
        memcpy(out + done, device->rxData + IS25_ADDRESS_PREFIX_BYTES, count);
        done += count; device->operationLog.completedBytes = done;
    }
    return IS25LP064A_Finish(device, status);
}

/* INPUT: owned context/range/expected bytes, or erased=true with expected=NULL.
 * OUTPUT: verifies incrementally; optionally publishes verified prefix length.
 */
static IS25LP064A_status_t IS25LP064A_VerifyLocked(IS25LP064A_device_t *device,
    uint32_t address, const uint8_t *expected, uint32_t length, bool erased, bool publishProgress)
{
    uint32_t done = 0U;
    while (done < length)
    {
        uint32_t count = length - done;
        if (count > IS25LP064A_PAGE_SIZE_BYTES) count = IS25LP064A_PAGE_SIZE_BYTES;
        IS25LP064A_status_t status = IS25LP064A_ReadChunkLocked(device, address + done, count);
        if (status != eIS25LP064A_Success) return status;
        for (uint32_t i = 0U; i < count; ++i)
            if (device->rxData[IS25_ADDRESS_PREFIX_BYTES + i] != (erased ? 0xFFU : expected[done + i]))
                return eIS25LP064A_VerificationFailed;
        done += count;
        if (publishProgress) device->operationLog.completedBytes = done;
    }
    return eIS25LP064A_Success;
}

/* INPUT: initialized context/range/expected. OUTPUT: no caller data changes.
 * Returns VerificationFailed on mismatch; reads and compares every requested byte.
 */
IS25LP064A_status_t IS25LP064A_VerifyBlocking(IS25LP064A_device_t *device,
    uint32_t address, const void *expected, uint32_t length)
{
    IS25LP064A_status_t status;
    if (expected == NULL || !IS25LP064A_RangeValid(address, length)) return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, true);
    if (status != eIS25LP064A_Success) return status;
    device->operationLog.address = address; device->operationLog.requestedBytes = length;
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    if (status == eIS25LP064A_Success) status = IS25LP064A_VerifyLocked(device, address, expected, length, false, true);
    return IS25LP064A_Finish(device, status);
}

/* Programs an already-checked range, split at page boundaries, with WEL,
 * completion and read-back checks. Caller owns the context and has established
 * readiness, protection and bit compatibility. Optional progress is relative
 * to this range; Update publishes its own progress only after a whole sector.
 */
static IS25LP064A_status_t IS25LP064A_ProgramRangeLocked(IS25LP064A_device_t *device,
    uint32_t address, const uint8_t *input, uint32_t length, bool publishProgress)
{
    IS25LP064A_status_t status;
    uint32_t done = 0U;
    while (done < length)
    {
        uint32_t count = IS25LP064A_PAGE_SIZE_BYTES - (address + done) % IS25LP064A_PAGE_SIZE_BYTES;
        if (count > length - done) count = length - done;
        status = IS25LP064A_WriteEnableLocked(device);
        if (status != eIS25LP064A_Success) return status;
        IS25LP064A_MakeAddressLocked(device, IS25_CMD_PROGRAM, address + done);
        memcpy(device->txData + IS25_ADDRESS_PREFIX_BYTES, input + done, count);
        status = IS25LP064A_TransferLocked(device, IS25_ADDRESS_PREFIX_BYTES + count);
        if (status != eIS25LP064A_Success) return status;
        status = IS25LP064A_WaitModificationLocked(device, IS25LP064A_PROGRAM_TIMEOUT_MS);
        if (status != eIS25LP064A_Success) return status;
        status = IS25LP064A_VerifyLocked(device, address + done, input + done, count, false, false);
        if (status != eIS25LP064A_Success) return status;
        done += count;
        if (publishProgress) device->operationLog.completedBytes = done;
    }
    return eIS25LP064A_Success;
}

/* INPUT: initialized context/range/data. OUTPUT: programs and verifies without
 * erasure. Entire preflight rejects 0->1 transitions before the first page write.
 * Each page gets its own WREN; payload starts at txData[4], never [5].
 */
IS25LP064A_status_t IS25LP064A_ProgramBlocking(IS25LP064A_device_t *device,
    uint32_t address, const void *data, uint32_t length)
{
    const uint8_t *input = data;
    uint32_t done = 0U;
    IS25LP064A_status_t status;
    if (data == NULL || !IS25LP064A_RangeValid(address, length)) return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, true);
    if (status != eIS25LP064A_Success) return status;
    device->operationLog.address = address; device->operationLog.requestedBytes = length;
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    if (status != eIS25LP064A_Success) goto end;
    status = IS25LP064A_CheckProtectionLocked(device, address, length);
    if (status != eIS25LP064A_Success) goto end;
    while (done < length)
    {
        uint32_t count = length - done;
        if (count > IS25LP064A_PAGE_SIZE_BYTES) count = IS25LP064A_PAGE_SIZE_BYTES;
        status = IS25LP064A_ReadChunkLocked(device, address + done, count);
        if (status != eIS25LP064A_Success) goto end;
        for (uint32_t i = 0U; i < count; ++i)
            if ((device->rxData[IS25_ADDRESS_PREFIX_BYTES + i] & input[done + i]) != input[done + i])
            { status = eIS25LP064A_RequiresErase; goto end; }
        done += count;
    }
    status = IS25LP064A_ProgramRangeLocked(device, address, input, length, true);
end:
    return IS25LP064A_Finish(device, status);
}

/* INPUT: exact aligned erase unit/opcode/timeout. OUTPUT: erased bytes verified.
 * Context stays owned. Caller has checked the entire requested range/protection.
 */
static IS25LP064A_status_t IS25LP064A_EraseUnitLocked(IS25LP064A_device_t *device,
    uint32_t address, uint32_t length, uint8_t command, uint32_t timeout_ms)
{
    IS25LP064A_status_t status = IS25LP064A_WriteEnableLocked(device);
    if (status != eIS25LP064A_Success) return status;
    IS25LP064A_MakeAddressLocked(device, command, address);
    status = IS25LP064A_TransferLocked(device, command == IS25_CMD_ERASECHIP ? 1U : IS25_ADDRESS_PREFIX_BYTES);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_WaitModificationLocked(device, timeout_ms);
    if (status != eIS25LP064A_Success) return status;
    return IS25LP064A_VerifyLocked(device, address, NULL, length, true, false);
}

/*
 * Updates an arbitrary byte range, preserving all bytes outside that range
 * within each affected 4 KiB sector. The complete operation is blocking and
 * holds the device context; the shared SPI bus is released between frames.
 *
 * INPUT:
 *   device            - Context initialized by a successful StartBlocking.
 *   address           - First FLASH byte to replace; need not be aligned.
 *   data              - New bytes; must stay valid and unchanged until return.
 *   length            - Nonzero byte count wholly inside the FLASH capacity.
 *   sectorBuffer      - Caller-owned writable work buffer, at least 4096 bytes.
 *   sectorBufferSize  - Available work-buffer size in bytes.
 *   data, sectorBuffer and device must not overlap. This is a caller contract.
 *
 * OUTPUT:
 *   FLASH contains the requested bytes; surrounding bytes are preserved on
 *   success. sectorBuffer is scratch and contains the last processed sector.
 *   No heap allocation or additional sector buffer is used in the context.
 *
 * PROCEDURE:
 *   Check protection over ALL affected complete sectors before any change.
 *   Read a complete sector, compare the requested bytes and merge new values.
 *   If unchanged, do no program/erase. If only 1->0 transitions are needed,
 *   program the requested slice without erasure. If any 0->1 transition is
 *   needed, erase that sector and program its complete merged image.
 *   Verify the entire sector, including preserved bytes, before moving on.
 *
 * RETURN / LOG:
 *   Success only after all requested sectors are verified. Otherwise return
 *   the first argument, ownership, protection, SPI, timeout or verification
 *   error. completedBytes counts requested bytes in fully completed sectors,
 *   including unchanged sectors, NOT the full sectors rewritten internally.
 *   A failing sector may already have been changed; earlier sectors are not
 *   rolled back. Protection is conservatively checked even for equal data.
 *
 * POWER LOSS:
 *   This read/modify/erase/program sequence is NOT atomic. Power loss or a
 *   write error after erase can destroy both requested and preserved data in
 *   that sector. Filesystem/journal recovery must be implemented above it.
 */
IS25LP064A_status_t IS25LP064A_UpdateBlocking(IS25LP064A_device_t *device,
    uint32_t address, const void *data, uint32_t length,
    void *sectorBuffer, size_t sectorBufferSize)
{
    const uint8_t *input = data;
    uint8_t *image = sectorBuffer;
    uint32_t done = 0U;
    uint32_t firstSector, endSector;
    IS25LP064A_status_t status;
    if (data == NULL || sectorBuffer == NULL ||
        sectorBufferSize < IS25LP064A_SECTOR_SIZE_BYTES ||
        !IS25LP064A_RangeValid(address, length))
        return eIS25LP064A_InvalidArgument;
    firstSector = address - address % IS25LP064A_SECTOR_SIZE_BYTES;
    endSector = (uint32_t)(((address + length - 1U) /
        IS25LP064A_SECTOR_SIZE_BYTES + 1U) * IS25LP064A_SECTOR_SIZE_BYTES);
    status = IS25LP064A_Acquire(device, true);
    if (status != eIS25LP064A_Success) return status;
    device->operationLog.address = address;
    device->operationLog.requestedBytes = length;
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    if (status != eIS25LP064A_Success) goto end;
    status = IS25LP064A_CheckProtectionLocked(device, firstSector, endSector - firstSector);
    if (status != eIS25LP064A_Success) goto end;
    while (done < length)
    {
        uint32_t current = address + done;
        uint32_t offset = (uint32_t)(current % IS25LP064A_SECTOR_SIZE_BYTES);
        uint32_t sector = current - offset;
        uint32_t count = (uint32_t)IS25LP064A_SECTOR_SIZE_BYTES - offset;
        bool changed = false, requiresErase = false;
        if (count > length - done) count = length - done;
        for (uint32_t read = 0U; read < IS25LP064A_SECTOR_SIZE_BYTES;
             read += (uint32_t)IS25LP064A_PAGE_SIZE_BYTES)
        {
            status = IS25LP064A_ReadChunkLocked(device, sector + read,
                (uint32_t)IS25LP064A_PAGE_SIZE_BYTES);
            if (status != eIS25LP064A_Success) goto end;
            memcpy(image + read, device->rxData + IS25_ADDRESS_PREFIX_BYTES,
                IS25LP064A_PAGE_SIZE_BYTES);
        }
        for (uint32_t i = 0U; i < count; ++i)
        {
            uint8_t old = image[offset + i], value = input[done + i];
            if (old != value) changed = true;
            if ((old & value) != value) requiresErase = true;
            image[offset + i] = value;
        }
        if (changed)
        {
            if (requiresErase)
            {
                status = IS25LP064A_EraseUnitLocked(device, sector,
                    (uint32_t)IS25LP064A_SECTOR_SIZE_BYTES, IS25_CMD_ERASE4,
                    IS25LP064A_SECTOR_TIMEOUT_MS);
                if (status != eIS25LP064A_Success) goto end;
                status = IS25LP064A_ProgramRangeLocked(device, sector, image,
                    (uint32_t)IS25LP064A_SECTOR_SIZE_BYTES, false);
            }
            else
                status = IS25LP064A_ProgramRangeLocked(device, current,
                    image + offset, count, false);
            if (status != eIS25LP064A_Success) goto end;
            status = IS25LP064A_VerifyLocked(device, sector, image,
                (uint32_t)IS25LP064A_SECTOR_SIZE_BYTES, false, false);
            if (status != eIS25LP064A_Success) goto end;
        }
        done += count;
        device->operationLog.completedBytes = done;
    }
end:
    return IS25LP064A_Finish(device, status);
}

/* Shared public implementation for aligned erasures. The complete range is
 * checked before its first erase. Updates completedBytes by fully verified units.
 */
static IS25LP064A_status_t IS25LP064A_EraseBlocking(IS25LP064A_device_t *device,
    uint32_t address, uint32_t length, uint32_t unit, uint8_t command, uint32_t timeout_ms)
{
    uint32_t done = 0U;
    IS25LP064A_status_t status;
    if (!IS25LP064A_RangeValid(address, length) || address % unit != 0U || length % unit != 0U)
        return eIS25LP064A_InvalidArgument;
    status = IS25LP064A_Acquire(device, true);
    if (status != eIS25LP064A_Success) return status;
    device->operationLog.address = address; device->operationLog.requestedBytes = length;
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    if (status == eIS25LP064A_Success) status = IS25LP064A_CheckProtectionLocked(device, address, length);
    while (status == eIS25LP064A_Success && done < length)
    {
        status = IS25LP064A_EraseUnitLocked(device, address + done, unit, command, timeout_ms);
        if (status == eIS25LP064A_Success) { done += unit; device->operationLog.completedBytes = done; }
    }
    return IS25LP064A_Finish(device, status);
}

/* INPUT: BYTE address aligned to 4096, not a sector index. OUTPUT: one erased sector. */
IS25LP064A_status_t IS25LP064A_EraseSectorBlocking(IS25LP064A_device_t *device, uint32_t address)
{
    return IS25LP064A_EraseBlocking(device, address, IS25LP064A_SECTOR_SIZE_BYTES,
        IS25LP064A_SECTOR_SIZE_BYTES, IS25_CMD_ERASE4, IS25LP064A_SECTOR_TIMEOUT_MS);
}

/* INPUT: address aligned to 32768. OUTPUT: one erased/verified 32 KiB block. */
IS25LP064A_status_t IS25LP064A_EraseBlock32Blocking(IS25LP064A_device_t *device, uint32_t address)
{
    return IS25LP064A_EraseBlocking(device, address, 32768U, 32768U,
        IS25_CMD_ERASE32, IS25LP064A_BLOCK32_TIMEOUT_MS);
}

/* INPUT: address aligned to 65536. OUTPUT: one erased/verified 64 KiB block. */
IS25LP064A_status_t IS25LP064A_EraseBlock64Blocking(IS25LP064A_device_t *device, uint32_t address)
{
    return IS25LP064A_EraseBlocking(device, address, 65536U, 65536U,
        IS25_CMD_ERASE64, IS25LP064A_BLOCK64_TIMEOUT_MS);
}

/* INPUT: address and length both multiples of 4096. OUTPUT: sequential sectors erased.
 * No outward rounding is done: neighboring bytes must never be erased implicitly.
 */
IS25LP064A_status_t IS25LP064A_EraseRangeBlocking(IS25LP064A_device_t *device, uint32_t address, uint32_t length)
{
    return IS25LP064A_EraseBlocking(device, address, length, IS25LP064A_SECTOR_SIZE_BYTES,
        IS25_CMD_ERASE4, IS25LP064A_SECTOR_TIMEOUT_MS);
}

/* Explicit whole-chip destructive operation, never called by startup. */
IS25LP064A_status_t IS25LP064A_EraseChipBlocking(IS25LP064A_device_t *device)
{
    return IS25LP064A_EraseBlocking(device, 0U, IS25LP064A_CAPACITY_BYTES,
        IS25LP064A_CAPACITY_BYTES, IS25_CMD_ERASECHIP, IS25LP064A_CHIP_TIMEOUT_MS);
}

/* INPUT: initialized context. OUTPUT: clears BP0..BP3 only, preserving QE/SRWD.
 * Explicit nonvolatile configuration write; not called automatically at startup.
 * Hardware write protection can reject it, which is detected by read-back.
 * Never writes OTP TBS or information-row lock bits.
 */
IS25LP064A_status_t IS25LP064A_ClearBlockProtectionBlocking(IS25LP064A_device_t *device)
{
    uint8_t original, requested, observed;
    IS25LP064A_status_t status = IS25LP064A_Acquire(device, true);
    if (status != eIS25LP064A_Success) return status;
    status = IS25LP064A_WaitReadyLocked(device, IS25LP064A_READY_TIMEOUT_MS);
    if (status != eIS25LP064A_Success) goto done;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &original);
    if (status != eIS25LP064A_Success || (original & IS25_STATUS_BP) == 0U) goto done;
    requested = original & 0xC0U;
    status = IS25LP064A_WriteEnableLocked(device);
    if (status != eIS25LP064A_Success) goto done;
    device->txData[0] = IS25_CMD_WRSR; device->txData[1] = requested;
    status = IS25LP064A_TransferLocked(device, 2U);
    if (status != eIS25LP064A_Success) goto done;
    status = IS25LP064A_WaitModificationLocked(device, IS25LP064A_REGISTER_TIMEOUT_MS);
    if (status != eIS25LP064A_Success) goto done;
    status = IS25LP064A_ReadRegisterLocked(device, IS25_CMD_STATUS, &observed);
    if (status == eIS25LP064A_Success && (observed & 0xFCU) != requested)
        status = eIS25LP064A_WriteProtected;
done:
    return IS25LP064A_Finish(device, status);
}
