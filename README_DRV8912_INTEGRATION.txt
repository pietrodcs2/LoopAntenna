DRV8912 integration repair - 2026-10-09

This archive contains the complete MCUXpresso project, including source/.
Generated Debug/Release build outputs are omitted; MCUXpresso regenerates them.

Changes:
- LA_config.h: SPI0, 1 MHz, 1000 ns delays, CTAR0, independent PCS2/PCS3.
- SPIS_drv.c: PCS0..PCS4 configured active low.
- MK64F12_Project.c: persistent contexts for two DRV8912 devices;
  InitContext and StartBlocking called once after shared SPI setup.
- Source include spelling standardized to the existing SPIS_drv.h filename.

Chip-select mapping (existing board.h):
ADL5961: PCS0 / PTC4
AD9959:  PCS1 / PTC3
DRV8912 device 0: PCS2 / PTC2
DRV8912 device 1: PCS3 / PTC1

DRV8912 enable pins must be held high externally with the default NULL hook.
StartBlocking uses extOvp=true for nominal 24 V supplies.
Startup leaves all 12 outputs of each driver in high impedance.
No relay activation or output test pulses are added.

Debugger:
DRV8912_InitContext_stat[0..1]
DRV8912_Start_stat[0..1]
DRV8912_devices[0..1].startupLog

Installation:
1. Keep a backup of the current project directory.
2. Close the project in MCUXpresso and extract this archive into its parent
   directory, replacing the existing project files.
3. Reopen/refresh the project, run Project > Clean, then Build Project.
   Alternatively import the extracted folder as an existing project into
   a workspace that does not already contain this project name.

Validation performed here:
GCC syntax check for every source/*.c file using project SDK headers and
configuration defines: zero errors. The host compiler is not an ARM compiler;
no MCUXpresso/Redlib link or hardware test was performed.
Existing unused-function and debugger-variable warnings are outside this fix.

Main-loop debugger tests (all request flags default to false):
- drvDeviceIndex = 0 or 1 selects the physical device.
- testDrvStart = true retries startup, leaving outputs Hi-Z.
- drvRelayMask = 0x001; testDrvApplyMask = true drives OUT1 HIGH,
  OUT2..OUT12 LOW. Valid masks: 0x000..0xFFF.
- testDrvHighImpedance = true returns all outputs to Hi-Z.
- testDrvReadFaults = true refreshes drvFaults[drvDeviceIndex].
- drvTestStatus contains the last test result.
Requests execute once and reset, except on SPI bus contention.
Set one request at a time. These tests hold static output levels;
they do not generate software-timed relay pulses.
