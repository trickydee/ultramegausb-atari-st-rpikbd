#include "flash_boot_guard.h"
#include "pico/flash.h"
#include "hardware/sync.h"

// Only Core 0 accesses this flag. Never reset it on Bluetooth reinitialization.
static bool core1_launching;

int __real_flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms);

void flash_boot_guard_core1_launching(void) {
    core1_launching = true;
    __dmb();
}

int __wrap_flash_safe_execute(void (*func)(void *), void *param, uint32_t timeout_ms) {
    int result;
    if (get_core_num() == 0 && !core1_launching) {
        // Bluetooth initializes TLV before Core 1 is launched. The SDK cannot
        // infer that Core 1 is still parked, so it otherwise refuses these writes.
        uint32_t irq_state = save_and_disable_interrupts();
        func(param);
        restore_interrupts(irq_state);
        result = PICO_OK;
    } else {
        // Once launch begins, always require the SDK's normal multicore lockout.
        // In particular, do not fall back to IRQ-only writes on a runtime error.
        result = __real_flash_safe_execute(func, param, timeout_ms);
    }
    return result;
}
