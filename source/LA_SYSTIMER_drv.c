/*
 * ARM CORTEX-M SYSTICK: 1 ms UPTIME AND LINKED SOFTWARE TIMERS
 * ========================================================
 * Port of Pietro di Castri's PIT0 list manager to MK64F12/CMSIS SysTick.
 * No PIT channels, heap, UART/debug dependencies or RAM linker sections.
 * TimerObj was reconstructed from the supplied .c: the pasted "header" was
 * another copy of that source, so its original type/flag layout was unavailable.
 *
 * INITIALIZATION / USE
 * After configuring board clocks: Init(), then Start(), checking both results.
 * Init refreshes SystemCoreClock, sets a core-clock 1 ms reload and the lowest
 * interrupt priority, but leaves the counter stopped. Start enables it.
 * Init refuses to take over a SysTick that is already enabled or to reset an
 * already initialized module. This module owns SysTick; not for an RTOS which
 * owns it. Configure exactly one strong SysTick_Handler in the whole project.
 * Default: this .c supplies the handler. To use your own, build THIS .c with
 * LA_SYSTIMER_DEFINE_HANDLER=0 and call ProcessTick exactly once per 1 ms ISR.
 * Clock changes require stopping and reconfiguring the reload at application
 * level; Init is deliberately not a hot-reconfiguration operation.
 *
 * TIMER OBJECTS
 * Use persistent zero-initialized TimerObj objects, never stack objects that
 * expire while listed. Set repeat (milliseconds, >=1), flags and timerFunc
 * before timer_add(). timerFunc may be NULL: a timer can be polled via take_hit.
 * period=1 fires every tick; period=N fires after N ticks, then every N ticks.
 * This fixes the old PIT loop's repeat+1 tick period. Deadlines are quantized:
 * first expiration occurs on the Nth tick after add/retrigger, not necessarily
 * N complete wall-clock milliseconds after the call.
 * timer_add returns bool (old callers may still ignore it); duplicates, NULL,
 * zero periods and more than MAX_TIMERS are rejected without changing the list.
 * Dropping a timer detaches it; Stop freezes uptime and all countdowns, without
 * removing timers. Restart resumes software countdowns on a fresh tick phase.
 * Disabled timers retain their countdown. Re-enable resumes it; retrig restarts.
 * update_count sets both period and countdown; cnt=0 is rejected/no change.
 * shifthalf_count loads max(1,repeat/2), leaving the recurring period unchanged.
 *
 * FLAGS
 * tmrEnabled: serviced each tick. tmrHit: at least one expiration is pending.
 * tmrOverflow: another expiration occurred before the previous hit was consumed.
 * timer_take_hit atomically reads/clears Hit and Overflow. These are coalesced
 * event flags, not a count. A callback also runs on every expiration, regardless
 * of the pending flags. No one-shot semantics are inferred from missing headers.
 *
 * INTERRUPT / CONCURRENCY CONTRACT
 * Timer callbacks and the optional tick hook execute INSIDE SysTick ISR. Keep
 * them short: set a flag, process it in main. No blocking FLASH/SPI, delay loops,
 * console output, list mutations, direct active-object writes or reentry into
 * ProcessTick. Lifecycle/list/control API calls from any ISR are rejected.
 * Foreground API critical sections save/restore PRIMASK, never unconditionally
 * enable interrupts. Listed object fields must be changed through these APIs.
 * MAX_TIMERS bounds traversal under critical sections and inside the ISR.
 * No implicit hwtimer_callback external symbol is required; SetTickCallback
 * installs an optional board hook with userData instead.
 *
 * FLASH TIMEBASE
 * Set config.getTimeMs = LA_SYSTIMER_GetTimeMs before FLASH InitContext.
 * The signature already matches; userData is ignored by the uptime getter.
 * Start SysTick first and keep interrupts enabled during FLASH waits. Stop,
 * debugger halt, long interrupt masking or long higher-priority ISRs stop/delay
 * software uptime; SysTick pending interrupts do not count all missed ticks.
 * getTimeMs is a modulo-2^32 uptime (wrap about 49.7 days). Never reset it while
 * a consumer has an outstanding timeout. No sleeping-time compensation.
 */

/* ------------------------ I N C L U D E    F I L E S ------------------------ */
#include "LA_SYSTIMER_drv.h"
#include "fsl_device_registers.h"
#include <stddef.h>

/* ------------------------ L O C A L    M A C R O S -------------------------- */
#define LA_SYSTIMER_PRIORITY ((1UL << __NVIC_PRIO_BITS) - 1UL)

/* -------------------------- L O C A L    D A T A --------------------------- */
static pTimerObj timerBase;
static uint32_t timerCount;
static volatile uint32_t milliseconds;
static volatile bool initialized;
static volatile bool running;
static bool inTick;
static LA_SYSTIMER_TickCallback_t tickCallback;
static void *tickUserData;

/* --------------------- L O C A L    F U N C T I O N S ----------------------- */
/* Foreground only. Save IRQ state, mask interrupts; callers restore that state. */
static uint32_t LA_SYSTIMER_Lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return saved;
}
static void LA_SYSTIMER_Unlock(uint32_t saved)
{
    __DMB();
    __set_PRIMASK(saved);
}
static bool LA_SYSTIMER_Foreground(void)
{
    return __get_IPSR() == 0U && !inTick;
}
/* Caller owns the critical section. Search is bounded by the configured limit. */
static bool LA_SYSTIMER_Contains(pTimerObj pt)
{
    pTimerObj runner = timerBase;
    for (uint32_t i = 0U; runner != NULL && i < LA_SYSTIMER_MAX_TIMERS; ++i)
    {
        if (runner == pt) return true;
        runner = runner->next;
    }
    return false;
}

/* ---------------------- I M P L E M E N T A T I O N ------------------------- */
/* INPUT: configured board clocks, exclusive SysTick ownership. OUTPUT: stopped
 * 1 ms SysTick configuration. RETURN false on context, ownership or clock error.
 * Failed validation leaves SysTick registers and this module unchanged.
 */
bool LA_SYSTIMER_Init(void)
{
    uint32_t saved, ticks;
    if (!LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock();
    if (initialized || (SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) != 0U)
    { LA_SYSTIMER_Unlock(saved); return false; }
    SystemCoreClockUpdate();
    ticks = SystemCoreClock / LA_SYSTIMER_TICK_HZ;
    if (ticks == 0U || SystemCoreClock % LA_SYSTIMER_TICK_HZ != 0U ||
        ticks - 1U > SysTick_LOAD_RELOAD_Msk)
    { LA_SYSTIMER_Unlock(saved); return false; }
    SysTick->CTRL = 0U;
    SysTick->LOAD = ticks - 1U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    NVIC_SetPriority(SysTick_IRQn, (uint32_t)LA_SYSTIMER_PRIORITY);
    milliseconds = 0U;
    initialized = true;
    LA_SYSTIMER_Unlock(saved);
    return true;
}

/* INPUT: initialized module. OUTPUT: starts a fresh hardware tick phase.
 * Repeated Start while running is harmless; software countdowns are preserved.
 */
bool LA_SYSTIMER_Start(void)
{
    uint32_t saved;
    if (!LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock();
    if (!initialized) { LA_SYSTIMER_Unlock(saved); return false; }
    if (!running)
    {
        SysTick->VAL = 0U;
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        running = true;
        SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
            SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    }
    LA_SYSTIMER_Unlock(saved);
    return true;
}

/* OUTPUT: freezes uptime/countdowns and removes a pending SysTick interrupt.
 * Does not clear the list, event flags or elapsed milliseconds. ISR call ignored.
 */
void LA_SYSTIMER_Stop(void)
{
    uint32_t saved;
    if (!LA_SYSTIMER_Foreground()) return;
    saved = LA_SYSTIMER_Lock();
    if (initialized)
    {
        SysTick->CTRL = 0U;
        SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
        running = false;
    }
    LA_SYSTIMER_Unlock(saved);
}

/* INPUT: one invocation per actual 1 ms ISR. OUTPUT: uptime, expirations and
 * callbacks. Guard against reentry; stopped/uninitialized ticks have no effect.
 * Stable list during callbacks: all mutation APIs are foreground-only.
 */
void LA_SYSTIMER_ProcessTick(void)
{
    pTimerObj pt;
    if (!running || inTick) return;
    inTick = true;
    ++milliseconds;
    if (tickCallback != NULL) tickCallback(tickUserData);
    pt = timerBase;
    for (uint32_t i = 0U; pt != NULL && i < LA_SYSTIMER_MAX_TIMERS; ++i)
    {
        if ((pt->flags & tmrEnabled) != 0U)
        {
            if (pt->countDown != 0U) --pt->countDown;
            if (pt->countDown == 0U)
            {
                pt->countDown = pt->repeat;
                if ((pt->flags & tmrHit) != 0U) pt->flags |= tmrOverflow;
                pt->flags |= tmrHit;
                if (pt->timerFunc != NULL) pt->timerFunc();
            }
        }
        pt = pt->next;
    }
    inTick = false;
}
#if LA_SYSTIMER_DEFINE_HANDLER
void SysTick_Handler(void)
{
    LA_SYSTIMER_ProcessTick();
}
#endif

/* RETURN: atomic aligned 32-bit Cortex-M uptime read; usable in any context. */
uint32_t LA_SYSTIMER_GetMilliseconds(void) { return milliseconds; }
/* FLASH callback: INPUT ignored board context. RETURN uptime in milliseconds. */
uint32_t LA_SYSTIMER_GetTimeMs(void *userData)
{
    (void)userData;
    return LA_SYSTIMER_GetMilliseconds();
}
/* INPUT callback/userData (NULL disables hook). OUTPUT installs an ISR hook.
 * Foreground only; hook must obey the same rules as timer callbacks.
 */
bool LA_SYSTIMER_SetTickCallback(LA_SYSTIMER_TickCallback_t callback, void *userData)
{
    uint32_t saved;
    if (!LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock();
    tickCallback = callback; tickUserData = userData;
    LA_SYSTIMER_Unlock(saved);
    return true;
}

/* INPUT persistent, unlisted object with repeat>=1. OUTPUT adds at list head,
 * loads countdown, clears old events, preserves Enabled. No duplicate links.
 */
bool timer_add(pTimerObj pt)
{
    uint32_t saved;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock();
    if (pt->repeat == 0U || timerCount >= LA_SYSTIMER_MAX_TIMERS || LA_SYSTIMER_Contains(pt))
    { LA_SYSTIMER_Unlock(saved); return false; }
    pt->next = timerBase;
    pt->countDown = pt->repeat;
    pt->flags &= ~(tmrHit | tmrOverflow);
    timerBase = pt;
    ++timerCount;
    LA_SYSTIMER_Unlock(saved);
    return true;
}
/* INPUT object. OUTPUT detaches it and clears next. RETURN whether found. */
bool timer_drop(pTimerObj pt)
{
    uint32_t saved;
    pTimerObj *link;
    bool found = false;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock();
    link = &timerBase;
    for (uint32_t i = 0U; *link != NULL && i < LA_SYSTIMER_MAX_TIMERS; ++i)
    {
        if (*link == pt)
        {
            *link = pt->next; pt->next = NULL; --timerCount; found = true; break;
        }
        link = &(*link)->next;
    }
    LA_SYSTIMER_Unlock(saved);
    return found;
}
/* INPUT object. RETURN membership, false for NULL or ISR context. */
bool timer_is_inlist(pTimerObj pt)
{
    uint32_t saved; bool found;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock(); found = LA_SYSTIMER_Contains(pt);
    LA_SYSTIMER_Unlock(saved); return found;
}
/* INPUT object (listed or not). OUTPUT reloads countdown, retains event flags. */
void timer_retrig(pTimerObj pt)
{
    uint32_t saved;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return;
    saved = LA_SYSTIMER_Lock(); pt->countDown = pt->repeat; LA_SYSTIMER_Unlock(saved);
}
/* INPUT object/new period >=1 ms. OUTPUT updates repeat and countdown.
 * Zero/NULL/ISR inputs are ignored. Existing event flags are retained.
 */
void timer_update_count(pTimerObj pt, uint32_t cnt)
{
    uint32_t saved;
    if (pt == NULL || cnt == 0U || !LA_SYSTIMER_Foreground()) return;
    saved = LA_SYSTIMER_Lock(); pt->repeat = cnt; pt->countDown = cnt;
    LA_SYSTIMER_Unlock(saved);
}
/* INPUT object. OUTPUT brings the next expiration to max(1,repeat/2) ticks. */
void timer_shifthalf_count(pTimerObj pt)
{
    uint32_t saved, half;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return;
    saved = LA_SYSTIMER_Lock(); half = pt->repeat / 2U;
    pt->countDown = half != 0U ? half : 1U; LA_SYSTIMER_Unlock(saved);
}
/* INPUT object/enable state. OUTPUT changes Enabled only, preserving countdown. */
bool timer_set_enabled(pTimerObj pt, bool enabled)
{
    uint32_t saved;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock();
    if (enabled) pt->flags |= tmrEnabled; else pt->flags &= ~tmrEnabled;
    LA_SYSTIMER_Unlock(saved); return true;
}
/* INPUT object, optional overflow output. OUTPUT consumes pending event flags.
 * RETURN true if Hit was pending. Overflow output is false for invalid calls.
 */
bool timer_take_hit(pTimerObj pt, bool *overflow)
{
    uint32_t saved, flags;
    if (overflow != NULL) *overflow = false;
    if (pt == NULL || !LA_SYSTIMER_Foreground()) return false;
    saved = LA_SYSTIMER_Lock(); flags = pt->flags;
    pt->flags &= ~(tmrHit | tmrOverflow);
    LA_SYSTIMER_Unlock(saved);
    if (overflow != NULL) *overflow = (flags & tmrOverflow) != 0U;
    return (flags & tmrHit) != 0U;
}
