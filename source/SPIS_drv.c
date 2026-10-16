/*
 * SPIS_drv.c
 *
 *  Created on: 6. okt. 2026
 *      Author: pc
 */

/* ------------------------ I N C L U D E    F I L E S ------------------------ */
#include "fsl_dspi.h"
#include "SPIS_drv.h"
#include "board.h"

/* ------------------- L O C A L     D E F I N I T I O N S  ------------------- */
typedef struct SPIs_transferContext_t
{
    dspi_transfer_t transfer;

    SPI_completionCallback_t callback;
    void *userData;

} SPIs_transferContext_t;

/* ------------------ L O C A L           F U N C T I O N S   ---------------- */

/* ------------------------- G L O B A L     D A T A   ------------------------ */
dspi_master_handle_t g_spi0Handle;
dspi_master_handle_t g_spi1Handle;
dspi_master_handle_t g_spi2Handle;




/* -------------------------- L O C A L     D A T A   ------------------------- */


/* ------------------ E X T E R N A L      F U N C T I O N S   ---------------- */


/* ------------------ E X T E R N A L      D A T A ---------------------------- */


/* --------------------- L O C A L     F U N C T I O N S   -------------------- */
static volatile bool *SPIs_GetBusLock(SPI_Type *base);

/* ----------------------  I M P L E M E N T A T I O N   ---------------------- */

/* One persistent transfer context for each peripheral. */
static SPIs_transferContext_t SPIs_transferContext[3] = { 0 };


static dspi_master_handle_t * const SPIs_handles[3] =
{
    &g_spi0Handle,
    &g_spi1Handle,
    &g_spi2Handle
};

/**
 * @brief Completes a shared-bus asynchronous transfer.
 *
 * Saves the completion recipient locally, releases the bus,
 * and then notifies the device driver.
 *
 * Called in interrupt context.
 * Device callbacks must therefore remain short and nonblocking.
 */
static void SPIs_TransferCompleted(SPI_Type *base, dspi_master_handle_t *handle, status_t status, void *userData )
{
	SPIs_transferContext_t *context = (SPIs_transferContext_t*) userData;

	SPI_completionCallback_t callback = context->callback;
	void *callbackUserData = context->userData;

	(void) handle;

	context->callback = NULL;
	context->userData = NULL;

	SPIs_Release(base);

	if (callback != NULL) {
		callback(status, callbackUserData);
	}
}


/*
 * Initialize the DSPI peripheral for AD9959 communication.
 *
 * Configuration:
 * - Master operation.
 * - Eight bits per SPI word.
 * - Clock idle low.
 * - Data sampled on the first, rising clock edge.
 * - Most significant bit transmitted first.
 * - Active-low peripheral chip select.
 *
 * sourceClock_Hz is the DSPI peripheral source clock,
 * not the DDS crystal frequency or DDS system clock.
 *
 * Pin multiplexing must be configured before this call.
 * Call this function only during initial shared-bus setup,
 * with no transfer in progress.
 *
 * This function does not initialize the shared interrupt
 * transfer handle or configure the AD9959 CSR register.
 */
/**
 * @brief Initializes a DSPI peripheral and its shared handle.
 *
 * Call once for each peripheral used by the application,
 * after configuring system clocks and pin multiplexing.
 */
status_t SPIs_Init(SPI_Type *p_SPIBase)
{
    dspi_master_config_t config;
    dspi_master_handle_t *handle;
    uint32_t sourceClock_Hz;

    if (p_SPIBase == SPI0)
    {
        handle = &g_spi0Handle;
    }
    else if (p_SPIBase == SPI1)
    {
        handle = &g_spi1Handle;
    }
    else if (p_SPIBase == SPI2)
    {
        handle = &g_spi2Handle;
    }
    else
    {
        return kStatus_InvalidArgument;
    }

    sourceClock_Hz = CLOCK_GetFreq(kCLOCK_BusClk);

    if (sourceClock_Hz == 0U)
    {
        return kStatus_InvalidArgument;
    }

    DSPI_MasterGetDefaultConfig(&config);

    config.whichCtar = kDSPI_Ctar0;
    config.whichPcs = kDSPI_Pcs0;

    config.enableContinuousSCK = false;
    config.enableModifiedTimingFormat = false;

    DSPI_MasterInit(
        p_SPIBase,
        &config,
        sourceClock_Hz);

    /* All chip selects exposed by SPIs_InitPins() are active low. */
    DSPI_SetAllPcsPolarity(
        p_SPIBase,
        kDSPI_Pcs0ActiveLow | kDSPI_Pcs1ActiveLow |
        kDSPI_Pcs2ActiveLow | kDSPI_Pcs3ActiveLow | kDSPI_Pcs4ActiveLow);

    DSPI_MasterTransferCreateHandle(
        p_SPIBase,
        handle,
        NULL,
        NULL);

    return kStatus_Success;
}

/*
 * Apply communication settings to CTAR0 of an initialized
 * DSPI master peripheral.
 *
 * Preconditions:
 * - The caller owns the shared SPI bus.
 * - The preceding transfer has fully completed.
 * - No transfer is queued or may start during this call.
 * - The peripheral uses standard, non-continuous SCK timing.
 *
 * The function temporarily halts the peripheral while
 * modifying CTAR0 and restores its previous HALT state.
 *
 * sourceClock_Hz is the DSPI peripheral source frequency.
 * It is unrelated to the clock of the external device.
 *
 * Baud rate and delays are quantized by the hardware.
 * Delay values outside the supported range are bounded
 * by the SDK.
 *
 * This function does not:
 * - Initialize the peripheral or its transfer handle.
 * - Acquire or release the bus.
 * - Select a device or change PCS polarity.
 * - Start a transfer.
 *
 * Returns:
 * kStatus_Success         Configuration applied.
 * kStatus_InvalidArgument Invalid parameters or peripheral mode.
 * kStatus_DSPI_Busy       Hardware transfer currently active.
 * kStatus_Fail            Baud rate configuration failed.
 */
status_t SPI_Bus_Configure( SPI_Type *base, const SPI_busConfig_t *config )
{
	uint32_t ctar;
	uint32_t previousCtar;
	uint32_t actualBaudrate_Hz;
	bool wasHalted;
	status_t status = kStatus_Success;

	if ((base == NULL) || (config == NULL) ) {
		return kStatus_InvalidArgument;
	}

	uint32_t sourceClock_Hz = CLOCK_GetFreq(kCLOCK_BusClk);


	if ((config->bitsPerFrame < 4U) || (config->bitsPerFrame > 16U)
			|| (config->baudrate_Hz == 0U)
			|| (config->baudrate_Hz > (sourceClock_Hz / 2U))) {
		return kStatus_InvalidArgument;
	}

	if (((config->cpol != kDSPI_ClockPolarityActiveHigh)
			&& (config->cpol != kDSPI_ClockPolarityActiveLow))
			|| ((config->cpha != kDSPI_ClockPhaseFirstEdge)
					&& (config->cpha != kDSPI_ClockPhaseSecondEdge))
			|| ((config->direction != kDSPI_MsbFirst)
					&& (config->direction != kDSPI_LsbFirst))) {
		return kStatus_InvalidArgument;
	}

	if (!DSPI_IsMaster(base) || ((base->MCR & SPI_MCR_MDIS_MASK) != 0U)) {
		return kStatus_InvalidArgument;
	}


	ctar = SPI_CTAR_FMSZ(config->bitsPerFrame - 1U);

	if (config->cpol == kDSPI_ClockPolarityActiveLow) {
		ctar |= SPI_CTAR_CPOL_MASK;
	}

	if (config->cpha == kDSPI_ClockPhaseSecondEdge) {
		ctar |= SPI_CTAR_CPHA_MASK;
	}

	if (config->direction == kDSPI_LsbFirst) {
		ctar |= SPI_CTAR_LSBFE_MASK;
	}

	previousCtar = base->CTAR[0];

	wasHalted = ((base->MCR & SPI_MCR_HALT_MASK) != 0U);

	DSPI_StopTransfer(base);

	base->CTAR[0] = ctar;

	actualBaudrate_Hz = DSPI_MasterSetBaudRate(base, kDSPI_Ctar0,
			config->baudrate_Hz, sourceClock_Hz);

	if (actualBaudrate_Hz == 0U) {
		base->CTAR[0] = previousCtar;
		status = kStatus_Fail;
	} else {
		(void) DSPI_MasterSetDelayTimes(base, kDSPI_Ctar0, kDSPI_PcsToSck,
				sourceClock_Hz, config->pcsToSckDelay_ns);

		(void) DSPI_MasterSetDelayTimes(base, kDSPI_Ctar0, kDSPI_LastSckToPcs,
				sourceClock_Hz, config->lastSckToPcsDelay_ns);

		(void) DSPI_MasterSetDelayTimes(base, kDSPI_Ctar0,
				kDSPI_BetweenTransfer, sourceClock_Hz,
				config->betweenTransferDelay_ns);
	}

	if (!wasHalted) {
		DSPI_StartTransfer(base);
	}

	return status;
}


/* Shared bus locks. Initially all buses are available. */
static volatile bool SPIs_busLocked[3] = { false };


/*
 * Return the lock associated with a supported peripheral.
 * Returns NULL for an invalid peripheral address.
 */
static volatile bool *SPIs_GetBusLock(SPI_Type *base)
{
    if (base == SPI0)
    {
        return &SPIs_busLocked[0];
    }

    if (base == SPI1)
    {
        return &SPIs_busLocked[1];
    }

    if (base == SPI2)
    {
        return &SPIs_busLocked[2];
    }

    return NULL;
}


/**
 * @brief Attempts to acquire exclusive ownership of a SPI bus.
 *
 * The test and update are performed in a short critical section.
 * The previous interrupt mask is restored before returning.
 *
 * Returns true if ownership was acquired.
 * Returns false if the bus is already owned or base is invalid.
 *
 * This function does not wait or access the SPI hardware.
 */
bool SPIs_TryAcquire(SPI_Type *base)
{
    volatile bool *lock = SPIs_GetBusLock(base);
    uint32_t previousPrimask;
    bool acquired = false;

    if (lock == NULL)
    {
        return false;
    }

    previousPrimask = __get_PRIMASK();
    __disable_irq();

    if (!*lock)
    {
        *lock = true;
        acquired = true;
    }

    __DMB();
    __set_PRIMASK(previousPrimask);

    return acquired;
}


/**
 * @brief Releases a previously acquired SPI bus.
 *
 * Call only after the transfer has fully completed, or after
 * a failed submission that did not start a transfer.
 *
 * The caller must be the current owner of the bus.
 * Ownership identity is not tracked by this implementation.
 *
 * May be called from normal execution or interrupt context.
 * The previous interrupt mask is preserved.
 */
void SPIs_Release(SPI_Type *base)
{
    volatile bool *lock = SPIs_GetBusLock(base);
    uint32_t previousPrimask;

    if (lock == NULL)
    {
        return;
    }

    previousPrimask = __get_PRIMASK();
    __disable_irq();

    /* Complete preceding memory accesses before publishing release. */
    __DMB();

    *lock = false;

    __DMB();
    __set_PRIMASK(previousPrimask);
}

/**
 * @brief Executes a synchronous transfer on a shared SPI bus.
 *
 * Acquires the requested bus, applies the communication
 * configuration, executes the SDK transfer, and releases
 * the bus before returning.
 *
 * @param transaction
 *     Transfer descriptor.
 *     txData must contain dataSize bytes in wire order.
 *     rxData may be NULL when reception is not required.
 *     When supplied, rxData must hold dataSize bytes.
 *
 * @return
 *     kStatus_InvalidArgument for invalid parameters.
 *     kStatus_DSPI_Busy when the bus cannot be acquired.
 *     Otherwise, the configuration or SDK transfer status.
 *
 * Received data is meaningful only on successful completion.
 * The receive buffer may be partially modified on failure.
 *
 * This function does not interpret device commands or
 * generate device-specific update signals.
 */
status_t SPIs_TransferBlocking(const SPI_transaction_t *transaction)
{
	dspi_transfer_t transfer = { 0 };
	status_t status;

	if ((transaction == NULL) || (transaction->config == NULL)
			|| (transaction->txData == NULL) || (transaction->dataSize == 0U)) {
		return kStatus_InvalidArgument;
	}

	if ((transaction->base != SPI0) && (transaction->base != SPI1)
			&& (transaction->base != SPI2)) {
		return kStatus_InvalidArgument;
	}

	if (!SPIs_TryAcquire(transaction->base)) {
		return kStatus_DSPI_Busy;
	}

	status = SPI_Bus_Configure(transaction->base, transaction->config);

	if (status == kStatus_Success) {
		/* The SDK treats this buffer as a transmit source. */
		transfer.txData = (uint8_t*) transaction->txData;

		transfer.rxData = transaction->rxData;
		transfer.dataSize = transaction->dataSize;
		transfer.configFlags = transaction->configFlags;

		status = DSPI_MasterTransferBlocking(transaction->base, &transfer);
	}

	SPIs_Release(transaction->base);

	return status;
}


/**
 * @brief Submits an interrupt-driven transfer on a shared SPI bus.
 *
 * Acquires the bus, configures the communication parameters,
 * and submits the transfer using the peripheral's shared handle.
 *
 * The transaction descriptor and configuration need remain
 * valid only for the duration of this call.
 *
 * The TX and RX buffers must remain valid until completion.
 * TX data is not copied by this function.
 * RX may be NULL when reception is not required.
 *
 * kStatus_Success indicates successful submission.
 * The final transfer status is delivered through callback.
 *
 * On submission failure, the bus is released and the callback
 * is not invoked.
 *
 * On completion, the bus is released before invoking callback.
 */
status_t SPIs_TransferNonBlocking( const SPI_transaction_t *transaction )
{

	SPIs_transferContext_t *context;
	dspi_master_handle_t *handle;

	uint32_t instance;
	status_t status;

	if ((transaction == NULL) || (transaction->config == NULL)
			|| (transaction->txData == NULL) || (transaction->dataSize == 0U)) {
		return kStatus_InvalidArgument;
	}

	if ((transaction->base != SPI0) && (transaction->base != SPI1)
			&& (transaction->base != SPI2)) {
		return kStatus_InvalidArgument;
	}

	if (!SPIs_TryAcquire(transaction->base)) {
		return kStatus_DSPI_Busy;
	}

	status = SPI_Bus_Configure(transaction->base, transaction->config);

	if (status != kStatus_Success) {
		SPIs_Release(transaction->base);
		return status;
	}

	instance = DSPI_GetInstance(transaction->base);

	context = &SPIs_transferContext[instance];
	handle = SPIs_handles[instance];

	context->callback = transaction->callback;
	context->userData = transaction->userData;

	context->transfer.txData = (uint8_t*) transaction->txData;

	context->transfer.rxData = transaction->rxData;
	context->transfer.dataSize = transaction->dataSize;
	context->transfer.configFlags = transaction->configFlags;

	/*
	 * The shared handle was initialized once by SPIs_Init().
	 * Only its completion recipient is assigned here.
	 */
	handle->callback = SPIs_TransferCompleted;
	handle->userData = context;

	status = DSPI_MasterTransferNonBlocking(transaction->base, handle,
			&context->transfer);

	if (status != kStatus_Success) {
		handle->callback = NULL;
		handle->userData = NULL;

		context->callback = NULL;
		context->userData = NULL;

		SPIs_Release(transaction->base);
	}

	return status;
}

#include "fsl_clock.h"
#include "fsl_port.h"

/*
 * Configure SPI0 signals and hardware chip selects.
 * Uses port C, with MOSI on PTD2.
 * Does not initialize the DSPI peripheral.
 */
void SPIs_InitPins(void)
{
    CLOCK_EnableClock(SPI_CLK_PORT_CLOCK);
    CLOCK_EnableClock(SPI_MOSI_PORT_CLOCK);

    PORT_SetPinMux(SPI_CLK_PORT,  SPI_CLK_PIN,  SPI_PIN_MUX);
    PORT_SetPinMux(SPI_MOSI_PORT, SPI_MOSI_PIN, SPI_PIN_MUX);
    PORT_SetPinMux(SPI_MISO_PORT, SPI_MISO_PIN, SPI_PIN_MUX);

    PORT_SetPinMux(SPI_CS0_PORT, SPI_CS0_PIN, SPI_PIN_MUX);
    PORT_SetPinMux(SPI_CS1_PORT, SPI_CS1_PIN, SPI_PIN_MUX);
    PORT_SetPinMux(SPI_CS2_PORT, SPI_CS2_PIN, SPI_PIN_MUX);
    PORT_SetPinMux(SPI_CS3_PORT, SPI_CS3_PIN, SPI_PIN_MUX);
    PORT_SetPinMux(SPI_CS4_PORT, SPI_CS4_PIN, SPI_PIN_MUX);
}
