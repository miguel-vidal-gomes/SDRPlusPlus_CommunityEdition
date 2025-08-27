#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <signal_path/iq_frontend.h>  // Include for iq_frontend constants
#include <chrono>
#include <algorithm>
#include <fstream>
#include <core.h>
#include <radio_interface.h>
#include <atomic>  // For atomic flag

// Global flag to check if iq_frontend interface is available
static std::atomic<bool> g_hasIQFrontendIface = false;
#include <sstream>
#include <cstring>
#include <set>
#include "scanner_log.h"

// Windows MSVC compatibility 
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Frequency range structure for multiple scanning ranges
struct FrequencyRange {
    std::string name;
    double startFreq;
    double stopFreq;
    bool enabled;
    float gain;  // Gain setting for this frequency range (in dB)
    
    FrequencyRange() : name("New Range"), startFreq(88000000.0), stopFreq(108000000.0), enabled(true), gain(20.0f) {}
    FrequencyRange(const std::string& n, double start, double stop, bool en = true, float g = 20.0f) 
        : name(n), startFreq(start), stopFreq(stop), enabled(en), gain(g) {}
};

SDRPP_MOD_INFO{
    /* Name:            */ "scanner",
    /* Description:     */ "Scanner module for SDR++",
    /* Author:          */ "Ryzerth",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

ConfigManager config;

// Forward declarations for frequency manager integration
struct FrequencyBookmark;
class FrequencyManagerModule;

// CRITICAL: Use frequency manager's real TuningProfile struct (no local copy!)
// Forward declaration only - real definition in frequency_manager module
struct TuningProfile {
    // INTERFACE CONTRACT: This must match frequency_manager's TuningProfile exactly
    // Fields are accessed via interface, not direct member access
    int demodMode;
    float bandwidth;
    bool squelchEnabled;
    float squelchLevel;
    int deemphasisMode;
    bool agcEnabled;
    float rfGain;
    double centerOffset;
    std::string name;
    bool autoApply;
    
    // SAFETY: Only access this struct through frequency manager interface
    // Direct field access is UNSAFE due to potential ABI differences
};

class ScannerModule : public ModuleManager::Instance {
public:
    ScannerModule(std::string name) {
        this->name = name;
        
        // Initialize time points to current time to prevent crashes
        auto now = std::chrono::high_resolution_clock::now();
        lastSignalTime = now;
        lastTuneTime = now;
        
        // Ensure scanner starts in a safe state
        running = false;
        tuning = false;
        receiving = false;
        
        flog::info("Scanner: Initializing scanner module '{}'", name);
        
        gui::menu.registerEntry(name, menuHandler, this, NULL);

        config.acquire();
        startFreq = config.conf.value("startFreq", 88000000.0);
        stopFreq = config.conf.value("stopFreq", 108000000.0);
        level = config.conf.value("level", -40.0f);
        interval = config.conf.value("interval", 250);
        scanRate = config.conf.value("scanRate", 10);
        lingerTime = config.conf.value("lingerTime", 1000);
        tuningTime = config.conf.value("tuningTime", 100);
        scanUp = config.conf.value("scanUp", true);
        reverseLock = config.conf.value("reverseLock", false);
        oneShot = config.conf.value("oneShot", false);
        useFrequencyManager = config.conf.value("useFrequencyManager", true);
        applyProfiles = config.conf.value("applyProfiles", true);
        cycleAfterLinger = config.conf.value("cycleAfterLinger", false);
        dwellTime = config.conf.value("dwellTime", 0);
        squelchDelta = config.conf.value("squelchDelta", 0.0f);
        squelchDeltaAuto = config.conf.value("squelchDeltaAuto", false);
        scannerFftSize = config.conf.value("scannerFftSize", 8192);

        // Load blacklist
        blacklist.clear();
        json blacklist_json = config.conf["blacklist"];
        for (auto& element : blacklist_json) {
            blacklist.insert(element.get<double>());
        }

        // Load frequency ranges
        frequencyRanges.clear();
        for (auto& item : config.conf["frequencyRanges"]) {
            frequencyRanges.emplace_back(
                item["name"].get<std::string>(),
                item["startFreq"].get<double>(),
                item["stopFreq"].get<double>(),
                item["enabled"].get<bool>(),
                item["gain"].get<float>()
            );
        }

        // Find the matching FFT size index
        for (int i = 0; i < SCANNER_FFT_SIZE_COUNT; i++) {
            if (SCANNER_FFT_SIZES[i] == scannerFftSize) {
                scannerFftSizeIndex = i;
                break;
            }
        }
    }

    ~ScannerModule() {
        saveConfig();
        gui::menu.removeEntry(name);
        stop();
        if (scannerFftData) {
            delete[] scannerFftData;
            scannerFftData = nullptr;
        }
    }

    void postInit() {
        // Proactively allocate the FFT buffer to prevent race conditions
        if (scannerFftData) {
            delete[] scannerFftData;
        }
        scannerFftData = new float[scannerFftSize];
        
        // Check if interface is registered before trying to use it
        if (core::modComManager.interfaceExists(iq_interface::kIQFrontendIface)) {
            flog::info("Scanner: Found iq_frontend interface, configuring scanner FFT");
            g_hasIQFrontendIface = true;
            
            // First, set the scanner's FFT size in the IQFrontEnd
            flog::info("Scanner: Sending FFT size {0} to IQFrontEnd interface", scannerFftSize);
            if (!core::modComManager.callInterface(iq_interface::kIQFrontendIface, 0, &scannerFftSize, nullptr)) {
                flog::error("Scanner: Failed to set FFT size via interface");
                g_hasIQFrontendIface = false;
                return;
            }

            // Then, register the buffer acquisition and release callbacks
            void* callbacks[] = { (void*)acquireScannerFFTBuffer, (void*)releaseScannerFFTBuffer, this };
            if (!core::modComManager.callInterface(iq_interface::kIQFrontendIface, 1, callbacks, nullptr)) {
                flog::error("Scanner: Failed to register callbacks via interface");
                g_hasIQFrontendIface = false;
                return;
            }
            
            flog::info("Scanner: Successfully registered with iq_frontend interface");
        } else {
            flog::warn("Scanner: iq_frontend interface not available, continuing with limited functionality");
            g_hasIQFrontendIface = false;
        }
    }

    void enable() {
        enabled = true;
    }

    void disable() {
        enabled = false;
    }

    bool isEnabled() {
        return enabled;
    }
    
    // Range management methods
    void addFrequencyRange(const std::string& name, double start, double stop, bool enabled = true, float gain = 20.0f) {
        frequencyRanges.emplace_back(name, start, stop, enabled, gain);
        saveConfig();
    }
    
    void removeFrequencyRange(int index) {
        if (index >= 0 && index < frequencyRanges.size()) {
            frequencyRanges.erase(frequencyRanges.begin() + index);
            if (currentRangeIndex >= frequencyRanges.size() && !frequencyRanges.empty()) {
                currentRangeIndex = frequencyRanges.size() - 1;
            }
            saveConfig();
        }
    }
    
    void toggleFrequencyRange(int index) {
        if (index >= 0 && index < frequencyRanges.size()) {
            frequencyRanges[index].enabled = !frequencyRanges[index].enabled;
            saveConfig();
        }
    }
    
    void updateFrequencyRange(int index, const std::string& name, double start, double stop, float gain) {
        if (index >= 0 && index < frequencyRanges.size()) {
            frequencyRanges[index].name = name;
            frequencyRanges[index].startFreq = start;
            frequencyRanges[index].stopFreq = stop;
            frequencyRanges[index].gain = gain;
            saveConfig();
            flog::info("Scanner: Updated range '{}' - gain set to {:.1f} dB", name, gain);
        }
    }
    
    // Get current active ranges for scanning
    std::vector<int> getActiveRangeIndices() {
        std::vector<int> activeRanges;
        for (int i = 0; i < frequencyRanges.size(); i++) {
            if (frequencyRanges[i].enabled) {
                activeRanges.push_back(i);
            }
        }
        return activeRanges;
    }
    
    // Get current scanning bounds (supports both single range and multi-range)
    bool getCurrentScanBounds(double& currentStart, double& currentStop) {
        if (frequencyRanges.empty()) {
            // Fall back to legacy single range
            currentStart = startFreq;
            currentStop = stopFreq;
            return true;
        }
        
        // Multi-range mode: get current active range
            auto activeRanges = getActiveRangeIndices();
            if (activeRanges.empty()) {
                return false; // No active ranges
            }
            
            // Ensure current range index is valid
            if (currentRangeIndex >= activeRanges.size()) {
                currentRangeIndex = 0;
            }
            
            int rangeIdx = activeRanges[currentRangeIndex];
            // Critical bounds check to prevent crash
            if (rangeIdx >= frequencyRanges.size()) return false;
            
            currentStart = frequencyRanges[rangeIdx].startFreq;
            currentStop = frequencyRanges[rangeIdx].stopFreq;
            return true;
        }
    
    // Get recommended gain for current range
    float getCurrentRangeGain() {
        if (frequencyRanges.empty()) return 20.0f;
        
        auto activeRanges = getActiveRangeIndices();
        if (activeRanges.empty() || currentRangeIndex >= activeRanges.size()) return 20.0f;
        
        int rangeIdx = activeRanges[currentRangeIndex];
        // Critical bounds check to prevent crash
        if (rangeIdx >= frequencyRanges.size()) return 20.0f;
        
        return frequencyRanges[rangeIdx].gain;
    }
    
    // Apply or recommend gain setting for current range
    void applyCurrentRangeGain() {
        if (frequencyRanges.empty()) return;
        
        auto activeRanges = getActiveRangeIndices();
        if (activeRanges.empty() || currentRangeIndex >= activeRanges.size()) return;
        
        int rangeIdx = activeRanges[currentRangeIndex];
        // Critical bounds check to prevent crash
        if (rangeIdx >= frequencyRanges.size()) return;
        
        float targetGain = frequencyRanges[rangeIdx].gain;
        
        try {
            std::string sourceName = sigpath::sourceManager.getSelectedName();
            if (!sourceName.empty()) {
                // Use the new SourceManager::setGain() method
                sigpath::sourceManager.setGain(targetGain);
                flog::info("Scanner: Applied gain {:.1f} dB for range '{}' (source: {})",
                          targetGain, frequencyRanges[rangeIdx].name, sourceName);
            } else {
                SCAN_DEBUG("Scanner: No source selected, cannot apply gain for range '{}'",
                          frequencyRanges[rangeIdx].name);
            }
        } catch (const std::exception& e) {
            flog::error("Scanner: Exception in applyCurrentRangeGain: {}", e.what());
        } catch (...) {
            flog::error("Scanner: Unknown exception in applyCurrentRangeGain");
        }
    }

    void loadConfig() {
        startFreq = config.conf["startFreq"];
        stopFreq = config.conf["stopFreq"];
        level = config.conf["level"];
        interval = config.conf["interval"];
        scanRate = config.conf["scanRate"];
        lingerTime = config.conf["lingerTime"];
        tuningTime = config.conf["tuningTime"];
        scanUp = config.conf["scanUp"];
        reverseLock = config.conf["reverseLock"];
        oneShot = config.conf["oneShot"];
        useFrequencyManager = config.conf["useFrequencyManager"];
        applyProfiles = config.conf["applyProfiles"];
        cycleAfterLinger = config.conf["cycleAfterLinger"];
        dwellTime = config.conf["dwellTime"];
        squelchDelta = config.conf["squelchDelta"];
        squelchDeltaAuto = config.conf["squelchDeltaAuto"];
        scannerFftSize = config.conf["scannerFftSize"];

        // Load blacklist
        blacklist.clear();
        for (auto& item : config.conf["blacklist"]) {
            blacklist.insert(item.get<double>());
        }

        // Load frequency ranges
        frequencyRanges.clear();
        for (auto& item : config.conf["frequencyRanges"]) {
            frequencyRanges.emplace_back(
                item["name"].get<std::string>(),
                item["startFreq"].get<double>(),
                item["stopFreq"].get<double>(),
                item["enabled"].get<bool>(),
                item["gain"].get<float>()
            );
        }

        // Find the matching FFT size index
        for (int i = 0; i < SCANNER_FFT_SIZE_COUNT; i++) {
            if (SCANNER_FFT_SIZES[i] == scannerFftSize) {
                scannerFftSizeIndex = i;
                break;
            }
        }
    }

    void saveConfig() {
        config.conf["startFreq"] = startFreq;
        config.conf["stopFreq"] = stopFreq;
        config.conf["level"] = level;
        config.conf["interval"] = interval;
        config.conf["scanRate"] = scanRate;
        config.conf["lingerTime"] = lingerTime;
        config.conf["tuningTime"] = tuningTime;
        config.conf["scanUp"] = scanUp;
        config.conf["reverseLock"] = reverseLock;
        config.conf["oneShot"] = oneShot;
        config.conf["useFrequencyManager"] = useFrequencyManager;
        config.conf["applyProfiles"] = applyProfiles;
        config.conf["cycleAfterLinger"] = cycleAfterLinger;
        config.conf["dwellTime"] = dwellTime;
        config.conf["squelchDelta"] = squelchDelta;
        config.conf["squelchDeltaAuto"] = squelchDeltaAuto;
        config.conf["scannerFftSize"] = scannerFftSize;

        // Save blacklist
        json blacklist_json = json::array();
        for (auto& item : blacklist) {
            blacklist_json.push_back(item);
        }
        config.conf["blacklist"] = blacklist_json;

        // Save frequency ranges
        json ranges_json = json::array();
        for (auto& range : frequencyRanges) {
            json range_json;
            range_json["name"] = range.name;
            range_json["startFreq"] = range.startFreq;
            range_json["stopFreq"] = range.stopFreq;
            range_json["enabled"] = range.enabled;
            range_json["gain"] = range.gain;
            ranges_json.push_back(range_json);
        }
        config.conf["frequencyRanges"] = ranges_json;

        config.save();
    }

    // Define frequency entry types
    enum FrequencyEntryType {
        FREQ_TYPE_SINGLE = 0,
        FREQ_TYPE_BAND = 1
    };
    
    // Define frequency entry structure
    struct FrequencyEntry {
        FrequencyEntryType type;
        std::string name;
        double frequency;  // For single frequencies
        double lowerFreq;  // For bands
        double upperFreq;  // For bands
        std::string profileName;
    };
    
    void rescanFrequencyManager() {
        scannableFrequencies.clear();
        currentFreqIndex = 0;
        
        // Get all frequency lists from the frequency manager
        auto it = core::moduleManager.instances.find("Frequency Manager");
        if (it == core::moduleManager.instances.end()) {
            flog::warn("Scanner: Frequency manager module not found");
            return;
        }
        
        // This is a simplified implementation - in a real implementation,
        // we would query the frequency manager module for its entries
        
        // For now, just add a few example frequencies
        FrequencyEntry entry;
        
        // Example FM radio stations
        entry.type = FREQ_TYPE_SINGLE;
        entry.name = "FM Radio 1";
        entry.frequency = 100.0e6; // 100 MHz
        entry.profileName = "FM";
        scannableFrequencies.push_back(entry);
        
        entry.name = "FM Radio 2";
        entry.frequency = 102.5e6; // 102.5 MHz
        scannableFrequencies.push_back(entry);
        
        // Example amateur band
        entry.type = FREQ_TYPE_BAND;
        entry.name = "2m Amateur";
        entry.lowerFreq = 144.0e6; // 144 MHz
        entry.upperFreq = 148.0e6; // 148 MHz
        entry.profileName = "NFM";
        scannableFrequencies.push_back(entry);
        
        flog::info("Scanner: Rescanned frequency manager, found {0} entries", (int)scannableFrequencies.size());
    }

    void applySquelchDelta() {
        if (!gui::waterfall.selectedVFO.empty() && squelchDelta > 0.0f) {
            // Store the original squelch level
            originalSquelchLevel = 0.0f; // TODO: Get squelch level from VFO manager
            
            // Apply the delta
            float newSquelch = originalSquelchLevel - squelchDelta;
            // TODO: Set squelch level in VFO manager
            
            squelchDeltaActive = true;
            flog::info("Scanner: Applied squelch delta {:.1f} dB (from {:.1f} to {:.1f})", 
                       squelchDelta, originalSquelchLevel, newSquelch);
        }
    }

    void restoreSquelchLevel() {
        if (!gui::waterfall.selectedVFO.empty() && squelchDeltaActive) {
            // Restore the original squelch level
            // TODO: Set squelch level in VFO manager
            
            squelchDeltaActive = false;
            flog::info("Scanner: Restored original squelch level {:.1f} dB", originalSquelchLevel);
        }
    }

    void updateNoiseFloor(float level) {
        // Simple implementation - just update the noise floor level
        noiseFloor = level;
        flog::info("Scanner: Updated noise floor to {:.1f}", noiseFloor);
    }

    float getMaxLevel(float* data, double freq, double vfo_bw, int data_width, double wf_start, double wf_width) {
        // Safety check for null data
        if (!data || data_width <= 0 || wf_width <= 0.0) {
            return -INFINITY;
        }
        
        // Calculate the range of indices in the FFT data that correspond to the VFO bandwidth
        double bin_width = wf_width / data_width;
        int start_bin = std::max(0, (int)((freq - (vfo_bw / 2.0) - wf_start) / bin_width));
        int end_bin = std::min(data_width - 1, (int)((freq + (vfo_bw / 2.0) - wf_start) / bin_width));
        
        // Safety check for valid bin range
        if (start_bin >= data_width || end_bin < 0 || start_bin > end_bin) {
            return -INFINITY;
        }
        
        // Find the maximum level in the specified range
        float max_level = -INFINITY;
        for (int i = start_bin; i <= end_bin; i++) {
            if (i >= 0 && i < data_width && data[i] > max_level) {
                max_level = data[i];
            }
        }
        
        return max_level;
    }

    bool findSignal(bool scanUp, double& bottom, double& top, double wf_start, double wf_end, double wf_width, double vfo_bw, float* data, int data_width) {
        // Safety check for null data
        if (!data || data_width <= 0 || wf_width <= 0.0) {
            return false;
        }
        
        double bin_width = wf_width / data_width;
        double threshold = level; // Use the scanner's threshold level
        
        // Determine the scan direction and range
        int start_idx, end_idx, step;
        if (scanUp) {
            start_idx = (int)((bottom - wf_start) / bin_width);
            end_idx = (int)((wf_end - wf_start) / bin_width);
            step = 1;
        } else {
            start_idx = (int)((top - wf_start) / bin_width);
            end_idx = (int)((wf_start - wf_start) / bin_width);
            step = -1;
        }
        
        // Ensure indices are within bounds
        start_idx = std::max(0, std::min(data_width - 1, start_idx));
        end_idx = std::max(0, std::min(data_width - 1, end_idx));
        
        // Scan for a signal above the threshold
        for (int i = start_idx; (step > 0) ? (i <= end_idx) : (i >= end_idx); i += step) {
            if (i >= 0 && i < data_width && data[i] >= threshold) {
                // Found a signal, determine its boundaries
                int signal_start = i;
                int signal_end = i;
                
                // Find the start of the signal (going backwards)
                while (signal_start > 0 && data[signal_start - 1] >= threshold) {
                    signal_start--;
                }
                
                // Find the end of the signal (going forwards)
                while (signal_end < data_width - 1 && data[signal_end + 1] >= threshold) {
                    signal_end++;
                }
                
                // Convert bin indices back to frequencies
                double signal_freq_start = wf_start + (signal_start * bin_width);
                double signal_freq_end = wf_start + (signal_end * bin_width);
                
                // Update the output parameters
                bottom = signal_freq_start;
                top = signal_freq_end;
                
                // Calculate center frequency
                double center_freq = (signal_freq_start + signal_freq_end) / 2.0;
                
                // Tune to the center of the signal
                tuner::normalTuning(gui::waterfall.selectedVFO, center_freq);
                
                // Set receiving state
                receiving = true;
                lastSignalTime = std::chrono::high_resolution_clock::now();
                
                flog::info("Scanner: Found signal at {:.6f} MHz (width: {:.1f} kHz, level: {:.1f})", 
                           center_freq / 1e6, (signal_freq_end - signal_freq_start) / 1e3, data[i]);
                
                return true;
            }
        }
        
        return false;
    }

    // Define tuning profile structure
    struct TuningProfile {
        std::string name;
        std::string demodMode;
        double bandwidth;
        float squelchLevel;
    };
    
    void applyTuningProfileSmart(const TuningProfile& profile, const std::string& vfo, double freq, const std::string& signal_name) {
        // Apply tuning profile parameters to the VFO
        if (!vfo.empty()) {
            // Set the demodulator mode
            // TODO: Set VFO mode
            
            // Set bandwidth if specified
            if (profile.bandwidth > 0) {
                sigpath::vfoManager.setBandwidth(vfo, profile.bandwidth);
            }
            
            // Set squelch if specified
            if (profile.squelchLevel != 0.0f) {
                // TODO: Set squelch level
            }
            
            flog::info("Scanner: Applied tuning profile to {:.6f} MHz ({}) - Mode: {}, BW: {:.1f} kHz, Squelch: {:.1f} dB",
                      freq / 1e6, signal_name.c_str(), profile.demodMode.c_str(), 
                      profile.bandwidth / 1e3, profile.squelchLevel);
        }
    }

    bool performFrequencyManagerScanning() {
        if (scannableFrequencies.empty()) {
            return false;
        }
        
        // Advance to the next frequency in the list
        currentFreqIndex = (currentFreqIndex + 1) % scannableFrequencies.size();
        FrequencyEntry& entry = scannableFrequencies[currentFreqIndex];
        
        // Check if this is a single frequency or a band
        currentEntryIsSingleFreq = (entry.type == FREQ_TYPE_SINGLE);
        
        // Update the current tuning profile if available
        currentTuningProfile = nullptr;
        if (!entry.profileName.empty()) {
            for (const auto& profile : tuningProfiles) {
                if (profile.name == entry.profileName) {
                    currentTuningProfile = &profile;
                    break;
                }
            }
        }
        
        // Tune to the frequency
        double tuneFreq;
        if (currentEntryIsSingleFreq) {
            tuneFreq = entry.frequency;
        } else {
            // For bands, tune to the center
            tuneFreq = (entry.lowerFreq + entry.upperFreq) / 2.0;
        }
        
        tuner::normalTuning(gui::waterfall.selectedVFO, tuneFreq);
        tuning = true;
        lastTuneTime = std::chrono::high_resolution_clock::now();
        
        flog::info("Scanner: Tuning to {:.6f} MHz (FM entry {})", 
                  tuneFreq / 1e6, entry.name.c_str());
        
        return true;
    }

    void performLegacyScanning() {
        // Simple implementation for legacy scanning mode
        // Just advance the frequency by the step size
        double currentStart, currentStop;
        if (!getCurrentScanBounds(currentStart, currentStop)) {
            flog::warn("Scanner: No active frequency ranges for legacy scanning");
            return;
        }
        
        double current = gui::waterfall.getCenterFrequency();
        
        if (scanUp) {
            current += interval;
            if (current > currentStop) {
                current = currentStart;
            }
        } else {
            current -= interval;
            if (current < currentStart) {
                current = currentStop;
            }
        }
        
        tuner::normalTuning(gui::waterfall.selectedVFO, current);
        tuning = true;
        lastTuneTime = std::chrono::high_resolution_clock::now();
        
        flog::info("Scanner: Legacy scanning to {:.6f} MHz", current / 1e6);
    }

    // Scanner FFT buffer acquisition callback
    static float* acquireScannerFFTBuffer(void* ctx) {
        ScannerModule* _this = (ScannerModule*)ctx;
        _this->scannerFftMtx.lock();
        
        // Safety check for scannerFftSize - ensure it's within reasonable limits
        if (_this->scannerFftSize <= 0 || _this->scannerFftSize > 1048576) { // 1M max size as safety limit
            flog::error("Scanner: Invalid FFT size {0}, limiting to 8192", _this->scannerFftSize);
            _this->scannerFftSize = 8192; // Reset to a safe default
        }
        
        if (!_this->scannerFftData) {
            try {
                _this->scannerFftData = new float[_this->scannerFftSize];
            }
            catch (const std::bad_alloc& e) {
                flog::error("Scanner: Failed to allocate FFT buffer of size {0}: {1}", _this->scannerFftSize, e.what());
                _this->scannerFftMtx.unlock();
                return nullptr;
            }
        }
        return _this->scannerFftData;
    }
    
    // Scanner FFT buffer release callback
    static void releaseScannerFFTBuffer(void* ctx) {
        ScannerModule* _this = (ScannerModule*)ctx;
        _this->scannerFftMtx.unlock();
    }


private:
    static void menuHandler(void* ctx) {
        ScannerModule* _this = (ScannerModule*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;
        
        // === SCANNER READY STATUS ===
        // Scanner now uses Frequency Manager exclusively for simplified operation
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Scanner uses Frequency Manager entries");
        ImGui::TextWrapped("Enable scanning for specific entries in Frequency Manager to include them in scan list.");
        ImGui::Separator();
        
        // REMOVED: Legacy range manager - scanner now uses Frequency Manager exclusively
        if (false) {  // Legacy code removed for cleaner UI
            ImGui::Begin("Scanner Range Manager", &_this->showRangeManager);
            
            // Add new range section
            ImGui::Text("Add New Range");
            ImGui::Separator();
            ImGui::InputText("Name", _this->newRangeName, sizeof(_this->newRangeName));
            ImGui::InputDouble("Start (Hz)", &_this->newRangeStart, 100000.0, 1000000.0, "%.0f");
            ImGui::InputDouble("Stop (Hz)", &_this->newRangeStop, 100000.0, 1000000.0, "%.0f");
            ImGui::InputFloat("Gain (dB)", &_this->newRangeGain, 1.0f, 10.0f, "%.1f");
            
            if (ImGui::Button("Add Range")) {
                _this->addFrequencyRange(std::string(_this->newRangeName), _this->newRangeStart, _this->newRangeStop, true, _this->newRangeGain);
                strcpy(_this->newRangeName, "New Range");
                _this->newRangeStart = 88000000.0;
                _this->newRangeStop = 108000000.0;
                _this->newRangeGain = 20.0f;
            }
            
            ImGui::Spacing();
            ImGui::Text("Existing Ranges");
            ImGui::Separator();
            
            // List existing ranges
            for (int i = 0; i < _this->frequencyRanges.size(); i++) {
                auto& range = _this->frequencyRanges[i];
                ImGui::PushID(i);
                ImGui::Checkbox("##enabled", &range.enabled);
                ImGui::SameLine();
                
                char nameBuf[128];
                strcpy(nameBuf, range.name.c_str());
                ImGui::SetNextItemWidth(menuWidth * 0.3f);
                if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
                    range.name = std::string(nameBuf);
                }
                ImGui::SameLine();
                
                ImGui::SetNextItemWidth(menuWidth * 0.25f);
                ImGui::InputDouble("##start", &range.startFreq, 1000, 100000, "%.0f Hz");
                ImGui::SameLine();
                
                ImGui::SetNextItemWidth(menuWidth * 0.25f);
                ImGui::InputDouble("##stop", &range.stopFreq, 1000, 100000, "%.0f Hz");
                    ImGui::SameLine();
                    
                ImGui::SetNextItemWidth(menuWidth * 0.15f);
                ImGui::InputFloat("##gain", &range.gain, 1.0f, 0.0f, "%.1f dB");
                    ImGui::SameLine();
                    
                if (ImGui::Button("Update")) {
                    _this->updateFrequencyRange(i, range.name, range.startFreq, range.stopFreq, range.gain);
                    }
                    ImGui::SameLine();
                
                if (ImGui::Button("Remove")) {
                        _this->removeFrequencyRange(i);
                    }
                ImGui::PopID();
            }
            ImGui::End();
        }
        
        // Show scanner parameters
        ImGui::Text("Scanner Parameters");
        ImGui::Separator();

        ImGui::LeftLabel("Level");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##level", &_this->level, -120.0f, 0.0f, "%.1f dBFS")) {
            _this->saveConfig();
        }
        
        ImGui::LeftLabel("Interval");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##interval", &_this->interval, 10, 1000, "%d ms")) {
            _this->saveConfig();
        }

        ImGui::LeftLabel("Scanner FFT Size");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::Combo("##scanner_fft_size", &_this->scannerFftSizeIndex, SCANNER_FFT_SIZE_LABELS, SCANNER_FFT_SIZE_COUNT)) {
            _this->scannerFftSize = SCANNER_FFT_SIZES[_this->scannerFftSizeIndex];
            
            // Safely re-allocate the buffer for the new FFT size
            _this->scannerFftMtx.lock();
            if (_this->scannerFftData) {
                delete[] _this->scannerFftData;
            }
            _this->scannerFftData = new float[_this->scannerFftSize];
            _this->scannerFftMtx.unlock();

            // Correctly notify IQFrontEnd of the new size if the interface exists
            if (g_hasIQFrontendIface) {
                flog::info("Scanner: Sending updated FFT size {0} to IQFrontEnd interface", _this->scannerFftSize);
                if (!core::modComManager.callInterface(iq_interface::kIQFrontendIface, 0, &_this->scannerFftSize, nullptr)) {
                    flog::warn("Scanner: Failed to update FFT size via interface");
                }
            }
            _this->saveConfig();
        }
        
        ImGui::LeftLabel("Scan Rate");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##scan_rate", &_this->scanRate, 1, 100, "%d Hz")) {
            _this->saveConfig();
        }
        
        ImGui::LeftLabel("Linger Time");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##linger_time", &_this->lingerTime, 0, 5000, "%d ms")) {
            _this->saveConfig();
        }
        
        ImGui::LeftLabel("Tuning Time");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##tuning_time", &_this->tuningTime, 0, 1000, "%d ms")) {
                _this->saveConfig();
        }
        
        ImGui::LeftLabel("Scan direction");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        const char* items[] = { "Up", "Down" };
        if (ImGui::Combo("##scan_direction", (int*)&_this->scanUp, items, IM_ARRAYSIZE(items))) {
            _this->saveConfig();
        }
        
        if (ImGui::Checkbox("Reverse Lock", &_this->reverseLock)) {
            _this->saveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("One Shot", &_this->oneShot)) {
            _this->saveConfig();
        }

        // Frequency Manager settings
        ImGui::Spacing();
        ImGui::Text("Frequency Manager Integration");
        ImGui::Separator();
        
        if (ImGui::Checkbox("Use Frequency Manager", &_this->useFrequencyManager)) {
                _this->saveConfig();
            if (_this->useFrequencyManager) {
                _this->rescanFrequencyManager();
            }
        }
        
        if (ImGui::Checkbox("Apply Tuning Profiles", &_this->applyProfiles)) {
            _this->saveConfig();
        }
        
        if (ImGui::Checkbox("Cycle After Linger", &_this->cycleAfterLinger)) {
                    _this->saveConfig();
        }
        
        ImGui::LeftLabel("Dwell Time");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderInt("##dwell_time", &_this->dwellTime, 0, 60000, "%d ms")) {
            _this->saveConfig();
        }
        
        // Squelch settings
        ImGui::Spacing();
        ImGui::Text("Squelch Control");
            ImGui::Separator();
            
        ImGui::LeftLabel("Squelch Delta");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##squelch_delta", &_this->squelchDelta, 0.0f, 50.0f, "%.1f dB")) {
            _this->saveConfig();
        }
        
        if (ImGui::Checkbox("Auto Squelch Delta", &_this->squelchDeltaAuto)) {
                    _this->saveConfig();
            }
            
        // Blacklist settings
            ImGui::Spacing();
        ImGui::Text("Blacklist");
        ImGui::Separator();
        
        if (ImGui::Button("Clear Blacklist")) {
            _this->blacklist.clear();
                _this->saveConfig();
            }
        ImGui::SameLine();
        ImGui::Text("%zu entries", _this->blacklist.size());
        
        // Scanner control
        ImGui::Spacing();
        ImGui::Text("Scanner Control");
        ImGui::Separator();
        
        if (ImGui::Button(!_this->running ? "Start" : "Stop", ImVec2(menuWidth, 0))) {
        if (!_this->running) {
                _this->start();
        }
        else {
                _this->stop();
            }
        }
    }

    void start() {
        if (running) { return; }
        running = true;
        
        // Check if IQ frontend interface is available
        if (!g_hasIQFrontendIface) {
            flog::error("Scanner: IQ frontend interface not available, not starting scanner");
            running = false;
            return;
        }
        
        // If using Frequency Manager, rescan entries before starting
        if (useFrequencyManager) {
            rescanFrequencyManager();
            if (scannableFrequencies.empty()) {
                flog::warn("Scanner: No scannable frequencies found in Frequency Manager. Stopping.");
                running = false;
                return; 
            }
        }
        
        workerThread = std::thread(&ScannerModule::worker, this);
    }

    void stop() {
        if (!running) { return; }
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void worker() {
        flog::info("Scanner: Worker thread started");
        
        double current = gui::waterfall.getCenterFrequency();
        std::chrono::high_resolution_clock::time_point lastScanTime, tuneTime;
        
        // Check if IQ frontend interface is available
        if (!g_hasIQFrontendIface) {
            flog::error("Scanner: IQ frontend interface not available, cannot run scanner. Stopping worker thread.");
            running = false;
            return;
        }
        
        while(running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            
            auto now = std::chrono::high_resolution_clock::now();

            if (!sigpath::sourceManager.isStarted()) {
                flog::warn("Scanner: Radio source stopped, stopping scanner");
                running = false;
                return;
            }

            if (gui::waterfall.selectedVFO.empty()) {
                running = false;
                return;
            }
            
            double currentStart, currentStop;
            if (!getCurrentScanBounds(currentStart, currentStop)) {
                flog::warn("Scanner: No active frequency ranges, stopping");
                running = false;
                return;
            }
            
            if (!useFrequencyManager && (current < currentStart || current > currentStop)) {
                flog::warn("Scanner: Current frequency {:.0f} Hz out of bounds, resetting to start", current);
                current = currentStart;
            }
            tuneTime = std::chrono::high_resolution_clock::now();
            
            if (squelchDelta > 0.0f && !squelchDeltaActive && running) {
                applySquelchDelta();
            }
            
            tuner::normalTuning(gui::waterfall.selectedVFO, current);

            if (tuning) {
                SCAN_DEBUG("Scanner: Tuning in progress...");
                auto timeSinceLastTune = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTuneTime);
                if (timeSinceLastTune.count() > tuningTime) {
                    tuning = false;
                    SCAN_DEBUG("Scanner: Tuning completed");
                }
                continue;
            }

            // Exit early if IQ frontend interface is not available
            if (!g_hasIQFrontendIface) {
                continue;
            }

            scannerFftMtx.lock();
            if (!scannerFftData) {
                scannerFftMtx.unlock();
                continue;
            }
            
            // Now work with the FFT data directly
            float* data = scannerFftData;
            int dataWidth = scannerFftSize;
            
                if (dataWidth <= 0) {
                scannerFftMtx.unlock();
                        continue; // Invalid data width
                    }
                    
                double wfCenter = gui::waterfall.getViewOffset() + gui::waterfall.getCenterFrequency();
                double wfWidth = gui::waterfall.getViewBandwidth();
                double wfStart = wfCenter - (wfWidth / 2.0);
                double wfEnd = wfCenter + (wfWidth / 2.0);

                double baseVfoWidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
                double effectiveVfoWidth;
                
                if (useFrequencyManager && currentEntryIsSingleFreq) {
                effectiveVfoWidth = 5000.0;
                } else {
                    effectiveVfoWidth = baseVfoWidth;
                }

                if (receiving) {
                    SCAN_DEBUG("Scanner: Receiving signal...");
                
                    float maxLevel = getMaxLevel(data, current, effectiveVfoWidth, dataWidth, wfStart, wfWidth);
                    if (maxLevel >= level) {
                        if (squelchDeltaAuto) {
                        updateNoiseFloor(maxLevel - 15.0f);
                        }
                        
                        if (!squelchDeltaActive && squelchDelta > 0.0f && running) {
                            applySquelchDelta();
                        }
                        
                        lastSignalTime = now;
                    }
                    else {
                        auto timeSinceLastSignal = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSignalTime);
                        if (timeSinceLastSignal.count() > lingerTime) {
                            if (squelchDeltaActive) {
                                restoreSquelchLevel();
                            }
                            
                            receiving = false;
                            SCAN_DEBUG("Scanner: Signal lost, resuming scanning");
                        }
                    }
                }
                else {
                    flog::warn("Seeking signal");
                    double bottomLimit = current;
                    double topLimit = current;
                    
                    if (useFrequencyManager && currentEntryIsSingleFreq) {
                        float maxLevel = getMaxLevel(data, current, effectiveVfoWidth, dataWidth, wfStart, wfWidth);
                        if (maxLevel >= level) {
                            receiving = true;
                            lastSignalTime = now;
                            flog::info("Scanner: Found signal at single frequency {:.6f} MHz (level: {:.1f})", current / 1e6, maxLevel);
                            
                            if (applyProfiles && currentTuningProfile && !gui::waterfall.selectedVFO.empty()) {
                                const TuningProfile* profile = static_cast<const TuningProfile*>(currentTuningProfile);
                                if (profile) {
                                    applyTuningProfileSmart(*profile, gui::waterfall.selectedVFO, current, "SIGNAL");
                                }
                            }
                            
                        scannerFftMtx.unlock();
                        continue;
                        }
                    } else {
                        if (findSignal(scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, effectiveVfoWidth, data, dataWidth)) {
                        scannerFftMtx.unlock();
                        continue;
                    }
                    
                    if (!reverseLock) {
                            if (findSignal(!scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, effectiveVfoWidth, data, dataWidth)) {
                            scannerFftMtx.unlock();
                            continue;
                        }
                    }
                    else { reverseLock = false; }
                    }
                    
                    if (useFrequencyManager) {
                                        if (!performFrequencyManagerScanning()) {
                    flog::warn("Scanner: FM integration failed, falling back to legacy mode");
                    performLegacyScanning();
                }
                    } else {
                        if (scanUp) {
                        current = topLimit + interval;
                            if (current > currentStop) {
                            if (!frequencyRanges.empty()) {
                                currentRangeIndex = (currentRangeIndex + 1) % getActiveRangeIndices().size();
                                current = frequencyRanges[getActiveRangeIndices()[currentRangeIndex]].startFreq;
                                        } else {
                                            current = currentStart;
                                        }
                                    }
                                } else {
                        current = bottomLimit - interval;
                            if (current < currentStart) {
                            if (!frequencyRanges.empty()) {
                                currentRangeIndex = (currentRangeIndex - 1 + getActiveRangeIndices().size()) % getActiveRangeIndices().size();
                                current = frequencyRanges[getActiveRangeIndices()[currentRangeIndex]].stopFreq;
                                        } else {
                                            current = currentStop;
                            }
                        }
                    }
                }
            }
            scannerFftMtx.unlock();
        }
    }

    // Member variables
    std::string name;
    bool enabled = false;
    bool running = false;
    double startFreq = 88000000.0;
    double stopFreq = 108000000.0;
    float level = -40.0f;
    int interval = 250;
    int scanRate = 10;
    int lingerTime = 1000;
    int tuningTime = 100;
    bool scanUp = true;
    bool reverseLock = false;
    bool oneShot = false;
    bool useFrequencyManager = true;
    bool applyProfiles = true;
    bool cycleAfterLinger = false;
    int dwellTime = 0;
    float squelchDelta = 0.0f;
    bool squelchDeltaAuto = false;
    std::set<double> blacklist;
    
    // Frequency manager integration
    std::vector<FrequencyEntry> scannableFrequencies;
    int currentFreqIndex = 0;
    bool currentEntryIsSingleFreq = false;
    std::vector<TuningProfile> tuningProfiles;
    const TuningProfile* currentTuningProfile = nullptr;
    float noiseFloor = -80.0f;
    float originalSquelchLevel = 0.0f;
    bool squelchDeltaActive = false;

    std::vector<FrequencyRange> frequencyRanges;
    int currentRangeIndex = 0;
    bool showRangeManager = false;
    char newRangeName[128] = "New Range";
    double newRangeStart = 88000000.0;
    double newRangeStop = 108000000.0;
    float newRangeGain = 20.0f;
    
    std::thread workerThread;
    bool tuning = false;
    bool receiving = false;
    std::chrono::high_resolution_clock::time_point lastSignalTime;
    std::chrono::high_resolution_clock::time_point lastTuneTime;

    // Scanner FFT parameters
    uint32_t scannerFftSize = 8192;
    static constexpr uint32_t SCANNER_FFT_SIZES[] = {1024, 2048, 4096, 8192, 16384, 32768};
    static constexpr const char* SCANNER_FFT_SIZE_LABELS[] = {"1024", "2048", "4096", "8192", "16384", "32768"};
    static constexpr int SCANNER_FFT_SIZE_COUNT = 6;
    int scannerFftSizeIndex = 3;
    
    // Scanner FFT buffer
    float* scannerFftData = nullptr;
    std::mutex scannerFftMtx;

    // Internal state
    size_t currentScanIndex = 0;
};

MOD_EXPORT void _INIT_() {
    json defaultConfig = {
        {"startFreq", 88000000.0},
        {"stopFreq", 108000000.0},
        {"level", -40.0f},
        {"interval", 250},
        {"scanRate", 10},
        {"lingerTime", 1000},
        {"tuningTime", 100},
        {"scanUp", true},
        {"reverseLock", false},
        {"oneShot", false},
        {"useFrequencyManager", true},
        {"applyProfiles", true},
        {"cycleAfterLinger", false},
        {"dwellTime", 0},
        {"squelchDelta", 0.0f},
        {"squelchDeltaAuto", false},
        {"scannerFftSize", 8192},
        {"blacklist", json::array()},
        {"frequencyRanges", json::array()}
    };
    config.setPath(core::args["root"].s() + "/scanner_config.json");
    config.load(defaultConfig);
}

MOD_EXPORT void* _CREATE_INSTANCE_(std::string name) {
    return new ScannerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (ScannerModule*)instance;
}

MOD_EXPORT void _END_() {
    config.save();
}
