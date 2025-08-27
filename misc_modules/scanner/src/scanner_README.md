# Scanner Module with Dedicated FFT Processing

## Overview

The Scanner module now includes a dedicated FFT processing engine that makes signal detection accuracy independent from the visual waterfall FFT size. This feature ensures consistent scanner performance regardless of the UI FFT settings.

## Key Features

- **FFT Size Independence**: Scanner detection accuracy is no longer tied to the visual FFT size
- **CFAR Detection**: Uses Constant False Alarm Rate detection for better signal detection in varying noise conditions
- **Sub-bin Interpolation**: Provides frequency accuracy beyond the FFT bin resolution using parabolic interpolation
- **Hz/Time Units**: All detection parameters are expressed in Hz and milliseconds, not bins or frames
- **Configurable Parameters**: Full control over FFT size, window function, overlap, averaging time, and detection thresholds

## Settings

### FFT Processing

- **Use Dedicated FFT**: Enable/disable the dedicated FFT engine (enabled by default)
- **FFT Size**: Select from 16K to 1048K points (default: 524K for optimal accuracy)
- **Window Function**: Choose from Rectangular, Blackman, Blackman-Harris 7, Hamming, or Hann
- **Overlap %**: Set the overlap percentage between consecutive FFT frames (default: 50%)
- **Averaging (ms)**: Time constant for spectrum averaging (default: 200ms)

### CFAR Detection

- **Threshold (dB)**: Signal detection threshold above local noise floor (default: 8dB)
- **Guard Band (Hz)**: Width of guard band around signal (default: 2000Hz)
- **Reference (Hz)**: Width of reference band for noise floor calculation (default: 15000Hz)
- **Min Width (Hz)**: Minimum width of a valid signal (default: 8000Hz)
- **Merge Width (Hz)**: Distance to merge adjacent signals (default: 2000Hz)

## Technical Implementation

The dedicated FFT engine operates independently from the visual waterfall FFT:
1. It taps directly into the IQ stream from the SDR source
2. Processes IQ samples with configurable FFT parameters
3. Applies time-domain averaging using an Exponential Moving Average (EMA)
4. Uses CFAR detection with local noise floor estimation
5. Refines peak frequency using parabolic interpolation for sub-bin accuracy

This implementation ensures consistent scanner performance regardless of visual FFT settings, providing the best possible signal detection accuracy.