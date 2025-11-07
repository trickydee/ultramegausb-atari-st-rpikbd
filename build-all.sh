#!/bin/bash
################################################################################
# Build Script for Atari ST USB Adapter
# Builds firmware for both Raspberry Pi Pico (RP2040) and Pico 2 (RP2350)
#
# IMPORTANT: Build directories (build-pico, build-pico2) should NEVER be
#            committed to git. They are automatically generated and cleaned
#            by this script. Make sure .gitignore includes them!
################################################################################

set -e  # Exit on error

# Configuration
LANGUAGE="${LANGUAGE:-EN}"
DEBUG="${DEBUG:-1}"  # Set to 1 to enable debug displays
OLED="${OLED:-1}"    # Set to 0 to disable OLED for speed mode
LOGGING="${LOGGING:-1}"  # Set to 0 to disable verbose logging

echo "================================================================================"
echo "  Atari ST USB Adapter - Multi-Variant Build"
echo "  Version: 12.1.0 (Ultra-low latency serial optimizations)"
echo "  Language: ${LANGUAGE}"
echo "  Debug Mode: ${DEBUG}"
echo "  OLED Display: ${OLED}"
echo "  Serial Logging: ${LOGGING}"
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

# Apply patches
echo ">>> Applying patches..."
./apply-patches.sh || echo "Warning: Patch application returned non-zero"
echo ""

################################################################################
# Build for Raspberry Pi Pico (RP2040)
################################################################################

echo ">>> Building for Raspberry Pi Pico (RP2040)..."
echo ""

# Clean and create build directory
rm -rf ./build-pico
mkdir -p ./build-pico

# Configure
echo "    Configuring CMake for RP2040..."
cmake -B ./build-pico -S . \
    -DPICO_BOARD=pico \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ./build-pico/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2040!"
    tail -20 ./build-pico/cmake.log
    exit 1
fi

# Build
echo "    Compiling firmware for RP2040..."
cmake --build ./build-pico -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > ./build-pico/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2040!"
    tail -30 ./build-pico/build.log
    exit 1
fi

echo "    ✅ RP2040 build complete!"
echo ""

################################################################################
# Build for Raspberry Pi Pico 2 (RP2350)
################################################################################

echo ">>> Building for Raspberry Pi Pico 2 (RP2350)..."
echo ""

# Clean and create build directory
rm -rf ./build-pico2
mkdir -p ./build-pico2

# Configure
echo "    Configuring CMake for RP2350..."
cmake -B ./build-pico2 -S . \
    -DPICO_BOARD=pico2 \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ./build-pico2/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2350!"
    tail -20 ./build-pico2/cmake.log
    exit 1
fi

# Build
echo "    Compiling firmware for RP2350..."
cmake --build ./build-pico2 -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > ./build-pico2/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2350!"
    tail -30 ./build-pico2/build.log
    exit 1
fi

echo "    ✅ RP2350 build complete!"
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
elif [ "$OLED" = "0" ]; then
    BUILD_VARIANT="_nooled"
elif [ "$LOGGING" = "0" ]; then
    BUILD_VARIANT="_nolog"
fi

# Copy RP2040 firmware
if [ -f "./build-pico/atari_ikbd_pico.uf2" ]; then
    cp ./build-pico/atari_ikbd_pico.uf2 ./dist/atari_ikbd_pico${BUILD_VARIANT}.uf2
    cp ./build-pico/atari_ikbd_pico.elf ./dist/atari_ikbd_pico${BUILD_VARIANT}.elf 2>/dev/null || true
    cp ./build-pico/atari_ikbd_pico.bin ./dist/atari_ikbd_pico${BUILD_VARIANT}.bin 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico${BUILD_VARIANT}.uf2"
else
    echo "    ⚠️  Warning: atari_ikbd_pico.uf2 not found!"
fi

# Copy RP2350 firmware
if [ -f "./build-pico2/atari_ikbd_pico2.uf2" ]; then
    cp ./build-pico2/atari_ikbd_pico2.uf2 ./dist/atari_ikbd_pico2${BUILD_VARIANT}.uf2
    cp ./build-pico2/atari_ikbd_pico2.elf ./dist/atari_ikbd_pico2${BUILD_VARIANT}.elf 2>/dev/null || true
    cp ./build-pico2/atari_ikbd_pico2.bin ./dist/atari_ikbd_pico2${BUILD_VARIANT}.bin 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico2${BUILD_VARIANT}.uf2"
else
    echo "    ⚠️  Warning: atari_ikbd_pico2.uf2 not found!"
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
    echo "  Build Variant: SPEED MODE (no OLED, minimal logging)"
elif [ "$OLED" = "0" ]; then
    echo "  Build Variant: NO OLED (display disabled)"
elif [ "$LOGGING" = "0" ]; then
    echo "  Build Variant: NO LOGGING (minimal logging)"
else
    echo "  Build Variant: STANDARD (full features)"
fi
echo ""

if [ -f "./dist/atari_ikbd_pico${BUILD_VARIANT}.uf2" ]; then
    SIZE_PICO=$(ls -lh "./dist/atari_ikbd_pico${BUILD_VARIANT}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico (RP2040):  atari_ikbd_pico${BUILD_VARIANT}.uf2  (${SIZE_PICO})"
fi

if [ -f "./dist/atari_ikbd_pico2${BUILD_VARIANT}.uf2" ]; then
    SIZE_PICO2=$(ls -lh "./dist/atari_ikbd_pico2${BUILD_VARIANT}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico 2 (RP2350): atari_ikbd_pico2${BUILD_VARIANT}.uf2 (${SIZE_PICO2})"
fi

echo ""
echo "To flash firmware:"
echo "  1. Hold BOOTSEL button on your Pico/Pico 2"
echo "  2. Connect USB cable"
echo "  3. Release BOOTSEL button"
echo "  4. Copy the appropriate .uf2 file to the RPI-RP2 drive"
echo ""
echo "================================================================================"

# Automatically build speed-mode variant after standard build (unless skipped)
if [ "${SKIP_SPEED_BUILD:-0}" != "1" ] && [ "${OLED}" = "1" ] && [ "${LOGGING}" = "1" ]; then
    echo ""
    echo ">>> Auto-building speed mode variant (OLED=0, LOGGING=0)..."
    OLED=0 LOGGING=0 SKIP_SPEED_BUILD=1 "$0"
fi

