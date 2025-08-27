# Squelch Delta Feature Guide

## Overview

The Squelch Delta feature implements hysteresis in the squelch system, creating different thresholds for opening and closing the squelch. This prevents rapid on/off cycling ("squelch flutter") when signal strength hovers near the threshold.

## How It Works

### Hysteresis Principle

Squelch Delta creates two separate thresholds:

- **Opening Threshold**: The main squelch level set in the radio module
- **Closing Threshold**: A lower threshold (main level minus delta)

When a signal exceeds the opening threshold, the squelch opens. The signal must then drop below the lower closing threshold before the squelch closes again.

### The Thermostat Analogy

Think of squelch delta like a home thermostat:

- You set your thermostat to 70°F (21°C)
- It doesn't turn on exactly at 70°F (21°C) and off exactly at 70°F (21°C)
- Instead, it might turn on at 69°F (20.5°C) and off at 71°F (21.5°C)
- This 2-degree (1°C) difference prevents rapid on/off cycling

In this analogy:

- The temperature dropping to 69°F (20.5°C) is like a signal exceeding the main squelch level
- The heater turning on is like the scanner stopping and audio passing through
- The temperature must rise to 71°F (21.5°C) before the heater turns off again
- This is like the signal needing to drop below the lower threshold before squelch closes

## Two Operation Modes

### 1. Manual Mode

- You set a fixed delta value (in dB)
- The closing threshold is calculated as: `closing_threshold = squelch_level - delta`
- Example: If squelch is -50 dB and delta is 2.5 dB, the closing threshold becomes -52.5 dB

### 2. Auto Mode

- The system dynamically calculates the delta based on the noise floor
- The closing threshold is placed just above the noise floor: `closing_threshold = noise_floor + delta`
- This adapts to different bands and conditions automatically
- Updates every 250ms when not actively receiving a signal

## Advanced Implementation Details

### Preemptive Application

When tuning to a new frequency, the squelch delta is applied preemptively to prevent initial noise bursts when jumping between bands with different noise characteristics.

### Noise Floor Estimation

In Auto mode, the noise floor is estimated using an exponential moving average (EMA) with a 95% smoothing factor:

```cpp
noise_floor = 0.95 * noise_floor + 0.05 * instantaneous_noise
```

This provides stable noise floor tracking while still adapting to changing conditions.

### Safety Bounds

- Delta values are clamped between 0 and 20 dB
- Closing threshold is never allowed to go below the minimum squelch level (-100 dB)
- Auto mode updates are rate-limited to prevent excessive adjustments

## Recommended Settings

- **Manual Mode**: Start with 2.5 dB delta for general use
- **Auto Mode**: Useful in environments with varying noise floors across different bands
- **Higher Delta**: Use in noisy environments or when signals fade frequently
- **Lower Delta**: Use in quiet environments with stable signals

## Technical Implementation

The squelch delta feature maintains separation between:

1. User-set squelch level (persisted in config)
2. Effective squelch level (runtime value with delta applied)

This ensures the UI remains consistent while allowing the runtime squelch level to be dynamically adjusted based on signal conditions.
