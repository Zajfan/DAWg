#include "NativeWindowState.h"

#if JUCE_LINUX || JUCE_BSD

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <poll.h>

#include <cerrno>
#include <chrono>
#include <cstdint>

namespace dawg::platform::juce::window::native {
namespace {

::Window nativeWindow(const ::juce::ComponentPeer* peer) noexcept
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

bool hasMaximizedState(Display* display, const ::Window window,
                       const MaximizeAtoms atoms) noexcept
{
    return propertyContainsAtom(display, window, atoms.state, atoms.horizontal)
           && propertyContainsAtom(display, window, atoms.state, atoms.vertical);
}

bool waitForMaximizedState(Display* display, const ::Window window,
                           const MaximizeAtoms atoms, const bool shouldBeMaximized) noexcept
{
    // GNOME applies EWMH requests asynchronously. Wait for property notifications
    // on our own connection (never dispatch JUCE callbacks reentrantly). One
    // second allows multiple compositor frames, while bounding an ignored request.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    for (;;) {
        while (XPending(display) != 0) {
            XEvent notification{};
            XNextEvent(display, &notification);
        }

        const auto horizontal = propertyContainsAtom(display, window, atoms.state, atoms.horizontal);
        const auto vertical = propertyContainsAtom(display, window, atoms.state, atoms.vertical);
        if (horizontal == shouldBeMaximized && vertical == shouldBeMaximized)
            return true;

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return false;

        // XGetWindowProperty may have queued a notification in Xlib while
        // reading its reply. Check that queue before blocking on the socket.
        if (XPending(display) != 0)
            continue;

        pollfd descriptor { ConnectionNumber(display), POLLIN, 0 };
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
        const auto result = poll(&descriptor, 1, static_cast<int>(remaining.count()));
        if ((result < 0 && errno != EINTR)
            || (result > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0))
            return false;
    }
}

} // namespace

WindowCapabilities queryCapabilities(const ::juce::ComponentPeer* peer) noexcept
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

bool setMaximized(::juce::ComponentPeer& peer, const bool shouldBeMaximized) noexcept
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

    // This connection observes only this operation's property changes; selecting
    // here does not replace the event mask on JUCE's separate X11 connection.
    XSelectInput(display, window, PropertyChangeMask);
    XSync(display, False);

    XEvent event{};
    event.xclient.display = display;
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
    const auto applied = sent != 0
                         && waitForMaximizedState(display, window, atoms, shouldBeMaximized);
    XCloseDisplay(display);
    return applied;
}

bool isMaximized(const ::juce::ComponentPeer& peer) noexcept
{
    const auto window = nativeWindow(&peer);
    if (window == 0)
        return false;

    auto* display = openDisplay();
    if (display == nullptr)
        return false;

    const auto atoms = maximizeAtoms(display);
    const auto maximized = ewmhSupportsMaximize(display, atoms)
                           && hasMaximizedState(display, window, atoms);
    XCloseDisplay(display);
    return maximized;
}

} // namespace dawg::platform::juce::window::native

#endif
