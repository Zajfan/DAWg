#include "NativeWindowState.h"

#if JUCE_WINDOWS

#include <windows.h>

namespace dawg::app::window::native {

WindowCapabilities queryCapabilities(const juce::ComponentPeer* peer) noexcept
{
    WindowCapabilities capabilities;
    capabilities.supportsMaximize = peer != nullptr && peer->getNativeHandle() != nullptr;
    return capabilities;
}

bool setMaximized(juce::ComponentPeer& peer, const bool shouldBeMaximized) noexcept
{
    auto* window = static_cast<HWND>(peer.getNativeHandle());
    if (window == nullptr)
        return false;

    return ShowWindow(window, shouldBeMaximized ? SW_MAXIMIZE : SW_RESTORE) != 0;
}

bool isMaximized(const juce::ComponentPeer& peer) noexcept
{
    auto* window = static_cast<HWND>(peer.getNativeHandle());
    return window != nullptr && IsZoomed(window) != FALSE;
}

} // namespace dawg::app::window::native

#endif
