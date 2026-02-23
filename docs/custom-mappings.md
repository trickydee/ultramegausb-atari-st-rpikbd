# Custom Controller→Atari Mapping Reference

This document captures how each supported USB controller is currently translated into the single Atari ST joystick (4-way + fire). It can be used as the starting point for future customizable mappings.

## Direction and Fire Mapping Summary

| Controller | Direction priority | Fire inputs used today | File / section |
| --- | --- | --- | --- |
| **PlayStation 4 (DualShock 4)** | D-pad overrides, left stick fallback with dead zone | Cross (X) primary, R2 trigger (>50%) alternate | `ps4_to_atari()` in `src/ps4_controller.c` |
| **PlayStation 5 (DualSense / Edge)** | D-pad overrides, left stick fallback with dead zone | Cross primary, R2 trigger (>200) alternate | `ps5_to_atari()` in `src/ps5_controller.c` |
| **PlayStation Classic (PSC)** | D-pad only (3-byte report, no sticks) | Cross primary, R2 alternate | `psc_to_atari()` in `src/psc_controller.c` |
| **PlayStation 3 (DualShock 3)** | Same pattern: D-pad hat first, left stick fallback | Cross (X) only | `ps3_to_atari()` in `src/ps3_controller.c` |
| **Xbox / XInput (Xbox One/360/OG)** | D-pad first, left stick fallback (±8000 dead zone) | A button primary, Right Trigger (>50%) alternate | `xinput_to_atari_joystick()` in `src/xinput_atari.cpp` |
| **Nintendo Switch Pro / PowerA Arcade** | D-pad first (POV hats/arcade), left stick fallback | B primary (south), A secondary, ZR trigger alternate | `switch_to_atari()` in `src/switch_controller.c` |
| **HORI HORIPAD (Switch)** | D-pad overrides, left stick (axis_x/y) fallback with dead zone | B primary, R2 alternate; Llamatron: A = Joy 0 fire | `horipad_to_atari()` in `src/horipad_controller.c` |
| **Google Stadia Controller** | D-pad first, left stick fallback | A/B/X/Y all map to fire; R1/R2 also count | `stadia_to_atari()` in `src/stadia_controller.c` |
| **Nintendo GameCube (USB adapter)** | Left stick with dead zone, D-pad overrides | A primary, B alternate | `gc_to_atari()` in `src/gamecube_adapter.c` |
| **Generic HID Joysticks** | Depends on submitted report (hat or axes) | Any reported button bit or trigger > threshold | `get_usb_joystick()` fallback in `src/HidInput.cpp` |

## Face-Button Equivalency Cheat Sheet

| Physical location | Xbox | PlayStation | Nintendo (Switch / GameCube) | Stadia | Currently used as fire? |
| --- | --- | --- | --- | --- | --- |
| South (bottom) | **A** | **Cross (X)** | **B** (Switch) / **A** (GC) | **A** | Yes – primary fire on all |
| East (right) | B | Circle | A (Switch) / B (GC) | B | Switch, Stadia, GameCube use as alternate fire |
| North (top) | Y | Triangle | X | Y | Only Stadia today |
| West (left) | X | Square | Y | X | Only Stadia today |
| Right trigger | RT | R2 | ZR | R2 | Xbox, PS4, Switch, Stadia accept it as fire |

These tables reflect the firmware state at version 21.0.4 and should be updated if additional controllers or custom-mapping features are implemented.

