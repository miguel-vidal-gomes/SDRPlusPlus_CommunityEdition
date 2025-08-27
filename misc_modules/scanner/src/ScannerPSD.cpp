#include "ScannerPSD.hpp"
#include <core.h>
#include <gui/gui.h>
#include <signal_path/signal_path.h>

namespace scanner {

// Constructor moved to header with initializers

ScannerPSD::~ScannerPSD() {
    reset();
}

void ScannerPSD::init(int fftSize, int sampleRate, WindowType windowType, float overlap, float avgTimeMs) {
    std::lock_guard<std::mutex> lck(m_mutex);
    
    // Validate input parameters
    if (fftSize <= 0 || sampleRate <= 0) {
        flog::error("Scanner: Invalid parameters for ScannerPSD: fftSize={}, sampleRate={}", fftSize, sampleRate);
        return;
    }
    
    double binWidth = (sampleRate > 0 && fftSize > 0) ? 
                      static_cast<double>(sampleRate) / static_cast<double>(fftSize) : 0.0;
    
    flog::info("Scanner: Initializing ScannerPSD with FFT size {}, sample rate {} Hz, window type {}, overlap {:.2f}, avg time {:.1f} ms, bin width {:.2f} Hz",
                      fftSize, sampleRate, static_cast<int>(windowType), overlap, avgTimeMs, binWidth);
    
    // Store parameters
    m_fftSize = fftSize;
    m_sampleRate = sampleRate;
    m_windowType = windowType;
    m_overlap = std::clamp(overlap, 0.0f, 0.99f); // Clamp overlap to valid range
    m_avgTimeMs = avgTimeMs;
    
    // Calculate hop size based on overlap
    m_hopSize = static_cast<int>(m_fftSize * (1.0f - m_overlap));
    if (m_hopSize < 1) m_hopSize = 1;
    
    // Create FFT engine using FFTW
    m_fftwIn = (fftwf_complex*)fftwf_malloc(m_fftSize * sizeof(fftwf_complex));
    m_fftwOut = (fftwf_complex*)fftwf_malloc(m_fftSize * sizeof(fftwf_complex));
    m_fftwPlan = fftwf_plan_dft_1d(m_fftSize, m_fftwIn, m_fftwOut, FFTW_FORWARD, FFTW_ESTIMATE);
    
    // Allocate buffers
    m_fftIn.resize(m_fftSize);
    m_fftOut.resize(m_fftSize);
    m_window.resize(m_fftSize);
    
    // Initialize ring buffer (4x FFT size to ensure space for overlap)
    m_sampleBuffer.resize(m_fftSize * 4);
    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);
    m_samplesAvailable.store(0, std::memory_order_relaxed);
    
    // Initialize frame buffer
    m_frameBuffer.resize(m_fftSize);
    
    // Initialize triple buffer
    for (auto& buf : m_psdBuffers) {
        buf.resize(m_fftSize, -200.0f);
    }
    
    // Generate window function
    generateWindow();
    
    // Calculate smoothing parameter
    calculateAlpha();
    
    m_initialized = true;
    m_firstFrame = true; // Mark as first frame for proper initialization
    m_processing.store(false, std::memory_order_release);
}

void ScannerPSD::reset() {
    std::lock_guard<std::mutex> lck(m_mutex);
    
    if (!m_initialized) return;
    
    if (m_fftwPlan) {
        fftwf_destroy_plan(m_fftwPlan);
        m_fftwPlan = nullptr;
    }
    if (m_fftwIn) {
        fftwf_free(m_fftwIn);
        m_fftwIn = nullptr;
    }
    if (m_fftwOut) {
        fftwf_free(m_fftwOut);
        m_fftwOut = nullptr;
    }
    m_fftIn.clear();
    m_fftOut.clear();
    m_window.clear();
    m_sampleBuffer.clear();
    m_frameBuffer.clear();
    for (auto& buf : m_psdBuffers) {
        buf.clear();
    }
    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);
    m_samplesAvailable.store(0, std::memory_order_relaxed);
    m_processing.store(false, std::memory_order_release);
    
    m_initialized = false;
}

void ScannerPSD::writeToRingBuffer(const std::complex<float>* data, size_t count) {
    size_t bufSize = m_sampleBuffer.size();
    size_t writePos = m_writePos.load(std::memory_order_relaxed);
    
    // First part: write until end of buffer or count
    size_t firstPart = std::min(count, bufSize - writePos);
    std::memcpy(&m_sampleBuffer[writePos], data, firstPart * sizeof(std::complex<float>));
    
    // Second part: wrap around if needed
    if (firstPart < count) {
        std::memcpy(&m_sampleBuffer[0], data + firstPart, 
                   (count - firstPart) * sizeof(std::complex<float>));
    }
    
    // Update write position
    m_writePos.store((writePos + count) % bufSize, std::memory_order_release);
    m_samplesAvailable.fetch_add(count, std::memory_order_release);
}

bool ScannerPSD::readFromRingBuffer(std::vector<std::complex<float>>& frame, size_t count) {
    if (m_samplesAvailable.load(std::memory_order_acquire) < count) {
        return false;
    }
    
    size_t bufSize = m_sampleBuffer.size();
    size_t readPos = m_readPos.load(std::memory_order_relaxed);
    
    frame.resize(count);
    
    // First part: read until end of buffer or count
    size_t firstPart = std::min(count, bufSize - readPos);
    std::memcpy(frame.data(), &m_sampleBuffer[readPos], 
                firstPart * sizeof(std::complex<float>));
    
    // Second part: wrap around if needed
    if (firstPart < count) {
        std::memcpy(frame.data() + firstPart, &m_sampleBuffer[0],
                   (count - firstPart) * sizeof(std::complex<float>));
    }
    
    // Update read position
    m_readPos.store((readPos + count) % bufSize, std::memory_order_release);
    m_samplesAvailable.fetch_sub(count, std::memory_order_release);
    
    return true;
}

bool ScannerPSD::feedSamples(const std::complex<float>* samples, int count) {
    if (!m_initialized || !samples || count <= 0) {
        return false;
    }
    
    // Debug logging (rate-limited)
    static int sampleCounter = 0;
    sampleCounter += count;
    static int lastLoggedCount = 0;
    if (sampleCounter - lastLoggedCount > 100000) {
        flog::info("Scanner: Fed {} samples to ScannerPSD (total: {})", count, sampleCounter);
        lastLoggedCount = sampleCounter;
    }
    
    bool newFrameReady = false;
    
    // Write new samples to ring buffer (no lock needed)
    writeToRingBuffer(samples, count);
    
    // Process frames while we have enough samples
    while (m_samplesAvailable.load(std::memory_order_acquire) >= m_fftSize) {
        // Extract frame from ring buffer (no lock needed)
        if (!readFromRingBuffer(m_frameBuffer, m_fftSize)) {
            break;
        }
        
        // Process the frame (this updates the power spectrum)
        processFrame(m_frameBuffer);
        newFrameReady = true;
        
        // Advance by hop size (discard samples we don't need)
        size_t skipCount = m_fftSize - m_hopSize;
        if (skipCount > 0) {
            m_readPos.fetch_add(skipCount, std::memory_order_release);
            m_samplesAvailable.fetch_sub(skipCount, std::memory_order_release);
        }
    }
    
    return newFrameReady;
}

bool ScannerPSD::process() {
    if (!m_initialized || m_samplesAvailable.load(std::memory_order_acquire) < m_fftSize) {
        flog::error("Scanner: Cannot process FFT - initialized: {}, samples available: {} (need {})",
                           m_initialized ? "true" : "false", 
                           static_cast<int>(m_samplesAvailable.load(std::memory_order_relaxed)),
                           m_fftSize);
        return false;
    }
    
    // Extract frame from ring buffer
    if (!readFromRingBuffer(m_frameBuffer, m_fftSize)) {
        return false;
    }
    
    // Process the frame
    return processFrame(m_frameBuffer);
}

bool ScannerPSD::processFrame(const std::vector<std::complex<float>>& frame) {
    if (!m_initialized || frame.size() < m_fftSize) {
        flog::error("Scanner: Cannot process frame - initialized: {}, frame size: {} (need {})",
                           m_initialized ? "true" : "false", static_cast<int>(frame.size()), m_fftSize);
        return false;
    }
    
    // Log FFT processing
    static int fftCounter = 0;
    if (++fftCounter % 10 == 0) {  // Log every 10 FFTs
        flog::info("Scanner: Processing FFT #{} (size: {}, sample rate: {} Hz, bin width: {:.2f} Hz)", 
                  fftCounter, m_fftSize, m_sampleRate, getBinWidthHz());
    }
    
    // Check for valid data in frame
    bool hasValidData = false;
    for (int i = 0; i < m_fftSize; i++) {
        if (std::abs(frame[i].real()) > 1e-6f || std::abs(frame[i].imag()) > 1e-6f) {
            hasValidData = true;
            break;
        }
    }
    
    if (!hasValidData) {
        static int warningCounter = 0;
        if (++warningCounter % 10 == 0) {
            flog::warn("Scanner: Frame contains no valid data");
        }
        return false;
    }
    
    // Apply window and copy to FFT input buffer
    for (int i = 0; i < m_fftSize; i++) {
        m_fftIn[i] = frame[i] * m_window[i];
    }
    
    // Copy input data to FFTW input buffer
    for (int i = 0; i < m_fftSize; i++) {
        m_fftwIn[i][0] = m_fftIn[i].real();
        m_fftwIn[i][1] = m_fftIn[i].imag();
    }
    
    // Execute FFTW
    fftwf_execute(m_fftwPlan);
    
    // Copy FFTW output to our output buffer
    for (int i = 0; i < m_fftSize; i++) {
        m_fftOut[i] = std::complex<float>(m_fftwOut[i][0], m_fftwOut[i][1]);
    }
    
    // Get current write buffer index
    int writeIdx = m_writeBuffer.load(std::memory_order_acquire);
    int processIdx = m_processBuffer.load(std::memory_order_acquire);
    
    // Ensure all buffers are properly sized
    for (auto& buf : m_psdBuffers) {
        if (buf.size() != m_fftSize) {
            buf.resize(m_fftSize, -200.0f);
        }
    }
    
    // Calculate power spectrum (shifted to center DC)
    for (int i = 0; i < m_fftSize; i++) {
        int binIdx = (i + m_fftSize/2) % m_fftSize;
        
        // Calculate power using VOLK for consistency with waterfall FFT
        float power = std::norm(m_fftOut[i]);
        
        // Convert to dB with proper scaling (same as waterfall)
        float powerDb = 10.0f * log10f(std::max(1e-15f, power)) - 20.0f * log10f(m_fftSize);
        
        // Store shifted result in write buffer
        m_psdBuffers[writeIdx][binIdx] = powerDb;
    }
    
    // Apply EMA smoothing
    if (m_firstFrame) {
        std::copy(m_psdBuffers[writeIdx].begin(), 
                 m_psdBuffers[writeIdx].end(), 
                 m_psdBuffers[processIdx].begin());
        m_firstFrame = false;
    } else {
        for (int i = 0; i < m_fftSize; i++) {
            m_psdBuffers[processIdx][i] = m_alpha * m_psdBuffers[writeIdx][i] + 
                                        (1.0f - m_alpha) * m_psdBuffers[processIdx][i];
        }
    }
    
    // Rate-limited logging of spectrum range
    static auto lastLogTime = std::chrono::steady_clock::now();
    static int logCounter = 0;
    ++logCounter;
    
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 1) {
        float minVal = -100.0f, maxVal = -100.0f;
        if (!m_psdBuffers[processIdx].empty()) {
            minVal = *std::min_element(m_psdBuffers[processIdx].begin(), m_psdBuffers[processIdx].end());
            maxVal = *std::max_element(m_psdBuffers[processIdx].begin(), m_psdBuffers[processIdx].end());
        }
        flog::info("Scanner: Processed {} FFTs, power range [{:.1f}, {:.1f}] dB", 
                  logCounter, minVal, maxVal);
        logCounter = 0;
        lastLogTime = now;
    }
    
    // Atomic buffer rotation (no mutex needed)
    int readIdx = m_readBuffer.load(std::memory_order_acquire);
    
    // Rotate buffers: write -> process -> read -> write
    m_writeBuffer.store((writeIdx + 1) % 3, std::memory_order_release);
    m_processBuffer.store((processIdx + 1) % 3, std::memory_order_release);
    m_readBuffer.store((readIdx + 1) % 3, std::memory_order_release);
    
    return true;
}

const float* ScannerPSD::acquireLatestPSD(int& width) {
    // DEPRECATED - Use copyLatestPSD() or getLatestPSDSnapshot() instead
    static bool warned = false;
    if (!warned) {
        flog::warn("Scanner: acquireLatestPSD is deprecated, use copyLatestPSD or getLatestPSDSnapshot instead");
        warned = true;
    }
    
    if (!m_initialized) {
        width = 0;
        return nullptr;
    }
    
    width = m_fftSize;
    int readIdx = m_readBuffer.load(std::memory_order_acquire);
    return m_psdBuffers[readIdx].data();
}

void ScannerPSD::releaseLatestPSD() {
    // DEPRECATED - No-op since we no longer use mutex for PSD access
    static bool warned = false;
    if (!warned) {
        flog::warn("Scanner: releaseLatestPSD is deprecated and no longer needed");
        warned = true;
    }
}

bool ScannerPSD::copyLatestPSD(std::vector<float>& out, int& width) const {
    if (!m_initialized) {
        width = 0;
        return false;
    }
    
    // Get latest data from read buffer (no locking needed)
    int readIdx = m_readBuffer.load(std::memory_order_acquire);
    out = m_psdBuffers[readIdx];
    width = m_fftSize;
    
    // Rate-limited logging
    static auto lastLogTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastLogTime).count() >= 1) {
        float minVal = -100.0f, maxVal = -100.0f;
        if (!out.empty()) {
            minVal = *std::min_element(out.begin(), out.end());
            maxVal = *std::max_element(out.begin(), out.end());
        }
        flog::info("Scanner: PSD range [{:.1f}, {:.1f}] dB", minVal, maxVal);
        lastLogTime = now;
    }
    
    return true;
}

double ScannerPSD::refineFrequencyHz(const std::vector<float>& PdB, int binIndex, double binWidthHz) {
    // Ensure we have a valid bin index with neighbors
    if (binIndex <= 0 || binIndex >= (int)PdB.size() - 1) {
        return binIndex * binWidthHz;
    }
    
    // Get power values for the peak and its neighbors
    double L = static_cast<double>(PdB[binIndex - 1]);
    double C = static_cast<double>(PdB[binIndex]);
    double R = static_cast<double>(PdB[binIndex + 1]);
    
    // Calculate the sub-bin offset using parabolic interpolation
    double num = 0.5 * (L - R);
    double den = (L - 2.0*C + R);
    if (std::abs(den) < 1e-6) den = 1e-6;  // Avoid division by small values
    double deltaBins = num / den;
    
    // Clamp to reasonable range
    if (deltaBins < -0.5) deltaBins = -0.5;
    if (deltaBins > 0.5) deltaBins = 0.5;
    
    // Calculate refined frequency
    double refinedBin = binIndex + deltaBins;
    double refinedHz = refinedBin * binWidthHz;
    
    return refinedHz;
}

void ScannerPSD::setFftSize(int size) {
    if (size <= 0) return;
    if (size == m_fftSize) return;
    
    // Re-initialize with new size
    init(size, m_sampleRate, m_windowType, m_overlap, m_avgTimeMs);
}

void ScannerPSD::setOverlap(float overlap) {
    if (overlap < 0.0f || overlap >= 1.0f) return;
    if (overlap == m_overlap) return;
    
    std::lock_guard<std::mutex> lck(m_mutex);
    
    m_overlap = overlap;
    
    // Recalculate hop size
    m_hopSize = static_cast<int>(m_fftSize * (1.0f - m_overlap));
    if (m_hopSize < 1) m_hopSize = 1;
    
    // Recalculate alpha
    calculateAlpha();
}

void ScannerPSD::setWindow(WindowType type) {
    if (type == m_windowType) return;
    
    std::lock_guard<std::mutex> lck(m_mutex);
    
    m_windowType = type;
    
    // Regenerate window
    generateWindow();
}

void ScannerPSD::setAverageTimeMs(float ms) {
    if (ms <= 0.0f) return;
    if (ms == m_avgTimeMs) return;
    
    std::lock_guard<std::mutex> lck(m_mutex);
    
    m_avgTimeMs = ms;
    
    // Recalculate alpha
    calculateAlpha();
}

void ScannerPSD::setSampleRate(int rate) {
    if (rate <= 0) return;
    if (rate == m_sampleRate) return;
    
    // Re-initialize with new sample rate
    init(m_fftSize, rate, m_windowType, m_overlap, m_avgTimeMs);
}

double ScannerPSD::getBinWidthHz() const {
    if (m_fftSize <= 0) return 0.0;
    return static_cast<double>(m_sampleRate) / static_cast<double>(m_fftSize);
}

void ScannerPSD::generateWindow() {
    if (m_window.size() != m_fftSize) {
        m_window.resize(m_fftSize);
    }
    
    switch (m_windowType) {
        case WindowType::RECTANGULAR:
            // Rectangular window (no windowing)
            std::fill(m_window.begin(), m_window.end(), 1.0f);
            break;
            
        case WindowType::BLACKMAN:
            // Blackman window
            for (int i = 0; i < m_fftSize; i++) {
                double ratio = (double)i / (double)(m_fftSize - 1);
                m_window[i] = 0.42 - 0.5 * cos(2.0 * M_PI * ratio) + 0.08 * cos(4.0 * M_PI * ratio);
            }
            break;
            
        case WindowType::BLACKMAN_HARRIS_7:
            // 7-term Blackman-Harris window (better dynamic range)
            for (int i = 0; i < m_fftSize; i++) {
                double ratio = (double)i / (double)(m_fftSize - 1);
                m_window[i] = 0.27105140069342f -
                             0.43329793923448f * cos(2.0 * M_PI * ratio) +
                             0.21812299954311f * cos(4.0 * M_PI * ratio) -
                             0.06592544638803f * cos(6.0 * M_PI * ratio) +
                             0.01081174209837f * cos(8.0 * M_PI * ratio) -
                             0.00077658482522f * cos(10.0 * M_PI * ratio) +
                             0.00001388721735f * cos(12.0 * M_PI * ratio);
            }
            break;
            
        case WindowType::HAMMING:
            // Hamming window
            for (int i = 0; i < m_fftSize; i++) {
                double ratio = (double)i / (double)(m_fftSize - 1);
                m_window[i] = 0.54 - 0.46 * cos(2.0 * M_PI * ratio);
            }
            break;
            
        case WindowType::HANN:
            // Hann window
            for (int i = 0; i < m_fftSize; i++) {
                double ratio = (double)i / (double)(m_fftSize - 1);
                m_window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * ratio));
            }
            break;
    }
    
    // Compute window power normalization
    double sumw2 = 0.0;
    for (int i = 0; i < m_fftSize; ++i) {
        sumw2 += double(m_window[i]) * double(m_window[i]);
    }
    m_windowU = float(sumw2 / double(m_fftSize));       // RMS window power
    m_psdScale = 1.0f / float(m_fftSize) / m_windowU;   // scale for |X|^2 → power/Hz-ish
    
    flog::info("Scanner: Window normalization factor: {:.6f} (window power: {:.6f})", 
              m_psdScale, m_windowU);
}

void ScannerPSD::calculateAlpha() {
    // Calculate alpha for exponential moving average based on time constant
    double hopRate = static_cast<double>(m_sampleRate) / 
                    (static_cast<double>(m_fftSize) * (1.0 - static_cast<double>(m_overlap)));
    double tauS = static_cast<double>(m_avgTimeMs) / 1000.0;
    m_alpha = static_cast<float>(1.0 - std::exp(-1.0 / (hopRate * tauS)));
    
    flog::info("Scanner: EMA alpha: {:.6f} (time constant: {:.1f} ms, hop rate: {:.1f} Hz)", 
              m_alpha, m_avgTimeMs, hopRate);
}

float getWindowValue(int n, int N, WindowType type) {
    double ratio = (double)n / (double)(N - 1);
    
    switch (type) {
        case WindowType::RECTANGULAR:
            return 1.0f;
            
        case WindowType::BLACKMAN:
            return 0.42 - 0.5 * cos(2.0 * M_PI * ratio) + 0.08 * cos(4.0 * M_PI * ratio);
            
        case WindowType::BLACKMAN_HARRIS_7:
            return 0.27105140069342f -
                   0.43329793923448f * cos(2.0 * M_PI * ratio) +
                   0.21812299954311f * cos(4.0 * M_PI * ratio) -
                   0.06592544638803f * cos(6.0 * M_PI * ratio) +
                   0.01081174209837f * cos(8.0 * M_PI * ratio) -
                   0.00077658482522f * cos(10.0 * M_PI * ratio) +
                   0.00001388721735f * cos(12.0 * M_PI * ratio);
                   
        case WindowType::HAMMING:
            return 0.54 - 0.46 * cos(2.0 * M_PI * ratio);
            
        case WindowType::HANN:
            return 0.5 * (1.0 - cos(2.0 * M_PI * ratio));
            
        default:
            return 1.0f;
    }
}

} // namespace scanner