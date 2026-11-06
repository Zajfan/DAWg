#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dawg::platform::juce::window {

using WindowHandle = std::uint64_t;
using DisplayId = std::uint32_t;

enum class WindowState { normal, minimized, maximized, fullscreen };

struct WindowBounds {
    int x { 0 };
    int y { 0 };
    int width { 0 };
    int height { 0 };

    friend bool operator==(const WindowBounds&, const WindowBounds&) = default;
};

struct WindowSizeLimits {
    int minimumWidth { 1 };
    int minimumHeight { 1 };
    int maximumWidth { 0 };
    int maximumHeight { 0 };
};

struct DisplayInfo {
    DisplayId id {};
    bool primary { false };
    WindowBounds logicalUserBounds;
    WindowBounds logicalBounds;
    WindowBounds physicalBounds;
    double scale { 1.0 };
    double dpi { 96.0 };
};

struct WindowSnapshot {
    WindowHandle handle {};
    WindowHandle parent {};
    std::string title;
    WindowBounds bounds;
    WindowState state { WindowState::normal };
    bool visible { false };
    bool modal { false };
    float opacity { 1.0f };
    std::optional<DisplayId> display;
};

struct WindowCapabilities {
    bool supportsTransparency { false };
    bool supportsFramelessWindows { true };
    bool supportsFullscreen { true };
    bool supportsPerWindowOpacity { false };
    bool supportsMaximize { false };
};

struct WindowOptions {
    std::string title;
    WindowBounds bounds { 0, 0, 900, 560 };
    int minimumWidth { 320 };
    int minimumHeight { 200 };
    int maximumWidth { 0 };
    int maximumHeight { 0 };
    bool resizable { true };
    bool useNativeTitleBar { true };
    bool transparent { false };
    float opacity { 1.0f };
    bool alwaysOnTop { false };
    bool visible { true };
    WindowState initialState { WindowState::normal };
    WindowHandle parent {};
    std::function<void(WindowHandle)> closeRequested;
};

struct DialogOptions {
    WindowOptions window;
    bool closeOnEscape { true };
    std::function<void(int)> completion;
};

struct WindowRegistration {
    WindowSnapshot snapshot;
    WindowSizeLimits limits;
    std::function<void(int)> completion;
};

struct ClosedWindow {
    WindowHandle handle {};
    std::function<void(int)> completion;

    ClosedWindow() = default;
    ClosedWindow(WindowHandle windowHandle, std::function<void(int)> callback)
        : handle(windowHandle), completion(static_cast<std::function<void(int)>&&>(callback))
    {
    }

    ClosedWindow(const ClosedWindow&) = delete;
    ClosedWindow& operator=(const ClosedWindow&) = delete;
    ClosedWindow(ClosedWindow&&) noexcept = default;
    ClosedWindow& operator=(ClosedWindow&&) noexcept = default;
};

} // namespace dawg::platform::juce::window
