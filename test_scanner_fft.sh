#!/bin/bash

# Script to test the scanner dedicated FFT path

# Exit on error
set -e

# Check if we're in the right directory
if [ ! -d "core/src/signal_path" ] || [ ! -d "misc_modules/scanner" ]; then
    echo "Error: This script must be run from the SDR++ root directory"
    exit 1
fi

echo "Testing scanner dedicated FFT path..."

# 1. Open the application
echo "Opening SDR++CE.app..."
open SDR++CE.app

# 2. Instructions for manual testing
echo ""
echo "Manual Testing Instructions:"
echo "1. Start the SDR source"
echo "2. Open the Scanner module"
echo "3. Try different FFT sizes and verify that the scanner can detect signals"
echo "4. Change the waterfall's zoom level and verify that the scanner still works"
echo ""
echo "Press Enter to continue..."
read

# 3. Check for scanner log messages
echo "Checking for scanner log messages..."
grep -i "scanner" ~/Library/Application\ Support/sdrpp/log.txt | tail -n 20

echo "Test complete!"
echo "Please verify that the scanner is working correctly."
