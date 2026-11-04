#include "NativeWindowState.h"

#if JUCE_LINUX || JUCE_BSD

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstdint>

namespace dawg::app::window::native {
namespace {

::Window nativeWindow(const juce::ComponentPeer* peer) noexcept
{
    if (peer == nullptr || peer->getNativeHandle() == nullptr)
        return 0;

    return static_cast<::Window>(reinterpret_cast<std::uintptr_t>(peer->getNativeHandle()));
}

Display* openDisplay() noexcept
{
    return XOpenDisplay(nullptr);
}

Atom stateAtom(Display* display)
{
    return XInternAtom(display, "_NET_WM_STATE", False);
}

Atom horizontalAtom(Display* display)
{
    return XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
}

Atom verticalAtom(Display* display)
{
    return XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
}

bool hasAtom(Display* display, const ::Window window, const Atom wanted) noexcept
{
    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    const auto result = XGetWindowProperty(display, window, stateAtom(display), 0, 1024,
                                           False, XA_ATOM, &actualType, &actualFormat,
                                           &itemCount, &bytesAfter, &data);
    if (result != Success || data == nullptr)
        return false;

    bool found = false;
    if (actualType == XA_ATOM && actualFormat == 32) {
        const auto* atoms = reinterpret_cast<const Atom*>(data);
        for (unsigned long index = 0; index < itemCount; ++index) {
            if (atoms[index] == wanted) {
                found = true;
                break;
            }
        }
    }

    XFree(data);
    return found;
}

} // namespace

WindowCapabilities queryCapabilities(const juce::ComponentPeer* peer) noexcept
{
    WindowCapabilities capabilities;
    const auto window = nativeWindow(peer);
    if (window == 0)
        return capabilities;

    if (auto* display = openDisplay()) {
        capabilities.supportsMaximize = true;
        XCloseDisplay(display);
    }
    return capabilities;
}

bool setMaximized(juce::ComponentPeer& peer, const bool shouldBeMaximized) noexcept
{
    const auto window = nativeWindow(&peer);
    if (window == 0)
        return false;

    auto* display = openDisplay();
    if (display == nullptr)
        return false;

    XEvent event{};
    event.xclient.window = window;
    event.xclient.type = ClientMessage;
    event.xclient.format = 32;
    event.xclient.message_type = stateAtom(display);
    event.xclient.data.l[0] = shouldBeMaximized ? 1 : 0;
    event.xclient.data.l[1] = static_cast<long>(horizontalAtom(display));
    event.xclient.data.l[2] = static_cast<long>(verticalAtom(display));
    event.xclient.data.l[3] = 1;

    const auto root = DefaultRootWindow(display);
    const auto sent = XSendEvent(display, root, False,
                                 SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    XCloseDisplay(display);
    return sent != 0;
}

bool isMaximized(const juce::ComponentPeer& peer) noexcept
{
    const auto window = nativeWindow(&peer);
    if (window == 0)
        return false;

    auto* display = openDisplay();
    if (display == nullptr)
        return false;

    const auto maximized = hasAtom(display, window, horizontalAtom(display))
                           && hasAtom(display, window, verticalAtom(display));
    XCloseDisplay(display);
    return maximized;
}

} // namespace dawg::app::window::native

#endif
