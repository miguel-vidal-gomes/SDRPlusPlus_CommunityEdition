## 3. Signal Path and DSP (Deepest Dive)

The DSP engine is the mathematical core of SDR++CE. It's designed for high performance and modularity, allowing complex signal processing chains to be constructed from simple, reusable blocks. The entire engine is built around a few key concepts in the `dsp::` namespace. A thorough understanding of these concepts is essential for creating or modifying any signal processing functionality.

### 3.1. Core DSP Concepts Explained

#### 3.1.1. `dsp::stream<T>` - The Thread-Safe Data Conduit

The `dsp::stream` is the fundamental component that connects DSP blocks. It provides thread-safe communication between producer and consumer threads using a double-buffering mechanism with mutex synchronization.

*   **Design and Purpose:** It is designed to safely pass blocks of data (like IQ or audio samples) from one thread to another. While it uses mutexes and condition variables (not lock-free), it minimizes contention through a clever double-buffering design where the producer and consumer work on separate buffers that are swapped atomically.
*   **How it Works:** 
    *   The stream maintains two buffers: `writeBuf` and `readBuf`
    *   The producer writes data to `writeBuf` then calls `swap()` to exchange buffers
    *   The consumer calls `read()` to wait for data, processes `readBuf`, then calls `flush()` to signal completion
    *   Synchronization uses mutexes but minimizes the critical section to just the buffer swap operation
    *   The `stopWriter()` and `stopReader()` methods provide clean shutdown semantics
*   **Usage Pattern:**
    ```cpp
    // Producer Thread (e.g., in a Source module)
    void worker() {
        while (running) {
            // Read data from hardware/file into stream's write buffer
            int count = readSDR(stream.writeBuf, BLOCK_SIZE);
            
            // Swap buffers - this will block if consumer hasn't processed yet
            if (!stream.swap(count)) {
                break; // Writer was stopped
            }
        }
    }

    // Consumer Thread (e.g., inside a dsp::block's run() method)
    int run() {
        // Wait for data - blocks until producer has swapped
        int count = in->read();
        if (count < 0) {
            return -1; // Reader was stopped
        }
        
        // Process the samples in readBuf
        processData(in->readBuf, out->writeBuf, count);
        
        // Flush input to signal we're done reading
        in->flush();
        
        // Swap output buffers to send processed data
        if (!out->swap(count)) {
            return -1;
        }
        
        return count;
    }
    ```

#### 3.1.2. `dsp::block` and `dsp::hier_block`

These are the base classes for all processing elements in the DSP chain.

*   **`dsp::block`:** The fundamental processing element that runs in its own thread. Key characteristics:
    *   Must implement the `run()` method which is called repeatedly by the worker thread
    *   The `run()` method should return the number of samples processed, or -1 to stop
    *   Manages input and output streams automatically
    *   Provides `start()` and `stop()` methods for thread lifecycle
    *   The base class handles thread creation/destruction in `doStart()` and `doStop()`
    *   Derived classes typically don't override `doStart()`/`doStop()` unless they need custom threading

*   **`dsp::hier_block` (Hierarchical Block):** A container for organizing multiple blocks without creating additional threads:
    *   Does NOT inherit from `dsp::block` - it's a separate base class
    *   Contains a vector of child blocks that are started/stopped together
    *   Child blocks run in their own threads independently
    *   Used to create logical groupings (like a complete demodulator) from multiple processing blocks
    *   Examples: `RxVFO` contains a mixer, NCO, and filter; demodulators contain multiple DSP stages
    *   The `registerBlock()` method adds child blocks to be managed

### 3.2. Data Flow and Threading In-Depth

```mermaid
graph TD
    subgraph Source Module Thread
        A[Hardware/Network Read] --> |out.write()| B(dsp::stream - Main IQ);
    end
    
    subgraph DSP Thread 1 (IQFrontEnd)
        B -->|inBuf.read()| C[dsp::buffer::SampleFrameBuffer];
        C --> D(Internal Ring Buffer);
        D -->|preproc.process()| E[dsp::chain - Preprocessing];
        E --> F[dsp::routing::Splitter];
    end

    F --> G(VFO 1 IQ Stream);
    F --> H(FFT IQ Stream);

    subgraph DSP Thread 1 (VFO)
        G --> I[dsp::channel::RxVFO];
        I --> J(VFO 1 IQ Output Stream);
    end
    
    subgraph DSP Thread 3 (Demodulator)
        J --> K[Demodulator hier_block];
        K --> L(VFO 1 Audio Stream);
    end
    
    subgraph DSP Thread 4 (Sink)
        L --> M[SignalPath Audio Mixer];
        M --> N[Sink Module];
    end
    
    subgraph DSP Thread 2 (FFT)
        H --> O[dsp::buffer::Reshaper];
        O --> P[dsp::sink::Handler];
        P -->|Callback via function pointer| Q[gui::waterfall];
    end

```
*Diagram Note: The threading is illustrative. Blocks connected by streams run in different threads. Blocks inside a `hier_block` or `chain` typically run in the same thread as their parent.*

### 3.3. `IQFrontEnd` - The DSP Core In-Depth

The `IQFrontEnd` class constructs and manages the primary DSP chain. It's not a hier_block itself but rather manages a collection of DSP blocks.

*   **`inBuf` (`dsp::buffer::SampleFrameBuffer`):** This block is critical for stability:
    *   Provides buffering between source modules and the DSP chain
    *   Can be enabled/disabled with `setBuffering()` 
    *   When enabled, adds latency but improves stability with bursty sources
    *   Contains its own thread that manages a large internal ring buffer

*   **`preproc` (`dsp::chain`):** A specialized container that runs multiple blocks in sequence within a single thread:
    1.  `decim` (`dsp::multirate::PowerDecimator`): Efficient decimation with built-in anti-aliasing
        *   Powers of 2 decimation only (2, 4, 8, 16, etc.)
        *   Uses optimized FIR filters for each stage
        *   Critical for reducing computational load from high-sample-rate sources
    2.  `conjugate` (`dsp::math::Conjugate`): Optional IQ inversion (swaps I and Q)
    3.  `dcBlock` (`dsp::correction::DCBlocker`): High-pass IIR filter
        *   Removes DC offset that can cause FFT artifacts
        *   Configurable cutoff frequency based on sample rate
        *   Can be enabled/disabled

*   **`split` (`dsp::routing::Splitter`):** Zero-copy stream distribution:
    *   Maintains a list of output streams
    *   Each `write()` operation writes the same data pointer to all outputs
    *   No memory allocation or copying in the data path
    *   Critical for feeding both FFT display and multiple VFOs efficiently

### 3.4. `dsp::channel::RxVFO` - The VFO Block In-Depth

This `hier_block` is responsible for selecting, tuning, and filtering a single signal of interest from the main IQ stream.

1.  **Mixing (`dsp::math::Multiply`):** The block contains an internal NCO (Numerically-Controlled Oscillator) implemented as a `dsp::math::Phasor`. The `work()` loop of the mixer block multiplies the incoming IQ stream sample-by-sample with the output of the Phasor. This complex multiplication performs a frequency shift, moving the desired signal from its original frequency down to 0 Hz (baseband). The frequency of the Phasor is controlled by the `setOffset` method, which is thread-safely called by the UI.
2.  **Filtering (`dsp::filter::DecimatingFIR`):** After mixing, the signal at 0 Hz needs to be isolated. This is done with a steep, efficient low-pass FIR filter. The filter's coefficients are chosen to have a cutoff frequency equal to half of the VFO's desired bandwidth.
3.  **Decimation:** Since the filter has removed all frequencies outside the desired bandwidth, the sample rate can now be lowered without violating the Nyquist theorem. The `DecimatingFIR` block does this as part of its operation, significantly reducing the data rate for the final demodulator stage.
4.  **Output:** The output of the `RxVFO` is a new, low-sample-rate IQ stream that contains only the tuned and filtered signal of interest.

### 3.5. FFT Processing and Waterfall Display

The FFT path is separate from the VFO processing and runs continuously for the spectrum display:

*   **`reshape` (`dsp::buffer::Reshaper`):** Converts the continuous IQ stream into fixed-size blocks for FFT:
    *   Configured based on FFT size and desired frame rate
    *   Can skip samples to achieve the target FFT rate without overwhelming the CPU
    *   Implements overlapping or non-overlapping FFT frames

*   **`fftSink` (`dsp::sink::Handler`):** Executes the FFT processing:
    *   Calls a handler function with each block of IQ data
    *   The handler (`IQFrontEnd::handler()`) performs:
        1. Windowing (Rectangular, Blackman, or Nuttall)
        2. FFT computation using FFTW3
        3. Magnitude calculation and conversion to dB
        4. Delivery to the waterfall display via callback

*   **FFT Threading:** The FFT runs in its own thread to prevent display updates from affecting DSP performance

### 3.6. Real-Time Constraints and Performance

The DSP system is designed with real-time audio processing in mind:

1. **Memory Allocation:**
   - All buffers are pre-allocated during initialization
   - No `malloc()`/`new` in the processing paths
   - Fixed-size stream buffers (default 1M samples)

2. **Thread Priorities:**
   - DSP threads run at higher priority than UI
   - Source threads may use real-time scheduling on supported platforms
   - FFT thread runs at lower priority than audio processing

3. **Latency Management:**
   - Each block adds one buffer's worth of latency
   - Typical audio latency: 20-50ms depending on buffer sizes
   - Trade-off between latency and CPU usage

4. **CPU Optimization:**
   - VOLK library for SIMD-optimized operations
   - Power-of-2 FFT sizes for FFTW efficiency
   - Decimation as early as possible in the chain

*For a deeper dive into the specific DSP blocks, demodulator integration, and audio mixing, see the [DSP Internals & Data Flow](./03a_dsp_internals.md) document.*

