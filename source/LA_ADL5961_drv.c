/*
 * LA_ADL5961_drv.c
 *
 *  Created on: 2. okt. 2026
 *      Author: pc
 */


/* ------------------------ I N C L U D E    F I L E S ------------------------ */


#include "LA_ADL5961_drv.h"
#include <LA_config.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "fsl_dspi.h"
#include "SPIS_drv.h"
#include "fsl_clock.h"

/* Provided by the hardware configuration */

/* ------------------------ L O C A L    M A C R O S   ------------------------ */


/* Register address */
// #define ADL5961_SPI_CONFIG_A_ADDRESS    	UINT16_C(0x0000)

/* Register reset value */
// #define ADL5961_SPI_CONFIG_A_RESET      	UINT8_C(0x00)
/* Default LO frequency increment */
// #define ADL5961_DEFULT_LO_SCAN_STEP_HZ    	100000UL
/* Maximum number of stored scan points */
#define ADL5961_SCAN_MAX_POINTS    			512UL	// 512 about 70KB



/* Peak validation settings. */
#define ADL5961_SCAN_PEAK_MIN_RATIO          2.0f
#define ADL5961_SCAN_BACKGROUND_PERCENT      90UL
#define ADL5961_SCAN_PEAK_MAX_WIDTH_HZ       1000000UL
#define ADL5961_SCAN_PEAK_PLATEAU_TOLERANCE  0.02f

/* Nominal detector conversion settings. */
#define ADL5961_FW_DETECTOR_SLOPE_MV_DB      24.0f
#define ADL5961_FW_DETECTOR_INTERCEPT_DBV    (-108.0f)

#define ADL5961_RW_DETECTOR_SLOPE_MV_DB      24.0f
#define ADL5961_RW_DETECTOR_INTERCEPT_DBV    (-108.0f)

/* ------------------- L O C A L     D E F I N I T I O N S  ------------------- */




/* Software reset */
typedef enum eResetAssert_t
{
    eReset_DeAsserted = 0,
    eReset_Asserted   = 1
} eResetAssert_t;

/* Serial bit order */
typedef enum eSpiBitOrder_t
{
    eSpiBitOrder_MsbFirst = 0,
    eSpiBitOrder_LsbFirst = 1
} eSpiBitOrder_t;

/* Streaming address direction */
typedef enum eSpiAddressDirection_t
{
    eSpiAddressDirection_Decrement = 0,
    eSpiAddressDirection_Increment = 1
} eSpiAddressDirection_t;



/*
 * Packing syntax for GCC / MCUXpresso.
 * Bit fields assume allocation from bit 0 upward.
 */
typedef union ADL5961_SPI_CONFIG_A_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bit 0: software reset */
        eResetAssert_t
            eResetAssert : 1;

        /* Bit 1: serial bit order */
        eSpiBitOrder_t
            eBitOrder : 1;

        /* Bit 2: streaming address direction */
        eSpiAddressDirection_t
            eAddressDirection : 1;

        /* Bits 3 and 4: reserved, keep zero */
        uint8_t : 2;

        /* Bit 5: copy of bit 2 */
        eSpiAddressDirection_t
            eAddressDirectionMirror : 1;

        /* Bit 6: copy of bit 1 */
        eSpiBitOrder_t
            eBitOrderMirror : 1;

        /* Bit 7: copy of bit 0 */
        eResetAssert_t
            eResetAssertMirror : 1;

    } bits;

} ADL5961_SPI_CONFIG_A_t;


_Static_assert(sizeof(ADL5961_SPI_CONFIG_A_t) == 1,
               "SPI_CONFIG_A must occupy exactly one byte");

/* SINGLE_INSTRUCTION */
typedef enum eInstructionMode_t
{
    eInstructionMode_Streaming = 0,
    eInstructionMode_Single    = 1
} eInstructionMode_t;

/* CSB_STALL */
typedef enum eCsbStall_t
{
    eCsbStall_Disabled = 0,
    eCsbStall_Enabled  = 1
} eCsbStall_t;

/* CONTROLLER_TARGET_RB */
typedef enum eControllerTargetRb_t
{
    eControllerTargetRb_TargetRegisters     = 0,
    eControllerTargetRb_ControllerFlipFlops = 1
} eControllerTargetRb_t;


typedef union ADL5961_SPI_CONFIG_B_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [4:0]: reserved, keep zero */
        uint8_t sp : 5;

        /* Bit 5: CONTROLLER_TARGET_RB */
        eControllerTargetRb_t eControllerTargetRb : 1;

        /* Bit 6: CSB_STALL */
        eCsbStall_t eCsbStall : 1;

        /* Bit 7: SINGLE_INSTRUCTION */
        eInstructionMode_t eInstructionMode : 1;

    } bits;

} ADL5961_SPI_CONFIG_B_t;


_Static_assert(sizeof(ADL5961_SPI_CONFIG_B_t) == 1,
               "SPI_CONFIG_B must occupy exactly one byte");


/* SHUTDOWN */
typedef enum eShutdown_t
{
    eShutdown_NormalOperation = 0,
    eShutdown_Asserted        = 1
} eShutdown_t;


typedef union ADL5961_DEVICE_CONFIG_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bit 0: reserved, keep zero */
        uint8_t sp0 : 1;

        /* Bit 1: SHUTDOWN */
        eShutdown_t eShutdown : 1;

        /* Bits [7:2]: reserved, keep zero */
        uint8_t sp1 : 6;

    } bits;

} ADL5961_DEVICE_CONFIG_t;


_Static_assert(sizeof(ADL5961_DEVICE_CONFIG_t) == 1,
               "DEVICE_CONFIG must occupy exactly one byte");







typedef union ADL5961_LO_CONFIG_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [1:0]: LOMODE */
        eLoMode_t eLoMode : 2;

        /* Bits [3:2]: OFMODE */
        eOfMode_t eOfMode : 2;

        /* Bit 4: BYPASS */
        eBypass_t eBypass : 1;

        /* Bits [7:5]: reserved, keep zero */
        uint8_t sp : 3;

    } bits;

} ADL5961_LO_CONFIG_t;


_Static_assert(sizeof(ADL5961_LO_CONFIG_t) == 1,
               "LO_CONFIG must occupy exactly one byte");




typedef union ADL5961_CT2_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [4:0]: CT2 filter setting */
        eCt2_t eCt2 : 5;

        /* Bits [7:5]: reserved, keep zero */
        uint8_t sp : 3;

    } bits;

} ADL5961_CT2_t;


_Static_assert(sizeof(ADL5961_CT2_t) == 1,
               "CT2 must occupy exactly one byte");




typedef union ADL5961_CT4_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [3:0]: CT4 filter setting */
        eCt4_t eCt4 : 4;

        /* Bits [7:4]: reserved, keep zero */
        uint8_t sp : 4;

    } bits;

} ADL5961_CT4_t;


_Static_assert(sizeof(ADL5961_CT4_t) == 1,
               "CT4 must occupy exactly one byte");





typedef union ADL5961_FGAIN_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [6:0]: forward IF amplifier gain */
        eGain_t eFgain : 7;

        /* Bit 7: reserved, keep zero */
        uint8_t sp : 1;

    } bits;

} ADL5961_FGAIN_t;


typedef char ADL5961_FGAIN_SizeCheck[
    (sizeof(ADL5961_FGAIN_t) == 1U) ? 1 : -1
];

typedef union ADL5961_RGAIN_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [6:0]: reverse IF amplifier gain */
        eGain_t eRgain : 7;

        /* Bit 7: reserved, keep zero */
        uint8_t sp : 1;

    } bits;

} ADL5961_RGAIN_t;


typedef char ADL5961_RGAIN_SizeCheck[
    (sizeof(ADL5961_RGAIN_t) == 1) ? 1 : -1
];





typedef union ADL5961_CIF2_CIF1_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [3:0]: CIF1 */
        eCif1_t eCif1 : 4;

        /* Bits [7:4]: CIF2 */
        eCif2_t eCif2 : 4;

    } bits;

} ADL5961_CIF2_CIF1_t;


typedef char ADL5961_CIF2_CIF1_SizeCheck[
    (sizeof(ADL5961_CIF2_CIF1_t) == 1) ? 1 : -1
];


typedef union ADL5961_TDEG_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [4:0]: temperature code */
        uint8_t tDeg : 5;

        /* Bits [7:5]: reserved */
        uint8_t sp : 3;

    } bits;

} ADL5961_TDEG_t;


typedef char ADL5961_TDEG_SizeCheck[
    (sizeof(ADL5961_TDEG_t) == 1) ? 1 : -1
];

typedef uint8_t eChipType_t;

typedef struct __attribute__((packed)) ADL5961_regs_t
{
    ADL5961_SPI_CONFIG_A_t SPI_CONFIG_A; /* 0x0000 */
    ADL5961_SPI_CONFIG_B_t SPI_CONFIG_B; /* 0x0001 */
    ADL5961_DEVICE_CONFIG_t DEVICE_CONFIG; /* 0x0002 */
    eChipType_t CHIPTYPE;               /* 0x0003 */

    /* Reserved addresses: 0x0004 through 0x001F */
    uint8_t sp[28];

    ADL5961_LO_CONFIG_t LO_CONFIG;      /* 0x0020 */
    ADL5961_CT2_t CT2;                  /* 0x0021 */
    ADL5961_CT4_t CT4;                  /* 0x0022 */
    ADL5961_FGAIN_t FGAIN;              /* 0x0023 */
    ADL5961_RGAIN_t RGAIN;              /* 0x0024 */
    ADL5961_CIF2_CIF1_t CIF2_CIF1;       /* 0x0025 */
    ADL5961_TDEG_t TDEG;                /* 0x0026 */

} ADL5961_regs_t;


/* Verify register map layout */
typedef char ADL5961_regs_SizeCheck[
    (sizeof(ADL5961_regs_t) == 0x27) ? 1 : -1
];

typedef char ADL5961_regs_LoConfigOffsetCheck[
    (__builtin_offsetof(ADL5961_regs_t, LO_CONFIG) == 0x20) ? 1 : -1
];

typedef char ADL5961_regs_TdegOffsetCheck[
    (__builtin_offsetof(ADL5961_regs_t, TDEG) == 0x26) ? 1 : -1
];


/* Read/write selection */
typedef enum eReadWrite_t
{
    eReadWrite_Write = 0,
    eReadWrite_Read  = 1
} eReadWrite_t;


/* First command byte */
typedef union ADL5961_SPI_CMD_ADDR_HIGH_t
{
    uint8_t b8;

    struct __attribute__((packed))
    {
        /* Bits [6:0]: register address bits [14:8] */
        uint8_t addressHigh : 7;

        /* Bit 7: read/write selection */
        eReadWrite_t eReadWrite : 1;

    } bits;

} ADL5961_SPI_CMD_ADDR_HIGH_t;


/* Single-register SPI transaction */
typedef union ADL5961_SPI_TRANSACTION_t
{
    uint8_t b8[3];

    struct __attribute__((packed))
    {
        /* Byte 0: read/write and upper address */
    	ADL5961_SPI_CMD_ADDR_HIGH_t cmdAddrHigh;

        /* Byte 1: register address bits [7:0] */
        uint8_t addressLow;

        /* Byte 2: transmitted or received register value */
        uint8_t data;

    } bytes;

} ADL5961_SPI_TRANSACTION_t;


typedef char ADL5961_SPI_ADDRESS_HIGH_SizeCheck[
    (sizeof(ADL5961_SPI_CMD_ADDR_HIGH_t) == 1) ? 1 : -1
];

typedef char ADL5961_SPI_TRANSACTION_SizeCheck[
    (sizeof(ADL5961_SPI_TRANSACTION_t) == 3) ? 1 : -1
];

/* Persistent non-blocking transfer context */
typedef struct ADL5961_transferContext_t
{
    ADL5961_SPI_TRANSACTION_t transaction;
    uint8_t rxData[3];
    dspi_transfer_t transfer;

    volatile status_t status;
    volatile bool pending;
    volatile bool completed;

} ADL5961_transferContext_t;


/* Qualification of the acquired channel level */
typedef enum eSignalQualification_t
{
    eSignalQualification_NotEvaluated = 0,
    eSignalQualification_Qualified,
    eSignalQualification_BelowWindow,
    eSignalQualification_AboveWindow,
    eSignalQualification_Saturation
} eSignalQualification_t;


typedef enum eScanValidation_t
{
    eScanValidation_NotEvaluated = 0,
    eScanValidation_Collecting,
    eScanValidation_Rejected,
    eScanValidation_Approved
} eScanValidation_t;

typedef enum eScanDirection_t
{
    eScanDirection_Ascending = 0,
    eScanDirection_Descending
} eScanDirection_t;


/* Channels contributing to a detection */
typedef enum eSignalSource_t
{
    eSignalSource_None = 0,
    eSignalSource_Forward,
    eSignalSource_Reverse,
    eSignalSource_Both
} eSignalSource_t;

/* Summary of one scan pass */
typedef struct ADL5961_scanPass_t
{
    /* Direction of this scan pass. */
    eScanDirection_t eDirection;

    /* All expected points have been acquired. */
    bool completed;

    /* Peak analysis has been performed. */
    bool evaluated;

    /* A unique peak satisfying the configured criteria was found. */
    bool qualified;

    /* Peak position expressed as LO frequency. */
    uint32_t peakLoFrequency_Hz;

    /* Gain-compensated peak amplitude, in linear units. */
    float peakAmplitude;

} ADL5961_scanPass_t;

/* Evaluation of the retained scan window */
typedef struct ADL5961_scanValidation_t
{
    eScanValidation_t eValidation;

    uint32_t evaluatedPassCount;
    uint32_t stablePassCount;

    bool frequencyStable;
    bool amplitudeStable;
    bool singleSignal;
    bool remainingBandClean;

    uint32_t frequencySpread_Hz;
    float signalLevelSpread_dB;

} ADL5961_scanValidation_t;


/* One scan point, acquired after settling */
typedef struct ADL5961_scanLogEntry_t
{
    /* Actual LO frequency reported by the DDS */
    uint32_t loFrequency_Hz;

    /* Forward channel */
    uint16_t fwSignalStrength_mV;
    eGain_t eFgain;
    eSignalQualification_t eFwQualification;

    /* Reverse channel */
    uint16_t rwSignalStrength_mV;
    eGain_t eRgain;
    eSignalQualification_t eRwQualification;

} ADL5961_scanLogEntry_t;


typedef struct ADL5961_scan_t
{
    /* Selected LO scan interval */
    uint32_t loFrequencyMin_Hz;
    uint32_t loFrequencyMax_Hz;
    uint32_t step_Hz;

    uint32_t pointCount;

    /* Acquired points in the current pass: never exceeds pointCount */
    uint32_t acquiredPointCount;

    /* Current pass direction */
    eScanDirection_t eDirection;

    /* Completed pass counters */
    uint64_t ascendingScanCount;
    uint64_t descendingScanCount;

    /* Circular history slot assigned to the current pass */
    uint32_t windowWriteIndex;

    /* Number of completed passes currently retained */
    uint32_t completedWindowCount;

    /* Summary associated with each history slot */
    ADL5961_scanPass_t pass[SCAN_WINDOW_MAX_LEN];

    /* Point index first, scan-history index second */
    ADL5961_scanLogEntry_t
        log[ADL5961_SCAN_MAX_POINTS][SCAN_WINDOW_MAX_LEN];

    /* Latest window evaluation */
    ADL5961_scanValidation_t validation;

} ADL5961_scan_t;


/* ------------------ L O C A L           F U N C T I O N S   ---------------- */

/* ------------------------- G L O B A L     D A T A   ------------------------ */


/* -------------------------- L O C A L     D A T A   ------------------------- */
static ADL5961_scan_t ADL5961_scan;
static ADL5961_transferContext_t ADL5961_transferContext;
static ADL5961_regs_t ADL5961_regs;

/* ------------------ E X T E R N A L      F U N C T I O N S   ---------------- */




/* ------------------ E X T E R N A L      D A T A ---------------------------- */

// External data


/* --------------------- L O C A L     F U N C T I O N S   -------------------- */
// static prototypes

static status_t ADL5961_WriteRegisterBlocking(uint8_t address, uint8_t data);
static status_t ADL5961_ReadRegisterBlocking(uint8_t address, uint8_t *data);
static status_t ADL5961_WriteRegisterNonBlocking(uint8_t address, uint8_t data);
static status_t ADL5961_ReadRegisterNonBlocking(uint8_t address);
static void ADL5961_SPI_TransferCompleted(status_t status);
static uint32_t ADL5961_GetScanPointCount(uint32_t min_Hz, uint32_t max_Hz, uint32_t step_Hz );
static bool ADL5961_ConfigureScan(uint32_t min_Hz, uint32_t max_Hz, uint32_t step_Hz );



/* ----------------------  I M P L E M E N T A T I O N   ---------------------- */

static const SPI_busConfig_t ADL5961_spiConfig =
{
    .baudrate_Hz = ADL5961_SPI_BAUDRATE_HZ,
    .bitsPerFrame = 8U,

    .cpol = kDSPI_ClockPolarityActiveHigh,
    .cpha = kDSPI_ClockPhaseFirstEdge,
    .direction = kDSPI_MsbFirst,

    .pcsToSckDelay_ns = ADL5961_SPI_DELAY_NS,
    .lastSckToPcsDelay_ns = ADL5961_SPI_DELAY_NS,
    .betweenTransferDelay_ns = ADL5961_SPI_DELAY_NS
};


/**
 * @brief Forwards SDK transfer completion to the ADL5961 driver.
 */
static void ADL5961_SPI_Callback(status_t status, void *userData )
{
	(void) userData;

	ADL5961_SPI_TransferCompleted(status);
}

static status_t ADL5961_TransferNonBlocking( ADL5961_SPI_TRANSACTION_t *transaction )
{
	ADL5961_transferContext_t *context = &ADL5961_transferContext;

	status_t status;

	if (transaction == NULL) {
		return kStatus_InvalidArgument;
	}

	if (context->pending) {
		return kStatus_DSPI_Busy;
	}

	context->transaction = *transaction;

	for (size_t i = 0U; i < sizeof(context->rxData); ++i) {
		context->rxData[i] = 0U;
	}

	SPI_transaction_t spiTransaction = {
			.base = ADL5961_SPI,
			.config = &ADL5961_spiConfig,
			.txData = context->transaction.b8,
			.rxData = context->rxData,
			.dataSize = sizeof(context->transaction.b8),
			.configFlags = ADL5961_SPI_TRANSFER_FLAGS,
			.callback = ADL5961_SPI_Callback,
			.userData = context
	};

	context->status = kStatus_DSPI_Busy;
	context->completed = false;
	context->pending = true;

	status = SPIs_TransferNonBlocking(&spiTransaction);

	if (status != kStatus_Success) {
		context->status = status;
		context->completed = false;
		context->pending = false;
	}

	return status;
}


// Implementation

/**
 * @brief Builds a three-byte SPI transaction for accessing one ADL5961
 *        register. Without sending .
 *
 * The returned transaction contains:
 *
 *     Byte 0:
 *         Bit 7     : Read/write selection.
 *         Bits [6:0]: Register address bits [14:8], set to zero.
 *
 *     Byte 1:
 *         Register address bits [7:0].
 *
 *     Byte 2:
 *         Register value to transmit for a write operation, or a
 *         zero-valued dummy byte to generate clocks for a read operation.
 *
 * Only addresses representable by uint8_t are supported by this helper.
 * The upper address bits are always cleared because all documented
 * ADL5961 registers used by this driver fit in the low address byte.
 *
 * For write operations, data must contain the complete register byte,
 * including all fields and the required values of reserved bits.
 * This function does not modify or validate individual register fields.
 *
 * For read operations, the data argument is ignored. The transmitted
 * dummy byte does not contain the register value; the SPI transfer
 * routine must retrieve that value from the receive buffer.
 *
 * This function only prepares the transaction. It does not acquire the
 * shared SPI bus, control chip select, access hardware or start a transfer.
 *
 * @param eReadWrite Operation selection:
 *                   eReadWrite_Write to write a register, or
 *                   eReadWrite_Read to read a register.
 * @param address   Register address, expressed as an unsigned byte.
 * @param data      Complete register value for a write operation.
 *                  Ignored for a read operation.
 *
 * @return Initialized transaction ready to be passed to the internal
 *         SPI transfer routine.
 *
 * @note The caller must provide a valid operation selection and register
 *       address. No argument validation is performed.
 *
 * @note The SPI peripheral must transmit bytes MSB first. The command
 *       bit-field layout must match the compiler layout verified for
 *       the target platform.
 */
static ADL5961_SPI_TRANSACTION_t ADL5961_MakeTransaction( eReadWrite_t eReadWrite, uint8_t address, uint8_t data )
{
    ADL5961_SPI_TRANSACTION_t transaction = { .b8 = { 0 } };

    /* All documented register addresses fit in the low byte */
    transaction.bytes.cmdAddrHigh.bits.addressHigh = 0;
    transaction.bytes.cmdAddrHigh.bits.eReadWrite = eReadWrite;
    transaction.bytes.addressLow = address;
    /* Dummy transmit byte for read operations */
    transaction.bytes.data = ( eReadWrite == eReadWrite_Write ) ? data : 0x00;

    return transaction;
}


/**
 * @brief Executes a blocking, three-byte ADL5961 SPI transaction.
 *
 * The function attempts to acquire the shared SPI bus before starting
 * the transfer. If the bus is unavailable, it returns immediately
 * without accessing the peripheral or modifying the transaction.
 *
 * Once the bus is acquired, the transaction bytes are transmitted
 * through ADL5961_SPI using the configuration selected by
 * ADL5961_SPI_TRANSFER_FLAGS. Received bytes are stored in a local
 * buffer that remains valid throughout the blocking SDK call.
 *
 * For a successful read operation, the third received byte is copied
 * into transaction->bytes.data. The first two received bytes, associated
 * with transmission of the instruction and address, are discarded.
 *
 * For write operations, received bytes are ignored and the supplied
 * transaction remains unchanged.
 *
 * If the SDK transfer returns an error, transaction->bytes.data is
 * left unchanged. The caller must check the returned status before
 * using the result of a read operation.
 *
 * The shared SPI bus is released after the SDK call returns, regardless
 * of whether the transfer succeeded or failed.
 *
 * @param transaction Pointer to an initialized single-register SPI
 *                    transaction. For a successful read, its data
 *                    member receives the register value.
 *
 * @retval kStatus_Success         The SDK transfer completed successfully.
 * @retval kStatus_InvalidArgument The transaction pointer is NULL.
 * @retval kStatus_DSPI_Busy        The shared SPI bus could not be acquired.
 *
 * @return Otherwise, the error status returned by
 *         DSPI_MasterTransferBlocking().
 *
 * @note The SPI peripheral must already be initialized with settings
 *       compatible with the ADL5961 transaction format. Transfer flags
 *       must select the intended device and maintain chip select
 *       throughout the instruction, address and data bytes.
 *
 * @note Bus acquisition is non-blocking, but the subsequent SPI transfer
 *       is blocking. This function implements no timeout of its own.
 *       If the SDK call does not return, the bus remains acquired.
 *
 * @note A successful SDK transfer confirms completion of the SPI
 *       operation; it does not confirm that the device accepted a write
 *       or that a received register value is valid. Such checks belong
 *       to the higher-level driver.
 */
static status_t ADL5961_TransferBlocking(ADL5961_SPI_TRANSACTION_t *transaction)
{

	uint8_t rxData[3] = { 0U };
	status_t status;

	if (transaction == NULL) {
		return kStatus_InvalidArgument;
	}

	SPI_transaction_t spiTransaction =
	{
		.base = ADL5961_SPI,
		.config = &ADL5961_spiConfig,
		.txData = transaction->b8,
		.rxData = rxData,
		.dataSize = sizeof(transaction->b8),
		.configFlags = ADL5961_SPI_TRANSFER_FLAGS,
	};

	status = SPIs_TransferBlocking(&spiTransaction);

	if ((status == kStatus_Success)
			&& (transaction->bytes.cmdAddrHigh.bits.eReadWrite
					== eReadWrite_Read)) {
		transaction->bytes.data = rxData[2];
	}

	return status;
}

/* Write a complete, previously prepared register byte */
static status_t ADL5961_WriteRegisterBlocking(uint8_t address, uint8_t data) {
	ADL5961_SPI_TRANSACTION_t transaction = ADL5961_MakeTransaction(
			eReadWrite_Write, address, data);

	return ADL5961_TransferBlocking(&transaction);
}

/* Read a register; leave *data unchanged on failure */
static status_t ADL5961_ReadRegisterBlocking(uint8_t address, uint8_t *data) {
	ADL5961_SPI_TRANSACTION_t transaction;
	status_t status;

	if (data == NULL) {
		return kStatus_InvalidArgument;
	}

	transaction = ADL5961_MakeTransaction(eReadWrite_Read, address, 0x00);

	status = ADL5961_TransferBlocking(&transaction);

	if (status == kStatus_Success) {
		*data = transaction.bytes.data;
	}

	return status;
}







/* Start a register write */
static status_t ADL5961_WriteRegisterNonBlocking(uint8_t address, uint8_t data)
{
	ADL5961_SPI_TRANSACTION_t transaction = ADL5961_MakeTransaction( eReadWrite_Write, address, data );

	return ADL5961_TransferNonBlocking(&transaction);
}

/* Start a register read */
static status_t ADL5961_ReadRegisterNonBlocking( uint8_t address )
{
	ADL5961_SPI_TRANSACTION_t transaction = ADL5961_MakeTransaction( eReadWrite_Read, address, 0x00);

	return ADL5961_TransferNonBlocking(&transaction);
}


/**
 * @brief Finalizes the ADL5961 driver context after a non-blocking
 *        SPI transfer completes.
 *
 * This function is called by ADL5961_SPI_Callback(), which adapts the
 * SDK callback signature and forwards the transfer completion status.
 * It normally executes in the SPI interrupt context.
 *
 * If no transaction is pending, or completion has already been recorded,
 * the notification is ignored.
 *
 * For a successful read operation, the third received byte is copied
 * into the persistent transaction data field. For write operations or
 * failed transfers, no received register value is copied.
 *
 * The function then stores the final status, releases the shared SPI
 * bus and sets the completed flag to make the result available to
 * ADL5961_GetTransferResult().
 *
 * The pending flag remains set until the application collects the result.
 * This prevents another ADL5961 transaction from overwriting the context
 * before the previous result has been consumed. Other devices may use
 * the shared SPI bus after it has been released.
 *
 * This function does not start another transfer or advance the ADL5961
 * state machine. Higher-level processing must collect the result and
 * decide the next operation.
 *
 * @param status Final transfer status supplied by the SDK callback.
 *               kStatus_Success indicates successful transfer completion;
 *               other values are preserved for the result collector.
 *
 * @note ADL5961_SPI_Release() must be safe to call from interrupt context.
 *
 * @note An immediate failure returned when starting a transfer is handled
 *       by ADL5961_TransferNonBlocking(), without calling this function.
 *
 * @note This function does not implement timeout or abort handling.
 *       Any future abort mechanism must prevent stale completion callbacks
 *       from being applied to a subsequent transaction.
 */

static void ADL5961_SPI_TransferCompleted(status_t status)
{
    ADL5961_transferContext_t *context =
        &ADL5961_transferContext;

    if ((!context->pending) || context->completed)
    {
        return;
    }

    if ((status == kStatus_Success) &&
        (context->transaction.bytes.cmdAddrHigh.bits.eReadWrite ==
         eReadWrite_Read))
    {
        context->transaction.bytes.data = context->rxData[2];
    }

    context->status = status;

    // ADL5961_SPI_Release();

    /* Publish completion after storing the result */
    context->completed = true;
}


/* Collect the result and release the transfer context */
status_t ADL5961_GetTransferResult( uint8_t *data )
{

	ADL5961_transferContext_t *context = &ADL5961_transferContext;

	status_t status;
	bool isRead;

	if (!context->pending) {
		return kStatus_Fail;
	}

	if (!context->completed) {
		return kStatus_DSPI_Busy;
	}

	status = context->status;

	isRead = (context->transaction.bytes.cmdAddrHigh.bits.eReadWrite
			== eReadWrite_Read);

	if ((status == kStatus_Success) && isRead) {
		if (data == NULL) {
			return kStatus_InvalidArgument;
		}

		*data = context->transaction.bytes.data;
	}

	context->completed = false;
	context->pending = false;

	return status;
}


/**
 * @brief Reads all documented ADL5961 registers into a register-map instance.
 *
 * Each register is read through a separate blocking SPI transaction.
 * Register addresses are derived from the corresponding structure offsets.
 *
 * Reserved address gaps are not accessed and remain zero in the snapshot.
 *
 * Readings are first collected in a temporary instance. The destination
 * is updated only if every register read succeeds; otherwise it remains
 * unchanged.
 *
 * @param regs Pointer to the register-map instance receiving the snapshot.
 *
 * @retval kStatus_Success         All register reads completed successfully.
 * @retval kStatus_InvalidArgument The destination pointer is NULL.
 *
 * @return Otherwise, the status returned by the first unsuccessful read.
 *
 * @note The snapshot is acquired sequentially, not atomically. Registers
 *       may change between reads. Call when no other operation is changing
 *       the device configuration.
 *
 * @note Successful SPI transfers do not establish that the returned
 *       register values are correct. Identification and configuration
 *       checks must be performed separately.
 */
status_t ADL5961_ReadRegistersBlocking(ADL5961_regs_t *regs)
{
    static const uint8_t addresses[] =
    {
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, SPI_CONFIG_A),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, SPI_CONFIG_B),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, DEVICE_CONFIG),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CHIPTYPE),

        (uint8_t)__builtin_offsetof(ADL5961_regs_t, LO_CONFIG),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CT2),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CT4),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, FGAIN),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, RGAIN),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CIF2_CIF1),
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, TDEG)
    };

    ADL5961_regs_t snapshot = { 0 };
    uint8_t *bytes = (uint8_t *)&snapshot;

    status_t status;
    size_t i;

    if (regs == NULL)
    {
        return kStatus_InvalidArgument;
    }

    for (i = 0U; i < sizeof(addresses) / sizeof(addresses[0]); ++i)
    {
        uint8_t address = addresses[i];

        status = ADL5961_ReadRegisterBlocking(
            address,
            &bytes[address]);

        if (status != kStatus_Success)
        {
            return status;
        }
    }

    /* Publish the snapshot only after every read succeeds. */
    *regs = snapshot;

    return kStatus_Success;
}

static bool ADL5961_IsGainValid(eGain_t gain)
{
    uint32_t value = (uint32_t)gain;

    return (value <= (uint32_t)eGain_66dB) &&
           ((value % 6U) == 0U);
}

/**
 * @brief Sets the forward-channel IF gain.
 */
status_t ADL5961_SetForwardGainBlocking(eGain_t gain)
{
    ADL5961_FGAIN_t reg = { .b8 = 0U };

    if (!ADL5961_IsGainValid(gain))
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eFgain = gain;

    return ADL5961_WriteRegisterBlocking(
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, FGAIN),
        reg.b8);
}


/**
 * @brief Sets the reverse-channel IF gain.
 */
status_t ADL5961_SetReverseGainBlocking(eGain_t gain)
{
    ADL5961_RGAIN_t reg = { .b8 = 0U };

    if (!ADL5961_IsGainValid(gain))
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eRgain = gain;

    return ADL5961_WriteRegisterBlocking(
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, RGAIN),
        reg.b8);
}

/**
 * @brief Starts a non-blocking forward-channel IF gain write.
 *
 * Success means the transfer was started.
 * Collect its final result before starting another ADL5961 transfer.
 */
status_t ADL5961_SetForwardGainNonBlocking(eGain_t gain)
{
    ADL5961_FGAIN_t reg = { .b8 = 0U };

    if (!ADL5961_IsGainValid(gain))
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eFgain = gain;

    return ADL5961_WriteRegisterNonBlocking(
        (uint8_t)offsetof(ADL5961_regs_t, FGAIN),
        reg.b8);
}

/**
 * @brief Starts a non-blocking reverse-channel IF gain write.
 *
 * Success means the transfer was started.
 * Collect its final result before starting another ADL5961 transfer.
 */
status_t ADL5961_SetReverseGainNonBlocking(eGain_t gain)
{
    ADL5961_RGAIN_t reg = { .b8 = 0U };

    if (!ADL5961_IsGainValid(gain))
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eRgain = gain;

    return ADL5961_WriteRegisterNonBlocking(
        (uint8_t)offsetof(ADL5961_regs_t, RGAIN),
        reg.b8);
}
/**
 * @brief Configures LO processing, offset processing and bypass.
 *
 * This function does not set the external DDS frequency.
 */
status_t ADL5961_SetLoConfigBlocking(eBypass_t bypass, eLoMode_t loMode, eOfMode_t ofMode)
{
	ADL5961_LO_CONFIG_t reg = { .b8 = 0U };

	if (((uint32_t) bypass > 1U) || ((uint32_t) loMode > 3U)
			|| ((uint32_t) ofMode > 3U)) {
		return kStatus_InvalidArgument;
	}

	reg.bits.eBypass = bypass;
	reg.bits.eLoMode = loMode;
	reg.bits.eOfMode = ofMode;

	return ADL5961_WriteRegisterBlocking(
			(uint8_t) __builtin_offsetof(ADL5961_regs_t, LO_CONFIG), reg.b8);
}

/**
 * @brief Asserts or deasserts device shutdown.
 */
status_t ADL5961_SetShutdown(eShutdown_t shutdown)
{
    ADL5961_DEVICE_CONFIG_t reg = { .b8 = 0U };

    if ((uint32_t)shutdown > 1U)
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eShutdown = shutdown;

    return ADL5961_WriteRegisterBlocking(
        (uint8_t)__builtin_offsetof(
            ADL5961_regs_t, DEVICE_CONFIG),
        reg.b8);
}

/**
 * @brief Releases device shutdown.
 *
 * Preserves the previously programmed operating configuration.
 * The external EN pin must already be asserted.
 */
status_t ADL5961_WakeUpBlocking(void)
{
    return ADL5961_SetShutdown(eShutdown_NormalOperation);
}

/**
 * @brief Places the device in shutdown.
 */
status_t ADL5961_ShutBlocking(void)
{
    return ADL5961_SetShutdown(eShutdown_Asserted);
}

/**
 * @brief Sets a documented CT2 tuning code.
 */
status_t ADL5961_SetCt2Blocking(eCt2_t setting)
{
    ADL5961_CT2_t reg = { .b8 = 0U };

    switch (setting)
    {
        case eCt2_Band15To20GHz:
        case eCt2_Band14To15GHz:
        case eCt2_Band11To14GHz:
        case eCt2_Band10To12GHz:
        case eCt2_Band9To10GHz:
        case eCt2_Band8To9GHz:
        case eCt2_Band7To8GHz:
        case eCt2_Band6To7GHz:
        case eCt2_Band5To6GHz:
        case eCt2_Band4To5GHz:
            break;

        default:
            return kStatus_InvalidArgument;
    }

    reg.bits.eCt2 = setting;

    return ADL5961_WriteRegisterBlocking(
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CT2),
        reg.b8);
}

/**
 * @brief Sets both IF filter tuning fields in one register write.
 */
status_t ADL5961_SetIfFiltersBlocking(
    eCif1_t cif1,
    eCif2_t cif2)
{
    ADL5961_CIF2_CIF1_t reg = { .b8 = 0U };

    if (((uint32_t)cif1 > 15U) ||
        ((uint32_t)cif2 > 15U))
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eCif1 = cif1;
    reg.bits.eCif2 = cif2;

    return ADL5961_WriteRegisterBlocking(
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CIF2_CIF1),
        reg.b8);
}

/**
 * @brief Sets the CT4 filter tuning code in the LO multiplier path.
 *
 * CT4 is a four-bit tuning field used in the LO multiply-by-four path.
 * Its setting must be selected together with CT2 according to the
 * frequency plan and the combinations recommended in the datasheet.
 *
 * This function checks that the supplied code fits in the CT4 field,
 * prepares the complete register byte with reserved bits cleared,
 * and performs a blocking register write.
 *
 * It does not select a code from the LO frequency, validate the CT2/CT4
 * combination, change LOMODE or configure bypass.
 *
 * @param setting CT4 tuning code, from eCt4_Setting0 through
 *                eCt4_Setting15.
 *
 * @retval kStatus_Success         The SPI write completed successfully.
 * @retval kStatus_InvalidArgument The supplied code is outside 0 through 15.
 *
 * @return Otherwise, the status returned by the internal blocking
 *         register-write function.
 *
 * @note CT4 does not affect the active LO path when bypass is enabled.
 *
 * @note Successful SPI completion does not verify register readback
 *       or indicate that the analog circuitry has settled.
 *       Any required settling delay must be handled by the state machine.
 */
status_t ADL5961_SetCt4Blocking( eCt4_t setting )
{
    ADL5961_CT4_t reg = { .b8 = 0U };

    if ((uint32_t)setting > 15U)
    {
        return kStatus_InvalidArgument;
    }

    reg.bits.eCt4 = setting;

    return ADL5961_WriteRegisterBlocking(
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, CT4),
        reg.b8);
}


/**
 * @brief Reads the five-bit temperature code.
 *
 * The destination remains unchanged if the read fails.
 * Conversion into degrees Celsius is not performed.
 */
status_t ADL5961_ReadTemperatureCodeBlocking(uint8_t *const code)
{
    ADL5961_TDEG_t reg = { .b8 = 0U };
    status_t status;

    if (code == NULL)
    {
        return kStatus_InvalidArgument;
    }

    status = ADL5961_ReadRegisterBlocking(
        (uint8_t)__builtin_offsetof(ADL5961_regs_t, TDEG),
        &reg.b8);

    if (status == kStatus_Success)
    {
        *code = reg.bits.tDeg;
    }

    return status;
}


static uint32_t ADL5961_GetScanPointCount(
    uint32_t min_Hz,
    uint32_t max_Hz,
    uint32_t step_Hz)
{
    if ((step_Hz == 0UL) || (min_Hz > max_Hz))
    {
        return 0UL;
    }

    return ((max_Hz - min_Hz) / step_Hz) + 1UL;
}

static bool ADL5961_ConfigureScan(uint32_t min_Hz, uint32_t max_Hz,
		uint32_t step_Hz)
{
	uint32_t pointCount;

	if ((min_Hz < ADL5961_LO_FMIN_HZ) || (max_Hz > ADL5961_LO_FMAX_HZ)) {
		return false;
	}

	pointCount = ADL5961_GetScanPointCount(min_Hz, max_Hz, step_Hz);

	if ((pointCount == 0UL) || (pointCount > ADL5961_SCAN_MAX_POINTS)) {
		return false;
	}

	/* Apply configuration only after all checks succeed */
	ADL5961_scan.loFrequencyMin_Hz = min_Hz;
	ADL5961_scan.loFrequencyMax_Hz = max_Hz;
	ADL5961_scan.step_Hz = step_Hz;
	ADL5961_scan.pointCount = pointCount;
	ADL5961_scan.acquiredPointCount = 0UL;

	return true;
}


/**
 * @brief Converts a detector output voltage into a gain-compensated
 * input amplitude.
 * The detector output is assumed to follow a logarithmic transfer function:
 *
 *     detectorLevel_dBV = signalStrength_mV / slope_mV_dB
 *                         + intercept_dBV
 *
 * The programmed ADL5961 gain, expressed in dB, is then subtracted:
 *
 *     inputLevel_dBV = detectorLevel_dBV - gain_dB
 *
 * Finally, the corrected level is converted into a linear RMS voltage:
 *
 *     inputAmplitude_Vrms = 10^(inputLevel_dBV / 20)
 *
 * The enumeration value of gain must represent the actual gain in dB.
 * This conversion compensates only the supplied gain value; additional
 * losses, fixed gains and frequency-dependent corrections are not included.
 *
 * The caller must provide a valid measurement within the detector's usable
 * range and a finite, strictly positive detector slope. This function does
 * not check saturation, acquisition settling or parameter validity.
 *
 * @param signalStrength_mV Detector DC output voltage, in millivolts.
 * @param gain             Programmed ADL5961 gain, expressed in dB.
 * @param slope_mV_dB      Detector logarithmic slope, in millivolts per dB.
 * @param intercept_dBV    Detector input level corresponding to the
 *                         extrapolated zero-volt output, in dBV.
 *
 * @return Estimated gain-compensated input amplitude, in volts RMS.
 */

 static float ADL5961_GetInputAmplitude(uint16_t signalStrength_mV, eGain_t gain, float slope_mV_dB, float intercept_dBV )
{
	float inputLevel_dBV;

	/* Convert detector voltage and compensate programmed gain. */
	inputLevel_dBV = ((float) signalStrength_mV / slope_mV_dB) + intercept_dBV
			- (float) gain;

	/* Convert dBV into volts RMS. */
	return powf(10.0f, inputLevel_dBV / 20.0f);
}


 /**
  * @brief Calculates the combined amplitude associated with one scan point.
  *
  * The forward and reflected detector readings are converted separately
  * into linear RMS amplitudes, using their respective detector parameters
  * and the gains stored in the log entry.
  *
  * The combined amplitude is calculated as:
  *
  *     combinedAmplitude = sqrt(fwAmplitude^2 + rwAmplitude^2)
  *
  * This value is a score used to detect a peak across the scan. It is not
  * the incident-wave amplitude or a reflection coefficient.
  *
  * Both channel readings must be available and free from suspected
  * saturation. A NotEvaluated or Saturation qualification on either
  * channel causes the function to reject the point.
  *
  * BelowWindow and AboveWindow readings are accepted. These qualifications
  * refer to the desired gain-control window and do not, by themselves,
  * indicate that the detector reading is unusable.
  *
  * Acquisition settling is assumed to have been enforced by the state
  * machine before the log entry was stored.
  *
  * @param entry     Pointer to the scan log entry to process.
  * @param amplitude Pointer receiving the combined amplitude, in volts RMS.
  *                  The output is valid only when the function returns true.
  *
  * @retval true  The combined amplitude is finite and strictly positive.
  * @retval false An argument is NULL, a channel reading is unavailable or
  *               suspected of saturation, or the calculated amplitude is
  *               invalid.
  *
  * @note A finite result does not establish that a signal is present.
  *       Signal detection is performed by the scan peak analysis.
  */
static bool ADL5961_GetScanPointAmplitude(const ADL5961_scanLogEntry_t *entry,
		float *amplitude) {
	float fwAmplitude;
	float rwAmplitude;

	if ((entry == NULL) || (amplitude == NULL)) {
		return false;
	}

	if ((entry->eFwQualification == eSignalQualification_NotEvaluated)
			|| (entry->eRwQualification == eSignalQualification_NotEvaluated)
			|| (entry->eFwQualification == eSignalQualification_Saturation)
			|| (entry->eRwQualification == eSignalQualification_Saturation)) {
		return false;
	}

	fwAmplitude = ADL5961_GetInputAmplitude(entry->fwSignalStrength_mV,
			entry->eFgain,
			ADL5961_FW_DETECTOR_SLOPE_MV_DB,
			ADL5961_FW_DETECTOR_INTERCEPT_DBV);

	rwAmplitude = ADL5961_GetInputAmplitude(entry->rwSignalStrength_mV,
			entry->eRgain,
			ADL5961_RW_DETECTOR_SLOPE_MV_DB,
			ADL5961_RW_DETECTOR_INTERCEPT_DBV);

	// *amplitude = hypotf(fwAmplitude, rwAmplitude);
	*amplitude = sqrtf(
	    fwAmplitude * fwAmplitude
	    + rwAmplitude * rwAmplitude);

	return isfinite(*amplitude) && (*amplitude > 0.0f);
}


/**
 * @brief Analyses one completed scan pass and stores its peak result.
 *
 * The function reads:
 *
 *     ADL5961_scan.log[pointIndex][windowIndex]
 *
 * and updates the corresponding summary:
 *
 *     ADL5961_scan.pass[windowIndex]
 *
 * Log entries must be stored in strictly increasing LO-frequency order,
 * regardless of the physical sweep direction. The acquisition state
 * machine must set the pass completed flag only after every expected
 * point has been acquired and stored.
 *
 * Analysis is performed as follows:
 *
 * 1. Convert every point into a gain-compensated combined amplitude.
 * 2. Locate the absolute maximum amplitude.
 * 3. Define a relative background limit as:
 *
 *        backgroundLimit = peakAmplitude / PEAK_MIN_RATIO
 *
 * 4. Require at least BACKGROUND_PERCENT percent of all points to be
 *    at or below that limit.
 * 5. Require the points above the limit to form exactly one contiguous
 *    region.
 * 6. Require the frequency span between the first and last points of
 *    that region not to exceed PEAK_MAX_WIDTH_HZ.
 * 7. Find the contiguous plateau surrounding the maximum and report
 *    the midpoint of its first and last LO frequencies.
 *
 * Plateau points must have amplitudes greater than or equal to:
 *
 *     peakAmplitude * (1 - PEAK_PLATEAU_TOLERANCE)
 *
 * A zero plateau tolerance accepts only amplitudes exactly equal to
 * the maximum. The reported peak amplitude is always the absolute
 * maximum, rather than the average plateau amplitude.
 *
 * No absolute signal-amplitude threshold is applied. Qualification
 * depends on the peak's contrast against the rest of the scan, its
 * uniqueness and its width. A single-point peak is allowed.
 *
 * On entry, previous analysis fields are cleared without modifying
 * the acquisition direction or completed flag.
 *
 * For an incomplete pass:
 *
 *     evaluated = false
 *     qualified = false
 *
 * For a completed pass that fails any analysis check:
 *
 *     evaluated = true
 *     qualified = false
 *
 * For an approved pass:
 *
 *     evaluated = true
 *     qualified = true
 *
 * The approved peak position and amplitude are stored in
 * peakLoFrequency_Hz and peakAmplitude, respectively. These fields
 * remain zero when the pass is not approved.
 *
 * @param windowIndex History slot containing the pass to analyse.
 *
 * @retval true  The pass contains a peak satisfying all configured criteria.
 * @retval false The history index is invalid, the pass is incomplete,
 *               configuration or point data is invalid, or the peak
 *               criteria are not satisfied.
 *
 * @note The reported position is an LO frequency. Conversion into an
 *       estimated RF frequency is outside the scope of this function.
 *
 * @note The midpoint may lie between two acquired frequencies.
 *
 * @note The function uses a static amplitude workspace and is not
 *       reentrant. Neither the scan configuration nor the selected
 *       history slot may be modified while analysis is running.
 *
 * @note This function validates only one pass. Repeatability across
 *       multiple passes is evaluated separately.
 */
static bool ADL5961_ValidateSingleScan( uint32_t windowIndex )
{
	/*
	 * Reusable workspace: no allocation and no large stack buffer.
	 * This function must not be called concurrently.
	 */
	static float amplitude[ADL5961_SCAN_MAX_POINTS];

	ADL5961_scanPass_t *pass;
	uint32_t pointCount;
	uint32_t i;

	uint32_t peakIndex = 0U;
	uint32_t backgroundCount = 0U;
	uint32_t regionCount = 0U;
	uint32_t regionFirst = 0U;
	uint32_t regionLast = 0U;
	uint32_t plateauFirst;
	uint32_t plateauLast;

	uint32_t firstFrequency;
	uint32_t lastFrequency;

	float peakAmplitude = 0.0f;
	float backgroundLimit;
	float plateauLimit;

	bool insideRegion = false;

	if (windowIndex >= SCAN_WINDOW_MAX_LEN) {
		return false;
	}

	pass = &ADL5961_scan.pass[windowIndex];

	/* Clear previous analysis without changing acquisition metadata. */
	pass->evaluated = false;
	pass->qualified = false;
	pass->peakLoFrequency_Hz = 0U;
	pass->peakAmplitude = 0.0f;

	if (!pass->completed) {
		return false;
	}

	pass->evaluated = true;

	pointCount = ADL5961_scan.pointCount;

	if ((pointCount == 0U) || (pointCount > ADL5961_SCAN_MAX_POINTS)) {
		return false;
	}

	/* Check configuration before using it. */
	if (!isfinite(ADL5961_SCAN_PEAK_MIN_RATIO)
			|| (ADL5961_SCAN_PEAK_MIN_RATIO <= 1.0f)
			|| (ADL5961_SCAN_BACKGROUND_PERCENT > 100UL)
			|| !isfinite(ADL5961_SCAN_PEAK_PLATEAU_TOLERANCE)
			|| (ADL5961_SCAN_PEAK_PLATEAU_TOLERANCE < 0.0f)
			|| (ADL5961_SCAN_PEAK_PLATEAU_TOLERANCE >= 1.0f)) {
		return false;
	}

	/* Convert all points and locate the absolute maximum. */
	for (i = 0U; i < pointCount; ++i) {
		const ADL5961_scanLogEntry_t *entry = &ADL5961_scan.log[i][windowIndex];

		/* Log indices must always follow increasing LO frequency. */
		if ((i > 0U)
				&& (entry->loFrequency_Hz
						<= ADL5961_scan.log[i - 1U][windowIndex].loFrequency_Hz)) {
			return false;
		}

		if (!ADL5961_GetScanPointAmplitude(entry, &amplitude[i])) {
			return false;
		}

		if (amplitude[i] > peakAmplitude) {
			peakAmplitude = amplitude[i];
			peakIndex = i;
		}
	}

	backgroundLimit = peakAmplitude / ADL5961_SCAN_PEAK_MIN_RATIO;

	/* Count background points and contiguous peak regions. */
	for (i = 0U; i < pointCount; ++i) {
		if (amplitude[i] <= backgroundLimit) {
			++backgroundCount;
			insideRegion = false;
		} else {
			if (!insideRegion) {
				++regionCount;
				regionFirst = i;
				insideRegion = true;
			}

			regionLast = i;
		}
	}

	/* Require the configured background percentage. */
	if (((uint64_t) backgroundCount * 100ULL)
			< ((uint64_t) pointCount * ADL5961_SCAN_BACKGROUND_PERCENT)) {
		return false;
	}

	/* Require exactly one region above the relative background limit. */
	if (regionCount != 1U) {
		return false;
	}

	firstFrequency = ADL5961_scan.log[regionFirst][windowIndex].loFrequency_Hz;

	lastFrequency = ADL5961_scan.log[regionLast][windowIndex].loFrequency_Hz;

	if ((lastFrequency - firstFrequency) >
	ADL5961_SCAN_PEAK_MAX_WIDTH_HZ) {
		return false;
	}

	/* Find the contiguous plateau surrounding the maximum. */
	plateauLimit = peakAmplitude * (1.0f - ADL5961_SCAN_PEAK_PLATEAU_TOLERANCE);

	plateauFirst = peakIndex;
	plateauLast = peakIndex;

	while ((plateauFirst > regionFirst)
			&& (amplitude[plateauFirst - 1U] >= plateauLimit)) {
		--plateauFirst;
	}

	while ((plateauLast < regionLast)
			&& (amplitude[plateauLast + 1U] >= plateauLimit)) {
		++plateauLast;
	}

	firstFrequency = ADL5961_scan.log[plateauFirst][windowIndex].loFrequency_Hz;

	lastFrequency = ADL5961_scan.log[plateauLast][windowIndex].loFrequency_Hz;

	/* Store plateau midpoint without overflowing the frequency sum. */
	pass->peakLoFrequency_Hz = firstFrequency
			+ ((lastFrequency - firstFrequency) / 2U);

	pass->peakAmplitude = peakAmplitude;
	pass->qualified = true;

	return true;
}


#define ADL5961_STARTUP_SUPPLY_WAIT_US       1000U
#define ADL5961_STARTUP_RESET_POLL_US          10U
#define ADL5961_STARTUP_RESET_MAX_POLLS       100U
#define ADL5961_STARTUP_ERROR_CAPACITY         24U
#define ADL5961_STARTUP_CHIPTYPE_EXPECTED       1U
#define ADL5961_STARTUP_FULL_MASK       UINT8_MAX

typedef enum eADL5961_StartupRegister_t
{
    eADL5961_StartupRegister_SPI_CONFIG_A,
    eADL5961_StartupRegister_SPI_CONFIG_B,
    eADL5961_StartupRegister_DEVICE_CONFIG,
    eADL5961_StartupRegister_CHIPTYPE,
    eADL5961_StartupRegister_LO_CONFIG,
    eADL5961_StartupRegister_CT2,
    eADL5961_StartupRegister_CT4,
    eADL5961_StartupRegister_FGAIN,
    eADL5961_StartupRegister_RGAIN,
    eADL5961_StartupRegister_CIF2_CIF1,
    eADL5961_StartupRegister_TDEG,
    eADL5961_StartupRegister_Count
} eADL5961_StartupRegister_t;

/* Structure offsets are the real SPI addresses for this register image. */
#define ADL5961_STARTUP_REGISTER(member) \
    [eADL5961_StartupRegister_##member] = \
        (uint8_t)offsetof(ADL5961_regs_t, member)

static const uint8_t ADL5961_startupAddresses[] =
{
    ADL5961_STARTUP_REGISTER(SPI_CONFIG_A),
    ADL5961_STARTUP_REGISTER(SPI_CONFIG_B),
    ADL5961_STARTUP_REGISTER(DEVICE_CONFIG),
    ADL5961_STARTUP_REGISTER(CHIPTYPE),
    ADL5961_STARTUP_REGISTER(LO_CONFIG),
    ADL5961_STARTUP_REGISTER(CT2),
    ADL5961_STARTUP_REGISTER(CT4),
    ADL5961_STARTUP_REGISTER(FGAIN),
    ADL5961_STARTUP_REGISTER(RGAIN),
    ADL5961_STARTUP_REGISTER(CIF2_CIF1),
    ADL5961_STARTUP_REGISTER(TDEG)
};

typedef enum eADL5961_StartupStep_t
{
    eADL5961_Startup_Parameters,
    eADL5961_Startup_SpiConfig,
    eADL5961_Startup_Reset,
    eADL5961_Startup_Identification,
    eADL5961_Startup_ResetRegisters,
    eADL5961_Startup_WriteReadTest,
    eADL5961_Startup_TestRestore,
    eADL5961_Startup_FinalPowerState,
    eADL5961_Startup_StepCount
} eADL5961_StartupStep_t;

typedef enum eADL5961_StartupCheck_t
{
    eADL5961_StartupCheck_NotExecuted,
    eADL5961_StartupCheck_Running,
    eADL5961_StartupCheck_Passed,
    eADL5961_StartupCheck_Failed
} eADL5961_StartupCheck_t;

typedef struct ADL5961_startupError_t
{
    eADL5961_StartupStep_t step;
    status_t status;
    eADL5961_StartupRegister_t registerIndex; /* Count: not applicable. */
    uint8_t expected;
    uint8_t actual;
    uint8_t compareMask; /* Zero: transport/parameter error, not comparison. */
} ADL5961_startupError_t;

typedef struct ADL5961_startupLog_t
{
    bool completed; /* Attempt ended; inspect successful for the outcome. */
    bool successful;
    bool powerDownRequested;
    bool finalPowerStateVerified; /* Register readback, not analog measurement. */
    bool shutdownAsserted;
    eADL5961_StartupStep_t step;
    eADL5961_StartupStep_t failedStep; /* StepCount when no error occurred. */
    status_t status; /* First error, or kStatus_Success. */
    eADL5961_StartupCheck_t checks[eADL5961_Startup_StepCount];
    uint32_t errorCount;
    uint8_t storedErrorCount;
    bool errorOverflow;
    ADL5961_startupError_t errors[ADL5961_STARTUP_ERROR_CAPACITY];
    uint32_t resetPollCount;
    ADL5961_regs_t resetImage;
    bool resetRegisterValid[eADL5961_StartupRegister_Count];
    eADL5961_StartupCheck_t resetRegisterChecks[eADL5961_StartupRegister_Count];
    uint8_t chipType;
} ADL5961_startupLog_t;

static ADL5961_startupLog_t ADL5961_startupLog;


const ADL5961_startupLog_t *ADL5961_GetStartupLog(void)
{
    return &ADL5961_startupLog;
}

static void ADL5961_StartupBegin(eADL5961_StartupStep_t step)
{
    ADL5961_startupLog.step = step;
    ADL5961_startupLog.checks[step] = eADL5961_StartupCheck_Running;
}

static void ADL5961_StartupError(status_t status,
    eADL5961_StartupRegister_t reg, uint8_t expected, uint8_t actual, uint8_t mask)
{
    ADL5961_startupLog_t *log = &ADL5961_startupLog;
    ADL5961_startupError_t error = {log->step, status, reg, expected, actual, mask};
    if (log->errorCount == 0U)
    {
        log->status = status;
        log->failedStep = log->step;
    }
    ++log->errorCount;
    log->checks[log->step] = eADL5961_StartupCheck_Failed;
    if (log->storedErrorCount < ADL5961_STARTUP_ERROR_CAPACITY)
        log->errors[log->storedErrorCount++] = error;
    else
        log->errorOverflow = true;
}

static void ADL5961_StartupEndCheck(void)
{
    if (ADL5961_startupLog.checks[ADL5961_startupLog.step] ==
        eADL5961_StartupCheck_Running)
        ADL5961_startupLog.checks[ADL5961_startupLog.step] =
            eADL5961_StartupCheck_Passed;
}

static status_t ADL5961_StartupAccess(bool write,
    eADL5961_StartupRegister_t reg, uint8_t *data)
{
    const uint8_t address = ADL5961_startupAddresses[reg];
    status_t status = write ? ADL5961_WriteRegisterBlocking(address, *data)
                           : ADL5961_ReadRegisterBlocking(address, data);
    if (status != kStatus_Success)
        ADL5961_StartupError(status, reg, 0U, 0U, 0U);
    return status;
}

static bool ADL5961_StartupCompare(eADL5961_StartupRegister_t reg,
                                  uint8_t actual, uint8_t expected, uint8_t mask)
{
    if (((actual ^ expected) & mask) == 0U) return true;
    ADL5961_StartupError(kStatus_Fail, reg, expected, actual, mask);
    return false;
}

static ADL5961_regs_t ADL5961_MakeStartupDefaults(void)
{
    ADL5961_regs_t regs = {0};
    regs.CHIPTYPE = ADL5961_STARTUP_CHIPTYPE_EXPECTED;
    regs.LO_CONFIG.bits.eBypass = eBypass_Enabled;
    regs.LO_CONFIG.bits.eLoMode = eLoMode_MultiplyBy1;
    regs.LO_CONFIG.bits.eOfMode = eOfMode_DivideBy4;
    /* All other documented configuration reset bytes are zero. */
    return regs;
}

/*
 * Software reset, reset-default checks, writable-register test and optional
 * shutdown. Does not initialize the MCU SPI or change application LO/gains.
 * CHIPTYPE is a chip-type check, not a unique product-ID verification.
 * Failure stops normal startup. CT4 restoration is attempted if its test
 * has started, even after a transport failure; restoration has its own log.
 * Final power state is applied only after all prior checks succeed.
 * No I/O_UPDATE is required by ADL5961 register writes.
 * EN must remain asserted for powerDownAtEnd=false to mean analog operation.
 */
status_t ADL5961_ColdStartBlocking(uint32_t coreClock_Hz, bool powerDownAtEnd )
{
    ADL5961_startupLog_t *log = &ADL5961_startupLog;
    ADL5961_regs_t defaults = ADL5961_MakeStartupDefaults();
    ADL5961_SPI_CONFIG_A_t configA = {.b8 = 0U};
    ADL5961_SPI_CONFIG_B_t configB = {.b8 = 0U};
    ADL5961_DEVICE_CONFIG_t device = {.b8 = 0U};
    ADL5961_TDEG_t temperatureMask = {.b8 = ADL5961_STARTUP_FULL_MASK};
    const eCt4_t testSettings[] = {eCt4_Setting5, eCt4_Setting10};
    uint8_t readback;
    bool resetComplete = false;

    /* Preserve the preceding log and do not reset during an active transfer. */
    if (ADL5961_transferContext.pending) return kStatus_DSPI_Busy;
    memset(log, 0, sizeof(*log));
    log->failedStep = eADL5961_Startup_StepCount;
    log->powerDownRequested = powerDownAtEnd;
    ADL5961_StartupBegin(eADL5961_Startup_Parameters);
    if (coreClock_Hz == 0U)
    {
        ADL5961_StartupError(kStatus_InvalidArgument,
            eADL5961_StartupRegister_Count, 0U, 0U, 0U);
        goto finish;
    }
    ADL5961_StartupEndCheck();
    SDK_DelayAtLeastUs(ADL5961_STARTUP_SUPPLY_WAIT_US, coreClock_Hz);

    ADL5961_StartupBegin(eADL5961_Startup_SpiConfig);
    /* Palindromic config: MSB first, address decrement, reset inactive. */
    configA.bits.eBitOrder = configA.bits.eBitOrderMirror = eSpiBitOrder_MsbFirst;
    configA.bits.eAddressDirection = configA.bits.eAddressDirectionMirror =
        eSpiAddressDirection_Decrement;
    configA.bits.eResetAssert = configA.bits.eResetAssertMirror = eReset_DeAsserted;
    if (ADL5961_StartupAccess(true, eADL5961_StartupRegister_SPI_CONFIG_A,
        &configA.b8) != kStatus_Success) goto finish;
    /* Keep actual reset values while examining the defaults. */
    configB.bits.eInstructionMode = eInstructionMode_Streaming;
    configB.bits.eCsbStall = eCsbStall_Disabled;
    configB.bits.eControllerTargetRb = eControllerTargetRb_TargetRegisters;
    if (ADL5961_StartupAccess(true, eADL5961_StartupRegister_SPI_CONFIG_B,
        &configB.b8) != kStatus_Success) goto finish;
    if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_SPI_CONFIG_A,
        &readback) != kStatus_Success) goto finish;
    ADL5961_StartupCompare(eADL5961_StartupRegister_SPI_CONFIG_A,
        readback, configA.b8, ADL5961_STARTUP_FULL_MASK);
    if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_SPI_CONFIG_B,
        &readback) != kStatus_Success) goto finish;
    ADL5961_StartupCompare(eADL5961_StartupRegister_SPI_CONFIG_B,
        readback, configB.b8, ADL5961_STARTUP_FULL_MASK);
    ADL5961_StartupEndCheck();
    if (log->errorCount != 0U) goto finish;

    ADL5961_StartupBegin(eADL5961_Startup_Reset);
    configA.bits.eResetAssert = configA.bits.eResetAssertMirror = eReset_Asserted;
    if (ADL5961_StartupAccess(true, eADL5961_StartupRegister_SPI_CONFIG_A,
        &configA.b8) != kStatus_Success) goto finish;
    for (uint32_t i = 0U; i < ADL5961_STARTUP_RESET_MAX_POLLS; ++i)
    {
        ADL5961_SPI_CONFIG_A_t observed;
        SDK_DelayAtLeastUs(ADL5961_STARTUP_RESET_POLL_US, coreClock_Hz);
        ++log->resetPollCount;
        if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_SPI_CONFIG_A,
            &observed.b8) != kStatus_Success) goto finish;
        readback = observed.b8;
        if (observed.bits.eResetAssert == eReset_DeAsserted &&
            observed.bits.eResetAssertMirror == eReset_DeAsserted)
        {
            resetComplete = true;
            break;
        }
    }
    if (!resetComplete)
    {
        ADL5961_StartupError(kStatus_Timeout, eADL5961_StartupRegister_SPI_CONFIG_A,
            0U, readback, configA.b8);
        goto finish;
    }
    ADL5961_StartupEndCheck();

    ADL5961_StartupBegin(eADL5961_Startup_Identification);
    if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_CHIPTYPE,
        &log->chipType) != kStatus_Success) goto finish;
    ADL5961_StartupCompare(eADL5961_StartupRegister_CHIPTYPE,
        log->chipType, defaults.CHIPTYPE, ADL5961_STARTUP_FULL_MASK);
    ADL5961_StartupEndCheck();

    ADL5961_StartupBegin(eADL5961_Startup_ResetRegisters);
    temperatureMask.bits.tDeg = 0U; /* Temperature is live, not a reset signature. */
    for (unsigned int i = 0U; i < eADL5961_StartupRegister_Count; ++i)
    {
        const eADL5961_StartupRegister_t reg = (eADL5961_StartupRegister_t)i;
        const uint8_t address = ADL5961_startupAddresses[i];
        uint8_t *destination = (uint8_t *)&log->resetImage + address;
        const uint8_t expected = *((const uint8_t *)&defaults + address);
        const uint8_t mask = (reg == eADL5961_StartupRegister_TDEG)
            ? temperatureMask.b8 : ADL5961_STARTUP_FULL_MASK;
        if (ADL5961_StartupAccess(false, reg, destination) != kStatus_Success)
        {
            log->resetRegisterChecks[i] = eADL5961_StartupCheck_Failed;
            goto finish;
        }
        log->resetRegisterValid[i] = true;
        log->resetRegisterChecks[i] = ADL5961_StartupCompare(reg, *destination,
            expected, mask) ? eADL5961_StartupCheck_Passed : eADL5961_StartupCheck_Failed;
    }
    ADL5961_StartupEndCheck();
    if (log->errorCount != 0U) goto finish;

    ADL5961_StartupBegin(eADL5961_Startup_WriteReadTest);
    for (size_t i = 0U; i < sizeof(testSettings) / sizeof(testSettings[0]); ++i)
    {
        ADL5961_CT4_t pattern = {.b8 = 0U};
        pattern.bits.eCt4 = testSettings[i];
        if (ADL5961_StartupAccess(true, eADL5961_StartupRegister_CT4,
            &pattern.b8) != kStatus_Success) break;
        if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_CT4,
            &readback) != kStatus_Success) break;
        ADL5961_StartupCompare(eADL5961_StartupRegister_CT4,
            readback, pattern.b8, ADL5961_STARTUP_FULL_MASK);
    }
    ADL5961_StartupEndCheck();

    /* Always attempt to restore CT4 after its test has started. */
    ADL5961_StartupBegin(eADL5961_Startup_TestRestore);
    if (ADL5961_StartupAccess(true, eADL5961_StartupRegister_CT4,
        &defaults.CT4.b8) != kStatus_Success) goto finish;
    if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_CT4,
        &readback) != kStatus_Success) goto finish;
    ADL5961_StartupCompare(eADL5961_StartupRegister_CT4,
        readback, defaults.CT4.b8, ADL5961_STARTUP_FULL_MASK);
    ADL5961_StartupEndCheck();
    if (log->errorCount != 0U) goto finish;

    ADL5961_StartupBegin(eADL5961_Startup_FinalPowerState);
    device.bits.eShutdown = powerDownAtEnd
        ? eShutdown_Asserted : eShutdown_NormalOperation;
    if (ADL5961_StartupAccess(true, eADL5961_StartupRegister_DEVICE_CONFIG,
        &device.b8) != kStatus_Success) goto finish;
    if (ADL5961_StartupAccess(false, eADL5961_StartupRegister_DEVICE_CONFIG,
        &readback) != kStatus_Success) goto finish;
    if (ADL5961_StartupCompare(eADL5961_StartupRegister_DEVICE_CONFIG,
        readback, device.b8, ADL5961_STARTUP_FULL_MASK))
    {
        log->finalPowerStateVerified = true;
        log->shutdownAsserted = powerDownAtEnd;
    }
    ADL5961_StartupEndCheck();

finish:
    log->completed = true;
    log->successful = (log->errorCount == 0U);
    return log->status;
}
