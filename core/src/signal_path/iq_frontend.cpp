#include "iq_frontend.h"
#include "../dsp/window/blackman.h"
#include "../dsp/window/nuttall.h"
#include <utils/flog.h>
#include <gui/gui.h>
#include <core.h>

IQFrontEnd::~IQFrontEnd() {
    if (!_init) { return; }
    stop();
    dsp::buffer::free(fftWindowBuf);
    fftwf_destroy_plan(fftwPlan);
    fftwf_free(fftInBuf);
    fftwf_free(fftOutBuf);
    dsp::buffer::free(scannerFftWindowBuf);
    fftwf_destroy_plan(scannerFftwPlan);
    fftwf_free(scannerFftInBuf);
    fftwf_free(scannerFftOutBuf);
}

void IQFrontEnd::init(dsp::stream<dsp::complex_t>* in, double sampleRate, bool buffering, int decimRatio, bool dcBlocking, 
                      int fftSize, double fftRate, FFTWindow fftWindow, float* (*acquireFFTBuffer)(void* ctx), void (*releaseFFTBuffer)(void* ctx), void* fftCtx,
                      uint32_t scannerFftSize, double scannerFftRate, FFTWindow scannerFftWindow, float* (*acquireScannerFFTBuffer)(void* ctx), void (*releaseScannerFFTBuffer)(void* ctx), void* scannerFftCtx) {
    _sampleRate = sampleRate;
    _decimRatio = decimRatio;
    _fftSize = fftSize;
    _fftRate = fftRate;
    _fftWindow = fftWindow;
    _acquireFFTBuffer = acquireFFTBuffer;
    _releaseFFTBuffer = releaseFFTBuffer;
    _fftCtx = fftCtx;
    _scannerFftSize = scannerFftSize;
    _scannerFftRate = scannerFftRate;
    _scannerFftWindow = scannerFftWindow;
    _acquireScannerFFTBuffer = acquireScannerFFTBuffer;
    _releaseScannerFFTBuffer = releaseScannerFFTBuffer;
    _scannerFftCtx = scannerFftCtx;

    effectiveSr = _sampleRate / _decimRatio;

    inBuf.init(in);
    inBuf.bypass = !buffering;

    decim.init(NULL, _decimRatio);
    dcBlock.init(NULL, genDCBlockRate(effectiveSr));
    conjugate.init(NULL);

    preproc.init(&inBuf.out);
    preproc.addBlock(&decim, _decimRatio > 1);
    preproc.addBlock(&dcBlock, dcBlocking);
    preproc.addBlock(&conjugate, false); // TODO: Replace by parameter

    split.init(preproc.out);

    // TODO: Do something to avoid basically repeating this code twice
    int skip;
    genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
    reshape.init(&fftIn, _fftSize, skip);
    fftSink.init(&reshape.out, handler, this);

    genReshapeParams(effectiveSr, _scannerFftSize, _scannerFftRate, skip, _scannerNzFFTSize);
    scannerReshape.init(&scannerFftIn, _scannerFftSize, skip);
    scannerFftSink.init(&scannerReshape.out, scannerHandler, this);

    fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
    if (_fftWindow == FFTWindow::RECTANGULAR) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = 0; }
    }
    else if (_fftWindow == FFTWindow::BLACKMAN) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::blackman(i, _nzFFTSize); }
    }
    else if (_fftWindow == FFTWindow::NUTTALL) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::nuttall(i, _nzFFTSize); }
    }

    scannerFftWindowBuf = dsp::buffer::alloc<float>(_scannerNzFFTSize);
    if (_scannerFftWindow == FFTWindow::RECTANGULAR) {
        for (int i = 0; i < _scannerNzFFTSize; i++) { scannerFftWindowBuf[i] = 0; }
    }
    else if (_scannerFftWindow == FFTWindow::BLACKMAN) {
        for (int i = 0; i < _scannerNzFFTSize; i++) { scannerFftWindowBuf[i] = dsp::window::blackman(i, _scannerNzFFTSize); }
    }
    else if (_scannerFftWindow == FFTWindow::NUTTALL) {
        for (int i = 0; i < _scannerNzFFTSize; i++) { scannerFftWindowBuf[i] = dsp::window::nuttall(i, _scannerNzFFTSize); }
    }

    fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftwPlan = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

    scannerFftInBuf = (fftwf_complex*)fftwf_malloc(_scannerFftSize * sizeof(fftwf_complex));
    scannerFftOutBuf = (fftwf_complex*)fftwf_malloc(_scannerFftSize * sizeof(fftwf_complex));
    scannerFftwPlan = fftwf_plan_dft_1d(_scannerFftSize, scannerFftInBuf, scannerFftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(fftInBuf, _fftSize - _nzFFTSize, _nzFFTSize);
    dsp::buffer::clear(scannerFftInBuf, _scannerFftSize - _scannerNzFFTSize, _scannerNzFFTSize);

    split.bindStream(&fftIn);
    split.bindStream(&scannerFftIn);

    // Register interface
    registerInterface();

    _init = true;
}

void IQFrontEnd::setInput(dsp::stream<dsp::complex_t>* in) {
    inBuf.setInput(in);
}

void IQFrontEnd::setSampleRate(double sampleRate) {
    // Temp stop the necessary blocks
    dcBlock.tempStop();
    for (auto& [name, vfo] : vfos) {
        vfo->tempStop();
    }

    // Update the samplerate
    _sampleRate = sampleRate;
    effectiveSr = _sampleRate / _decimRatio;
    dcBlock.setRate(genDCBlockRate(effectiveSr));
    for (auto& [name, vfo] : vfos) {
        vfo->setInSamplerate(effectiveSr);
    }

    // Reconfigure the FFTs
    updateMainFFTPath();
    updateScannerFFTPath();

    // Restart blocks
    dcBlock.tempStart();
    for (auto& [name, vfo] : vfos) {
        vfo->tempStart();
    }
}

void IQFrontEnd::setBuffering(bool enabled) {
    inBuf.bypass = !enabled;
}

void IQFrontEnd::setDecimation(int ratio) {
    // Temp stop the decimator
    decim.tempStop();

    // Update the decimation ratio
    _decimRatio = ratio;
    if (_decimRatio > 1) { decim.setRatio(_decimRatio); }
    setSampleRate(_sampleRate);

    // Restart the decimator if it was running
    decim.tempStart();

    // Enable or disable in the chain
    preproc.setBlockEnabled(&decim, _decimRatio > 1, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });

    // Update the DSP sample rate (TODO: Find a way to get rid of this)
    core::setInputSampleRate(_sampleRate);
}

void IQFrontEnd::setDCBlocking(bool enabled) {
    preproc.setBlockEnabled(&dcBlock, enabled, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });
}

void IQFrontEnd::setInvertIQ(bool enabled) {
    preproc.setBlockEnabled(&conjugate, enabled, [=](dsp::stream<dsp::complex_t>* out){ split.setInput(out); });
}

void IQFrontEnd::bindIQStream(dsp::stream<dsp::complex_t>* stream) {
    split.bindStream(stream);
}

void IQFrontEnd::unbindIQStream(dsp::stream<dsp::complex_t>* stream) {
    split.unbindStream(stream);
}

dsp::channel::RxVFO* IQFrontEnd::addVFO(std::string name, double sampleRate, double bandwidth, double offset) {
    // Make sure no other VFO with that name already exists
    if (vfos.find(name) != vfos.end()) {
        flog::error("[IQFrontEnd] Tried to add VFO with existing name.");
        return NULL;
    }

    // Create VFO and its input stream
    dsp::stream<dsp::complex_t>* vfoIn = new dsp::stream<dsp::complex_t>;
    dsp::channel::RxVFO* vfo = new dsp::channel::RxVFO(vfoIn, effectiveSr, sampleRate, bandwidth, offset);

    // Register them
    vfoStreams[name] = vfoIn;
    vfos[name] = vfo;
    bindIQStream(vfoIn);

    // Start VFO
    vfo->start();

    return vfo;
}

void IQFrontEnd::removeVFO(std::string name) {
    // Make sure that a VFO with that name exists
    if (vfos.find(name) == vfos.end()) {
        flog::error("[IQFrontEnd] Tried to remove a VFO that doesn't exist.");
        return;
    }

    // Remove the VFO and stream from registry
    dsp::stream<dsp::complex_t>* vfoIn = vfoStreams[name];
    dsp::channel::RxVFO* vfo = vfos[name];

    // Stop the VFO
    vfo->stop();

    unbindIQStream(vfoIn);
    vfoStreams.erase(name);
    vfos.erase(name);

    // Delete the VFO and its input stream
    delete vfo;
    delete vfoIn;
}

void IQFrontEnd::setFFTSize(int size) {
    _fftSize = size;
    updateMainFFTPath(true);
}

void IQFrontEnd::setFFTRate(double rate) {
    _fftRate = rate;
    updateMainFFTPath();
}

void IQFrontEnd::setFFTWindow(FFTWindow fftWindow) {
    _fftWindow = fftWindow;
    updateMainFFTPath();
}

void IQFrontEnd::setScannerFFTSize(uint32_t size) {
    // Log the incoming size for debugging
    flog::info("IQFrontEnd: Setting scanner FFT size to {0}", size);
    
    // Safety check for reasonable FFT size limits
    if (size == 0 || size > 1048576) { // 1M max size as safety limit
        flog::error("IQFrontEnd: Invalid scanner FFT size {0}, limiting to 8192", size);
        _scannerFftSize = 8192; // Reset to a safe default
    } else {
        _scannerFftSize = size;
    }
    
    // Log the final size after validation
    flog::info("IQFrontEnd: Scanner FFT size set to {0}", _scannerFftSize);
    
    updateScannerFFTPath();
}

void IQFrontEnd::setScannerFFTRate(double rate) {
    _scannerFftRate = rate;
    updateScannerFFTPath();
}

void IQFrontEnd::setScannerFFTWindow(FFTWindow fftWindow) {
    _scannerFftWindow = fftWindow;
    updateScannerFFTPath();
}

void IQFrontEnd::registerInterface() {
    // Handle scanner FFT callbacks - using constant to avoid string mismatches
    flog::info("Registering IQFrontEnd interface: {0}", iq_interface::kIQFrontendIface);
    // Register early to make sure it's available when scanner module initializes
    // IMPORTANT: First parameter is the module name, second parameter is the interface name
    // The scanner looks for interface name "iq_frontend", so we use that as the interface name (2nd param)
    bool registered = core::modComManager.registerInterface("scanner_fft", iq_interface::kIQFrontendIface, 
        [](int code, void* in, void* out, void* ctx) {
            IQFrontEnd* _this = (IQFrontEnd*)ctx;
            
            switch (code) {
                case 0: { // Set Scanner FFT Size
                    // Input: size
                    uint32_t* size = (uint32_t*)in;
                    
                    // Log the pointer and value for debugging
                    flog::info("IQFrontEnd: Received scanner FFT size request with value {0}", *size);
                    
                    // Update scanner FFT size
                    _this->setScannerFFTSize(*size);
                    break;
                }
                case 1: { // Register Scanner FFT Callbacks
                    // Input: [0]=acquireCallback, [1]=releaseCallback, [2]=callbackCtx
                    void** args = (void**)in;
                    float* (*acquireCallback)(void*) = (float* (*)(void*))args[0];
                    void (*releaseCallback)(void*) = (void (*)(void*))args[1];
                    void* callbackCtx = args[2];
                    
                    // Update scanner FFT parameters
                    _this->_acquireScannerFFTBuffer = acquireCallback;
                    _this->_releaseScannerFFTBuffer = releaseCallback;
                    _this->_scannerFftCtx = callbackCtx;
                    break;
                }
            }
        }, 
        this
    );
    
    // Log registration status
    if (registered) {
        flog::info("Successfully registered IQFrontEnd interface: {0}", iq_interface::kIQFrontendIface);
    } else {
        flog::error("Failed to register IQFrontEnd interface: {0}", iq_interface::kIQFrontendIface);
    }
}

void IQFrontEnd::flushInputBuffer() {
    inBuf.flush();
}

void IQFrontEnd::start() {
    // Start input buffer
    inBuf.start();

    // Start pre-proc chain (automatically start all bound blocks)
    preproc.start();

    // Start IQ splitter
    split.start();

    // Start all VFOs
    for (auto& [name, vfo] : vfos) {
        vfo->start();
    }

    // Start FFT chain
    reshape.start();
    fftSink.start();
    scannerReshape.start();
    scannerFftSink.start();
}

void IQFrontEnd::stop() {
    // Stop input buffer
    inBuf.stop();

    // Stop pre-proc chain (automatically start all bound blocks)
    preproc.stop();

    // Stop IQ splitter
    split.stop();

    // Stop all VFOs
    for (auto& [name, vfo] : vfos) {
        vfo->stop();
    }

    // Stop FFT chain
    reshape.stop();
    fftSink.stop();
    scannerReshape.stop();
    scannerFftSink.stop();
}

double IQFrontEnd::getEffectiveSamplerate() {
    return effectiveSr;
}

void IQFrontEnd::handler(dsp::complex_t* data, int count, void* ctx) {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;

    // Apply window
    volk_32fc_32f_multiply_32fc((lv_32fc_t*)_this->fftInBuf, (lv_32fc_t*)data, _this->fftWindowBuf, _this->_nzFFTSize);

    // Execute FFT
    fftwf_execute(_this->fftwPlan);

    // Aquire buffer
    float* fftBuf = _this->_acquireFFTBuffer(_this->_fftCtx);

    // Convert the complex output of the FFT to dB amplitude
    if (fftBuf) {
        volk_32fc_s32f_power_spectrum_32f(fftBuf, (lv_32fc_t*)_this->fftOutBuf, _this->_fftSize, _this->_fftSize);
    }

    // Release buffer
    _this->_releaseFFTBuffer(_this->_fftCtx);
}

void IQFrontEnd::scannerHandler(dsp::complex_t* data, int count, void* ctx) {
    IQFrontEnd* _this = (IQFrontEnd*)ctx;
    
    // Safety checks for null pointers
    if (!_this || !data || !_this->scannerFftInBuf || !_this->scannerFftWindowBuf || 
        !_this->scannerFftOutBuf || !_this->scannerFftwPlan ||
        !_this->_acquireScannerFFTBuffer || !_this->_releaseScannerFFTBuffer) {
        flog::error("IQFrontEnd: scannerHandler called with null pointers");
        return;
    }

    try {
        // Apply window
        volk_32fc_32f_multiply_32fc((lv_32fc_t*)_this->scannerFftInBuf, (lv_32fc_t*)data, 
                                   _this->scannerFftWindowBuf, _this->_scannerNzFFTSize);

        // Execute FFT
        fftwf_execute(_this->scannerFftwPlan);

        // Acquire buffer
        float* fftBuf = _this->_acquireScannerFFTBuffer(_this->_scannerFftCtx);

        // Convert the complex output of the FFT to dB amplitude
        if (fftBuf) {
            volk_32fc_s32f_power_spectrum_32f(fftBuf, (lv_32fc_t*)_this->scannerFftOutBuf, 
                                            _this->_scannerFftSize, _this->_scannerFftSize);
            // Release buffer
            _this->_releaseScannerFFTBuffer(_this->_scannerFftCtx);
        }
    }
    catch (const std::exception& e) {
        flog::error("IQFrontEnd: Exception in scannerHandler: {0}", e.what());
    }
}

void IQFrontEnd::updateMainFFTPath(bool updateWaterfall) {
    // Temp stop branch
    reshape.tempStop();
    fftSink.tempStop();

    // Update reshaper settings
    int skip;
    genReshapeParams(effectiveSr, _fftSize, _fftRate, skip, _nzFFTSize);
    reshape.setKeep(_nzFFTSize);
    reshape.setSkip(skip);

    // Update window
    dsp::buffer::free(fftWindowBuf);
    fftWindowBuf = dsp::buffer::alloc<float>(_nzFFTSize);
    if (_fftWindow == FFTWindow::RECTANGULAR) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = 1.0f * ((i % 2) ? -1.0f : 1.0f); }
    }
    else if (_fftWindow == FFTWindow::BLACKMAN) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::blackman(i, _nzFFTSize) * ((i % 2) ? -1.0f : 1.0f); }
    }
    else if (_fftWindow == FFTWindow::NUTTALL) {
        for (int i = 0; i < _nzFFTSize; i++) { fftWindowBuf[i] = dsp::window::nuttall(i, _nzFFTSize) * ((i % 2) ? -1.0f : 1.0f); }
    }

    // Update FFT plan
    fftwf_free(fftInBuf);
    fftwf_free(fftOutBuf);
    fftInBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftOutBuf = (fftwf_complex*)fftwf_malloc(_fftSize * sizeof(fftwf_complex));
    fftwPlan = fftwf_plan_dft_1d(_fftSize, fftInBuf, fftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(fftInBuf, _fftSize - _nzFFTSize, _nzFFTSize);

    // Update waterfall (TODO: This is annoying, it makes this module non testable and will constantly clear the waterfall for any reason)
    if (updateWaterfall) { gui::waterfall.setRawFFTSize(_fftSize); }

    // Restart branch
    reshape.tempStart();
    fftSink.tempStart();
}

void IQFrontEnd::updateScannerFFTPath(bool updateWaterfall) {
    // Temp stop branch
    scannerReshape.tempStop();
    scannerFftSink.tempStop();

    // Update reshaper settings
    int skip;
    genReshapeParams(effectiveSr, _scannerFftSize, _scannerFftRate, skip, _scannerNzFFTSize);
    scannerReshape.setKeep(_scannerNzFFTSize);
    scannerReshape.setSkip(skip);

    // Update window
    dsp::buffer::free(scannerFftWindowBuf);
    scannerFftWindowBuf = dsp::buffer::alloc<float>(_scannerNzFFTSize);
    if (_scannerFftWindow == FFTWindow::RECTANGULAR) {
        for (int i = 0; i < _scannerNzFFTSize; i++) { scannerFftWindowBuf[i] = 1.0f * ((i % 2) ? -1.0f : 1.0f); }
    }
    else if (_scannerFftWindow == FFTWindow::BLACKMAN) {
        for (int i = 0; i < _scannerNzFFTSize; i++) { scannerFftWindowBuf[i] = dsp::window::blackman(i, _scannerNzFFTSize) * ((i % 2) ? -1.0f : 1.0f); }
    }
    else if (_scannerFftWindow == FFTWindow::NUTTALL) {
        for (int i = 0; i < _scannerNzFFTSize; i++) { scannerFftWindowBuf[i] = dsp::window::nuttall(i, _scannerNzFFTSize) * ((i % 2) ? -1.0f : 1.0f); }
    }

    // Update FFT plan
    fftwf_free(scannerFftInBuf);
    fftwf_free(scannerFftOutBuf);
    
    // Safety check for memory allocation
    try {
        scannerFftInBuf = (fftwf_complex*)fftwf_malloc(_scannerFftSize * sizeof(fftwf_complex));
        if (!scannerFftInBuf) {
            throw std::bad_alloc();
        }
        
        scannerFftOutBuf = (fftwf_complex*)fftwf_malloc(_scannerFftSize * sizeof(fftwf_complex));
        if (!scannerFftOutBuf) {
            fftwf_free(scannerFftInBuf);
            scannerFftInBuf = nullptr;
            throw std::bad_alloc();
        }
        
        scannerFftwPlan = fftwf_plan_dft_1d(_scannerFftSize, scannerFftInBuf, scannerFftOutBuf, FFTW_FORWARD, FFTW_ESTIMATE);
        if (!scannerFftwPlan) {
            fftwf_free(scannerFftInBuf);
            fftwf_free(scannerFftOutBuf);
            scannerFftInBuf = nullptr;
            scannerFftOutBuf = nullptr;
            flog::error("IQFrontEnd: Failed to create FFT plan for scanner");
            _scannerFftSize = 8192; // Attempt with smaller size next time
            return;
        }
    }
    catch (const std::exception& e) {
        flog::error("IQFrontEnd: Memory allocation failed for scanner FFT buffers of size {0}: {1}", _scannerFftSize, e.what());
        _scannerFftSize = 8192; // Attempt with smaller size next time
        return;
    }

    // Clear the rest of the FFT input buffer
    dsp::buffer::clear(scannerFftInBuf, _scannerFftSize - _scannerNzFFTSize, _scannerNzFFTSize);

    // Restart branch
    scannerReshape.tempStart();
    scannerFftSink.tempStart();
}