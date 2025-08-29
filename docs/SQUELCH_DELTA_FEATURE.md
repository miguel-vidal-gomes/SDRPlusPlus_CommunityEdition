# Squelch Delta Feature Technical Documentation

## Overview

The Squelch Delta feature implements hysteresis in the squelch system, creating different thresholds for opening and closing the squelch. This prevents rapid on/off cycling ("squelch flutter") when signal strength hovers near the threshold.

## Architecture

### Core Components

1. **Radio Module**
   - `userSquelchLevel`: User-set squelch level (persisted in config)
   - `effectiveSquelchLevel`: Runtime squelch level with delta applied
   - `setUserSquelchLevel()`: Sets user level and updates effective level
   - `setEffectiveSquelchLevel()`: Applies level to DSP path without persisting
   - `updateEffectiveSquelch()`: Updates effective level based on current settings

2. **Scanner Module**
   - `squelchDelta`: Difference between opening and closing thresholds (0-10 dB)
   - `squelchDeltaAuto`: Flag for auto mode calculation
   - `noiseFloor`: Estimated noise floor for auto mode
   - `squelchDeltaActive`: Tracks whether delta is currently applied
   - `applySquelchDelta()`: Applies delta when signal detected
   - `restoreSquelchLevel()`: Restores original level when signal lost

### Interface Commands

- `RADIO_IFACE_CMD_GET_SQUELCH_LEVEL`: Gets user-set squelch level
- `RADIO_IFACE_CMD_SET_SQUELCH_LEVEL`: Sets user squelch level
- `RADIO_IFACE_CMD_GET_EFFECTIVE_SQUELCH_LEVEL`: Gets runtime squelch level with delta applied

## Implementation Details

### Dual Threshold Model

1. **Opening Threshold**: The main squelch level set in the radio module
   - Used to detect signals initially
   - Controlled by the user via the squelch slider

2. **Closing Threshold**: A lower threshold (main level minus delta)
   - Used to maintain reception once a signal is detected
   - Calculated as:
     - Manual Mode: `userSquelchLevel - squelchDelta`
     - Auto Mode: `noiseFloor + squelchDelta`

### Preemptive Application

When tuning to a new frequency, the squelch delta is applied preemptively to prevent initial noise bursts when jumping between bands with different noise characteristics.

### Auto Mode

The auto mode dynamically calculates the closing threshold based on the estimated noise floor:
- Noise floor is estimated using an exponential moving average (EMA)
- Updates occur every 250ms when not receiving a signal
- Smoothing factor of 95% for stability

### Thread Safety

The implementation includes checks to prevent race conditions and deadlocks:
- Squelch state is checked before applying or restoring delta
- Error handling with try/catch blocks protects against exceptions

## Performance Considerations

- The closing threshold is never allowed to go below the minimum squelch level (-100 dB)
- Auto mode updates are rate-limited to avoid excessive CPU usage
- Frequency manager name lookups are cached to improve UI responsiveness

## Configuration

Default settings:
- Delta: 2.5 dB
- Auto Mode: Off
- Minimum Delta: 0 dB
- Maximum Delta: 10 dB

## Future Enhancements

Potential improvements for future versions:
- Visual indication of both thresholds on the squelch slider
- More sophisticated noise floor estimation algorithms
- Per-band squelch delta settings