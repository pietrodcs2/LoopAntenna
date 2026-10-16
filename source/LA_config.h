/*
 * ADL5961_config.h
 *
 *  Created on: 2. okt. 2026
 *      Author: pc
 */

#ifndef LA_CONFIG_H_
#define LA_CONFIG_H_

#include "fsl_dspi.h"

/* Maximum number of retained scan passes */
#define SCAN_WINDOW_MAX_LEN          5UL

/* Minimum number of agreeing passes required for validation */
#define ADL5961_SCAN_MIN_STABLE_PASSES 3UL

/* Desired IF frequency */
#define ADL5961_IF_HZ                  1000000UL

/* LO scan range: RF range |–3| MHz with high-side injection */
#define ADL5961_LO_FMIN_HZ              1000000UL
#define ADL5961_LO_FMAX_HZ             31000000UL

/* Initial settling time after a frequency or gain change */
#define ADL5961_SETUP_TIME_MS                  5UL

/* Initial acceptable signal-strength voltage window */
#define ADL5961_SIG_MIN_MV                   600U
#define ADL5961_SIG_MAX_MV                  1000U

#define ADL5961_SPI_BAUDRATE_HZ				1000000UL
#define ADL5961_SPI_DELAY_NS       			1000UL

#define ADL5961_SPI SPI0

/* Adjust CTAR and PCS to the actual hardware configuration */
#define ADL5961_SPI_TRANSFER_FLAGS \
    (kDSPI_MasterCtar0 |           \
     kDSPI_MasterPcs0  |           \
     kDSPI_MasterPcsContinuous)



// IL DDS

#define AD9959_XTAL_FRQ_HZ				25000000UL
#define AD9959_SYSCLK_HZ				500000000UL
#define AD9959_SPI                    	SPI0


/* Example assignments: adapt to the selected CS and CTAR. */
#define AD9959_SPI_CTAR                kDSPI_Ctar0
#define AD9959_SPI_PCS                 kDSPI_Pcs1

#define AD9959_SPI_BAUDRATE_HZ         1000000UL
#define AD9959_SPI_DELAY_NS            1000UL

#define AD9959_SPI_TRANSFER_FLAGS \
    (kDSPI_MasterCtar0         \
     | kDSPI_MasterPcs1       \
     | kDSPI_MasterPcsContinuous)

/* Handle created by the shared SPI bus initialization */
#define ADL5961_SPI_HANDLE   g_spi0Handle
#define AD9959_SPI_HANDLE    g_spi0Handle



/* DRV8912-Q1: two devices on SPI0, independent active-low chip selects.
 * Existing pin mapping: PCS2 = PTC2; PCS3 = PTC1.
 * Keep CTAR0: the shared bus configures it for each device transaction.
 * Enable pins must be held high externally when no GPIO hook is supplied.
 */
#define DRV8912_SPI                    SPI0
#define DRV8912_SPI_BAUDRATE_HZ         1000000UL
#define DRV8912_SPI_DELAY_NS            1000UL

#define DRV8912_0_SPI_TRANSFER_FLAGS \
    (kDSPI_MasterCtar0 | kDSPI_MasterPcs2 | kDSPI_MasterPcsContinuous)
#define DRV8912_1_SPI_TRANSFER_FLAGS \
    (kDSPI_MasterCtar0 | kDSPI_MasterPcs3 | kDSPI_MasterPcsContinuous)

#endif /* LA_CONFIG_H_ */
