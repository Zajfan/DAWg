#pragma once

#include "WindowTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace dawg::platform::juce::window::native {

[[nodiscard]] WindowCapabilities queryCapabilities(
    const ::juce::ComponentPeer*) noexcept;

[[nodiscard]] bool setMaximized(
    ::juce::ComponentPeer&, bool shouldBeMaximized) noexcept;

[[nodiscard]] bool isMaximized(const ::juce::ComponentPeer&) noexcept;

} // namespace dawg::platform::juce::window::native
