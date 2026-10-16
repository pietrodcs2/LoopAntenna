#ifndef LA_SYSTIMER_DRV_H_
#define LA_SYSTIMER_DRV_H_
#include <stdint.h>
#include <stdbool.h>

#ifndef LA_SYSTIMER_DEFINE_HANDLER
#define LA_SYSTIMER_DEFINE_HANDLER 1
#endif
#ifndef LA_SYSTIMER_MAX_TIMERS
#define LA_SYSTIMER_MAX_TIMERS 32U
#endif
#define LA_SYSTIMER_TICK_HZ 1000U
#define tmrEnabled (UINT32_C(1) << 0)
#define tmrHit (UINT32_C(1) << 1)
#define tmrOverflow (UINT32_C(1) << 2)

typedef void (*TimerFunc_t)(void);
typedef struct TimerObj
{
    struct TimerObj *next;
    volatile uint32_t countDown;
    uint32_t repeat;
    volatile uint32_t flags;
    TimerFunc_t timerFunc;
} TimerObj;
typedef TimerObj *pTimerObj;
typedef void (*LA_SYSTIMER_TickCallback_t)(void *userData);

#ifdef __cplusplus
extern "C" {
#endif
bool LA_SYSTIMER_Init(void);
bool LA_SYSTIMER_Start(void);
void LA_SYSTIMER_Stop(void);
void LA_SYSTIMER_ProcessTick(void);
uint32_t LA_SYSTIMER_GetMilliseconds(void);
uint32_t LA_SYSTIMER_GetTimeMs(void *userData);
bool LA_SYSTIMER_SetTickCallback(LA_SYSTIMER_TickCallback_t callback, void *userData);
bool timer_add(pTimerObj pt);
bool timer_drop(pTimerObj pt);
bool timer_is_inlist(pTimerObj pt);
void timer_retrig(pTimerObj pt);
void timer_update_count(pTimerObj pt, uint32_t cnt);
void timer_shifthalf_count(pTimerObj pt);
bool timer_set_enabled(pTimerObj pt, bool enabled);
bool timer_take_hit(pTimerObj pt, bool *overflow);
#ifdef __cplusplus
}
#endif
#endif
