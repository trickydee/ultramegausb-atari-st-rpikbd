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
#include "pico.h"
#include "pico/flash.h"
#include "hardware/flash.h"

// Pre-fix layout: always 0x1FF000 (last 4 KiB of 2 MiB), including wrong placement on 4 MiB parts.
#define FLASH_LOCATION_LEGACY_FIXED  (0x200000u - FLASH_SECTOR_SIZE)

#ifndef PICO_FLASH_BANK_TOTAL_SIZE
#define PICO_FLASH_BANK_TOTAL_SIZE (FLASH_SECTOR_SIZE * 2u)
#endif

static uint32_t nv_settings_flash_offset(void) {
#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
    const uint32_t bt_bank =
        PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - PICO_FLASH_BANK_TOTAL_SIZE;
#else
    const uint32_t bt_bank = PICO_FLASH_SIZE_BYTES - PICO_FLASH_BANK_TOTAL_SIZE;
#endif
    return bt_bank - FLASH_SECTOR_SIZE;
}

static uint32_t flash_location = 0;

static union {
    Settings settings;
    uint8_t  raw[FLASH_SECTOR_SIZE];
} storage;

NVSettings::NVSettings() {
    read();
}

Settings& NVSettings::get_settings() {
    return storage.settings;
}

static void flash_write_callback(void* param) {
    (void)param;
    flash_range_erase(flash_location, FLASH_SECTOR_SIZE);
    flash_range_program(flash_location, &storage.raw[0], FLASH_PAGE_SIZE);
}

void NVSettings::write() {
    int result = flash_safe_execute(flash_write_callback, nullptr, 100);
    if (result != PICO_OK) {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(flash_location, FLASH_SECTOR_SIZE);
        flash_range_program(flash_location, &storage.raw[0], FLASH_PAGE_SIZE);
        restore_interrupts(ints);
    }
}

void NVSettings::read() {
    flash_location = nv_settings_flash_offset();

    memcpy(&storage.raw[0], (void*)(XIP_BASE + flash_location), FLASH_SECTOR_SIZE);
    if (storage.settings.version == 1) {
        return;
    }

    if (flash_location != FLASH_LOCATION_LEGACY_FIXED) {
        memcpy(&storage.raw[0], (void*)(XIP_BASE + FLASH_LOCATION_LEGACY_FIXED), FLASH_SECTOR_SIZE);
        if (storage.settings.version == 1) {
            write();
            return;
        }
    }

    memset(&storage.raw[0], 0, FLASH_SECTOR_SIZE);
    storage.settings.version = 1;
    write();
}
