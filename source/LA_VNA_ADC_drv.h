#ifndef LA_VNA_ADC_DRV_H_
#define LA_VNA_ADC_DRV_H_

#include "fsl_common.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct VNA_ADC_scan_t
{
    uint16_t forwardLevel;
    uint16_t reverseLevel;
    uint16_t gainRatio;
    uint16_t phase;
} VNA_ADC_scan_t;

#define VNA_ADC_WINDOW_SAMPLES 128U
#define VNA_ADC_REFERENCE_MV  3300U


typedef struct VNA_ADC_window_t
{
    VNA_ADC_scan_t samples[VNA_ADC_WINDOW_SAMPLES];
    uint32_t samplePeriod_us; /* Nominal requested period. */
    uint32_t timerPeriod_ticks; /* Exact configured period: ticks / timerClock_Hz. */
    uint32_t timerClock_Hz;
    uint16_t requestedSamples;
    uint16_t validSamples; /* Only complete, committed four-input scans. */
} VNA_ADC_window_t;

/* Reserve this application status group for the VNA ADC driver. */
enum
{
    kStatus_VNA_ADC_Aborted = MAKE_STATUS(kStatusGroup_ApplicationRangeStart, 0),
    kStatus_VNA_ADC_Overrun = MAKE_STATUS(kStatusGroup_ApplicationRangeStart, 1),
    kStatus_VNA_ADC_DmaError = MAKE_STATUS(kStatusGroup_ApplicationRangeStart, 2),
    kStatus_VNA_ADC_ClockUnknown = MAKE_STATUS(kStatusGroup_ApplicationRangeStart, 3)
};


typedef struct VNA_ADC_moduleTiming_t
{
    uint32_t cfg1, cfg2, sc1, sc2, sc3; /* Read-only hardware snapshot. */
    uint32_t sourceClock_Hz;
    uint32_t busClock_Hz;
    uint32_t adcClock_Hz; /* Integer display value: source / divider. */
    uint16_t conversionAdckCycles;
    uint8_t conversionBusCycles;
    uint8_t clockDivider;
    uint8_t resolutionBits;
    uint8_t averageCount;
    bool differential;
    bool continuous;
    bool hardwareTrigger;
    uint64_t conversion_ns; /* Maximum single/first conversion, rounded up. */
} VNA_ADC_moduleTiming_t;

typedef struct VNA_ADC_timing_t
{
    VNA_ADC_moduleTiming_t adc[2]; /* ADC0, ADC1. */
    uint64_t hardwareScan_ns; /* max(2*tADC0, 2*tADC1); excludes DMA/IRQ gaps. */
} VNA_ADC_timing_t;

/*
 * Read initialized hardware without changing configuration or reading Rn.
 * K64 RM 35.4.4.5: SFC + AverageNum*(BCT + LST + HSC).
 * Sampling is already included. Timing is for single/first conversions;
 * continuous/trigger flags are reported, but inter-trigger waits are excluded.
 * Clock frequencies come from CLOCK_GetFreq(), not a frequency measurement.
 * ADACK cannot be inferred accurately from configuration registers: return
 * kStatus_VNA_ADC_ClockUnknown. Output remains unchanged on any failure.
 * Call from main with system clocks and ADC configuration stable.
 */
status_t VNA_ADC_GetTiming(VNA_ADC_timing_t *timing);

/* Initialize once at boot, after board clocks are configured. */
void VNA_ADC_InitPins(void);
status_t VNA_ADC_Init(void);

/* Start one asynchronous four-input scan. */
status_t VNA_ADC_StartScan(void);

/*
 * Return false while no result is available; outputs remain unchanged.
 * Return true once, with the final status and a complete snapshot on success.
 * Both pointers must be non-NULL. Failed scans leave *scan unchanged.
 * Call from the same processing context as StartScan().
 */
bool VNA_ADC_GetScanResult(VNA_ADC_scan_t *scan, status_t *status);

/*
 * Start a timed window; first scan starts immediately, then PIT schedules scans.
 * Single scans and windows share one context and cannot overlap.
 * sampleCount must be 1 through VNA_ADC_WINDOW_SAMPLES; period must be nonzero.
 */
status_t VNA_ADC_StartWindow(uint32_t samplePeriod_us, uint16_t sampleCount);

/*
 * Return true once with the final status and the internally owned window.
 * Both pointers must be non-NULL. Partial data remain available on abort/error.
 * Collect the result before restarting. The window remains stable until the
 * next successful StartWindow() call; copy it if it must be retained longer.
 */
bool VNA_ADC_GetWindowResult(const VNA_ADC_window_t **window, status_t *status);

/*
 * Stop the active single scan or window. Success means acquisition was stopped;
 * the result collector reports kStatus_VNA_ADC_Aborted. Complete committed
 * scans are retained. Return kStatus_NoTransferInProgress when already idle/ready.
 * Call Start/Get/Abort from main only; the PIT/DMA IRQs are driver-owned.
 */
status_t VNA_ADC_AbortScan(void);

uint32_t VNA_ADC_RawToMillivolts(uint16_t raw, uint32_t reference_mV );


#endif
