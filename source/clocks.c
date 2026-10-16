/*
 * clock.c
 *
 *  Created on: 9. okt. 2026
 *      Author: pc
 */

/* ------------------------ I N C L U D E    F I L E S ------------------------ */
#include "clocks.h"
#include "fsl_clock.h"

#include <stdint.h>

/* ------------------- L O C A L     D E F I N I T I O N S  ------------------- */

/* ------------------ L O C A L           F U N C T I O N S   ---------------- */

/* ------------------------- G L O B A L     D A T A   ------------------------ */
clocks_t clocks;

/* -------------------------- L O C A L     D A T A   ------------------------- */


/* ------------------ E X T E R N A L      F U N C T I O N S   ---------------- */


/* ------------------ E X T E R N A L      D A T A ---------------------------- */


/* --------------------- L O C A L     F U N C T I O N S   -------------------- */

/* ----------------------  I M P L E M E N T A T I O N   ---------------------- */

void clocks_dump( void )
{
	clocks.core_frequency = CLOCK_GetFreq( kCLOCK_CoreSysClk );
	clocks.bus_frequency = CLOCK_GetFreq( kCLOCK_BusClk );
	clocks.flash_frequency = CLOCK_GetFreq( kCLOCK_FlashClk );
	clocks.osc32_frequency = CLOCK_GetFreq( kCLOCK_Er32kClk );
	clocks.osc0Er_frequency = CLOCK_GetFreq( kCLOCK_Osc0ErClk );
	clocks.fast_PeriphClk = CLOCK_GetFreq( kCLOCK_FastPeriphClk );
	clocks.pll_Fll_Sel = CLOCK_GetFreq( kCLOCK_PllFllSelClk );
	clocks.osc0Er_Undiv = CLOCK_GetFreq( kCLOCK_Osc0ErClkUndiv );
	clocks.mcg_Fixed_Freq = CLOCK_GetFreq( kCLOCK_McgFixedFreqClk );
	clocks.mcg_Internal_Ref = CLOCK_GetFreq( kCLOCK_McgInternalRefClk );
	clocks.mcg_Fll_frequency = CLOCK_GetFreq( kCLOCK_McgFllClk );
	clocks.mcg_Pll0Clk = CLOCK_GetFreq( kCLOCK_McgPll0Clk );
	clocks.mcg_Pll1Clk = CLOCK_GetFreq( kCLOCK_McgPll1Clk );
	clocks.mcg_ExtPllClk = CLOCK_GetFreq( kCLOCK_McgExtPllClk );
	clocks.mcg_PeriphClk = CLOCK_GetFreq( kCLOCK_McgPeriphClk );
	clocks.mcg_Irc48MClk = CLOCK_GetFreq( kCLOCK_McgIrc48MClk );


	clocks.lpo_frequency = CLOCK_GetFreq( kCLOCK_LpoClk );
}
