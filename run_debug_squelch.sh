#!/bin/bash

# Create a log file with timestamp
LOG_FILE="squelch_delta_debug_$(date +%Y%m%d_%H%M%S).log"

echo "Starting SDR++ with squelch delta debugging..."
echo "Debug log will be saved to: $LOG_FILE"

# Run the app and capture all output to the log file
./SDR++_debug_squelch.app/Contents/MacOS/sdrpp_ce 2>&1 | tee "$LOG_FILE"

echo "SDR++ closed. Debug log saved to: $LOG_FILE"

