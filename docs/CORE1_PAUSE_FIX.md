# Core 1 Pause Fix for Xbox/Stadia Bluetooth Gamepad Enumeration

## Summary

This fix prevents Core 1 from freezing when Xbox or Stadia controllers connect via Bluetooth. The freeze was occurring during the GATT service discovery phase, which happens after pairing but before the device is marked as "ready".

## Solution

**Core 1 is paused during Bluetooth gamepad enumeration** to prevent flash access conflicts that cause the freeze. The pause happens early (when device is discovered) and resumes after a minimal 10ms delay following enumeration completion.

## Changes Made

### 1. Core 1 Pause/Resume Mechanism (`src/main.cpp`)

**Added:**
- `g_core1_paused` flag to control Core 1's execution
- `core1_pause_for_bt_enumeration()` function to pause Core 1
- `core1_resume_after_bt_enumeration()` function to resume Core 1
- Pause check in Core 1's main loop (skips emulation when paused)

**Location:** `src/main.cpp` lines ~163-175 and ~197-203

### 2. Early Pause on Device Discovery (`src/bluepad32_platform.c`)

**Added to `my_platform_on_device_discovered()`:**
- Detects gamepads by COD (0x0508) or name ("Stadia", "Xbox", "XBOX")
- Pauses Core 1 immediately when gamepad is discovered
- This happens BEFORE connection attempt, covering the entire enumeration phase

**Location:** `src/bluepad32_platform.c` lines ~144-168

### 3. Ensure Pause on Connection (`src/bluepad32_platform.c`)

**Added to `my_platform_on_device_connected()`:**
- Checks vendor ID to identify Xbox (0x045E) or Stadia (0x18D1)
- Ensures Core 1 is paused (may have been paused earlier)
- Covers the GATT service discovery phase where freeze occurs

**Location:** `src/bluepad32_platform.c` lines ~170-186

### 4. Resume After Enumeration (`src/bluepad32_platform.c`)

**Added to `my_platform_on_device_ready()`:**
- Detects Xbox/Stadia gamepads by vendor/product ID
- Waits 10ms after "gamepad ready" before resuming Core 1
- Ensures all BTStack operations complete before Core 1 resumes

**Location:** `src/bluepad32_platform.c` lines ~234-260

### 5. Safety Resume on Disconnect (`src/bluepad32_platform.c`)

**Added to `my_platform_on_device_disconnected()`:**
- Ensures Core 1 is resumed if device disconnects during enumeration
- Safety check in case resume wasn't called

**Location:** `src/bluepad32_platform.c` lines ~191-194

## How It Works

1. **Device Discovered** → Core 1 paused immediately (if gamepad detected)
2. **Device Connecting** → Core 1 still paused (ensured for Xbox/Stadia)
3. **Pairing** → Core 1 still paused
4. **GATT Service Discovery** → Core 1 still paused (prevents freeze here)
5. **Device Ready** → Core 1 still paused
6. **Wait 10ms** → Core 1 still paused (safety buffer)
7. **Resume Core 1** → Normal operation resumes

## Key Insight

The freeze was happening during **GATT service discovery** (between "Device Information service found" and "GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED"), which occurs AFTER pairing but BEFORE the device_ready callback. By pausing Core 1 when the device is discovered (much earlier), we cover this entire phase.

## Testing

- ✅ Stadia controller: Works with 10ms pause
- ✅ Xbox controller: Works with 10ms pause
- ✅ Other gamepads: No impact (pause only for Xbox/Stadia)

## Applying to Other Branches

To apply this fix to another branch (e.g., `feature/bluetooth-irq`):

1. **Add Core 1 pause/resume functions** to `src/main.cpp`:
   - Add `g_core1_paused` flag
   - Add `core1_pause_for_bt_enumeration()` and `core1_resume_after_bt_enumeration()` functions
   - Add pause check in Core 1's main loop

2. **Modify `src/bluepad32_platform.c`**:
   - Add forward declarations at top
   - Add pause logic to `my_platform_on_device_discovered()`
   - Add pause logic to `my_platform_on_device_connected()`
   - Add resume logic to `my_platform_on_device_ready()` (with 10ms delay)
   - Add safety resume to `my_platform_on_device_disconnected()`

3. **No other changes needed** - this fix is independent of:
   - TLV configuration changes
   - Code moved to RAM
   - BTStack memory database changes
   - Other optimizations

## Files Modified

- `src/main.cpp` - Core 1 pause/resume mechanism
- `src/bluepad32_platform.c` - Pause/resume calls during BT enumeration

## Version

Applied in: `bluepad-develop-exp` branch
Target: `feature/bluetooth-irq` branch


