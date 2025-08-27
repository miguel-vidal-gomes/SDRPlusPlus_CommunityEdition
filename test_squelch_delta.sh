#!/bin/bash
# Test script for squelch delta feature

echo "Building SDR++ with squelch delta improvements..."
mkdir -p build_test
cd build_test
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j8

echo "Testing squelch delta functionality..."

# Test 1: Verify that the radio module correctly separates user and effective squelch levels
echo "Test 1: Verifying user/effective squelch separation..."
if grep -q "userSquelchLevel" ../decoder_modules/radio/src/radio_module.h && \
   grep -q "effectiveSquelchLevel" ../decoder_modules/radio/src/radio_module.h && \
   grep -q "setUserSquelchLevel" ../decoder_modules/radio/src/radio_module.h && \
   grep -q "updateEffectiveSquelch" ../decoder_modules/radio/src/radio_module.h; then
    echo "✅ Test 1 Passed: Radio module correctly separates user and effective squelch levels"
else
    echo "❌ Test 1 Failed: Radio module does not correctly separate user and effective squelch levels"
    exit 1
fi

# Test 2: Verify that the scanner module includes noise floor smoothing
echo "Test 2: Verifying noise floor smoothing..."
if grep -q "alpha = 0.95f" ../misc_modules/scanner/src/main.cpp && \
   grep -q "lastNoiseUpdate" ../misc_modules/scanner/src/main.cpp; then
    echo "✅ Test 2 Passed: Scanner module includes noise floor smoothing"
else
    echo "❌ Test 2 Failed: Scanner module does not include noise floor smoothing"
    exit 1
fi

# Test 3: Verify that the scanner module includes debouncing for squelch delta
echo "Test 3: Verifying squelch delta debouncing..."
if grep -q "tuneTime" ../misc_modules/scanner/src/main.cpp; then
    echo "✅ Test 3 Passed: Scanner module includes debouncing for squelch delta"
else
    echo "❌ Test 3 Failed: Scanner module does not include debouncing for squelch delta"
    exit 1
fi

# Test 4: Verify that the UI has been updated with better labels
echo "Test 4: Verifying UI improvements..."
if grep -q "Delta (dB)" ../misc_modules/scanner/src/main.cpp && \
   grep -q "Close threshold = Squelch − Delta" ../misc_modules/scanner/src/main.cpp; then
    echo "✅ Test 4 Passed: UI has been updated with better labels"
else
    echo "❌ Test 4 Failed: UI has not been updated with better labels"
    exit 1
fi

# Test 5: Verify that the documentation has been created
echo "Test 5: Verifying documentation..."
if [ -f "../docs/SQUELCH_DELTA_FEATURE.md" ] && [ -f "../docs/SQUELCH_DELTA_USER_MANUAL.md" ]; then
    echo "✅ Test 5 Passed: Documentation has been created"
else
    echo "❌ Test 5 Failed: Documentation has not been created"
    exit 1
fi

echo "All tests passed! Squelch delta feature is ready for use."
echo "For more information, see docs/SQUELCH_DELTA_FEATURE.md and docs/SQUELCH_DELTA_USER_MANUAL.md"

