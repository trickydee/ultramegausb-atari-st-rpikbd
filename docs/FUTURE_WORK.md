# Future work & regression notes

**Last updated:** June 2026  
**Current release:** `main` @ **v21.1.2**  
**Active branch:** `feature/ui-alignment` @ **v21.1.3** (`fae81c3`) — OLED UI alignment + Map Devices; **not merged to `main`**. Open BT regression below.  
**Purpose:** Deferred experiments, roadmap, and **lessons learned** — what broke Bluetooth, what we tried, what to avoid. Sprint history is in §Archive.

---

## Documentation map (how our docs align)

| Document | Tracked in git | Role |
|----------|----------------|------|
| **`docs/FUTURE_WORK.md`** (this file) | Yes | **Canonical** — open work, closed experiments, regression lessons, BT/hardware checklists |
| **`docs/TECHNICAL_NOTES.md`** | Yes | Current architecture, timing, serial fixes; some historical sections |
| **`docs/DEVELOPER_GUIDE.md`** | Yes | How to build, add controllers, dual-core design |
| **`docs/IMPLEMENTATION_HISTORY.md`** | Yes | **Shipped** features (controllers, versions) — past tense, not roadmap |
| **`docs/USB_DEBUGGING_METHODOLOGY.md`** | Yes | USB enumeration debugging playbook |
| **`docs/custom-mappings.md`** | Yes | Per-controller Atari mapping reference |
| **`RELEASE_NOTES.md`** | Yes | Version changelog |
| **`AGENTS.md`** | Yes | Agent/build conventions |
| **`docs/UI_UNIFICATION.md`** | Yes | OLED UI (v22.1.0), Map Devices, BLE gamepad pairing fix notes |
| **`docs/OPTIMIZATION_REVIEW.md`** | No (local) | Deep-dive timing/flash analysis; conclusions mirrored here and in `TECHNICAL_NOTES.md` |
| **`docs/COMPARISON_RP2_ATARIST.md`** | No (local) | Logronoid / rp2-atarist-rpikb alignment notes |
| **`DOCUMENTATION_ANALYSIS.md`** | No (root) | Meta audit for doc completeness — optional |

**Rule of thumb:** put **new experiments and “don’t do this again”** here; put **how the code works today** in `TECHNICAL_NOTES.md` / `DEVELOPER_GUIDE.md`; put **what shipped in vX.Y** in `RELEASE_NOTES.md` and `IMPLEMENTATION_HISTORY.md`.

---

## Deferred work (priority)

| Priority | Item | Status | Notes |
|----------|------|--------|-------|
| **P1** | **Xbox/Stadia BT + KB/mouse** | **Resolved v22.1.0** | BLE HID gamepad pairing (Stadia, Xbox Wireless) with BLE KB/mouse connected — Core 1 flash-safe timing fixed. See §Xbox/Stadia BT regression (June 2026). |
| — | **BTstack v1.8+** | **Blocked** | v1.7-rc1 hardware fail; v1.8+ needs pico-sdk [#2996](https://github.com/raspberrypi/pico-sdk/pull/2996) + bluepad32 `hids_host` port. Stay on **pico-sdk pin v1.6.2**. |
| — | **2 ms USB+HID poll** (`9f83ed6`) | **Rejected** | Stadia/Xbox pairing hang; keep **10 ms** HID block. Optional later: 2 ms `tuh_task` only (`c07ad1a`) — not tested on hardware recently. |
| P2 | **pico-sdk submodule upgrade** | Open | Re-apply `setup_tlv()` before `hci_init()` patch in `btstack_cyw43.c` after any SDK bump. |
| P2 | **Bluepad32 pin update** | Open | Test Xbox/Stadia BT after any bluepad32 bump; do not edit submodule in place. |
| P2 | **RAM-hot Core 1** (XIP builds) | Open | Copy 6301 ROM/RAM hot for Pico 2 W BT builds — see `OPTIMIZATION_REVIEW.md` locally. |
| P3 | **NVSettings write debounce** | Open | Reduce flash wear from frequent UI writes. |
| P3 | **Pico W soak** | Open | 2 MiB flash overlap was fixed in NVSettings map; limited BT RAM — validate on hardware. |
| P3 | **UART hardware FIFO A/B test** | Open | Currently FIFO off (logronoid baseline). |
| P3 | **Map Devices — cycle gamepad per port** | Open | Design + checklist in `docs/UI_UNIFICATION.md` §Planned. |
| P3 | **Customizable controller mappings** | Open | Starting point: `docs/custom-mappings.md`. |

---

## Xbox/Stadia BT regression (June 2026) — **resolved in v22.1.0**

### Symptoms (before fix)

- BT keyboard and mouse worked until a **Google Stadia** or **Xbox Wireless** controller was paired over Bluetooth.
- Adapter could hang (including OLED) or keyboard/mouse stopped reaching the Atari ST.
- PS5 (BLE) unaffected in the same session.

### Bluetooth type

Stadia and Xbox Wireless (Xbox One S, Series X|S) pair as **BLE HID gamepads** — **Bluetooth Low Energy**, **HID-over-GATT**, Class of Device **0x0508**. Same BLE radio as MX keyboards/mice, but a longer pairing path (bonding + flash TLV writes). Not BR/EDR classic in the traces captured on Pico 2 W.

### Root cause

Race between BTstack `flash_safe_execute` and Core 1 in the HD6301 XIP emulator. `sleep_ms()` / `__wfe()` in Bluepad32 callbacks could freeze Core 0 entirely.

### Shipped fix (v22.1.0)

- Refcounted `g_core1_pause_depth`
- `busy_wait_us()` only in BT callbacks (`bt_callback_busy_wait_ms`)
- `BT_GAMEPAD_DISCOVERY_SETTLE_MS` (30 ms), `BT_GAMEPAD_CORE1_RESUME_DELAY_MS` (100 ms)
- Core 1 pause loop uses `__wfe()` for flash lockout
- `[DIAG]` builds: `BUILD_VARIANT=debug` → `22.1.0-dbgN`

Full write-up: `RELEASE_NOTES.md` §22.1.0.

### Historical investigation (v21.1.3 era)

| Change | File | Risk |
|--------|------|------|
| Device names + `notify_ui_device_update()` | `bluepad32_platform.c` | Low–medium |
| USB `usb_device_map` / Map Devices OLED | various | None for BT fix |

Leading hypotheses (pre-fix): Core 1 pause refcount; flash timing; callback blocking. All addressed in v22.1.0.

Full UI handoff: `docs/UI_UNIFICATION.md`.

---

## BTstack upgrade experiment — **closed** (June 2026)

**Branch:** `feature/btstack-upgrade` (deleted). **Approach:** `PICO_BTSTACK_PATH` → `vendor/btstack`. **Outcome:** Do not ship; remain on **pico-sdk/lib/btstack @ v1.6.2**.

### Bisect (compile + hardware)

No formal **v1.7.0** tag. Ladder: **v1.6.2 → v1.7-rc1 → v1.8 → v1.8.1 → v1.8.2**.

| Tag | `hids_client.c` | Compiles | Xbox/Stadia BT | PS5 / KB / mouse |
|-----|-----------------|----------|----------------|------------------|
| **v1.6.2** (SDK pin) | yes | yes | **OK** | OK |
| **v1.7-rc1** | yes | yes | **FAIL** | OK |
| **v1.8+** | no (`hids_host`) | no | — | — |

**v1.8 boundary:** BLE HID renamed `hids_client` → `hids_host`. pico-sdk `pico_btstack/CMakeLists.txt` and bluepad32 still expect `hids_client` — v1.8.x does not compile without porting.

**v1.7-rc1 hardware (v21.1.4 test):** Boots; keyboard, mouse, **PS5 (BLE)** work. **Xbox and Stadia (classic BR/EDR HID)** hit the same pairing/connect failure class as past regressions. Flashed **v21.1.2 / SDK pin** → Xbox/Stadia OK again.

### Where BTstack lives

| Copy | Used by Pico 2 W build? |
|------|-------------------------|
| `pico-sdk/lib/btstack` | **Yes** — default |
| `bluepad32/external/btstack` | No — ESP32/posix examples only |

Updating `bluepad32/external/btstack` alone does **not** change the Pico UF2.

### If revisiting v1.8+

1. Cherry-pick or merge [pico-sdk #2996](https://github.com/raspberrypi/pico-sdk/pull/2996) (`hids_host` in CMakeLists).
2. Port bluepad32 `uni_bt_le.c` / HID client calls to `hids_host` API (or wait for upstream bluepad32).
3. Preserve local **`setup_tlv()` before `hci_init()`** in `pico-sdk/.../btstack_cyw43.c`.
4. Full BT checklist below on **each** bump.

### Useful links

- [bluekitchen/btstack](https://github.com/bluekitchen/btstack) — CHANGELOG
- [pico-sdk pico_btstack CMakeLists](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_btstack/CMakeLists.txt)
- [pico-sdk #2964](https://github.com/raspberrypi/pico-sdk/issues/2964) — SDK BTstack update tracking
- [pico-sdk #2725](https://github.com/raspberrypi/pico-sdk/issues/2725) — multicore + TLV

---

## Known regressions (do not repeat)

| Change | Symptom | Verdict |
|--------|---------|---------|
| Cherry-pick **`9f83ed6`** — 2 ms `tuh_task` + all HID every 2 ms | Stadia (and likely Xbox) pairing hang | **Rejected** — keep 10 ms HID block |
| Cherry-pick **`67fa026` wholesale** | BT regressions before flash sprint fixes | Split only; flash sprint landed separately |
| **BTstack v1.7-rc1** via `PICO_BTSTACK_PATH` | Xbox/Stadia classic BT broken; BLE OK | **Rejected** — SDK v1.6.2 |
| **BTstack v1.8+** without port | CMake failure (`hids_client.c` missing) | Blocked on SDK + bluepad32 |

**Safe patterns (hardware-tested on path to v21.1.2):** non-blocking mount splash (`420e8a4`), atomic cross-core state (`e92dc4e`), mouse delta drain (`204d8b9` / `4f42d8e`), board-aware NVSettings + TLV persistence + universal Core 1 resume (flash sprint).

**Current `main.cpp` timing:** one **10 ms** block (USB + HID + UI); `bluepad32_poll()` ~**1 ms**; `tuh_task()` also ~**1 ms** when USB+BT on.

---

## Hardware test checklist

Use after **any** change touching Bluetooth, flash layout, `main.cpp` timing, or BTstack/bluepad32 pins:

- [ ] **Multi-device BT:** KB + mouse connected, then pair Xbox/Stadia — KB/mouse still work on ST
- [ ] Xbox BT — fresh pair + reboot reconnect
- [ ] Stadia BT — fresh pair + reboot reconnect
- [ ] DualSense / Switch BT — pair + IKBD responds (not frozen)
- [ ] Xbox/Stadia USB
- [ ] Mouse feel (slow move + fast flick)
- [ ] Mount splash ~5 s, no OLED flicker during splash
- [ ] Keyboard / serial to Atari ST
- [ ] NVSettings survive reboot (if flash touched)
- [ ] Clear BT keys (splash right button) — re-pair works

---

## Archive — cherry-pick sprint (June 2026)

**Status:** Complete — merged to `main` @ v21.1.0.  
**Baseline:** `ebafed7` (v21.0.5, Xbox/Stadia BT OK). **Avoided tip:** `170ac75` (v21.0.7).

### Progress log

| Step | Change | BT (Xbox/Stadia) | Notes |
|------|--------|------------------|-------|
| ✅ | `7a2c46e` — dual-core + build (no TLV/NVSettings) | OK | From `67fa026` |
| ⚠️ | `9f83ed6` — 2 ms USB+HID | **Hang** | Reverted to 10 ms |
| ✅ | `420e8a4` — non-blocking mount splashes | OK | |
| ✅ | `e92dc4e` — atomic cross-core state | OK | |
| ⏭️ | `c07ad1a` — 2 ms `tuh_task` + splash hold | skipped | Redundant with current loop |
| ✅ | `204d8b9` — mouse delta accumulation | OK | |
| ✅ | `4f42d8e` — stable splash + mouse tuning | OK | |

### Commits between working and broken (historical)

```
170ac75  chore: release v21.0.7
4f42d8e  fix: stable mount splash, mouse tuning (P1)
204d8b9  perf: accumulate USB mouse deltas (P1)
c07ad1a  fix: restore mouse speed, 2s splash (P1)
e92dc4e  fix: atomic joystick/mouse state (P1)
420e8a4  perf: non-blocking USB mount splashes (P1)
9f83ed6  perf: poll USB HID every 2ms (P1)  ← BT unsafe
67fa026  fix: P0 correctness, persistent BT (v21.0.6)  ← split, don't wholesale
ebafed7  chore: v21.0.5  ← WORKING BASELINE
```

Detail on each commit: see git history or pre-rename `CHERRY_PICK_REGRESSION.md` in older checkouts.

---

## Archive — flash layout sprint (merged @ v21.1.2)

| Step | Change | Version |
|------|--------|---------|
| **1** | Board-aware `NVSettings` below BTstack bank; migrate from `0x1FF000` | — |
| **2** | Persistent BT pairing (TLV flash); clear keys via splash right btn | v21.1.1 |
| **3** | Core 1 resume for all BT device types after `on_device_ready` | v21.1.2 |

### Flash map

| Board | BTstack bank (SDK default) | NVSettings sector |
|-------|---------------------------|-------------------|
| 2 MiB (Pico W) | `0x1FE000`–`0x1FFFFF` (8 KiB) | `0x1FD000` |
| 4 MiB (Pico 2 W) | `0x3FD000`–`0x3FFFFF` | `0x3FC000` |

Legacy `0x1FF000` overlapped the BT bank on 2 MiB parts and was wrong on 4 MiB parts.

---

## References

- Original Xbox/Stadia BT fix: commit `4047c52` (Dec 2025), RAM-only keys era
- [pico/flash.h](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_flash/include/pico/flash.h) — `flash_safe_execute_core_init()` on Core 1
- Local deep-dive: `docs/OPTIMIZATION_REVIEW.md` (gitignored) — flash layout §2, BT Core 1 resume §3
