#ifndef LA_PIT_DRV_H_
#define LA_PIT_DRV_H_
#include "fsl_pit.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*LA_PIT_callback_t)(pit_chnl_t channel, void *userData);
typedef struct LA_PIT_period_t
{
    uint32_t requested_us;
    uint32_t ticks;
    uint32_t clock_Hz;
} LA_PIT_period_t;

/* Call at boot after clocks. Same setting is idempotent; a different global
 * debug setting returns Busy. Never calls SDK PIT_Init twice.
 * All four PIT IRQ handlers belong to this module; do not define duplicates. */
status_t LA_PIT_Init(bool runInDebug);
bool LA_PIT_IsInitialized(void);
/* Independent, unchained channels. Callback runs in IRQ context: keep it short.
 * Same callback/userData reservation is idempotent; another owner gets Busy.
 * Only the reserving client may configure/start/stop/release that channel.
 * Public operations are called from one main context; Stop is also ISR-safe. */
status_t LA_PIT_ReserveChannel(pit_chnl_t channel, LA_PIT_callback_t callback,
                               void *userData);
status_t LA_PIT_SetPeriod(pit_chnl_t channel, uint32_t period_us);
status_t LA_PIT_GetPeriod(pit_chnl_t channel, LA_PIT_period_t *period);
/* periodic=false stops the channel before delivering its one-shot callback. */
status_t LA_PIT_Start(pit_chnl_t channel, bool periodic);
status_t LA_PIT_Stop(pit_chnl_t channel);
status_t LA_PIT_ReleaseChannel(pit_chnl_t channel);
#endif
