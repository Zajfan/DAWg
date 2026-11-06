#include "NativeWindowState.h"

#if JUCE_MAC

#import <AppKit/AppKit.h>

namespace dawg::platform::juce::window::native {

WindowCapabilities queryCapabilities(const ::juce::ComponentPeer* peer) noexcept
{
    WindowCapabilities capabilities;
    capabilities.supportsMaximize = peer != nullptr && peer->getNativeHandle() != nullptr;
    return capabilities;
}

bool setMaximized(::juce::ComponentPeer& peer, const bool shouldBeMaximized) noexcept
{
    auto* window = static_cast<NSWindow*>(peer.getNativeHandle());
    if (window == nullptr)
        return false;

    if (window.zoomed != shouldBeMaximized)
        [window zoom:nil];
    return window.zoomed == shouldBeMaximized;
}

bool isMaximized(const ::juce::ComponentPeer& peer) noexcept
{
    auto* window = static_cast<NSWindow*>(peer.getNativeHandle());
    return window != nullptr && window.zoomed;
}

} // namespace dawg::platform::juce::window::native

#endif
