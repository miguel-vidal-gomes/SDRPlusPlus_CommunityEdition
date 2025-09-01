# 1b. Backend Architecture (Deepest Dive)

This document provides a detailed analysis of the `backend` namespace, which serves as the Hardware Abstraction Layer (HAL) for windowing, graphics context, and user input across different platforms.

## 1. The Backend Interface (`core/src/backend.h`)

The backend interface is a simple C++ namespace defining a contract that any platform-specific implementation must adhere to. This ensures that the core application (`sdrpp_main`) can initialize the GUI and run the render loop without any platform-specific code.

**Key Interface Functions:**
*   `int init(std::string resDir)`: Initializes the window, graphics context (OpenGL/EGL), and input handlers.
*   `void beginFrame()`: Prepares the graphics backend and ImGui for a new frame.
*   `void render(bool vsync)`: Renders the ImGui draw data and swaps the graphics buffers.
*   `int renderLoop()`: Enters the main, blocking event loop that continuously processes input and calls the drawing functions.
*   `int end()`: Cleans up all platform-specific resources.
*   `getMouseScreenPos()` / `setMouseScreenPos()`: Utility functions for mouse cursor control.

## 2. Implementations: GLFW vs. Android

SDR++CE has two primary backend implementations, which handle vastly different application lifecycles.

```mermaid
graph TD
    subgraph Core Application
        A[sdrpp_main] --> B{backend::init};
        B --> C{backend::renderLoop};
        C --> D{gui::mainWindow.draw};
        C --> E{backend::end};
    end

    subgraph "Desktop (core/backends/glfw/backend.cpp)"
        B --> G[glfwInit];
        G --> H[glfwCreateWindow];
        H --> I[ImGui_ImplGlfw_Init];
        C --> J[while !glfwWindowShouldClose];
        J --> K[glfwPollEvents];
        J --> D;
        E --> L[glfwTerminate];
    end

    subgraph "Android (core/backends/android/backend.cpp)"
        M[android_main] --> A;
        B --> N[eglGetDisplay];
        N --> O[eglInitialize];
        O --> P[eglCreateWindowSurface];
        P --> Q[ImGui_ImplAndroid_Init];
        C --> R[while(true) loop];
        R --> S[ALooper_pollAll];
        S -- APP_CMD_TERM_WINDOW --> T{Pause Rendering};
        S -- Event --> D;
        E --> U[eglTerminate];
    end
```

### 2.1. GLFW Backend (Desktops: Windows, Linux, macOS)

The GLFW backend is a traditional, straightforward implementation for desktop operating systems.

**Initialization (`backend::init`):**
1.  **Config Loading:** Window size, maximized, and fullscreen states are loaded from `config.json`.
2.  **GLFW Initialization:** `glfwInit()` is called, and an error callback is set.
3.  **OpenGL Context Probing:** 
    *   On platforms other than macOS, the code iterates through a list of potential OpenGL and OpenGL ES versions (from 4.6 down to 3.0, then ES 3.0), attempting to create a window with each one. This provides robust support for a wide range of graphics drivers and hardware.
    *   On macOS, it specifically requests OpenGL 3.2 Core Profile with forward compatibility:
        ```cpp
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        ```
    *   This macOS-specific configuration is critical when built with `USE_BUNDLE_DEFAULTS=ON`
4.  **Window Creation:** `glfwCreateWindow()` creates the main application window with the configured size.
5.  **Icon Loading:** The application icon is loaded from a PNG file using `stb_image` and resized to multiple standard dimensions (16x16, 32x32, 48x48, 64x64, 128x128, 256x256) using `stb_image_resize`. This multi-resolution approach ensures proper icon display across different contexts (window frame, taskbar, alt-tab switcher).
6.  **ImGui Initialization:** `ImGui_ImplGlfw_InitForOpenGL()` and `ImGui_ImplOpenGL3_Init()` are called to bind ImGui to the GLFW window and the OpenGL context.

**Render Loop (`backend::renderLoop`):**
*   The loop is a simple `while (!glfwWindowShouldClose(window))` loop. This is a standard pattern where the loop continues until the user clicks the close button or presses Alt+F4.
*   **Event Handling:** `glfwPollEvents()` is called at the beginning of each frame. This function processes all pending OS events, such as mouse movement, keyboard presses, and window resize events, and calls the appropriate callbacks that were registered by ImGui and the application.
*   **State Management:** The loop contains logic to detect changes in window size or fullscreen state (`F11` key), saving these changes back to the configuration file.
*   **Drawing:** It calls `gui::mainWindow.draw()` to render the entire UI, followed by `backend::render()` to swap the graphics buffers.

### 2.2. Android Backend

The Android backend is significantly more complex due to the Android application lifecycle, which is managed by the operating system. The application can be started, stopped, paused, and resumed at any time.

**Entry Point (`android_main`):**
*   The true entry point is `android_main`, which is called by the Android OS. This function sets up callbacks (`onAppCmd`, `onInputEvent`) to handle lifecycle and input events.
*   It then calls the standard `sdrpp_main`, passing in the correct root path for configuration files obtained via a JNI call to the Android activity (`getAppFilesDir`).

**Initialization (`backend::init`):**
1.  **Window Acquisition:** Unlike desktop, the `ANativeWindow` might not be immediately available. The code enters a loop, polling the Android `ALooper` until the `APP_CMD_INIT_WINDOW` event provides a valid window handle.
2.  **EGL Initialization:** The Android backend does not use GLX or WGL. Instead, it uses **EGL (Embedded-System Graphics Library)**, which is the standard Khronos API for interfacing with native windowing systems on embedded devices.
    *   `eglGetDisplay`, `eglInitialize`, and `eglChooseConfig` are called to select a compatible graphics configuration.
    *   `eglCreateWindowSurface` binds EGL to the native Android window.
    *   `eglCreateContext` creates the OpenGL ES 3 context.
3.  **ImGui Initialization:** `ImGui_ImplAndroid_Init()` and `ImGui_ImplOpenGL3_Init()` are called to bind ImGui to the Android window and the OpenGL ES context.

**Render Loop (`backend::renderLoop`):**
*   The loop is an infinite `while(true)` loop. Exit is not determined by a window state, but by the Android OS sending events.
*   **Event Handling:** The primary event handling is done via `ALooper_pollAll()`. This function processes events from the Android OS.
    *   If `app->destroyRequested` becomes non-zero, the application is shutting down, and the loop returns.
    *   The `handleAppCmd` function receives lifecycle events. Crucially, on `APP_CMD_TERM_WINDOW` (when the user switches apps), it sets `pauseRendering = true` and calls `backend::end()` to destroy the EGL surface. When `APP_CMD_INIT_WINDOW` is received again, it re-initializes the backend. This is essential for correctly handling the app being backgrounded and foregrounded.
*   **Input Handling:**
    *   Touch and mouse events are passed to ImGui via `ImGui_ImplAndroid_HandleInputEvent`.
    *   Keyboard input is more complex. Since the Android NDK does not provide a direct way to get Unicode characters from key events, the backend uses **JNI (Java Native Interface)** to call Java/Kotlin code in the main Android Activity:
        *   `PollUnicodeChars()`: Retrieves queued Unicode characters from the Java side
        *   `ShowSoftKeyboardInput()`: Shows the Android soft keyboard
        *   `HideSoftKeyboardInput()`: Hides the soft keyboard
        *   `GetClipboardText()`: Retrieves clipboard content
        *   `SetClipboardText()`: Sets clipboard content
        
        These JNI calls bridge the gap between the native C++ code and Android's Java-based input system.

This event-driven, state-machine-like approach is fundamentally different from the simple polling loop of the GLFW backend and is necessary to correctly integrate with the Android operating system.

## 3. Platform-Specific Build Flags and Considerations

### 3.1. macOS Build Flags

**Critical Build Flags:**
*   `USE_BUNDLE_DEFAULTS=ON`: Required for proper macOS app bundle behavior
*   `CMAKE_OSX_DEPLOYMENT_TARGET`: Should be set to appropriate minimum version (typically 10.15 or 11.0)
*   `IS_MACOS_BUNDLE`: Defined automatically when building as an app bundle, changes:
    *   Configuration path: `~/Library/Application Support/sdrpp/` instead of `~/.config/sdrpp/`
    *   Module search path: `.app/Contents/Plugins/`
    *   Working directory management in `sdrpp_main`

### 3.2. Windows Considerations

**Console Management:**
*   By default, `FreeConsole()` is called to detach from any console window
*   Use `--con` flag to keep console output visible
*   `SetErrorMode()` suppresses system error dialogs

### 3.3. Android Build System

**JNI Requirements:**
*   The backend expects specific Java methods in the Activity class
*   Asset management is handled through the Android asset manager
*   File paths must be relative, not absolute

## 4. Debugging Backend Issues

### 4.1. Common GLFW Backend Issues

1. **OpenGL Context Creation Failure:**
   - Check GPU driver support for requested OpenGL version
   - On macOS, ensure forward compatibility is set
   - Try different OpenGL versions in the probing list

2. **Window State Persistence:**
   - Window position/size saved in config may be off-screen
   - Delete config.json to reset window state

3. **Icon Loading Failures:**
   - Verify icon file exists in resources directory
   - Check PNG file integrity
   - Ensure stb_image is properly linked

### 4.2. Android Backend Debugging

1. **EGL Initialization Issues:**
   - Check device OpenGL ES support
   - Verify EGL config attributes match device capabilities
   - Monitor logcat for EGL error codes

2. **Lifecycle Problems:**
   - App crashes on resume: Check EGL surface recreation
   - Black screen: Verify rendering is resumed after `APP_CMD_INIT_WINDOW`
   - Input issues: Check JNI method signatures match Java side

3. **JNI Debugging:**
   - Use `adb logcat` to see both native and Java logs
   - Check for `JNI DETECTED ERROR` messages
   - Verify Java method names and signatures exactly match

### 4.3. Performance Considerations

**Frame Rate:**
- GLFW backend targets 60 FPS with VSync
- Android backend may have variable frame rates based on device
- Use `--no-vsync` flag for uncapped frame rate (testing only)

**Event Processing:**
- GLFW: Events processed once per frame
- Android: Events can arrive asynchronously
- Both: Heavy processing should not block event handling
