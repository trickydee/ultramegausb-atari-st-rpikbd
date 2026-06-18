# Cherry-pick plan: ebafed7 (working) → 21.0.7 (broken)

**Created:** June 2026  
**Status:** `picking-cherrys` — **P1 in progress**; Xbox/Stadia BT OK at 10 ms polling  
**Baseline (verified on hardware):** `ebafed7` — v21.0.5  
**Regression tip:** `170ac75` — v21.0.7  

Hardware confirmed: **v21.0.5 (`ebafed7`) works** including Xbox/Stadia Bluetooth pairing. Full **v21.0.6 (`67fa026`)** TLV/NVSettings/resume changes still regress BT — deferred.

---

## Progress log (picking-cherrys)

| Step | Commit / change | BT (Xbox/Stadia) | Notes |
|------|-----------------|------------------|-------|
| ✅ | `7a2c46e` — dual-core + build consolidation (no TLV/NVSettings) | OK | Baseline cherry-pick from `67fa026` |
| ✅ | `d664d94` — regression plan doc | OK | |
| ⚠️ | `cd98008` — cherry-pick `9f83ed6` (2 ms USB+HID) | **Hang on pair** | Stadia pairing lock-up |
| ✅ | Revert to **10 ms** single block (`main.cpp`) | OK | Proved 2 ms HID path is the culprit |
| ✅ | `build-all.sh` — `BUILD_BOARDS=pico2_w` default + docs | — | Faster dev builds |
| ✅ | `420e8a4` — non-blocking mount splashes (`3bb2139`) | OK | USB splash; BT pairing still OK |
| ⏳ | `e92dc4e` — atomic cross-core state | *next* | |

### What we proved (June 2026)

1. **`7a2c46e` without TLV/NVSettings is BT-safe** — dual-core joystick fix + pico-sdk `setup_tlv` submodule alone did not break pairing on tested hardware.
2. **Raw `9f83ed6` is NOT BT-safe** — running `tuh_task()` + `handle_mouse`/`handle_keyboard`/`handle_joystick` every **2 ms** caused Stadia (and likely Xbox) pairing hangs. Restoring one **10 ms** block fixed it.
3. **Do not cherry-pick `9f83ed6` as-is.** When we want USB responsiveness later, use **`c07ad1a` pattern**: 2 ms `tuh_task()` only, 10 ms HID — not 2 ms on everything.
4. **`bluepad32_poll()` at 1 ms unchanged** — the regression was the extra 2 ms HID/USB work, not BT poll rate.

**Current `main.cpp` timing:** one 10 ms block (USB + HID + UI); `bluepad32_poll()` still 1 ms; serial RX every loop.

**Next picks (revised order):** `420e8a4` → `e92dc4e` → `c07ad1a` (skip re-applying raw `9f83ed6`; `c07ad1a` adds 2 ms `tuh_task` only if desired) → `204d8b9` → `4f42d8e`. Test BT after each.

---

## Regression diagnosis (short)

| Suspect | What changed |
|---------|----------------|
| **Primary (v21.0.6)** | `67fa026` TLV flash persistence + universal Core 1 resume |
| **Secondary** | Board-aware **NVSettings** flash layout |
| **Proven (P1)** | **`9f83ed6` as cherry-picked** — 2 ms HID + USB → Stadia pairing hang; 10 ms OK |
| **Likely safe** | `420e8a4`–`4f42d8e` mount splash / mouse / atomic (test BT each pick) |

---

## All commits between working and broken

```
170ac75  chore: release v21.0.7 — P1 input, mount splash, and mouse tuning
4f42d8e  fix: stable mount splash, mouse tuning, and per-build version bumps (P1)
204d8b9  perf: accumulate USB mouse deltas per HID tick (P1)
c07ad1a  fix: restore mouse speed and hold mount splashes for 2s (P1)
e92dc4e  fix: atomic joystick/mouse state and volatile quadrature regs (P1)
420e8a4  perf: non-blocking USB mount splashes (P1)
9f83ed6  perf: poll USB HID input every 2ms instead of 10ms (P1)
67fa026  fix: P0 correctness, persistent BT pairing, and build consolidation (v21.0.6)
ebafed7  chore: bump version to 21.0.5  ← WORKING BASELINE
```

---

## Commit-by-commit detail

### `67fa026` — v21.0.6 — ⚠️ DO NOT cherry-pick wholesale

**Cherry-pick recommendation:** Split. Take dual-core fixes; defer or rework BT flash + resume.

#### Bluetooth / flash (regression suspects)

- Re-enabled **BTstack TLV flash persistence** (`setup_tlv()` in pico-sdk `btstack_cyw43.c` before `hci_init`)
- Removed boot-time `uni_bt_del_keys_unsafe()` — keys persist in flash
- **`bluepad32_platform.c`:** Removed Xbox/Stadia-only resume at `on_device_ready`; added universal resume (all device types) + 10ms delay
- **`NVSettings.cpp`:** Board-aware flash offset (`0x1FD000` 2 MiB / `0x3FC000` 4 MiB), migration from `0x1FF000`

#### Dual-core correctness (likely safe to cherry-pick)

- Removed `update_joystick_state()` from Core 1 / 6301 port reads (`6301/ireg.c`, `HidInput.cpp`)
- `volatile` / cross-core fixes in `include/HidInput.h`

#### Build / docs (optional)

- Deleted `build-wireless.sh`, `build-wireless-picow.sh`, `apply-patches.sh`
- Consolidated `build-all.sh`
- Added `AGENTS.md`
- pico-sdk submodule bump
- Removed `patches/hid_gamecube_skip.patch`, `patches/pioasm-cmake-fix.patch`

**Files touched:** `.gitignore`, `6301/ireg.c`, `AGENTS.md`, `CMakeLists.txt`, `README.md`, `RELEASE_NOTES.md`, `build-all.sh`, `docs/*`, `include/HidInput.h`, `pico-sdk`, `src/HidInput.cpp`, `src/NVSettings.cpp`, `src/bluepad32_platform.c`

---

### `9f83ed6` — USB polling split — ⚠️ DO NOT cherry-pick as-is

**Hardware result:** Cherry-picked on `picking-cherrys` → Stadia BT pairing hang. Reverting to single **10 ms** block restored pairing.

- Original commit runs `tuh_task()` + all HID handlers every **2 ms**
- Extra Core 0 load during BT SMP/flash window conflicts with pairing (even though `bluepad32_platform.c` unchanged)
- **Use `c07ad1a` instead** when adding USB responsiveness: 2 ms `tuh_task()` only, 10 ms mouse/keyboard/joystick

**Files:** `src/main.cpp`

---

### `420e8a4` — Non-blocking mount splashes — ✅ Next to cherry-pick

- New `src/mount_splash.c`, `include/mount_splash.h`
- Replaced `sleep_ms()` OLED blocks in USB mount callbacks
- `UserInterface` skips redraw while splash active
- `ssd1306.c` — initial splash guard hooks

**Files:** `mount_splash.*`, `*_controller.c`, `xinput.c`, `hid_app_host.c`, `UserInterface.*`, `ssd1306.c`, `CMakeLists.txt`

---

### `e92dc4e` — Atomic cross-core state — ✅ Safe to cherry-pick

- `std::atomic` mouse/joystick state (Core 0 write, Core 1 read)
- `volatile` on `AtariSTMouse` quadrature registers

**Files:** `HidInput.cpp`, `HidInput.h`, `AtariSTMouse.h`, `AtariSTMouse.cpp`

---

### `c07ad1a` — Mouse speed + 2s splash — ✅ Safe to cherry-pick

- Mouse/kb/joy back to 10 ms sampling (keep 2 ms `tuh_task`)
- Mount splashes held ~2 s; cached text; poll from main loop
- Xbox mount path in `main.cpp` → `mount_splash`

**Files:** `main.cpp`, `mount_splash.c`, `HidInput.cpp`, `UserInterface.cpp`

---

### `204d8b9` — Mouse delta accumulation — ✅ Safe to cherry-pick

- Drain up to **16** USB mouse reports per 10 ms HID tick
- Sum dx/dy, single `set_speed()` per tick

**Files:** `src/HidInput.cpp`

---

### `4f42d8e` — Stable splash + mouse tuning — ✅ Safe to cherry-pick

- Fixed **inverted splash expiry** (splashes hold **5 s**)
- `mount_splash_blocks_oled()` — suppress other OLED writers while active
- Draw once per splash (no full-frame I2C every poll)
- Mouse: drain cap **32**, `scale_mouse_burst()`, lower `MIN_SPEED`
- `PROJECT_VERSION_STRING` from version macros
- `build-all.sh` auto-bumps patch (`SKIP_VERSION_BUMP=1` to disable)

**Files:** `mount_splash.c`, `mount_splash.h`, `ssd1306.c`, `HidInput.cpp`, `AtariSTMouse.cpp`, `include/version.h`, `build-all.sh`, controller sources, `AGENTS.md`

---

### `170ac75` — v21.0.7 release — ✅ Docs/version only

- `include/version.h` → 21.0.7
- `RELEASE_NOTES.md`, `README.md`, `AGENTS.md` tweak

---

## Working vs broken — behaviour matrix

| Area | `ebafed7` (21.0.5) ✅ | `170ac75` (21.0.7) ❌ |
|------|------------------------|------------------------|
| BT pairing keys | RAM / prior behaviour | TLV flash persistence |
| Core 1 resume at ready | Xbox/Stadia only, 10ms | All devices, 10ms |
| NVSettings flash | Fixed `0x1FF000` | Board-aware below BT bank |
| USB `tuh_task` | 10 ms (combined) | 2 ms |
| HID mouse/kb/joy | 10 ms | 10 ms (after c07ad1a) |
| Mount splash | `sleep_ms()` in callbacks | 5 s non-blocking |
| Mouse feel | Per-tick deltas | Accumulated + burst scaling |
| Core 1 USB reads | `update_joystick_state()` in 6301 path | Removed (Core 0 only) |

---

## Suggested cherry-pick order (from `ebafed7` branch)

1. **Create branch** from `ebafed7`:
   ```bash
   git checkout -b q3-26-cherry-pick ebafed7
   ```

2. **Dual-core fixes only** from `67fa026` (manual patch, not full commit):
   - `6301/ireg.c` — remove USB from Core 1
   - `HidInput.cpp` / `HidInput.h` — volatile/atomic
   - **Do not** take `bluepad32_platform.c` resume change or TLV enable yet
   - **Do not** take `NVSettings.cpp` until TLV layout validated with Xbox/Stadia

3. **P1 commits (revised order)** — test BT Xbox/Stadia after each:
   ```bash
   # SKIP raw 9f83ed6 — proved BT-unsafe; keep 10 ms or use c07ad1a split later
   git cherry-pick 420e8a4
   git cherry-pick e92dc4e
   git cherry-pick c07ad1a    # 2 ms tuh_task only; verify BT still OK
   git cherry-pick 204d8b9
   git cherry-pick 4f42d8e
   ```

4. **TLV / persistent pairing** — separate follow-up on `stadia-xbox-bluetooth-broken` or new branch:
   - Pause Core 1 for all discoveries during pairing
   - Restore Xbox/Stadia-specific resume timing
   - Re-enable TLV only after hardware sign-off

5. **Build consolidation** from `67fa026` — optional, independent of BT:
   - `build-all.sh` improvements can be applied manually or from later commits

---

## Other branches / WIP (not in 21.0.7)

| Branch / stash | Contents |
|----------------|----------|
| `stadia-xbox-bluetooth-broken` (`d90e00f`) | `CYCLES_PER_LOOP=500`, extended BT pause experiments (still broken) |
| `q3-26-improvements` (`170ac75`) | Full broken tip |
| git stash | `BUILD_BOARDS` default now in `build-all.sh` |

---

## Hardware test checklist (after each cherry-pick)

- [ ] Xbox BT — fresh pair
- [ ] Stadia BT — fresh pair
- [ ] DualSense / Switch BT — pair + IKBD responds (not frozen)
- [ ] Xbox/Stadia USB
- [ ] Mouse feel (slow move + fast flick)
- [ ] Mount splash visible ~5 s, no OLED flicker during splash
- [ ] Keyboard / serial to Atari ST
- [ ] Reboot — BT reconnect (only if TLV re-enabled)

---

## References

- Original Xbox/Stadia fix: commit `4047c52` (Dec 2025), RAM-only keys
- [pico-sdk #2725](https://github.com/raspberrypi/pico-sdk/issues/2725) — multicore + TLV (we launch Core 1 — should be OK)
- [pico/flash.h](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_flash/include/pico/flash.h) — `flash_safe_execute_core_init()` required on Core 1
- Local: `docs/OPTIMIZATION_REVIEW.md` §2 flash layout, §3 BT Core 1 resume bug

---

## Future cool stuff — BTstack upgrade

**When:** After cherry-pick branch is stable on hardware (especially Xbox/Stadia BT). Treat as a separate experiment branch — not mixed with P1 picks or TLV rework.

**Upstream:** [bluekitchen/btstack](https://github.com/bluekitchen/btstack) — the stack bluepad32 and pico-sdk both vendor.

### Where BTstack lives in this repo

| Copy | Submodule URL | Used by |
|------|---------------|---------|
| `pico-sdk/lib/btstack` | `https://github.com/bluekitchen/btstack.git` | **Pico 2 W build** — `pico_btstack_classic`, `pico_btstack_cyw43` in top-level `CMakeLists.txt` |
| `bluepad32/external/btstack` | same bluekitchen URL | ESP32 / posix bluepad32 examples only; **not** the default Pico wireless path |

Pico firmware includes `${PICO_SDK_PATH}/lib/btstack/src` and links SDK BTstack targets. Updating `bluepad32/external/btstack` alone does **not** change the Pico 2 W UF2 unless you also redirect the SDK build.

### How to upgrade (Pico 2 W)

**Option A — override path (good for experiments):**

1. Check out a bluekitchen/btstack commit (tag or `master`) outside the SDK, e.g. `vendor/btstack/`.
2. Before `pico_sdk_init()` in `CMakeLists.txt` (or via env when running cmake):
   ```cmake
   set(PICO_BTSTACK_PATH /path/to/btstack)
   ```
   Or: `export PICO_BTSTACK_PATH=...` when invoking cmake.
3. If the newer tree has `src/hci_event_builder.c`, pico-sdk's `pico_btstack/CMakeLists.txt` picks it up automatically (see bluepad32 `examples/pico_w/CMakeLists.txt` comments).
4. Rebuild all wireless boards; run full BT checklist above.

**Option B — bump `pico-sdk/lib/btstack` submodule** — couples BTstack version to pico-sdk revision; harder to merge SDK updates. Only consider when upstream SDK ships a newer pin you want.

**Option C — sync `bluepad32/external/btstack`** — optional parity with ESP32 examples; re-apply `bluepad32/external/patches/*.patch` if using that copy via `PICO_BTSTACK_PATH`.

### Cautions (given current BT regression)

- **TLV / flash pairing** — newer BTstack may change `btstack_tlv_flash_bank.c` or link-key persistence; retest fresh pair + reboot reconnect.
- **Dual-core + flash** — any persistent keys need Core 1 `flash_safe_execute` coordination (`bluepad32_platform.c`, `pico/flash.h`).
- **Do not edit `bluepad32/` submodule in place** — vendor a fork or use `PICO_BTSTACK_PATH` per `AGENTS.md`.
- **Licensing** — Pico builds: covered by [Raspberry Pi BTstack license](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_btstack/LICENSE.RP). ESP32: BlueKitchen commercial terms.

### Suggested workflow

1. Cherry-pick + TLV fix lands on a known-good BT baseline.
2. Branch `btstack-bump-experiment` from that tip.
3. Try latest bluekitchen tag or a specific fix commit (check CHANGELOG for SMP/L2CAP/HID fixes relevant to Xbox/Stadia).
4. If pairing regresses, bisect bluekitchen commits or stay on pico-sdk's pinned `lib/btstack` until SDK catches up.

### Useful links

- [bluekitchen/btstack](https://github.com/bluekitchen/btstack) — upstream, CHANGELOG
- [bluepad32 Pico W example](https://github.com/ricardoquesada/bluepad32/blob/main/examples/pico_w/CMakeLists.txt) — `PICO_BTSTACK_PATH` pattern
- [pico-sdk pico_btstack CMakeLists](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_btstack/CMakeLists.txt) — `PICO_BTSTACK_PATH` / source list
- [pico-sdk #2725](https://github.com/raspberrypi/pico-sdk/issues/2725) — multicore + TLV
