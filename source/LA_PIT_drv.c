/* ------------------------ I N C L U D E    F I L E S ------------------------ */


#include "LA_PIT_drv.h"
#include "fsl_clock.h"


/* ------------------------ L O C A L    M A C R O S   ------------------------ */


#define LA_PIT_CHANNEL_COUNT 4U
#define LA_PIT_IRQ_PRIORITY 3U
#define LA_PIT_US_PER_SECOND 1000000U

/* ------------------- L O C A L     D E F I N I T I O N S  ------------------- */


typedef struct LA_PIT_channel_t
{
    LA_PIT_callback_t callback;
    void *userData;
    LA_PIT_period_t period;
    bool periodic;
} LA_PIT_channel_t;

/* ------------------ L O C A L           F U N C T I O N S   ---------------- */
static bool LA_PIT_Valid(pit_chnl_t channel);
static void LA_PIT_StopLocked(pit_chnl_t channel);
static void LA_PIT_Dispatch(pit_chnl_t channel);

/* ------------------------- G L O B A L     D A T A   ------------------------ */


/* -------------------------- L O C A L     D A T A   ------------------------- */

static LA_PIT_channel_t LA_PIT_channels[LA_PIT_CHANNEL_COUNT];
static bool LA_PIT_initialized;
static bool LA_PIT_runInDebug;
static const IRQn_Type LA_PIT_irqs[LA_PIT_CHANNEL_COUNT] =
    { PIT0_IRQn, PIT1_IRQn, PIT2_IRQn, PIT3_IRQn };



/* ----------------------  I M P L E M E N T A T I O N   ---------------------- */


static bool LA_PIT_Valid(pit_chnl_t channel)
{
    return (uint32_t)channel < LA_PIT_CHANNEL_COUNT;
}

static void LA_PIT_StopLocked(pit_chnl_t channel)
{
    PIT_StopTimer(PIT, channel);
    PIT_DisableInterrupts(PIT, channel, kPIT_TimerInterruptEnable);
    PIT_ClearStatusFlags(PIT, channel, kPIT_TimerFlag);
    NVIC_ClearPendingIRQ(LA_PIT_irqs[channel]);
}

status_t LA_PIT_Init(bool runInDebug)
{
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_Success;
    if (LA_PIT_initialized)
    {
        if (LA_PIT_runInDebug != runInDebug) status = kStatus_Busy;
    }
    else
    {
        pit_config_t config;
        PIT_GetDefaultConfig(&config);
        config.enableRunInDebug = runInDebug;
        PIT_Init(PIT, &config);
        LA_PIT_runInDebug = runInDebug;
        LA_PIT_initialized = true;
    }
    EnableGlobalIRQ(irqState);
    return status;
}

bool LA_PIT_IsInitialized(void)
{
	return LA_PIT_initialized;
}

status_t LA_PIT_ReserveChannel(pit_chnl_t channel, LA_PIT_callback_t callback,
                               void *userData)
{
    if (!LA_PIT_Valid(channel) || callback == NULL) return kStatus_InvalidArgument;
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_Success;
    LA_PIT_channel_t *ctx = &LA_PIT_channels[channel];
    if (!LA_PIT_initialized) status = kStatus_Fail;
    else if (ctx->callback != NULL)
    {
        if (ctx->callback != callback || ctx->userData != userData) status = kStatus_Busy;
    }
    else
    {
        LA_PIT_StopLocked(channel);
        PIT_SetTimerChainMode(PIT, channel, false);
        ctx->callback = callback;
        ctx->userData = userData;
        NVIC_SetPriority(LA_PIT_irqs[channel], LA_PIT_IRQ_PRIORITY);
        (void)EnableIRQ(LA_PIT_irqs[channel]);
    }
    EnableGlobalIRQ(irqState);
    return status;
}
status_t LA_PIT_SetPeriod(pit_chnl_t channel, uint32_t period_us)
{
    if (!LA_PIT_Valid(channel) || period_us == 0U) return kStatus_InvalidArgument;
    uint32_t clock = CLOCK_GetFreq(kCLOCK_BusClk);
    if (clock == 0U) return kStatus_Fail;
    uint64_t ticks = ((uint64_t)period_us * clock + LA_PIT_US_PER_SECOND - 1U)
                     / LA_PIT_US_PER_SECOND;
    if (ticks == 0U || ticks > UINT32_MAX) return kStatus_OutOfRange;
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_Success;
    if (!LA_PIT_initialized || LA_PIT_channels[channel].callback == NULL) status = kStatus_Fail;
    else if ((PIT->CHANNEL[channel].TCTRL & PIT_TCTRL_TEN_MASK) != 0U) status = kStatus_Busy;
    else
    {
        PIT_SetTimerPeriod(PIT, channel, (uint32_t)ticks);
        LA_PIT_channels[channel].period = (LA_PIT_period_t){period_us, (uint32_t)ticks, clock};
    }
    EnableGlobalIRQ(irqState);
    return status;
}
status_t LA_PIT_GetPeriod(pit_chnl_t channel, LA_PIT_period_t *period)
{
    if (!LA_PIT_Valid(channel) || period == NULL) return kStatus_InvalidArgument;
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_Fail;
    if (LA_PIT_initialized && LA_PIT_channels[channel].callback != NULL &&
        LA_PIT_channels[channel].period.ticks != 0U)
    {
        *period = LA_PIT_channels[channel].period;
        status = kStatus_Success;
    }
    EnableGlobalIRQ(irqState);
    return status;
}


status_t LA_PIT_Start(pit_chnl_t channel, bool periodic)
{
    if (!LA_PIT_Valid(channel)) return kStatus_InvalidArgument;
    uint32_t irqState = DisableGlobalIRQ();
    LA_PIT_channel_t *ctx = &LA_PIT_channels[channel];
    status_t status = kStatus_Success;
    if (!LA_PIT_initialized || ctx->callback == NULL || ctx->period.ticks == 0U ||
        ctx->period.clock_Hz != CLOCK_GetFreq(kCLOCK_BusClk)) status = kStatus_Fail;
    else if ((PIT->CHANNEL[channel].TCTRL & PIT_TCTRL_TEN_MASK) != 0U) status = kStatus_Busy;
    else
    {
        LA_PIT_StopLocked(channel);
        ctx->periodic = periodic;
        PIT_EnableInterrupts(PIT, channel, kPIT_TimerInterruptEnable);
        PIT_StartTimer(PIT, channel);
    }
    EnableGlobalIRQ(irqState);
    return status;
}


status_t LA_PIT_Stop(pit_chnl_t channel)
{
    if (!LA_PIT_Valid(channel)) return kStatus_InvalidArgument;
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_Fail;
    if (LA_PIT_initialized && LA_PIT_channels[channel].callback != NULL)
    {
        LA_PIT_StopLocked(channel);
        status = kStatus_Success;
    }
    EnableGlobalIRQ(irqState);
    return status;
}


status_t LA_PIT_ReleaseChannel(pit_chnl_t channel)
{
    if (!LA_PIT_Valid(channel)) return kStatus_InvalidArgument;
    uint32_t irqState = DisableGlobalIRQ();
    status_t status = kStatus_Fail;
    if (LA_PIT_initialized && LA_PIT_channels[channel].callback != NULL)
    {
        LA_PIT_StopLocked(channel);
        (void)DisableIRQ(LA_PIT_irqs[channel]);
        LA_PIT_channels[channel] = (LA_PIT_channel_t){0};
        status = kStatus_Success;
    }
    EnableGlobalIRQ(irqState);
    return status;
}


static void LA_PIT_Dispatch(pit_chnl_t channel)
{
    /* Snapshot and acknowledge atomically against client Stop/Release. */
    uint32_t irqState = DisableGlobalIRQ();
    LA_PIT_callback_t callback = NULL;
    void *userData = NULL;
    if ((PIT_GetStatusFlags(PIT, channel) & kPIT_TimerFlag) != 0U)
    {
        PIT_ClearStatusFlags(PIT, channel, kPIT_TimerFlag);
        LA_PIT_channel_t *ctx = &LA_PIT_channels[channel];
        if (ctx->callback != NULL &&
            (PIT->CHANNEL[channel].TCTRL & PIT_TCTRL_TEN_MASK) != 0U)
        {
            callback = ctx->callback;
            userData = ctx->userData;
            if (!ctx->periodic) LA_PIT_StopLocked(channel);
        }
    }
    EnableGlobalIRQ(irqState);
    if (callback != NULL) callback(channel, userData);
    __DSB();
}

void PIT0_IRQHandler(void) {
	LA_PIT_Dispatch(kPIT_Chnl_0);
}

void PIT1_IRQHandler(void) {
	LA_PIT_Dispatch(kPIT_Chnl_1);
}

void PIT2_IRQHandler(void) {
	LA_PIT_Dispatch(kPIT_Chnl_2);
}

void PIT3_IRQHandler(void) {
	LA_PIT_Dispatch(kPIT_Chnl_3);
}
