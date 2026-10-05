# Native bonded BLE reconnect: Atari Mega / TT IKBD

## Startup flash fix

Atari initializes CYW43/Bluetooth before launching Core 1. The SDK's
`flash_safe_execute()` otherwise refuses early TLV erase/program requests
because the other core has not registered its lockout handler. The flash-bank
backend ignores that error, allowing TLV to report success for unreadable bonds.

`src/flash_boot_guard.c` wraps `flash_safe_execute()` for wireless builds.
During single-core boot, Core 0 executes the callback with interrupts disabled.
Immediately before the sole Core 1 launch, `main()` permanently closes this
boot path. All subsequent calls use the SDK's normal multicore lockout and
return its result. Bluetooth reinitialization does not reopen the boot path.
The emulator loop, launch position, UART baud and input timing are unchanged.

Temporary bond/flash readback diagnostics used to establish the failure and
verify the fix have been removed. Existing project diagnostics remain.

## Reconnect integration and dependencies

`BLUEPAD32_SOURCE_DIR` optionally selects an external Bluepad32 checkout.
Without it, the build uses the pinned submodule, which does not include the
native reconnect changes tested here. The tested shared checkout is:

- `/Users/rich/Documents/Code/Pico/bluepad32`
- Branch `codex/bonded-ble-reconnect`
- Commit `28ac7526840f32c7ec8cb63bff8c6281d49ad498`

That stack checks bonded address/type before accepting directed advertisements
and classifies HID devices after discovery. The adapter callback already accepts
empty-name/CoD-0 discovery without storing an empty pending name. A C-only
preinclude bridges newer HIDS host API spellings to the SDK's client API.
The shared checkout and adapter submodule pointers were not changed.

The local SDK's BTstack has pre-existing uncommitted edits disabling LE bond
storage. Those edits remain untouched and MUST be bypassed for these builds.
Use a clean archive of the same BTstack revision with the supported override:

```sh
mkdir -p /tmp/atari-reconnect-btstack
git -C pico-sdk/lib/btstack archive 501e6d2b86e6c92bfb9c390bcf55709938e25ac1 |
    tar -x -C /tmp/atari-reconnect-btstack
PICO_BTSTACK_PATH=/tmp/atari-reconnect-btstack \
BLUEPAD32_SOURCE_DIR=/Users/rich/Documents/Code/Pico/bluepad32 \
BUILD_BOARDS=pico2_w BUILD_VARIANT=debug SKIP_VARIANTS=1 CLEAN_BUILD_DIRS=0 \
./build-all.sh
```

Use `BUILD_VARIANT=production` for the production build. All builds use
`build-all.sh`; its Pico 2 W path now stops on configuration/compilation failure
instead of copying a stale artifact. A reused CMake tree must select the clean
BTstack override; verify `PICO_BTSTACK_PATH` in its cache.

## Hardware evidence (2026-10-05)

MX Keys Mini and ERGO M575 for Business initially paired and sent input, but
neither reconnected after device off/on. Instrumented firmware showed failed
startup flash operations, failed bond readback and two cached entries with
zero readable bonds. Core 1 continued running and scanning resumed.

The startup fix was tested as `22.2.0-native-reconnect-test-dbg3`, UF2 SHA256:
`92a21aa962035731a607cec940215d786e9ee09d4e897b325e50c6dec477130a`.
The user reported success, and UART confirms:

- Startup bank erase/header creation succeeded.
- Identity and encryption records read back correctly, with two readable bonds
  and 16-byte encryption keys; no flash write mismatches.
- Both devices reconnected using stored bonds and completed re-encryption
  without another pairing sequence, then resumed HID reports.
- Core 1 cycles continued throughout.

The cleanup build uses the normal version labels (`22.2.0` production,
`22.2.0-dbg4` debug). It removes only temporary instrumentation/test labels;
hardware success above refers to dbg3, not a new hardware test of cleanup.

## Remaining checks and separate observations

Capture UART0 at 115200 8N1 from reset. Pair both devices, verify input, switch
each off/on with the adapter running, then power-cycle the adapter and confirm
reconnection without entering pairing mode. Persistence across adapter power
cycles is not demonstrated by the supplied successful capture.

One flash-safe refusal remained after Core 1 launch began but before its lockout
registration completed; the trace did not identify its caller. Subsequent bond
writes and reconnects succeeded. The existing NVSettings runtime fallback is
outside this fix. The SDK's committed `setup_tlv()`-before-`hci_init()` ordering
also leaves a separate Classic key-database initialization defect; it was not
changed here. Reconnected devices use generic BLE keyboard/mouse display names.

Cleanup validation: Pico 2 W debug and production builds both passed through
`build-all.sh` with the dependency overrides above. ELF checks confirm the
startup guard remains and temporary storage wrappers/log strings are absent.
`bash -n build-all.sh` and `git diff --check` passed.


## Gamepad slot regression follow-up (2026-10-05)

A later UART capture (still containing dbg3 BOND diagnostics) shows both Stadia
and DualSense reaching device-ready in GP2 and sending reports (`joy_in` rises,
GP2 updated=1), while `joy_ok` and HidInput `joy_get` remain zero. The connected
counter is correct, but input and the mapping UI address gamepads by ordinal
0/1 and were reading raw storage slots instead.

The pre-existing disconnect handler called allocating storage getters for all
three device classes. Keyboard/mouse disconnects could therefore reserve empty
gamepad mappings that were not released because connected was false. Successful
keyboard/mouse off/on reconnect testing exposed this fault.

The follow-up separates non-allocating lookup from ready-time allocation and
unconditionally releases a disconnected device's mappings. Gamepad data/name
APIs now share connected-device enumeration, so genuine gaps after unplugging
one of multiple gamepads also work. Internal maps remain stable while connected.
Temporary bond diagnostics stay removed. Build label: `22.2.0-dbg5`.

Hardware retest: connect keyboard and mouse, cycle both off/on, then pair Stadia
or DualSense. Verify the gamepad appears in Map Devices and joystick input works;
existing diagnostics should show joy_ok/joy_get increasing while controls move.
With two gamepads, disconnect the first and verify the remaining controller is
listed and usable. Finally repeat keyboard/mouse reconnects. Hardware validation
of this follow-up is pending; no new commit was requested for the follow-up.

Pico 2 W debug build passed through `build-all.sh`; diff whitespace and UF2
block/version checks passed. Startup flash guard remains in the ELF.
Follow-up UF2 SHA256: `e7d5a05ccf335905bca21ad7add2c92849f90622eaf2c92347ef17778c4c483a`.

## BLE display-name retention (22.2.0-dbg6)

Ported the name-persistence portion of the Amiga adapter's
`src/bluepad32_bonded_reconnect.c` into `src/bluepad32_ble_names.c`, preserving
its implementation and copyright. No shared Bluepad32 changes. The BLE connect
callback restores saved names when discovery yields a generic BLE label, then
saves genuine names. Metadata uses the existing TLV store, keyed by bond slot
and checked against identity address/type. Recycled slots cannot inherit another
identity's name. Generic fallback names do not overwrite saved names, unchanged
records skip flash writes, and clear-pairing also removes saved name metadata.

Existing Atari bonds have no saved name yet: connect each device once in pairing
mode to learn its advertised name. Then test device off/on and an adapter restart
without re-pairing. This image also includes the pending gamepad slot fix.

Pico 2 W debug build passed via `build-all.sh` with the same dependency overrides.
UF2 block/version checks passed; final ELF includes all three name helpers and
the startup flash guard. The reused name-storage code was compared byte-for-byte
with the Amiga implementation. Hardware verification is pending; changes remain
uncommitted. Temporary bond instrumentation is still absent.

## Model-name fallback and console baud (22.2.0-dbg7)

Observe the existing Device Information query through an adapter-local linker
wrapper. Restore a saved name first; otherwise use the successful Model Number
string when the device has no useful advertised name. Bluepad32 receives every
original event and query status. Its single-client callback is retained when a
duplicate query is rejected. No extra GATT queries or shared dependency edits.
Existing bonds can now acquire a model name on reconnect without re-pairing;
devices that expose neither a name nor a model still use the generic label.

The boot clock change switches clk_peri from 150 MHz to 48 MHz on Pico 2 W,
which previously slowed UART0 from 115200 to approximately 36864 baud. Drain
UART0 before the existing clock change, then recalculate only UART0's divisor.
UART1 is still initialized afterward at 7812 baud; its configuration and the
system clock selection are unchanged. Stadia reconnect and stick mapping are
unchanged.

Pico 2 W debug build passed via build-all.sh. A host harness checked model
fallback, saved/advertised name priority, failed/empty model reads, missing
devices, rejected duplicate queries, event forwarding and subsequent queries.
UF2 structure/version and final ELF wrapper symbols were checked. Hardware
retest: keep the console at 115200 from reset, reconnect the existing keyboard
and mouse bonds, check model names in Devices, and verify Atari input as usual.

## Release 23.1.0 (2026-10-05)

The user confirmed the gamepad routing fix, model names, stable UART0 console
baud, and swapped hardware buttons on Pico 2 W. The final button identifiers
are Up (GP18), Middle/Enter (GP16), and Down (GP17); the naming-only follow-up
preserves behavior. Release version is 23.1.0, with the diagnostic counter reset
to 1. Production validation uses the same dependency overrides above.
