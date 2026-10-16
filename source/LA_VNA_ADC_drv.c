#include "LA_VNA_ADC_drv.h"
#include "board.h"
#include "fsl_adc16.h"
#include "fsl_dmamux.h"
#include "fsl_edma.h"
#include "fsl_port.h"
#include "LA_PIT_drv.h"

enum
{
    VNA_ADC_FORWARD_INDEX = 0,
    VNA_ADC_REVERSE_INDEX,
    VNA_ADC_GAIN_INDEX,
    VNA_ADC_PHASE_INDEX,
    VNA_ADC_INPUT_COUNT,
    VNA_ADC_CONVERSIONS_PER_MODULE = 2,
    VNA_ADC_DISABLED_CHANNEL = 31
};

#define VNA_ADC_CODE_COUNT_16BIT  65536UL
#define VNA_ADC_CODE_COUNT_12BIT  4096UL

#define VNA_ADC_REGISTER_GROUP       0U
#define VNA_ADC_DMA_WORD_BYTES       4U
#define VNA_ADC_MIN_CLOCK_HZ         2000000U
#define VNA_ADC_MAX_CLOCK_HZ         12000000U
#define VNA_ADC_CLOCK_DIVISOR        8U
#define VNA_ADC_DMA_STOP_POLL_LIMIT  4096U

#define VNA_ADC_DMA_CHANNEL_MASK \
    ((1UL << VNA_ADC0_RESULT_DMA_CHANNEL) | \
     (1UL << VNA_ADC0_SELECT_DMA_CHANNEL) | \
     (1UL << VNA_ADC1_RESULT_DMA_CHANNEL) | \
     (1UL << VNA_ADC1_SELECT_DMA_CHANNEL))

#if (VNA_ADC0_RESULT_DMA_CHANNEL >= 16U) || \
    (VNA_ADC0_SELECT_DMA_CHANNEL >= 16U) || \
    (VNA_ADC1_RESULT_DMA_CHANNEL >= 16U) || \
    (VNA_ADC1_SELECT_DMA_CHANNEL >= 16U)
#error "K64 eDMA channels must be in the range 0 through 15"
#endif

#if (VNA_ADC0_RESULT_DMA_CHANNEL == VNA_ADC0_SELECT_DMA_CHANNEL) || \
    (VNA_ADC0_RESULT_DMA_CHANNEL == VNA_ADC1_RESULT_DMA_CHANNEL) || \
    (VNA_ADC0_RESULT_DMA_CHANNEL == VNA_ADC1_SELECT_DMA_CHANNEL) || \
    (VNA_ADC0_SELECT_DMA_CHANNEL == VNA_ADC1_RESULT_DMA_CHANNEL) || \
    (VNA_ADC0_SELECT_DMA_CHANNEL == VNA_ADC1_SELECT_DMA_CHANNEL) || \
    (VNA_ADC1_RESULT_DMA_CHANNEL == VNA_ADC1_SELECT_DMA_CHANNEL)
#error "VNA eDMA channels must be distinct"
#endif

/* DMA reads 32-bit R registers; only the low 16 bits are ADC data. */
static volatile uint32_t VNA_ADC_raw[VNA_ADC_INPUT_COUNT];
static edma_handle_t VNA_ADC_resultHandles[2];
static volatile bool VNA_ADC_done[2];
static volatile bool VNA_ADC_pending;
static volatile bool VNA_ADC_initialized;
static volatile bool VNA_ADC_stopFault;
static volatile bool VNA_ADC_scanActive;
static volatile bool VNA_ADC_ready;
static volatile status_t VNA_ADC_finalStatus;
static volatile bool VNA_ADC_windowMode;
static VNA_ADC_window_t VNA_ADC_window;
static uint32_t VNA_ADC_periodCoreCycles;
static uint32_t VNA_ADC_expectedTick;

static const uint32_t VNA_ADC_dmaChannels[4] =
{
    VNA_ADC0_RESULT_DMA_CHANNEL, VNA_ADC0_SELECT_DMA_CHANNEL,
    VNA_ADC1_RESULT_DMA_CHANNEL, VNA_ADC1_SELECT_DMA_CHANNEL
};

static void VNA_ADC_PitTick(pit_chnl_t channel, void *userData);
static void VNA_ADC_StopRequests(void);
static status_t VNA_ADC_Finish(status_t result);
static status_t VNA_ADC_StartScanLocked(void);

static IRQn_Type VNA_ADC_DmaIrq(uint32_t channel)
{
    return (IRQn_Type)((int32_t)DMA0_IRQn + (int32_t)channel);
}

static void VNA_ADC_CopyScan(VNA_ADC_scan_t *scan)
{
    __DMB();
    scan->forwardLevel = (uint16_t)VNA_ADC_raw[VNA_ADC_FORWARD_INDEX];
    scan->reverseLevel = (uint16_t)VNA_ADC_raw[VNA_ADC_REVERSE_INDEX];
    scan->gainRatio = (uint16_t)VNA_ADC_raw[VNA_ADC_GAIN_INDEX];
    scan->phase = (uint16_t)VNA_ADC_raw[VNA_ADC_PHASE_INDEX];
}

/* Writing SC1A with AIEN=0 and DIFF=0 starts a single-ended conversion. */
static const uint32_t VNA_ADC_secondChannels[2] =
{
    ADC_SC1_ADCH(VNA_REVERSE_LEVEL_ADC_CHANNEL),
    ADC_SC1_ADCH(VNA_PHASE_ADC_CHANNEL)
};

void VNA_ADC_InitPins(void)
{
    const port_pin_config_t analog =
    {
        .pullSelect = kPORT_PullDisable,
        .slewRate = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterDisable,
        .openDrainEnable = kPORT_OpenDrainDisable,
        .driveStrength = kPORT_LowDriveStrength,
        .mux = kPORT_PinDisabledOrAnalog,
        .lockRegister = kPORT_UnlockRegister
    };

    CLOCK_EnableClock(VNA_FORWARD_LEVEL_PORT_CLOCK);
    CLOCK_EnableClock(VNA_REVERSE_LEVEL_PORT_CLOCK);
    CLOCK_EnableClock(VNA_GAIN_RATIO_PORT_CLOCK);
    CLOCK_EnableClock(VNA_PHASE_PORT_CLOCK);
    PORT_SetPinConfig(VNA_FORWARD_LEVEL_PORT, VNA_FORWARD_LEVEL_PIN, &analog);
    PORT_SetPinConfig(VNA_REVERSE_LEVEL_PORT, VNA_REVERSE_LEVEL_PIN, &analog);
    PORT_SetPinConfig(VNA_GAIN_RATIO_PORT, VNA_GAIN_RATIO_PIN, &analog);
    PORT_SetPinConfig(VNA_PHASE_PORT, VNA_PHASE_PIN, &analog);
}

static void VNA_ADC_DmaCompleted(edma_handle_t *handle, void *userData,
                                 bool transferDone, uint32_t tcds)
{
    (void)userData;
    (void)tcds;

    uint32_t irqState = DisableGlobalIRQ();

    if (transferDone && VNA_ADC_pending &&
        VNA_ADC_scanActive && !VNA_ADC_ready)
    {
        /* Separate flags avoid a shared read/modify/write in the two IRQs. */
        __DMB();
        if (handle == &VNA_ADC_resultHandles[0])
        {
            VNA_ADC_done[0] = true;
        }
        else if (handle == &VNA_ADC_resultHandles[1])
        {
            VNA_ADC_done[1] = true;
        }

        if ((DMA0->ERR & VNA_ADC_DMA_CHANNEL_MASK) != 0U)
        {
            (void)VNA_ADC_Finish(kStatus_VNA_ADC_DmaError);
        }
        else if (VNA_ADC_done[0] && VNA_ADC_done[1])
        {
            VNA_ADC_scanActive = false;
            if (VNA_ADC_windowMode)
            {
                VNA_ADC_CopyScan(&VNA_ADC_window.samples[VNA_ADC_window.validSamples]);
                ++VNA_ADC_window.validSamples;
                if (VNA_ADC_window.validSamples == VNA_ADC_window.requestedSamples)
                {
                    (void)VNA_ADC_Finish(kStatus_Success);
                }
            }
            else
            {
                (void)VNA_ADC_Finish(kStatus_Success);
            }
        }
    }
    EnableGlobalIRQ(irqState);
}

static void VNA_ADC_StopRequests(void)
{
    ADC16_EnableDMA(ADC0, false);
    ADC16_EnableDMA(ADC1, false);
    ADC0->SC1[VNA_ADC_REGISTER_GROUP] = ADC_SC1_ADCH(VNA_ADC_DISABLED_CHANNEL);
    ADC1->SC1[VNA_ADC_REGISTER_GROUP] = ADC_SC1_ADCH(VNA_ADC_DISABLED_CHANNEL);
    EDMA_DisableChannelRequest(DMA0, VNA_ADC0_RESULT_DMA_CHANNEL);
    EDMA_DisableChannelRequest(DMA0, VNA_ADC0_SELECT_DMA_CHANNEL);
    EDMA_DisableChannelRequest(DMA0, VNA_ADC1_RESULT_DMA_CHANNEL);
    EDMA_DisableChannelRequest(DMA0, VNA_ADC1_SELECT_DMA_CHANNEL);
}

static void VNA_ADC_StopTimer(void)
{
    (void)LA_PIT_Stop(VNA_ADC_PIT_CHANNEL);
}

/* Called with global interrupts masked. No TCD is reset while it is active. */
static status_t VNA_ADC_QuiesceDma(void)
{
    const bool wasHalted = (DMA0->CR & DMA_CR_HALT_MASK) != 0U;
    bool active = true;

    /* Briefly stall new DMA arbitration, including queued linked channels. */
    DMA0->CR |= DMA_CR_HALT_MASK;
    __DSB();
    VNA_ADC_StopRequests();

    for (uint32_t poll = 0U; poll < VNA_ADC_DMA_STOP_POLL_LIMIT; ++poll)
    {
        active = false;
        for (unsigned int i = 0U; i < ARRAY_SIZE(VNA_ADC_dmaChannels); ++i)
        {
            active = active ||
                ((DMA0->TCD[VNA_ADC_dmaChannels[i]].CSR & DMA_CSR_ACTIVE_MASK) != 0U);
        }
        if (!active)
        {
            break;
        }
    }
    if (active)
    {
        /* Keep DMA halted on a hardware stop fault; restart is prohibited. */
        VNA_ADC_initialized = false;
        VNA_ADC_stopFault = true;
        return kStatus_Timeout;
    }

    for (unsigned int i = 0U; i < ARRAY_SIZE(VNA_ADC_dmaChannels); ++i)
    {
        const uint32_t channel = VNA_ADC_dmaChannels[i];
        EDMA_ResetChannel(DMA0, channel); /* Also cancels queued START/link requests. */
        EDMA_ClearChannelStatusFlags(DMA0, channel,
            kEDMA_InterruptFlag | kEDMA_ErrorFlag | kEDMA_DoneFlag);
        NVIC_ClearPendingIRQ(VNA_ADC_DmaIrq(channel));
    }
    (void)ADC0->R[VNA_ADC_REGISTER_GROUP];
    (void)ADC1->R[VNA_ADC_REGISTER_GROUP];
    __DSB();
    if (!wasHalted)
    {
        DMA0->CR &= ~DMA_CR_HALT_MASK;
    }
    return kStatus_Success;

}

static status_t VNA_ADC_Finish(status_t result)
{
    status_t stopStatus;
    VNA_ADC_StopTimer();
    VNA_ADC_scanActive = false;
    stopStatus = VNA_ADC_QuiesceDma();
    VNA_ADC_finalStatus = (stopStatus == kStatus_Success) ? result : stopStatus;
    __DMB();
    VNA_ADC_ready = true;
    return stopStatus;

}

status_t VNA_ADC_Init(void)
{
    adc16_config_t adcConfig;
    edma_config_t dmaConfig;
    status_t status;
    const uint32_t adcClock_Hz =
        CLOCK_GetFreq(kCLOCK_BusClk) / VNA_ADC_CLOCK_DIVISOR;

    if (VNA_ADC_stopFault)
    {
        return kStatus_Fail;
    }
    if (VNA_ADC_pending)
    {
        return kStatus_Busy;
    }
    if (VNA_ADC_initialized)
    {
        return kStatus_Success;
    }
    if ((adcClock_Hz < VNA_ADC_MIN_CLOCK_HZ) ||
        (adcClock_Hz > VNA_ADC_MAX_CLOCK_HZ))
    {
        return kStatus_InvalidArgument;
    }

    if (!LA_PIT_IsInitialized()) return kStatus_Fail;
    status = LA_PIT_ReserveChannel(VNA_ADC_PIT_CHANNEL, VNA_ADC_PitTick, NULL);
    if (status != kStatus_Success) return status;

    VNA_ADC_InitPins();
    ADC16_GetDefaultConfig(&adcConfig);
    adcConfig.clockSource = kADC16_ClockSourceAlt0; /* Bus clock. */
    adcConfig.clockDivider = kADC16_ClockDivider8;
    adcConfig.resolution = kADC16_ResolutionSE16Bit;
    adcConfig.longSampleMode = kADC16_LongSampleCycle24;
    adcConfig.enableContinuousConversion = false;
    adcConfig.hardwareAverageMode = VNA_ADC_HARDWARE_AVERAGE;

    ADC16_Init(ADC0, &adcConfig);
    ADC16_Init(ADC1, &adcConfig);
    ADC16_EnableHardwareTrigger(ADC0, false);
    ADC16_EnableHardwareTrigger(ADC1, false);
    ADC16_EnableDMA(ADC0, false);
    ADC16_EnableDMA(ADC1, false);

    status = ADC16_DoAutoCalibration(ADC0);
    if (status != kStatus_Success)
    {
        (void)LA_PIT_ReleaseChannel(VNA_ADC_PIT_CHANNEL);
        return status;
    }
    status = ADC16_DoAutoCalibration(ADC1);
    if (status != kStatus_Success)
    {
        (void)LA_PIT_ReleaseChannel(VNA_ADC_PIT_CHANNEL);
        return status;
    }

    /* Global DMA initialization belongs here only while no other DMA user exists. */
    EDMA_GetDefaultConfig(&dmaConfig);
    EDMA_Init(DMA0, &dmaConfig);
    DMAMUX_Init(DMAMUX0);
    DMAMUX_DisableChannel(DMAMUX0, VNA_ADC0_SELECT_DMA_CHANNEL);
    DMAMUX_DisableChannel(DMAMUX0, VNA_ADC1_SELECT_DMA_CHANNEL);
    DMAMUX_SetSource(DMAMUX0, VNA_ADC0_RESULT_DMA_CHANNEL, kDmaRequestMux0ADC0);
    DMAMUX_SetSource(DMAMUX0, VNA_ADC1_RESULT_DMA_CHANNEL, kDmaRequestMux0ADC1);
    DMAMUX_EnableChannel(DMAMUX0, VNA_ADC0_RESULT_DMA_CHANNEL);
    DMAMUX_EnableChannel(DMAMUX0, VNA_ADC1_RESULT_DMA_CHANNEL);

    EDMA_CreateHandle(&VNA_ADC_resultHandles[0], DMA0, VNA_ADC0_RESULT_DMA_CHANNEL);
    EDMA_CreateHandle(&VNA_ADC_resultHandles[1], DMA0, VNA_ADC1_RESULT_DMA_CHANNEL);
    EDMA_SetCallback(&VNA_ADC_resultHandles[0], VNA_ADC_DmaCompleted, NULL);
    EDMA_SetCallback(&VNA_ADC_resultHandles[1], VNA_ADC_DmaCompleted, NULL);
    for (unsigned int i = 0U; i < ARRAY_SIZE(VNA_ADC_dmaChannels); ++i)
    {
        EDMA_DisableChannelInterrupts(DMA0, VNA_ADC_dmaChannels[i],
                                      kEDMA_ErrorInterruptEnable);
    }
    NVIC_SetPriority(VNA_ADC_DmaIrq(VNA_ADC0_RESULT_DMA_CHANNEL), VNA_ADC_IRQ_PRIORITY);
    NVIC_SetPriority(VNA_ADC_DmaIrq(VNA_ADC1_RESULT_DMA_CHANNEL), VNA_ADC_IRQ_PRIORITY);
    VNA_ADC_StopTimer();
    /* Do not reset a cycle counter that another module may already be using. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    VNA_ADC_StopRequests();
    VNA_ADC_initialized = true;
    return kStatus_Success;

}

static status_t VNA_ADC_ArmModule(ADC_Type *adc, edma_handle_t *resultHandle,
                                  uint32_t selectChannel, unsigned int module,
                                  volatile uint32_t *destination)
{

	edma_transfer_config_t transfer;
    status_t status;

    EDMA_ResetChannel(DMA0, resultHandle->channel);
    EDMA_ResetChannel(DMA0, selectChannel);
    EDMA_ClearChannelStatusFlags(DMA0, resultHandle->channel,
                                 kEDMA_InterruptFlag | kEDMA_ErrorFlag | kEDMA_DoneFlag);
    EDMA_ClearChannelStatusFlags(DMA0, selectChannel,
                                 kEDMA_InterruptFlag | kEDMA_ErrorFlag | kEDMA_DoneFlag);
    NVIC_ClearPendingIRQ(VNA_ADC_DmaIrq(resultHandle->channel));
    NVIC_ClearPendingIRQ(VNA_ADC_DmaIrq(selectChannel));

    /* Linked channel writes the second ADC input number to SC1A. */
    EDMA_PrepareTransfer(&transfer,
                         (void *)&VNA_ADC_secondChannels[module], VNA_ADC_DMA_WORD_BYTES,
                         (void *)&adc->SC1[VNA_ADC_REGISTER_GROUP], VNA_ADC_DMA_WORD_BYTES,
                         VNA_ADC_DMA_WORD_BYTES, VNA_ADC_DMA_WORD_BYTES,
                         kEDMA_MemoryToPeripheral);
    EDMA_SetTransferConfig(DMA0, selectChannel, &transfer, NULL);

    /* Each COCO/DMA request transfers one result, for two results total. */
    EDMA_PrepareTransfer(&transfer,
                         (void *)&adc->R[VNA_ADC_REGISTER_GROUP], VNA_ADC_DMA_WORD_BYTES,
                         (void *)destination, VNA_ADC_DMA_WORD_BYTES,
                         VNA_ADC_DMA_WORD_BYTES,
                         VNA_ADC_CONVERSIONS_PER_MODULE * VNA_ADC_DMA_WORD_BYTES,
                         kEDMA_PeripheralToMemory);
    status = EDMA_SubmitTransfer(resultHandle, &transfer);
    if (status != kStatus_Success)
    {
        return status;
    }

    /* Minor linking occurs after the first result, not the final major iteration. */
    EDMA_SetChannelLink(DMA0, resultHandle->channel, kEDMA_MinorLink, selectChannel);
    return kStatus_Success;

}

static status_t VNA_ADC_StartScanLocked(void)
{
    status_t status;

    VNA_ADC_StopRequests();
    /* Clear any stale conversion-complete condition before enabling DMA. */
    (void)ADC0->R[VNA_ADC_REGISTER_GROUP];
    (void)ADC1->R[VNA_ADC_REGISTER_GROUP];
    VNA_ADC_done[0] = false;
    VNA_ADC_done[1] = false;

    status = VNA_ADC_ArmModule(ADC0, &VNA_ADC_resultHandles[0],
                               VNA_ADC0_SELECT_DMA_CHANNEL, 0U,
                               &VNA_ADC_raw[VNA_ADC_FORWARD_INDEX]);
    if (status != kStatus_Success)
    {
        return status;
    }
    status = VNA_ADC_ArmModule(ADC1, &VNA_ADC_resultHandles[1],
                               VNA_ADC1_SELECT_DMA_CHANNEL, 1U,
                               &VNA_ADC_raw[VNA_ADC_GAIN_INDEX]);
    if (status != kStatus_Success)
    {
        return status;
    }

    VNA_ADC_scanActive = true;
    __DMB();
    EDMA_StartTransfer(&VNA_ADC_resultHandles[0]);
    EDMA_StartTransfer(&VNA_ADC_resultHandles[1]);
    ADC16_EnableDMA(ADC0, true);
    ADC16_EnableDMA(ADC1, true);

    /* The two first conversions start a few CPU instructions apart. */
    ADC0->SC1[VNA_ADC_REGISTER_GROUP] = ADC_SC1_ADCH(VNA_FORWARD_LEVEL_ADC_CHANNEL);
    ADC1->SC1[VNA_ADC_REGISTER_GROUP] = ADC_SC1_ADCH(VNA_GAIN_RATIO_ADC_CHANNEL);
    return kStatus_Success;

}

status_t VNA_ADC_StartScan(void)
{
    uint32_t irqState = DisableGlobalIRQ();
    status_t status;

    if (!VNA_ADC_initialized || ((DMA0->CR & DMA_CR_HALT_MASK) != 0U))
    {
        status = kStatus_Fail;
    }
    else if (VNA_ADC_pending)
    {
        status = kStatus_Busy;
    }
    else
    {
        VNA_ADC_windowMode = false;
        VNA_ADC_pending = true;
        VNA_ADC_ready = false;
        status = VNA_ADC_StartScanLocked();
        if (status != kStatus_Success)
        {
            status_t stopStatus = VNA_ADC_QuiesceDma();
            if (stopStatus != kStatus_Success)
            {
                status = stopStatus;
            }
            VNA_ADC_pending = false;
        }
    }
    EnableGlobalIRQ(irqState);
    return status;

}

bool VNA_ADC_GetScanResult(VNA_ADC_scan_t *scan, status_t *status)
{
    bool available = false;
    if ((scan == NULL) || (status == NULL))
    {
        return false;
    }

    uint32_t irqState = DisableGlobalIRQ();
    if (VNA_ADC_pending && !VNA_ADC_windowMode)
    {
        if (!VNA_ADC_ready && ((DMA0->ERR & VNA_ADC_DMA_CHANNEL_MASK) != 0U))
        {
            (void)VNA_ADC_Finish(kStatus_VNA_ADC_DmaError);
        }
        if (VNA_ADC_ready)
        {
            *status = VNA_ADC_finalStatus;
            if (*status == kStatus_Success)
            {
                VNA_ADC_CopyScan(scan);
            }
            VNA_ADC_pending = false;
            VNA_ADC_ready = false;
            available = true;
        }
    }
    EnableGlobalIRQ(irqState);
    return available;

}

status_t VNA_ADC_StartWindow(uint32_t samplePeriod_us, uint16_t sampleCount)
{

	const uint32_t timerClock_Hz = CLOCK_GetFreq(kCLOCK_BusClk);
    const uint32_t coreClock_Hz = CLOCK_GetFreq(kCLOCK_CoreSysClk);
    uint64_t ticks;
    uint64_t coreCycles;
    status_t status;

    if ((samplePeriod_us == 0U) || (sampleCount == 0U) ||
        (sampleCount > VNA_ADC_WINDOW_SAMPLES) || (timerClock_Hz == 0U) ||
        (coreClock_Hz == 0U))
    {
        return kStatus_InvalidArgument;
    }
    ticks = ((uint64_t)samplePeriod_us * timerClock_Hz + 999999U) / 1000000U;
    if ((ticks == 0U) || (ticks > UINT32_MAX))
    {
        return kStatus_OutOfRange;
    }
    coreCycles = (ticks * coreClock_Hz + timerClock_Hz - 1U) / timerClock_Hz;
    if ((coreCycles == 0U) || (coreCycles > INT32_MAX))
    {
        return kStatus_OutOfRange;
    }

    uint32_t irqState = DisableGlobalIRQ();
    if (!VNA_ADC_initialized || ((DMA0->CR & DMA_CR_HALT_MASK) != 0U))
    {
        status = kStatus_Fail;
    }
    else if (VNA_ADC_pending)
    {
        status = kStatus_Busy;
    }
    else
    {
        VNA_ADC_StopTimer();
        VNA_ADC_windowMode = true;
        VNA_ADC_pending = true;
        VNA_ADC_ready = false;
        status = LA_PIT_SetPeriod(VNA_ADC_PIT_CHANNEL, samplePeriod_us);
        if (status == kStatus_Success) status = VNA_ADC_StartScanLocked();
        if (status == kStatus_Success)
        {
            VNA_ADC_periodCoreCycles = (uint32_t)coreCycles;
            /* One-scan windows do not need a running timer. */
            if (sampleCount > 1U)
            {
                VNA_ADC_expectedTick = DWT->CYCCNT + VNA_ADC_periodCoreCycles;
                status = LA_PIT_Start(VNA_ADC_PIT_CHANNEL, true);
            }
        }
        if (status == kStatus_Success)
        {
            /* Publish only after both DMA and timer setup have succeeded. */
            VNA_ADC_window.samplePeriod_us = samplePeriod_us;
            VNA_ADC_window.timerPeriod_ticks = (uint32_t)ticks;
            VNA_ADC_window.timerClock_Hz = timerClock_Hz;
            VNA_ADC_window.requestedSamples = sampleCount;
            VNA_ADC_window.validSamples = 0U;
        }
        else
        {
            status_t stopStatus = VNA_ADC_QuiesceDma();
            if (stopStatus != kStatus_Success)
            {
                status = stopStatus;
            }
            VNA_ADC_pending = false;
        }
    }
    EnableGlobalIRQ(irqState);
    return status;

}

static void VNA_ADC_PitTick(pit_chnl_t channel, void *userData)
{

	(void)channel;
    (void)userData;
    uint32_t irqState = DisableGlobalIRQ();
    if (VNA_ADC_pending && VNA_ADC_windowMode && !VNA_ADC_ready)
    {
        const int32_t lateCycles = (int32_t)( DWT->CYCCNT - VNA_ADC_expectedTick );
        if ((DMA0->ERR & VNA_ADC_DMA_CHANNEL_MASK) != 0U)
        {
            (void)VNA_ADC_Finish(kStatus_VNA_ADC_DmaError);
        }
        else if (VNA_ADC_scanActive ||
                 (lateCycles >= (int32_t)VNA_ADC_periodCoreCycles))
        {
            /* Never silently skip a sample or merge missed PIT ticks. */
            (void)VNA_ADC_Finish(kStatus_VNA_ADC_Overrun);
        }
        else
        {
            VNA_ADC_expectedTick += VNA_ADC_periodCoreCycles;
            status_t status = VNA_ADC_StartScanLocked();
            if (status != kStatus_Success)
            {
                (void)VNA_ADC_Finish(status);
            }
        }
    }
    EnableGlobalIRQ(irqState);

}

bool VNA_ADC_GetWindowResult(const VNA_ADC_window_t **window, status_t *status)
{
    bool available = false;
    if ((window == NULL) || (status == NULL))
    {
        return false;
    }
    uint32_t irqState = DisableGlobalIRQ();
    if (VNA_ADC_pending && VNA_ADC_windowMode)
    {
        if (!VNA_ADC_ready && ((DMA0->ERR & VNA_ADC_DMA_CHANNEL_MASK) != 0U))
        {
            (void)VNA_ADC_Finish(kStatus_VNA_ADC_DmaError);
        }
        if (VNA_ADC_ready)
        {
            __DMB();
            *window = &VNA_ADC_window;
            *status = VNA_ADC_finalStatus;
            VNA_ADC_pending = false;
            VNA_ADC_ready = false;
            available = true;
        }
    }
    EnableGlobalIRQ(irqState);
    return available;

}

status_t VNA_ADC_AbortScan(void)
{
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_NoTransferInProgress;
    if (VNA_ADC_pending && !VNA_ADC_ready)
    {
        status = VNA_ADC_Finish(kStatus_VNA_ADC_Aborted);
    }
    EnableGlobalIRQ(irqState);
    return status;

}

/* K64 RM tables 35-104 through 35-108. MODE order is 8,12,10,16. */
static status_t VNA_ADC_DecodeTiming(VNA_ADC_moduleTiming_t *t)
{

	static const uint8_t seBits[4] = {8U, 12U, 10U, 16U};
    static const uint8_t diffBits[4] = {9U, 13U, 11U, 16U};
    static const uint8_t seBct[4] = {17U, 20U, 20U, 25U};
    static const uint8_t diffBct[4] = {27U, 30U, 30U, 34U};
    static const uint8_t longAdders[4] = {20U, 12U, 6U, 2U};
    enum { ClockBus = 0U, ClockBusHalf, ClockExternal, ClockAsynchronous };
    const uint32_t source = (t->cfg1 & ADC_CFG1_ADICLK_MASK) >> ADC_CFG1_ADICLK_SHIFT;
    const uint32_t mode = (t->cfg1 & ADC_CFG1_MODE_MASK) >> ADC_CFG1_MODE_SHIFT;
    const bool longSample = (t->cfg1 & ADC_CFG1_ADLSMP_MASK) != 0U;
    uint32_t sourceDivider = 1U;
    switch (source)
    {
        case ClockBus: t->sourceClock_Hz = t->busClock_Hz; break;
        case ClockBusHalf:
            t->sourceClock_Hz = t->busClock_Hz / 2U;
            sourceDivider = 2U;
            break;
        case ClockExternal:
            t->sourceClock_Hz = CLOCK_GetFreq(kCLOCK_Osc0ErClk);
            break;
        case ClockAsynchronous: return kStatus_VNA_ADC_ClockUnknown;
        default: return kStatus_Fail;
    }
    if (t->sourceClock_Hz == 0U || t->busClock_Hz == 0U) return kStatus_VNA_ADC_ClockUnknown;
    t->clockDivider = (uint8_t)(1U << ((t->cfg1 & ADC_CFG1_ADIV_MASK) >> ADC_CFG1_ADIV_SHIFT));
    t->adcClock_Hz = t->sourceClock_Hz / t->clockDivider;
    t->differential = (t->sc1 & ADC_SC1_DIFF_MASK) != 0U;
    t->continuous = (t->sc3 & ADC_SC3_ADCO_MASK) != 0U;
    t->hardwareTrigger = (t->sc2 & ADC_SC2_ADTRG_MASK) != 0U;
    t->resolutionBits = t->differential ? diffBits[mode] : seBits[mode];
    t->averageCount = (t->sc3 & ADC_SC3_AVGE_MASK) != 0U ?
        (uint8_t)(4U << ((t->sc3 & ADC_SC3_AVGS_MASK) >> ADC_SC3_AVGS_SHIFT)) : 1U;
    uint32_t bct = t->differential ? diffBct[mode] : seBct[mode];
    uint32_t lst = longSample ? longAdders[(t->cfg2 & ADC_CFG2_ADLSTS_MASK) >> ADC_CFG2_ADLSTS_SHIFT] : 0U;
    uint32_t hsc = (t->cfg2 & ADC_CFG2_ADHSC_MASK) != 0U ? 2U : 0U;
    t->conversionAdckCycles = (uint16_t)((longSample ? 3U : 5U) +
                                       t->averageCount * (bct + lst + hsc));
    t->conversionBusCycles = 5U;
    /* Preserve bus/2 as a rational rate rather than rounding a half-Hz source. */
    uint32_t sourceHz = source == ClockBusHalf ? t->busClock_Hz : t->sourceClock_Hz;
    uint64_t numerator = (uint64_t)t->conversionAdckCycles * t->clockDivider *
                         sourceDivider * 1000000000ULL;
    t->conversion_ns = (numerator + sourceHz - 1U) / sourceHz;
    numerator = (uint64_t)t->conversionBusCycles * 1000000000ULL;
    t->conversion_ns += (numerator + t->busClock_Hz - 1U) / t->busClock_Hz;
    return kStatus_Success;

}
status_t VNA_ADC_GetTiming(VNA_ADC_timing_t *timing)
{
    if (timing == NULL)
    	return kStatus_InvalidArgument;
    if (!VNA_ADC_initialized || VNA_ADC_stopFault)
    	return kStatus_Fail;

    VNA_ADC_timing_t snapshot = {0};
    ADC_Type *const modules[2] = {ADC0, ADC1};
    uint32_t irqState = DisableGlobalIRQ();
    uint32_t busClock = CLOCK_GetFreq(kCLOCK_BusClk);

    for (unsigned int i = 0U; i < 2U; ++i)
    {
        VNA_ADC_moduleTiming_t *t = &snapshot.adc[i];
        t->cfg1 = modules[i]->CFG1;
        t->cfg2 = modules[i]->CFG2;
        t->sc1 = modules[i]->SC1[VNA_ADC_REGISTER_GROUP];
        t->sc2 = modules[i]->SC2;
        t->sc3 = modules[i]->SC3;
        t->busClock_Hz = busClock;
    }

    EnableGlobalIRQ(irqState);

    for (unsigned int i = 0U; i < 2U; ++i)
    {
        status_t status = VNA_ADC_DecodeTiming(&snapshot.adc[i]);
        if (status != kStatus_Success) return status;
        uint64_t moduleScan = VNA_ADC_CONVERSIONS_PER_MODULE * snapshot.adc[i].conversion_ns;
        if (moduleScan > snapshot.hardwareScan_ns) snapshot.hardwareScan_ns = moduleScan;
    }
    *timing = snapshot;
    return kStatus_Success;

}

/*
 * Convert a 16-bit single-ended ADC code to integer millivolts.
 * Assumes VREFL = ground. Round to the nearest millivolt.
 * reference_mV is the actual VREFH voltage, nominally 3300 mV.
 * Esample usage
 * uint32_t forward_mV = VNA_ADC_RawToMillivolts(
    adcValues.forwardLevel, VNA_ADC_REFERENCE_MV);
 */
uint32_t VNA_ADC_RawToMillivolts(uint16_t raw, uint32_t reference_mV )
{
    return (uint32_t)(
        ((uint64_t)raw * reference_mV +
         VNA_ADC_CODE_COUNT_16BIT / 2U) /
        VNA_ADC_CODE_COUNT_16BIT);
}
