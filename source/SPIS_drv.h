/*
 * SPIS_drv.h
 *
 *  Created on: 6. okt. 2026
 *      Author: pc
 */

#ifndef SPIS_DRV_H_
#define SPIS_DRV_H_


#include "fsl_dspi.h"

typedef void (*SPI_completionCallback_t)(
    status_t status,
    void *userData);

typedef struct SPI_busConfig_t
{
    uint32_t baudrate_Hz;
    uint32_t bitsPerFrame;

    dspi_clock_polarity_t cpol;
    dspi_clock_phase_t cpha;
    dspi_shift_direction_t direction;

    uint32_t pcsToSckDelay_ns;
    uint32_t lastSckToPcsDelay_ns;
    uint32_t betweenTransferDelay_ns;

} SPI_busConfig_t;

/**
 * @brief Describes a device-independent SPI transfer.
 *
 * Buffers contain bytes in their final wire order.
 * Device protocol encoding and decoding are handled
 * by the calling driver.
 */
typedef struct SPI_transaction_t
{
    SPI_Type *base;

    const SPI_busConfig_t *config;

    const uint8_t *txData;
    uint8_t *rxData;

    size_t dataSize;

    /*
     * SDK transfer flags selecting CTAR, PCS,
     * and chip-select continuity.
     */
    uint32_t configFlags;
    /* Used only by asynchronous transfers. */
    SPI_completionCallback_t callback;
    void *userData;

} SPI_transaction_t;

void SPIs_InitPins(void);
status_t SPIs_Init(SPI_Type *p_SPIBase);
bool SPIs_TryAcquire(SPI_Type *base);
void SPIs_Release(SPI_Type *base);
status_t SPI_Bus_Configure(SPI_Type *base, const SPI_busConfig_t *config);
status_t SPIs_TransferBlocking(const SPI_transaction_t *transaction);
status_t SPIs_TransferNonBlocking(const SPI_transaction_t *transaction);

#endif /* SPIS_DRV_H_ */
