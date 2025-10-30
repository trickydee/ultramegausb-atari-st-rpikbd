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
    src/switch_controller.c  # Nintendo Switch controller support
    src/stadia_controller.c  # Google Stadia controller support
    ssd1306/ssd1306.c
    6301/6301.c
    hidparser/HIDParser.c
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

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-implicit-int")

#add_definitions(-DUNIX -DPICO -DTRACE_6301 -DPICO_DEOPTIMIZED_DEBUG=1 -DLOG=2 -DDEBUG)
#add_definitions(-DUNIX -DPICO -DTRACE_6301)
add_definitions(-DUNIX -DPICO)

target_link_libraries(atari_ikbd pico_stdlib pico_multicore hardware_i2c hardware_flash hardware_sync tinyusb_host)
pico_enable_stdio_uart(atari_ikbd 1)
pico_add_extra_outputs(atari_ikbd)
pico_set_binary_type(atari_ikbd copy_to_ram)

# Set output name based on board type for clarity
if(PICO_BOARD STREQUAL "pico2")
    set_target_properties(atari_ikbd PROPERTIES OUTPUT_NAME "atari_ikbd_pico2")
    message(STATUS "Output files will be named: atari_ikbd_pico2.*")
else()
    set_target_properties(atari_ikbd PROPERTIES OUTPUT_NAME "atari_ikbd_pico")
    message(STATUS "Output files will be named: atari_ikbd_pico.*")
endif()


