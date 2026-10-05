#ifndef FLASH_BOOT_GUARD_H
#define FLASH_BOOT_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

// Permanently end the single-core boot phase BEFORE launching Core 1.
void flash_boot_guard_core1_launching(void);

#ifdef __cplusplus
}
#endif
#endif
