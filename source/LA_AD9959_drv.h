/*
 * LA_AD9959_drv.h
 *
 *  Created on: 8. okt. 2026
 *      Author: pc
 */

#ifndef LA_AD9959_DRV_H_
#define LA_AD9959_DRV_H_

#include "fsl_common.h"
#include <stdint.h>



typedef enum eAD9959_channel_t
{
    eAD9959_Channel0 = 0,
    eAD9959_Channel1,
    eAD9959_Channel2,
    eAD9959_Channel3,
    eAD9959_ChannelCount

} eAD9959_channel_t;


void AD9959_IoUpdate(uint32_t coreClock_Hz);
status_t AD9959_PowerUpAutotestBlocking(void);
status_t AD9959_PowerUpLoBlocking(void);
status_t AD9959_PowerDownBlocking(void);

status_t AD9959_ColdStartBlocking(uint32_t coreClock_Hz, bool powerDownAtEnd);
status_t AD9959_StartLoBlocking(uint32_t coreClock_Hz, uint32_t loFrequency_Hz);
status_t AD9959_StartAutotestBlocking(uint32_t coreClock_Hz, uint32_t loFrequency_Hz, uint32_t rfFrequency_Hz );

status_t AD9959_SetFrequencySelected( uint32_t frequency_Hz, bool blocking );
status_t AD9959_SetFrequencyBlocking( eAD9959_channel_t channel, uint32_t frequency_Hz );
bool AD9959_GetTransferResult(status_t *status);

#endif /* LA_AD9959_DRV_H_ */
