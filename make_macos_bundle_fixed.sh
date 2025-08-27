#!/bin/sh
set -e

# ========================= Boilerplate =========================
BUILD_DIR=$1
BUNDLE=$2

source macos/bundle_utils.sh

# ========================= Prepare dotapp structure =========================

# Clear .app
rm -rf $BUNDLE

# Create .app structure
bundle_create_struct $BUNDLE

# Add resources
cp -R root/res/* $BUNDLE/Contents/Resources/

# Create the icon file
bundle_create_icns root/res/icons/sdrpp_ce.macos.png $BUNDLE/Contents/Resources/sdrppce

# Create the property list
bundle_create_plist sdrppce "SDR++CE Community Edition" org.sdrppce.sdrppce 1.2.3-CE sdrp sdrpp_ce sdrppce $BUNDLE/Contents/Info.plist

# ========================= Install binaries =========================

# Core
echo "Installing core binaries..."
bundle_install_binary $BUNDLE $BUNDLE/Contents/MacOS $BUILD_DIR/sdrpp_ce 
bundle_install_binary $BUNDLE $BUNDLE/Contents/Frameworks $BUILD_DIR/core/libsdrpp_core.dylib

# Source modules
echo "Installing source modules..."
for module in $BUILD_DIR/source_modules/*/*.dylib; do
    if [ -f "$module" ]; then
        echo "Installing $module"
        bundle_install_binary $BUNDLE $BUNDLE/Contents/Plugins $module
    fi
done

# Sink modules
echo "Installing sink modules..."
for module in $BUILD_DIR/sink_modules/*/*.dylib; do
    if [ -f "$module" ]; then
        echo "Installing $module"
        bundle_install_binary $BUNDLE $BUNDLE/Contents/Plugins $module
    fi
done

# Decoder modules
echo "Installing decoder modules..."
for module in $BUILD_DIR/decoder_modules/*/*.dylib; do
    if [ -f "$module" ]; then
        echo "Installing $module"
        bundle_install_binary $BUNDLE $BUNDLE/Contents/Plugins $module
    fi
done

# Misc modules
echo "Installing misc modules..."
for module in $BUILD_DIR/misc_modules/*/*.dylib; do
    if [ -f "$module" ]; then
        echo "Installing $module"
        bundle_install_binary $BUNDLE $BUNDLE/Contents/Plugins $module
    fi
done

# ========================= Finalize =========================

# Sign the app
bundle_sign $BUNDLE