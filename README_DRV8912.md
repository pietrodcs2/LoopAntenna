# DRV8912-Q1 driver integration

Base: MK64F12_Project(1).zip, uploaded 9 October 2026.
Device: DRV8912QPWPRQ1 / DRV8912-Q1. Source: TI SLVSEC9C,
https://www.ti.com/lit/ds/symlink/drv8912-q1.pdf (sections 7.6, 8.5 and 8.6.1).

## Files

- source/LA_DRV8912_drv.h and .c: new multi-instance driver.
- source/LA_config.h: SPI settings, two CS selections, optional debug tests.
- source/SPIS_drv.c: active-low polarity for PCS0..PCS4; corrected include spelling.
- source/MK64F12_Project.c: optional debugger-controlled tests; corrected include spelling.
- ../validation/test_drv8912.c: host SPI simulation, outside MCUXpresso source folders.

Existing DDS and VNA driver behavior is unchanged. The source directory is already
registered in .cproject; refresh the project and perform Clean / Build so MCUXpresso
regenerates its makefiles. Do not reuse the old generated Debug makefiles directly.

## SPI and wiring

The new driver reuses SPIs_TransferBlocking / SPIs_TransferNonBlocking, including
bus ownership and reconfiguration on each transfer. No additional SDK DSPI handle
is created. Initialize pins and SPIs_Init(SPI0) once as in the existing main.

Defaults are independent chip selects, not a daisy chain:

| Device context | SPI | PCS | Existing board pin macro |
| --- | --- | --- | --- |
| 0 | SPI0 | PCS2 | SPI_CS2_PIN = PTC2 |
| 1 | SPI0 | PCS3 | SPI_CS3_PIN = PTC1 |

These are proposed assignments, not verified physical connections. Confirm EVM
external-control connections and isolation of the onboard controller before use.
The two devices have separate contexts and diagnostics. One context must correspond
to exactly one physical chip, and its configuration must not change while busy.

Mode 1: CPOL=0, CPHA=1, MSB first, 1 MHz. A message consists of two 8-bit SDK frames
with CS continuously asserted, giving exactly 16 clocks. CS is deasserted at the end.
Header bit 6 is READ=1 / WRITE=0, bit 7 stays zero, address uses bits 5..0.
RX byte 0 is the SPI summary status; RX byte 1 is register data. A WRITE returns the
previous register value, so high-level output setters perform a separate readback.
SPI summary bits [7:6] must be 11. The SPI summary is not the IC_STAT register.
Timing includes 3 us delays and an explicit 3 us recovery gap after each message.

## Basic use

```c
static DRV8912_device_t relay[DRV8912_DEVICE_COUNT];

DRV8912_InitContext(&relay[0], &DRV8912_DefaultConfigs[0]);
DRV8912_InitContext(&relay[1], &DRV8912_DefaultConfigs[1]);

status_t status = DRV8912_StartBlocking(&relay[0], coreClock_Hz, true);
if (status == kStatus_Success)
{
    status = DRV8912_SetOutputBlocking(&relay[0], 1U, eDRV8912_Output_High);
}
```

StartBlocking waits for readiness, checks IC_ID, records all defined registers,
sets all outputs Hi-Z, disables PWM and active open-load detection, configures
EXT_OVP and thermal warning reporting, clears faults and reads diagnostics.
It does not force a hardware cold reset. The snapshot contains holes for reserved
addresses; OLD_CTRL_4 is at 0x24, not 0x1C. No relay test pulses are generated.
Calling StartBlocking on an already active driver can change outputs to Hi-Z.

For a nominal 24 V relay supply, pass extOvp=true (default debug-test setting).
OCP reporting stays enabled and OCP deglitch stays at its 10 us default.
Active open-load detection is deliberately disabled for pulse/capacitive relay loads.
Its configuration can be changed using the register APIs after load characterization.
The startup log is available in relay[i].startupLog, including the final fault snapshot.
Register read/write verification does not prove that a physical output or relay moved.

## Output control

OUT1..OUT12 are numbered 1..12. Mask bit 0 corresponds to OUT1 and bit 11 to OUT12.

- SetOutputBlocking: High, Low or Hi-Z for one output, preserving the other outputs
  in the same control register.
- SetOutputMasksBlocking(highMask, lowMask): separate 12-bit masks. Bits in neither
  mask are Hi-Z. Overlapping masks and out-of-range bits are rejected.
- ApplyRelayMaskBlocking(mask): bits set drive high; all other 12 outputs drive low.
- AllHighImpedanceBlocking: both high-side and low-side switches disabled.

Each group of four outputs is updated in one SPI message; all 12 outputs and the
two boards are not updated simultaneously. Errors can leave a partially applied
configuration. Stop the application sequence and reacquire a verified state before
continuing after any failure. The driver does not infer physical relay state.

These APIs set electrical output states. They do not generate timed coil pulses,
provide an L/C mapping, or decide settling times. With capacitor-coupled latching
relay circuitry, transitions (including startup, Hi-Z and sleep transitions) can
actuate a relay. Test that circuitry separately before using output changes in tuning.

## Enable and sleep

Default configurations have setEnable=NULL: the board must hold ENABLE/nSLEEP high.
No GPIO assignment is guessed. If MCU control is needed, supply a setEnable callback
and userData in your own deviceConfig before InitContext. Configure the GPIO first.
The callback receives true for awake and false for sleep.

SleepBlocking requires that callback; otherwise it returns kStatus_InvalidArgument.
It attempts to set Hi-Z then asserts sleep even if SPI fails. Its returned status
reflects the SPI operation; the GPIO callback has no independent verification.
Wake and reconfigure by calling StartBlocking again. SPI is unavailable during sleep.

## Nonblocking register access

```c
status_t submitted = DRV8912_ReadRegisterNonBlocking(&relay[0], eDRV8912_IC_STAT);
/* In the foreground loop, after successful submission: */
status_t completed;
DRV8912_response_t response;
if (DRV8912_GetTransferResult(&relay[0], &completed, &response))
{
    /* response is valid only when completed == kStatus_Success. */
}
```

The context owns persistent TX/RX storage. It remains busy until the completion is
consumed, preventing another call from overwriting a pending result. Other physical
devices can use the bus once the shared SPI completion callback releases it.
The interrupt callback performs only decode, result publication and a 3 us gap.
Do not call the public APIs from IRQ context. Do not reinitialize or free the context
while pending. A failed submission does not generate a completion to poll.

High-level initialization and output setters are blocking. The existing shared SPI
layer has no timeout/abort API; an interrupt transfer whose callback never arrives
remains pending. Do not clear busy manually or release a hardware-active bus. A
system-level timeout/abort mechanism is still required if bounded completion is needed.

## Debugger-controlled hardware tests

DRV8912_ENABLE_DEBUG_TESTS defaults to 0. Enable only after verifying wiring and power.
Then inspect relayDevice[0..1], relayTestStatus[0..1] and the startup logs.

1. Set relayStartTest=true to initialize and test both devices, leaving outputs Hi-Z.
2. Set relayOutputMask[0..1], then relayApplyMaskTest=true to apply output levels.
3. Set relayHiZTest=true to disable all high-side and low-side switches.

A mask is not applied unless that device passed StartBlocking. All trigger variables
are initially false. This does not automatically exercise all relays on boot.

## Validation performed

- GCC syntax checks of new driver, shared SPI file and main against supplied NXP
  headers, both with and without optional relay tests.
- Host SPI simulation with AddressSanitizer and UndefinedBehaviorSanitizer:
  read/write command encoding, previous-value write response, all 12 output indices,
  mask mapping, two-device separation, startup configuration and readback, invalid
  register/mask rejection, diagnostic reads, missing-MISO detection, transport failure,
  busy handling, asynchronous completion ownership, device-ID rejection and sleep hook.

The ARM compiler/MCUXpresso installation is not available in this environment.
No ARM link or physical EVM validation has been performed.

To reproduce the host tests from the archive root:

```sh
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    validation/test_drv8912.c -IMK64F12_Project/drivers -o test_drv8912
ASAN_OPTIONS=detect_leaks=0 ./test_drv8912
```
