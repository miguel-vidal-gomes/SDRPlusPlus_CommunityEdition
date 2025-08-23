#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <chrono>
#include <algorithm>
#include <fstream> // Added for file operations
#include <core.h>

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
        
        // === FREQUENCY RANGES SECTION ===
        ImGui::Text("Frequency Ranges");
        ImGui::Separator();
        
        // Show current active ranges count
        auto activeRanges = _this->getActiveRangeIndices();
        ImGui::Text("Active ranges: %d/%d", (int)activeRanges.size(), (int)_this->frequencyRanges.size());
        
        // Show current scanning range info
        if (!activeRanges.empty() && _this->currentRangeIndex < activeRanges.size()) {
            int rangeIdx = activeRanges[_this->currentRangeIndex];
            // Critical bounds check to prevent crash
            if (rangeIdx < _this->frequencyRanges.size()) {
                ImGui::Separator();
                
                // Current range display
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // Green text
                ImGui::Text("Current Range: %s (%.1f dB)", _this->frequencyRanges[rangeIdx].name.c_str(), _this->frequencyRanges[rangeIdx].gain);
                ImGui::PopStyleColor();
                
                ImGui::Separator();
            }
        }
        
        // Range management buttons
        if (ImGui::Button("Manage Ranges", ImVec2(menuWidth * 0.48f, 0))) {
            _this->showRangeManager = !_this->showRangeManager;
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Quick Range", ImVec2(menuWidth * 0.48f, 0))) {
            // Add a new range with current single-range values as default
            _this->addFrequencyRange("New Range", _this->startFreq, _this->stopFreq, true);
        }
        
        // Range Manager Window
        if (_this->showRangeManager) {
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
        
        // Legacy single range controls (for backward compatibility or when no ranges exist)
        if (_this->frequencyRanges.empty()) {
            ImGui::Spacing();
            ImGui::Text("Legacy Single Range Mode");
            ImGui::Separator();
            
            if (_this->running) { ImGui::BeginDisabled(); }
            ImGui::LeftLabel("Start");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputDouble("##start_freq_scanner", &_this->startFreq, 100.0, 100000.0, "%0.0f")) {
                _this->startFreq = round(_this->startFreq);
                _this->saveConfig();
            }
            ImGui::LeftLabel("Stop");
            ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
            if (ImGui::InputDouble("##stop_freq_scanner", &_this->stopFreq, 100.0, 100000.0, "%0.0f")) {
                _this->stopFreq = round(_this->stopFreq);
                _this->saveConfig();
            }
            if (_this->running) { ImGui::EndDisabled(); }
        }
        
        // === COMMON SCANNER PARAMETERS ===
        ImGui::Spacing();
        ImGui::Text("Scanner Parameters");
        ImGui::Separator();
        
        if (_this->running) { ImGui::BeginDisabled(); }
        ImGui::LeftLabel("Interval");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##interval_scanner", &_this->interval, 100.0, 100000.0, "%0.0f")) {
            _this->interval = round(_this->interval);
            _this->saveConfig();
        }
        ImGui::LeftLabel("Passband Ratio (%)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##pb_ratio_scanner", &_this->passbandRatio, 1.0, 10.0, "%0.0f")) {
            _this->passbandRatio = std::clamp<double>(round(_this->passbandRatio), 1.0, 100.0);
            _this->saveConfig();
        }
        ImGui::LeftLabel("Tuning Time (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##tuning_time_scanner", &_this->tuningTime, 100, 1000)) {
            _this->tuningTime = std::clamp<int>(_this->tuningTime, 100, 10000);
            _this->saveConfig();
        }
        ImGui::LeftLabel("Linger Time (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##linger_time_scanner", &_this->lingerTime, 100, 1000)) {
            _this->lingerTime = std::clamp<int>(_this->lingerTime, 100, 10000);
            _this->saveConfig();
        }
        if (_this->running) { ImGui::EndDisabled(); }

        ImGui::LeftLabel("Level");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::SliderFloat("##scanner_level", &_this->level, -150.0, 0.0)) {
            _this->saveConfig();
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
                // Each frequency on its own line with remove button
                ImGui::Text("%.0f Hz (%.3f MHz)", _this->blacklistedFreqs[i], _this->blacklistedFreqs[i] / 1e6);
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
        if (ImGui::Button(("<<##scanner_back_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            std::lock_guard<std::mutex> lck(_this->scanMtx);
            _this->reverseLock = true;
            _this->receiving = false;
            _this->scanUp = false;
        }
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button((">>##scanner_forw_" + _this->name).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            std::lock_guard<std::mutex> lck(_this->scanMtx);
            _this->reverseLock = true;
            _this->receiving = false;
            _this->scanUp = true;
        }
        ImGui::EndTable();

        if (!_this->running) {
            if (ImGui::Button("Start##scanner_start", ImVec2(menuWidth, 0))) {
                _this->start();
            }
            ImGui::Text("Status: Idle");
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
    }

    void start() {
        if (running) { 
            flog::warn("Scanner: Already running");
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
        
        config.release(true);
    }

    void loadConfig() {
        config.acquire();
        startFreq = config.conf.value("startFreq", 88000000.0);
        stopFreq = config.conf.value("stopFreq", 108000000.0);
        interval = config.conf.value("interval", 100000.0);
        passbandRatio = config.conf.value("passbandRatio", 10.0);
        tuningTime = config.conf.value("tuningTime", 250);
        lingerTime = config.conf.value("lingerTime", 1000.0);
        level = config.conf.value("level", -50.0);
        blacklistTolerance = config.conf.value("blacklistTolerance", 1000.0);
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
    }

    void worker() {
        flog::info("Scanner: Worker thread started");
        try {
            // 10Hz scan loop
            while (running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                try {
                    std::lock_guard<std::mutex> lck(scanMtx);
                    auto now = std::chrono::high_resolution_clock::now();

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
                if (current < currentStart || current > currentStop) {
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

                // Get FFT data
                int dataWidth = 0;
                float* data = gui::waterfall.acquireLatestFFT(dataWidth);
                if (!data) { 
                    flog::debug("Scanner: No FFT data available");
                    continue; 
                }
                if (dataWidth <= 0) {
                    flog::warn("Scanner: Invalid FFT data width: {}", dataWidth);
                    gui::waterfall.releaseLatestFFT();
                    continue;
                }

                // Get gather waterfall data
                double wfCenter = gui::waterfall.getViewOffset() + gui::waterfall.getCenterFrequency();
                double wfWidth = gui::waterfall.getViewBandwidth();
                double wfStart = wfCenter - (wfWidth / 2.0);
                double wfEnd = wfCenter + (wfWidth / 2.0);

                // Gather VFO data
                double vfoWidth = sigpath::vfoManager.getBandwidth(gui::waterfall.selectedVFO);

                if (receiving) {
                    flog::debug("Scanner: Receiving signal...");
                
                    float maxLevel = getMaxLevel(data, current, vfoWidth, dataWidth, wfStart, wfWidth);
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
                    
                    // Search for a signal in scan direction
                    if (findSignal(scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, vfoWidth, data, dataWidth)) {
                        gui::waterfall.releaseLatestFFT();
                        continue;
                    }
                    
                    // Search for signal in the inverse scan direction if direction isn't enforced
                    if (!reverseLock) {
                        if (findSignal(!scanUp, bottomLimit, topLimit, wfStart, wfEnd, wfWidth, vfoWidth, data, dataWidth)) {
                            gui::waterfall.releaseLatestFFT();
                            continue;
                        }
                    }
                    else { reverseLock = false; }
                    

                    // There is no signal on the visible spectrum, tune in scan direction and retry
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
                    if (current - (vfoWidth/2.0) < wfStart || current + (vfoWidth/2.0) > wfEnd) {
                        lastTuneTime = now;
                        tuning = true;
                    }
                }

                // Release FFT Data
                gui::waterfall.releaseLatestFFT();
                
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
            if (std::any_of(blacklistedFreqs.begin(), blacklistedFreqs.end(),
                            [freq, this](double blacklistedFreq) {
                                return std::abs(freq - blacklistedFreq) < blacklistTolerance;
                            })) {
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
                break;
            }
        }
        return found;
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
    

};

MOD_EXPORT void _INIT_() {
    json def = json({});
    
    // Legacy single range (for backward compatibility)
    def["startFreq"] = 88000000.0;
    def["stopFreq"] = 108000000.0;
    
    // Common scanner parameters
    def["interval"] = 100000.0;
    def["passbandRatio"] = 10.0;
    def["tuningTime"] = 250;
    def["lingerTime"] = 1000.0;
    def["level"] = -50.0;
    def["blacklistTolerance"] = 1000.0;
    def["blacklistedFreqs"] = json::array();
    
    // Multiple frequency ranges support
    def["frequencyRanges"] = json::array();
    def["currentRangeIndex"] = 0;

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