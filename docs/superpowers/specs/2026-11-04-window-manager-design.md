# DAWg Window Manager — Architecture Spec

**Date:** 2026-11-04  
**Status:** Draft for review  
**Scope:** DAWg application shell and reusable window-management contracts

## 1. Goal

Give DAWg a single application-owned window manager that can create, resize,
close, position, and control desktop windows and asynchronous modal dialogs.
The subsystem must handle multi-monitor layouts and DPI scaling while keeping
the audio engine independent of JUCE and desktop windowing.

The window manager is application/platform infrastructure, not part of the
real-time sound engine. DAWg may deeply integrate the current `dawg::engine`
target as its primary sound engine, but the engine remains a separately usable
library so future projects can reuse it without a rewrite.

## 2. Scope

### In scope

- Create and own top-level application windows.
- Resize windows directly and enforce minimum/maximum dimensions.
- Close windows from code and route native close-button requests.
- Minimized, maximized, fullscreen, and normal window states.
- Native-title-bar and truly frameless windows.
- Opaque, transparent, and semi-transparent window presentation.
- Always-on-top windows.
- Multi-monitor display snapshots, usable work areas, scale factors, and DPI.
- Logical-to-physical and physical-to-logical display conversion helpers.
- Placement on a selected monitor with work-area constraints.
- Parent/child window ownership within the application.
- Non-blocking modal dialogs associated with a parent window.
- DAWg main-window integration.
- Unit and integration tests registered with CTest.

### Out of scope

- Persistence of window geometry across application launches.
- A plugin-host window bridge or embedding foreign native windows.
- Custom operating-system title bars and window chrome design.
- Window management inside the audio callback or audio engine.
- A general-purpose UI framework replacing JUCE components.

## 3. Architectural boundary

The repository keeps the following dependency direction:

```text
DAWg application shell
  ├── dawg::app::window::WindowManager (JUCE desktop integration)
  ├── dawg::app::audio::AudioAdapter   (JUCE device integration)
  └── dawg::engine                    (GUI-independent audio core)
```

`dawg::engine` must not include JUCE, depend on `WindowManager`, or know that
DAWg exists. The window manager is permitted to include JUCE because it is an
application-layer subsystem. Keeping the concrete window code in DAWg avoids
polluting the reusable audio library while still allowing DAWg to configure the
engine deeply and directly.

All window-manager operations are message-thread operations. The manager must
assert or fail cleanly when called from another thread; it must never marshal
work synchronously from the audio thread. No window object may be created,
destroyed, or mutated by the audio callback.

## 4. Public model and API

The public application-layer types live under `dawg::app::window`.

### 4.1 Handles and states

```cpp
using WindowHandle = std::uint64_t;
using DisplayId = std::uint32_t;

enum class WindowState {
    normal,
    minimized,
    maximized,
    fullscreen,
};
```

Handles are opaque, monotonically allocated identifiers owned by one
`WindowManager`. `0` is reserved as the invalid handle. A closed handle is no
longer valid and must not be silently reused while the manager is alive.

### 4.2 Window options

`WindowOptions` describes how a top-level window is created:

```cpp
struct WindowBounds {
    int x { 0 };
    int y { 0 };
    int width { 0 };
    int height { 0 };
};

struct WindowOptions {
    std::string title;
    WindowBounds bounds { 0, 0, 900, 560 };
    int minimumWidth { 320 };
    int minimumHeight { 200 };
    int maximumWidth { 0 };
    int maximumHeight { 0 };
    bool resizable { true };
    bool useNativeTitleBar { true };
    bool transparent { false };
    float opacity { 1.0f };
    bool alwaysOnTop { false };
    bool visible { true };
    WindowState initialState { WindowState::normal };
    WindowHandle parent {};
};
```

`maximumWidth` and `maximumHeight` equal to `0` mean unlimited. Bounds are
logical desktop coordinates, matching JUCE's `userBounds` coordinates. The
manager validates dimensions and clamps them to the configured limits before
creating or resizing a window.

### 4.3 Dialog options

Dialogs use a separate options type because they have modal behavior:

```cpp
struct DialogOptions {
    WindowOptions window;
    bool closeOnEscape { true };
    std::function<void(int)> completion;
};
```

Dialogs are launched asynchronously. The completion callback receives the
dialog result and runs on the message thread. A dialog must be closable by its
close button, Escape when enabled, its parent being closed, or an explicit
`closeWindow` call.

### 4.4 Window manager operations

The manager exposes these operations:

```cpp
class WindowManager final {
public:
    [[nodiscard]] std::optional<WindowHandle> createWindow(
        std::unique_ptr<juce::Component> content,
        WindowOptions options = {});

    [[nodiscard]] std::optional<WindowHandle> showModalDialog(
        std::unique_ptr<juce::Component> content,
        DialogOptions options = {});

    [[nodiscard]] bool closeWindow(WindowHandle);
    [[nodiscard]] bool resizeWindow(WindowHandle, WindowBounds);
    [[nodiscard]] bool setWindowState(WindowHandle, WindowState);
    [[nodiscard]] bool setWindowOpacity(WindowHandle, float);
    [[nodiscard]] bool moveToDisplay(WindowHandle, DisplayId);

    [[nodiscard]] std::optional<WindowSnapshot> getWindow(WindowHandle) const;
    [[nodiscard]] WindowCapabilities getCapabilities() const noexcept;
    [[nodiscard]] std::vector<DisplayInfo> getDisplays() const;
    [[nodiscard]] bool refreshDisplays();
    [[nodiscard]] std::optional<WindowBounds> logicalToPhysical(
        DisplayId, WindowBounds) const;
    [[nodiscard]] std::optional<WindowBounds> physicalToLogical(
        DisplayId, WindowBounds) const;
};
```

`WindowSnapshot` reports the handle, parent, title, logical bounds, current
state, visibility, opacity, and monitor identity. `DisplayInfo` reports a
stable-in-snapshot display id, primary status, logical user bounds, logical
total bounds, physical bounds, scale factor, and DPI.

`WindowCapabilities` contains four boolean capabilities:

```cpp
struct WindowCapabilities {
    bool supportsTransparency { false };
    bool supportsFramelessWindows { true };
    bool supportsFullscreen { true };
    bool supportsPerWindowOpacity { false };
};
```

`WindowCapabilities` reports whether the current platform backend supports
transparent windows, frameless windows, fullscreen, and per-window opacity.
The conversion helpers return `std::nullopt` for an unknown display id and
delegate to JUCE's display-aware conversion functions.

Operations on an invalid handle return `false` or `std::nullopt` and do not
throw. Creating a window with a null content component fails. Unsupported
platform capabilities are reported through a `WindowCapabilities` query and
must not be silently represented as a successful state transition.

## 5. JUCE implementation

### 5.1 Managed windows

`ManagedWindow` is an internal `juce::ResizableWindow` subclass. It owns the
content component and forwards `userTriedToCloseWindow()` to the manager.

- Native-title-bar windows use JUCE's native title-bar setting.
- Frameless windows use the non-native-title-bar peer configuration and do not
  add a JUCE title bar component.
- Transparent windows use a non-opaque background and set the peer alpha using
  JUCE's supported component/peer APIs.
- Resizing uses `setResizable` and a `ComponentBoundsConstrainer` configured
  from `WindowOptions`.
- Minimized, fullscreen, and restoration behavior delegates to JUCE's
  `ResizableWindow` state methods.

The manager keeps `std::unique_ptr<ManagedWindow>` instances in a registry
keyed by `WindowHandle`. Destruction always removes the registry entry before
the JUCE object is destroyed so callbacks cannot observe a stale handle.

### 5.2 Parent and child windows

The manager-level parent relationship determines ownership, centering, close
cascade, and z-order. Child windows are still independent desktop windows,
not nested audio/editor components. A child closes automatically when its
parent closes. A missing parent at creation time is an invalid request rather
than an implicit attachment to the active window.

### 5.3 Modal dialogs

Dialogs use `DialogOptions::window.parent` as their parent relationship and
JUCE's asynchronous modal-state APIs. The manager must not call a
blocking modal loop. The parent window is brought to the front before launch,
the dialog is centered around it when no explicit bounds were supplied, and
the completion callback is invoked exactly once before the dialog registry
entry is removed.

### 5.4 Monitor and DPI handling

`getDisplays()` and `refreshDisplays()` read JUCE's current display snapshot.
All public bounds are logical/user coordinates. Conversion helpers delegate to
JUCE's display-aware physical/logical conversion functions and require an
explicit `DisplayId` when a scale factor must be forced.

`moveToDisplay` selects the requested display, preserves the window's logical
size, and clamps its position to that display's user area. If the display id
is absent from the current snapshot, the operation fails without moving the
window. On a headless system, the display list is empty and windows may still
be created with explicit bounds, but display-relative placement fails.

## 6. DAWg integration

`DawApplication` owns a `WindowManager` for the application lifetime. During
initialization it creates the main DAW window with:

- the existing `MainComponent` as content;
- native title bar enabled;
- resizable bounds with the current 900x560 default;
- a minimum size suitable for the current status UI;
- close handling that requests application shutdown.

`shutdown()` closes all managed windows before releasing the audio/UI shell.
The existing `AudioAdapter` lifecycle remains unchanged and continues to be
owned by `MainComponent`; no window operation is added to the audio callback.

## 7. Error handling and invariants

- All public operations are non-throwing at the API boundary.
- Null content, invalid handles, invalid parent handles, invalid display ids,
  and invalid dimensions return failure without mutating existing windows.
- Window ids are unique for the lifetime of a manager.
- A parent close recursively closes child windows and completes active dialogs
  exactly once.
- Window state changes update snapshots after JUCE has accepted the request.
- Opacity is clamped to `[0.0f, 1.0f]` before being applied.
- The audio thread never calls into the manager or JUCE.

## 8. Testing strategy

The current repository has no test target, so this feature adds a small test
layer rather than depending on an untracked test convention.

### Deterministic tests

The `WindowRegistry` is a JUCE-free state model. It uses framework-independent
geometry and display snapshot values from `WindowTypes.h`; the JUCE manager
translates those values to `juce::Rectangle` and desktop peers. Its tests cover:

- handles begin at a non-zero value and are not reused;
- invalid handles fail safely;
- parent/child relationships close descendants;
- modal completion is delivered once;
- bounds are clamped to minimum and maximum dimensions;
- display placement preserves size and clamps to the user area;
- logical/physical conversion uses the requested display scale.

The registry accepts injected display snapshots and does not require a visible
desktop window. JUCE-specific window behavior is tested separately.

### JUCE integration smoke tests

Message-thread tests cover:

- creating and closing a real managed window;
- native-title-bar and frameless creation paths;
- transparent-window configuration;
- direct resize and state changes;
- asynchronous modal dialog creation and completion;
- main-window close routing.

The integration tests must skip cleanly on a headless environment when a real
desktop peer cannot be created, while deterministic tests remain mandatory.

CTest will register the deterministic suite and the integration suite under a
feature-specific test label.

## 9. Files

### DAWg application

- Create: `Source/Window/WindowTypes.h`
- Create: `Source/Window/WindowRegistry.h`
- Create: `Source/Window/WindowRegistry.cpp`
- Create: `Source/Window/WindowManager.h`
- Create: `Source/Window/WindowManager.cpp`
- Create: `Source/Window/ManagedWindow.h`
- Create: `Source/Window/ManagedWindow.cpp`
- Modify: `Source/App/Main.cpp`
- Modify: `CMakeLists.txt`

### Tests and build support

- Create: `Tests/WindowRegistryTests.cpp`
- Create: `Tests/WindowManagerIntegrationTests.cpp`
- Modify: `CMakeLists.txt` to add the test option, targets, and CTest entries.

No files under `../VersaTone/engine` are required for this window-manager
feature. The engine remains available as the DAWg audio core through the
existing `dawg_engine` target.

## 10. Acceptance criteria

The feature is complete when:

1. The main DAW window is created and closed through `WindowManager`.
2. Application code can create, resize, close, and query multiple windows.
3. All requested window states are supported through one API.
4. Frameless and transparent options are applied through JUCE peer settings.
5. Display snapshots include monitor bounds, work areas, scale, and DPI.
6. A window can be moved to another display while preserving logical size.
7. Child windows close with their parent.
8. Modal dialogs are asynchronous and complete exactly once.
9. The audio engine remains JUCE-free and unchanged by window operations.
10. Deterministic tests pass, and integration tests pass or skip only when the
    environment has no usable desktop peer.
