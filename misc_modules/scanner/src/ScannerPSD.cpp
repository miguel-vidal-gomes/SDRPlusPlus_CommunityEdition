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
    m_powerSpectrum.resize(m_fftSize);
    m_avgPowerSpectrum.resize(m_fftSize);
    m_sampleBuffer.resize(m_fftSize * 2); // Extra room for overlap
    
    // Generate window function
    generateWindow();
    
    // Calculate smoothing parameter
    calculateAlpha();
    
    // Reset buffers - initialize to a very low but finite value
    std::fill(m_powerSpectrum.begin(), m_powerSpectrum.end(), -200.0f);
    std::fill(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end(), -200.0f);
    m_bufferOffset = 0;
    
    m_initialized = true;
    m_firstFrame = true; // Mark as first frame for proper initialization
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
    m_powerSpectrum.clear();
    m_avgPowerSpectrum.clear();
    m_sampleBuffer.clear();
    m_bufferOffset = 0;
    
    m_initialized = false;
}

bool ScannerPSD::feedSamples(const std::complex<float>* samples, int count) {
    if (!m_initialized) {
        flog::error("Scanner: Cannot feed samples - not initialized");
        return false;
    }
    
    if (!samples || count <= 0) {
        flog::error("Scanner: Invalid samples pointer or count: {}", count);
        return false;
    }
    
    // Debug logging for sample feeding
    static int sampleCounter = 0;
    sampleCounter += count;
    static int lastLoggedCount = 0;
    if (sampleCounter - lastLoggedCount > 100000) {  // Log every ~100k samples
        flog::info("Scanner: Fed {} samples to ScannerPSD (total: {})", count, sampleCounter);
        lastLoggedCount = sampleCounter;
    }
    
    bool newFrameReady = false;
    
    // Lock only during buffer manipulation
    {
        std::lock_guard<std::mutex> lck(m_mutex);
        
        // Copy samples to buffer
        int remainingSpace = m_sampleBuffer.size() - m_bufferOffset;
        int copyCount = std::min(count, remainingSpace);
        
        std::memcpy(&m_sampleBuffer[m_bufferOffset], samples, copyCount * sizeof(std::complex<float>));
        m_bufferOffset += copyCount;
    }
    
    // Process all available frames without holding the lock
    for (;;) {
        std::vector<std::complex<float>> frame;
        
        // Lock only to check/extract a frame
        {
            std::lock_guard<std::mutex> lck(m_mutex);
            
            // If we don't have enough samples for a frame, break
            if (m_bufferOffset < m_fftSize) break;
            
            // Copy one frame's worth of samples
            frame.assign(m_sampleBuffer.begin(), m_sampleBuffer.begin() + m_fftSize);
            
            // Advance buffer by hop size
            int remaining = m_bufferOffset - m_hopSize;
            if (remaining > 0) {
                std::memmove(&m_sampleBuffer[0], &m_sampleBuffer[m_hopSize],
                            remaining * sizeof(m_sampleBuffer[0]));
            }
            m_bufferOffset = remaining;
        }
        
        // Process the frame without holding the lock
        processFrame(frame);
        newFrameReady = true;
    }
    
    return newFrameReady;
}

bool ScannerPSD::process() {
    if (!m_initialized || m_bufferOffset < m_fftSize) {
        flog::error("Scanner: Cannot process FFT - initialized: {}, buffer offset: {} (need {})",
                           m_initialized ? "true" : "false", m_bufferOffset, m_fftSize);
        return false;
    }
    
    // Create a local copy of the buffer to process
    std::vector<std::complex<float>> frame(m_sampleBuffer.begin(), m_sampleBuffer.begin() + m_fftSize);
    
    // Process the frame
    return processFrame(frame);
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
    
    // Calculate and update power spectrum with mutex protection
    {
        std::lock_guard<std::mutex> lck(m_mutex);
        
        // Calculate power spectrum (shifted to center DC)
        for (int i = 0; i < m_fftSize; i++) {
            int binIdx = (i + m_fftSize/2) % m_fftSize;
            
            // Calculate power as |z|² and normalize by FFT size and window
            // Use proper normalization factor for power spectrum
            float power = std::norm(m_fftOut[i]) * m_psdScale;
            
            // Convert to dB with proper floor
            float powerDb = lin2db(power);
            
            // Store shifted result - initialize to -100 dB if we're just starting
            if (m_firstFrame) {
                m_avgPowerSpectrum[binIdx] = powerDb;
            } else {
                // Apply exponential moving average
                m_avgPowerSpectrum[binIdx] = (1.0f - m_alpha) * m_avgPowerSpectrum[binIdx] + m_alpha * powerDb;
            }
            
            // Store in power spectrum
            m_powerSpectrum[binIdx] = powerDb;
            
            // Debug output for first few bins and center bin
            if (i == 0 || i == m_fftSize/4 || i == m_fftSize/2 || i == 3*m_fftSize/4 || i == m_fftSize-1) {
                static int debugCounter = 0;
                if (++debugCounter % 100 == 0) {  // Limit debug output
                    flog::debug("Scanner: FFT bin {} (shifted to {}): power={:.6e}, powerDb={:.2f} dB", 
                              i, binIdx, power, powerDb);
                }
            }
        }
        
        // Debug power spectrum values
        static int debugCounter = 0;
        if (++debugCounter % 10 == 0) {  // Log every 10 FFTs
            float minVal = -100.0f, maxVal = -100.0f;
            if (!m_avgPowerSpectrum.empty()) {
                minVal = *std::min_element(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end());
                maxVal = *std::max_element(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end());
            }
            flog::info("Scanner: Power spectrum range [{:.1f}, {:.1f}] dB", minVal, maxVal);
        }
        
        m_firstFrame = false;
    }
    
    return true;
}

const std::vector<float>& ScannerPSD::getPowerSpectrum() const {
    return m_powerSpectrum;
}

const float* ScannerPSD::acquireLatestPSD(int& width) {
    // Use try_lock to avoid deadlocks
    if (!m_mutex.try_lock()) {
        flog::warn("Scanner: acquireLatestPSD mutex is already locked, skipping");
        width = 0;
        return nullptr;
    }
    
    if (!m_initialized) {
        flog::error("Scanner: acquireLatestPSD called but PSD not initialized");
        m_mutex.unlock();
        width = 0;
        return nullptr;
    }
    
    // Check if we have any data yet
    static int callCount = 0;
    if (++callCount % 10 == 0) {
        auto [mnIt, mxIt] = std::minmax_element(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end());
        flog::info("Scanner: acquireLatestPSD returning {} bins, range [{:.2f}, {:.2f}] dB", 
                  m_fftSize, *mnIt, *mxIt);
    }
    
    width = m_fftSize;
    return m_avgPowerSpectrum.data();
}

void ScannerPSD::releaseLatestPSD() {
    // Only unlock if we're the owner of the lock
    try {
        m_mutex.unlock();
    } catch (const std::system_error& e) {
        flog::error("Scanner: Failed to unlock mutex in releaseLatestPSD: {}", e.what());
    }
}

bool ScannerPSD::copyLatestPSD(std::vector<float>& out, int& width) const {
    std::lock_guard<std::mutex> lk(m_mutex);  // short lock, just for copy
    if (!m_initialized || m_avgPowerSpectrum.empty()) { 
        width = 0; 
        return false; 
    }
    out = m_avgPowerSpectrum; // copy snapshot
    width = m_fftSize;
    
    // Log PSD range info occasionally
    static int callCount = 0;
    if (++callCount % 10 == 0) {
        auto [mnIt, mxIt] = std::minmax_element(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end());
        flog::info("Scanner: copyLatestPSD returning {} bins, range [{:.2f}, {:.2f}] dB", 
                  m_fftSize, *mnIt, *mxIt);
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