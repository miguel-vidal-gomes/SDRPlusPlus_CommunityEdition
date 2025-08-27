#!/bin/bash

# Script to integrate the scanner dedicated FFT path

# Exit on error
set -e

# Check if we're in the right directory
if [ ! -d "core/src/signal_path" ] || [ ! -d "misc_modules/scanner" ]; then
    echo "Error: This script must be run from the SDR++ root directory"
    exit 1
fi

echo "Integrating scanner dedicated FFT path..."

# 1. Update IQFrontEnd header
echo "Updating IQFrontEnd header..."
cp core/src/signal_path/iq_frontend.h core/src/signal_path/iq_frontend.h.bak
echo "Backup created: core/src/signal_path/iq_frontend.h.bak"

# 2. Update IQFrontEnd implementation
echo "Updating IQFrontEnd implementation..."
cp core/src/signal_path/iq_frontend.cpp core/src/signal_path/iq_frontend.cpp.bak
echo "Backup created: core/src/signal_path/iq_frontend.cpp.bak"

# 3. Update Scanner module
echo "Updating Scanner module..."
cp misc_modules/scanner/src/main.cpp misc_modules/scanner/src/main.cpp.bak
echo "Backup created: misc_modules/scanner/src/main.cpp.bak"

# 4. Build the application
echo "Building the application..."
rm -rf build
mkdir build
cd build
cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 .. -DUSE_BUNDLE_DEFAULTS=ON -DCMAKE_BUILD_TYPE=Release
make -j8
cd ..

# 5. Create the macOS app bundle
echo "Creating the macOS app bundle..."
./make_macos_bundle.sh build ./SDR++CE.app

echo "Integration complete!"
echo "Please test the scanner with the dedicated FFT path."
echo "See SCANNER_FFT_INTEGRATION_README.md for more information."
