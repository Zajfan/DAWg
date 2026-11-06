#pragma once

#include "WindowTypes.h"

#include <unordered_map>

namespace dawg::platform::juce::window {

class WindowRegistry final {
public:
    [[nodiscard]] std::optional<WindowHandle> registerWindow(WindowRegistration);
    [[nodiscard]] bool contains(WindowHandle) const noexcept;
    [[nodiscard]] std::optional<WindowSnapshot> snapshot(WindowHandle) const;
    [[nodiscard]] bool resize(WindowHandle, WindowBounds);
    [[nodiscard]] bool setState(WindowHandle, WindowState);
    [[nodiscard]] bool setOpacity(WindowHandle, float);
    [[nodiscard]] std::vector<ClosedWindow> removeTree(WindowHandle);
    [[nodiscard]] bool moveToDisplay(WindowHandle, const DisplayInfo&);

    [[nodiscard]] static WindowBounds logicalToPhysical(
        const DisplayInfo&, WindowBounds);
    [[nodiscard]] static WindowBounds physicalToLogical(
        const DisplayInfo&, WindowBounds);

private:
    struct Record {
        WindowSnapshot snapshot;
        WindowSizeLimits limits;
        std::function<void(int)> completion;
    };

    std::unordered_map<WindowHandle, Record> m_windows;
    WindowHandle m_nextHandle { 1 };
};

} // namespace dawg::platform::juce::window
