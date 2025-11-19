/*
 * Atari ST RP2040 IKBD Emulator
 * Copyright (C) 2021 Roy Hopkins
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#pragma once

// GPIO assignments for display
#define SSD1306_SDA         8
#define SSD1306_SCL         9
#define SSD1306_I2C         i2c0
#define SSD1306_ADDR        0x3c
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64

// GPIO assignments for UI buttons
#define GPIO_BUTTON_LEFT    18
#define GPIO_BUTTON_MIDDLE  17
#define GPIO_BUTTON_RIGHT   16

// GPIO assignments for serial connection to Atari ST
#define UART_TX             4
#define UART_RX             5
#define UART_DEVICE         uart1

// Joystick 1
#define JOY1_UP             10
#define JOY1_DOWN           11
#define JOY1_LEFT           12
#define JOY1_RIGHT          13
#define JOY1_FIRE           14

// Joystick 0
#define JOY0_UP             19
#define JOY0_DOWN           20
#define JOY0_LEFT           21
#define JOY0_RIGHT          22
#define JOY0_FIRE           26

// Performance tuning
// Default CPU clock speed in kHz
// 150000 = 150MHz (default/safe)
// 270000 = 270MHz (maximum performance)
#define DEFAULT_CPU_CLOCK_KHZ   270000

// Debug features (set to 0 to disable for production)
// Can be overridden by CMake with -DENABLE_DEBUG=1
#ifndef ENABLE_DEBUG
  #define ENABLE_DEBUG 0
#endif

// HD6301 emulation speed multiplier (1 = stock timing)
#ifndef HD6301_OVERCLOCK_NUM
  #define HD6301_OVERCLOCK_NUM 1
#endif

// Enables detailed Xbox/PS4/HID diagnostic counters on USB Debug page
#define ENABLE_CONTROLLER_DEBUG ENABLE_DEBUG

// Switch controller debug logging (verbose printf messages)
// Disabling this improves USB performance, especially for mouse responsiveness
#define ENABLE_SWITCH_DEBUG ENABLE_DEBUG

// Stadia controller debug displays (OLED screens during detection)
#define ENABLE_STADIA_DEBUG ENABLE_DEBUG

// ============================================================================
// SPEED MODE OPTIMIZATIONS
// Can be overridden by CMake for custom builds
// ============================================================================

// OLED Display Toggle
// Set to 0 to disable OLED display for maximum performance (speed mode)
// Set to 1 to enable OLED display and UI (standard mode)
#ifndef ENABLE_OLED_DISPLAY
  #define ENABLE_OLED_DISPLAY 1
#endif

// Serial Debug Logging Toggle
// Set to 0 to disable verbose logging for maximum performance
// Set to 1 to enable detailed logging (helps with troubleshooting)
#ifndef ENABLE_SERIAL_LOGGING
  #define ENABLE_SERIAL_LOGGING 1
#endif

// If the OLED display is disabled, also disable all OLED-based debug displays
#if !ENABLE_OLED_DISPLAY
  #undef ENABLE_CONTROLLER_DEBUG
  #define ENABLE_CONTROLLER_DEBUG 0

  #undef ENABLE_SWITCH_DEBUG
  #define ENABLE_SWITCH_DEBUG 0

  #undef ENABLE_STADIA_DEBUG
  #define ENABLE_STADIA_DEBUG 0
#endif
