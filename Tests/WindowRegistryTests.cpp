#include "WindowRegistry.h"

#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace dawg::app::window {
namespace {

int failureCount = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failureCount;
    }
}

WindowRegistration makeRegistration(WindowBounds bounds = { 10, 20, 640, 480 })
{
    WindowRegistration registration{};
    registration.snapshot.bounds = bounds;
    registration.snapshot.state = WindowState::normal;
    registration.snapshot.visible = true;
    registration.snapshot.opacity = 1.0f;
    registration.limits.minimumWidth = 320;
    registration.limits.minimumHeight = 200;
    registration.limits.maximumWidth = 1280;
    registration.limits.maximumHeight = 900;
    return registration;
}

bool containsHandle(const std::vector<ClosedWindow>& closedWindows, WindowHandle handle)
{
    for (const auto& closedWindow : closedWindows) {
        if (closedWindow.handle == handle)
            return true;
    }

    return false;
}

DisplayInfo makeDisplay()
{
    DisplayInfo display{};
    display.logicalUserBounds = { 100, 50, 800, 600 };
    display.logicalBounds = display.logicalUserBounds;
    display.physicalBounds = { 200, 100, 1600, 1200 };
    display.scale = 2.0;
    display.dpi = 192.0;
    return display;
}

void handlesAreUniqueAndParentCloseCascades()
{
    WindowRegistry registry;

    const auto parent = registry.registerWindow(makeRegistration());
    expect(parent.has_value(), "parent registration succeeds");
    if (!parent)
        return;

    auto childRegistration = makeRegistration();
    childRegistration.snapshot.parent = *parent;
    const auto child = registry.registerWindow(std::move(childRegistration));
    expect(child.has_value(), "child registration succeeds");
    if (!child)
        return;

    expect(*parent != 0, "parent handle is non-zero");
    expect(*child != 0, "child handle is non-zero");
    expect(*parent != *child, "window handles are distinct");

    const auto childSnapshot = registry.snapshot(*child);
    expect(childSnapshot.has_value(), "child snapshot exists");
    if (childSnapshot)
        expect(childSnapshot->parent == *parent, "child records its parent");

    const auto closedWindows = registry.removeTree(*parent);
    expect(closedWindows.size() == 2, "closing a parent returns both windows");
    expect(containsHandle(closedWindows, *parent), "closed windows contain the parent");
    expect(containsHandle(closedWindows, *child), "closed windows contain the child");
    expect(registry.removeTree(*parent).empty(), "a second removal returns no windows");
}

void resizingClampsToRegisteredLimits()
{
    WindowRegistry registry;
    const auto handle = registry.registerWindow(makeRegistration());
    expect(handle.has_value(), "window registration succeeds");
    if (!handle)
        return;

    expect(registry.resize(*handle, { 0, 0, 100, 100 }), "small resize succeeds");
    auto snapshot = registry.snapshot(*handle);
    expect(snapshot.has_value(), "snapshot exists after small resize");
    if (snapshot)
        expect(snapshot->bounds == WindowBounds{ 0, 0, 320, 200 },
               "small resize clamps to minimum limits");

    expect(registry.resize(*handle, { 0, 0, 2000, 2000 }), "large resize succeeds");
    snapshot = registry.snapshot(*handle);
    expect(snapshot.has_value(), "snapshot exists after large resize");
    if (snapshot)
        expect(snapshot->bounds == WindowBounds{ 0, 0, 1280, 900 },
               "large resize clamps to maximum limits");
}

void invalidHandlesDoNotMutateState()
{
    WindowRegistry registry;
    const auto handle = registry.registerWindow(makeRegistration());
    expect(handle.has_value(), "window registration succeeds");
    if (!handle)
        return;

    const auto before = registry.snapshot(*handle);
    expect(before.has_value(), "initial snapshot exists");
    if (!before)
        return;

    expect(!registry.contains(0), "zero handle is not contained");
    expect(!registry.resize(0, { 0, 0, 1, 1 }), "zero handle resize fails");
    expect(!registry.setState(0, WindowState::maximized), "zero handle state update fails");
    expect(!registry.setOpacity(0, 0.5f), "zero handle opacity update fails");
    expect(registry.removeTree(0).empty(), "zero handle removal is empty");

    const auto after = registry.snapshot(*handle);
    expect(after.has_value(), "valid snapshot still exists");
    if (after)
        expect(after->handle == before->handle && after->parent == before->parent
                   && after->title == before->title && after->bounds == before->bounds
                   && after->state == before->state && after->visible == before->visible
                   && after->modal == before->modal && after->opacity == before->opacity
                   && after->display == before->display,
               "invalid handles do not mutate valid state");
}

void displayPlacementPreservesSizeAndClampsPosition()
{
    WindowRegistry registry;
    const auto handle = registry.registerWindow(makeRegistration({ 0, 0, 300, 200 }));
    expect(handle.has_value(), "window registration succeeds");
    if (!handle)
        return;

    expect(registry.moveToDisplay(*handle, makeDisplay()), "display placement succeeds");
    const auto snapshot = registry.snapshot(*handle);
    expect(snapshot.has_value(), "snapshot exists after display placement");
    if (snapshot)
        expect(snapshot->bounds == WindowBounds{ 100, 50, 300, 200 },
               "display placement clamps position and preserves size");
}

void modalCompletionIsReturnedOnlyOnce()
{
    WindowRegistry registry;
    int callbackCount = 0;
    auto registration = makeRegistration();
    registration.snapshot.modal = true;
    registration.completion = [&callbackCount](int) { ++callbackCount; };

    const auto handle = registry.registerWindow(std::move(registration));
    expect(handle.has_value(), "modal registration succeeds");
    if (!handle)
        return;

    const auto closedWindows = registry.removeTree(*handle);
    expect(callbackCount == 0, "modal completion is not delivered by removeTree");
    expect(closedWindows.size() == 1, "modal removal returns one closed window");
    expect(!closedWindows.empty() && static_cast<bool>(closedWindows.front().completion),
           "modal removal returns a completion callback");
    for (const auto& closedWindow : closedWindows) {
        if (closedWindow.completion)
            closedWindow.completion(7);
    }

    expect(registry.removeTree(*handle).empty(), "modal removal only succeeds once");
    expect(callbackCount == 1, "modal completion callback is delivered exactly once");
}

void displayConversionUsesScaleAndOrigins()
{
    const auto display = makeDisplay();
    const WindowBounds logicalBounds{ 110, 70, 300, 200 };
    const auto physicalBounds = WindowRegistry::logicalToPhysical(display, logicalBounds);
    expect(physicalBounds == WindowBounds{ 220, 140, 600, 400 },
           "logical bounds convert to physical bounds using origins and scale");

    const auto roundTrippedBounds = WindowRegistry::physicalToLogical(display, physicalBounds);
    expect(roundTrippedBounds == logicalBounds,
           "physical bounds convert back to the original logical bounds");
}

} // namespace
} // namespace dawg::app::window

int main()
{
    using namespace dawg::app::window;

    handlesAreUniqueAndParentCloseCascades();
    resizingClampsToRegisteredLimits();
    invalidHandlesDoNotMutateState();
    displayPlacementPreservesSizeAndClampsPosition();
    modalCompletionIsReturnedOnlyOnce();
    displayConversionUsesScaleAndOrigins();

    return failureCount == 0 ? 0 : 1;
}
