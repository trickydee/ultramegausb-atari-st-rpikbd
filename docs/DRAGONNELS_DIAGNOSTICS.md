# Dragonnels IKBD diagnostics

Branch: `feature/dragonnels-ikbd-diagnostics`. Current firmware: `23.1.0-dbg13`.

## Change summary

- Fixed SCI receive acknowledgement so a byte arriving between status and data
  reads stays pending; repeated identical bytes are preserved.
- Preserved Atari RX ordering by draining older deferred bytes before delivering
  newly received data.
- Replaced emulator fault-print flooding with retained snapshots and added
  reset, RAM-execution and receive-history diagnostics.
- Added `IKBD_DEBUG_LEVEL`: 0 disables added IKBD output, 1 (default) prints
  summaries, 2 includes snapshot dumps, and 3 enables continuous wire traces.
  For example: `IKBD_DEBUG_LEVEL=3 BUILD_VARIANT=debug CLEAN_BUILD_DIRS=0 ./build-all.sh`.
  Existing Bluetooth/heartbeat logging remains separate; levels 2/3 affect timing.
- Dragonnels waits for F1 after a CPU RESET when selecting a demo. Ctrl+F11
  resets the emulated IKBD and releases this wait in user tests. Automatic
  hardware/break reset handling is not implemented; original-keyboard comparison
  and broader compatibility testing remain outstanding.

Validation: Pico 2 W dbg13 level-1 build and SCI receive regression tests pass.
The level-1 binary contains no `[WIRE]` message strings. User confirmed dbg12
has clean output and is continuing hardware tests; dbg13 adds configurable levels.
No UART1 baud, Core 1 loop structure or emulator cycle-budget changes.

## Initial investigation (dbg2)

The supplied `22.2.0-dbg8` capture shows repeated writes to address `0x10000`,
execution sampled at PC `0x00A7`, and receive-queue overflow. Word accesses in
`6301/memory.h` do not wrap their second byte at `0xFFFF`; this is a candidate,
not yet a confirmed explanation of the demo failure.

This build replaces invalid-address printf calls from the emulator with a
first-fault snapshot. It records read/write, address/value, instruction-start PC,
opcode, three raw code bytes, current PC, X/SP/D/CCR/TRCSR and call-stack context.
Opcode `100` means interrupt entry or opcode fetch before an opcode was recorded.
Raw code bytes are only read from RAM/ROM and never through I/O callbacks.
Core 0 prints the snapshot once, followed by a total count at most every five
seconds. Release/acquire publication keeps the first snapshot immutable and
complete for the reader. It survives an emulated reset; restart the adapter for
a fresh first-fault capture. Instrumentation adds per-instruction bookkeeping,
so this is diagnostic firmware, not a timing reference.

Address handling, Core 1 loop structure, UART1 baud/FIFO, and instruction cycle
counts are unchanged. No memory-wrap fix is applied. Existing unrelated local
changes are retained. Build via `build-all.sh` using the dependency overrides
in `BLUEPAD32_NATIVE_RECONNECT_TEST.md`, with `BUILD_VARIANT=debug`.

Test: start a fresh UART0 capture at 115200 8N1, restart the adapter and reload
Dragonnels from a fresh start. Enter the same mouse-controlled menu, move the
mouse, and capture the first `IKBD first memory fault` line plus the following
heartbeats and fault counts. Record whether menu movement changes when the
repeated Core 1 prints are removed. If no fault is reported, return the full
capture and behavior rather than assuming the underlying fault is fixed.

## Expanded capture: 23.1.0-dbg3

The dbg2 first fault was at instruction PC 0x0100 (unmapped in this emulator),
opcode FF, X=FFFF, SP=00A5. The resulting STX FFFF explains the subsequent
0x10000 write but does not explain why control left RAM. The received log
confirmed Bluetooth ERGO/MX devices and continuing mouse movement consumption.

Dbg3 freezes the last 16 pre-execution instruction records from 0x0080 through
0x0100 inclusive, plus all 128 bytes of controller RAM, at the first invalid
access. This is a filtered RAM trace, not a continuous trace through ROM or
interrupt entry. Register values are before instruction execution.

Core 0 separately retains the latest 512 received bytes and 512 SCI-injection
bytes with sequence numbers. Recording stops when the first-fault publication
is observed; these histories are not hardware arrival timestamps or proof of
ROM consumption, and publication can race one in-progress Core 0 operation.
RX means bytes removed from the IRQ ring by the main loop. Comparing RX and
DELIVER can expose software-queue reordering but cannot detect bytes lost before
that ring. No queue-order, wrapping, or emulated timing fix is included.

The expanded dump emits at most one line per 20 ms from Core 0 and ends with
`[DIAG] IKBD capture END`. Keep capture running until that marker and subsequent
heartbeats appear. Instrumentation and console output still affect timing;
interpret the initial frozen capture separately from later symptoms.

Pico 2 W debug compile and host snapshot checks passed, including RAM capture,
wrapped instruction-history ordering, first-fault retention and counter saturation.

## CPU receive-register reads: 23.1.0-dbg4

The paired success/failure dbg3 log showed functioning custom RAM execution on
the successful run. The failed run captured 109 identical RX and DELIVER bytes.
Reconstructing the reverse-stack upload from the loader at A0 suggests a one-byte
shift in the executable body: the final BRA is at ED rather than EC, and its
operand at EE is cleared by the preceding store to EE. This is consistent with
an omitted byte during loading; the precise cause remains unconfirmed.

Dbg4 adds a Core 1-owned ring of the last 512 values actually returned from RDR
reads, frozen with the first fault and printed as READ after RX/DELIVER. These
include reads when RDRF was clear, so READ count alone is not a lost-byte count.
The existing read/clear ordering is intentionally retained for this diagnostic;
no receive-race fix is included. Capture through the END marker again.

## Diagnostic stack reduction: 23.1.0-dbg5

The paired startup capture shows dbg4 freezing Core 1 with RUN_CLOCKS,
run_in one ahead of run_out, and pause_depth=0, while Core 0 and Bluetooth
continue. The dbg3 startup continues advancing emulator cycles. The reported
PC is the last batch-entry PC, not necessarily the instruction that stalled.

Dbg4's expanded automatic snapshot increased stack pressure in main(). Dbg5
copies the immutable first-fault snapshot directly into the existing static
dump buffer. ELF disassembly confirms main's frame falls from 1,216 bytes
(including saved registers) to 320 bytes against a configured 2,048-byte
Core 0 stack. Stack corruption is a hypothesis, not confirmed by the capture.
No UART configuration or emulator scheduling change is included.

Pico 2 W debug compilation, host snapshot checks, and diff whitespace checks
passed. Hardware validation remains required: restart with dbg5, confirm
desktop mouse/keyboard operation and at least two advancing heartbeats, then
retry Dragonnels and retain the complete diagnostic capture.

## Preserve receive history during polling: 23.1.0-dbg6

User confirmed dbg5 restores normal input. The failed menu capture has 113
identical RX/DELIVER bytes, continuing mouse report consumption, and a first
fault at PC 00FF after BRA at 00ED falls through to 00EF. Queue-full warnings
follow the first fault. This still points to corrupted custom code execution;
it does not establish which receive operation caused it.

Dbg5 recorded 62,864 RDR reads; the retained last 512 are all zero. Empty
polling reads displaced the upload history. Dbg6 records the returned value
only when the existing RDRF test succeeds in a running CPU. It preserves the
existing clear-before-read behavior and the static snapshot storage. READ now
means reads observing RDRF set, not all RDR accesses; it is not an atomic
cross-core transaction and does not itself resolve the suspected race.

Host checks exercise full-register capture, retention across 70,000 empty
reads, non-running CPU reads, and first-fault freeze behavior. Repeat the
same menu test and capture through the END marker and following heartbeat.

## Blocked receive trigger: 23.1.0-dbg7

Dbg6's failed menu run continued executing at sampled PCs 00B2/00BA with
mouse reports arriving, but SCI stayed busy and the queue filled. No invalid
memory access occurred, so the fault-only capture did not trigger.

Dbg7 requests a one-shot capture when Core 0 observes SCI busy and at least
8 queued bytes continuously for 1 second. Core 0 sends an atomic request;
Core 1 takes the existing immutable snapshot at the next opcode dispatch.
The label is `blocked RX snapshot`; write-kind HD6301_DIAG_RX_BLOCKED (2)
distinguishes this diagnostic event from read/write faults and does not
increment the memory-fault counter. A previously published fault wins.
Execution continues; no Core 1 pause or UART configuration change is made.
RX/DELIVER histories stop when Core 0 observes publication, so their end may
include an in-flight operation. The trigger detects backlog, not its cause.

Pico 2 W debug compilation and host snapshot tests passed, including blocked
capture publication, zero initial fault count, retained history, and subsequent
fault counting without overwriting the snapshot. Main's compiled frame remains
320 bytes including saved registers. Hardware validation remains pending.

Flash dbg7, restart the adapter before launching Dragonnels, enter the menu,
and move the mouse for several seconds. Leave the failed menu running without
resetting the adapter. Capture through `IKBD capture END` plus the next heartbeat;
if no dump appears, return 30 seconds of output and the observed behavior.

## Upload and lifecycle capture: 23.1.0-dbg8

A clean-boot failure showed only sampled ROM PCs, arriving mouse reports, and
empty receive queues. Dbg8 adds a timed capture one second after observing
Dragonnels' raw RX signatures `20 00 A0 0E` or `22 00 A0`. These are candidate
upload/execute markers, not a complete IKBD command parser or proof the CPU
consumed a command. RX sequence positions are logged. The existing first-fault
or blocked-reception snapshot can win first; only one snapshot is retained.

A heartbeat lifecycle line reports calls to hd6301_reset (including startup),
the last pre-reset PC, entries into 0080..00FF, and direct RAM-to-ROM opcode
transitions. Counts continue after capture; RX/DELIVER totals freeze with the
histories. ROM-handled reset commands need not call hd6301_reset: raw receive
history and execution transitions must also be inspected. Individual counters
are atomic observations, not a simultaneous state snapshot. No printf runs
on Core 1. UART configuration and Core 1 loop structure remain unchanged.

Build and host tests cover upload-state snapshots without false fault counts,
first-snapshot retention, and raw-signature detection/sequence positions.
Test from a clean machine boot with OLED connected, load the demo, and capture
through END plus two heartbeats. Report whether the menu responds. The timed
capture should happen in both successful and failed runs.

## Receive acknowledgement experiment: 23.1.0-dbg9

Dbg8's failed capture has identical 154-byte RX/DELIVER/READ streams but RAM
matches omission of one 54 byte from RX positions 80..83, apart from runtime
writes to EF and D1. The loader can read an empty TRCSR, then read a newly
arrived byte from RDR and clear it; BPL uses the earlier empty status and skips
storing it. A controlled host interleaving reproduces this with the old handler.

Dbg9 latches RDRF/ORFE observed by CPU status reads and only acknowledges those
flags on a subsequent data read. RDR data is copied before clearing flags, with
a release fence before making the register available for refill. Empty-status
reads cannot acknowledge a later arrival. The latch resets with the emulator;
debugger reads and the internal TDR helper do not arm it. READ diagnostics now
record acknowledged reads, excluding unacknowledged peeks at a pending byte.

This is a focused receive-semantics experiment, not a general repair of all
shared-register concurrency or queue-order issues. The UART configuration,
polling intervals and Core 1 loop structure remain unchanged. Pico 2 W debug
build and tests/test_sci_receive.py pass; the host test covers the late-arrival
window, four repeated 54 bytes, data-only reads, reset, ORFE and debugger reads.
Hardware validation is pending: repeat clean boots with OLED connected and
retain captures from both responsive and unresponsive menu attempts.

Reference: Hitachi HD6301/HD6303 Series Handbook, SCI status/read sequence:
https://www.jaapsch.net/psion/pdffiles/hd6301-3_handbook.pdf

## Click/selection traffic: 23.1.0-dbg10

Dbg9 restored menu navigation in five runs, but selecting an entry hangs until
adapter replug. Core 1 continues running custom RAM code. Earlier RX/DELIVER
histories froze at upload and contain no post-click traffic; TX was not captured.

Dbg10 adds independent 512-entry SPSC diagnostic rings for UART RX IRQ reads
and Core 1 writes to UART TX. Core 0 drains at most 32 events per direction per
20 ms and logs sampled mouse-button state changes. Records continue after the
upload snapshot. No printing occurs in the IRQ or emulator TX producer.
`[WIRE] RX/TX us=<decimal base>: <hex byte>+<hex microsecond offset> ...`
RX timestamps mark IRQ reads, TX timestamps mark hardware-register writes,
not physical start/stop bits or confirmation of Atari receipt. Each boot resets
the time origin; the 32-bit timer wraps after about 71 minutes. Directions drain
separately: use timestamps, not line order, to correlate events. BUTTON records
are Core 0 observations of cached mouse state, not raw HID event timestamps.

`TRACE_DROPS` counts diagnostic ring losses only. A full trace ring drops its
new record without suppressing the actual serial byte. Console output changes
timing and sustained traffic may exceed UART0 logging capacity. Missing trace
records must not be interpreted as missing wire traffic. Actual UART RX buffer
overflow behavior is unchanged. The existing unused C++ send method is not
instrumented; the emulator transmits through serial_send.

Pico 2 W debug build, trace-ring capacity/wrap/overflow/repeated-byte tests,
and SCI receive regression tests pass. Hardware validation is pending.
Capture from cold boot, navigate, click/release once, wait at least 10 seconds,
then replug only the adapter if stuck. Keep the same terminal capture running
through reconnection and demo loading, then another 10 seconds. State whether
loading began upon unplug, upon replug, or after the firmware startup delay.

## Queue-order repair and trace deferral: 23.1.0-dbg11

Dbg10 failed before RAM execution: RX has `11 2A FC 37`, while DELIVER and
READ have `11 37 2A FC`. A fresh UART byte bypassed older deferred bytes when
SCI became ready after the initial queue drain. The log also reports actual
queue overflow, with 154 captured RX bytes versus 134 delivered/read bytes.
The extra console logging changes timing and can exacerbate this path.

Dbg11 drains pending bytes again before handling each fresh UART byte and
permits direct injection only when no older queued bytes remain. The queue
size and baud are unchanged. Wire printing is deferred until 200 ms after
the last RX inter-byte gap below 5 ms, retaining events in RAM during the
upload. This affects only wire printing, not other diagnostic output. Button
sampling/printing is also deferred during that interval, so brief transitions
during the upload can be missed; test clicks after the menu has settled.

The capture also establishes that this firmware writes F1 at startup and
again after receiving 80 01. It does not yet prove F1 releases the selection
hang, because this run never reached working navigation.

Pico 2 W debug build, SCI regression tests, and a controlled busy-to-ready
queue test pass. The latter forces readiness to change after the initial
drain with 2A/FC queued and 37 arriving; delivery remains 2A FC 37.
Hardware validation remains pending. Repeat the navigation/click/replug test
with the same OLED and devices; retain the full UART capture.

## Reduced console output: 23.1.0-dbg12

Two dbg11 hardware runs recovered from the selection black screen using
Ctrl+F11: reset count increased to 2 and UART TX emitted F1. Decrypted menu
code executes RESET at $1238, clears the palette, then waits for F1 at
$1262-$1272. This confirms the manual workaround; the external keyboard's
reset mechanism and any serial break remain unverified. Navigation failures
remain a separate intermittent issue.

Dbg12 defaults ENABLE_IKBD_VERBOSE_TRACE to 0, removing continuous wire
recording/printing and RAM/instruction/byte-history dump output. Compact
snapshot reports, the 10-second heartbeat and lifecycle counts remain;
unchanged memory-fault totals no longer print every five seconds. SCI and
queue-order fixes are retained. This isolates the effect of verbose logging;
it does not implement automatic reset or break detection. Test repeated
cold boots for navigation and use Ctrl+F11 if selection waits on black.

Validation: Pico 2 W debug build and SCI receive regression test passed.
Hardware validation of dbg12 remains pending.

## Explicit output levels: 23.1.0-dbg13

IKBD_DEBUG_LEVEL replaces ENABLE_IKBD_VERBOSE_TRACE. Set it through
build-all.sh's environment or CMake cache: 0 disables the added IKBD capture
and lifecycle output, 1 (default) retains compact summaries, 2 adds RAM,
instruction and RX/DELIVER/READ snapshot dumps, and 3 adds continuous wire
RX/TX and button traces. ENABLE_SERIAL_LOGGING must also be enabled.
Level 0 does not remove in-memory instrumentation or existing BT/heartbeat
logs. Production remains controlled by ENABLE_SERIAL_LOGGING=0.
Startup explicitly prints the selected level. No serial or reset behavior
changes. Latest user capture still identifies dbg11; it is not evidence of
wire output from dbg12. For general compatibility testing use dbg13 level 1.

## Hardware validation update — 7 October 2026

User reports Froggies, Pandemonium and Carrier Command working on the Mega ST.
Dragonnels also works with Ctrl+F11 after selection; the apparent reload
failure was traced to accidentally toggling mouse input off with Ctrl+F12.
These are user hardware observations, not exhaustive compatibility tests.
