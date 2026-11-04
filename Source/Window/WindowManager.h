#pragma once

#include "WindowRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dawg::app::window {

class ManagedWindow;

class WindowManager final {
public:
    WindowManager();
    ~WindowManager();

    [[nodiscard]] std::optional<WindowHandle> createWindow(
        std::unique_ptr<juce::Component> content,
        WindowOptions options = {});

    [[nodiscard]] std::optional<WindowHandle> showModalDialog(
        std::unique_ptr<juce::Component> content,
        DialogOptions options = {});

    [[nodiscard]] bool closeWindow(WindowHandle);
    [[nodiscard]] bool resizeWindow(WindowHandle, WindowBounds);
    [[nodiscard]] bool setWindowState(WindowHandle, WindowState);
    [[nodiscard]] bool setWindowOpacity(WindowHandle, float);
    [[nodiscard]] bool moveToDisplay(WindowHandle, DisplayId);

    [[nodiscard]] std::optional<WindowSnapshot> getWindow(WindowHandle) const;
    [[nodiscard]] WindowCapabilities getCapabilities() const noexcept;
    [[nodiscard]] std::vector<DisplayInfo> getDisplays() const;
    [[nodiscard]] bool refreshDisplays();
    [[nodiscard]] std::optional<WindowBounds> logicalToPhysical(
        DisplayId, WindowBounds) const;
    [[nodiscard]] std::optional<WindowBounds> physicalToLogical(
        DisplayId, WindowBounds) const;

private:
    class ModalCallback;

    [[nodiscard]] bool isMessageThread() const noexcept;
    [[nodiscard]] std::optional<WindowHandle> createWindowInternal(
        std::unique_ptr<juce::Component>, WindowOptions, bool, bool,
        std::function<void(int)>);
    [[nodiscard]] const DisplayInfo* findDisplay(DisplayId) const noexcept;
    [[nodiscard]] const DisplayInfo* displayForBounds(WindowBounds) const noexcept;
    void finishModal(WindowHandle, int);
    void destroyTree(WindowHandle, int, bool exitModalDescendants = true);

    WindowRegistry m_registry;
    std::unordered_map<WindowHandle, std::unique_ptr<ManagedWindow>> m_windows;
    WindowCapabilities m_capabilities;
    std::vector<DisplayInfo> m_displays;
    std::unordered_set<WindowHandle> m_closingModals;
    std::shared_ptr<int> m_lifetime { std::make_shared<int>(0) };
};

} // namespace dawg::app::window
