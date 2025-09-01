# 1a. Core Architecture Internals (Deepest Dive)

This document provides a granular, implementation-level analysis of the SDR++CE core systems. It is intended for developers who need to understand the fundamental bootstrap process, rendering loop, and module management logic.

## 1.1. The Backend Abstraction

The `backend` namespace is a hardware abstraction layer (HAL) for the operating system's windowing and graphics context. The implementation is selected at compile time and is different for each platform (e.g., `core/backends/glfw/backend.cpp` for desktops, `core/backends/android/backend.cpp` for Android). However, all backends implement the same interface defined in `core/src/backend.h`.

**Key `backend` Functions:**
*   `backend::init(resDir)`: Initializes the windowing system (e.g., GLFW), creates a window, and sets up the OpenGL graphics context.
*   `backend::renderLoop()`: Enters the main, blocking event loop for the application.
*   `backend::beginFrame()`: Prepares for a new frame of rendering (e.g., calling `ImGui::NewFrame()`).
*   `backend::render()`: Renders the ImGui draw data to the screen and swaps the graphics buffers.
*   `backend::end()`: Shuts down the windowing system and cleans up graphics resources.

## 1.2. Application Bootstrap In-Depth (`sdrpp_main`)

The application entry point in `core/src/core.cpp` is a sequence of critical initialization steps. Here is a more detailed breakdown:

1.  **Platform-Specific Setup:**
    *   On macOS bundles (`IS_MACOS_BUNDLE`), the process's working directory is changed to the `Contents/` directory of the `.app` bundle. This is essential for relative paths to plugins and resources to work correctly.
    *   On Windows, if the console is not explicitly requested (`--con`), `FreeConsole()` is called to detach the process from any command-line shell, making it a purely GUI application. `SetErrorMode()` is also used to prevent system-level crash dialogs from appearing.

2.  **Configuration and Filesystem:**
    *   The `--root` argument is processed. If the specified directory doesn't exist, it is created. This root directory is the foundation for finding `config.json`, modules, and other resources.
    *   The `defConfig` JSON object is constructed in memory. This is a hardcoded, comprehensive schema of every single configuration key the application recognizes.
    *   `core::configManager.load(defConfig)` is called. This function performs a critical "config repair" operation:
        *   It first loads `config.json` from the root path.
        *   It then iterates through `defConfig`. If any key from the schema is missing in the loaded config, it's added with its default value.
        *   Next, it iterates through the *loaded* config. If any key is found that does *not* exist in `defConfig`, it is erased. This prevents outdated or typo-ridden keys from persisting across versions.

3.  **GUI and Backend Initialization:**
    *   `backend::init(resDir)`: Initializes the window and OpenGL context.
    *   `SmGui::init(false)`: Initializes the ImGui library context.
    *   `style::loadFonts(resDir)` and `thememenu::init(resDir)`: Load fonts and UI theme information.
    *   `icons::load(resDir)`: Loads icon image files into OpenGL textures.
    *   `gui::mainWindow.init()`: **This is a major initialization step.** It:
        *   Initializes the waterfall display with the configured FFT size
        *   **Creates FFTW plans**: Allocates FFT input/output buffers and creates the forward FFT plan (line 87-89)
        *   Initializes the IQ frontend with FFT processing callbacks
        *   Sets up VFO event handlers
        *   Discovers and loads all modules from the configured directory
        *   Creates module instances based on the configuration

*For a deeper dive into the platform-specific backends (GLFW vs. Android), see the [Backend Architecture](./01b_backend_architecture.md) document.*

## 1.3. The Main Window and Render Loop (`MainWindow` & `backend`)

The `MainWindow` class (`core/src/gui/main_window.cpp`) is the central object for the entire UI. It orchestrates all the individual UI components.

**The Render Loop (from `core/backends/glfw/backend.cpp`):**

The GLFW backend provides a classic, straightforward render loop.

```cpp
int backend::renderLoop() {
    // Main loop runs until the user closes the window
    while (!glfwWindowShouldClose(window)) {
        // Check for OS events (mouse moves, key presses, etc.)
        glfwPollEvents();

        // Prepare ImGui for a new frame
        beginFrame(); // Calls ImGui_ImplOpenGL3_NewFrame, etc.

        // Update window state from config (maximized, etc.)
        // and handle F11 fullscreen toggle.
        // ...

        // Set the size and position for the main window content
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(_winWidth, _winHeight));
        
        // RENDER THE ENTIRE SDR++CE UI
        gui::mainWindow.draw();

        // Render the ImGui draw data to the back buffer and swap
        render();
    }
    return 0;
}
```
**`MainWindow::draw()` - The UI Orchestrator:**

This massive function is called once per frame from the render loop. It is responsible for drawing every part of the main UI:
1.  **Top Bar:** Draws the menu button, play/stop buttons, volume slider, frequency display, and other top-level controls.
2.  **State Synchronization:** It contains the "glue logic" that connects the UI to the backend. For example, it checks `if (gui::waterfall.centerFreqMoved)` and, if true, calls `sigpath::sourceManager.tune(...)` to command the selected source module to retune.
3.  **Columns Layout:** It sets up the main ImGui columns for the side menu and the waterfall display.
4.  **Menu Drawing:** If the menu is shown, it calls `gui::menu.draw()`. This function iterates through all registered menu entries (from modules and the core) and calls their respective draw functions.
5.  **Waterfall Drawing:** It calls `gui::waterfall.draw()`, which renders the main spectrum and waterfall display.
6.  **Dialogs:** It handles showing pop-up dialogs like the "About" screen (`credits::show()`).

## 1.4. Module Manager Internals (`ModuleManager`)

The `ModuleManager` (`core/src/module.cpp`) is the engine for the plugin system.

**Loading Process (`loadModule`):**

1.  **Dynamic Loading:** It uses platform-specific functions (`LoadLibraryA` on Windows, `dlopen` on POSIX) to load the shared library file into the process's address space.
    *   On Windows, if loading fails, `GetLastError()` provides the error code
    *   On POSIX, `dlerror()` provides a human-readable error message
    *   Android requires relative paths; absolute paths are not validated

2.  **Symbol Resolution:** It then uses `GetProcAddress` / `dlsym` to resolve the addresses of the five required exported symbols:
    *   `_INFO_`: Module metadata (name, description, version, max instances)
    *   `_INIT_`: Module initialization function
    *   `_CREATE_INSTANCE_`: Factory function for creating module instances
    *   `_DELETE_INSTANCE_`: Destructor function for module instances  
    *   `_END_`: Module cleanup function
    
    **Error Handling:** If any symbol is missing, the module is rejected and not loaded. Each missing symbol generates a specific error log.

3.  **Duplicate Check:** The loader verifies that no module with the same name is already loaded. It also checks if the same library handle is already loaded (preventing double-loading).

4.  **Registration:** If all checks pass, it calls the module's `init()` function, allowing the module to perform one-time initialization.

5.  **Storage:** The module's handle and its function pointers are stored in a `Module_t` struct, which is placed into the `modules` map, keyed by the module's name from its `_INFO_` struct.

**Instantiation Process (`createInstance`):**

1.  It looks up the module by name in the `modules` map.
2.  It checks if creating a new instance would exceed the `maxInstances` limit specified in the module's `_INFO_`.
3.  It calls the resolved `createInstance` function pointer, passing the desired instance name.
4.  The returned `ModuleManager::Instance*` pointer is stored along with the original `Module_t` in an `Instance_t` struct. This is placed in the `instances` map, keyed by the unique instance name.
5.  It emits the `onInstanceCreated` event.

This two-step load-then-instantiate process, orchestrated by the `MainWindow::init` function, ensures that all modules are loaded and ready before any inter-module communication is attempted during the `postInit` phase.

**The Critical Post-Init Phase (`doPostInitAll`):**

The post-initialization phase is where modules can safely interact with each other and the core systems:

1.  **Timing:** Called after ALL modules are loaded and ALL instances are created
2.  **Purpose:** Allows modules to:
    *   Register with global managers (SourceManager, SinkManager, etc.)
    *   Set up inter-module communication endpoints via ModuleComManager
    *   Subscribe to events from other modules
    *   Perform initialization that depends on other modules being present
3.  **Order:** Modules are post-initialized in the order they appear in the `instances` map
4.  **Safety:** At this point, all core systems are fully initialized, making it safe to:
    *   Access the VFO manager
    *   Register menu entries
    *   Set up DSP chains
    *   Access the configuration system

**Common Module Loading Failures:**

1.  **Missing Dependencies:** Module's shared library dependencies not found
2.  **Symbol Conflicts:** Two modules exporting the same symbol names
3.  **ABI Mismatch:** Module compiled with incompatible compiler or SDK version
4.  **Missing Exports:** Module doesn't export all required symbols
5.  **Init Failure:** Module's `_INIT_` function returns an error
