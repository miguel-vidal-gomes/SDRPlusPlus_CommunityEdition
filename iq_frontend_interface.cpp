// IQ_FRONTEND INTERFACE COMMANDS
// This file contains the interface commands needed for the IQFrontEnd class

// Add these to the IQFrontEnd class in core/src/signal_path/iq_frontend.h:

// In the public section:
void registerInterface();

// Add this implementation to core/src/signal_path/iq_frontend.cpp:

void IQFrontEnd::registerInterface() {
    // Register the interface
    core::modComManager.registerInterface("iq_frontend", this);
    
    // Register Scanner FFT Callbacks command
    core::modComManager.registerInterfaceCommand("iq_frontend", 1, [](void* ctx, void* arg0, void* arg1, void* arg2, void* arg3) -> int {
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
    
    // Register Set Scanner FFT Size command
    core::modComManager.registerInterfaceCommand("iq_frontend", 2, [](void* ctx, void* arg0, void* arg1) -> int {
        IQFrontEnd* _this = (IQFrontEnd*)ctx;
        int* size = (int*)arg0;
        
        // Update scanner FFT size
        _this->setScannerFFTSize(*size);
        
        return 0;
    });
}

// Add this call to the IQFrontEnd constructor or init method:
registerInterface();
