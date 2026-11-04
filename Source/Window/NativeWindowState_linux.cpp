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

struct MaximizeAtoms {
    Atom supported { None };
    Atom state { None };
    Atom horizontal { None };
    Atom vertical { None };
};

MaximizeAtoms maximizeAtoms(Display* display)
{
    return {
        XInternAtom(display, "_NET_SUPPORTED", True),
        XInternAtom(display, "_NET_WM_STATE", True),
        XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", True),
        XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", True),
    };
}

bool propertyContainsAtom(Display* display, const ::Window window,
                          const Atom property, const Atom wanted) noexcept
{
    if (property == None || wanted == None)
        return false;

    Atom actualType = None;
    int actualFormat = 0;
    unsigned long itemCount = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    const auto result = XGetWindowProperty(display, window, property, 0, 1024,
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

bool ewmhSupportsMaximize(Display* display, const MaximizeAtoms atoms) noexcept
{
    if (display == nullptr || atoms.supported == None || atoms.state == None
        || atoms.horizontal == None || atoms.vertical == None)
        return false;

    const auto root = DefaultRootWindow(display);
    return propertyContainsAtom(display, root, atoms.supported, atoms.state)
           && propertyContainsAtom(display, root, atoms.supported, atoms.horizontal)
           && propertyContainsAtom(display, root, atoms.supported, atoms.vertical);
}

} // namespace

WindowCapabilities queryCapabilities(const juce::ComponentPeer* peer) noexcept
{
    WindowCapabilities capabilities;
    const auto window = nativeWindow(peer);
    if (window == 0)
        return capabilities;

    if (auto* display = openDisplay()) {
        capabilities.supportsMaximize = ewmhSupportsMaximize(display, maximizeAtoms(display));
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

    const auto atoms = maximizeAtoms(display);
    if (! ewmhSupportsMaximize(display, atoms)) {
        XCloseDisplay(display);
        return false;
    }

    XEvent event{};
    event.xclient.window = window;
    event.xclient.type = ClientMessage;
    event.xclient.format = 32;
    event.xclient.message_type = atoms.state;
    event.xclient.data.l[0] = shouldBeMaximized ? 1 : 0;
    event.xclient.data.l[1] = static_cast<long>(atoms.horizontal);
    event.xclient.data.l[2] = static_cast<long>(atoms.vertical);
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

    const auto atoms = maximizeAtoms(display);
    const auto maximized = ewmhSupportsMaximize(display, atoms)
                           && propertyContainsAtom(display, window, atoms.state, atoms.horizontal)
                           && propertyContainsAtom(display, window, atoms.state, atoms.vertical);
    XCloseDisplay(display);
    return maximized;
}

} // namespace dawg::app::window::native

#endif
