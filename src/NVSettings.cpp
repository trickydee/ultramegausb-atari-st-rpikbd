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

#include "NVSettings.h"
#include <hardware/sync.h>
#include <string.h>
#include "pico.h"  // For PICO_OK
#include "pico/flash.h"  // For flash_safe_execute() - required to coordinate with Core 1

// The PICO has 2Mb of flash storage. We will assume the code will not be taking
// all of this and carve 4K out near the end
#define FLASH_LOCATION  (0x200000-FLASH_SECTOR_SIZE)

static union {
    Settings settings;
    uint8_t  raw[FLASH_SECTOR_SIZE];
} storage;

NVSettings::NVSettings() {
    // NV settings disabled for now as the RP2040 seems to crash if you write flash with
    // interrupts enabled or disabled.
    read();
}

Settings& NVSettings::get_settings() {
    return storage.settings;
}

// Flash write callback - executed safely with Core 1 paused
static void flash_write_callback(void* param) {
    (void)param;  // Unused
    flash_range_erase(FLASH_LOCATION, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_LOCATION, &storage.raw[0], FLASH_PAGE_SIZE);
}

void NVSettings::write() {
    // Use flash_safe_execute() to coordinate with Core 1
    // This ensures Core 1 pauses execution during flash operations, preventing freezes
    // Timeout of 100ms should be sufficient for flash erase/program operations
    int result = flash_safe_execute(flash_write_callback, nullptr, 100);
    if (result != PICO_OK) {
        // If flash_safe_execute fails, fall back to old method (but this may cause Core 1 freeze)
        // This should only happen if Core 1 hasn't called flash_safe_execute_core_init()
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_LOCATION, FLASH_SECTOR_SIZE);
        flash_range_program(FLASH_LOCATION, &storage.raw[0], FLASH_PAGE_SIZE);
        restore_interrupts(ints);
    }
}

void NVSettings::read() {
    // Copy from flash to our structure
    memcpy(&storage.raw[0], (void*)(XIP_BASE + FLASH_LOCATION), FLASH_SECTOR_SIZE);
    // Make sure flash contained a valid structure
    if (storage.settings.version != 1) {
        memset(&storage.raw[0], 0, FLASH_SECTOR_SIZE);
        storage.settings.version = 1;
        write();
    }
}

