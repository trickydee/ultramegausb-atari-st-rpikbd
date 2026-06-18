# AGENTS.md

Firmware that emulates the Atari ST IKBD (HD6301) on Raspberry Pi Pico boards, routing USB and Bluetooth keyboards, mice, and gamepads to vintage Atari Mega ST / STE / TT computers. Version **21.0.7+** (`include/version.h`; `build-all.sh` bumps patch each run).

Human docs: `README.md`. Deep technical detail: `docs/DEVELOPER_GUIDE.md`.

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
| `BUILD_VARIANT` | `production` | `production`, `debug`, or `speed` |
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
| `include/config.h` | GPIO pins, clock, debug flags |
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

---

## Boundaries

### Always

- Poll serial RX every Core 0 loop iteration (not on a 10 ms timer).
- Keep UART hardware FIFO enabled in `SerialPort.cpp`.
- Check specific controllers in `hid_app_host.c` before generic HID parsing.
- Read surrounding code and a sibling controller before editing.
- Run a compile check after firmware changes.
- Link to `docs/` for detail instead of duplicating long guides here.

### Ask first

- Bumping `include/version.h` or editing `RELEASE_NOTES.md`.
- Creating git commits or pushing to remote.
- Modifying `pico-sdk/` or `bluepad32/` submodules.
- Changing `CYCLES_PER_LOOP`, serial baud rate, or Core 1 loop structure.
- Changing binary type (`copy_to_ram` vs XIP) in `CMakeLists.txt`.

### Never

- Add `sleep` or blocking I/O to Core 1's tight loop.
- Change `CYCLES_PER_LOOP` from `1000` in `main.cpp` without explicit approval (breaks 1 MHz 6301 timing).
- Change Atari serial baud from **7812**.
- Never commit `build/` directories or local build artifacts.
- Force-push to `main`/`master`.
- Refactor unrelated code in the same change.

---

## Gotchas

- **USB vs HID poll rate:** Core 0 runs `tuh_task()` every **2 ms** so USB mount/enumeration stays responsive. Mouse, keyboard, and joystick sampling run every **10 ms** because `AtariSTMouse::set_speed()` converts delta magnitude to quadrature timing (`MAX_SPEED / delta`); calling it every 2 ms with single-report deltas feels sluggish. `handle_mouse()` drains and sums up to `MOUSE_REPORT_DRAIN_MAX` USB reports per 10 ms tick before one `set_speed()`. Fast flicks get mild burst scaling above ~96 counts/tick; `MIN_SPEED` caps maximum quadrature rate.
- **Mount splash OLED:** Production uses `mount_splash_show()` on attach (default **5 s**). Splash is queued in mount callbacks, drawn once by `mount_splash_service()` after `tuh_task()`. `mount_splash_blocks_oled()` suppresses other writers until expiry — do not redraw every poll (full-frame I2C starves mouse input). Firmware version is on the splash footer. `build-all.sh` auto-bumps patch (`SKIP_VERSION_BUMP=1` to disable).
- **Core 1 freeze:** Bluetooth pairing writes flash — Core 1 must pause (`flash_safe_execute`). See `docs/TECHNICAL_NOTES.md`.
- **BT clock:** 225 MHz for CYW43 stability; 270 MHz can cause stalls. USB-only builds use 270 MHz.
- **BT binary type:** wireless builds use XIP (RAM constrained); USB-only builds use `copy_to_ram`.
- **Controller detected, no input:** check report parsing and `HidInput.cpp` priority chain.
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
| `docs/DEVELOPER_GUIDE.md` | Architecture, adding controllers, design decisions |
| `docs/TECHNICAL_NOTES.md` | Timing optimisations, serial fixes |
| `docs/USB_DEBUGGING_METHODOLOGY.md` | USB enumeration debugging |
| `docs/custom-mappings.md` | Controller button mappings |
| `RELEASE_NOTES.md` | Version history |
