#pragma once

#include <complex>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstring>
#include <cmath>
#include <thread>
#include <condition_variable>

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
    
    // Safe PSD access methods
    
    // Copy latest PSD data to provided buffer (thread-safe, no lock held during copy)
    bool copyLatestPSD(std::vector<float>& out, int& width) const;
    
    // Get snapshot of latest PSD data (zero-copy, thread-safe)
    // Returns pointer valid until next processFrame()
    bool getLatestPSDSnapshot(const float*& data, int& width) {
        if (!m_initialized) return false;
        int readIdx = m_readBuffer.load(std::memory_order_acquire);
        data = m_psdBuffers[readIdx].data();
        width = m_fftSize;
        return true;
    }
    
    // DEPRECATED - Use copyLatestPSD() or getLatestPSDSnapshot() instead
    // These methods will be removed in a future version
    const std::vector<float>& getPowerSpectrum() const { 
        int readIdx = m_readBuffer.load(std::memory_order_acquire);
        return m_psdBuffers[readIdx];
    }
    const float* acquireLatestPSD(int& width);
    void releaseLatestPSD();
    
    void start();
    void stop();

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
    
    // Processing buffers (not protected by mutex)
    std::vector<std::complex<float>> m_fftIn;
    std::vector<std::complex<float>> m_fftOut;
    std::vector<float> m_window;
    
    // Triple-buffered PSD output
    std::array<std::vector<float>, 3> m_psdBuffers;  // Triple buffer for PSD data
    std::atomic<int> m_readBuffer{0};    // Buffer being read by consumers
    std::atomic<int> m_writeBuffer{1};   // Buffer being written by producer
    std::atomic<int> m_processBuffer{2}; // Buffer being processed (EMA)
    
    // Ring buffer for input samples
    std::vector<std::complex<float>> m_sampleBuffer;
    std::atomic<size_t> m_writePos{0};  // Current write position in ring buffer
    std::atomic<size_t> m_readPos{0};   // Current read position in ring buffer
    std::atomic<size_t> m_samplesAvailable{0};  // Number of valid samples in buffer
    
    // Frame extraction buffer
    std::vector<std::complex<float>> m_frameBuffer;
    
    // Multi-threading protection
    mutable std::recursive_mutex m_mutex;
    
    // Processing thread
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::condition_variable m_cv;
    std::mutex m_cvMutex;

    // State tracking
    bool m_initialized = false;
    std::atomic<bool> m_processing{false};
    bool m_firstFrame = true;   // Track first frame for proper initialization
    
    // Helper methods for ring buffer
    void writeToRingBuffer(const std::complex<float>* data, size_t count);
    bool readFromRingBuffer(std::vector<std::complex<float>>& frame, size_t count);
    
    // Generate window function
    void generateWindow();
    
    // Processing loop for the dedicated thread
    void processingLoop();

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

