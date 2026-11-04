#include "WindowManager.h"
#include "ManagedWindow.h"
#include "WindowTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string_view>

namespace dawg::app::window {
namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool withinOneLogicalPixel(int actual, int expected)
{
    return std::abs(actual - expected) <= 1;
}

bool boundsAreWithinOneLogicalPixel(const WindowBounds& actual,
                                    const WindowBounds& expected)
{
    return withinOneLogicalPixel(actual.x, expected.x)
           && withinOneLogicalPixel(actual.y, expected.y)
           && withinOneLogicalPixel(actual.width, expected.width)
           && withinOneLogicalPixel(actual.height, expected.height);
}

std::unique_ptr<juce::Component> makeContent()
{
    return std::make_unique<juce::Component>();
}

void testCreateResizeAndClose()
{
    WindowManager manager;
    bool closeRequested = false;
    WindowHandle requestedHandle = 0;
    int closeRequestCount = 0;

    WindowOptions options;
    options.title = "Test Window";
    options.bounds = { 80, 80, 640, 480 };
    options.minimumWidth = 320;
    options.minimumHeight = 200;
    options.useNativeTitleBar = true;
    options.closeRequested = [&](WindowHandle handle) {
        closeRequested = true;
        requestedHandle = handle;
        ++closeRequestCount;
    };

    const auto handle = manager.createWindow(makeContent(), options);
    expect(handle.has_value(), "creating a real window returns a handle");
    if (!handle)
        return;

    const auto initialSnapshot = manager.getWindow(*handle);
    expect(initialSnapshot.has_value(), "created window has a snapshot");
    if (initialSnapshot) {
        expect(initialSnapshot->handle == *handle, "snapshot reports its handle");
        expect(initialSnapshot->title == "Test Window", "snapshot reports its title");
        expect(initialSnapshot->bounds == WindowBounds { 80, 80, 640, 480 },
               "snapshot reports initial bounds");
        expect(initialSnapshot->visible, "created window is visible");
    }

    expect(manager.resizeWindow(*handle, { 120, 100, 800, 600 }),
           "resizing a real window succeeds");
    const auto resizedSnapshot = manager.getWindow(*handle);
    expect(resizedSnapshot.has_value(), "resized window has a snapshot");
    if (resizedSnapshot)
        expect(resizedSnapshot->bounds == WindowBounds { 120, 100, 800, 600 },
               "snapshot reports resized bounds");

    expect(manager.closeWindow(*handle), "closing a real window succeeds");
    expect(!manager.getWindow(*handle).has_value(), "closed handle is no longer queryable");
    expect(!closeRequested, "explicit close does not invoke close-request callback");

    WindowOptions callbackOptions;
    callbackOptions.title = "Callback Window";
    callbackOptions.bounds = { 100, 100, 320, 240 };
    callbackOptions.closeRequested = [&](WindowHandle callbackHandle) {
        closeRequested = true;
        requestedHandle = callbackHandle;
        ++closeRequestCount;
    };
    ManagedWindow managedWindow("Callback Window", juce::Colours::darkgrey,
                                callbackOptions, makeContent(), [&callbackOptions, handle] {
                                    if (callbackOptions.closeRequested)
                                        callbackOptions.closeRequested(handle);
                                });
    managedWindow.userTriedToCloseWindow();

    expect(closeRequested, "managed window forwards a close request");
    expect(requestedHandle == *handle, "close-request callback receives the window handle");
    expect(closeRequestCount == 1, "close-request callback runs exactly once");
}

void testWindowStatesAndCapabilities()
{
    WindowManager manager;
    WindowOptions options;
    options.title = "State Window";
    options.bounds = { 120, 120, 640, 480 };
    const auto handle = manager.createWindow(makeContent(), options);
    expect(handle.has_value(), "state window is created");
    if (!handle)
        return;

    expect(manager.setWindowState(*handle, WindowState::minimized),
           "normal window can be minimized");
    auto snapshot = manager.getWindow(*handle);
    expect(snapshot.has_value() && snapshot->state == WindowState::minimized,
           "minimized state is reported");

    expect(manager.setWindowState(*handle, WindowState::normal),
           "minimized window can be restored");
    snapshot = manager.getWindow(*handle);
    expect(snapshot.has_value() && snapshot->state == WindowState::normal,
           "normal state is reported after restore");

    expect(manager.setWindowState(*handle, WindowState::fullscreen),
           "window can enter fullscreen");
    snapshot = manager.getWindow(*handle);
    expect(snapshot.has_value() && snapshot->state == WindowState::fullscreen,
           "fullscreen state is reported");

    expect(manager.setWindowState(*handle, WindowState::normal),
           "fullscreen window can be restored");
    snapshot = manager.getWindow(*handle);
    expect(snapshot.has_value() && snapshot->state == WindowState::normal,
           "normal state is reported after fullscreen restore");

    const auto capabilities = manager.getCapabilities();
    const auto beforeMaximize = manager.getWindow(*handle);
    expect(beforeMaximize.has_value(), "snapshot exists before maximize attempt");
    if (!beforeMaximize)
        return;

    if (capabilities.supportsMaximize) {
        expect(manager.setWindowState(*handle, WindowState::maximized),
               "supported maximize succeeds");
        snapshot = manager.getWindow(*handle);
        expect(snapshot.has_value() && snapshot->state == WindowState::maximized,
               "supported maximize is reported");
        expect(manager.setWindowState(*handle, WindowState::normal),
               "maximized window can be restored");
    } else {
        expect(!manager.setWindowState(*handle, WindowState::maximized),
               "unsupported maximize fails");
        snapshot = manager.getWindow(*handle);
        expect(snapshot.has_value() && snapshot->state == beforeMaximize->state,
               "failed maximize leaves state unchanged");
    }

    WindowOptions framelessOptions;
    framelessOptions.title = "Frameless Window";
    framelessOptions.bounds = { 140, 140, 400, 300 };
    framelessOptions.useNativeTitleBar = false;
    const auto frameless = manager.createWindow(makeContent(), framelessOptions);
    if (capabilities.supportsFramelessWindows) {
        expect(frameless.has_value(), "supported frameless window is created");
    } else {
        expect(!frameless.has_value(), "unsupported frameless window is rejected");
    }

    WindowOptions transparentOptions;
    transparentOptions.title = "Transparent Window";
    transparentOptions.bounds = { 160, 160, 400, 300 };
    transparentOptions.transparent = true;
    const auto transparent = manager.createWindow(makeContent(), transparentOptions);
    if (capabilities.supportsTransparency) {
        expect(transparent.has_value(), "supported transparent window is created");
    } else {
        expect(!transparent.has_value(), "unsupported transparency is rejected");
    }

    const auto upperOpacityResult = manager.setWindowOpacity(*handle, 2.0f);
    const auto lowerOpacityResult = manager.setWindowOpacity(*handle, -1.0f);
    if (capabilities.supportsPerWindowOpacity) {
        expect(upperOpacityResult, "supported opacity update accepts an upper value");
        snapshot = manager.getWindow(*handle);
        expect(snapshot.has_value() && snapshot->opacity == 1.0f,
               "opacity is clamped to one");
        expect(lowerOpacityResult, "supported opacity update accepts a lower value");
        snapshot = manager.getWindow(*handle);
        expect(snapshot.has_value() && snapshot->opacity == 0.0f,
               "opacity is clamped to zero");
    } else {
        expect(!upperOpacityResult, "unsupported opacity update fails");
        expect(!lowerOpacityResult, "unsupported opacity update fails consistently");
    }

    if (frameless)
        manager.closeWindow(*frameless);
    if (transparent)
        manager.closeWindow(*transparent);
    manager.closeWindow(*handle);
}

void testChildCascadeAndModalCompletion()
{
    WindowManager manager;
    WindowOptions parentOptions;
    parentOptions.title = "Parent Window";
    parentOptions.bounds = { 180, 180, 640, 480 };
    const auto parent = manager.createWindow(makeContent(), parentOptions);
    expect(parent.has_value(), "parent window is created");
    if (!parent)
        return;

    WindowOptions childOptions;
    childOptions.title = "Child Window";
    childOptions.bounds = { 200, 200, 400, 300 };
    childOptions.parent = *parent;
    const auto child = manager.createWindow(makeContent(), childOptions);
    expect(child.has_value(), "child window is created");
    if (child) {
        const auto childSnapshot = manager.getWindow(*child);
        expect(childSnapshot.has_value() && childSnapshot->parent == *parent,
               "child snapshot records its parent");
    }

    int completionCount = 0;
    int completionResult = -1;
    DialogOptions dialogOptions;
    dialogOptions.window.title = "Test Dialog";
    dialogOptions.window.bounds = { 240, 240, 360, 240 };
    dialogOptions.window.parent = *parent;
    dialogOptions.completion = [&](int result) {
        ++completionCount;
        completionResult = result;
    };
    const auto dialog = manager.showModalDialog(makeContent(), dialogOptions);
    expect(dialog.has_value(), "modal dialog is created");
    if (dialog) {
        const auto dialogSnapshot = manager.getWindow(*dialog);
        expect(dialogSnapshot.has_value() && dialogSnapshot->modal,
               "dialog snapshot reports modal state");
        expect(manager.closeWindow(*dialog), "modal dialog closes through the manager");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        expect(completionCount == 1, "modal completion runs exactly once");
        expect(completionResult == 0, "explicit modal close completes with result zero");
    }

    expect(manager.closeWindow(*parent), "closing a parent window succeeds");
    expect(!manager.getWindow(*parent).has_value(), "closed parent is no longer queryable");
    if (child)
        expect(!manager.getWindow(*child).has_value(), "closing a parent closes its child");
}

void testDisplaySnapshotAndConversion()
{
    WindowManager manager;
    const auto displays = manager.getDisplays();
    expect(!displays.empty(), "manager reports the desktop displays");

    for (const auto& display : displays) {
        expect(display.scale > 0.0, "display scale is positive");
        expect(display.dpi > 0.0, "display DPI is positive");

        const WindowBounds logicalBounds {
            display.logicalUserBounds.x,
            display.logicalUserBounds.y,
            std::max(1, std::min(200, display.logicalUserBounds.width)),
            std::max(1, std::min(200, display.logicalUserBounds.height)),
        };
        const auto physicalBounds = manager.logicalToPhysical(display.id, logicalBounds);
        expect(physicalBounds.has_value(), "known display converts logical bounds");
        if (!physicalBounds)
            continue;

        const auto roundTrippedBounds =
            manager.physicalToLogical(display.id, *physicalBounds);
        expect(roundTrippedBounds.has_value(), "known display converts physical bounds");
        if (roundTrippedBounds)
            expect(boundsAreWithinOneLogicalPixel(*roundTrippedBounds, logicalBounds),
                   "display conversion round-trips within one logical pixel");
    }
}

} // namespace
} // namespace dawg::app::window

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        return 77;

    using namespace dawg::app::window;
    testCreateResizeAndClose();
    testWindowStatesAndCapabilities();
    testChildCascadeAndModalCompletion();
    testDisplaySnapshotAndConversion();
    return failures == 0 ? 0 : 1;
}
