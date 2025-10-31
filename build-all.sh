#!/bin/bash
################################################################################
# Build Script for Atari ST USB Adapter
# Builds firmware for both Raspberry Pi Pico (RP2040) and Pico 2 (RP2350)
################################################################################

set -e  # Exit on error

# Configuration
LANGUAGE="${LANGUAGE:-EN}"
DEBUG="${DEBUG:-0}"  # Set to 1 to enable debug displays

echo "================================================================================"
echo "  Atari ST USB Adapter - Dual Board Build"
echo "  Version: 10.0.0"
echo "  Language: ${LANGUAGE}"
echo "  Debug Mode: ${DEBUG}"
echo "================================================================================"
echo ""

# Initialize git submodules if needed
if [ ! -d "./pico-sdk/.git" ]; then
    echo ">>> Initializing git submodules..."
    git submodule update --init --recursive
    echo ""
fi

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
# Copy firmware files to dist directory
################################################################################

echo ">>> Collecting firmware files..."
mkdir -p ./dist

# Copy RP2040 firmware
if [ -f "./build-pico/atari_ikbd_pico.uf2" ]; then
    cp ./build-pico/atari_ikbd_pico.uf2 ./dist/
    cp ./build-pico/atari_ikbd_pico.elf ./dist/ 2>/dev/null || true
    cp ./build-pico/atari_ikbd_pico.bin ./dist/ 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico.uf2"
else
    echo "    ⚠️  Warning: atari_ikbd_pico.uf2 not found!"
fi

# Copy RP2350 firmware
if [ -f "./build-pico2/atari_ikbd_pico2.uf2" ]; then
    cp ./build-pico2/atari_ikbd_pico2.uf2 ./dist/
    cp ./build-pico2/atari_ikbd_pico2.elf ./dist/ 2>/dev/null || true
    cp ./build-pico2/atari_ikbd_pico2.bin ./dist/ 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico2.uf2"
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

if [ -f "./dist/atari_ikbd_pico.uf2" ]; then
    SIZE_PICO=$(ls -lh "./dist/atari_ikbd_pico.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico (RP2040):  atari_ikbd_pico.uf2  (${SIZE_PICO})"
fi

if [ -f "./dist/atari_ikbd_pico2.uf2" ]; then
    SIZE_PICO2=$(ls -lh "./dist/atari_ikbd_pico2.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico 2 (RP2350): atari_ikbd_pico2.uf2 (${SIZE_PICO2})"
fi

echo ""
echo "To flash firmware:"
echo "  1. Hold BOOTSEL button on your Pico/Pico 2"
echo "  2. Connect USB cable"
echo "  3. Release BOOTSEL button"
echo "  4. Copy the appropriate .uf2 file to the RPI-RP2 drive"
echo ""
echo "================================================================================"

