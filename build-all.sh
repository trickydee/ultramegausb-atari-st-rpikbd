#!/bin/bash
################################################################################
# Build Script for Atari ST USB Adapter
# Builds firmware for:
#   - Raspberry Pi Pico (RP2040)
#   - Raspberry Pi Pico 2 (RP2350)
#   - Raspberry Pi Pico 2 W (RP2350 with WiFi/Bluetooth)
#   - Raspberry Pi Pico W (RP2040 with WiFi)
#
# Build Variants:
#   - debug: OLED=1, LOGGING=1 (full features, debug enabled)
#   - production: OLED=1, LOGGING=0 (OLED enabled, minimal logging)
#   - speed: OLED=0, LOGGING=0 (no OLED, minimal logging)
#
# Environment variables:
#   CLEAN_BUILD_DIRS  - 1 (default) remove build-* dirs after UF2 copied to dist/
#                       0 keep build directories for incremental rebuilds / debugging
#   SKIP_VARIANTS     - 1 (default) build only the variant selected by BUILD_VARIANT
#                       0 after a debug build, also build production and speed
#   DEBUG             - 0 (default) production UI; 1 = debug OLED screens
#   LANGUAGE          - EN (default), FR, DE, SP, or IT
#   BUILD_VARIANT     - production (default), debug, or speed
#
# IMPORTANT: Build directories (build/build-pico, build/build-pico2, etc.)
#            should NEVER be committed to git. They are automatically generated
#            and cleaned by this script. Make sure .gitignore includes build/
################################################################################

set -e  # Exit on error

# Configuration
LANGUAGE="${LANGUAGE:-EN}"
DEBUG="${DEBUG:-0}"  # Set to 1 to enable debug OLED screens
BUILD_VARIANT="${BUILD_VARIANT:-production}"  # production, debug, or speed
SKIP_VARIANTS="${SKIP_VARIANTS:-1}"  # 1 = single variant; 0 = after debug, also build production and speed
CLEAN_BUILD_DIRS="${CLEAN_BUILD_DIRS:-1}"  # 1 = remove build dirs after successful dist copy

# All CMake build trees live under ./build/ (e.g. build/build-pico2_w)
BUILD_ROOT="./build"
DIR_PICO2_W="${BUILD_ROOT}/build-pico2_w"
DIR_PICOW="${BUILD_ROOT}/build-picow"
DIR_PICO2="${BUILD_ROOT}/build-pico2"
DIR_PICO="${BUILD_ROOT}/build-pico"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

mkdir -p "${BUILD_ROOT}"

# Remove legacy root-level build dirs from older script versions
for legacy in build-pico build-pico2 build-pico2_w build-picow build-pico_w; do
    if [ -d "./${legacy}" ]; then
        rm -rf "./${legacy}"
        echo "    Removed legacy ./${legacy}/ (build trees now live under ./build/)"
    fi
done

# Map build variant to OLED and LOGGING settings
case "$BUILD_VARIANT" in
    "production")
        OLED="${OLED:-1}"
        LOGGING="${LOGGING:-0}"
        ;;
    "speed")
        OLED="${OLED:-0}"
        LOGGING="${LOGGING:-0}"
        ;;
    "debug"|*)
        OLED="${OLED:-1}"
        LOGGING="${LOGGING:-1}"
        BUILD_VARIANT="debug"
        ;;
esac

echo "================================================================================"
echo "  Atari ST USB Adapter - Multi-Variant Build"
echo "  Build Variant: ${BUILD_VARIANT}"
echo "  Language: ${LANGUAGE}"
echo "  Debug Mode: ${DEBUG}"
echo "  OLED Display: ${OLED}"
echo "  Serial Logging: ${LOGGING}"
echo "  Skip Variants: ${SKIP_VARIANTS}"
echo "  Clean Build Dirs: ${CLEAN_BUILD_DIRS}"
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

################################################################################
# Build for Raspberry Pi Pico 2 W (RP2350 with WiFi/Bluetooth)
################################################################################

echo ">>> Building for Raspberry Pi Pico 2 W (RP2350 with WiFi/Bluetooth)..."
echo ""

# Check for Bluepad32 submodule (required for Bluetooth)
BLUEPAD32_OK=false
if [ -d "./bluepad32/src" ] && [ -f "./bluepad32/src/components/bluepad32/CMakeLists.txt" ]; then
    BLUEPAD32_OK=true
    echo "    ✅ Bluepad32 is properly initialized"
else
    echo "    ⚠️  Bluepad32 missing - initializing..."
    if ! git submodule update --init --recursive bluepad32; then
        echo "    ⚠️  Warning: Bluepad32 initialization failed, skipping Pico 2 W build"
        BLUEPAD32_OK=false
    else
        BLUEPAD32_OK=true
        echo "    ✅ Bluepad32 initialized successfully"
    fi
fi

if [ "$BLUEPAD32_OK" = true ]; then
    # Clean and create build directory
    rm -rf ${DIR_PICO2_W}
    mkdir -p ${DIR_PICO2_W}

    # Configure
    echo "    Configuring CMake for RP2350 WiFi/Bluetooth (pico2_w)..."
    if ! cmake -B ${DIR_PICO2_W} -S . \
        -DPICO_BOARD=pico2_w \
        -DLANGUAGE="${LANGUAGE}" \
        -DENABLE_DEBUG="${DEBUG}" \
        -DENABLE_OLED_DISPLAY="${OLED}" \
        -DENABLE_SERIAL_LOGGING="${LOGGING}" \
        > ${DIR_PICO2_W}/cmake.log 2>&1; then
        echo "    ⚠️  Warning: CMake configuration failed for RP2350 WiFi/Bluetooth (pico2_w)!"
        tail -20 ${DIR_PICO2_W}/cmake.log
    else
        # Build (|| true: set -e must not abort — other board variants should still build)
        echo "    Compiling firmware for RP2350 WiFi/Bluetooth (this may take longer due to Bluepad32)..."
        if ! cmake --build "${DIR_PICO2_W}" -j"${JOBS}" \
            > ${DIR_PICO2_W}/build.log 2>&1; then
            echo "    ⚠️  Warning: Build failed for RP2350 WiFi/Bluetooth (pico2_w)!"
            tail -30 ${DIR_PICO2_W}/build.log
        else
            echo "    ✅ RP2350 WiFi/Bluetooth (pico2_w) build complete!"
        fi
    fi
fi

echo ""

################################################################################
# Build for Raspberry Pi Pico W (RP2040 with WiFi)
################################################################################

echo ">>> Building for Raspberry Pi Pico W (RP2040 with WiFi)..."
echo ""

# Clean and create build directory
rm -rf ${DIR_PICOW}
mkdir -p ${DIR_PICOW}

# Configure
echo "    Configuring CMake for RP2040 WiFi (pico_w)..."
cmake -B ${DIR_PICOW} -S . \
    -DPICO_BOARD=pico_w \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ${DIR_PICOW}/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2040 WiFi (pico_w)!"
    tail -20 ${DIR_PICOW}/cmake.log
    exit 1
fi

# Build
echo "    Compiling firmware for RP2040 WiFi (pico_w)..."
cmake --build "${DIR_PICOW}" -j"${JOBS}" \
    > ${DIR_PICOW}/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2040 WiFi (pico_w)!"
    tail -30 ${DIR_PICOW}/build.log
    exit 1
fi

echo "    ✅ RP2040 WiFi (pico_w) build complete!"
echo ""

################################################################################
# Build for Raspberry Pi Pico 2 (RP2350)
################################################################################

echo ">>> Building for Raspberry Pi Pico 2 (RP2350)..."
echo ""

# Clean and create build directory
rm -rf ${DIR_PICO2}
mkdir -p ${DIR_PICO2}

# Configure
echo "    Configuring CMake for RP2350..."
cmake -B ${DIR_PICO2} -S . \
    -DPICO_BOARD=pico2 \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ${DIR_PICO2}/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2350!"
    tail -20 ${DIR_PICO2}/cmake.log
    exit 1
fi

# Build
echo "    Compiling firmware for RP2350..."
cmake --build "${DIR_PICO2}" -j"${JOBS}" \
    > ${DIR_PICO2}/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2350!"
    tail -30 ${DIR_PICO2}/build.log
    exit 1
fi

echo "    ✅ RP2350 build complete!"
echo ""

################################################################################
# Build for Raspberry Pi Pico (RP2040)
################################################################################

echo ">>> Building for Raspberry Pi Pico (RP2040)..."
echo ""

# Clean and create build directory
rm -rf ${DIR_PICO}
mkdir -p ${DIR_PICO}

# Configure
echo "    Configuring CMake for RP2040..."
cmake -B ${DIR_PICO} -S . \
    -DPICO_BOARD=pico \
    -DLANGUAGE="${LANGUAGE}" \
    -DENABLE_DEBUG="${DEBUG}" \
    -DENABLE_OLED_DISPLAY="${OLED}" \
    -DENABLE_SERIAL_LOGGING="${LOGGING}" \
    > ${DIR_PICO}/cmake.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed for RP2040!"
    tail -20 ${DIR_PICO}/cmake.log
    exit 1
fi

# Build
echo "    Compiling firmware for RP2040..."
cmake --build "${DIR_PICO}" -j"${JOBS}" \
    > ${DIR_PICO}/build.log 2>&1

if [ $? -ne 0 ]; then
    echo "ERROR: Build failed for RP2040!"
    tail -30 ${DIR_PICO}/build.log
    exit 1
fi

echo "    ✅ RP2040 build complete!"
echo ""

################################################################################
# Copy firmware files to dist directory with variant-specific names
################################################################################

echo ">>> Collecting firmware files..."
mkdir -p ./dist

# Build variant suffix
VARIANT_SUFFIX="_${BUILD_VARIANT}"

# Copy RP2040 firmware
COPIED_PICO=false
if [ -f "${DIR_PICO}/atari_ikbd_pico.uf2" ]; then
    cp ${DIR_PICO}/atari_ikbd_pico.uf2 ./dist/atari_ikbd_pico${VARIANT_SUFFIX}.uf2
    echo "    ✅ Copied atari_ikbd_pico${VARIANT_SUFFIX}.uf2"
    COPIED_PICO=true
else
    echo "    ⚠️  Warning: atari_ikbd_pico.uf2 not found!"
fi

# Copy RP2040 WiFi (Pico W) firmware
COPIED_PICOW=false
if [ -f "${DIR_PICOW}/atari_ikbd_pico.uf2" ]; then
    cp ${DIR_PICOW}/atari_ikbd_pico.uf2 ./dist/atari_ikbd_pico_w${VARIANT_SUFFIX}.uf2
    echo "    ✅ Copied atari_ikbd_pico_w${VARIANT_SUFFIX}.uf2"
    COPIED_PICOW=true
else
    echo "    ⚠️  Warning: atari_ikbd_pico.uf2 for Pico W (pico_w) not found!"
fi

# Copy RP2350 firmware
COPIED_PICO2=false
if [ -f "${DIR_PICO2}/atari_ikbd_pico2.uf2" ]; then
    cp ${DIR_PICO2}/atari_ikbd_pico2.uf2 ./dist/atari_ikbd_pico2${VARIANT_SUFFIX}.uf2
    echo "    ✅ Copied atari_ikbd_pico2${VARIANT_SUFFIX}.uf2"
    COPIED_PICO2=true
else
    echo "    ⚠️  Warning: atari_ikbd_pico2.uf2 not found!"
fi

# Copy RP2350 WiFi/Bluetooth (Pico 2 W) firmware
COPIED_PICO2_W=false
if [ -f "${DIR_PICO2_W}/atari_ikbd_pico2_w.uf2" ]; then
    cp ${DIR_PICO2_W}/atari_ikbd_pico2_w.uf2 ./dist/atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2
    echo "    ✅ Copied atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2"
    COPIED_PICO2_W=true
elif [ -f "${DIR_PICO2_W}/atari_ikbd_pico2.uf2" ]; then
    cp ${DIR_PICO2_W}/atari_ikbd_pico2.uf2 ./dist/atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2
    echo "    ✅ Copied atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2"
    COPIED_PICO2_W=true
fi

echo ""

################################################################################
# Remove build directories after successful dist copy (optional)
################################################################################

if [ "${CLEAN_BUILD_DIRS}" = "1" ]; then
    echo ">>> Cleaning build directories..."
    if [ "$COPIED_PICO" = true ] && [ -d "${DIR_PICO}" ]; then
        rm -rf "${DIR_PICO}"
        echo "    ✅ Removed ${DIR_PICO}"
    fi
    if [ "$COPIED_PICOW" = true ] && [ -d "${DIR_PICOW}" ]; then
        rm -rf "${DIR_PICOW}"
        echo "    ✅ Removed ${DIR_PICOW}"
    fi
    if [ "$COPIED_PICO2" = true ] && [ -d "${DIR_PICO2}" ]; then
        rm -rf "${DIR_PICO2}"
        echo "    ✅ Removed ${DIR_PICO2}"
    fi
    if [ "$COPIED_PICO2_W" = true ] && [ -d "${DIR_PICO2_W}" ]; then
        rm -rf "${DIR_PICO2_W}"
        echo "    ✅ Removed ${DIR_PICO2_W}"
    fi
    echo ""
fi

################################################################################
# Display firmware information
################################################################################

echo "================================================================================"
echo "  BUILD COMPLETE!"
echo "================================================================================"
echo ""
echo "Firmware files available in: ./dist/"
echo ""
echo "  Build Variant: ${BUILD_VARIANT}"
echo "  - OLED Display: ${OLED}"
echo "  - Serial Logging: ${LOGGING}"
echo ""

if [ -f "./dist/atari_ikbd_pico${VARIANT_SUFFIX}.uf2" ]; then
    SIZE_PICO=$(ls -lh "./dist/atari_ikbd_pico${VARIANT_SUFFIX}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico (RP2040):  atari_ikbd_pico${VARIANT_SUFFIX}.uf2  (${SIZE_PICO})"
fi

if [ -f "./dist/atari_ikbd_pico2${VARIANT_SUFFIX}.uf2" ]; then
    SIZE_PICO2=$(ls -lh "./dist/atari_ikbd_pico2${VARIANT_SUFFIX}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico 2 (RP2350): atari_ikbd_pico2${VARIANT_SUFFIX}.uf2 (${SIZE_PICO2})"
fi

if [ -f "./dist/atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2" ]; then
    SIZE_PICO2_W=$(ls -lh "./dist/atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico 2 W (RP2350 + Bluetooth): atari_ikbd_pico2_w${VARIANT_SUFFIX}.uf2 (${SIZE_PICO2_W})"
fi

if [ -f "./dist/atari_ikbd_pico_w${VARIANT_SUFFIX}.uf2" ]; then
    SIZE_PICOW=$(ls -lh "./dist/atari_ikbd_pico_w${VARIANT_SUFFIX}.uf2" | awk '{print $5}')
    echo "  📦 Raspberry Pi Pico W (RP2040 WiFi): atari_ikbd_pico_w${VARIANT_SUFFIX}.uf2 (${SIZE_PICOW})"
fi

echo ""
echo "To flash firmware:"
echo "  1. Hold BOOTSEL button on your Pico/Pico 2"
echo "  2. Connect USB cable"
echo "  3. Release BOOTSEL button"
echo "  4. Copy the appropriate .uf2 file to the RPI-RP2 drive"
echo ""
echo "================================================================================"

# Automatically build production & speed variants after debug build (unless skipped)
if [ "${SKIP_VARIANTS:-0}" != "1" ] && [ "${BUILD_VARIANT}" = "debug" ]; then
    echo ""
    echo ">>> Auto-building production variant (OLED=1, LOGGING=0)..."
    BUILD_VARIANT=production SKIP_VARIANTS=1 "$0"
    echo ""
    echo ">>> Auto-building speed variant (OLED=0, LOGGING=0)..."
    BUILD_VARIANT=speed SKIP_VARIANTS=1 "$0"
fi
