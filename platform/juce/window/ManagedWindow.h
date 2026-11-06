#pragma once

#include "WindowTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace dawg::platform::juce::window {

class ManagedWindow final : public ::juce::ResizableWindow {
public:
    using CloseRequested = std::function<void()>;

    ManagedWindow(const ::juce::String&, ::juce::Colour, WindowOptions,
                  std::unique_ptr<::juce::Component>, CloseRequested, bool closeOnEscape);
    ~ManagedWindow() override;

    void userTriedToCloseWindow() override;
    bool keyPressed(const ::juce::KeyPress&) override;

private:
#if JUCE_LINUX || JUCE_BSD
    int getDesktopWindowStyleFlags() const override;
#endif

    // A callback may synchronously destroy this window. Share the callable so
    // the active invocation retains its captures and repeated attempts retain
    // mutable callback state.
    std::shared_ptr<CloseRequested> m_closeRequested;
    bool m_closeOnEscape { false };
};

} // namespace dawg::platform::juce::window
