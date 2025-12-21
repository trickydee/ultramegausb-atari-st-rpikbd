#!/bin/bash
################################################################################
# Build Script for Atari ST USB Adapter - Pico W Bluetooth Build
# Builds firmware for Raspberry Pi Pico W (RP2040 with WiFi/Bluetooth)
# with Bluepad32 Bluetooth gamepad support
#
# IMPORTANT: Build directory (build-pico_w) should NEVER be committed to git.
#            It is automatically generated and cleaned by this script.
################################################################################

set -e  # Exit on error

# Configuration
LANGUAGE="${LANGUAGE:-EN}"
DEBUG="${DEBUG:-1}"  # Set to 1 to enable debug displays
OLED="${OLED:-1}"    # Set to 0 to disable OLED for speed mode
LOGGING="${LOGGING:-1}"  # Set to 0 to disable verbose logging

echo "================================================================================"
echo "  Atari ST USB Adapter - Pico 2 W Bluetooth Build"
echo "  Version: 14.0.4 (Ultra-low latency serial optimizations + Bluetooth)"
echo "  Language: ${LANGUAGE}"
echo "  Debug Mode: ${DEBUG}"
echo "  OLED Display: ${OLED}"
echo "  Serial Logging: ${LOGGING}"
echo ""
echo "  ℹ️  NOTE: This build is for Pico 2 W (RP2350) only."
echo "     Pico W (RP2040) does not have enough RAM for Bluetooth support."
echo "     Pico 2 W uses XIP (code in flash) so only data needs to fit in RAM."
echo "================================================================================"
echo ""

################################################################################
# Initialize git submodules
################################################################################

echo ">>> Checking Pico SDK..."

# Check if pico-sdk exists and has actual content
PICO_SDK_OK=false
if [ -d "./pico-sdk/src" ] && [ -f "./pico-sdk/pico_sdk_init.cmake" ]; then
    # Check if it's actually populated (has more than just error files)
    if [ -d "./pico-sdk/src/rp2_common" ]; then
        PICO_SDK_OK=true
        echo "    ✅ Pico SDK is properly initialized"
    fi
fi

# If pico-sdk is missing or incomplete, reinitialize it
if [ "$PICO_SDK_OK" = false ]; then
    echo "    ⚠️  Pico SDK missing or incomplete - reinitializing..."
    
    # Clean up any partial/broken pico-sdk directory
    if [ -d "./pico-sdk" ]; then
        echo "    Removing incomplete pico-sdk directory..."
        rm -rf ./pico-sdk
    fi
    
    # Initialize submodules
    echo "    Cloning Pico SDK and dependencies (this may take a minute)..."
    if ! git submodule update --init --recursive; then
        echo "ERROR: Failed to initialize git submodules!"
        echo "Please check your internet connection and try again."
        exit 1
    fi
    
    echo "    ✅ Pico SDK initialized successfully"
fi

echo ""

# Check for Bluepad32 submodule
echo ">>> Checking Bluepad32 submodule..."

BLUEPAD32_OK=false
if [ -d "./bluepad32/src" ] && [ -f "./bluepad32/src/components/bluepad32/CMakeLists.txt" ]; then
    BLUEPAD32_OK=true
    echo "    ✅ Bluepad32 is properly initialized"
else
    echo "    ⚠️  Bluepad32 missing or incomplete - initializing..."
    
    # Initialize Bluepad32 submodule
    if ! git submodule update --init --recursive bluepad32; then
        echo "ERROR: Failed to initialize Bluepad32 submodule!"
        echo "Please check your internet connection and try again."
        exit 1
    fi
    
    echo "    ✅ Bluepad32 initialized successfully"
fi

echo ""

# Apply patches
echo ">>> Applying patches..."
./apply-patches.sh || echo "Warning: Patch application returned non-zero"
echo ""

################################################################################
# Build for Raspberry Pi Pico 2 W (RP2350 with WiFi/Bluetooth)
# Note: Using Pico 2 W instead of Pico W due to RAM requirements
#       Bluepad32 + btstack needs more RAM than Pico W (RP2040) provides
################################################################################

echo ">>> Building for Raspberry Pi Pico 2 W (RP2350 with Bluetooth)..."
echo ""

# Clean and create build directory
rm -rf ./build-pico2_w
mkdir -p ./build-pico2_w

# Configure
echo "    Configuring CMake for Pico 2 W (RP2350 + Bluetooth)..."
cmake -B ./build-pico2_w -S . \
    -DPICO_BOARD=pico2_w \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ./build-pico2_w/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for Pico 2 W!"
    tail -20 ./build-pico2_w/cmake.log
    exit 1
fi

# Build
echo "    Compiling firmware for Pico 2 W (this may take longer due to Bluepad32)..."
cmake --build ./build-pico2_w -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > ./build-pico2_w/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for Pico 2 W!"
    tail -30 ./build-pico2_w/build.log
    exit 1
fi

echo "    ✅ Pico 2 W build complete!"
echo ""

################################################################################
# Copy firmware files to dist directory with variant-specific names
################################################################################

echo ">>> Collecting firmware files..."
mkdir -p ./dist

# Determine build variant suffix
BUILD_VARIANT=""
if [ "$OLED" = "0" ] && [ "$LOGGING" = "0" ]; then
    BUILD_VARIANT="_speed"
elif [ "$OLED" = "1" ] && [ "$LOGGING" = "0" ]; then
    BUILD_VARIANT="_production"
elif [ "$OLED" = "0" ]; then
    BUILD_VARIANT="_nooled"
elif [ "$LOGGING" = "0" ]; then
    BUILD_VARIANT="_nolog"
fi

# Add _wireless suffix to indicate Bluetooth build
WIRELESS_SUFFIX="_wireless"

# Copy Pico 2 W firmware (check both possible output names)
if [ -f "./build-pico2_w/atari_ikbd_pico2_w.uf2" ]; then
    cp ./build-pico2_w/atari_ikbd_pico2_w.uf2 ./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2
    cp ./build-pico2_w/atari_ikbd_pico2_w.elf ./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.elf 2>/dev/null || true
    cp ./build-pico2_w/atari_ikbd_pico2_w.bin ./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.bin 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2"
elif [ -f "./build-pico2_w/atari_ikbd_pico2.uf2" ]; then
    # Fallback to old naming if CMakeLists.txt hasn't been updated yet
    cp ./build-pico2_w/atari_ikbd_pico2.uf2 ./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2
    cp ./build-pico2_w/atari_ikbd_pico2.elf ./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.elf 2>/dev/null || true
    cp ./build-pico2_w/atari_ikbd_pico2.bin ./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.bin 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2"
else
    echo "    ⚠️  Warning: atari_ikbd_pico2_w.uf2 or atari_ikbd_pico2.uf2 not found!"
fi

echo ""

################################################################################
# Display firmware information
################################################################################

echo "================================================================================"
echo "  BUILD COMPLETE!"
echo "================================================================================"
echo ""
echo "Firmware files available in: ./dist/"
echo ""

# Display build variant info
if [ "$OLED" = "0" ] && [ "$LOGGING" = "0" ]; then
    echo "  Build Variant: SPEED MODE (no OLED, minimal logging) + Bluetooth"
elif [ "$OLED" = "0" ]; then
    echo "  Build Variant: NO OLED (display disabled) + Bluetooth"
elif [ "$LOGGING" = "0" ]; then
    echo "  Build Variant: NO LOGGING (minimal logging) + Bluetooth"
else
    echo "  Build Variant: STANDARD (full features) + Bluetooth"
fi
echo ""

if [ -f "./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2" ]; then
    SIZE_PICO2_W=$(ls -lh "./dist/atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico 2 W (RP2350 + Bluetooth):"
    echo "     atari_ikbd_pico2_w${BUILD_VARIANT}${WIRELESS_SUFFIX}.uf2  (${SIZE_PICO2_W})"
fi

echo ""
echo "To flash firmware:"
echo "  1. Hold BOOTSEL button on your Pico 2 W"
echo "  2. Connect USB cable"
echo "  3. Release BOOTSEL button"
echo "  4. Copy the .uf2 file to the RPI-RP2 drive"
echo ""
echo "Note: This build is for Pico 2 W (RP2350) only."
echo "      Pico W (RP2040) does not have enough RAM for Bluetooth support."
echo ""
echo "Bluetooth Gamepad Support:"
echo "  - Supports DualSense, DualShock 4, Switch Pro, Xbox Wireless, and more"
echo "  - Gamepads will appear in joystick count alongside USB controllers"
echo "  - Pair gamepads via standard Bluetooth pairing"
echo ""
echo "================================================================================"

# Automatically build production & speed variants after standard build (unless skipped)
if [ "${SKIP_SPEED_BUILD:-0}" != "1" ] && [ "${OLED}" = "1" ] && [ "${LOGGING}" = "1" ]; then
    echo ""
    echo ">>> Auto-building production variant (OLED=1, LOGGING=0)..."
    OLED=1 LOGGING=0 SKIP_SPEED_BUILD=1 "$0"
    echo ""
    echo ">>> Auto-building speed mode variant (OLED=0, LOGGING=0)..."
    OLED=0 LOGGING=0 SKIP_SPEED_BUILD=1 "$0"
fi

