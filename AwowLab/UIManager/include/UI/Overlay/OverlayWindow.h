#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct GLFWwindow;

// Creates and manages an always-on-top overlay window for the combat meter.
// The window is borderless, draggable, and can be made transparent.
//
// Features:
// - Always-on-top (GLFW_FLOATING + HWND_TOPMOST)
// - Borderless (no title bar or frame)
// - Draggable when unlocked
// - Adjustable opacity
// - Click-through mode (optional)
// - Position persistence
//
// Usage:
//   OverlayWindow overlay;
//   overlay.create(300, 400);
//   overlay.setAlwaysOnTop(true);
//
//   while (!overlay.shouldClose()) {
//       overlay.pollEvents();
//       overlay.beginFrame();
//       // ImGui rendering here
//       overlay.endFrame();
//   }
//
class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    // Non-copyable
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    // Create the overlay window
    // Returns true if created successfully
    bool create(int width, int height, const char* title = "AwowLab Overlay");

    // Destroy the window
    void destroy();

    // Check if window is valid
    bool isValid() const { return window_ != nullptr; }

    // Check if window should close
    bool shouldClose() const;

    // Poll events
    void pollEvents() const;

    // Get the GLFW window handle (for Vulkan/ImGui integration)
    GLFWwindow* getWindow() const { return window_; }

    // === Window Properties ===

    // Set always-on-top behavior
    void setAlwaysOnTop(bool enable);
    bool isAlwaysOnTop() const { return alwaysOnTop_; }

    // Set window opacity (0.0 = fully transparent, 1.0 = fully opaque)
    void setOpacity(float alpha);
    float getOpacity() const { return opacity_; }

    // Set click-through mode (window ignores mouse input, passes through to below)
    void setClickThrough(bool enable);
    bool isClickThrough() const { return clickThrough_; }

    // When click-through is on, this rect (in screen coordinates) stays
    // clickable so the user can turn click-through back off. Set every
    // frame from the caller because ImGui layout can move the button.
    // Pass a zero-sized rect to disable the exception.
    void setInteractiveRect(int left, int top, int right, int bottom);

    // Lock/unlock window position (when locked, can't drag)
    void setLocked(bool locked) { locked_ = locked; }
    bool isLocked() const { return locked_; }

    // === Position and Size ===

    // Get/set window position
    void getPosition(int& x, int& y) const;
    void setPosition(int x, int y);

    // Get/set window size
    void getSize(int& width, int& height) const;
    void setSize(int width, int height);

    // === Input Handling ===

    // Process input for dragging (call in main loop)
    // Returns true if window was dragged this frame
    bool processDragInput();

    // Check if mouse is over the drag area (top of window)
    bool isInDragArea() const;

private:
    GLFWwindow* window_ = nullptr;

    // Window state
    bool alwaysOnTop_ = true;
    float opacity_ = 0.85f;
    bool clickThrough_ = false;
    bool locked_ = false;

    // Drag state
    bool isDragging_ = false;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;
    int windowStartX_ = 0;
    int windowStartY_ = 0;

    // Drag area height (pixels from top)
    static constexpr int kDragAreaHeight = 24;

#ifdef _WIN32
    HWND hwnd_ = nullptr;

    // Subclassed WndProc + saved original, used to implement the
    // "click-through everywhere except one rect" behaviour via
    // WM_NCHITTEST. Set once in applyWindowStyles.
    WNDPROC originalWndProc_ = nullptr;
    RECT interactiveRect_ = {0, 0, 0, 0};  // screen coords

    static LRESULT CALLBACK subclassedWndProc(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam);

    // Apply Windows-specific window styles
    void applyWindowStyles();

    // Get HWND from GLFW window
    HWND getHWND() const;
#endif
};
