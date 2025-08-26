# Squelch Delta Feature - User Manual

## Introduction

The Squelch Delta feature is a powerful enhancement to the SDR++ scanner module that improves reception quality when jumping between frequency bands and listening to fluctuating signals. This guide explains how to use this feature effectively.

## What is Squelch Delta?

Squelch Delta creates a "hysteresis" effect for the squelch system. In simple terms, it uses two different thresholds:
- A higher threshold to detect signals (opening threshold)
- A lower threshold to maintain reception once a signal is detected (closing threshold)

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

Our implementation enhances this behavior by also preemptively setting the thermostat to the lower threshold when changing rooms (like when tuning to a new frequency), which helps prevent temperature fluctuations during the transition.

Similarly, squelch delta prevents your radio from rapidly opening and closing the squelch when signal strength hovers around the threshold.

## Benefits

- **Prevents Squelch Flutter**: Eliminates annoying rapid opening/closing when receiving marginal signals
- **Smoother Band Transitions**: Reduces noise bursts when jumping between frequency bands
- **Improves Weak Signal Reception**: Maintains reception during brief signal fades
- **Enhances Scanning Experience**: Creates more pleasant listening with fewer interruptions

## How to Use Squelch Delta

### Location
The Squelch Delta controls are located in the Scanner module, just below the Level control.

### Controls

1. **Squelch Delta Slider**
   - Range: 0 to 10 dB
   - Default: 3 dB
   - Purpose: Sets the difference between opening and closing thresholds

2. **Auto Delta Checkbox**
   - When unchecked (Manual Mode): Uses a fixed delta below the main squelch level
   - When checked (Auto Mode): Calculates closing threshold based on noise floor

### Setting Up Squelch Delta

#### Manual Mode (Recommended for Most Users)

1. First, set your main squelch level in the radio module (or via the Frequency Manager override) to silence background noise
2. In the scanner module, set your desired Squelch Delta value:
   - 0 dB: No hysteresis (only the main squelch level is used)
   - 1-2 dB: Minimal hysteresis (signal must exceed main squelch level to be detected, but can then drop 1-2 dB before squelch closes)
   - 3-4 dB: Moderate hysteresis (signal must exceed main squelch level to be detected, but can then drop 3-4 dB before squelch closes)
   - 5-10 dB: Strong hysteresis (signal must exceed main squelch level to be detected, but can then drop 5-10 dB before squelch closes)

#### Auto Mode (Advanced Users)

1. Enable "Auto Delta" by checking the box
2. Set the delta value to determine how far above the noise floor the closing threshold will be
3. The system will automatically estimate the noise floor as you scan

In Auto Mode:
- Signal detection still uses the main squelch level (opening threshold)
- The closing threshold is calculated as: noise floor + delta value
- For example, with a noise floor of -90 dB and delta of 1 dB, the closing threshold would be -89 dB

## Recommended Settings

### For General Scanning
- Manual Mode
- 3 dB Delta

### For Band Hopping (e.g., VHF Airband to UHF Amateur Radio: 118MHz to 445MHz)
- Manual Mode
- 4-5 dB Delta

### For Weak Signal Work
- Auto Mode
- 0.5-1.0 dB Delta

### For Strong Local Signals
- Manual Mode
- 2-3 dB Delta

## How It Works

Our implementation applies squelch delta in two key scenarios:

### 1. When Tuning to a New Frequency (Preemptive Application)

1. **Before Signal Detection**: When tuning to a new frequency, the squelch delta is applied preemptively
2. **Lower Threshold Active**: The squelch threshold is immediately set to the lower level (main level minus delta)
3. **Benefit**: This prevents noise bursts when jumping between bands with different noise characteristics

### 2. When a Signal is Detected (Standard Hysteresis)

1. **Signal Detection**: When signal strength exceeds the main squelch level (e.g., -50 dB), scanning stops
2. **Delta Activation**: The squelch threshold is temporarily lowered by the delta amount (e.g., to -53 dB with a 3 dB delta)
3. **Hysteresis Effect**: Signal can now fluctuate between the main level and lower level (-50 to -53 dB) without closing the squelch
4. **Resuming Scan**: Only when signal drops below the lower threshold (-53 dB) does scanning resume

> **Note**: This is an enhancement over the original gqrx-scanner project, which only applied squelch delta after detecting a signal. Our implementation applies it both preemptively when tuning and after signal detection.

## Visual Representation

```
Signal Strength
    ^
    |                    Signal Path
    |                      ↓
    |                 ┌────●────┐
    |                 │         │
    |          Opening Threshold (-50 dB)
    |----------------●─────────●----------------
    |                │         │
    |                │ Hysteresis Band (3 dB)
    |                │         │
    |          Closing Threshold (-53 dB)
    |----------------●─────────●----------------
    |                │         │
    |                └────●────┘
    |                      ↑
    |                  Noise Floor
    +---------------------------------> Time
                       ↑       ↑
                    Signal    Signal
                    Detected  Lost
```

## Troubleshooting

### Issue: Too Much Noise
- **Symptom**: Scanner stops on frequencies with only noise
- **Solution**: Decrease the delta value or increase the main squelch level

### Issue: Missing Weak Signals
- **Symptom**: Scanner jumps past known weak signals
- **Solution**: Try Auto Mode or increase the delta value

### Issue: Scanner Gets "Stuck"
- **Symptom**: Scanner stays on a frequency too long after signal fades
- **Solution**: Decrease the delta value

## Advanced Tips

- The noise floor is continuously estimated during scanning to optimize auto mode
- When jumping between bands with very different noise characteristics, manual mode may perform better
- For optimal performance, adjust both the main squelch level and delta value based on band conditions

## International Band Examples

| Region | Band Description | Frequency Range | Recommended Delta |
|--------|-----------------|----------------|------------------|
| Global | VHF Airband | 118-137 MHz | 3-4 dB |
| Europe | DAB Digital Radio | 174-240 MHz | 2-3 dB |
| Global | Amateur 2m | 144-148 MHz | 3-4 dB |
| North America | Public Safety | 450-470 MHz | 4-5 dB |
| Global | Amateur 70cm | 430-440 MHz | 3-4 dB |
| Global | Marine VHF | 156-162 MHz | 3-4 dB |
