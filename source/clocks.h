/*
 * clock.h
 *
 *  Created on: 9. okt. 2026
 *      Author: pc
 */

#ifndef CLOCKS_H_
#define CLOCKS_H_

#include <stdint.h>

/*!
 * @brief Error code definition for the clock manager APIs
 */
typedef enum _clock_manager_error_code {
    kClockManagerSuccess,                 /*!< Success */
    kClockManagerError,                   /*!< Some error occurs. */
    kClockManagerNoSuchClockName,         /*!< Invalid name */
    kClockManagerInvalidParam,            /*!< Invalid parameter */
    kClockManagerErrorOutOfRange,         /*!< Configuration index out of range.               */
    kClockManagerErrorNotificationBefore, /*!< Error occurs during send "BEFORE" notification. */
    kClockManagerErrorNotificationAfter,  /*!< Error occurs during send "AFTER" notification.  */
    kClockManagerErrorUnknown,            /*!< Unknown error.                                  */
} clock_manager_error_code_t;



typedef struct _clocks_t
{
	uint32_t core_frequency;
	uint32_t bus_frequency;
	uint32_t flash_frequency;
	uint32_t fast_PeriphClk;
	uint32_t pll_Fll_Sel;
	uint32_t osc32_frequency;
	uint32_t osc0Er_frequency;
	uint32_t osc1Er_frequency;
	uint32_t osc0Er_Undiv;

	uint32_t mcg_Fixed_Freq;
	uint32_t mcg_Internal_Ref;
	uint32_t mcg_Fll_frequency;

	uint32_t mcg_Pll0Clk;
	uint32_t mcg_Pll1Clk;
	uint32_t mcg_ExtPllClk;
	uint32_t mcg_PeriphClk;
	uint32_t mcg_Irc48MClk;

	uint32_t lpo_frequency;
} clocks_t;


void clocks_dump( void );
extern clocks_t clocks;

#endif /* CLOCKS_H_ */
