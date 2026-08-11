# Bluetooth pairing hangs — handoff notes (Pico / Bluepad32 / dual-core)

**Audience:** LLM or developer working on another **ultramegausb** Pico 2 W HID adapter that sees **random Bluetooth pairing hangs** while keyboards/mice work until a gamepad pairs.

**Applies to:** Any sibling project with the same architecture — e.g. **Atari ST IKBD**, **Amiga** keyboard/joystick USB/BT bridge, **Apple ADB** mouse/keyboard adapter — not one host protocol only.

**Reference implementation:** This repo (`ultramegausb-atari-st-rpikbd`). Fixes shipped in **v22.1.0** (`RELEASE_NOTES.md` §22.1.0). The Amiga firmware line shares the same Bluepad32 + dual-core + OLED UI patterns (`docs/UI_UNIFICATION.md` references Amiga as the UI design source).

---

## Ultramegausb family — what is usually the same

| Layer | Typical across adapters | Varies per product |
|-------|-------------------------|-------------------|
| **MCU / radio** | Raspberry Pi Pico 2 W, CYW43 BLE | Pico W (tighter RAM) |
| **Core 0** | TinyUSB host, Bluepad32, OLED UI, main loop | Input routing, shortcuts |
| **Core 1** | Tight timing loop from **XIP flash** | Atari: HD6301 emulator; Amiga: CIA/VIA bit-bang or protocol state machine; ADB: ADB bus timing |
| **BT stack** | pico-sdk BTstack + Bluepad32 platform callbacks | Device-type storage layout |
| **Flash** | BTstack TLV pairing keys + user **NVSettings** | Sector addresses per board |
| **Symptoms** | KB/mouse OK until BLE gamepad pairs; intermittent hang | Which gamepads trigger long pair |

If your adapter uses **dual-core + Core 1 in XIP + Bluepad32 on Core 0**, treat this document as directly applicable even when Core 1 is not an HD6301.

---

## Symptom pattern (what users reported)

- BLE **keyboard + mouse** work reliably.
- Pairing a **BLE HID gamepad** (Google Stadia, Xbox Wireless — CoD **0x0508**, HID-over-GATT) while KB/mouse are connected can:
  - **Hang the whole adapter** (OLED/UI frozen too → Core 0 blocked, not just Core 1).
  - Stop keyboard/mouse reaching the **host** (Atari serial, Amiga clock/data lines, ADB bus) even though the UI still updates.
- **Intermittent / “random”** — classic race with flash and multicore timing.
- **PS5 DualSense (BLE)** often unaffected; problem clustered on **longer pairing paths** (Xbox/Stadia bonding + TLV flash writes).
- Bug **vanished or moved** when verbose `printf` / `logi` was added (Heisenbug).

---

## Architecture that makes this possible

| Piece | Role |
|-------|------|
| **Core 0** | TinyUSB, Bluepad32/BTstack, OLED, main loop |
| **Core 1** | Timing-critical work from **XIP flash** (emulator, protocol engine, or bus bit-bang) — typically **no `sleep`** in the inner loop |
| **BTstack TLV** | Pairing keys / persistence written via **`flash_safe_execute()`** |
| **CYW43** | WiFi/BT chip; flash access must coordinate with **both cores** |

If Core 1 keeps executing from flash while Core 0/BTstack erases/writes flash, you get **stalls, corruption, or permanent freeze**. `flash_safe_execute()` tries to pause the other core — but only if that core cooperates.

**Amiga / ADB note:** You do not need an HD6301 on Core 1 for this failure mode. Any Core 1 loop that **runs code from XIP** during BT pairing is in the same risk class.

---

## Root causes we confirmed

### 1. Core 1 not enrolled in flash-safe multicore

**Fix:** Call `flash_safe_execute_core_init()` at the **start of Core 1** before any XIP work.

Without this, BT pairing flash writes can freeze Core 1 or fail silently.

**Atari reference:** `src/main.cpp` → `core1_entry()`.  
**Porting:** First instruction block on your secondary core entry (whatever runs the Amiga/ADB timing path).

### 2. Core 1 still in XIP during pairing flash writes

**Fix:** **Pause Core 1** before BTstack writes pairing data, resume only after enumeration completes.

- Pause on **gamepad discovery** (CoD `0x0508` or name match `Stadia` / `Xbox`), not on every BLE device.
- **BLE keyboards/mice** use a shorter path — we do **not** pause Core 1 for them.
- Core 1 pause loop must **`__wfe()`** (not busy-spin forever) so multicore lockout / `flash_safe_execute` can preempt.

**Atari reference:** `src/main.cpp` (`g_core1_paused`, `CORE1_PHASE_PAUSED`), `src/bluepad32_platform.c` → `my_platform_on_device_discovered()`.  
**Porting:** Same hooks in your `bluepad32_platform.c` (or equivalent Bluepad32 platform file).

### 3. Single bool pause flag → refcount bugs

**Symptom:** Double-pause on `connected` + single resume on `ready` left **`pause_depth > 0`** forever, or resumed too early.

**Fix:** **Refcounted** `g_core1_pause_depth`:
- `core1_pause_for_bt_enumeration()` → `++depth`
- `core1_resume_after_bt_enumeration()` → `--depth` (only if depth > 0)
- Pause **once** on discovery; **do not pause again** in `on_device_connected`.
- Resume **once** in `on_device_ready` (or on disconnect if pairing aborted).

**Atari reference:** `src/main.cpp` — `core1_pause_for_bt_enumeration`, `core1_resume_after_bt_enumeration`.

### 4. `sleep_ms()` / `__wfe()` inside Bluepad32 callbacks froze Core 0

**Symptom:** Entire adapter hung including OLED — Core 0 stuck inside BT callback.

**Fix:** In BT platform callbacks use **`busy_wait_us()` only** via a small helper (`bt_callback_busy_wait_ms`). Never `sleep_ms()` or `__wfe()` on Core 0 in those paths.

**Reference:** `src/bluepad32_platform.c` — `bt_callback_busy_wait_ms()`.

### 5. Resume too early (race masked by debug logging)

**Symptom:** Production hang; debug build OK because extra `logi`/`printf` added ~10–50 ms delay.

**Fix:** Explicit delays (tune in project `config.h`):
- `BT_GAMEPAD_DISCOVERY_SETTLE_MS` = **30** — after pausing Core 1 on discovery, before BTstack flash activity.
- `BT_GAMEPAD_CORE1_RESUME_DELAY_MS` = **100** — in `on_device_ready`, before `core1_resume_after_bt_enumeration()`.

Also: `core1_wait_for_pause_active()` — poll until Core 1 reports paused state (busy-wait poll, max ~20 ms).

**Reference:** `src/bluepad32_platform.c` → `my_platform_on_device_ready()`.

### 6. NVSettings / custom flash overlapping BTstack TLV bank

Separate issue but same “random BT” class: **user flash sector must not overlap** the BTstack pairing TLV region. Board-aware sector layout.

**Atari reference:** `src/NVSettings.cpp`.  
**Porting:** Re-validate flash map for **your** board (`pico` vs `pico2_w` sectors differ).

### 7. Main-loop timing experiments broke pairing

Cherry-picking **2 ms** USB/HID polling (e.g. calling input handlers twice per tick) caused **Stadia/Xbox pairing hangs**. Reverted to **10 ms** HID block + **1 ms** `bluepad32_poll()`.

Do not speed up Core 0 HID without retesting **multi-device BT pairing** (KB + mouse + gamepad).

**Reference:** `docs/FUTURE_WORK.md` — “Known regressions”; `AGENTS.md` — Core 0 timing table.

### 8. BTstack version pin

Upgrading BTstack past **pico-sdk v1.6.2** (`hids_client` → `hids_host` in v1.8+) broke compile or Xbox/Stadia paths. **Stay on SDK-pinned BTstack** unless you port `pico_btstack` CMake + Bluepad32 HID client.

**Reference:** `docs/FUTURE_WORK.md` — “BTstack upgrade experiment”.

---

## What did *not* cause the hang (ruled out)

- Map Devices OLED / USB device name registry / UI refresh on connect (Atari v21.1.3+; Amiga uses the same UI pattern).
- Stadia vs Xbox being “classic BR/EDR only” on Pico 2 W — both were **BLE HID gamepads** in captures.
- Core 1 “frozen” with `pause_depth=0` — work stuck **inside** the Core 1 hot path (XIP), not in the pause spin; diagnose with phase/program-counter breadcrumbs.

---

## Checklist for a new Pico HID host (Amiga, ADB, Atari, …)

### Boot / multicore

- [ ] `flash_safe_execute_core_init()` on **secondary core** entry (first line of Core 1).
- [ ] Secondary core loop: when paused, **`__wfe()`** in pause branch.
- [ ] If secondary core runs from XIP: wireless build often needs **XIP** (RAM tight); USB-only may use `copy_to_ram`.

### Bluetooth platform callbacks

- [ ] **No** `sleep_ms()` / `__wfe()` on Core 0 in Bluepad32/BTstack callbacks.
- [ ] Delays: **`busy_wait_us()`** only.
- [ ] **Refcounted** Core 1 pause (not a single `bool`).
- [ ] Pause on **gamepad discovery** (or your long-pairing device class); resume on **`device_ready`** with **post-ready delay** before resume.
- [ ] On **disconnect during pairing**, resume if `pause_depth > 0`.
- [ ] Do **not** double-pause on `connected` if you already paused on `discovered`.

### Flash layout

- [ ] NVSettings / user config sector **below** BTstack TLV bank; board-specific addresses (Pico W vs Pico 2 W differ).

### Clock / RF

- [ ] BT builds: **225 MHz** system clock for CYW43 stability (270 MHz caused stalls on Atari; verify on Amiga/ADB if you use a different default).
- [ ] USB-only builds: 270 MHz typically OK.

### Core 0 main loop

- [ ] Document and keep stable: HID handler **call frequency** and **once-per-tick** rules.
- [ ] `bluepad32_poll()` ~1 ms; avoid blocking Core 0 for tens of ms during pair.
- [ ] If you have separate USB + BT input paths, guard each handler so it runs **once** per tick when either transport is enabled (Atari wheel regression: double `handle_keyboard()`).

### Host-specific smoke tests

| Product | After pairing gamepad with KB+mouse connected, verify |
|---------|--------------------------------------------------------|
| **Atari ST** | Serial IKBD: keys, mouse, joysticks still work |
| **Amiga** | Clock/data: keyboard, mouse, joystick ports still work |
| **Apple ADB** | ADB devices still enumerate and stream events |

Common to all: reboot → bonded devices reconnect; clear pairing keys → fresh pair works.

### Diagnostics (build with serial logging enabled)

Add before guessing:

| Signal | Meaning |
|--------|---------|
| `pause_depth` | Should return to **0** after `device_ready` |
| Core 1 `phase` | `PAUSED` during pair; active phase when running protocol/emulator |
| Program counter at hot-path entry | If frozen in active phase and cycle counter not advancing → stuck in XIP/flash |
| Heartbeat “frozen” flag | Secondary core cycle/loop counter unchanged over ~10 s |
| `BT(kb=… mouse=… joy=…)` | Device counts still updating when hang is “soft” |

Gate verbose `[DIAG]` logs behind a compile flag — they **change timing** and can hide races.

---

## Where to look in the Atari reference tree

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | Core 1 loop, pause/refcount, `flash_safe_execute_core_init`, heartbeat diagnostics |
| `src/bluepad32_platform.c` | Discovery pause, ready resume + delays, `busy_wait` only, BT storage getters |
| `include/config.h` | `BT_GAMEPAD_DISCOVERY_SETTLE_MS`, `BT_GAMEPAD_CORE1_RESUME_DELAY_MS`, timing constants |
| `src/NVSettings.cpp` | Flash sector layout vs BTstack TLV |
| `CMakeLists.txt` | XIP for wireless, `copy_to_ram` for USB-only |

**Amiga / ADB porting:** Expect the same filenames or close equivalents (`bluepad32_platform.c`, `main.cpp` multicore split, `NVSettings`). Diff your Core 1 entry and platform callbacks against this tree first.

---

## Suggested porting order

1. Confirm **dual-core + XIP on Core 1** — if yes, flash-safe path is mandatory.
2. Add `flash_safe_execute_core_init()` on Core 1 if missing.
3. Implement **refcounted pause** + discovery / ready / disconnect hooks in Bluepad32 platform code.
4. Replace any `sleep_ms` in BT callbacks with `busy_wait_us`.
5. Tune **settle** (30 ms) and **resume delay** (100 ms); adjust per hardware if still flaky.
6. Run hardware matrix: **KB + mouse connected → pair gamepad → host I/O still works**; reboot reconnect; clear pairing keys.
7. Only then tune Core 0 poll rates or BTstack versions.

---

## References in this repo (Atari)

| Document | Content |
|----------|---------|
| `RELEASE_NOTES.md` §22.1.0 | User-facing fix summary |
| `docs/FUTURE_WORK.md` §Xbox/Stadia BT regression | Root cause + BTstack experiment |
| `docs/TECHNICAL_NOTES.md` | Current timing, flash, pause behaviour |
| `docs/UI_UNIFICATION.md` | Shared OLED UI model (Amiga ↔ Atari) |
| `AGENTS.md` | Core 0 timing, BT callback rules |

---

## One-paragraph summary for paste into another LLM session

> **Context:** ultramegausb Pico 2 W HID adapters (Atari ST, **Amiga**, **Apple ADB**, same Bluepad32 stack). **Problem:** random BLE gamepad pairing hangs while KB/mouse work until a gamepad pairs. **Cause:** multicore **flash race** — BTstack writes pairing TLV via `flash_safe_execute` while Core 1 still runs from **XIP flash** (emulator or bus timing loop). **Fix:** `flash_safe_execute_core_init()` on Core 1; **refcounted** Core 1 pause on gamepad discovery; resume in `device_ready` after **~100 ms `busy_wait_us`**; **~30 ms settle** after pause; Core 1 pause loop uses **`__wfe()`**; BT callbacks use **`busy_wait_us` only** (never `sleep_ms`/`__wfe` on Core 0); no double-pause on connect; NVSettings must not overlap BTstack flash; keep BTstack at pico-sdk pin; don’t double-call HID handlers per tick or drop to 2 ms HID without retesting pair. Debug `printf` masked the bug — use explicit delays and Core 1 phase/PC diagnostics. **Reference:** `ultramegausb-atari-st-rpikbd` `docs/BT_PAIRING_HANDOFF.md`, v22.1.0.
