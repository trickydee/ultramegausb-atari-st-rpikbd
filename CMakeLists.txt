# Atari ST RP2040 IKDB Emulator
# Copyright (C) 2021 Roy Hopkins
# 
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

cmake_minimum_required(VERSION 3.13...3.27)

# Apply patches for build compatibility
execute_process(
    COMMAND ${CMAKE_SOURCE_DIR}/apply-patches.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE PATCH_RESULT
)
if(NOT PATCH_RESULT EQUAL "0")
    message(WARNING "Failed to apply patches: ${PATCH_RESULT}")
endif()

# Pull in SDK (must be before project)
set(PICO_SDK_PATH ${CMAKE_SOURCE_DIR}/pico-sdk)
include(pico_sdk_import.cmake)

# Board selection: Set PICO_BOARD before calling project()
# Options: pico (RP2040), pico2 (RP2350)
# Usage: cmake -DPICO_BOARD=pico2 .. (for Pico 2)
# Or:    cmake -DPICO_BOARD=pico ..  (for Pico 1, this is the default)
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico" CACHE STRING "Pico board type (pico or pico2)")
    message(STATUS "PICO_BOARD not specified, defaulting to 'pico' (RP2040)")
else()
    message(STATUS "Building for board: ${PICO_BOARD}")
endif()

project(atari_ikbd C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Display what we're building for
if(PICO_PLATFORM STREQUAL "rp2040")
    message(STATUS "Target: Raspberry Pi Pico (RP2040)")
elseif(PICO_PLATFORM STREQUAL "rp2350-arm-s" OR PICO_PLATFORM STREQUAL "rp2350-riscv")
    message(STATUS "Target: Raspberry Pi Pico 2 (RP2350)")
else()
    message(STATUS "Target platform: ${PICO_PLATFORM}")
endif()

# Select interface language
set(LANGUAGE "EN" CACHE STRING "Select interface language: EN, FR, DE, SP, IT")
if(LANGUAGE STREQUAL "FR")
    add_definitions(-DLANG_FR)
    set(LANG_SRC src/UserInterface_FR.cpp)
elseif(LANGUAGE STREQUAL "DE")
    add_definitions(-DLANG_DE)
    set(LANG_SRC src/UserInterface_DE.cpp)
elseif(LANGUAGE STREQUAL "SP")
    add_definitions(-DLANG_SP)
    set(LANG_SRC src/UserInterface_SP.cpp)
elseif(LANGUAGE STREQUAL "IT")
    add_definitions(-DLANG_IT)
    set(LANG_SRC src/UserInterface_IT.cpp)
else()
    add_definitions(-DLANG_EN)
    set(LANG_SRC src/UserInterface_EN.cpp)
endif()

# Debug mode (0=production, 1=debug displays enabled)
set(ENABLE_DEBUG "0" CACHE STRING "Enable debug displays (0 or 1)")
if(ENABLE_DEBUG EQUAL "1")
    add_definitions(-DENABLE_DEBUG=1)
    message(STATUS "Debug mode: ENABLED")
else()
    add_definitions(-DENABLE_DEBUG=0)
    message(STATUS "Debug mode: DISABLED (production)")
endif()

# OLED Display mode (0=disabled for speed, 1=enabled for standard)
set(ENABLE_OLED_DISPLAY "1" CACHE STRING "Enable OLED display (0 or 1)")
if(ENABLE_OLED_DISPLAY EQUAL "1")
    add_definitions(-DENABLE_OLED_DISPLAY=1)
    message(STATUS "OLED Display: ENABLED")
else()
    add_definitions(-DENABLE_OLED_DISPLAY=0)
    message(STATUS "OLED Display: DISABLED (speed mode)")
endif()

# Serial logging mode (0=minimal, 1=verbose)
set(ENABLE_SERIAL_LOGGING "1" CACHE STRING "Enable verbose serial logging (0 or 1)")
if(ENABLE_SERIAL_LOGGING EQUAL "1")
    add_definitions(-DENABLE_SERIAL_LOGGING=1)
    message(STATUS "Serial Logging: VERBOSE")
else()
    add_definitions(-DENABLE_SERIAL_LOGGING=0)
    message(STATUS "Serial Logging: MINIMAL (speed mode)")
endif()

# Bluepad32 Bluetooth support (Pico W only)
# Note: Pico W (pico_w) doesn't have enough RAM - using Pico 2 W only
if(PICO_BOARD STREQUAL "pico2_w")
    set(ENABLE_BLUEPAD32 ON)
    add_definitions(-DENABLE_BLUEPAD32=1)
    message(STATUS "Bluepad32: ENABLED (Pico 2 W detected)")
else()
    set(ENABLE_BLUEPAD32 OFF)
    add_definitions(-DENABLE_BLUEPAD32=0)
    message(STATUS "Bluepad32: DISABLED (Not Pico 2 W)")
endif()

set(SOURCES
    src/main.cpp
    src/HD6301V1ST.cpp
    src/st_key_lookup_hid_gb.cpp
    src/AtariSTMouse.cpp
    src/SerialPort.cpp
    src/HidInput.cpp
    src/util.cpp
    src/UserInterface.cpp
    ${LANG_SRC}  # Specific language source file
    src/NVSettings.cpp
    src/hid_app_host.c
    src/xinput_host.c  # Official tusb_xinput driver
    src/xinput_atari.cpp  # Xbox-to-Atari joystick mapper
    src/ps4_controller.c
    src/ps3_controller.c
    src/gamecube_adapter.c
    # src/gamecube_vendor.c  # v11.1+: Disabled, using HID hijacking approach instead
    src/switch_controller.c
    src/stadia_controller.c
    ssd1306/ssd1306.c
    6301/6301.c
    hidparser/HIDParser.c
)

# Add Bluepad32 source files if enabled
if(ENABLE_BLUEPAD32)
    list(APPEND SOURCES
        src/bluepad32_platform.c
        src/bluepad32_atari.cpp
        src/bluepad32_init.c
    )
endif()

# Add runtime toggle source (always included for runtime USB/Bluetooth control)
list(APPEND SOURCES
    src/runtime_toggle.c
)
set(INCLUDES
    include
)

add_executable(atari_ikbd ${SOURCES})

target_include_directories(
    atari_ikbd PRIVATE 
    6301
    ssd1306
    src
    include
    hidparser
    ${INCLUDES}
)

# Add Bluepad32 include directories if enabled
if(ENABLE_BLUEPAD32)
    target_include_directories(
        atari_ikbd PRIVATE
        ${CMAKE_SOURCE_DIR}/bluepad32/src/components/bluepad32/include
        ${CMAKE_SOURCE_DIR}/src # For btstack_config.h and sdkconfig.h
        ${CMAKE_SOURCE_DIR}/include # For bluepad32_init.h
        ${PICO_SDK_PATH}/lib/btstack/src # For btstack headers
    )
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-implicit-int")

#add_definitions(-DUNIX -DPICO -DTRACE_6301 -DPICO_DEOPTIMIZED_DEBUG=1 -DLOG=2 -DDEBUG)
#add_definitions(-DUNIX -DPICO -DTRACE_6301)
add_definitions(-DUNIX -DPICO)

target_link_libraries(atari_ikbd pico_stdlib pico_multicore hardware_i2c hardware_flash hardware_sync tinyusb_host)

# Add Bluepad32 libraries if enabled
if(ENABLE_BLUEPAD32)
    # Add bluepad32 subdirectory first
    add_subdirectory(${CMAKE_SOURCE_DIR}/bluepad32/src/components/bluepad32 libbluepad32)
    
    # Add include directory for btstack_config.h to bluepad32 library
    target_include_directories(bluepad32 PRIVATE ${CMAKE_SOURCE_DIR}/src)
    
    target_link_libraries(atari_ikbd
        pico_cyw43_arch_none
        pico_btstack_classic
        pico_btstack_cyw43
        pico_async_context_poll  # For async_context_poll_init_with_defaults
        bluepad32
    )
endif()

pico_enable_stdio_uart(atari_ikbd 1)
pico_add_extra_outputs(atari_ikbd)

# Set binary type based on board
if(ENABLE_BLUEPAD32)
    pico_set_binary_type(atari_ikbd default) # Use default (XIP) for Bluetooth builds
    message(STATUS "Binary type: default (Pico 2 W with Bluetooth)")
else()
    pico_set_binary_type(atari_ikbd copy_to_ram) # Use copy_to_ram for standard builds
    message(STATUS "Binary type: copy_to_ram (standard)")
endif()

# Set output name based on board type for clarity
if(PICO_BOARD STREQUAL "pico2_w")
    set_target_properties(atari_ikbd PROPERTIES OUTPUT_NAME "atari_ikbd_pico2_w")
    message(STATUS "Output files will be named: atari_ikbd_pico2_w.*")
elseif(PICO_BOARD STREQUAL "pico2")
    set_target_properties(atari_ikbd PROPERTIES OUTPUT_NAME "atari_ikbd_pico2")
    message(STATUS "Output files will be named: atari_ikbd_pico2.*")
else()
    set_target_properties(atari_ikbd PROPERTIES OUTPUT_NAME "atari_ikbd_pico")
    message(STATUS "Output files will be named: atari_ikbd_pico.*")
endif()


