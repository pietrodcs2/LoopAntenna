/*
 * LA_ADL5961_drv.h
 *
 *  Created on: 9. okt. 2026
 *      Author: pc
 */

#ifndef LA_ADL5961_DRV_H_
#define LA_ADL5961_DRV_H_

#include <fsl_common.h>
#include <stdbool.h>
#include <stdint.h>

/* IF amplifier gain: shared by FGAIN and RGAIN */
typedef enum eGain_t
{
    eGain_0dB  =  0,
    eGain_6dB  =  6,
    eGain_12dB = 12,
    eGain_18dB = 18,
    eGain_24dB = 24,
    eGain_30dB = 30,
    eGain_36dB = 36,
    eGain_42dB = 42,
    eGain_48dB = 48,
    eGain_54dB = 54,
    eGain_60dB = 60,
    eGain_66dB = 66
} eGain_t;

/* BYPASS */
typedef enum eBypass_t
{
    eBypass_Disabled = 0,
    eBypass_Enabled  = 1
} eBypass_t;

/* LOMODE */
typedef enum eLoMode_t
{
    eLoMode_DivideBy2   = 0,
    eLoMode_MultiplyBy1 = 1,
    eLoMode_MultiplyBy2 = 2,
    eLoMode_MultiplyBy4 = 3
} eLoMode_t;

/* OFMODE */
typedef enum eOfMode_t
{
    eOfMode_DivideBy1    = 0,
    eOfMode_DivideBy2    = 1,
    eOfMode_DivideBy4    = 2,
    eOfMode_Disconnected = 3
} eOfMode_t;

/* CT2: recommended passbands expressed as 2 * fLO */
typedef enum eCt2_t
{
    eCt2_Band15To20GHz =  0,
    eCt2_Band14To15GHz =  1,
    eCt2_Band11To14GHz =  3,
    eCt2_Band10To12GHz =  4,
    eCt2_Band9To10GHz  =  6,
    eCt2_Band8To9GHz   =  8,
    eCt2_Band7To8GHz   = 12,
    eCt2_Band6To7GHz   = 15,
    eCt2_Band5To6GHz   = 22,
    eCt2_Band4To5GHz   = 31
} eCt2_t;

/* CT4: filter tuning code, used together with CT2 */
typedef enum eCt4_t
{
    eCt4_Setting0  =  0,
    eCt4_Setting1  =  1,
    eCt4_Setting2  =  2,
    eCt4_Setting3  =  3,
    eCt4_Setting4  =  4,
    eCt4_Setting5  =  5,
    eCt4_Setting6  =  6,
    eCt4_Setting7  =  7,
    eCt4_Setting8  =  8,
    eCt4_Setting9  =  9,
    eCt4_Setting10 = 10,
    eCt4_Setting11 = 11,
    eCt4_Setting12 = 12,
    eCt4_Setting13 = 13,
    eCt4_Setting14 = 14,
    eCt4_Setting15 = 15
} eCt4_t;

/* CIF1: nominal -3 dB bandwidth with CIF2 = 0 */
typedef enum eCif1_t
{
    eCif1_Bandwidth126MHz  =  0,
    eCif1_Bandwidth64MHz   =  1,
    eCif1_Bandwidth41MHz   =  2,
    eCif1_Bandwidth30_1MHz =  3,
    eCif1_Bandwidth24_1MHz =  4,
    eCif1_Bandwidth19_7MHz =  5,
    eCif1_Bandwidth16_7MHz =  6,
    eCif1_Bandwidth14_5MHz =  7,
    eCif1_Bandwidth13MHz   =  8,
    eCif1_Bandwidth11_6MHz =  9,
    eCif1_Bandwidth10_5MHz = 10,
    eCif1_Bandwidth9_5MHz  = 11,
    eCif1_Bandwidth8_8MHz  = 12,
    eCif1_Bandwidth8_1MHz  = 13,
    eCif1_Bandwidth7_6MHz  = 14,
    eCif1_Bandwidth7MHz    = 15
} eCif1_t;

/* CIF2: nominal -3 dB bandwidth with CIF1 = 0 */
typedef enum eCif2_t
{
    eCif2_Bandwidth126MHz  =  0,
    eCif2_Bandwidth11_1MHz =  1,
    eCif2_Bandwidth5_9MHz  =  2,
    eCif2_Bandwidth3_8MHz  =  3,
    eCif2_Bandwidth3_1MHz  =  4,
    eCif2_Bandwidth2_3MHz  =  5,
    eCif2_Bandwidth2MHz    =  6,
    eCif2_Bandwidth1_6MHz  =  7,
    eCif2_Bandwidth1_5MHz  =  8,
    eCif2_Bandwidth1_3MHz  =  9,
    eCif2_Bandwidth1_2MHz  = 10,
    eCif2_Bandwidth1MHz    = 11,
    eCif2_Bandwidth990kHz  = 12,
    eCif2_Bandwidth860kHz  = 13,
    eCif2_Bandwidth800kHz  = 14,
    eCif2_Bandwidth700kHz  = 15
} eCif2_t;


status_t ADL5961_ColdStartBlocking(uint32_t coreClock_Hz, bool powerDownAtEnd);
// const ADL5961_startupLog_t *ADL5961_GetStartupLog(void);
// status_t ADL5961_ReadRegistersBlocking(ADL5961_regs_t *regs);
status_t ADL5961_ShutBlocking(void);
status_t ADL5961_SetForwardGainBlocking(eGain_t gain);
status_t ADL5961_SetReverseGainBlocking(eGain_t gain);
status_t ADL5961_WakeUpBlocking(void);

status_t ADL5961_SetLoConfigBlocking( eBypass_t bypass, eLoMode_t loMode, eOfMode_t ofMode);
status_t ADL5961_SetCt2Blocking(eCt2_t setting);
status_t ADL5961_SetCt4Blocking(eCt4_t setting);
status_t ADL5961_SetIfFiltersBlocking( eCif1_t cif1, eCif2_t cif2 );
status_t ADL5961_ReadTemperatureCodeBlocking(uint8_t * const code);
status_t ADL5961_SetForwardGainNonBlocking(eGain_t gain);
status_t ADL5961_SetReverseGainNonBlocking(eGain_t gain);
status_t ADL5961_GetTransferResult(uint8_t *data);

#endif /* LA_ADL5961_DRV_H_ */
