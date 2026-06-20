# UI unification (Atari ST)

**Last updated:** June 2026  
**Branch / context:** `feature/ui-alignment` (from `main` @ v21.1.2+)  
**Design reference:** ultramegausb Amiga firmware OLED UI (Devices + Map Devices page structure)  
**Audience:** Developers continuing this work (including follow-up LLM sessions)

---

## Purpose

This document records:

1. **Completed work** — aligning the Atari ST IKBD OLED UI with the Amiga product line.
2. **Completed work** — surfacing correct USB and Bluetooth device names on the **Map Devices** page.
3. **Planned work** — extending Map Devices button controls to **cycle gamepad assignments** per joystick port (J1 / J2).

---

## Production page flow (after alignment)

| Order | Page enum | Title on OLED | Role |
|-------|-----------|---------------|------|
| 0 | `PAGE_SPLASH` | ATARI / version / mode | Unchanged home screen; L = USB/BT mode cycle; R = delete BT pairing keys |
| 1 | `PAGE_DEVICES` | **Devices** | Connection counts: `Keybd` / `Mouse` / `Game` with **U** (USB) and **BT** columns; mouse speed bar |
| 2 | `PAGE_MAPPING` | **Map Devices** | Per-port mapping labels (J2, J1, K1, M1); L/R toggles DSub vs USB/BT for joysticks |
| → | (wrap) | `PAGE_SPLASH` | Middle button advances page |

Debug builds add extra pages (`PAGE_SERIAL`, `PAGE_USB_DEBUG`, `PAGE_PRO_INIT`) beyond `PAGE_MAPPING`.

**Key files:** `include/UserInterface.h`, `src/UserInterface.cpp` (`on_button_down`, page cycle in `BUTTON_MIDDLE` handler).

---

## Completed: UI alignment with Amiga

### Goals

| Amiga page | Atari target | Status |
|------------|--------------|--------|
| Home / splash | Keep ATARI splash as-is | Done |
| **Devices** — `Keybd` / `Mouse` / `Game` with U and BT counts | Replace old USB-only status page | Done |
| **Map Devices** — J2/J1/K1/M1 + DSub toggle | Replace separate Joy0 / Joy1 pages | Done |

### Page model changes

**Before:** separate pages for mouse status, joystick 0, joystick 1 (`PAGE_MOUSE`, `PAGE_JOY0`, `PAGE_JOY1` — removed).

**After:**

- `PAGE_DEVICES` — `update_devices()` draws Amiga-style count lines plus mouse speed (L/R on this page adjusts speed).
- `PAGE_MAPPING` — `update_mapping()` draws routing labels; L/R toggle DSub physical joystick vs USB/BT (`joy_device` bitfield in flash).

### Device count API

`UserInterface::device_connect_state(usb_kb, usb_mouse, usb_joy, bt_kb, bt_mouse, bt_joy)` replaces the old USB-only `usb_connect_state()`.

**`HidInput.cpp` fix:** Bluetooth keyboards and mice were previously mixed into USB `kb_count` / `mouse_count`. Counts are now split:

- USB side: `kb_count`, `mouse_count`, `joy_count + xinput_joy_count`
- BT side: `bluepad32_get_keyboard_count()`, `bluepad32_get_mouse_count()`, `bluepad32_get_connected_count()`

`notify_ui_device_counts()` calls `device_connect_state()` with both sides.

### Joystick routing labels (display only)

Display order matches **input routing** in `HidInput::handle_joystick()`:

| OLED label | Atari port | `joy_device` bit | BT gamepad index | USB map slot |
|------------|------------|------------------|------------------|--------------|
| **J2** | Joystick 1 | bit 1 | 0 (first BT pad) | slot 0 (first USB pad) |
| **J1** | Joystick 0 | bit 0 | 1 (second BT pad) | slot 1 (second USB pad) |

First connected gamepad routes to joystick 1, second to joystick 0 (leaves joystick 0 free for mouse when needed).

### Map Devices button behaviour (current)

On `PAGE_MAPPING`:

- **Left** — toggle J2 (joy1): DSub physical ↔ USB/BT (`joy_device` bit 1 XOR).
- **Right** — toggle J1 (joy0): DSub physical ↔ USB/BT (`joy_device` bit 0 XOR).

Settings persist via `NVSettings::write()` (`include/NVSettings.h`, `joy_device` byte).

### Files touched for UI alignment

| File | Change |
|------|--------|
| `include/UserInterface.h` | New page enums; `device_connect_state()`; `invalidate()` |
| `src/UserInterface.cpp` | `update_devices()`, `update_mapping()`, button handlers, production page cycle |
| `src/HidInput.cpp` | Split USB/BT counts; `hid_request_ui_refresh()` |
| `src/bluepad32_platform.c` | Device names, UI refresh on connect/disconnect |
| `include/bluepad32_platform.h` | `bluepad32_get_device_name()` |

---

## Completed: Map Devices — USB and BT name surfacing

### Problem

The mapping page originally showed Bluetooth-oriented labels only. USB controllers either showed nothing, a generic **USB Gamepad** label, or the wrong name depending on mount order and driver path.

### Solution overview

Two parallel name sources feed `update_mapping()`:

1. **Bluetooth** — `bluepad32_get_device_name('J'|'K'|'M', idx)` from `bluepad32_platform.c`.
2. **USB** — `usb_device_map.c` registry with labels set at mount time.

**Display priority per row:** DSub override → Bluetooth name → USB name → `--`.

### USB device map module

**New files:**

- `include/usb_device_map.h`
- `src/usb_device_map.c` (listed in `CMakeLists.txt`)

**API:**

| Function | Purpose |
|----------|---------|
| `usb_map_register_gamepad(dev_addr, name)` | Add/update gamepad in slot 0 or 1 (connection order); compacts on unmount |
| `usb_map_unregister_gamepad(dev_addr)` | Remove by USB device address |
| `usb_map_gamepad_registered(dev_addr)` | True if this address already has a map entry |
| `usb_map_get_gamepad(slot)` | Name for slot 0 or 1 |
| `usb_map_set_keyboard` / `usb_map_clear_keyboard` | Single USB keyboard label |
| `usb_map_set_mouse` / `usb_map_clear_mouse` | Single USB mouse label |

Each register/clear calls `hid_request_ui_refresh()` → `UserInterface::invalidate()` so the Map Devices page redraws when labels change without a count change.

### Where USB names are registered

| Source | Mount | Label examples |
|--------|-------|----------------|
| `HidInput.cpp` `tuh_hid_mounted_cb` | Generic HID keyboard/mouse/joystick | `USB Keyboard`, `USB Mouse`, `USB Gamepad` |
| `stadia_controller.c` | Stadia | `Stadia` |
| `ps3_controller.c` | PS3 | `PS3` |
| `ps4_controller.c` | PS4 | `PS4` |
| `ps5_controller.c` | PS5 | `PS5` |
| `psc_controller.c` | PlayStation Classic | `PSC` |
| `switch_controller.c` | Switch / PowerA | Controller-type string (e.g. `Pro Controller`) |
| `horipad_controller.c` | HORIPAD | `HORIPAD` |
| `gamecube_adapter.c` | GC USB adapter | `GameCube` |
| `main.cpp` `tuh_xinput_mount_cb` | Xbox (XInput path) | `Xbox One`, `Xbox 360`, `Xbox` |
| `xinput.c` | Legacy `xinput_mount_cb` | `Xbox` (dead path — XInput host uses `main.cpp` callback) |

Corresponding `*_unmount_cb` handlers call `usb_map_unregister_gamepad()`.

### Bluetooth name surfacing

**`bluepad32_platform.c` changes:**

- `name[32]` on `bt_gamepad_storage_t`, `bt_keyboard_storage_t`, `bt_mouse_storage_t`.
- Pending names keyed by BD_ADDR during discovery (`store_pending_name_by_addr`) so names are available before the device is fully ready.
- `notify_ui_device_update()` on connect, disconnect, and ready events.
- `bluepad32_get_device_name('J'|'K'|'M', idx)` for the Map Devices page.

### Bugs found and fixed during USB labelling

#### 1. Specific controller name overwritten by generic label

**Symptom:** PS5 (and PS3/PS4/PSC/Switch/HORIPAD) showed `USB Gamepad` instead of `PS5`.

**Cause:** In `hid_app_host.c`, driver-specific `*_mount_cb()` runs **before** `tuh_hid_mounted_cb()`. The mount callback registered the correct name, then `tuh_hid_mounted_cb()` in `HidInput.cpp` always registered `USB Gamepad` for the same `dev_addr`, overwriting it.

Stadia worked because `stadia_mount_cb()` is invoked **inside** `tuh_hid_mounted_cb()` after the generic registration, so it overwrote in the correct direction.

**Fix:** Only register `USB Gamepad` when `!usb_map_gamepad_registered(actual_addr)`.

#### 2. Xbox showed nothing on Map Devices but worked in-game

**Symptom:** Xbox controller input worked; Map Devices row was `--`.

**Cause:** Xbox uses the **XInput vendor-class path** (`tuh_xinput_mount_cb` in `main.cpp`), not the HID joystick path. `usb_map_register_gamepad()` lived in `xinput.c` → `xinput_init_controller()`, but `xinput_mount_cb()` is never called from the live XInput host stack.

**Fix:** Register/unregister in `main.cpp`:

- `tuh_xinput_mount_cb` → `usb_map_register_gamepad(dev_addr, map_name)`
- `tuh_xinput_umount_cb` → `usb_map_unregister_gamepad(dev_addr)`

### `update_mapping()` display logic (reference)

```text
J2:  DSub Phy | <BT name slot 0> | <USB name slot 0> | --
J1:  DSub Phy | <BT name slot 1> | <USB name slot 1> | --
K1:  <BT keyboard 0> | <USB keyboard> | --
M1:  <BT mouse 0> | <USB mouse> | --
```

Implementation: `src/UserInterface.cpp` → `UserInterface::update_mapping()`.

---

## Planned: Cycle gamepad devices on J1 / J2

### User story

On the **Map Devices** page, the same L/R buttons that today toggle **DSub physical** vs **USB/BT** should also allow **cycling which connected gamepad** is bound to each Atari joystick port — e.g. when three or more pads are connected (USB + BT mixed).

### Current limitation (why this is not trivial)

Input routing in `HidInput::handle_joystick()` is **automatic**, not user-selected:

- USB: walks a priority chain (HID joystick list → PS4 → PS5 → PSC → PS3 → GameCube → Switch → HORIPAD → Xbox) and always uses the **first** matching device per driver.
- BT: joy1 → `bluepad32` index 0, joy0 → index 1.

Driver helpers (`get_ps5_joystick()`, `get_xbox_joystick()`, etc.) take `joystick_num` (which Atari port to fill) but do **not** select among multiple devices of the same type. There is no per-port “use device X” setting beyond the DSub bit.

The Map Devices page today shows **what auto-routing would use**, not a user-chosen binding.

### Proposed UX (recommended)

Cycle order per port (L = J2, R = J1):

```text
DSub Phy → Auto → <Pad A> → <Pad B> → … → DSub Phy
```

- **Auto** — preserve today’s behaviour (backward compatible default).
- **Pad N** — explicit binding from a runtime catalog.
- Optional: **long press** = jump to DSub, **short press** = cycle pads only.

OLED line shows the **selected** source, not only connected devices.

### Implementation checklist (for ST board)

#### Phase 1 — Catalog and settings (medium)

| Task | Details |
|------|---------|
| **Runtime gamepad catalog** | Unified list: `{type, id, name}` across USB (HID addr, XInput addr, per-driver addr) and BT (bluepad index). Build on mount/unmount; max ~8 entries practical. |
| **Extend `NVSettings`** | Bump `Settings.version` (currently `1`). Add e.g. `joy0_bind`, `joy1_bind`: `0xFF` = DSub, `0xFE` = Auto, `0..N` = catalog index. Migrate flash in `NVSettings::read()`. |
| **UI cycle handler** | Replace XOR toggle in `UserInterface::on_button_down` for `PAGE_MAPPING` with state machine advancing bind value; call `update_mapping()`. |
| **Display** | `update_mapping()` shows selected bind label (including `Auto`, `DSub Phy`, or device name). |

**Files:** `include/NVSettings.h`, `src/NVSettings.cpp`, `src/UserInterface.cpp`, new `gamepad_catalog.c` / `.h` (or extend `usb_device_map`).

#### Phase 2 — Input routing (harder)

| Task | Details |
|------|---------|
| **`read_gamepad(catalog_idx, &axis, &button)`** | Dispatcher by type/id to existing `*_to_atari` / `get_usb_joystick(addr)` paths. |
| **Per-device getters** | Many drivers need `get_*_joystick_by_addr(dev_addr, …)` — today most loops return first connected only. |
| **`handle_joystick()` refactor** | Per port: if DSub → GPIO; if Auto → existing waterfall; else → `read_gamepad(selected)`. |
| **Conflict rules** | Same pad bound to J1 and J2; device unplugged while selected; fall back to Auto or `--`. |
| **Llamatron mode** | `g_llamatron_mode` path in `handle_joystick()` must respect or override explicit binds — define policy. |

**Files:** `src/HidInput.cpp` (main change), per-controller `src/*_controller.c`, `src/xinput_atari.cpp`, `src/bluepad32_platform.c`.

#### Phase 3 — Polish

| Task | Details |
|------|---------|
| Hot-swap | Rebuild catalog on connect/disconnect; clamp invalid bind indices. |
| Persist debounce | Consider debouncing flash writes (`docs/FUTURE_WORK.md` P3 item). |
| Hardware test matrix | USB-only, BT-only, mixed, 2+ pads same family (two PS5), Xbox + Stadia + BT pad. |

### Effort estimate

| Scope | Estimate |
|-------|----------|
| UI + settings + catalog (display only, routing still Auto) | ~1 day |
| Full routing with Auto fallback | ~4–6 days |
| Minimal v1 (swap two pads + DSub only) | ~2–3 days |

### Design decisions to resolve before coding

1. Keep **Auto** in the cycle (recommended) vs force explicit assignment.
2. Independent J1/J2 selection vs “swap assignment” only.
3. Whether BT pads appear in the same catalog as USB or in a fixed order (USB first, then BT).
4. Interaction with **mouse on joy0** (`mouse_enabled` in settings).

---

## Related code map (quick reference)

```
OLED UI
  src/UserInterface.cpp       Pages, buttons, update_devices/mapping
  include/NVSettings.h        joy_device bitfield (DSub vs USB/BT)

USB names
  src/usb_device_map.c        Gamepad slots 0/1, keyboard, mouse
  src/HidInput.cpp            Generic HID mount + notify_ui_device_counts
  src/main.cpp                XInput mount → usb_map_register_gamepad
  src/*_controller.c          Per-driver register on mount

BT names
  src/bluepad32_platform.c    name storage, bluepad32_get_device_name

Input routing (today — automatic)
  src/HidInput.cpp            handle_joystick() ~line 2028+
```

---

## Known open issues (outside this doc’s scope)

| Issue | Notes |
|-------|-------|
| **Stadia BT pairing lock** | Regressed during UI work; deferred. Suspect OLED refresh / `notify_ui_device_update()` during BT enumeration vs Core 1 `flash_safe_execute`. See `docs/FUTURE_WORK.md`, `docs/TECHNICAL_NOTES.md`. |
| **Map Devices shows auto-assignment** | Until Phase 2 above, labels reflect connection order / priority, not user cycle selection. |

---

## Handoff notes for another LLM

1. Read this file, then `src/UserInterface.cpp` (`update_mapping`, `on_button_down`) and `src/HidInput.cpp` (`handle_joystick`).
2. Do **not** change `CYCLES_PER_LOOP`, Atari UART baud (7812), or Core 1 loop structure without explicit approval (`AGENTS.md`).
3. USB map registration must stay **before** generic `USB Gamepad` in `tuh_hid_mounted_cb`, or use `usb_map_gamepad_registered()`.
4. Xbox **must** register via `tuh_xinput_mount_cb` in `main.cpp`, not only `xinput.c`.
5. Run compile check: `BUILD_BOARDS=pico2_w BUILD_VARIANT=production SKIP_VARIANTS=1 CLEAN_BUILD_DIRS=0 ./build-all.sh`
6. Controller checklist after routing changes: `docs/DEVELOPER_GUIDE.md` (detect, map, hot-swap, Llamatron).

---

## See also

| Document | When |
|----------|------|
| `docs/DEVELOPER_GUIDE.md` | Architecture, adding controllers |
| `docs/FUTURE_WORK.md` | BT experiments, deferred optimisations |
| `docs/custom-mappings.md` | Per-controller button mappings |
| `AGENTS.md` | Build commands, agent boundaries |
