#include "UI/Overlay/OverlayWindow.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>  // for std::min, std::max

#ifdef _WIN32
// Prevent Windows min/max macros from interfering with std::min/max
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM
#endif

OverlayWindow::OverlayWindow() = default;

OverlayWindow::~OverlayWindow() {
    destroy();
}

bool OverlayWindow::create(int width, int height, const char* title) {
    if (window_) {
        return false;  // Already created
    }

    // Initialize GLFW if not already initialized
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return false;
    }

    // Configure window hints for overlay
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No OpenGL context (using Vulkan)
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);    // Borderless
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);      // Always on top (initial setting)
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);     // Allow resizing
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);  // Transparent background

    // Create the window
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create overlay window\n";
        return false;
    }

    // Store user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);

#ifdef _WIN32
    // Get the native Windows handle
    hwnd_ = glfwGetWin32Window(window_);

    // Apply Windows-specific styles
    applyWindowStyles();
#endif

    // Apply initial settings
    setAlwaysOnTop(alwaysOnTop_);
    setOpacity(opacity_);

    std::cout << "Overlay window created: " << width << "x" << height << "\n";
    return true;
}

void OverlayWindow::destroy() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

#ifdef _WIN32
    hwnd_ = nullptr;
#endif
}

bool OverlayWindow::shouldClose() const {
    return window_ && glfwWindowShouldClose(window_);
}

void OverlayWindow::pollEvents() const {
    glfwPollEvents();
}

void OverlayWindow::setAlwaysOnTop(bool enable) {
    alwaysOnTop_ = enable;

#ifdef _WIN32
    if (hwnd_) {
        SetWindowPos(
            hwnd_,
            enable ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        );
    }
#else
    // On non-Windows, use GLFW hint (only works at creation time)
    // This is a fallback - may not work after window creation
#endif
}

void OverlayWindow::setOpacity(float alpha) {
    opacity_ = (std::max)(0.1f, (std::min)(1.0f, alpha));  // Clamp to [0.1, 1.0]

#ifdef _WIN32
    if (hwnd_) {
        // Use layered window for opacity
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);

        // Set opacity (0-255)
        BYTE alpha255 = static_cast<BYTE>(opacity_ * 255.0f);
        SetLayeredWindowAttributes(hwnd_, 0, alpha255, LWA_ALPHA);
    }
#else
    // GLFW opacity (may not be supported on all platforms)
    if (window_) {
        glfwSetWindowOpacity(window_, opacity_);
    }
#endif
}

void OverlayWindow::setClickThrough(bool enable) {
    clickThrough_ = enable;

    // Note: we do NOT toggle WS_EX_TRANSPARENT anymore. Instead the
    // subclassed WndProc returns HTTRANSPARENT from WM_NCHITTEST whenever
    // clickThrough_ is on, EXCEPT for the interactive rect (the lock
    // button). That leaves a live escape hatch so the user can always
    // click the button to turn click-through back off.
}

void OverlayWindow::setInteractiveRect(int left, int top, int right, int bottom) {
#ifdef _WIN32
    interactiveRect_.left = left;
    interactiveRect_.top = top;
    interactiveRect_.right = right;
    interactiveRect_.bottom = bottom;
#else
    (void)left; (void)top; (void)right; (void)bottom;
#endif
}

void OverlayWindow::getPosition(int& x, int& y) const {
    if (window_) {
        glfwGetWindowPos(window_, &x, &y);
    } else {
        x = y = 0;
    }
}

void OverlayWindow::setPosition(int x, int y) {
    if (window_) {
        glfwSetWindowPos(window_, x, y);
    }
}

void OverlayWindow::getSize(int& width, int& height) const {
    if (window_) {
        glfwGetWindowSize(window_, &width, &height);
    } else {
        width = height = 0;
    }
}

void OverlayWindow::setSize(int width, int height) {
    if (window_) {
        glfwSetWindowSize(window_, width, height);
    }
}

bool OverlayWindow::processDragInput() {
    if (!window_ || locked_) {
        return false;
    }

    // Get current mouse state
    double mouseX, mouseY;
    glfwGetCursorPos(window_, &mouseX, &mouseY);

    bool leftButtonDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (!isDragging_) {
        // Check if we should start dragging
        if (leftButtonDown && isInDragArea()) {
            isDragging_ = true;
            dragStartX_ = mouseX;
            dragStartY_ = mouseY;
            glfwGetWindowPos(window_, &windowStartX_, &windowStartY_);
        }
    } else {
        // Continue dragging or stop
        if (leftButtonDown) {
            // Calculate new window position
            double deltaX = mouseX - dragStartX_;
            double deltaY = mouseY - dragStartY_;

            int newX = windowStartX_ + static_cast<int>(deltaX);
            int newY = windowStartY_ + static_cast<int>(deltaY);

            glfwSetWindowPos(window_, newX, newY);
            return true;
        } else {
            // Stop dragging
            isDragging_ = false;
        }
    }

    return isDragging_;
}

bool OverlayWindow::isInDragArea() const {
    if (!window_) return false;

    double mouseX, mouseY;
    glfwGetCursorPos(window_, &mouseX, &mouseY);

    // Drag area is the top portion of the window
    return mouseY >= 0 && mouseY < kDragAreaHeight && mouseX >= 0;
}

#ifdef _WIN32
LRESULT CALLBACK OverlayWindow::subclassedWndProc(HWND hwnd, UINT msg,
                                                  WPARAM wParam, LPARAM lParam) {
    // Get the owning OverlayWindow via the GWLP_USERDATA slot we set in
    // applyWindowStyles. If we lose it for any reason, fall back to
    // DefWindowProc so we don't wedge the process.
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    if (msg == WM_NCHITTEST && self->clickThrough_) {
        // LPARAM packs the screen-space cursor position into low/high
        // words. Return HTTRANSPARENT to make Windows pass the message
        // to whatever's under the overlay - except when the cursor is
        // inside the interactive rect (i.e. hovering the lock button).
        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);
        const RECT& r = self->interactiveRect_;
        bool insideEscapeHatch = (r.right > r.left && r.bottom > r.top)
            && pt.x >= r.left && pt.x < r.right
            && pt.y >= r.top && pt.y < r.bottom;
        if (!insideEscapeHatch) {
            return HTTRANSPARENT;
        }
        // Fall through to original proc which will return HTCLIENT for
        // the button area, letting ImGui receive the click normally.
    }

    return CallWindowProcW(self->originalWndProc_, hwnd, msg, wParam, lParam);
}

void OverlayWindow::applyWindowStyles() {
    if (!hwnd_) return;

    // Get current extended style
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);

    // Add layered window style for opacity support
    exStyle |= WS_EX_LAYERED;

    // Tool window: doesn't show in taskbar
    exStyle |= WS_EX_TOOLWINDOW;

    // Remove app window: ensures it doesn't show in Alt+Tab
    exStyle &= ~WS_EX_APPWINDOW;

    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);

    // Install our WndProc subclass. GLFW stores its own state in
    // GWLP_WNDPROC; we save the previous proc so we can chain to it,
    // and stash `this` in GWLP_USERDATA so the static callback can find
    // us. GLFW does not read GWLP_USERDATA on the window HWND (it uses
    // its own userpointer on the GLFWwindow), so this is safe.
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    originalWndProc_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd_, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(&OverlayWindow::subclassedWndProc)));

    // Force repaint
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

HWND OverlayWindow::getHWND() const {
    return hwnd_;
}
#endif
