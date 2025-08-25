# Scanner Module Technical Documentation

## Implementation Status: Complete

The scanner module has been completely redesigned to integrate with the frequency manager system. This implementation addresses the three primary feature requests: frequency manager integration, per-entry tuning profiles, and frequency band support.

---

## System Architecture

### Frequency Manager Integration
The scanner now operates exclusively through the frequency manager interface. Manual frequency range entry has been deprecated in favor of centralized frequency management.

**Configuration Steps:**
1. Configure frequency entries in the Frequency Manager module
2. Enable the scanning flag [S] for entries to be included in scan lists
3. Scanner automatically imports and processes these entries
4. Blacklist management operates on the imported frequency set

### Frequency Band Implementation
Frequency bands allow scanning across defined ranges with configurable step intervals.

**Band Parameters:**
- **Start Frequency**: Lower boundary of scan range
- **End Frequency**: Upper boundary of scan range  
- **Step Frequency**: Interval between frequency checks (5-200 kHz recommended)
- **Tuning Profile**: Applied radio configuration for the entire band

**Scanner Behavior:**
- Band entries use full VFO bandwidth for signal detection
- Step frequency determines scan resolution and speed
- Smaller steps provide better coverage but slower scan rates
- Larger steps enable rapid discovery of active frequencies

### Tuning Profile System
Each frequency manager entry supports an associated tuning profile containing complete radio configuration parameters.

**Profile Parameters:**
- Demodulation mode (NFM, WFM, AM, USB, LSB, CW, DSB, RAW)
- Bandwidth specification in Hz
- Squelch enable/disable and threshold level
- RF gain setting
- De-emphasis configuration
- AGC enable/disable

**Automatic Application:**
- Profiles are applied when the scanner stops on active signals
- Configuration changes occur without user intervention
- Settings are cached to prevent redundant radio adjustments
- Profile application time is optimized for real-time operation

---

## Scanner Control Parameters

### Signal Detection
- **Trigger Level**: Signal strength threshold in dBFS (-120 to 0 range)
- **Passband Ratio**: Detection bandwidth as percentage of VFO width (10-100%)

### Timing Controls  
- **Scan Rate**: Frequency check rate in Hz (5-50 per second)
- **Tuning Time**: Hardware settling delay in milliseconds (100-10000 range)
- **Linger Time**: Duration to remain on active signals in milliseconds (100-10000 range)
- **Interval**: Step size for band scanning in Hz (5000-200000 range)

### Direction Control
- Forward scanning: increasing frequency direction
- Reverse scanning: decreasing frequency direction
- Direction state persists across scanner sessions

---

## Signal Processing

### Adaptive Detection Algorithm
The scanner implements frequency-specific detection algorithms:

**Single Frequency Mode:**
- Uses fixed 5 kHz detection window
- Prevents locking onto adjacent channel activity
- Optimized for monitoring specific assignments

**Band Scanning Mode:**
- Uses full VFO bandwidth for detection
- Captures wide-bandwidth signals effectively
- Suitable for discovery and monitoring applications

### FFT Analysis
- Real-time FFT data processing with mutex protection
- Configurable analysis window and overlap
- Level detection uses peak-hold algorithms
- Processing optimized to prevent UI thread blocking

---

## Blacklist Management

### Functionality
- Dynamic addition of frequencies during scan operation
- Automatic scan resumption after blacklist entry
- Persistent storage across sessions
- Integration with frequency manager naming

### Enhanced Display
- Displays frequency manager entry names when available
- Priority system: single frequency names override band names  
- Fallback to raw frequency display for unnamed entries

---

## Performance Characteristics

### Optimization Features
- Smart profile caching prevents redundant radio configuration
- Single-pass frequency manager integration
- Minimal-copy FFT data handling
- Thread-safe operation with UI responsiveness

### Resource Requirements
- CPU usage scales with scan rate and active frequency count
- Memory usage proportional to frequency manager entry count
- Real-time operation maintained across supported hardware platforms

---

## Integration Notes

### Module Dependencies
- Frequency Manager: Required for frequency list and profile data
- Radio Module: Required for demodulation control and squelch
- Signal Path: Required for VFO control and source management
- Module Communication Manager: Required for inter-module messaging

### Configuration Persistence
- Scanner parameters stored in global configuration
- Frequency manager entries maintain independent storage
- Blacklist entries persist across application sessions
- Direction and timing preferences preserved

---

## Technical Specifications

### Supported Frequency Range
- Limited by hardware capabilities of connected SDR device
- No software-imposed frequency restrictions
- Band definitions support full hardware range

### Timing Specifications
- Minimum scan rate: 5 Hz
- Maximum scan rate: 50 Hz (hardware dependent)
- Tuning settling time: 100-10000 ms (configurable)
- Profile application latency: <10 ms typical

### Signal Analysis
- FFT-based level detection
- Configurable detection bandwidth
- Peak-hold and averaging algorithms
- Threshold detection with hysteresis

---

## Implementation Details

This implementation represents a complete architectural redesign focused on professional scanning applications. The system eliminates manual configuration requirements while providing comprehensive automation for frequency management and radio control.

The codebase maintains backward compatibility with existing configuration files while deprecating legacy scanning modes in favor of the integrated frequency manager approach.
