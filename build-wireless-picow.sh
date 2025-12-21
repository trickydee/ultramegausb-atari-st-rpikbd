#!/bin/bash
################################################################################
# Wireless Build Script for Atari ST USB Adapter - Raspberry Pi Pico W (RP2040)
# NOTE: Current CMake configuration disables Bluepad32 on Pico W due to RAM
#       limits. This script builds a Pico W firmware using the same codebase
#       and build variants as other boards.
################################################################################

set -e  # Exit on error

# Configuration (inherits same env vars as build-all.sh)
LANGUAGE="${LANGUAGE:-EN}"
DEBUG="${DEBUG:-1}"
OLED="${OLED:-1}"
LOGGING="${LOGGING:-1}"

echo "================================================================================"
echo "  Atari ST USB Adapter - Pico W (RP2040 WiFi) Build"
echo "  Language: ${LANGUAGE}"
echo "  Debug Mode: ${DEBUG}"
echo "  OLED Display: ${OLED}"
echo "  Serial Logging: ${LOGGING}"
echo "================================================================================"
echo ""

################################################################################
# Initialize git submodules (Pico SDK, etc.)
################################################################################

echo ">>> Checking Pico SDK..."

PICO_SDK_OK=false
if [ -d "./pico-sdk/src" ] && [ -f "./pico-sdk/pico_sdk_init.cmake" ]; then
    if [ -d "./pico-sdk/src/rp2_common" ]; then
        PICO_SDK_OK=true
        echo "    ✅ Pico SDK is properly initialized"
    fi
fi

if [ "$PICO_SDK_OK" = false ]; then
    echo "    ⚠️  Pico SDK missing or incomplete - reinitializing..."

    if [ -d "./pico-sdk" ]; then
        echo "    Removing incomplete pico-sdk directory..."
        rm -rf ./pico-sdk
    fi

    echo "    Cloning Pico SDK and dependencies (this may take a minute)..."
    if ! git submodule update --init --recursive; then
        echo "ERROR: Failed to initialize git submodules!"
        echo "Please check your internet connection and try again."
        exit 1
    fi

    echo "    ✅ Pico SDK initialized successfully"
fi

echo ""

################################################################################
# Apply patches
################################################################################

echo ">>> Applying patches..."
./apply-patches.sh || echo "Warning: Patch application returned non-zero"
echo ""

################################################################################
# Build for Raspberry Pi Pico W (RP2040 with WiFi)
################################################################################

echo ">>> Building for Raspberry Pi Pico W (RP2040 with WiFi)..."
echo ""

rm -rf ./build-picow
mkdir -p ./build-picow

echo "    Configuring CMake for RP2040 WiFi (pico_w)..."
cmake -B ./build-picow -S . \
    -DPICO_BOARD=pico_w \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ./build-picow/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2040 WiFi (pico_w)!"
    tail -20 ./build-picow/cmake.log
    exit 1
fi

echo "    Compiling firmware for RP2040 WiFi (pico_w)..."
cmake --build ./build-picow -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > ./build-picow/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2040 WiFi (pico_w)!"
    tail -30 ./build-picow/build.log
    exit 1
fi

echo "    ✅ RP2040 WiFi (pico_w) build complete!"
echo ""

################################################################################
# Copy firmware to dist
################################################################################

echo ">>> Collecting firmware files..."
mkdir -p ./dist

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

if [ -f "./build-picow/atari_ikbd_pico.uf2" ]; then
    cp ./build-picow/atari_ikbd_pico.uf2 ./dist/atari_ikbd_picow${BUILD_VARIANT}.uf2
    cp ./build-picow/atari_ikbd_pico.elf ./dist/atari_ikbd_picow${BUILD_VARIANT}.elf 2>/dev/null || true
    cp ./build-picow/atari_ikbd_pico.bin ./dist/atari_ikbd_picow${BUILD_VARIANT}.bin 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_picow${BUILD_VARIANT}.uf2"
else
    echo "    ⚠️  Warning: atari_ikbd_pico.uf2 for Pico W (pico_w) not found!"
fi

echo ""
echo "================================================================================"
echo "  BUILD COMPLETE!"
echo "================================================================================"
echo ""
echo "Firmware files available in: ./dist/"
if [ -f "./dist/atari_ikbd_picow${BUILD_VARIANT}.uf2" ]; then
    SIZE_PICOW=$(ls -lh "./dist/atari_ikbd_picow${BUILD_VARIANT}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico W (RP2040 WiFi): atari_ikbd_picow${BUILD_VARIANT}.uf2 (${SIZE_PICOW})"
fi
echo ""
echo "To flash firmware:"
echo "  1. Hold BOOTSEL button on your Pico W"
echo "  2. Connect USB cable"
echo "  3. Release BOOTSEL button"
echo "  4. Copy the appropriate .uf2 file to the RPI-RP2 drive"
echo ""
echo "================================================================================"



