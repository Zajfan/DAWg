#pragma once

#include "WindowTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace dawg::app::window {

class ManagedWindow final : public juce::ResizableWindow {
public:
    using CloseRequested = std::function<void()>;

    ManagedWindow(const juce::String&, juce::Colour, WindowOptions,
                  std::unique_ptr<juce::Component>, CloseRequested);
    ~ManagedWindow() override;

    void userTriedToCloseWindow() override;

private:
    CloseRequested m_closeRequested;
};

} // namespace dawg::app::window
