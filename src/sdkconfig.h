//
// Bluepad32 SDK Configuration
// Based on bluepad32/examples/pico_w/src/sdkconfig.h
//
#define CONFIG_BLUEPAD32_MAX_DEVICES 4
#define CONFIG_BLUEPAD32_MAX_ALLOWLIST 4
#define CONFIG_BLUEPAD32_GAP_SECURITY 1
#define CONFIG_BLUEPAD32_ENABLE_BLE_BY_DEFAULT 1

#define CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#define CONFIG_TARGET_PICO_W

// 2 == Info
#define CONFIG_BLUEPAD32_LOG_LEVEL 2

// Enable both Classic and BLE
#define ENABLE_CLASSIC 1
#define ENABLE_BLE 1

