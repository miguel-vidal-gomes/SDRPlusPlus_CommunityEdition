// SCANNER MODULE INTEGRATION WITH DEDICATED FFT PATH
// This file contains code snippets to integrate the scanner module with the dedicated FFT path

// ===== STEP 1: ADD TO SCANNER MODULE CLASS DEFINITION =====

// Add these member variables to the ScannerModule class:
private:
    // Scanner FFT parameters
    int scannerFftSize = 8192;    // Default FFT size for scanner
    static constexpr int SCANNER_FFT_SIZES[] = {1024, 2048, 4096, 8192, 16384, 32768};
    static constexpr const char* SCANNER_FFT_SIZE_LABELS[] = {"1024", "2048", "4096", "8192", "16384", "32768"};
    static constexpr int SCANNER_FFT_SIZE_COUNT = 6;
    int scannerFftSizeIndex = 3;  // Default to 8192 (index 3)
    
    // Scanner FFT buffer
    float* scannerFftData = nullptr;
    int scannerFftWidth = 0;
    bool scannerFftDataAvailable = false;

// Add these callback methods to the ScannerModule class:
public:
    // Scanner FFT buffer acquisition callback
    static float* acquireScannerFFTBuffer(void* ctx) {
        ScannerModule* _this = (ScannerModule*)ctx;
        _this->scannerFftDataAvailable = true;
        return _this->scannerFftData;
    }
    
    // Scanner FFT buffer release callback
    static void releaseScannerFFTBuffer(void* ctx) {
        ScannerModule* _this = (ScannerModule*)ctx;
        _this->scannerFftDataAvailable = false;
    }

// ===== STEP 2: MODIFY SCANNER MODULE INITIALIZATION =====

// Add this to the ScannerModule::postInit method:
void ScannerModule::postInit() {
    // Register scanner FFT callbacks with IQFrontEnd
    // This connects the scanner's dedicated FFT path
    core::modComManager.callInterface("iq_frontend", 1, // 1 = Register Scanner FFT Callbacks
                                     &scannerFftSize, &acquireScannerFFTBuffer, 
                                     &releaseScannerFFTBuffer, this);
}

// ===== STEP 3: ADD UI CONTROL FOR SCANNER FFT SIZE =====

// Add this to the menuHandler function, in the "Scanner Parameters" section:
// (after the "Interval" slider and before the "Scan Rate" slider)
ImGui::LeftLabel("Scanner FFT Size");
ImGui::SetNextItemWidth(menuWidth - ImGui::GetCursorPosX());
if (ImGui::SliderInt("##scanner_fft_size", &_this->scannerFftSizeIndex, 0, 
                    _this->SCANNER_FFT_SIZE_COUNT - 1, 
                    _this->SCANNER_FFT_SIZE_LABELS[_this->scannerFftSizeIndex])) {
    _this->scannerFftSize = _this->SCANNER_FFT_SIZES[_this->scannerFftSizeIndex];
    
    // Update the FFT size in the IQFrontEnd
    core::modComManager.callInterface("iq_frontend", 2, // 2 = Set Scanner FFT Size
                                     &_this->scannerFftSize, nullptr);
    
    _this->saveConfig();
}
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("FFT size for the scanner's dedicated FFT path\n"
                     "Larger sizes provide better frequency resolution\n"
                     "but may increase CPU usage");
}

// ===== STEP 4: MODIFY SCANNER WORKER THREAD =====

// Replace the FFT acquisition code in the worker thread with this:
// (around line 1152-1177 in the worker function)

// Acquire FFT data from the dedicated scanner FFT path
scannerFftDataAvailable = false;
if (scannerFftData) {
    // Data is automatically filled by the IQFrontEnd via the callbacks
    // Just wait for it to be available (with timeout)
    int waitAttempts = 0;
    while (!scannerFftDataAvailable && waitAttempts < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waitAttempts++;
    }
    
    if (!scannerFftDataAvailable) {
        continue; // No FFT data available, try again
    }
    
    // Work with the FFT data directly - no need to copy
    // The data will remain valid until we call releaseScannerFFTBuffer
    float* data = scannerFftData;
    int dataWidth = scannerFftWidth;
    
    if (dataWidth <= 0) {
        continue; // Invalid data width
    }
    
    // No need for downsampling - the dedicated FFT is already sized appropriately
    
    // Get waterfall data for frequency mapping
    double wfCenter = gui::waterfall.getViewOffset() + gui::waterfall.getCenterFrequency();
    double wfWidth = gui::waterfall.getViewBandwidth();
    double wfStart = wfCenter - (wfWidth / 2.0);
    double wfEnd = wfCenter + (wfWidth / 2.0);
    
    // Continue with existing signal detection logic...
} else {
    // Allocate scanner FFT buffer if not already allocated
    scannerFftData = new float[scannerFftSize];
    scannerFftWidth = scannerFftSize;
}

// ===== STEP 5: UPDATE SAVECONFIG AND LOADCONFIG METHODS =====

// Add to the saveConfig method:
config.conf["scannerFftSize"] = scannerFftSize;

// Add to the loadConfig method:
scannerFftSize = config.conf.value("scannerFftSize", 8192);
// Find the matching FFT size index
for (int i = 0; i < SCANNER_FFT_SIZE_COUNT; i++) {
    if (SCANNER_FFT_SIZES[i] == scannerFftSize) {
        scannerFftSizeIndex = i;
        break;
    }
}

// ===== STEP 6: UPDATE DESTRUCTOR =====

// Add to the ~ScannerModule destructor:
if (scannerFftData) {
    delete[] scannerFftData;
    scannerFftData = nullptr;
}

// ===== STEP 7: UPDATE MODULE INITIALIZATION =====

// Add to the _INIT_() function:
def["scannerFftSize"] = 8192;  // Default scanner FFT size

// ===== STEP 8: ADD INTERFACE COMMANDS TO IQ_FRONTEND =====

// Add these interface commands to the IQFrontEnd class:
// 1. Register Scanner FFT Callbacks
// 2. Set Scanner FFT Size

// In IQFrontEnd::registerInterface():
REGISTER_INTERFACE_COMMAND(1, [](void* ctx, void* arg0, void* arg1, void* arg2, void* arg3) -> int {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;
    int* size = (int*)arg0;
    float* (*acquireCallback)(void*) = (float* (*)(void*))arg1;
    void (*releaseCallback)(void*) = (void (*)(void*))arg2;
    void* callbackCtx = arg3;
    
    // Update scanner FFT parameters
    _this->setScannerFFTSize(*size);
    _this->_acquireScannerFFTBuffer = acquireCallback;
    _this->_releaseScannerFFTBuffer = releaseCallback;
    _this->_scannerFftCtx = callbackCtx;
    
    return 0;
});

REGISTER_INTERFACE_COMMAND(2, [](void* ctx, void* arg0, void* arg1) -> int {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;
    int* size = (int*)arg0;
    
    // Update scanner FFT size
    _this->setScannerFFTSize(*size);
    
    return 0;
});
