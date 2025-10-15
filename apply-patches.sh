#!/bin/bash

# Apply patches to fix build issues with newer toolchains
echo "Applying patches for build compatibility..."

# Fix pioasm CMake version requirement
if [ -f "patches/pioasm-cmake-fix.patch" ]; then
    echo "Applying pioasm CMake fix..."
    cd pico-sdk
    patch -p1 < ../patches/pioasm-cmake-fix.patch
    cd ..
    echo "Pioasm CMake fix applied successfully"
else
    echo "Warning: pioasm-cmake-fix.patch not found"
fi

echo "All patches applied successfully!"


