# 4a. UI Internals & The Waterfall Widget (Deepest Dive)

This document explores the implementation details of the UI system, focusing on the main rendering orchestrator and the complex custom waterfall widget.

## 1. `MainWindow::draw()` - The UI Orchestrator

The `MainWindow::draw()` function in `core/src/gui/main_window.cpp` is the single entry point for rendering the entire user interface each frame. It's called from the backend's render loop at the display refresh rate (typically 60 FPS).

### 1.1. Execution Flow

1.  **State Synchronization:** The function begins with critical "glue logic" that bridges UI events to DSP actions:
    ```cpp
    // Example state sync pattern
    if (gui::waterfall.centerFreqMoved) {
        gui::waterfall.centerFreqMoved = false;
        // Convert UI state change to DSP command
        double newFreq = gui::waterfall.getCenterFrequency();
        if (vfo != NULL) {
            tuner::tune(tuningMode, selectedVFO, newFreq);
        }
        core::configManager.acquire();
        core::configManager.conf["frequency"] = newFreq;
        core::configManager.release(true);
    }
    ```

2.  **Window Setup:** Creates an invisible fullscreen ImGui window as the canvas:
    ```cpp
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(displayWidth, displayHeight));
    ImGui::Begin("Main", NULL, WINDOW_FLAGS);
    ```

3.  **Top Bar Rendering:** 
    - Menu button (hamburger icon)
    - Play/Stop button with conditional styling
    - Volume slider with dB display
    - Frequency selection widget (complex custom control)
    - SNR meter (if enabled)
    - Recording indicator

4.  **Layout Management:**
    - Uses ImGui columns for menu/waterfall split
    - Handles draggable divider between menu and waterfall
    - Saves layout state to config

5.  **Conditional Menu Rendering:**
    ```cpp
    if (showMenu) {
        ImGui::BeginChild("Menu", ImVec2(menuWidth, 0), false, CHILD_FLAGS);
        gui::menu.draw(firstMenuRender);
        firstMenuRender = false;
        ImGui::EndChild();
    }
    ```

6.  **Waterfall Widget:** The main display area
    ```cpp
    gui::waterfall.draw();
    ```

7.  **Global Hotkeys:** Processes keyboard shortcuts when waterfall has focus

## 2. The Menu System (`gui::menu`)

The side menu is not a static component. It's dynamically built based on the configuration and the loaded modules.

*   **Configuration (`config.json`):** The `menuElements` array in the configuration file defines the order and default visibility (`open` state) of each menu panel.
*   **Registration:** Core components and modules add their UI to the menu by calling `gui::menu.registerEntry("Panel Name", callback_function, context_pointer)`. This adds the panel to the menu's internal list.
*   **Drawing:** The `gui::menu.draw()` function iterates through the `menu.order` list (from the config) and, for each entry, calls the registered callback function. This allows the user to reorder the menu panels simply by editing the `config.json` file. It's a flexible system that allows modules to seamlessly integrate their UI into the main window.

## 3. Case Study: The Waterfall Widget Internals

The `ImGui::WaterFall` class in `gui/widgets/waterfall.cpp` is the most complex UI component in SDR++CE. It is a custom-drawn widget that doesn't use standard ImGui controls for its main display area.

### 3.1. Architecture Overview

```cpp
class WaterFall {
    // Display buffers
    float* latestFFT;        // Current FFT for line display (protected by latestFFTMtx)
    float* rawFFTs;          // Circular buffer of raw FFT data (protected by buf_mtx)
    uint32_t* waterfallFb;   // Waterfall pixel buffer
    
    // OpenGL resources
    GLuint textureId;        // Waterfall texture
    
    // Threading
    std::mutex buf_mtx;      // Protects rawFFTs buffer
    std::recursive_mutex latestFFTMtx;  // Protects display data
    
    // State
    int currentFFTLine;      // Current position in circular buffer
    int fftLines;            // Number of valid FFT lines
};
```

### 3.2. Custom Rendering with `DrawList`

The waterfall uses ImGui's low-level drawing API for performance:

#### FFT Display (`drawFFT()`)
```cpp
void drawFFT() {
    // Get the ImGui drawing context
    auto window = ImGui::GetCurrentWindow();
    auto draw_list = window->DrawList;
    
    // Draw frequency scale
    for (float freq : scaleFreqs) {
        float xPos = freqToPixel(freq);
        draw_list->AddLine(ImVec2(xPos, fftMin.y), 
                          ImVec2(xPos, fftMax.y), 
                          gridColor);
        draw_list->AddText(ImVec2(xPos, fftMax.y), 
                          textColor, 
                          formatFreq(freq).c_str());
    }
    
    // Draw FFT trace as connected lines
    float lastX = fftMin.x;
    float lastY = dbToPixel(latestFFT[0]);
    
    for (int i = 1; i < dataWidth; i++) {
        float x = fftMin.x + i;
        float y = dbToPixel(latestFFT[i]);
        draw_list->AddLine(ImVec2(lastX, lastY), 
                          ImVec2(x, y), 
                          fftColor, 2.0f);
        lastX = x; lastY = y;
    }
    
    // Fill area under FFT
    draw_list->AddConvexPolyFilled(fftPath, dataWidth, fftFillColor);
}
```

#### Waterfall Texture Management
- **Circular Buffer:** Raw FFT data stored in `rawFFTs[waterfallHeight][rawFFTSize]`
- **Texture Update:** When new FFT arrives:
  1. Shift waterfall framebuffer down by one line
  2. Convert new FFT line to pixels using colormap
  3. Update OpenGL texture with new data
- **Rendering:** Single `AddImage()` call draws entire waterfall

### 3.2. Input Processing (`processInputs`)

This function is a complex state machine that handles all user interaction with the waterfall.

*   **State Tracking:** It uses a set of boolean flags (`fftResizeSelect`, `freqScaleSelect`, `vfoBorderSelect`, etc.) to track what the user is currently dragging.
*   **Hit Detection:** When a mouse click occurs, it performs manual hit detection by checking if the mouse cursor is within the screen-space rectangle of various elements (the VFO body, its resizable borders, the frequency scale, the FFT resize handle).
*   **Stateful Dragging:** Once an element is "selected" for dragging, subsequent calls to `processInputs` (while the mouse button is held down) will apply the drag delta to the appropriate state variable (e.g., changing a VFO's bandwidth, the waterfall's view offset, or the FFT height).

### 3.3. The `WaterfallVFO` Data Model

A key design element is the `ImGui::WaterfallVFO` class. This is **not** the same as the `dsp::channel::RxVFO` class. It is a pure **UI-side data model** that mirrors the state of a DSP VFO.

#### Architecture
```cpp
class WaterfallVFO {
    // Frequency domain position
    double generalOffset;     // Actual VFO frequency offset
    double centerOffset;      // Center frequency (for center ref mode)
    double bandwidth;         // VFO filter bandwidth
    
    // Screen space coordinates (calculated each frame)
    ImVec2 rectMin, rectMax;  // VFO body rectangle
    ImVec2 lbwSelMin, lbwSelMax;  // Left bandwidth handle
    ImVec2 rbwSelMin, rbwSelMax;  // Right bandwidth handle
    
    // Interaction state
    bool leftClamped;         // VFO extends beyond left edge
    bool rightClamped;       // VFO extends beyond right edge
    
    // Events
    Event<double> onUserChangedBandwidth;
    
    // Visual properties
    ImU32 color;             // VFO color for drawing
};
```

#### Update Flow
1. **User drags VFO** → `processInputs()` detects mouse movement
2. **Update UI model** → `wtfVFO->setOffset(newOffset)`
3. **Notify VFOManager** → `vfo->wtfVFOChanged = true`
4. **VFOManager syncs** → Updates `dsp::channel::RxVFO` offset
5. **Next frame** → VFO drawn at new position immediately

### 3.4. Performance Optimizations

The waterfall widget employs several optimization techniques:

#### 1. Decimation for Display
```cpp
// From pushFFT() - decimates raw FFT to display width
void doZoom(int inStart, int inCount, int inSize, int outWidth, 
           float* in, float* out) {
    float factor = (float)inCount / (float)outWidth;
    for (int i = 0; i < outWidth; i++) {
        float pos = i * factor;
        int idx = inStart + (int)pos;
        // Linear interpolation for smooth display
        float a = in[idx];
        float b = in[idx + 1];
        float t = pos - (int)pos;
        out[i] = a + (b - a) * t;
    }
}
```

#### 2. Conditional Rendering
- Only visible VFOs are processed
- Grid lines outside view are skipped
- Text labels use level-of-detail (hide when too dense)

#### 3. Texture Caching
- Waterfall texture only updated when new data arrives
- Single texture for entire waterfall (not per-line)
- Hardware acceleration via OpenGL

#### 4. Event Batching
- Mouse movements accumulated per frame
- Config saves deferred until interaction ends
- Redraw flags minimize unnecessary updates

### 3.5. Advanced Features

#### Raw FFT Access
For modules like the scanner that need full-bandwidth FFT data:
```cpp
float* acquireRawFFT(int& width) {
    buf_mtx.lock();
    width = rawFFTSize;
    return &rawFFTs[currentFFTLine * rawFFTSize];
}

void releaseRawFFT() {
    buf_mtx.unlock();
}
```

#### Module Extensions
Modules can extend the waterfall display:
- `onFFTRedraw`: Draw overlays on the FFT
- `onWaterfallRedraw`: Draw on the waterfall
- `onInputProcess`: Handle custom mouse input

These callbacks enable features like:
- Frequency markers (frequency manager)
- Signal detection indicators (scanner)
- Custom measurement tools
