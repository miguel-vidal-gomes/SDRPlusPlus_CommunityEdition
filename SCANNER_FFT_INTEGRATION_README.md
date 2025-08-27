# Scanner Dedicated FFT Path Integration

This document explains how to integrate the scanner module with the dedicated FFT path that has been added to the `IQFrontEnd` class.

## Overview of Changes

1. **IQFrontEnd Class**: A dedicated FFT path has been added to the `IQFrontEnd` class, with its own FFT size, rate, and window settings.
2. **Scanner Module**: The scanner module has been updated to use this dedicated FFT path instead of the waterfall's FFT.
3. **User Interface**: A new UI control has been added to the scanner module to allow the user to select the FFT size.

## Integration Steps

### 1. IQFrontEnd Class

The `IQFrontEnd` class has been modified to include a dedicated FFT path for the scanner. The changes include:

- New member variables for the scanner FFT path
- New methods to set the scanner FFT size, rate, and window
- New callbacks for the scanner to acquire and release FFT buffers
- Interface commands to allow the scanner to register its callbacks and update the FFT size

### 2. Scanner Module

The scanner module has been modified to use the dedicated FFT path. The changes include:

- New member variables for the scanner FFT path
- New callbacks to acquire and release FFT buffers
- A new UI control to select the FFT size
- Updated worker thread to use the dedicated FFT path
- Removal of the downsampling workaround

### 3. Integration Files

The following files have been provided to help with the integration:

- `scanner_fft_integration.cpp`: Contains code snippets to integrate the scanner module with the dedicated FFT path
- `iq_frontend_interface.cpp`: Contains the interface commands needed for the IQFrontEnd class

## Integration Instructions

1. **Update IQFrontEnd Class**:
   - Add the new member variables and methods to `core/src/signal_path/iq_frontend.h`
   - Add the new implementations to `core/src/signal_path/iq_frontend.cpp`
   - Add the interface commands as shown in `iq_frontend_interface.cpp`

2. **Update Scanner Module**:
   - Add the new member variables and methods to the `ScannerModule` class
   - Update the `postInit` method to register the callbacks
   - Add the new UI control to the `menuHandler` function
   - Update the worker thread to use the dedicated FFT path
   - Update the `saveConfig` and `loadConfig` methods to save and load the FFT size
   - Update the destructor to clean up the FFT buffer

3. **Build and Test**:
   - Build the application
   - Test the scanner with different FFT sizes
   - Verify that the scanner can detect signals regardless of the waterfall's zoom level

## Benefits of the Dedicated FFT Path

1. **Independence from UI**: The scanner's FFT is now completely independent of the waterfall's FFT, so it is not affected by the user's zoom level.
2. **Configurable Resolution**: The user can now select the FFT size for the scanner, allowing them to trade off between resolution and performance.
3. **Improved Performance**: The dedicated FFT path can be optimized for the scanner's needs, potentially improving performance.
4. **Better Signal Detection**: With a dedicated FFT path, the scanner can use a higher resolution FFT for better signal detection, regardless of the waterfall's settings.

## Troubleshooting

If the scanner is not detecting signals after the integration:

1. **Check FFT Size**: Make sure the scanner's FFT size is set appropriately. A larger FFT size provides better frequency resolution but may increase CPU usage.
2. **Check Interface Registration**: Make sure the interface commands are registered correctly in the `IQFrontEnd` class.
3. **Check Callback Registration**: Make sure the scanner's callbacks are registered correctly with the `IQFrontEnd` class.
4. **Check FFT Data**: Add debug logging to verify that the scanner is receiving FFT data from the dedicated FFT path.
5. **Check Signal Detection**: Add debug logging to verify that the signal detection algorithm is working correctly with the new FFT data.
