#include "NativeWindowState.h"

#if ! JUCE_WINDOWS && ! JUCE_MAC && ! JUCE_LINUX && ! JUCE_BSD

namespace dawg::app::window::native {

WindowCapabilities queryCapabilities(const juce::ComponentPeer*) noexcept
{
    return {};
}

bool setMaximized(juce::ComponentPeer&, bool) noexcept
{
    return false;
}

bool isMaximized(const juce::ComponentPeer&) noexcept
{
    return false;
}

} // namespace dawg::app::window::native

#endif
