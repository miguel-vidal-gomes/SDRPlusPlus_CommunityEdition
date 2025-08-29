#include <imgui.h>
#include <module.h>
#include <gui/gui.h>
#include <gui/style.h>
#include <signal_path/signal_path.h>
#include <thread>
#include <chrono>
#include <random>

SDRPP_MOD_INFO{
    /* Name:            */ "spiritbox_emulator",
    /* Description:     */ "Spiritbox emulator module for SDR++ - Random frequency sweeping for paranormal investigation",
    /* Author:          */ "Jack Heinlein",
    /* Version:         */ 0, 1, 0,
    /* Max instances    */ 1
};

class SpiritboxEmulator : public ModuleManager::Instance {
public:
    SpiritboxEmulator(std::string name) {
        this->name = name;
        gui::menu.registerEntry(name, menuHandler, this, NULL);
        
        // Initialize random number generator
        std::random_device rd;
        rng = std::mt19937(rd());
    }

    ~SpiritboxEmulator() {
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

private:
    static void menuHandler(void* ctx) {
        SpiritboxEmulator* _this = (SpiritboxEmulator*)ctx;
        float menuWidth = ImGui::GetContentRegionAvail().x;

        if (_this->running) { ImGui::BeginDisabled(); }

        ImGui::LeftLabel("Start Frequency");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##start_freq", &_this->startFreq, 0, 0, "%.0f")) {
            _this->startFreq = std::max(0.0, _this->startFreq);
        }

        ImGui::LeftLabel("Stop Frequency");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##stop_freq", &_this->stopFreq, 0, 0, "%.0f")) {
            _this->stopFreq = std::max(_this->startFreq, _this->stopFreq);
        }

        ImGui::LeftLabel("Step Size");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputDouble("##step_size", &_this->stepSize, 0, 0, "%.0f")) {
            _this->stepSize = std::max(1.0, _this->stepSize);
        }

        ImGui::LeftLabel("Step Interval (ms)");
        ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
        if (ImGui::InputInt("##step_interval", &_this->stepIntervalMs, 10, 100)) {
            _this->stepIntervalMs = std::clamp<int>(_this->stepIntervalMs, 10, 10000);
        }

        ImGui::BeginTable(("spiritbox_mode_table" + _this->name).c_str(), 2);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Checkbox("Auto Repeat", &_this->autoRepeat);
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox("Random Mode", &_this->randomMode);
        ImGui::EndTable();

        if (_this->running) { ImGui::EndDisabled(); }

        if (!_this->running) {
            if (ImGui::Button("Start Spiritbox##spiritbox_start", ImVec2(menuWidth, 0))) {
                _this->start();
            }
            ImGui::Text("Status: Ready");
        }
        else {
            if (ImGui::Button("Stop Spiritbox##spiritbox_stop", ImVec2(menuWidth, 0))) {
                _this->stop();
            }
            ImGui::TextColored(ImVec4(0, 1, 1, 1), 
                _this->randomMode ? "Status: Scanning (Random)" : "Status: Scanning");

            // Display current frequency
            ImGui::Text("Current Frequency: %.3f MHz", _this->currentFreq / 1000000.0);
            
            if (!_this->randomMode) {
                double progress = (_this->currentFreq - _this->startFreq) / 
                                (_this->stopFreq - _this->startFreq);
                ImGui::ProgressBar(progress, ImVec2(menuWidth, 0));
            }

            if (_this->debugMode) {
                ImGui::Text("Scans Completed: %d", _this->stepsCompleted);
                ImGui::Text("Last Step Time: %d ms", _this->lastStepTimeMs);
                if (_this->randomMode) {
                    ImGui::Text("Last Jump Size: %.3f MHz", _this->lastJumpSize / 1000000.0);
                }
            }
        }

        if (ImGui::CollapsingHeader("Debug")) {
            ImGui::Checkbox("Debug Mode", &_this->debugMode);
        }
    }

    double getNextFrequency() {
        if (randomMode) {
            // Generate random frequency within range
            std::uniform_real_distribution<double> freqDist(startFreq, stopFreq);
            double nextFreq = freqDist(rng);
            
            // Round to nearest step size
            nextFreq = round(nextFreq / stepSize) * stepSize;
            
            // Calculate jump size for debug display
            lastJumpSize = std::abs(nextFreq - currentFreq);
            
            return std::clamp(nextFreq, startFreq, stopFreq);
        }
        else {
            // Sequential mode
            return currentFreq + stepSize;
        }
    }

    void start() {
        if (running) { return; }
        if (gui::waterfall.selectedVFO.empty()) { return; }
        
        currentFreq = startFreq;
        stepsCompleted = 0;
        lastStepTime = std::chrono::high_resolution_clock::now();
        lastJumpSize = 0.0;
        
        running = true;
        workerThread = std::thread(&SpiritboxEmulator::worker, this);
    }

    void stop() {
        if (!running) { return; }
        running = false;
        if (workerThread.joinable()) {
            workerThread.join();
        }
    }

    void worker() {
        while (running) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastStepTime).count();
            
            if (elapsed >= stepIntervalMs) {
                lastStepTimeMs = elapsed;
                lastStepTime = now;

                if (!gui::waterfall.selectedVFO.empty()) {
                    tuner::normalTuning(gui::waterfall.selectedVFO, currentFreq);
                }
                else {
                    running = false;
                    return;
                }

                double nextFreq = getNextFrequency();
                
                if (nextFreq > stopFreq) {
                    if (autoRepeat) {
                        currentFreq = startFreq;
                        if (!randomMode) {
                            stepsCompleted = 0;
                        }
                    }
                    else {
                        running = false;
                        return;
                    }
                }
                else {
                    currentFreq = nextFreq;
                    stepsCompleted++;
                }
            }

            // Small sleep to prevent CPU overload
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::string name;
    bool enabled = true;
    bool running = false;
    bool debugMode = false;
    
    // Frequency settings
    double startFreq = 9600000.0;
    double stopFreq = 10000000.0;
    double stepSize = 1000.0;
    double currentFreq = 9600000.0;
    int stepIntervalMs = 250;
    bool autoRepeat = false;
    bool randomMode = false;

    // Step control
    std::chrono::time_point<std::chrono::high_resolution_clock> lastStepTime;
    int lastStepTimeMs = 0;
    int stepsCompleted = 0;
    double lastJumpSize = 0.0;

    // Random number generator
    std::mt19937 rng;

    std::thread workerThread;
    std::mutex sweepMtx;
};

MOD_EXPORT void _INIT_() {
}

MOD_EXPORT ModuleManager::Instance* _CREATE_INSTANCE_(std::string name) {
    return new SpiritboxEmulator(name);
}

MOD_EXPORT void _DELETE_INSTANCE_(void* instance) {
    delete (SpiritboxEmulator*)instance;
}

MOD_EXPORT void _END_() {
}