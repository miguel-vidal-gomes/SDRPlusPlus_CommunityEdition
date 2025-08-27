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
    
    // Reset buffers
    std::fill(m_powerSpectrum.begin(), m_powerSpectrum.end(), -100.0f);
    std::fill(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end(), -100.0f);
    m_bufferOffset = 0;
    
    m_initialized = true;
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
    if (!m_initialized || m_processing) {
        flog::error("Scanner: Cannot feed samples - initialized: {}, processing: {}", 
                           m_initialized ? "true" : "false", m_processing.load() ? "true" : "false");
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
    
    {
        std::lock_guard<std::mutex> lck(m_mutex);
        
        // Copy samples into buffer
        if (count + m_bufferOffset > m_sampleBuffer.size()) {
            // Buffer overflow - resize to accommodate
            m_sampleBuffer.resize(count + m_bufferOffset);
        }
        
        std::memcpy(&m_sampleBuffer[m_bufferOffset], samples, count * sizeof(std::complex<float>));
        m_bufferOffset += count;
        
        // Check if we have enough samples for FFT
        if (m_bufferOffset >= m_fftSize) {
            // Process a frame
            process();
            
            // Shift buffer by hop size
            int remaining = m_bufferOffset - m_hopSize;
            if (remaining > 0) {
                std::memmove(&m_sampleBuffer[0], &m_sampleBuffer[m_hopSize], 
                            remaining * sizeof(std::complex<float>));
            }
            m_bufferOffset = remaining;
            
            newFrameReady = true;
        }
    }
    
    return newFrameReady;
}

bool ScannerPSD::process() {
    if (!m_initialized || m_bufferOffset < m_fftSize) {
        flog::error("Scanner: Cannot process FFT - initialized: {}, buffer offset: {} (need {})",
                           m_initialized ? "true" : "false", m_bufferOffset, m_fftSize);
        return false;
    }
    
    // Log FFT processing
    static int fftCounter = 0;
    if (++fftCounter % 10 == 0) {  // Log every 10 FFTs
        flog::info("Scanner: Processing FFT #{} (size: {}, sample rate: {} Hz, bin width: {:.2f} Hz)", 
                  fftCounter, m_fftSize, m_sampleRate, getBinWidthHz());
    }
    
    m_processing = true;
    
    // Check for valid data in buffer
    bool hasValidData = false;
    for (int i = 0; i < m_fftSize; i++) {
        if (std::abs(m_sampleBuffer[i].real()) > 1e-6f || std::abs(m_sampleBuffer[i].imag()) > 1e-6f) {
            hasValidData = true;
            break;
        }
    }
    
    if (!hasValidData) {
        static int warningCounter = 0;
        if (++warningCounter % 10 == 0) {
            flog::warn("Scanner: Sample buffer contains no valid data");
        }
    }
    
    // Apply window and copy to FFT input buffer
    for (int i = 0; i < m_fftSize; i++) {
        m_fftIn[i] = m_sampleBuffer[i] * m_window[i];
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
    
    // Calculate power spectrum (shifted to center DC)
    for (int i = 0; i < m_fftSize; i++) {
        int binIdx = (i + m_fftSize/2) % m_fftSize;
        
        // Calculate power as |z|² and normalize by FFT size
        // Use proper normalization factor for power spectrum
        float power = std::norm(m_fftOut[i]) / (float)(m_fftSize * m_fftSize);
        
        // Convert to dB with proper floor
        constexpr float minPower = 1e-15f;
        constexpr float refPower = 1.0f;
        float powerDb = 10.0f * log10f(std::max(power, minPower) / refPower);
        
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
        float minVal = -100.0f, maxVal = -100.0f, avgVal = 0.0f;
        if (!m_powerSpectrum.empty()) {
            minVal = *std::min_element(m_powerSpectrum.begin(), m_powerSpectrum.end());
            maxVal = *std::max_element(m_powerSpectrum.begin(), m_powerSpectrum.end());
            avgVal = std::accumulate(m_powerSpectrum.begin(), m_powerSpectrum.end(), 0.0f) / m_powerSpectrum.size();
            flog::info("Scanner: Power spectrum range: [{:.1f}, {:.1f}] dB, avg: {:.1f} dB", 
                      minVal, maxVal, avgVal);
        }
    }
    
    // Apply time-domain averaging (EMA)
    for (int i = 0; i < m_fftSize; i++) {
        m_avgPowerSpectrum[i] = (1.0f - m_alpha) * m_avgPowerSpectrum[i] + m_alpha * m_powerSpectrum[i];
    }
    
    m_processing = false;
    return true;
}

const std::vector<float>& ScannerPSD::getPowerSpectrum() const {
    return m_avgPowerSpectrum;
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
        // Check if the power spectrum contains valid data
        bool hasValidData = false;
        float minVal = 0.0f, maxVal = -200.0f;
        
        if (!m_avgPowerSpectrum.empty()) {
            // Check for non-default values
            for (size_t i = 0; i < m_avgPowerSpectrum.size(); i++) {
                if (m_avgPowerSpectrum[i] != -100.0f) {
                    hasValidData = true;
                    break;
                }
            }
            
            if (hasValidData) {
                minVal = *std::min_element(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end());
                maxVal = *std::max_element(m_avgPowerSpectrum.begin(), m_avgPowerSpectrum.end());
                
                flog::info("Scanner: acquireLatestPSD returning {} samples, range [{:.1f}, {:.1f}] dB", 
                          m_fftSize, minVal, maxVal);
            } else {
                flog::warn("Scanner: acquireLatestPSD returning {} samples, but no valid data yet", m_fftSize);
            }
        } else {
            flog::error("Scanner: acquireLatestPSD called but power spectrum is empty");
        }
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

double ScannerPSD::refineFrequencyHz(const std::vector<float>& PdB, int binIndex, double binWidthHz) {
    // Ensure we have valid indices for parabolic interpolation
    if (binIndex <= 0 || binIndex >= PdB.size() - 1) {
        return binIndex * binWidthHz;
    }
    
    float L = PdB[binIndex-1];
    float C = PdB[binIndex];
    float R = PdB[binIndex+1];
    
    // Parabolic interpolation formula
    double num = 0.5 * (static_cast<double>(L) - static_cast<double>(R));
    double den = (static_cast<double>(L) - 2.0*static_cast<double>(C) + static_cast<double>(R));
    if (den < 1e-6) den = 1e-6;  // Avoid division by small values
    double deltaBins = num / den;
    // Clamp to range [-0.5, 0.5]
    if (deltaBins < -0.5) deltaBins = -0.5;
    if (deltaBins > 0.5) deltaBins = 0.5;
    
    // Return frequency with sub-bin precision
    return (binIndex + deltaBins) * binWidthHz;
}

void ScannerPSD::setFftSize(int size) {
    if (m_fftSize == size) return;
    
    // Reinitialize with new FFT size
    init(size, m_sampleRate, m_windowType, m_overlap, m_avgTimeMs);
}

void ScannerPSD::setOverlap(float overlap) {
    if (std::abs(m_overlap - overlap) < 0.001f) return;
    
    // Update overlap and recalculate hop size
    std::lock_guard<std::mutex> lck(m_mutex);
    m_overlap = std::clamp(overlap, 0.0f, 0.99f);
    m_hopSize = static_cast<int>(m_fftSize * (1.0f - m_overlap));
    if (m_hopSize < 1) m_hopSize = 1;
}

void ScannerPSD::setWindow(WindowType type) {
    if (m_windowType == type) return;
    
    // Update window type and regenerate window
    std::lock_guard<std::mutex> lck(m_mutex);
    m_windowType = type;
    generateWindow();
}

void ScannerPSD::setAverageTimeMs(float ms) {
    if (std::abs(m_avgTimeMs - ms) < 0.001f) return;
    
    // Update averaging time and recalculate alpha
    std::lock_guard<std::mutex> lck(m_mutex);
    m_avgTimeMs = ms;
    calculateAlpha();
}

void ScannerPSD::setSampleRate(int rate) {
    if (m_sampleRate == rate) return;
    
    // Update sample rate and recalculate alpha
    std::lock_guard<std::mutex> lck(m_mutex);
    m_sampleRate = rate;
    calculateAlpha();
}

double ScannerPSD::getBinWidthHz() const {
    if (m_sampleRate <= 0 || m_fftSize <= 0) return 0.0;
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
            
        default:
            // Default to Blackman-Harris
            for (int i = 0; i < m_fftSize; i++) {
                double ratio = (double)i / (double)(m_fftSize - 1);
                m_window[i] = 0.42 - 0.5 * cos(2.0 * M_PI * ratio) + 0.08 * cos(4.0 * M_PI * ratio);
            }
            break;
    }
    
    // Normalize window for proper scaling
    double sum = 0.0;
    for (int i = 0; i < m_fftSize; i++) {
        sum += m_window[i];
    }
    
    if (sum > 0.0) {
        double normFactor = m_fftSize / sum;
        for (int i = 0; i < m_fftSize; i++) {
            m_window[i] *= normFactor;
        }
    }
}

void ScannerPSD::calculateAlpha() {
    if (m_sampleRate <= 0 || m_fftSize <= 0 || m_hopSize <= 0) {
        m_alpha = 0.1f; // Default value if we don't have valid parameters
        return;
    }
    
    // Calculate frames per second based on sample rate, FFT size, and overlap
    double framesPerSec = static_cast<double>(m_sampleRate) / static_cast<double>(m_hopSize);
    
    // Convert time constant from ms to seconds
    double timeConstantSec = m_avgTimeMs / 1000.0;
    
    // Calculate alpha for EMA filter: alpha = 1 - exp(-1/(framesPerSec * timeConstant))
    m_alpha = 1.0 - exp(-1.0 / (framesPerSec * timeConstantSec));
    
    // Clamp to reasonable values
    m_alpha = std::clamp(m_alpha, 0.01, 1.0);
}

} // namespace scanner

