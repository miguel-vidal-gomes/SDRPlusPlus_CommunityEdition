## 4. UI System (Deepest Dive)

The UI in SDR++CE is built entirely with the immediate-mode GUI library, ImGui. This choice has significant architectural implications: the UI is completely decoupled from the application state, redrawing from scratch every frame. State is stored within the core application logic and modules, not in the UI itself. This is a fundamental concept for anyone wishing to add UI elements.

### 4.1. UI-to-DSP Communication Patterns (Control)

Communicating user intent from the UI thread to the real-time DSP thread(s) must be done with extreme care to avoid priority inversion, race conditions, and audio glitches. SDR++CE employs several patterns for this.

#### 4.1.1. The "Manager as Mediator" Pattern

The primary method for the UI to affect the DSP is by calling methods on the global manager classes (`VFOManager`, `SourceManager`, etc.). These managers then act as mediators to safely update the underlying DSP objects.

**Example Walkthrough: Changing VFO Frequency**

1.  **UI Interaction:** The user clicks and drags a VFO on the waterfall. The `WaterFall::processInputs()` function in `gui/widgets/waterfall.cpp` detects this mouse event.
2.  **UI State Update:** It calculates the new frequency based on the mouse's pixel position relative to the displayed spectrum.
3.  **Manager Call:** It calls a method on the VFO object, which is a UI-side representation managed by the `VFOManager`: `vfo->setOffset(newOffset)`.
4.  **Manager as Mediator:** The `VFO::setOffset` method (in `signal_path/vfo_manager.cpp`) does two things:
    a. Updates the UI-side representation (`wtfVFO->setOffset(...)`) so the VFO is drawn in the new position on the next frame.
    b. **Calls into the DSP object:** `dspVFO->setOffset(newOffset)`.
5.  **Thread-Safe DSP Update:** The `dsp::channel::RxVFO::setOffset` method is designed to be thread-safe. It updates a `std::atomic<double>` that holds the frequency for the Numerically-Controlled Oscillator (NCO).

**Code Example (Conceptual):**

```cpp
// --- In UI Thread (e.g., vfo_manager.cpp) ---
void VFOManager::VFO::setOffset(double offset) {
    // ... update UI state (wtfVFO) ...
    dspVFO->setOffset(offset); // Call into the DSP object
}

// --- In DSP Object (e.g., dsp/channel/rx_vfo.h) ---
class RxVFO : public dsp::hier_block<dsp::complex_t, dsp::complex_t> {
public:
    void setOffset(double offset) {
        // Thread-safe update that can be called from UI thread
        lo.setFrequency(-offset, sampleRate);  // NCO uses negative frequency for down-conversion
    }

private:
    // The NCO (Phasor) block handles thread safety internally
    dsp::math::Phasor lo;  // Local oscillator
    
    // In Phasor class:
    void setFrequency(double freq, double sampleRate) {
        // Calculate phase increment
        double phaseInc = (2.0 * M_PI * freq) / sampleRate;
        // Atomic update - the run() method will pick this up
        _phaseInc.store(phaseInc);
    }
    
    std::atomic<double> _phaseInc;
};
```

This pattern of "fire-and-forget" updates from the UI thread to atomic variables that are then read by the DSP thread is the preferred method for frequent, low-latency control changes.

### 4.2. DSP-to-UI Communication (Visualization)

Getting high-throughput data like FFTs from the DSP thread to the UI thread requires a different approach to avoid blocking the real-time thread.

*   **The Challenge:** The DSP thread produces FFT data at a high, steady rate (e.g., 30 times per second). The UI thread consumes this data at a variable rate (the screen's refresh rate, which can fluctuate). A simple mutex would cause the DSP thread to block if the UI is slow, leading to audio dropouts.
*   **The Solution: A Mutex-Protected, Callback-Driven Buffer System:**
    1.  **GUI Owns Buffers:** The `gui::waterfall` object allocates and owns the memory for the FFT data. It maintains a circular buffer for waterfall lines.
    2.  **Function Pointers:** During initialization (`MainWindow::init`), the `IQFrontEnd` is given function pointers to `MainWindow::acquireFFTBuffer` and `MainWindow::releaseFFTBuffer`. These static methods forward calls to the global `gui::waterfall` instance.
    3.  **DSP Thread's Role (`IQFrontEnd::handler`):**
        a. After calculating a new FFT, it calls the `acquireFFTBuffer` function pointer.
        b. `gui::waterfall.getFFTBuffer()` is executed. This function:
           - Locks `buf_mtx` (a standard mutex)
           - Updates the circular buffer index for waterfall mode
           - Returns a pointer to the current FFT line buffer
        c. The DSP thread writes its FFT data directly into this buffer. **It never allocates memory itself.**
        d. The DSP thread then calls the `releaseFFTBuffer` function pointer.
        e. `gui::waterfall.pushFFT()` is executed. This function:
           - Locks `latestFFTMtx` to protect the display buffer
           - Performs zoom/decimation from raw FFT to display resolution
           - Updates the waterfall texture by shifting old data down
           - Unlocks and signals new data is available
    4.  **UI Thread's Role (`WaterFall::draw`):**
        a. When drawing, it locks `latestFFTMtx` to access the display buffer
        b. Renders the FFT trace and waterfall texture
        c. The mutex is only held during data access, not during OpenGL operations

This mechanism decouples the threads. The DSP thread's lock contention is minimal (just the time it takes to swap a pointer), and it never has to wait for the UI to finish drawing.

### 4.3. Developer Guide: Creating a Module with a UI

1.  **Register a Draw Function:** Your module needs a UI entry point. In your module's `postInit()` method, register a draw callback with the `Menu`.
    ```cpp
    // In MyModule::postInit()
    // This adds "My Module" to the side menu panel.
    gui::menu.registerEntry("My Module", MyModule::drawMenu, this);
    ```
    The third parameter, `this`, is a `void*` context that will be passed back to your static draw function.

2.  **Create a Static Draw Function:** The callback itself must be a static function, but it can cast the `void*` context back to your class type to access member variables.
    ```cpp
    // In MyModule.h
    class MyModule : public ModuleManager::Instance {
        // ...
    public:
        static void drawMenu(void* ctx);
    private:
        void _drawMenu(); // Instance method for the actual UI code
        float _mySliderValue = 0.5f;
    };

    // In MyModule.cpp
    void MyModule::drawMenu(void* ctx) {
        // Cast context back to the class instance
        ((MyModule*)ctx)->_drawMenu();
    }
    ```

3.  **Implement the UI Logic:** The instance method contains your ImGui code.
    ```cpp
    void MyModule::_drawMenu() {
        // SmGui namespace provides styled widgets
        if (SmGui::BeginMenu("My Awesome Module")) {
            SmGui::LeftLabel("Status:");
            SmGui::Text(running ? "Running" : "Stopped");
            
            // Standard SDR++CE button styling
            if (running) {
                if (SmGui::Button("Stop##mymodule")) {
                    stop();
                }
            }
            else {
                if (SmGui::Button("Start##mymodule")) {
                    start();
                }
            }
            
            // Configuration with proper locking pattern
            bool changed = false;
            
            // Slider with left-aligned label
            SmGui::LeftLabel("Gain:");
            if (SmGui::SliderFloatWithSteps("##mymodule_gain", &_gain, 0.0f, 100.0f, 1.0f, "%.1f dB")) {
                changed = true;
                // Apply to DSP immediately
                if (dspBlock) { dspBlock->setGain(_gain); }
            }
            
            // Combo box example
            SmGui::LeftLabel("Mode:");
            if (SmGui::Combo("##mymodule_mode", &_modeId, _modesTxt.c_str())) {
                changed = true;
                // Apply mode change
                setMode(_modeId);
            }
            
            // Save configuration if anything changed
            if (changed) {
                ConfigManager config;
                config.acquire();
                config.conf[_name]["gain"] = _gain;
                config.conf[_name]["mode"] = _modeId;
                config.release(true);
            }
            
            SmGui::EndMenu();
        }
    }
    ```

This pattern ensures that your module's UI is correctly integrated into the main window, manages its own state, and persists its settings in a thread-safe manner.

### 4.4. Common UI Patterns and Best Practices

#### 4.4.1. Widget IDs and Collisions

ImGui identifies widgets by their label. To avoid collisions:
- Use `##` to create invisible unique IDs: `"Label##unique_id"`
- Use `ImGui::PushID()/PopID()` for scoped IDs
- For modules, always prefix with module name: `"##mymodule_setting"`

#### 4.4.2. Thread Safety Rules

1. **Never call DSP methods that allocate memory from UI thread**
2. **Use atomic variables or thread-safe setters for DSP parameters**
3. **Configuration changes should be batched to minimize lock time**
4. **Never block the UI thread waiting for DSP operations**

#### 4.4.3. SmGui Style Guidelines

The `SmGui` namespace provides SDR++CE-styled widgets:
- `SmGui::BeginMenu()` / `SmGui::EndMenu()` for collapsible sections
- `SmGui::LeftLabel()` for aligned labels
- `SmGui::Button()` for styled buttons
- `SmGui::SliderFloatWithSteps()` for precise control
- Always use SmGui variants when available for consistent look

#### 4.4.4. Performance Considerations

1. **Conditional Rendering:** Only render visible UI elements
   ```cpp
   if (SmGui::BeginMenu("Section")) {
       // This code only runs if section is expanded
       expensiveCalculation();
       SmGui::EndMenu();
   }
   ```

2. **Cache Computed Values:** Don't recalculate every frame
   ```cpp
   // Bad: Calculates every frame
   ImGui::Text("Power: %.2f dBm", 10 * log10(power));
   
   // Good: Cache the result
   if (powerChanged) {
       powerDbm = 10 * log10(power);
       powerChanged = false;
   }
   ImGui::Text("Power: %.2f dBm", powerDbm);
   ```

3. **Minimize Config Access:** Read config once, write only on changes

#### 4.4.5. Debugging UI Issues

- Enable ImGui Demo Window: Shows all available widgets and their code
- Use `ImGui::GetIO().Framerate` to check UI performance
- `ImGui::IsItemHovered()` and tooltips for interactive help
- Check widget IDs with ImGui's ID Stack Tool

*For a deeper dive into the UI's internal structure, including the `MainWindow` orchestrator and the custom Waterfall widget, see the [UI Internals](./04a_ui_internals.md) document.*
