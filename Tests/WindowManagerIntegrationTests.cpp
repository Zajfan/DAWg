#include "WindowManager.h"
#include "ManagedWindow.h"
#include "WindowTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
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

bool snapshotsEqual(const WindowSnapshot& actual, const WindowSnapshot& expected)
{
    return actual.handle == expected.handle && actual.parent == expected.parent
           && actual.title == expected.title && actual.bounds == expected.bounds
           && actual.state == expected.state && actual.visible == expected.visible
           && actual.modal == expected.modal
           && std::abs(actual.opacity - expected.opacity) <= 0.00001f
           && actual.display == expected.display;
}

bool opacityEquals(const float actual, const float expected)
{
    return std::abs(actual - expected) <= 0.00001f;
}

void dispatchMessagesFor(std::chrono::milliseconds duration)
{
    auto* messageManager = juce::MessageManager::getInstance();
    static_cast<void>(messageManager->runDispatchLoopUntil(static_cast<int>(duration.count())));
}

std::unique_ptr<juce::Component> makeContent()
{
    return std::make_unique<juce::Component>();
}

ManagedWindow* findManagedWindow(const char* name)
{
    for (int index = 0; index < juce::TopLevelWindow::getNumTopLevelWindows(); ++index) {
        auto* topLevelWindow = juce::TopLevelWindow::getTopLevelWindow(index);
        auto* candidate = dynamic_cast<ManagedWindow*>(topLevelWindow);
        if (candidate != nullptr && candidate->getName() == juce::String(name))
            return candidate;
    }
    return nullptr;
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
    const auto managerHandle = *handle;

    const auto initialSnapshot = manager.getWindow(*handle);
    expect(initialSnapshot.has_value(), "created window has a snapshot");
    if (initialSnapshot) {
        expect(initialSnapshot->handle == *handle, "snapshot reports its handle");
        expect(initialSnapshot->title == "Test Window", "snapshot reports its title");
        expect(initialSnapshot->bounds == WindowBounds { 80, 80, 640, 480 },
               "snapshot reports initial bounds");
        expect(initialSnapshot->visible, "created window is visible");
        expect(initialSnapshot->display.has_value(),
               "created window reports its initial display");
    }

    expect(manager.resizeWindow(*handle, { 120, 100, 800, 600 }),
           "resizing a real window succeeds");
    const auto resizedSnapshot = manager.getWindow(*handle);
    expect(resizedSnapshot.has_value(), "resized window has a snapshot");
    if (resizedSnapshot)
        expect(resizedSnapshot->bounds == WindowBounds { 120, 100, 800, 600 },
               "snapshot reports resized bounds");

    const auto beforeInvalidHandle = manager.getWindow(managerHandle);
    expect(beforeInvalidHandle.has_value(), "valid snapshot exists before invalid-handle calls");
    constexpr WindowHandle invalidHandle = 0;
    expect(!manager.getWindow(invalidHandle).has_value(),
           "invalid handle lookup returns no snapshot");
    expect(!manager.closeWindow(invalidHandle), "invalid handle close fails");
    expect(!manager.resizeWindow(invalidHandle, { 20, 20, 200, 160 }),
           "invalid handle resize fails");
    expect(!manager.setWindowState(invalidHandle, WindowState::minimized),
           "invalid handle state update fails");
    expect(!manager.setWindowOpacity(invalidHandle, 0.5f),
           "invalid handle opacity update fails");
    expect(!manager.moveToDisplay(invalidHandle, 1),
           "invalid handle display move fails");
    const auto afterInvalidHandle = manager.getWindow(managerHandle);
    expect(afterInvalidHandle.has_value() && beforeInvalidHandle.has_value()
               && snapshotsEqual(*afterInvalidHandle, *beforeInvalidHandle),
           "invalid-handle calls leave the valid snapshot unchanged");

    const auto beforeUnknownDisplay = manager.getWindow(managerHandle);
    expect(beforeUnknownDisplay.has_value(),
           "valid snapshot exists before unknown-display calls");
    constexpr DisplayId unknownDisplay = std::numeric_limits<DisplayId>::max();
    const auto displays = manager.getDisplays();
    expect(std::none_of(displays.begin(), displays.end(), [unknownDisplay](const auto& display) {
        return display.id == unknownDisplay;
    }), "chosen display id is unknown");
    expect(!manager.moveToDisplay(managerHandle, unknownDisplay),
           "unknown display move fails");
    expect(!manager.logicalToPhysical(unknownDisplay, { 0, 0, 100, 100 }).has_value(),
           "unknown display logical conversion fails");
    expect(!manager.physicalToLogical(unknownDisplay, { 0, 0, 100, 100 }).has_value(),
           "unknown display physical conversion fails");
    const auto afterUnknownDisplay = manager.getWindow(managerHandle);
    expect(afterUnknownDisplay.has_value() && beforeUnknownDisplay.has_value()
               && snapshotsEqual(*afterUnknownDisplay, *beforeUnknownDisplay),
           "unknown-display calls leave the valid snapshot unchanged");

    auto* managerWindow = findManagedWindow("Test Window");
    expect(managerWindow != nullptr,
           "manager-created window is discoverable as a ManagedWindow");
    if (managerWindow)
        managerWindow->userTriedToCloseWindow();

    expect(closeRequested, "manager-created window forwards a close request");
    expect(requestedHandle == managerHandle,
           "manager-created close request receives its concrete handle");
    expect(closeRequestCount == 1,
           "manager-created close-request callback runs exactly once");
    expect(manager.getWindow(managerHandle).has_value(),
           "close-request callback leaves the manager-created window open");

    expect(manager.closeWindow(managerHandle), "closing a real window succeeds");
    expect(!manager.getWindow(managerHandle).has_value(),
           "closed handle is no longer queryable");
    expect(closeRequestCount == 1,
           "explicit close does not invoke the close-request callback again");

    WindowOptions callbackOptions;
    callbackOptions.title = "Callback Window";
    callbackOptions.bounds = { 100, 100, 320, 240 };
    bool directCloseRequested = false;
    WindowHandle directRequestedHandle = 0;
    int directCloseRequestCount = 0;
    callbackOptions.closeRequested = [&](WindowHandle callbackHandle) {
        directCloseRequested = true;
        directRequestedHandle = callbackHandle;
        ++directCloseRequestCount;
    };
    ManagedWindow managedWindow("Callback Window", juce::Colours::darkgrey,
                                callbackOptions, makeContent(), [&callbackOptions, managerHandle] {
                                    if (callbackOptions.closeRequested)
                                        callbackOptions.closeRequested(managerHandle);
                                }, false);
    managedWindow.userTriedToCloseWindow();

    expect(directCloseRequested, "direct ManagedWindow forwards a close request");
    expect(directRequestedHandle == managerHandle,
           "direct close-request callback receives a concrete window handle");
    expect(directCloseRequestCount == 1,
           "direct ManagedWindow close-request callback runs exactly once");
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

    WindowOptions initialMaximizeOptions;
    initialMaximizeOptions.title = "Initial Maximized Window";
    initialMaximizeOptions.bounds = { 140, 140, 400, 300 };
    initialMaximizeOptions.initialState = WindowState::maximized;
    const auto initiallyMaximized =
        manager.createWindow(makeContent(), initialMaximizeOptions);
    if (capabilities.supportsMaximize) {
        expect(initiallyMaximized.has_value(),
               "supported initial maximize creates a window");
        if (initiallyMaximized) {
            const auto initialSnapshot = manager.getWindow(*initiallyMaximized);
            expect(initialSnapshot.has_value()
                       && initialSnapshot->state == WindowState::maximized,
                   "initial maximize is reported");
        }
    } else {
        expect(!initiallyMaximized,
               "unsupported initial maximize is rejected after peer capability check");
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
        expect(snapshot.has_value() && opacityEquals(snapshot->opacity, 1.0f),
               "opacity is clamped to one");
        expect(lowerOpacityResult, "supported opacity update accepts a lower value");
        snapshot = manager.getWindow(*handle);
        expect(snapshot.has_value() && opacityEquals(snapshot->opacity, 0.0f),
               "opacity is clamped to zero");
    } else {
        expect(!upperOpacityResult, "unsupported opacity update fails");
        expect(!lowerOpacityResult, "unsupported opacity update fails consistently");
    }

    if (frameless)
        static_cast<void>(manager.closeWindow(*frameless));
    if (transparent)
        static_cast<void>(manager.closeWindow(*transparent));
    if (initiallyMaximized)
        static_cast<void>(manager.closeWindow(*initiallyMaximized));
    static_cast<void>(manager.closeWindow(*handle));
}

void testCloseRequestRoutesThroughManager()
{
    WindowManager manager;
    WindowHandle requestedHandle = 0;
    int closeRequestCount = 0;
    bool closeSucceeded = false;

    WindowOptions options;
    options.title = "Close Routing Window";
    options.bounds = { 120, 120, 480, 320 };
    options.closeRequested = [&](const WindowHandle handle) {
        requestedHandle = handle;
        ++closeRequestCount;
        closeSucceeded = manager.closeWindow(handle);
    };

    const auto handle = manager.createWindow(makeContent(), options);
    expect(handle.has_value(), "close-routing window is created");
    if (! handle)
        return;

    auto* managedWindow = findManagedWindow("Close Routing Window");
    expect(managedWindow != nullptr, "close-routing window is discoverable");
    if (managedWindow != nullptr)
        managedWindow->userTriedToCloseWindow();

    dispatchMessagesFor(std::chrono::milliseconds(100));
    expect(closeRequestCount == 1, "close-routing callback runs exactly once");
    expect(requestedHandle == *handle, "close-routing callback receives its handle");
    expect(closeSucceeded, "close-routing callback closes through the manager");
    expect(!manager.getWindow(*handle).has_value(),
           "close-routing callback removes the managed window");
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
        expect(manager.closeWindow(*parent), "closing a parent window succeeds");
        expect(!manager.getWindow(*dialog).has_value(),
               "closing a parent closes its modal descendant");
    }

    expect(!manager.getWindow(*parent).has_value(), "closed parent is no longer queryable");
    if (child)
        expect(!manager.getWindow(*child).has_value(), "closing a parent closes its child");

    WindowOptions escapeParentOptions;
    escapeParentOptions.title = "Escape Parent";
    escapeParentOptions.bounds = { 180, 180, 640, 480 };
    const auto escapeParent = manager.createWindow(makeContent(), escapeParentOptions);
    expect(escapeParent.has_value(), "escape test parent is created");
    if (!escapeParent)
        return;

    int escapeCompletionCount = 0;
    DialogOptions escapeOptions;
    escapeOptions.window.title = "Escape Dialog";
    escapeOptions.window.parent = *escapeParent;
    escapeOptions.closeOnEscape = true;
    escapeOptions.completion = [&](int) { ++escapeCompletionCount; };
    const auto escapeDialog = manager.showModalDialog(makeContent(), escapeOptions);
    expect(escapeDialog.has_value(), "escape-enabled dialog is created");
    if (escapeDialog) {
        auto* escapeWindow = findManagedWindow("Escape Dialog");
        expect(escapeWindow != nullptr, "escape-enabled dialog is discoverable");
        if (escapeWindow)
            expect(escapeWindow->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
                   "escape closes an escape-enabled dialog");
        if (escapeWindow)
            expect(!escapeWindow->isCurrentlyModal(false),
                   "escape exits the dialog modal state");
    }

    int noEscapeCompletionCount = 0;
    DialogOptions noEscapeOptions;
    noEscapeOptions.window.title = "No Escape Dialog";
    noEscapeOptions.window.parent = *escapeParent;
    noEscapeOptions.closeOnEscape = false;
    noEscapeOptions.completion = [&](int) { ++noEscapeCompletionCount; };
    const auto noEscapeDialog = manager.showModalDialog(makeContent(), noEscapeOptions);
    expect(noEscapeDialog.has_value(), "escape-disabled dialog is created");
    if (noEscapeDialog) {
        auto* noEscapeWindow = findManagedWindow("No Escape Dialog");
        expect(noEscapeWindow != nullptr, "escape-disabled dialog is discoverable");
        if (noEscapeWindow)
            expect(!noEscapeWindow->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),
                   "escape does not close an escape-disabled dialog");
        expect(manager.getWindow(*noEscapeDialog).has_value(),
               "escape-disabled dialog remains open");
        expect(manager.closeWindow(*noEscapeDialog), "escape-disabled dialog closes explicitly");

        dispatchMessagesFor(std::chrono::milliseconds(100));
        expect(completionCount == 1, "parent-close modal completion runs exactly once");
        expect(completionResult == 0, "parent-close modal completes with result zero");
        expect(escapeCompletionCount == 1,
               "escape-enabled dialog completion runs exactly once");
        expect(!manager.getWindow(*escapeDialog).has_value(),
               "escape closes the dialog handle");
        expect(noEscapeCompletionCount == 1,
               "escape-disabled dialog completion runs exactly once");
    }
    expect(manager.closeWindow(*escapeParent), "escape test parent closes");
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
    testCloseRequestRoutesThroughManager();
    testWindowStatesAndCapabilities();
    testChildCascadeAndModalCompletion();
    testDisplaySnapshotAndConversion();
    return failures == 0 ? 0 : 1;
}
