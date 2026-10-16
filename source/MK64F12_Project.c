/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    MK64F12_Project.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "clocks.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK64F12.h"
#include "fsl_debug_console.h"
#include "SPIS_drv.h"
#include "LA_VNA_ADC_drv.h"
#include "LA_PIT_drv.h"
#include "LA_SYSTIMER_drv.h"
#include "LA_IS25LP064A_drv.h"


#include "LA_AD9959_drv.h"
#include "LA_ADL5961_drv.h"
#include "LA_DRV8912_drv.h"

/* Persistent contexts and startup results, visible in the debugger.
 * Default configurations have no enable GPIO hook: enable is external.
 */
DRV8912_device_t DRV8912_devices[DRV8912_DEVICE_COUNT];
status_t DRV8912_InitContext_stat[DRV8912_DEVICE_COUNT];
status_t DRV8912_Start_stat[DRV8912_DEVICE_COUNT];

/* Set one request flag from the debugger; main consumes it once.
 * Device index is 0 or 1. Mask bit 0 selects OUT1, bit 11 selects OUT12.
 * Applying a relay mask drives selected outputs HIGH and all others LOW.
 * These tests produce static levels, not timed pulses.
 */
volatile uint8_t drvDeviceIndex = 0U;
volatile uint16_t drvRelayMask = 0U;
volatile bool testDrvStart = false;
volatile bool testDrvApplyMask = false;
volatile bool testDrvHighImpedance = false;
volatile bool testDrvReadFaults = false;
volatile status_t drvTestStatus = kStatus_Success;
DRV8912_faults_t drvFaults[DRV8912_DEVICE_COUNT];

status_t ADL5961_ColdStart_stat;
status_t AD9959_ColdStart_stat, SPIs_Init_stat;
status_t LA_PIT_Init_stat;
status_t VNA_ADC_Init_stat;
volatile status_t adcTimingStatus;
VNA_ADC_timing_t adcTiming;
static volatile bool systimerReady = false;


#define FLASH_TEST_ADDRESS \
    (IS25LP064A_CAPACITY_BYTES - IS25LP064A_SECTOR_SIZE_BYTES)

static IS25LP064A_device_t flashTestDevice;

static uint8_t flashTestWriteBuffer[IS25LP064A_SECTOR_SIZE_BYTES];
static uint8_t flashTestReadBuffer[IS25LP064A_SECTOR_SIZE_BYTES];

/* Inspect these variables in the debugger. */
static volatile IS25LP064A_status_t flashTestStatus =
    eIS25LP064A_NotInitialized;

static volatile bool flashTestPassed = false;


/* TODO: insert other include files here. */

/* TODO: insert other definitions and declarations here. */

/*
 * @brief   Application entry point.
 */
int main(void) {

    /* Init board hardware. */
    // BOARD_InitBootPins();
    BOARD_InitBootClocks();
    //BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    // BOARD_InitDebugConsole();
#endif
    clocks_dump();
    SPIs_InitPins();
    VNA_ADC_InitPins();
    SPIs_Init_stat = SPIs_Init( SPI0 );
    uint32_t coreClock_Hz = CLOCK_GetFreq(kCLOCK_CoreSysClk);
    LA_PIT_Init_stat = LA_PIT_Init(false);
    VNA_ADC_Init_stat = (LA_PIT_Init_stat == kStatus_Success) ?
        VNA_ADC_Init() : LA_PIT_Init_stat;
    adcTimingStatus = VNA_ADC_GetTiming(&adcTiming);


    if (SPIs_Init_stat == kStatus_Success)
    {
        ADL5961_ColdStart_stat =
            ADL5961_ColdStartBlocking(coreClock_Hz, true);

        AD9959_ColdStart_stat =
            AD9959_ColdStartBlocking(coreClock_Hz, true);
    }
    else
    {
        /* Device cold starts were not executed. */
        ADL5961_ColdStart_stat = SPIs_Init_stat;
        AD9959_ColdStart_stat = SPIs_Init_stat;
    }
    /* Configure both relay drivers, leaving every output in Hi-Z.
     * extOvp=true selects the range suitable for a nominal 24 V supply.
     * No relay masks or output test pulses are applied at startup.
     */
    for ( uint32_t i = 0U; i < DRV8912_DEVICE_COUNT; ++i )
    {
        DRV8912_InitContext_stat[ i ] =
            DRV8912_InitContext( &DRV8912_devices[ i ], &DRV8912_DefaultConfigs[ i ] );
        DRV8912_Start_stat[ i ] =
            ( DRV8912_InitContext_stat[ i ] != kStatus_Success ) ?
                DRV8912_InitContext_stat[ i ] :
            (SPIs_Init_stat != kStatus_Success) ? SPIs_Init_stat :
                DRV8912_StartBlocking( &DRV8912_devices[ i ], coreClock_Hz, true );
    }
    systimerReady =
        LA_SYSTIMER_Init() && LA_SYSTIMER_Start();
    do
    {
        IS25LP064A_config_t config;

        if (SPIs_Init_stat != kStatus_Success)
        {
        	flashTestStatus = eIS25LP064A_SPIError;
            break;
        }

        if (!systimerReady)
        {
        	flashTestStatus = eIS25LP064A_NotInitialized;
            break;
        }
        /* Configure the FLASH context and its timeout timebase. */
        IS25LP064A_GetDefaultConfig(&config);
        config.getTimeMs = LA_SYSTIMER_GetTimeMs;

        flashTestStatus =
            IS25LP064A_InitContext(&flashTestDevice, &config);

        if (flashTestStatus != eIS25LP064A_Success)
            break;

        /* Wake the FLASH, check its ID and acquire startup diagnostics. */
        flashTestStatus =
            IS25LP064A_StartBlocking(&flashTestDevice);

        if (flashTestStatus != eIS25LP064A_Success)
            break;

        /* Prepare a deterministic pattern for the complete sector. */
        for (uint32_t i = 0U; i < sizeof(flashTestWriteBuffer); ++i)
        {
            flashTestWriteBuffer[i] =
                (uint8_t)((i * 37U + i / 256U) & 0xFFU);
        }

        memset(flashTestReadBuffer, 0, sizeof(flashTestReadBuffer));

        /* Erase the complete 4 KiB sector.
         * This function also verifies that every byte is 0xFF.
         */
        flashTestStatus = IS25LP064A_EraseSectorBlocking(
            &flashTestDevice,
            FLASH_TEST_ADDRESS);

        if (flashTestStatus != eIS25LP064A_Success)
            break;

        /* Program all 4096 bytes.
         * The driver splits the transfer into 16 pages of 256 bytes,
         * waits for completion and verifies each page.
         */
        flashTestStatus = IS25LP064A_ProgramBlocking(
            &flashTestDevice,
            FLASH_TEST_ADDRESS,
            flashTestWriteBuffer,
            (uint32_t)sizeof(flashTestWriteBuffer));

        if (flashTestStatus != eIS25LP064A_Success)
            break;

        /* Read the complete sector into a separate buffer. */
        flashTestStatus = IS25LP064A_ReadBlocking(
            &flashTestDevice,
            FLASH_TEST_ADDRESS,
            flashTestReadBuffer,
            (uint32_t)sizeof(flashTestReadBuffer));

        if (flashTestStatus != eIS25LP064A_Success)
            break;

        /* Independently compare the written and read buffers. */
        if (memcmp(flashTestWriteBuffer,
                   flashTestReadBuffer,
                   sizeof(flashTestWriteBuffer)) != 0)
        {
            flashTestStatus = eIS25LP064A_VerificationFailed;
            break;
        }

        flashTestPassed = true;

        /**
         *
         * eXPECTED
         * flashTestPassed == true
		 * flashTestStatus == eIS25LP064A_Success
		 * flashTestDevice.startupLog.jedecId == 0x009D6017
		 *
         */

    } while (0);
    /* Add these declarations to the GLOBAL data section, outside main(). */
    volatile bool testAdcScan = false;
    volatile status_t adcStartStatus;
    volatile status_t adcScanStatus;
    volatile VNA_ADC_scan_t adcValues;
    static bool adcScanPending = false;

    volatile bool testAdcWindow = false;
    volatile bool testAdcAbort = false;
    volatile uint32_t adcSamplePeriod_us = 100U; /* Nominal 10 kHz per input. */
    volatile uint16_t adcSampleCount = VNA_ADC_WINDOW_SAMPLES;
    volatile status_t adcWindowStartStatus;
    volatile status_t adcWindowStatus;
    volatile status_t adcAbortStatus;
    const VNA_ADC_window_t * volatile adcWindow = NULL;
    static bool adcWindowPending = false;

    static volatile bool testAdlWakeUp = false;
    static volatile bool testAdlShutdown = false;
    static volatile bool testAdlForwardGain = false;
    static volatile bool testAdlReverseGain = false;

    static volatile eGain_t forwardGain = eGain_0dB;
    static volatile eGain_t reverseGain = eGain_0dB;

    static volatile status_t adlTestStatus;
    static volatile status_t adlTransferStatus;

    static bool adlTransferPending = false;
    static volatile bool test1 = false;
    static volatile bool test2 = false;
    static volatile bool test3 = false;
    static volatile bool test4 = false;

    static volatile status_t testStatus;
    static volatile status_t transferStatus;
    static bool frequencyUpdatePending = false;

    while (1)
    {
    	if (adcWindowPending)
    	{
    	    const VNA_ADC_window_t *window;
    	    status_t result;
    	    if (VNA_ADC_GetWindowResult(&window, &result))
    	    {
    	        /* On success, abort or overrun, inspect only window->validSamples. */
    	        adcWindow = window;
    	        adcWindowStatus = result;
    	        adcWindowPending = false;
    	    }
    	}

    	if (adcScanPending)
    	{
    	    VNA_ADC_scan_t scan;
    	    status_t result;

    	    if (VNA_ADC_GetScanResult(&scan, &result))
    	    {
    	        adcScanStatus = result;
    	        adcScanPending = false;

    	        if (result == kStatus_Success)
    	        {
    	            adcValues = scan;
    	        }
    	    }
    	}
    	/* Complete the asynchronous frequency change. */
        if (frequencyUpdatePending)
        {
            status_t result;

            if ( AD9959_GetTransferResult(&result) )
            {
                transferStatus = result;
                frequencyUpdatePending = false;

                if (result == kStatus_Success)
                {
                    AD9959_IoUpdate(coreClock_Hz);
                }
            }
        }
        /* Collect the asynchronous ADL5961 transfer result. */
        if (adlTransferPending)
        {
            status_t result = ADL5961_GetTransferResult(NULL);

            if (result != kStatus_DSPI_Busy)
            {
                adlTransferStatus = result;
                adlTransferPending = false;
            }
        }

        /* Do not start another test while awaiting completion. */
        if (!frequencyUpdatePending  &&
        	    !adlTransferPending  && (AD9959_ColdStart_stat == kStatus_Success) )
        {
            static uint32_t add = 0;
            if (test1)
            {
                test1 = false;
                testStatus = AD9959_StartAutotestBlocking( coreClock_Hz, add + 10000000U, add + 12000000U );
                add += 100000;
            }
            else if (test2)
            {
                test2 = false;
                testStatus = AD9959_StartLoBlocking( coreClock_Hz, add + 20000000U);
                add += 100000;
            }
            else if (test3)
            {
                test3 = false;
                testStatus = AD9959_PowerDownBlocking();
                add += 100000;
            }
            else if (test4)
            {
                test4 = false;
                testStatus = AD9959_SetFrequencySelected( add + 5000000, false );
                add += 100000;

                frequencyUpdatePending =
                    (testStatus == kStatus_Success);
            }
        }
        /* Execute one ADL5961 test at a time. */
        if ((ADL5961_ColdStart_stat == kStatus_Success) &&
            !adlTransferPending &&
            !frequencyUpdatePending)
        {
            volatile bool *request = NULL;
            bool asynchronous = false;
            status_t status = kStatus_Success;

            if (testAdlWakeUp)
            {
                request = &testAdlWakeUp;
                status = ADL5961_WakeUpBlocking();
            }
            else if (testAdlShutdown)
            {
                request = &testAdlShutdown;
                status = ADL5961_ShutBlocking();
            }
            else if (testAdlForwardGain)
            {
                request = &testAdlForwardGain;
                asynchronous = true;

                status = ADL5961_SetForwardGainNonBlocking(
                    forwardGain);
            }
            else if (testAdlReverseGain)
            {
                request = &testAdlReverseGain;
                asynchronous = true;

                status = ADL5961_SetReverseGainNonBlocking(
                    reverseGain);
            }

            if (request != NULL)
            {
                adlTestStatus = status;

                /* Keep the request asserted while the bus is busy. */
                if (status != kStatus_DSPI_Busy)
                {
                    *request = false;

                    if (asynchronous && (status == kStatus_Success))
                    {
                        adlTransferStatus = kStatus_DSPI_Busy;
                        adlTransferPending = true;
                    }
                }
            }
        }



        /* Execute one DRV8912 debugger request when SPI is available.
         * Preserve the request on bus contention; consume all other results.
         * Read/start remain available after a failed startup for diagnosis.
         */
        if (!frequencyUpdatePending && !adlTransferPending)
        {
            volatile bool *request = NULL;
            status_t status = kStatus_Success;
            uint8_t index = drvDeviceIndex;

            /* Hi-Z takes priority when several flags were set together. */
            if (testDrvHighImpedance)
            	request = &testDrvHighImpedance;
            else if (testDrvStart)
            	request = &testDrvStart;
            else if (testDrvReadFaults)
            	request = &testDrvReadFaults;
            else if (testDrvApplyMask)
            	request = &testDrvApplyMask;

            if (request != NULL)
            {
                if (index >= DRV8912_DEVICE_COUNT)
                    status = kStatus_InvalidArgument;
                else if (SPIs_Init_stat != kStatus_Success)
                    status = SPIs_Init_stat;
                else if (DRV8912_InitContext_stat[index] != kStatus_Success)
                    status = DRV8912_InitContext_stat[index];
                else if (request == &testDrvHighImpedance)
                    status = DRV8912_AllHighImpedanceBlocking(&DRV8912_devices[index]);
                else if (request == &testDrvStart)
                {
                    status = DRV8912_StartBlocking(&DRV8912_devices[index], coreClock_Hz, true);
                    if (status != kStatus_DSPI_Busy)
                        DRV8912_Start_stat[index] = status;
                }
                else if (request == &testDrvReadFaults)
                    status = DRV8912_ReadFaultsBlocking(&DRV8912_devices[index], &drvFaults[index]);
                else if (DRV8912_Start_stat[index] != kStatus_Success)
                    status = DRV8912_Start_stat[index];
                else
                    status = DRV8912_ApplyRelayMaskBlocking(&DRV8912_devices[index], drvRelayMask);

                drvTestStatus = status;
                if (status != kStatus_DSPI_Busy) *request = false;
            }
        }

        if (testAdcAbort)
        {
            testAdcAbort = false;
            adcAbortStatus = VNA_ADC_AbortScan();
            /* Leave the pending flag set: collect the aborted result next iteration. */
        }
        else if (testAdcWindow && !adcWindowPending && !adcScanPending &&
            !frequencyUpdatePending && !adlTransferPending &&
            (VNA_ADC_Init_stat == kStatus_Success))
        {
            status_t status = VNA_ADC_StartWindow(adcSamplePeriod_us, adcSampleCount);
            adcWindowStartStatus = status;
            if (status != kStatus_Busy)
            {
                testAdcWindow = false;
                adcWindowPending = (status == kStatus_Success);
                if (adcWindowPending)
                {
                    adcWindow = NULL;
                    adcWindowStatus = kStatus_Busy;
                }
            }
        }
        else if (testAdcScan && !adcScanPending && !adcWindowPending &&
            !frequencyUpdatePending && !adlTransferPending &&
            (VNA_ADC_Init_stat == kStatus_Success))
        {
            status_t status = VNA_ADC_StartScan();
            adcStartStatus = status;

            if (status != kStatus_Busy)
            {
                testAdcScan = false;
                adcScanPending = (status == kStatus_Success);

                if (adcScanPending)
                {
                    adcScanStatus = kStatus_Busy;
                }
            }
        }
        __asm volatile ("nop");
    }
    return 0 ;
}
