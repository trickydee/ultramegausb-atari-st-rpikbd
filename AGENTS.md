# AGENTS.md

Firmware that emulates the Atari ST IKBD (HD6301) on Raspberry Pi Pico boards, routing USB and Bluetooth keyboards, mice, and gamepads to vintage Atari Mega ST / STE / TT computers. Version **22.1.1** (`include/version.h`).

Human docs: `README.md`. UI alignment handoff: `docs/UI_UNIFICATION.md`. Deep technical detail: `docs/DEVELOPER_GUIDE.md`.

---

## Dev environment

- Run `git submodule update --init --recursive` before the first build (needs `pico-sdk/` and `bluepad32/`).
- Requires `cmake`, `git`, and `arm-none-eabi-gcc` (Homebrew `arm-none-eabi-gcc` on Mac; `gcc-arm-none-eabi` on Debian/Ubuntu).
- Prefer `./build-all.sh` — CMake trees are created under `build/` (e.g. `build/build-pico2_w`).
- Serial debug console: UART0 @ 115200 baud (GP0/GP1).

---

## Build commands

```bash
# Default: Pico 2 W production (fast dev loop)
./build-all.sh

# Incremental rebuild — keep CMake tree between runs
CLEAN_BUILD_DIRS=0 ./build-all.sh

# Full multi-board release build
BUILD_BOARDS=all ./build-all.sh

# Single board manual build
mkdir -p build/build-pico2_w && cd build/build-pico2_w
cmake ../.. -DPICO_BOARD=pico2_w -DENABLE_DEBUG=0
make -j$(nproc)
```

### `build-all.sh` environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `BUILD_BOARDS` | `pico2_w` | `pico2_w`, comma-separated list, or `all` for every board |
| `BUILD_VARIANT` | `production` | `production`, `debug` (`ENABLE_SERIAL_LOGGING=1`, `[DIAG]` logs), or `speed` |
| `SKIP_VARIANTS` | `1` | `1` = one variant; `0` = after debug, also build production + speed |
| `CLEAN_BUILD_DIRS` | `1` | `0` = keep `build/build-*` for incremental rebuilds |
| `DEBUG` | `0` | `1` = debug OLED screens |
| `LANGUAGE` | `EN` | `EN`, `FR`, `DE`, `SP`, `IT` |

- UF2 outputs: `dist/atari_ikbd_{pico|pico2|pico_w|pico2_w}_{debug|production|speed}.uf2`
- Bluetooth enabled when `PICO_BOARD` is `pico_w` or `pico2_w` (`CMakeLists.txt`)

---

## Architecture

Dual-core: **Core 0** runs USB (TinyUSB), Bluetooth (Bluepad32 on wireless boards), input processing, OLED UI, and UART to the Atari. **Core 1** runs the HD6301 emulator in a tight loop with no delays.

```
USB/BT → hid_app_host.c / bluepad32 → HidInput.cpp → serial → Core 1 (6301) → UART1 @ 7812 baud → Atari ST
```

| Board | MCU | Bluetooth |
|-------|-----|-----------|
| `pico` | RP2040 | No |
| `pico2` | RP2350 | No |
| `pico_w` | RP2040 | Limited (RAM) |
| `pico2_w` | RP2350 | Yes (recommended) |

Full diagrams and design rationale: `docs/DEVELOPER_GUIDE.md`.

### Core 0 main-loop timing

| Interval | What runs |
|----------|-----------|
| **Every loop** (~continuous) | `handle_rx_from_st()`, `SerialPort::drain_tx_log()`, `AtariSTMouse::update()` |
| **10 ms** | `tuh_task()`, `switch_check_delayed_init()`, `mount_splash_service()`, `handle_mouse()` → `handle_keyboard()` → `handle_joystick()`, `bluepad32_check_ui_update()`, `ui.update()` |
| **1 ms** (wireless builds, BT on) | `bluepad32_poll()`; then `tuh_task()` + `mount_splash_service()` if USB is on (anti-starvation) |

- **`handle_mouse()` / `handle_keyboard()` / `handle_joystick()`** run on the **10 ms** tick only — not 2 ms. A 2 ms HID poll was tried (`cd98008`) and reverted (`f3fa3ea`) because it broke BLE gamepad pairing.
- Call **`handle_keyboard()` exactly once** per 10 ms tick (`usb_runtime_is_enabled() \|\| bt_runtime_is_enabled()`). A second call drains `wheel_pulses` and clears one-frame scroll→arrow injections before the Atari sees them.
- **`handle_mouse()` before `handle_keyboard()`** on every 10 ms tick so scroll wheel pulses are enqueued then applied in the same tick.
- Wheel scroll → cursor keys is applied **after** all keyboard sources in `handle_keyboard()` (BT `peek` rebuilds `key_states` every tick).
- USB mice: all entries in the `device` map with `HID_MOUSE`. BT mice: poll every slot (`MAX_BT_MICE` = 2), not index 0 only.

---

## Project structure

| Path | Purpose |
|------|---------|
| `src/main.cpp` | Core 0 entry, main loop, Core 1 launch |
| `src/HidInput.cpp` | Input hub, shortcuts, joystick routing, Llamatron mode |
| `src/hid_app_host.c` | USB HID attach/detach, report routing |
| `src/*_controller.c` | Per-controller USB drivers |
| `src/bluepad32_*.c/cpp` | Bluetooth (wireless builds only) |
| `src/SerialPort.cpp` | Atari UART (7812 baud, FIFO) |
| `include/config.h` | GPIO pins, clock, `CYCLES_PER_LOOP` (default 500), debug flags |
| `6301/` | HD6301 CPU emulator |
| `pico-sdk/`, `bluepad32/` | Git submodules — do not modify directly |
| `hardware/` | KiCad PCB for Mega ST/STE/TT adapter |

GPIO: Atari UART TX=GP4 RX=GP5; OLED I2C GP8/GP9; UI buttons GP16–18. See `include/config.h`.

---

## Conventions

- **C** (`*.c`): controller drivers, USB host, low-level code.
- **C++** (`*.cpp`): main app, input processing, UI.
- New USB controller: `include/{name}_controller.h` + `src/{name}_controller.c`, following the pattern in `docs/DEVELOPER_GUIDE.md`.
- Copy an existing controller before inventing a new pattern:
  - Simple HID → `src/stadia_controller.c`
  - No analog sticks → `src/psc_controller.c`
  - Custom protocol → `src/xinput_atari.cpp`
  - Bluetooth → `src/bluepad32_atari.cpp`
- Atari joystick mapping: axis = 4-bit (up/down/left/right); fire = `0` pressed, `1` not pressed (active LOW); ~15–25% stick deadzone.
- Minimal scope — match surrounding code; no drive-by refactors.
- Comments only for non-obvious logic.

### Adding a USB controller (checklist)

1. Create `include/` + `src/` controller files with `*_is_controller`, `*_process_report`, `*_mount_cb`, `*_unmount_cb`, `*_to_atari`.
2. Wire mount/report/unmount in `src/hid_app_host.c` (**before** generic HID parsing).
3. Add getter to joystick priority chain in `src/HidInput.cpp`.
4. Add source to `CMakeLists.txt`.
5. Document mapping in `docs/custom-mappings.md` if user-visible.

---

## Testing

No automated test suite. Verify changes compile and behave on hardware where possible.

```bash
# Compile check (fastest — default is Pico 2 W production)
CLEAN_BUILD_DIRS=0 ./build-all.sh

# Full multi-board compile check
BUILD_BOARDS=all CLEAN_BUILD_DIRS=0 ./build-all.sh

# Single board
mkdir -p build/build-pico && cd build/build-pico && cmake ../.. -DPICO_BOARD=pico && make -j4
```

**Controller checklist** (from `docs/DEVELOPER_GUIDE.md`):

- [ ] Detected (OLED splash or serial log)
- [ ] Report parsing updates state
- [ ] Atari mapping correct (directions + fire)
- [ ] Hot-swap works
- [ ] No interference with other controller types
- [ ] Llamatron dual-stick works if applicable (`Ctrl+F4`)

**USB debugging:** `docs/USB_DEBUGGING_METHODOLOGY.md`  
**Timing / serial issues:** `docs/TECHNICAL_NOTES.md`  
**BT regression / `[DIAG]` interpretation:** `docs/FUTURE_WORK.md`, `docs/UI_UNIFICATION.md`

---

## Serial diagnostics (`[DIAG]` builds)

Use a **debug variant** when investigating Bluetooth, Core 1 freezes, or input routing. Production builds set `ENABLE_SERIAL_LOGGING=0` and omit `[DIAG]` lines.

### Build and flash

```bash
# Pico 2 W diagnostic firmware (incremental rebuild)
BUILD_VARIANT=debug CLEAN_BUILD_DIRS=0 ./build-all.sh
```

- Output: `dist/atari_ikbd_pico2_w_debug.uf2`
- `build-all.sh` maps `BUILD_VARIANT=debug` → `ENABLE_SERIAL_LOGGING=1` (OLED on, verbose log).
- `BUILD_VARIANT=production` → `ENABLE_SERIAL_LOGGING=0` (minimal log). Do not rely on production UF2 for serial diagnosis.

### Serial capture

- **Port:** UART0 @ **115200** 8N1 — **GP0 = TX**, GP1 = RX (not USB CDC).
- **Confirm correct firmware** before testing — boot must show:
  - `Firmware version: 22.1.0-dbg1` (or current `-dbgN`)
  - `[DIAG] firmware 22.1.0-dbg1 (Core1 phase/pc, BT storage+getters, HidInput consume)`
- OLED splash / mount footer also show `v22.1.0-dbg1` via `PROJECT_VERSION_DISPLAY`.

If you only see Bluepad32/BTstack lines and no `[DIAG]` heartbeat, you are on old or production firmware.

### Version label (`include/version.h`)

| Macro | Purpose |
|-------|---------|
| `PROJECT_VERSION_STRING` | Release version only, e.g. `22.1.0` |
| `PROJECT_VERSION_DISPLAY` | User-visible label; adds `-dbgN` when `ENABLE_SERIAL_LOGGING=1` |
| `PROJECT_DIAG_BUILD` | Increment **N** each time you flash a new diagnostic UF2 (`-dbg1`, `-dbg2`, …) |

- Bump `PROJECT_DIAG_BUILD` when iterating on diagnostic firmware — **no need to bump patch** for each flash.
- Bump `PROJECT_VERSION_PATCH` only for a real release (ask first per boundaries below).

### Adding `[DIAG]` logs

| Location | Pattern |
|----------|---------|
| `src/main.cpp` (Core 0) | `printf("[DIAG] ...\n")` inside `#if ENABLE_SERIAL_LOGGING` |
| `src/bluepad32_platform.c` | `logi("[DIAG] ...\n")` (Bluepad32 routes to UART when logging on) |
| Pause state | `core1_get_pause_depth()`, `core1_is_paused()` — do not read `g_core1_*` from other files |

**Conventions:**

- Prefix every diagnostic line with `[DIAG]` so logs are grep-friendly.
- Throttle noisy counters (see `bt_diag_maybe_log()` — every 5 s in `bluepad32_platform.c`).
- Heartbeat in `main.cpp` runs every **10 s** when `ENABLE_SERIAL_LOGGING=1`.
- Declare `extern "C"` helpers at **file scope** in `main.cpp` — never nest `extern "C" { }` inside a function (breaks the build).

**Existing instrumentation (v22.1.0-dbg1):**

- **Boot:** pause/resume refcount (`core1_pause_for_bt_enumeration` / `core1_resume_after_bt_enumeration` in `main.cpp`).
- **BT discovery:** pause on gamepad COD `0x0508` or Xbox/Stadia name (`bluepad32_platform.c`); slot index logged on `device ready`.
- **Device ready:** type, name, `pause_depth` before/after 10 ms resume delay.
- **Heartbeat (10 s):** Core 0 loops, BT poll count, Core 1 `hb` / `cycles` / `loops`, **`phase`** (`PAUSED` / `TX_EMPTY` / `RUN_CLOCKS` / `LOOP_DONE`), **`pc`** at last `hd6301_run_clocks` entry, `run_in` vs `run_out`, `pause_spins`, `sci_busy`, `rx_q`, `uart_tx_spin`, `pause_depth`, `BT(kb=… mouse=… joy=…)`, `[CYCLES_FROZEN!]` / `[LOOPS_FROZEN!]`.
- **BT reports (5 s):** HID callback counts per class + `pause_depth` (from `on_controller_data`).
- **BT storage snapshot (5 s):** per-slot `connected` / `updated` / name for GP/KB/MS; callback drops; getter `ok` vs `noupd`.
- **HidInput snapshot (5 s):** `kb_get` / `kb_peek` / `kb_none` / `kb_keys`, `ms_get` / `ms_miss` / `ms_move`, `joy_get`, `mouse_en`, `joy`, `mouse_btn`.

### Interpreting freeze (`phase` + `run_in`/`run_out`)

| `phase` when frozen | `run_in` > `run_out` | Likely cause |
|---------------------|----------------------|--------------|
| `RUN_CLOCKS` | yes | Stuck inside `hd6301_run_clocks` / `instr_exec` |
| `TX_EMPTY` | no | Stuck in `serial_send` UART wait (`uart_tx_spin` climbing) |
| `PAUSED` | no | Pause spin (should still increment `loops` unless wedged in `busy_wait`) |
| `LOOP_TOP` / `LOOP_DONE` | no | Stuck between phases (rare) |

If `kb_in=0` in callbacks but storage shows `c=1 u=0`, Bluepad32 stopped delivering KB reports. If `kb_in>0` but `HidInput kb_get=0` and `kb_noupd` high, polling faster than consumption or getter path issue.

### Reading logs (Stadia/Xbox regression example)

Healthy Core 1: `cycles` and `loops` increase every heartbeat; no `FROZEN` tags.

| Observation | Meaning |
|---------------|---------|
| `pause_depth=1` during pairing, `0` after `device ready` resume | Refcount pause path behaved as designed |
| `pause_depth=0` but `[CYCLES_FROZEN!]` `[LOOPS_FROZEN!]` | Core 1 stuck **outside** the pause loop (e.g. inside `hd6301_run_clocks` or flash-safe path) — not a stuck pause flag |
| `BT reports/5s: kb=0 mouse=0 joy=…` while heartbeat still shows `BT(kb=1 mouse=1 …)` | Connections alive but HID reports not reaching handlers — separate data-path issue |
| OLED responsive, Atari input dead | Core 0 + UI OK; suspect Core 1 emulator or 6301→UART path |

See `docs/FUTURE_WORK.md` for the open Xbox/Stadia BT + KB/mouse regression.

---

## Boundaries

### Always

- Poll serial RX every Core 0 loop iteration (not on a 10 ms timer).
- Call `handle_keyboard()` once per 10 ms tick when USB or BT is enabled.
- Keep UART hardware FIFO enabled in `SerialPort.cpp`.
- Check specific controllers in `hid_app_host.c` before generic HID parsing.
- Read surrounding code and a sibling controller before editing.
- Run a compile check after firmware changes.
- Link to `docs/` for detail instead of duplicating long guides here.

### Ask first

- Bumping `PROJECT_VERSION_PATCH` (or minor/major) or editing `RELEASE_NOTES.md`.
- Creating git commits or pushing to remote.
- Modifying `pico-sdk/` or `bluepad32/` submodules.
- Changing `CYCLES_PER_LOOP` in `include/config.h` (hardware regression test).
- Changing binary type (`copy_to_ram` vs XIP) in `CMakeLists.txt`.

### Never

- Add `sleep` or blocking I/O to Core 1's tight loop.
- Change Atari serial baud from **7812**.
- Change Core 1 loop structure (tight loop, no delays) without explicit approval.
- Never commit `build/` directories or local build artifacts.
- Force-push to `main`/`master`.
- Refactor unrelated code in the same change.

---

## Gotchas

- **USB vs HID poll rate:** `handle_mouse()`, `handle_keyboard()`, and `handle_joystick()` run every **10 ms**. `tuh_task()` also runs on the 10 ms tick and again on each **1 ms** `bluepad32_poll()` when USB+BT are on (~10 extra calls per 10 ms window). `AtariSTMouse::set_speed()` converts delta magnitude to quadrature timing (`MAX_SPEED / delta`); `handle_mouse()` drains up to `MOUSE_REPORT_DRAIN_MAX` USB reports per 10 ms tick before one `set_speed()`. Fast flicks get mild burst scaling above ~96 counts/tick; `MIN_SPEED` caps maximum quadrature rate.
- **Mount splash OLED:** Production uses `mount_splash_show()` on attach (default **5 s**). Splash is queued in mount callbacks, drawn once by `mount_splash_service()` after `tuh_task()` (10 ms and 1 ms BT paths). `mount_splash_blocks_oled()` suppresses other writers until expiry — do not redraw every poll (full-frame I2C starves mouse input). Footer shows `PROJECT_VERSION_DISPLAY` on debug builds (`-dbgN`), else `PROJECT_VERSION_STRING`.
- **Core 1 freeze:** Bluetooth pairing writes flash — Core 1 must pause (`flash_safe_execute`). See `docs/TECHNICAL_NOTES.md`. Pause is **refcounted** (`g_core1_pause_depth` in `main.cpp`): gamepad discovery pauses once; `on_device_ready` resumes once. KB/mouse (BLE) do not pause. **`[DIAG]` logs can show `pause_depth=0` while `[CYCLES_FROZEN!]`** — Core 1 halted inside the emulator loop, not stuck in the pause spin. Use `BUILD_VARIANT=debug` and the Serial diagnostics section above.
- **Xbox/Stadia BT + KB/mouse (resolved v22.1.0):** **BLE HID gamepads** (Stadia, Xbox Wireless — CoD `0x0508`, HID-over-GATT). Refcounted Core 1 pause; `busy_wait_us()` in BT callbacks only; `BT_GAMEPAD_DISCOVERY_SETTLE_MS` + `BT_GAMEPAD_CORE1_RESUME_DELAY_MS` in `config.h`. See `RELEASE_NOTES.md` §22.1.0.
- **BT callbacks (Bluepad32):** Never call `sleep_ms()` or `__wfe()` on Core 0 inside platform callbacks — IRQs may be constrained and the whole adapter (UI included) can hang. Use `busy_wait_us()` for short delays (`bt_callback_busy_wait_ms` in `bluepad32_platform.c`). Defer long work to the main loop when possible.
- **BT binary type:** wireless builds use XIP (RAM constrained); USB-only builds use `copy_to_ram`.
- **Mouse wheel → cursor keys:** Scroll enqueues pulses in `handle_mouse()`; `handle_keyboard()` applies them last. Core 1 reads **`keydown()`** (not `key_states` directly) — `wheel_hold_frames[]` keeps cursor up/down visible for several 10 ms ticks so BT keyboard `peek` cannot erase wheel before the 6301 matrix scan.
- **Multiple BT mice:** Poll all `MAX_BT_MICE` slots in `handle_mouse()`, not `bluepad32_get_mouse(0)` only.
- **Atari comms failures:** baud, FIFO, RX queue overflow, or Core 1 not running.
- **Pico 2 W build failure:** other boards still build; check `build/build-pico2_w/build.log` when `CLEAN_BUILD_DIRS=0`.

---

## PR / commit instructions

- Do not commit or push unless the user explicitly asks.
- Keep diffs focused; no unrelated changes.
- On release: update `include/version.h` and `RELEASE_NOTES.md` together.
- Update this file when introducing new conventions agents must follow.

---

## Further reading

| Document | When |
|----------|------|
| `docs/UI_UNIFICATION.md` | OLED UI (v22.1.0), Map Devices, gamepad cycling plan |
| `docs/FUTURE_WORK.md` | Deferred experiments, regression lessons, `[DIAG]` log interpretation |
| `docs/DEVELOPER_GUIDE.md` | Architecture, adding controllers, design decisions |
| `docs/TECHNICAL_NOTES.md` | Timing optimisations, serial fixes |
| `docs/USB_DEBUGGING_METHODOLOGY.md` | USB enumeration debugging |
| `docs/custom-mappings.md` | Controller button mappings |
| `RELEASE_NOTES.md` | Version history |
