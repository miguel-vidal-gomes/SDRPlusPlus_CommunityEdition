#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <chrono>
#include <algorithm>
#include <fstream> // Added for file operations
#include <core.h>
#include <radio_interface.h>
#include <sstream>
#include <cstring>  // For memcpy
#include <set>      // For std::set in profile diagnostics

// Windows MSVC compatibility 
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
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
    /* Description:     */ "Frequency scanner for SDR++",
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
        loadConfig();
        
        flog::info("Scanner: Scanner module '{}' initialized successfully", name);
    }

    ~ScannerModule() {
        saveConfig();
        gui::menu.removeEntry(name);
        stop();
    }

    void postInit() {}

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
                flog::debug("Scanner: No source selected, cannot apply gain for range '{}'",
                          frequencyRanges[rangeIdx].name);
            }
        } catch (const std::exception& e) {
            flog::error("Scanner: Exception in applyCurrentRangeGain: {}", e.what());
        } catch (...) {
            flog::error("Scanner: Unknown exception in applyCurrentRangeGain");
        }
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
                
                // Enabled checkbox
                bool enabled = range.enabled;
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    _this->toggleFrequencyRange(i);
                }
                ImGui::SameLine();
                
                // Range info with edit capability
                static char editName[256];
                static double editStart, editStop;
                static float editGain;
                static int editingIndex = -1;
                
                if (editingIndex == i) {
                    // Editing mode
                    ImGui::SetNextItemWidth(80);
                    ImGui::InputText("##edit_name", editName, sizeof(editName));
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    ImGui::InputDouble("##edit_start", &editStart, 1000000.0, 10000000.0, "%.0f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    ImGui::InputDouble("##edit_stop", &editStop, 1000000.0, 10000000.0, "%.0f");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(60);
                    ImGui::InputFloat("##edit_gain", &editGain, 1.0f, 10.0f, "%.1f");
                    ImGui::SameLine();
                    
                    if (ImGui::Button("Save")) {
                        _this->updateFrequencyRange(i, std::string(editName), editStart, editStop, editGain);
                        editingIndex = -1;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) {
                        editingIndex = -1;
                    }
            } else {
                    // Display mode
                    ImGui::Text("%s: %.1f - %.1f MHz (%.1f dB)", 
                        range.name.c_str(), 
                        range.startFreq / 1e6, 
                        range.stopFreq / 1e6,
                        range.gain);
                    ImGui::SameLine();
                    
                    if (ImGui::Button("Edit")) {
                        editingIndex = i;
                        strcpy(editName, range.name.c_str());
                        editStart = range.startFreq;
                        editStop = range.stopFreq;
                        editGain = range.gain;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete")) {
                        _this->removeFrequencyRange(i);
                        break; // Break to avoid iterator invalidation
                    }
                }
                
                ImGui::PopID();
            }
            
            // Quick presets section
            if (ImGui::CollapsingHeader("Quick Presets")) {
                if (ImGui::Button("FM Broadcast (88-108 MHz)")) {
                    _this->addFrequencyRange("FM Broadcast", 88000000.0, 108000000.0, true, 15.0f);
                }
                if (ImGui::Button("Airband (118-137 MHz)")) {
                    _this->addFrequencyRange("Airband", 118000000.0, 137000000.0, true, 25.0f);
                }
                if (ImGui::Button("2m Ham (144-148 MHz)")) {
                    _this->addFrequencyRange("2m Ham", 144000000.0, 148000000.0, true, 30.0f);
                }
                if (ImGui::Button("PMR446 (446.0-446.2 MHz)")) {
                    _this->addFrequencyRange("PMR446", 446000000.0, 446200000.0, true, 35.0f);
                }
                if (ImGui::Button("70cm Ham (420-450 MHz)")) {
                    _this->addFrequencyRange("70cm Ham", 420000000.0, 450000000.0, true, 35.0f);
                }
            }
            
            ImGui::End();
        }
        
        // REMOVED: Legacy single range controls - scanner now uses Frequency Manager exclusively
        
        // === COMMON SCANNER PARAMETERS ===
        ImGui::Spacing();
        ImGui::Text("Scanner Parameters");
        ImGui::Separator();

        // LIVE PARAMETERS: Can be changed while scanning for immediate effect!
        ImGui::LeftLabel("Interval");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        
        // DISCRETE SLIDER: Show actual values with units instead of indices
        if (ImGui::SliderInt("##interval_scanner_discrete", &_this->intervalIndex, 0, _this->INTERVAL_VALUES_COUNT - 1, _this->INTERVAL_LABELS[_this->intervalIndex])) {
            flog::debug("Scanner: Interval slider changed to index {} ({})", _this->intervalIndex, _this->INTERVAL_LABELS[_this->intervalIndex]);
            _this->syncDiscreteValues(); // Update actual interval value
            _this->saveConfig();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Frequency step size for band scanning\n"
                             "Used when scanning frequency bands from Frequency Manager\n"
                             "TIP: Use larger steps (100+ kHz) for wide band discovery\n"
                             "Use smaller steps (5-25 kHz) for precise band coverage\n"
                             "Single frequencies ignore this setting");
        }
        
        // PERFORMANCE: Configurable scan rate (consistent across all modes)
        ImGui::LeftLabel("Scan Rate");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        
        // DISCRETE SLIDER: Show actual values with units instead of indices
        if (ImGui::SliderInt("##scan_rate_discrete", &_this->scanRateIndex, 0, _this->SCAN_RATE_VALUES_COUNT - 1, _this->SCAN_RATE_LABELS[_this->scanRateIndex])) {
            flog::debug("Scanner: Scan rate slider changed to index {} ({})", _this->scanRateIndex, _this->SCAN_RATE_LABELS[_this->scanRateIndex]);
            _this->syncDiscreteValues(); // Update actual scan rate value
            _this->saveConfig();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How fast to jump between frequencies during scanning\n"
                             "TIP: Start at 25/sec (middle) for best balance\n"
                             "If hardware supports it, increase iteratively - affects false negatives");
        }
        ImGui::LeftLabel("Passband Ratio");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        
        // DISCRETE SLIDER: Show actual values with units instead of indices
        if (ImGui::SliderInt("##passband_ratio_discrete", &_this->passbandIndex, 0, _this->PASSBAND_VALUES_COUNT - 1, _this->PASSBAND_FORMATS[_this->passbandIndex])) {
            _this->syncDiscreteValues(); // Update actual passband ratio value
            _this->saveConfig();
            flog::debug("Scanner: Passband slider changed to index {} ({})", _this->passbandIndex, _this->PASSBAND_LABELS[_this->passbandIndex]);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Signal detection bandwidth as percentage of VFO width\n"
                             "TIP: Start at 100%% for best signal detection\n"
                             "Lower if catching too many false positives");
        }
        ImGui::LeftLabel("Tuning Time (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##tuning_time_scanner", &_this->tuningTime, 100, 1000)) {
            _this->tuningTime = std::clamp<int>(_this->tuningTime, 100, 10000);
            _this->saveConfig();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Time to wait after tuning before checking for signals (ms)\n"
                             "Allows hardware and DSP to settle after frequency change\n"
                             "TIP: Increase if missing signals (slow hardware)\n"
                             "Decrease for faster scanning (stable hardware)\n"
                             "Range: 100ms - 10000ms, default: 250ms");
        }
        ImGui::LeftLabel("Linger Time (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##linger_time_scanner", &_this->lingerTime, 100, 1000)) {
            _this->lingerTime = std::clamp<int>(_this->lingerTime, 100, 10000);
            _this->saveConfig();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Time to stay on frequency when signal is detected (ms)\n"
                             "Scanner pauses to let you listen to the signal\n"
                             "TIP: Longer times for voice communications (2000+ ms)\n"
                             "Shorter times for quick signal identification (500-1000 ms)\n"
                             "Range: 100ms - 10000ms, default: 1000ms");
        }
        // LIVE PARAMETERS: No more disabling - all can be changed during scanning!

        ImGui::LeftLabel("Trigger Level");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##scanner_trigger_level", &_this->level, -150.0, 0.0)) {
            _this->saveConfig();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Signal strength threshold for stopping scanner (dBFS)\n"
                             "Scanner stops when signal exceeds this level\n"
                             "Lower values = more sensitive, higher values = less sensitive");
        }

        // Blacklist controls
        ImGui::Separator();
        ImGui::Text("Frequency Blacklist");
        
        // Add frequency to blacklist
        static double newBlacklistFreq = 0.0;
        ImGui::LeftLabel("Add Frequency (Hz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##new_blacklist_freq", &newBlacklistFreq, 1000.0, 100000.0, "%0.0f")) {
            newBlacklistFreq = round(newBlacklistFreq);
        }
        if (ImGui::Button("Add to Blacklist##scanner_add_blacklist", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            if (newBlacklistFreq > 0) {
                _this->blacklistedFreqs.push_back(newBlacklistFreq);
                newBlacklistFreq = 0.0;
                _this->saveConfig();
            }
        }
        
        // Show current frequency for reference
        if (!gui::waterfall.selectedVFO.empty()) {
            double currentFreq = gui::waterfall.getCenterFrequency();
            if (gui::waterfall.vfos.find(gui::waterfall.selectedVFO) != gui::waterfall.vfos.end()) {
                currentFreq += gui::waterfall.vfos[gui::waterfall.selectedVFO]->centerOffset;
            }
            ImGui::Text("Current Frequency: %.0f Hz (%.3f MHz)", currentFreq, currentFreq / 1e6);
        } else {
            ImGui::TextDisabled("Current Frequency: No VFO selected");
        }
        
        // Add current tuned frequency to blacklist
        bool hasValidFreq = !gui::waterfall.selectedVFO.empty();
        if (!hasValidFreq) { ImGui::BeginDisabled(); }
        if (ImGui::Button("Blacklist Current Frequency##scanner_blacklist_current", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            if (!gui::waterfall.selectedVFO.empty()) {
                // Get current center frequency + VFO offset
                double currentFreq = gui::waterfall.getCenterFrequency();
                if (gui::waterfall.vfos.find(gui::waterfall.selectedVFO) != gui::waterfall.vfos.end()) {
                    currentFreq += gui::waterfall.vfos[gui::waterfall.selectedVFO]->centerOffset;
                }
                
                // Check if frequency is already blacklisted (avoid duplicates)
                bool alreadyBlacklisted = false;
                for (const double& blacklisted : _this->blacklistedFreqs) {
                    if (std::abs(currentFreq - blacklisted) < _this->blacklistTolerance) {
                        alreadyBlacklisted = true;
                        break;
                    }
                }
                
                if (!alreadyBlacklisted) {
                    _this->blacklistedFreqs.push_back(currentFreq);
                    _this->saveConfig();
                    flog::info("Scanner: Added current frequency {:.0f} Hz to blacklist", currentFreq);
                    
                    // UX FIX: Automatically resume scanning after blacklisting
                    // (Same mechanism as directional arrow buttons)
                    {
                        std::lock_guard<std::mutex> lck(_this->scanMtx);
                        _this->receiving = false;
                    }
                    flog::debug("Scanner: Auto-resuming scanning after blacklisting frequency");
                    
                } else {
                    flog::warn("Scanner: Frequency {:.0f} Hz already blacklisted (within tolerance)", currentFreq);
                }
            } else {
                flog::warn("Scanner: No VFO selected, cannot blacklist current frequency");
            }
        }
        if (!hasValidFreq) { ImGui::EndDisabled(); }
        
        // Blacklist tolerance
        ImGui::LeftLabel("Blacklist Tolerance (Hz)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##blacklist_tolerance", &_this->blacklistTolerance, 100.0, 10000.0, "%0.0f")) {
            _this->blacklistTolerance = std::clamp<double>(round(_this->blacklistTolerance), 100.0, 100000.0);
            _this->saveConfig();
        }
        
        // List of blacklisted frequencies
        if (!_this->blacklistedFreqs.empty()) {
            ImGui::Text("Blacklisted Frequencies:");
            ImGui::Separator();
            
            // Create a scrollable region for the blacklist if there are many entries
            if (_this->blacklistedFreqs.size() > 5) {
                ImGui::BeginChild("##blacklist_scroll", ImVec2(0, 150), true);
            }
            
            for (size_t i = 0; i < _this->blacklistedFreqs.size(); i++) {
                // ENHANCED UX: Show frequency manager entry name if available
                std::string entryName = _this->lookupFrequencyManagerName(_this->blacklistedFreqs[i]);
                
                if (!entryName.empty()) {
                    // Show name + frequency for better UX
                    ImGui::Text("%s (%.3f MHz)", entryName.c_str(), _this->blacklistedFreqs[i] / 1e6);
                } else {
                    // Fallback to frequency only
                ImGui::Text("%.0f Hz (%.3f MHz)", _this->blacklistedFreqs[i], _this->blacklistedFreqs[i] / 1e6);
                }
                ImGui::SameLine();
                
                // Right-align the remove button
                ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80);
                if (ImGui::Button(("Remove##scanner_remove_blacklist_" + std::to_string(i)).c_str())) {
                    _this->blacklistedFreqs.erase(_this->blacklistedFreqs.begin() + i);
                    _this->saveConfig();
                    break;
                }
            }
            
            if (_this->blacklistedFreqs.size() > 5) {
                ImGui::EndChild();
            }
            
            ImGui::Spacing();
            if (ImGui::Button("Clear All Blacklisted##scanner_clear_blacklist", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                _this->blacklistedFreqs.clear();
                _this->saveConfig();
            }
        }

        ImGui::BeginTable(("scanner_bottom_btn_table" + _this->name).c_str(), 2);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        
        // INSTANT VISUAL FEEDBACK: Use immediate state for responsiveness
        
        // Left button (decreasing frequency)
        bool leftSelected = !_this->scanUp;
        if (leftSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f)); // Blue highlight
        }
        if (ImGui::Button(("<<##scanner_back_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            // INSTANT: No locks, no blocking operations
            _this->reverseLock = true;
            _this->receiving = false;
            _this->scanUp = false;
            _this->configNeedsSave = true;
        }
        if (leftSelected) {
            ImGui::PopStyleColor(); // Safe: matches the PushStyleColor above
        }
        
        ImGui::TableSetColumnIndex(1);
        
        // Right button (increasing frequency)
        bool rightSelected = _this->scanUp;
        if (rightSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f)); // Blue highlight
        }
        if (ImGui::Button((">>##scanner_forw_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            // INSTANT: No locks, no blocking operations
            _this->reverseLock = true;
            _this->receiving = false;
            _this->scanUp = true;
            _this->configNeedsSave = true;
        }
        if (rightSelected) {
            ImGui::PopStyleColor(); // Safe: matches the PushStyleColor above
        }
        ImGui::EndTable();

        if (!_this->running) {
            // Check if radio source is running
            bool sourceRunning = gui::mainWindow.sdrIsRunning();
            
            if (!sourceRunning) {
                // Disable button and show warning when source is stopped
                style::beginDisabled();
            }
            
            if (ImGui::Button("Start##scanner_start", ImVec2(menuWidth, 0))) {
                _this->start();
            }
            
            if (!sourceRunning) {
                style::endDisabled();
                // Show warning status
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: Radio source not running");
            } else {
            ImGui::Text("Status: Idle");
            }
        }
        else {
            ImGui::BeginTable(("scanner_control_table" + _this->name).c_str(), 2);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Button("Stop##scanner_start", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                _this->stop();
            }
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Reset##scanner_reset", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                _this->reset();
            }
            ImGui::EndTable();
            
            if (_this->receiving) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Receiving");
            }
            else if (_this->tuning) {
                ImGui::TextColored(ImVec4(0, 1, 1, 1), "Status: Tuning");
            }
            else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Status: Scanning");
            }
        }
        
        // PERFORMANCE FIX: Delayed config saving for responsive UI
        if (_this->configNeedsSave) {
            _this->configNeedsSave = false;
            _this->saveConfig(); // Save in background, doesn't block UI
        }
    }

    void start() {
        if (running) { 
            flog::warn("Scanner: Already running");
            return; 
        }
        
        // SAFETY CHECK: Ensure radio source is running before starting scanner
        if (!gui::mainWindow.sdrIsRunning()) {
            flog::error("Scanner: Cannot start scanning - radio source is not running");
            return; 
        }
        
        // Validate scanner state before starting
        if (gui::waterfall.selectedVFO.empty()) {
            flog::error("Scanner: No VFO selected, cannot start scanning");
            return;
        }
        
        // Initialize scanning parameters
        current = startFreq;
        tuning = false;
        receiving = false;
        currentEntryIsSingleFreq = false; // Default to band-style detection
        
        flog::info("Scanner: Starting scanner from {:.3f} MHz", current / 1e6);
        
        running = true;
        
        // Apply gain setting for the initial scanning range
        if (!frequencyRanges.empty()) {
            try {
                applyCurrentRangeGain();
            } catch (const std::exception& e) {
                flog::error("Scanner: Exception applying initial gain: {}", e.what());
                // Continue anyway, gain is not critical for basic operation
            }
        }
        
        // Start worker thread
        try {
            workerThread = std::thread(&ScannerModule::worker, this);
            flog::info("Scanner: Worker thread started successfully");
        } catch (const std::exception& e) {
            flog::error("Scanner: Failed to start worker thread: {}", e.what());
            running = false;
            throw;
        }
    }

    void stop() {
        if (!running) { return; }
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void reset() {
        std::lock_guard<std::mutex> lck(scanMtx);
            current = startFreq;
        receiving = false;
        tuning = false;
        reverseLock = false;
        flog::warn("Scanner: Reset to start frequency {:.0f} Hz", startFreq);
    }

    void saveConfig() {
        config.acquire();
        
        // Save legacy single range (for backward compatibility)
        config.conf["startFreq"] = startFreq;
        config.conf["stopFreq"] = stopFreq;
        
        // Save common scanner parameters
        config.conf["interval"] = interval;
        config.conf["passbandRatio"] = passbandRatio;
        config.conf["tuningTime"] = tuningTime;
        config.conf["lingerTime"] = lingerTime;
        config.conf["level"] = level;
        config.conf["blacklistTolerance"] = blacklistTolerance;
        config.conf["scanUp"] = scanUp; // Save scanning direction preference
        config.conf["blacklistedFreqs"] = blacklistedFreqs;
        
        // Save frequency ranges
        json rangesArray = json::array();
        for (const auto& range : frequencyRanges) {
            json rangeJson;
            rangeJson["name"] = range.name;
            rangeJson["startFreq"] = range.startFreq;
            rangeJson["stopFreq"] = range.stopFreq;
            rangeJson["enabled"] = range.enabled;
            rangeJson["gain"] = range.gain;
            rangesArray.push_back(rangeJson);
        }
        config.conf["frequencyRanges"] = rangesArray;
        config.conf["currentRangeIndex"] = currentRangeIndex;
        
        // Save frequency manager integration settings
        // NOTE: useFrequencyManager and applyProfiles are now always enabled (no longer configurable)
        config.conf["scanRateHz"] = scanRateHz;
        
        config.release(true);
    }

    void loadConfig() {
        config.acquire();
        startFreq = config.conf.value("startFreq", 88000000.0);
        stopFreq = config.conf.value("stopFreq", 108000000.0);
        interval = std::clamp(config.conf.value("interval", 100000.0), 5000.0, 200000.0);  // Guardrails: 5 kHz - 200 kHz
        passbandRatio = config.conf.value("passbandRatio", 100.0);
        tuningTime = config.conf.value("tuningTime", 250);
        lingerTime = config.conf.value("lingerTime", 1000.0);
        level = config.conf.value("level", -50.0);
        blacklistTolerance = config.conf.value("blacklistTolerance", 1000.0);
        scanUp = config.conf.value("scanUp", true); // Load scanning direction preference
        if (config.conf.contains("blacklistedFreqs")) {
            blacklistedFreqs = config.conf["blacklistedFreqs"].get<std::vector<double>>();
        }
        
        // Load frequency ranges if they exist (BEFORE releasing config!)
        if (config.conf.contains("frequencyRanges") && config.conf["frequencyRanges"].is_array()) {
            frequencyRanges.clear();
            for (const auto& rangeJson : config.conf["frequencyRanges"]) {
                if (rangeJson.contains("name") && rangeJson.contains("startFreq") && 
                    rangeJson.contains("stopFreq") && rangeJson.contains("enabled")) {
                    float gain = 20.0f; // Default gain for backward compatibility
                    if (rangeJson.contains("gain")) {
                        gain = rangeJson["gain"].get<float>();
                    }
                    frequencyRanges.emplace_back(
                        rangeJson["name"].get<std::string>(),
                        rangeJson["startFreq"].get<double>(),
                        rangeJson["stopFreq"].get<double>(),
                        rangeJson["enabled"].get<bool>(),
                        gain
                    );
                }
            }
            if (config.conf.contains("currentRangeIndex")) {
                currentRangeIndex = config.conf["currentRangeIndex"].get<int>();
                int maxIndex = std::max(0, (int)frequencyRanges.size() - 1);
                currentRangeIndex = std::clamp<int>(currentRangeIndex, 0, maxIndex);
            }
        }
        
        // Load frequency manager integration settings
        // NOTE: useFrequencyManager and applyProfiles are now always true (simplified UI)
        scanRateHz = config.conf.value("scanRateHz", 25);
        
        config.release();
        
        // Ensure current frequency is within bounds
        double currentStart, currentStop;
        if (getCurrentScanBounds(currentStart, currentStop)) {
            if (current < currentStart || current > currentStop) {
                current = currentStart;
            }
        } else {
            // Fall back to legacy range
            if (current < startFreq || current > stopFreq) {
                current = startFreq;
            }
        }
        
        // Initialize discrete parameter indices to match loaded values
        initializeDiscreteIndices();
    }

    void worker() {
        flog::info("Scanner: Worker thread started");
        try {
            // PERFORMANCE-CRITICAL: Configurable scan rate (consistent across all modes)
            while (running) {
                // Implement actual scan rate control (was hardcoded at 100ms)
                int clampedRate = std::clamp(scanRateHz, 5, 50);  // Safety bounds
                int intervalMs = 1000 / clampedRate;
                std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
                
                try {
                    std::lock_guard<std::mutex> lck(scanMtx);
                    auto now = std::chrono::high_resolution_clock::now();

                // SAFETY CHECK: Stop scanner if radio source is stopped
                if (!gui::mainWindow.sdrIsRunning()) {
                    flog::warn("Scanner: Radio source stopped, stopping scanner");
                    running = false;
                    return;
                }

                // Enforce tuning
                if (gui::waterfall.selectedVFO.empty()) {
                    running = false;
                    return;
                }
                
                // Get current range bounds (supports multi-range and legacy single range)
                double currentStart, currentStop;
                if (!getCurrentScanBounds(currentStart, currentStop)) {
                    flog::warn("Scanner: No active frequency ranges, stopping");
                    running = false;
                    return;
                }
                
                // Ensure current frequency is within bounds
                // PERFORMANCE-CRITICAL: Ensure current frequency is within bounds (legacy mode only)
                if (!useFrequencyManager && (current < currentStart || current > currentStop)) {
                    flog::warn("Scanner: Current frequency {:.0f} Hz out of bounds, resetting to start", current);
                    current = currentStart;
                }
                tuner::normalTuning(gui::waterfall.selectedVFO, current);

                // Check if we are waiting for a tune
                if (tuning) {
                    flog::debug("Scanner: Tuning in progress...");
                    auto timeSinceLastTune = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTuneTime);
                    if (timeSinceLastTune.count() > tuningTime) {
                        tuning = false;
                        flog::debug("Scanner: Tuning completed");
                    }
                    continue;
                }

                // PERFORMANCE FIX: Minimize FFT lock time by copying data immediately
                int dataWidth = 0;
                static std::vector<float> fftDataCopy; // Reusable buffer to avoid allocations
                
                // Acquire FFT data and copy it immediately to minimize lock time
                {
                float* data = gui::waterfall.acquireLatestFFT(dataWidth);
                if (!data) { 
                        continue; // No FFT data available, try again
                }
                if (dataWidth <= 0) {
                    gui::waterfall.releaseLatestFFT();
                        continue; // Invalid data width
                    }
                    
                    // Resize buffer if needed and copy data (minimize lock time)
                    fftDataCopy.resize(dataWidth);
                    memcpy(fftDataCopy.data(), data, dataWidth * sizeof(float));
                    gui::waterfall.releaseLatestFFT(); // CRITICAL: Release lock immediately
                }
                
                // Now work with local copy, FFT mutex is free for UI thread
                float* data = fftDataCopy.data();

                // Get gather waterfall data
                double wfCenter = gui::waterfall.getViewOffset() + gui::waterfall.getCenterFrequency();
                double wfWidth = gui::waterfall.getViewBandwidth();
                double wfStart = wfCenter - (wfWidth / 2.0);
                double wfEnd = wfCenter + (wfWidth / 2.0);

                // ADAPTIVE SIGNAL DETECTION: Use different tolerances for single freq vs bands
                double baseVfoWidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);
                double effectiveVfoWidth;
                
                if (useFrequencyManager && currentEntryIsSingleFreq) {
                    // Single frequency: Use tight tolerance (5 kHz) to ignore nearby signals
                    effectiveVfoWidth = 5000.0; // 5 kHz window for single frequencies
                    static bool loggedSingleFreq = false;
                    if (!loggedSingleFreq) {
                        flog::info("Scanner: Single frequency mode - using 5 kHz tolerance (ignoring nearby signals)");
                        loggedSingleFreq = true;
                    }
                } else {
                    // Band or legacy scanning: Use full VFO bandwidth for wide signal detection 
                    effectiveVfoWidth = baseVfoWidth;
                    static bool loggedBandMode = false;
                    if (!loggedBandMode && useFrequencyManager) {
                        flog::info("Scanner: Band scanning mode - using full VFO bandwidth ({:.1f} kHz) for signal detection", baseVfoWidth / 1000.0);
                        loggedBandMode = true;
                    }
                }

                if (receiving) {
                    flog::debug("Scanner: Receiving signal...");
                
                    float maxLevel = getMaxLevel(data, current, effectiveVfoWidth, dataWidth, wfStart, wfWidth);
                    if (maxLevel >= level) {
                        lastSignalTime = now;
                    }
                    else {
                        auto timeSinceLastSignal = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSignalTime);
                        if (timeSinceLastSignal.count() > lingerTime) {
                            receiving = false;
                            flog::debug("Scanner: Signal lost, resuming scanning");
                        }
                    }
                }
                else {
                    flog::warn("Seeking signal");
                    double bottomLimit = current;
                    double topLimit = current;
                    
                    // ADAPTIVE SIGNAL SEARCH: Different behavior for single freq vs band scanning
                    if (useFrequencyManager && currentEntryIsSingleFreq) {
                        // SINGLE FREQUENCY: Only check signal at exact current frequency (no scanning)
                        float maxLevel = getMaxLevel(data, current, effectiveVfoWidth, dataWidth, wfStart, wfWidth);
                        if (maxLevel >= level) {
                            receiving = true;
                            lastSignalTime = now;
                            flog::info("Scanner: Found signal at single frequency {:.6f} MHz (level: {:.1f})", current / 1e6, maxLevel);
                            
                            // TUNING PROFILE APPLICATION: Apply profile when signal found (CRITICAL FIX)
                            if (applyProfiles && currentTuningProfile && !gui::waterfall.selectedVFO.empty()) {
                                const TuningProfile* profile = static_cast<const TuningProfile*>(currentTuningProfile);
                                if (profile) {
                                    applyTuningProfileSmart(*profile, gui::waterfall.selectedVFO, current, "SIGNAL");
                                }
                            } else {
                                if (applyProfiles && !currentTuningProfile) {
                                    flog::debug("Scanner: No profile available for {:.6f} MHz (Index:{})", current / 1e6, (int)currentScanIndex);
                                }
                            }
                            
                            continue; // Signal found, stay on this frequency
                        }
                        // No signal at exact frequency - continue to frequency stepping
                        flog::debug("Scanner: No signal at single frequency {:.6f} MHz (level: {:.1f} < {:.1f})", current / 1e6, maxLevel, level);
                    } else {
                        // BAND SCANNING: Search for signals across range using interval stepping
                        if (findSignal(scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, effectiveVfoWidth, data, dataWidth)) {
                            continue; // Signal found using band scanning
                    }
                    
                    // Search for signal in the inverse scan direction if direction isn't enforced
                    if (!reverseLock) {
                            if (findSignal(!scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, effectiveVfoWidth, data, dataWidth)) {
                                continue; // Signal found using reverse band scanning
                        }
                    }
                    else { reverseLock = false; }
                    }
                    

                    // There is no signal on the visible spectrum, tune in scan direction and retry
                    // CRITICAL FIX: Use frequency manager integration or legacy frequency stepping
                    if (useFrequencyManager) {
                        // Use frequency manager for frequency stepping
                                        if (!performFrequencyManagerScanning()) {
                    // Fall back to legacy scanning if frequency manager unavailable
                    flog::warn("Scanner: FM integration failed, falling back to legacy mode");
                    performLegacyScanning();
                }
                    } else {
                        // Legacy frequency stepping
                        if (scanUp) {
                        current = topLimit + interval;
                            // Handle range wrapping for multi-range scanning
                            if (current > currentStop) {
                                // Move to next range or wrap to beginning of current range
                            if (!frequencyRanges.empty()) {
                                    auto activeRanges = getActiveRangeIndices();
                                    if (!activeRanges.empty()) {
                                        currentRangeIndex = (currentRangeIndex + 1) % activeRanges.size();
                                        if (!getCurrentScanBounds(currentStart, currentStop)) {
                                            current = startFreq; // Fallback
                                        } else {
                                            current = currentStart;
                                            // Apply gain setting for new range
                                            applyCurrentRangeGain();
                                        }
                                    } else {
                                        current = currentStart;
                                    }
                                } else {
                                    // Legacy single range wrapping
                                    while (current > stopFreq) {
                                    current = startFreq + (current - stopFreq - interval);
                                    }
                                    if (current < startFreq) { current = startFreq; }
                                }
                            }
                        }
                        else {
                        current = bottomLimit - interval;
                            // Handle range wrapping for multi-range scanning
                            if (current < currentStart) {
                                // Move to previous range or wrap to end of current range
                            if (!frequencyRanges.empty()) {
                                    auto activeRanges = getActiveRangeIndices();
                                    if (!activeRanges.empty()) {
                                        currentRangeIndex = (currentRangeIndex - 1 + activeRanges.size()) % activeRanges.size();
                                        if (!getCurrentScanBounds(currentStart, currentStop)) {
                                            current = stopFreq; // Fallback
                                        } else {
                                            current = currentStop;
                                            // Apply gain setting for new range
                                            applyCurrentRangeGain();
                                        }
                                    } else {
                                        current = currentStop;
                                    }
                                } else {
                                    // Legacy single range wrapping
                                    while (current < startFreq) {
                                    current = stopFreq - (startFreq - current - interval);
                                    }
                                    if (current > stopFreq) { current = stopFreq; }
                            }
                        }
                    }
                    
                    // Update current range bounds after potential range change
                    getCurrentScanBounds(currentStart, currentStop);
                    
                    // Add debug logging
                    flog::warn("Scanner: Tuned to {:.0f} Hz (range: {:.0f} - {:.0f})", current, currentStart, currentStop);

                    // If the new current frequency is outside the visible bandwidth, wait for retune
                    if (current - (effectiveVfoWidth/2.0) < wfStart || current + (effectiveVfoWidth/2.0) > wfEnd) {
                        lastTuneTime = now;
                        tuning = true;
                    }
                }
                } // End of legacy frequency stepping

                // FFT data already released above after copying
                
                } catch (const std::exception& e) {
                    flog::error("Scanner: Exception in worker loop: {}", e.what());
                    running = false;
                    break;
                } catch (...) {
                    flog::error("Scanner: Unknown exception in worker loop");
                    running = false;
                    break;
                }
            }
        } catch (const std::exception& e) {
            flog::error("Scanner: Critical exception in worker thread: {}", e.what());
            running = false;
        } catch (...) {
            flog::error("Scanner: Critical unknown exception in worker thread");
            running = false;
        }
        
        flog::info("Scanner: Worker thread ended");
    }

    bool findSignal(bool scanDir, double& bottomLimit, double& topLimit, double wfStart, double wfEnd, double wfWidth, double vfoWidth, float* data, int dataWidth) {
        bool found = false;
        double freq = current;
        int maxIterations = 1000; // Prevent infinite loops
        int iterations = 0;
        
        // Get current range bounds
        double currentStart, currentStop;
        if (!getCurrentScanBounds(currentStart, currentStop)) {
            return false; // No valid range
        }
        
        for (freq += scanDir ? interval : -interval;
            scanDir ? (freq <= currentStop) : (freq >= currentStart);
            freq += scanDir ? interval : -interval) {
            
            iterations++;
            if (iterations > maxIterations) {
                flog::warn("Scanner: Max iterations reached, forcing frequency wrap");
                break;
            }

            // Check if signal is within bounds
            if (freq - (vfoWidth/2.0) < wfStart) { break; }
            if (freq + (vfoWidth/2.0) > wfEnd) { break; }

            // Check if frequency is blacklisted
            if (isFrequencyBlacklisted(freq)) {
                continue;
            }

            if (freq < bottomLimit) { bottomLimit = freq; }
            if (freq > topLimit) { topLimit = freq; }
            
            // Check signal level
            float maxLevel = getMaxLevel(data, freq, vfoWidth * (passbandRatio * 0.01f), dataWidth, wfStart, wfWidth);
            if (maxLevel >= level) {
                found = true;
                receiving = true;
                current = freq;
                
                // TUNING PROFILE APPLICATION: Apply profile when signal found (CRITICAL FIX)
                if (useFrequencyManager && applyProfiles && currentTuningProfile && !gui::waterfall.selectedVFO.empty()) {
                    const TuningProfile* profile = static_cast<const TuningProfile*>(currentTuningProfile);
                    if (profile) {
                        applyTuningProfileSmart(*profile, gui::waterfall.selectedVFO, freq, "BAND-SIGNAL");
                    }
                } else {
                    if (useFrequencyManager && applyProfiles && !currentTuningProfile) {
                        flog::debug("Scanner: No profile available for {:.6f} MHz BAND (Index:{})", freq / 1e6, (int)currentScanIndex);
                    }
                }
                
                break;
            }
        }
        return found;
    }

    // DISCRETE PARAMETER HELPERS: Sync indices with actual values
    void initializeDiscreteIndices() {
        flog::debug("Scanner: initializeDiscreteIndices() called - BEFORE: passbandIndex={}, passbandRatio={}", passbandIndex, passbandRatio);
        // Find closest interval index
        intervalIndex = 4; // Default to 100 kHz
        double minDiff = std::abs(interval - INTERVAL_VALUES_HZ[intervalIndex]);
        for (int i = 0; i < INTERVAL_VALUES_COUNT; i++) {
            double diff = std::abs(interval - INTERVAL_VALUES_HZ[i]);
            if (diff < minDiff) {
                intervalIndex = i;
                minDiff = diff;
            }
        }
        
        // Find closest scan rate index
        scanRateIndex = 3; // Default to 10/sec
        int minScanDiff = std::abs(scanRateHz - SCAN_RATE_VALUES[scanRateIndex]);
        for (int i = 0; i < SCAN_RATE_VALUES_COUNT; i++) {
            int diff = std::abs(scanRateHz - SCAN_RATE_VALUES[i]);
            if (diff < minScanDiff) {
                scanRateIndex = i;
                minScanDiff = diff;
            }
        }
        
        // Find closest passband index
        passbandIndex = 6; // Default to 100% (recommended starting point)
        double minPassbandDiff = std::abs(passbandRatio - PASSBAND_VALUES[passbandIndex]);
        for (int i = 0; i < PASSBAND_VALUES_COUNT; i++) {
            double diff = std::abs(passbandRatio - PASSBAND_VALUES[i]);
            if (diff < minPassbandDiff) {
                passbandIndex = i;
                minPassbandDiff = diff;
            }
        }
        flog::debug("Scanner: initializeDiscreteIndices() completed - AFTER: passbandIndex={}, passbandRatio={}", passbandIndex, passbandRatio);
    }
    
    void syncDiscreteValues() {
        interval = INTERVAL_VALUES_HZ[intervalIndex];
        scanRateHz = SCAN_RATE_VALUES[scanRateIndex];
        passbandRatio = PASSBAND_VALUES[passbandIndex];
        flog::debug("Scanner: syncDiscreteValues - passbandIndex={}, passbandRatio={}", passbandIndex, passbandRatio);
    }
    
    // BLACKLIST: Helper function for consistent blacklist checking
    bool isFrequencyBlacklisted(double frequency) const {
        return std::any_of(blacklistedFreqs.begin(), blacklistedFreqs.end(),
                          [frequency, this](double blacklistedFreq) {
                              return std::abs(frequency - blacklistedFreq) < blacklistTolerance;
                          });
    }
    
    // ENHANCED UX: Look up frequency manager entry name for a given frequency
    std::string lookupFrequencyManagerName(double frequency) const {
        try {
            // Check if frequency manager interface is available
            if (!core::modComManager.interfaceExists("frequency_manager")) {
                return "";
            }
            
            // Use new frequency manager interface to get bookmark name
            const int CMD_GET_BOOKMARK_NAME = 2;
            std::string bookmarkName;
            
            if (!core::modComManager.callInterface("frequency_manager", CMD_GET_BOOKMARK_NAME, 
                                                 const_cast<double*>(&frequency), &bookmarkName)) {
                flog::debug("Scanner: Failed to call frequency manager getBookmarkName interface");
                return "";
            }
            
            return bookmarkName;
            
        } catch (const std::exception& e) {
            flog::debug("Scanner: Error looking up frequency manager name: {}", e.what());
            return "";
        }
    }
    
    // PERFORMANCE-OPTIMIZED: Smart profile application with caching (prevents redundant operations)
    bool applyTuningProfileSmart(const TuningProfile& profile, const std::string& vfoName, double frequency, const char* context = "Scanner") {
        // PERFORMANCE CHECK: Skip if same profile already applied to same VFO recently
        if (lastAppliedProfile == &profile && 
            lastAppliedVFO == vfoName && 
            std::abs(lastProfileFrequency - frequency) < 1000.0) { // Within 1 kHz
            
            flog::debug("{}: SKIPPED redundant profile '{}' for {:.6f} MHz (already applied)", 
                       context, profile.name.empty() ? "Auto" : profile.name, frequency / 1e6);
            return false; // Skipped - no change needed
        }
        
        // Apply the profile using fast method
        bool success = applyTuningProfileFast(profile, vfoName);
        
        if (success) {
            // Cache the successful application
            lastAppliedProfile = &profile;
            lastProfileFrequency = frequency;
            lastAppliedVFO = vfoName;
            
            flog::info("{}: APPLIED PROFILE '{}' for {:.6f} MHz (Mode:{} BW:{:.1f}kHz Squelch:{}@{:.1f}dB)", 
                      context, profile.name.empty() ? "Auto" : profile.name, 
                      frequency / 1e6, profile.demodMode, profile.bandwidth / 1000.0f,
                      profile.squelchEnabled ? "ON" : "OFF", profile.squelchLevel);
        }
        
        return success;
    }
    
    // PERFORMANCE-CRITICAL: Fast tuning profile application (< 10ms target)
    bool applyTuningProfileFast(const TuningProfile& profile, const std::string& vfoName) {
        try {
            if (!core::modComManager.interfaceExists(vfoName) || 
                core::modComManager.getModuleName(vfoName) != "radio") {
                return false;
            }
            
            // Core demodulation settings (fast direct calls)
            int mode = profile.demodMode;
            float bandwidth = profile.bandwidth;
            core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_MODE, &mode, NULL);
            core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_BANDWIDTH, &bandwidth, NULL);
            
            // SQUELCH CONTROL: Use existing radio interface (available!)
            if (profile.squelchEnabled) {
                bool squelchEnabled = profile.squelchEnabled;
                float squelchLevel = profile.squelchLevel;
                core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_SQUELCH_ENABLED, &squelchEnabled, NULL);
                core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_SQUELCH_LEVEL, &squelchLevel, NULL);
            } else {
                bool squelchDisabled = false;
                core::modComManager.callInterface(vfoName, RADIO_IFACE_CMD_SET_SQUELCH_ENABLED, &squelchDisabled, NULL);
            }
            
            // RF GAIN CONTROL: Use universal gain API from source manager
            if (profile.rfGain > 0.0f) {
                sigpath::sourceManager.setGain(profile.rfGain);
            }
            
            // TODO: AGC settings require direct demodulator access (not exposed via radio interface yet)
            // Available in TuningProfile: agcEnabled, but no radio interface command for AGC mode
            
            return true; // Success
            
        } catch (const std::exception& e) {
            flog::error("Scanner: Error applying tuning profile: {}", e.what());
            return false; // Failure
        }
    }
    
    // PERFORMANCE-CRITICAL: Frequency manager integration (< 5ms target)  
    bool performFrequencyManagerScanning() {
        // PERFORMANCE FIX: Check interface exists only once on first call
        static bool interfaceChecked = false;
        static bool interfaceAvailable = false;
        
        if (!interfaceChecked) {
            interfaceAvailable = core::modComManager.interfaceExists("frequency_manager");
            if (!interfaceAvailable) {
                flog::warn("Scanner: Frequency manager module NOT AVAILABLE - check if module is enabled/loaded");
                flog::warn("Scanner: Falling back to legacy scanning (interval setting will be used)");
            }
            interfaceChecked = true;
        }
        
        if (!interfaceAvailable) {
            return false;
        }
        
        try {
            // REAL FREQUENCY MANAGER INTEGRATION: Get actual scan list
            // Note: ModuleComManager doesn't have getInstance, we'll use the interface directly

            // Get the actual scan list from frequency manager
            static std::vector<double> realScanList;
            static std::vector<bool> realScanTypes;
            static std::vector<const void*> realScanProfiles; // Store profile pointers
            static bool scanListLoaded = false;
            static auto lastScanListUpdate = std::chrono::steady_clock::now();
            
            // Refresh scan list every 5 seconds to pick up frequency manager changes
            auto now = std::chrono::steady_clock::now();
            auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::seconds>(now - lastScanListUpdate);
            if (timeSinceUpdate.count() >= 5) {
                scanListLoaded = false; // Force refresh
                lastScanListUpdate = now;
            }
            
            if (!scanListLoaded) {
                try {
                    flog::info("Scanner: Loading REAL frequency manager scan list...");
                    
                    // REAL INTERFACE CALL: Get scan list from frequency manager  
                    // CRITICAL: Use frequency manager's real ScanEntry structure (must match exactly!)
                    struct ScanEntry {
                        double frequency;                           // Target frequency
                        const TuningProfile* profile;              // Real profile pointer (not void*)
                        const FrequencyBookmark* bookmark;         // Real bookmark pointer (not void*)
                        bool isFromBand;                           // true = from band, false = direct frequency
                        
                        // SAFETY: This struct MUST match frequency_manager's ScanEntry exactly
                        // Any mismatch will cause memory corruption
                    };
                    const std::vector<ScanEntry>* scanList = nullptr;
                    const int CMD_GET_SCAN_LIST = 1; // Match frequency manager command
                    
                    if (!core::modComManager.callInterface("frequency_manager", CMD_GET_SCAN_LIST, nullptr, &scanList)) {
                        flog::error("Scanner: Failed to call frequency manager getScanList interface");
                        return false;
                    }
                    
                    if (!scanList || scanList->empty()) {
                        flog::warn("Scanner: No scannable entries found in frequency manager");
                        flog::warn("Scanner: Please add some frequencies to your frequency manager and mark them as scannable (S checkbox)");
                        return false;
                    }
                    
                    // Convert ScanEntry list to scanner format
                    realScanList.clear();
                    realScanTypes.clear();
                    realScanProfiles.clear();
            
            // CRITICAL: Comprehensive profile extraction with full diagnostics
            for (const auto& entry : *scanList) {
                realScanList.push_back(entry.frequency);
                realScanTypes.push_back(!entry.isFromBand); // Single frequency if NOT from band  
                realScanProfiles.push_back(entry.profile);   // Store profile pointer
                
                // DIAGNOSTIC: Log each profile extraction for debugging
                if (entry.profile != nullptr) {
                    const TuningProfile* profile = entry.profile;
                    std::string profileName = profile->name.empty() ? "Auto" : profile->name;
                    flog::info("Scanner: Entry {:.6f} MHz - Profile: '{}' (Mode:{} BW:{:.1f}kHz Squelch:{}@{:.1f}dB RFGain:{:.1f}dB)", 
                              entry.frequency / 1e6,
                              profileName,
                              profile->demodMode, 
                              profile->bandwidth / 1000.0f,
                              profile->squelchEnabled ? "ON" : "OFF",
                              profile->squelchLevel,
                              profile->rfGain);
                } else {
                    flog::warn("Scanner: Entry {:.6f} MHz - NO PROFILE (null pointer)", entry.frequency / 1e6);
                }
            }
                    
                    scanListLoaded = true;
                    flog::info("Scanner: Loaded {} real scannable entries from frequency manager", (int)realScanList.size());
                    
                    // VERIFICATION: Cross-check profile array integrity
                    if (realScanList.size() != realScanProfiles.size()) {
                        flog::error("Scanner: CRITICAL BUG - Array size mismatch! Frequencies:{} Profiles:{}", 
                                   (int)realScanList.size(), (int)realScanProfiles.size());
                    }
                    
                    // DIAGNOSTIC: Check for profile pointer duplication
                    std::set<const void*> uniqueProfiles;
                    int nullCount = 0;
                    for (const auto* profile : realScanProfiles) {
                        if (profile) {
                            uniqueProfiles.insert(profile);
                        } else {
                            nullCount++;
                        }
                    }
                    flog::info("Scanner: Profile Analysis - Total:{} Unique:{} Null:{}", 
                              (int)realScanProfiles.size(), (int)uniqueProfiles.size(), nullCount);
                    if (realScanList.size() > 10) {
                        flog::info("Scanner: ... and {} more entries", (int)(realScanList.size() - 10));
                    }
                    
                } catch (const std::exception& e) {
                    flog::error("Scanner: Failed to load frequency manager scan list: {}", e.what());
                    return false;
                }
            }
            
            const std::vector<double>& testScanList = realScanList;
            const std::vector<bool>& isSingleFrequency = realScanTypes;
            const std::vector<const void*>& testScanProfiles = realScanProfiles;
            
            // Real frequency manager integration - no more demo mode!
            
            // Initialize current frequency if not already in scan list  
            // Also ensure starting frequency is not blacklisted
            bool currentFreqInList = false;
            for (const auto& freq : testScanList) {
                if (std::abs(current - freq) < 1000.0) { // Within 1 kHz tolerance
                    currentFreqInList = true;
                    break;
                }
            }
            
            // Count how many frequencies are blacklisted for user info
            int blacklistedCount = 0;
            for (const auto& freq : testScanList) {
                if (isFrequencyBlacklisted(freq)) {
                    blacklistedCount++;
                }
            }
            if (blacklistedCount > 0) {
                flog::info("Scanner: {} of {} frequency manager entries are blacklisted and will be skipped", 
                          blacklistedCount, (int)testScanList.size());
            }
            
            if (!currentFreqInList || isFrequencyBlacklisted(current)) {
                // Find first non-blacklisted frequency to start with
                bool foundStartFreq = false;
                for (size_t i = 0; i < testScanList.size(); i++) {
                    if (!isFrequencyBlacklisted(testScanList[i])) {
                        current = testScanList[i];
                        currentScanIndex = i;
                        // Store the corresponding profile
                        if (i < testScanProfiles.size()) {
                            currentTuningProfile = testScanProfiles[i];
                            // DIAGNOSTIC: Log profile assignment during initialization (reduced logging)
                            if (currentTuningProfile) {
                                const TuningProfile* profile = static_cast<const TuningProfile*>(currentTuningProfile);
                                
                                // CRITICAL FIX: Apply profile IMMEDIATELY when selecting start frequency
                                // This ensures radio starts in the correct mode
                                if (applyProfiles && !gui::waterfall.selectedVFO.empty()) {
                                    applyTuningProfileSmart(*profile, gui::waterfall.selectedVFO, testScanList[i], "STARTUP");
                                }
                            } else {
                                flog::debug("Scanner: INIT NULL PROFILE for start freq {:.6f} MHz (Index:{})", 
                                           testScanList[i] / 1e6, (int)i);
                            }
                        } else {
                            currentTuningProfile = nullptr;
                            flog::warn("Scanner: INIT INDEX OUT OF BOUNDS for profile! Index:{} Size:{}", 
                                      (int)i, (int)testScanProfiles.size());
                        }
                        foundStartFreq = true;
                        break;
                    }
                }
                
                if (!foundStartFreq) {
                    flog::error("Scanner: All frequencies in frequency manager are blacklisted!");
                    return false;
                }
                
                flog::info("Scanner: Starting with non-blacklisted frequency {:.6f} MHz", current / 1e6);
            }
            
            if (testScanList.empty()) {
                return false;
            }
            
            // Find current scan index matching current frequency and store its profile
            for (size_t i = 0; i < testScanList.size(); i++) {
                if (std::abs(current - testScanList[i]) < 1000.0) { // Within 1 kHz tolerance
                    currentScanIndex = i;
                    // Store the corresponding profile
                    if (i < testScanProfiles.size()) {
                        currentTuningProfile = testScanProfiles[i];
                        // DIAGNOSTIC: Log profile assignment during current frequency lookup (reduced logging)
                        if (currentTuningProfile) {
                            const TuningProfile* profile = static_cast<const TuningProfile*>(currentTuningProfile);
                            
                            // CRITICAL FIX: Apply profile IMMEDIATELY when finding current frequency
                            // This ensures radio is in correct mode from the start
                            if (applyProfiles && !gui::waterfall.selectedVFO.empty()) {
                                applyTuningProfileSmart(*profile, gui::waterfall.selectedVFO, current, "INITIAL");
                            }
                        } else {
                            flog::debug("Scanner: LOOKUP NULL PROFILE for current freq {:.6f} MHz (Index:{})", 
                                       current / 1e6, (int)i);
                        }
                    } else {
                        currentTuningProfile = nullptr;
                        flog::warn("Scanner: LOOKUP INDEX OUT OF BOUNDS for profile! Index:{} Size:{}", 
                                  (int)i, (int)testScanProfiles.size());
                    }
                    break;
                }
            }
            
            // Ensure current index is valid
            if (currentScanIndex >= testScanList.size()) {
                currentScanIndex = 0;
                current = testScanList[0];  // Ensure current matches index
            }
            
            // FREQUENCY STEPPING WITH BLACKLIST SUPPORT: Step to next non-blacklisted frequency
            size_t originalIndex = currentScanIndex;
            int attempts = 0;
            const int maxAttempts = (int)testScanList.size(); // Avoid infinite loop
            
            do {
                // Step to next frequency in scan list
                if (scanUp) {
                    currentScanIndex = (currentScanIndex + 1) % testScanList.size();
                } else {
                    currentScanIndex = (currentScanIndex == 0) ? testScanList.size() - 1 : currentScanIndex - 1;
                }
                
                // Set current frequency to the NEW scan list entry
                current = testScanList[currentScanIndex];
                
                // Store the corresponding tuning profile for later use
                if (currentScanIndex < testScanProfiles.size()) {
                    currentTuningProfile = testScanProfiles[currentScanIndex];
                    // DIAGNOSTIC: Log profile tracking during frequency stepping (reduced logging)
                    if (currentTuningProfile) {
                        const TuningProfile* profile = static_cast<const TuningProfile*>(currentTuningProfile);
                        
                        // CRITICAL FIX: Apply profile IMMEDIATELY when stepping to frequency
                        // This ensures radio is in correct mode BEFORE signal detection
                        if (applyProfiles && !gui::waterfall.selectedVFO.empty()) {
                            applyTuningProfileSmart(*profile, gui::waterfall.selectedVFO, current, "PREEMPTIVE");
                        }
                    } else {
                        flog::debug("Scanner: TRACKING NULL PROFILE for {:.6f} MHz (Index:{})", 
                                   current / 1e6, (int)currentScanIndex);
                    }
                } else {
                    currentTuningProfile = nullptr;
                    flog::warn("Scanner: INDEX OUT OF BOUNDS for profile tracking! Index:{} Size:{}", 
                              (int)currentScanIndex, (int)testScanProfiles.size());
                }
                
                attempts++;
                
                // Check if this frequency is blacklisted
                if (!isFrequencyBlacklisted(current)) {
                    // Found a non-blacklisted frequency
                    break;
                } else {
                    flog::debug("Scanner: Skipping blacklisted frequency {:.3f} MHz", current / 1e6);
                    // Continue to next frequency
                }
                
            } while (attempts < maxAttempts && currentScanIndex != originalIndex);
            
            // Check if we found any non-blacklisted frequency
            if (attempts >= maxAttempts || isFrequencyBlacklisted(current)) {
                flog::warn("Scanner: All frequencies in scan list are blacklisted!");
                return false; // No valid frequencies to scan
            }
            
            // CRITICAL: Store entry type for adaptive signal detection
            bool isCurrentSingleFreq = (currentScanIndex < isSingleFrequency.size()) ? isSingleFrequency[currentScanIndex] : false;
            currentEntryIsSingleFreq = isCurrentSingleFreq; // Update global state
            
            // CRITICAL: Immediate VFO tuning (same as performLegacyScanning) 
            // This frequency is guaranteed to NOT be blacklisted
            tuner::normalTuning(gui::waterfall.selectedVFO, current);
            tuning = true;
            lastTuneTime = std::chrono::high_resolution_clock::now();
            
            flog::debug("Scanner: Stepped to non-blacklisted frequency {:.6f} MHz ({})", 
                       current / 1e6, currentEntryIsSingleFreq ? "single freq" : "band");
            
            return true; // Successfully performed FM frequency stepping
            
        } catch (const std::exception& e) {
            flog::error("Scanner: Error in frequency manager scanning: {}", e.what());
            return false;
        }
    }
    
    // PERFORMANCE-OPTIMIZED: Legacy scanning fallback
    void performLegacyScanning() {
        // Legacy mode always uses band-style detection (wide tolerance)
        currentEntryIsSingleFreq = false;
        // Get current range bounds (supports multi-range and legacy single range)
        double currentStart, currentStop;
        if (!getCurrentScanBounds(currentStart, currentStop)) {
            // Use single range fallback
            currentStart = startFreq;
            currentStop = stopFreq;
        }
        
        // Ensure current frequency is within bounds
        if (current < currentStart || current > currentStop) {
            current = currentStart;
        }
        
        // Simple linear stepping
        current += (scanUp ? interval : -interval);
        if (current > currentStop) current = currentStart;
        if (current < currentStart) current = currentStop;
        
        // Standard tuning
        tuner::normalTuning(gui::waterfall.selectedVFO, current);
        tuning = true;
        lastTuneTime = std::chrono::high_resolution_clock::now();
    }

    float getMaxLevel(float* data, double freq, double width, int dataWidth, double wfStart, double wfWidth) {
        double low = freq - (width/2.0);
        double high = freq + (width/2.0);
        int lowId = std::clamp<int>((low - wfStart) * (double)dataWidth / wfWidth, 0, dataWidth - 1);
        int highId = std::clamp<int>((high - wfStart) * (double)dataWidth / wfWidth, 0, dataWidth - 1);
        float max = -INFINITY;
        for (int i = lowId; i <= highId; i++) {
            if (data[i] > max) { max = data[i]; }
        }
        return max;
    }

    std::string name;
    bool enabled = true;
    
    bool running = false;
    //std::string selectedVFO = "Radio";
    
    // Multiple frequency ranges support
    std::vector<FrequencyRange> frequencyRanges;
    int currentRangeIndex = 0;
    
    // Legacy single-range support (for backward compatibility)
    double startFreq = 88000000.0;
    double stopFreq = 108000000.0;
    
    double interval = 100000.0;
    double current = 88000000.0;
    double passbandRatio = 10.0;
    int tuningTime = 250;
    int lingerTime = 1000.0;
    float level = -50.0;
    bool receiving = false;  // Should start as false, not receiving initially
    bool tuning = false;
    bool scanUp = true;
    bool reverseLock = false;
    bool configNeedsSave = false; // Flag for delayed config saving
    std::chrono::time_point<std::chrono::high_resolution_clock> lastSignalTime = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> lastTuneTime = std::chrono::high_resolution_clock::now();
    std::thread workerThread;
    std::mutex scanMtx;
    
    // Blacklist functionality
    std::vector<double> blacklistedFreqs;
    double blacklistTolerance = 1000.0; // Tolerance in Hz for blacklisted frequencies
    
    // UI state for range management
    bool showRangeManager = false;
    char newRangeName[256] = "New Range";
    double newRangeStart = 88000000.0;
    double newRangeStop = 108000000.0;
    float newRangeGain = 20.0f;
    
    // PERFORMANCE: Frequency manager integration (always enabled for simplified operation)
    bool useFrequencyManager = true;     // Always enabled - scanner uses frequency manager exclusively  
    bool applyProfiles = true;           // Always enabled - automatically apply tuning profiles
    size_t currentScanIndex = 0;         // Current position in frequency manager scan list
    bool currentEntryIsSingleFreq = false; // Track if current entry is single frequency vs band
    const void* currentTuningProfile = nullptr; // Current frequency's tuning profile (from FM)
    
    // PERFORMANCE: Smart profile caching to prevent redundant applications
    const void* lastAppliedProfile = nullptr; // Last successfully applied profile
    double lastProfileFrequency = 0.0;        // Frequency where last profile was applied
    std::string lastAppliedVFO = "";           // VFO name where profile was applied
    
    // PERFORMANCE: Configurable scan timing (consistent across all modes) 
    int scanRateHz = 10;                 // Default: 10 scans per second (same as legacy 100ms)
    
    // DISCRETE PARAMETER CONTROLS: Safe preset values to prevent user errors
    static constexpr double INTERVAL_VALUES_HZ[] = {5000, 10000, 25000, 50000, 100000, 200000};
    static constexpr const char* INTERVAL_LABELS[] = {"5 kHz", "10 kHz", "25 kHz", "50 kHz", "100 kHz", "200 kHz"};
    static constexpr int INTERVAL_VALUES_COUNT = 6;
    int intervalIndex = 4; // Default to 100 kHz (index 4)
    
    static constexpr int SCAN_RATE_VALUES[] = {1, 2, 5, 10, 15, 20, 25, 30, 40, 50};
    static constexpr const char* SCAN_RATE_LABELS[] = {"1/sec", "2/sec", "5/sec", "10/sec", "15/sec", "20/sec", "25/sec", "30/sec", "40/sec", "50/sec"};
    static constexpr int SCAN_RATE_VALUES_COUNT = 10;
    int scanRateIndex = 6; // Default to 25/sec (index 6, recommended starting point)
    
    static constexpr int PASSBAND_VALUES[] = {5, 10, 20, 30, 50, 75, 100};
    static constexpr const char* PASSBAND_LABELS[] = {"5%", "10%", "20%", "30%", "50%", "75%", "100%"};
    static constexpr const char* PASSBAND_FORMATS[] = {"5%%", "10%%", "20%%", "30%%", "50%%", "75%%", "100%%"}; // Safe for ImGui format strings
    static constexpr int PASSBAND_VALUES_COUNT = 7;
    int passbandIndex = 6; // Default to 100% (index 6, recommended starting point)
    

};

MOD_EXPORT void _INIT_() {
    json def = json({});
    
    // Legacy single range (for backward compatibility)
    def["startFreq"] = 88000000.0;
    def["stopFreq"] = 108000000.0;
    
    // Common scanner parameters
    def["interval"] = 100000.0;  // 100 kHz - good balance of speed vs coverage
    def["passbandRatio"] = 100.0;  // 100% - recommended starting point for best signal detection
    def["tuningTime"] = 250;
    def["lingerTime"] = 1000.0;
    def["level"] = -50.0;
    def["blacklistTolerance"] = 1000.0;
    def["blacklistedFreqs"] = json::array();
    
    // Scanning direction preference 
    def["scanUp"] = true; // Default to increasing frequency
    
    // Multiple frequency ranges support
    def["frequencyRanges"] = json::array();
    def["currentRangeIndex"] = 0;
    
    // Frequency manager integration (now always enabled for simplified operation)
    def["scanRateHz"] = 25;                 // 25 scans per second (recommended starting point)

    config.setPath(core::args["root"].s() + "/scanner_config.json");
    config.load(def);
    config.enableAutoSave();
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new ScannerModule(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (ScannerModule*)instance;
}

MOD_EXPORT void _END_() {
    config.disableAutoSave();
    config.save();
}