#pragma once

#include <complex>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstring>
#include <cmath>

// Include FFT implementation
#include <fftw3.h>

namespace scanner {

// Window function types
enum class WindowType {
    RECTANGULAR,
    BLACKMAN,
    BLACKMAN_HARRIS_7,
    HAMMING,
    HANN
};

class ScannerPSD {
public:
    ScannerPSD() : m_fftwPlan(nullptr), m_fftwIn(nullptr), m_fftwOut(nullptr) {}
    ~ScannerPSD();

    // Initialize and configure the PSD engine
    void init(int fftSize, int sampleRate, WindowType windowType = WindowType::BLACKMAN_HARRIS_7, 
              float overlap = 0.5f, float avgTimeMs = 200.0f);
    
    // Reset the engine
    void reset();
    
    // Feed IQ samples and get a new power spectrum if available
    bool feedSamples(const std::complex<float>* samples, int count);
    
    // Process samples in buffer and generate power spectrum
    bool process();
    
    // Process a frame of samples without locking
    bool processFrame(const std::vector<std::complex<float>>& frame);
    
    // Get current power spectrum frame (dB)
    const std::vector<float>& getPowerSpectrum() const;
    
    // Get access to the latest power spectrum with mutex protection
    const float* acquireLatestPSD(int& width);
    void releaseLatestPSD();
    
    // Sub-bin interpolation for accurate peak detection
    static double refineFrequencyHz(const std::vector<float>& PdB, int binIndex, double binWidthHz);
    
    // Parameter getters and setters
    int getFftSize() const { return m_fftSize; }
    void setFftSize(int size);
    
    float getOverlap() const { return m_overlap; }
    void setOverlap(float overlap);
    
    WindowType getWindow() const { return m_windowType; }
    void setWindow(WindowType type);
    
    float getAverageTimeMs() const { return m_avgTimeMs; }
    void setAverageTimeMs(float ms);
    
    int getSampleRate() const { return m_sampleRate; }
    void setSampleRate(int rate);
    
    double getBinWidthHz() const;
    
private:
    // FFT and processing parameters
    int m_fftSize = 524288;    // Default to 524288 (matching the optimal size)
    int m_sampleRate = 0;
    float m_overlap = 0.5f;    // Default 50% overlap
    WindowType m_windowType = WindowType::BLACKMAN_HARRIS_7;
    float m_avgTimeMs = 200.0f;
    
    // Derived parameters
    int m_hopSize = 0;         // Hop size between FFTs (derived from overlap)
    
    // EMA smoothing parameter
    double m_alpha = 0.0;      // Calculated from avgTimeMs
    
    // PSD normalization factor
    float m_psdScale = 1.0f;   // Scale factor for power spectrum normalization
    float m_windowU = 1.0f;    // RMS window power
    
    // FFT engine
    fftwf_plan m_fftwPlan;
    fftwf_complex* m_fftwIn;
    fftwf_complex* m_fftwOut;
    
    // Buffers
    std::vector<std::complex<float>> m_fftIn;
    std::vector<std::complex<float>> m_fftOut;
    std::vector<float> m_window;
    std::vector<float> m_powerSpectrum;     // Power in dB
    std::vector<float> m_avgPowerSpectrum;  // Time-averaged power in dB
    
    // Input sample buffer
    std::vector<std::complex<float>> m_sampleBuffer;
    int m_bufferOffset = 0;
    
    // Multi-threading protection
    mutable std::mutex m_mutex;
    
    // State tracking
    bool m_initialized = false;
    std::atomic<bool> m_processing{false};
    bool m_firstFrame = true;   // Track first frame for proper initialization
    
    // Generate window function
    void generateWindow();
    
    // Calculate smoothing alpha from time constant
    void calculateAlpha();
    
    // Helper function to get window value
    float getWindowValue(int n, int N, WindowType type);
    
    // Helper function to convert linear power to dB with safety floor
    static inline float lin2db(float power) {
        constexpr float EPS = 1e-20f;
        return 10.0f * log10f(std::max(power, EPS));
    }
};

} // namespace scanner

