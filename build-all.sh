#!/bin/bash
################################################################################
# Build Script for Atari ST USB Adapter
# Builds firmware for both Raspberry Pi Pico (RP2040) and Pico 2 (RP2350)
################################################################################

set -e  # Exit on error

# Configuration
LANGUAGE="${LANGUAGE:-EN}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR_PICO="${SCRIPT_DIR}/build-pico"
BUILD_DIR_PICO2="${SCRIPT_DIR}/build-pico2"
DIST_DIR="${SCRIPT_DIR}/dist"

echo "================================================================================"
echo "  Atari ST USB Adapter - Dual Board Build"
echo "  Version: 7.5.0"
echo "  Language: ${LANGUAGE}"
echo "================================================================================"
echo ""

# Apply patches
echo ">>> Applying patches..."
"${SCRIPT_DIR}/apply-patches.sh" || echo "Warning: Patch application returned non-zero"
echo ""

################################################################################
# Build for Raspberry Pi Pico (RP2040)
################################################################################

echo ">>> Building for Raspberry Pi Pico (RP2040)..."
echo ""

# Clean and create build directory
rm -rf "${BUILD_DIR_PICO}"
mkdir -p "${BUILD_DIR_PICO}"

# Configure
echo "    Configuring CMake for RP2040..."
cmake -B "${BUILD_DIR_PICO}" -S "${SCRIPT_DIR}" \
    -DPICO_BOARD=pico \
    -DLANGUAGE="${LANGUAGE}" \
    > "${BUILD_DIR_PICO}/cmake.log" 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2040!"
    tail -20 "${BUILD_DIR_PICO}/cmake.log"
    exit 1
fi

# Build
echo "    Compiling firmware for RP2040..."
cmake --build "${BUILD_DIR_PICO}" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > "${BUILD_DIR_PICO}/build.log" 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2040!"
    tail -30 "${BUILD_DIR_PICO}/build.log"
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
rm -rf "${BUILD_DIR_PICO2}"
mkdir -p "${BUILD_DIR_PICO2}"

# Configure
echo "    Configuring CMake for RP2350..."
cmake -B "${BUILD_DIR_PICO2}" -S "${SCRIPT_DIR}" \
    -DPICO_BOARD=pico2 \
    -DLANGUAGE="${LANGUAGE}" \
    > "${BUILD_DIR_PICO2}/cmake.log" 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2350!"
    tail -20 "${BUILD_DIR_PICO2}/cmake.log"
    exit 1
fi

# Build
echo "    Compiling firmware for RP2350..."
cmake --build "${BUILD_DIR_PICO2}" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) \
    > "${BUILD_DIR_PICO2}/build.log" 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2350!"
    tail -30 "${BUILD_DIR_PICO2}/build.log"
    exit 1
fi

echo "    ✅ RP2350 build complete!"
echo ""

################################################################################
# Copy firmware files to dist directory
################################################################################

echo ">>> Collecting firmware files..."
mkdir -p "${DIST_DIR}"

# Copy RP2040 firmware
if [ -f "${BUILD_DIR_PICO}/atari_ikbd_pico.uf2" ]; then
    cp "${BUILD_DIR_PICO}/atari_ikbd_pico.uf2" "${DIST_DIR}/"
    cp "${BUILD_DIR_PICO}/atari_ikbd_pico.elf" "${DIST_DIR}/" 2>/dev/null || true
    cp "${BUILD_DIR_PICO}/atari_ikbd_pico.bin" "${DIST_DIR}/" 2>/dev/null || true
    echo "    ✅ Copied atari_ikbd_pico.uf2"
else
    echo "    ⚠️  Warning: atari_ikbd_pico.uf2 not found!"
fi

# Copy RP2350 firmware
if [ -f "${BUILD_DIR_PICO2}/atari_ikbd_pico2.uf2" ]; then
    cp "${BUILD_DIR_PICO2}/atari_ikbd_pico2.uf2" "${DIST_DIR}/"
    cp "${BUILD_DIR_PICO2}/atari_ikbd_pico2.elf" "${DIST_DIR}/" 2>/dev/null || true
    cp "${BUILD_DIR_PICO2}/atari_ikbd_pico2.bin" "${DIST_DIR}/" 2>/dev/null || true
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
echo "Firmware files available in: ${DIST_DIR}/"
echo ""

if [ -f "${DIST_DIR}/atari_ikbd_pico.uf2" ]; then
    SIZE_PICO=$(ls -lh "${DIST_DIR}/atari_ikbd_pico.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico (RP2040):  atari_ikbd_pico.uf2  (${SIZE_PICO})"
fi

if [ -f "${DIST_DIR}/atari_ikbd_pico2.uf2" ]; then
    SIZE_PICO2=$(ls -lh "${DIST_DIR}/atari_ikbd_pico2.uf2" | awk '{print $5}')
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

