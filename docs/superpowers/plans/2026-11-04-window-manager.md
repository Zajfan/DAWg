# DAWg Window Manager Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved DAWg window manager with multi-monitor/DPI support, window states, child windows, asynchronous modal dialogs, and main-window integration without coupling the reusable audio engine to JUCE windowing.

**Architecture:** Keep `dawg::engine` unchanged and GUI-independent. Add a JUCE-backed `dawg::app::window::WindowManager` with a JUCE-free `WindowRegistry` model underneath it, plus a small native-state adapter for programmatic maximize. DAWg's `DawApplication` owns the manager and creates the main window through it.

**Tech Stack:** C++26, CMake 3.25+, JUCE 9.0.4, CTest, platform-native window APIs for maximize state (Win32, AppKit, X11/EWMH).

**Spec:** `docs/superpowers/specs/2026-11-04-window-manager-design.md`

## Global Constraints

- The engine tier (`dawg::engine::`) must have zero JUCE/Qt includes.
- All window-manager operations are message-thread operations; the audio callback never creates, destroys, or mutates windows.
- Public window coordinates use logical/user pixels; physical conversions require an explicit display id.
- Invalid handles, invalid parents, invalid displays, null content, unsupported capabilities, and invalid dimensions fail without mutating existing windows.
- The manager must use asynchronous modal APIs and must not run a blocking modal loop.
- The existing C++26, zero-warning, and debug-sanitizer policies remain enabled for DAWg sources.
- `DAWG_BUILD_TESTS` defaults to `OFF`; deterministic and JUCE integration tests are registered with CTest when enabled.
- Every production behavior is preceded by a failing test and verified with the focused test before moving on.

---

## File Map

### Window model and backend

- Create: `Source/Window/WindowTypes.h` — framework-independent handles, bounds, display snapshots, options, and capability records.
- Create: `Source/Window/WindowRegistry.h` — JUCE-free lifecycle, relationship, bounds, state, and callback model.
- Create: `Source/Window/WindowRegistry.cpp` — registry implementation and display math.
- Create: `Source/Window/ManagedWindow.h` — internal JUCE top-level window wrapper.
- Create: `Source/Window/ManagedWindow.cpp` — peer configuration, content ownership, and close forwarding.
- Create: `Source/Window/WindowManager.h` — DAWg-facing window API.
- Create: `Source/Window/WindowManager.cpp` — registry/backend orchestration.
- Create: `Source/Window/NativeWindowState.h` — cross-platform maximize capability/query/set contract.
- Create: `Source/Window/NativeWindowState_windows.cpp` — Win32 maximize implementation.
- Create: `Source/Window/NativeWindowState_mac.mm` — AppKit maximize implementation.
- Create: `Source/Window/NativeWindowState_linux.cpp` — X11/EWMH maximize implementation.
- Create: `Source/Window/NativeWindowState_fallback.cpp` — unsupported-platform capability response.
- Modify: `CMakeLists.txt` — application sources, platform source selection, and optional tests.
- Modify: `Source/App/Main.cpp` — create and close the main window through `WindowManager`.

### Tests

- Create: `Tests/WindowRegistryTests.cpp` — deterministic model tests with no desktop peer.
- Create: `Tests/WindowManagerIntegrationTests.cpp` — JUCE message-thread smoke tests.

## Task 1: Write the failing JUCE-free registry tests

**Files:**
- Create: `Tests/WindowRegistryTests.cpp`

**Interfaces:**
- Consumes: the planned `dawg::app::window::WindowBounds`, `WindowSizeLimits`, `WindowRegistration`, `DisplayInfo`, and `WindowRegistry` interfaces defined below.
- Produces: executable test cases that fail because `Source/Window/WindowRegistry.h` does not exist yet.

The registry interface used by the tests is:

```cpp
namespace dawg::app::window {

class WindowRegistry final {
public:
    [[nodiscard]] std::optional<WindowHandle> registerWindow(WindowRegistration);
    [[nodiscard]] bool contains(WindowHandle) const noexcept;
    [[nodiscard]] std::optional<WindowSnapshot> snapshot(WindowHandle) const;
    [[nodiscard]] bool resize(WindowHandle, WindowBounds);
    [[nodiscard]] bool setState(WindowHandle, WindowState);
    [[nodiscard]] bool setOpacity(WindowHandle, float);
    [[nodiscard]] std::vector<ClosedWindow> removeTree(WindowHandle);
    [[nodiscard]] bool moveToDisplay(WindowHandle, const DisplayInfo&);

    [[nodiscard]] static WindowBounds logicalToPhysical(
        const DisplayInfo&, WindowBounds);
    [[nodiscard]] static WindowBounds physicalToLogical(
        const DisplayInfo&, WindowBounds);
};

} // namespace dawg::app::window
```

- [ ] **Step 1: Create the test harness and lifecycle tests**

Write `Tests/WindowRegistryTests.cpp` with a local `expect(bool, string_view)` helper that counts failures and returns `1` from `main()` when any expectation fails. Add these tests before any production window source exists:

```cpp
void handlesAreUniqueAndParentCloseCascades();
void resizingClampsToRegisteredLimits();
void invalidHandlesDoNotMutateState();
void displayPlacementPreservesSizeAndClampsPosition();
void modalCompletionIsReturnedOnlyOnce();
void displayConversionUsesScaleAndOrigins();
```

Each test must create a fresh `WindowRegistry`. Use a helper registration with bounds `{ 10, 20, 640, 480 }`, limits `{ 320, 200, 1280, 900 }`, normal state, visible `true`, and opacity `1.0f`. Register one parent and one child for the cascade test; assert that both handles are non-zero and distinct, that the child records the parent, and that `removeTree(parent)` returns both handles while a second removal returns an empty vector.

For the bounds test, call `resize(handle, { 0, 0, 100, 100 })` and assert the snapshot is `{ 0, 0, 320, 200 }`, then call `resize(handle, { 0, 0, 2000, 2000 })` and assert `{ 0, 0, 1280, 900 }`.

For invalid handles, assert that `contains(0)`, `resize(0, ...)`, `setState(0, ...)`, `setOpacity(0, ...)`, and `removeTree(0)` fail or return empty, and that the valid window snapshot remains unchanged.

For display placement, use a display with logical user bounds `{ 100, 50, 800, 600 }`, physical bounds `{ 200, 100, 1600, 1200 }`, scale `2.0`, and DPI `192.0`. Register a `{ 0, 0, 300, 200 }` window, move it to that display, and assert its size is unchanged while its position is clamped to `{ 100, 50 }`.

For modal completion, register a modal window with a callback that increments an integer, remove it once, invoke every returned callback with result `7`, remove it again, and assert that the callback count is exactly `1`.

For conversion, assert that logical `{ 110, 70, 300, 200 }` maps to physical `{ 220, 140, 600, 400 }` and maps back to the original logical rectangle.

- [ ] **Step 2: Run the focused test to verify the failure is structural**

Run:

```bash
c++ -std=c++26 -I Source/Window Tests/WindowRegistryTests.cpp -o /tmp/dawg-window-registry-tests
```

Expected: compilation fails because `WindowRegistry.h` and its model types do not exist. Do not add production code in this step.

- [ ] **Step 3: Commit the failing tests**

```bash
git add Tests/WindowRegistryTests.cpp
git commit -m "test(window): define registry behavior before implementation"
```

## Task 2: Implement the JUCE-free window model and deterministic test target

**Files:**
- Create: `Source/Window/WindowTypes.h`
- Create: `Source/Window/WindowRegistry.h`
- Create: `Source/Window/WindowRegistry.cpp`
- Modify: `CMakeLists.txt`
- Test: `Tests/WindowRegistryTests.cpp`

**Interfaces:**
- Consumes: the failing tests and the exact registry signatures from Task 1.
- Produces: `WindowRegistry`, `WindowTypes`, and the `dawg_window_registry_tests` CTest target.

- [ ] **Step 1: Define the framework-independent public types**

Create `Source/Window/WindowTypes.h` with these types in `dawg::app::window`:

```cpp
using WindowHandle = std::uint64_t;
using DisplayId = std::uint32_t;

enum class WindowState { normal, minimized, maximized, fullscreen };

struct WindowBounds {
    int x { 0 };
    int y { 0 };
    int width { 0 };
    int height { 0 };
    friend bool operator==(const WindowBounds&, const WindowBounds&) = default;
};

struct WindowSizeLimits {
    int minimumWidth { 1 };
    int minimumHeight { 1 };
    int maximumWidth { 0 };
    int maximumHeight { 0 };
};

struct DisplayInfo {
    DisplayId id {};
    bool primary { false };
    WindowBounds logicalUserBounds;
    WindowBounds logicalBounds;
    WindowBounds physicalBounds;
    double scale { 1.0 };
    double dpi { 96.0 };
};

struct WindowSnapshot {
    WindowHandle handle {};
    WindowHandle parent {};
    std::string title;
    WindowBounds bounds;
    WindowState state { WindowState::normal };
    bool visible { false };
    bool modal { false };
    float opacity { 1.0f };
    std::optional<DisplayId> display;
};

struct WindowCapabilities {
    bool supportsTransparency { false };
    bool supportsFramelessWindows { true };
    bool supportsFullscreen { true };
    bool supportsPerWindowOpacity { false };
    bool supportsMaximize { false };
};
```

Also define `WindowOptions`, `DialogOptions`, `WindowRegistration`, and `ClosedWindow` exactly as follows: `WindowOptions` contains the spec's title, bounds, `minimumWidth`, `minimumHeight`, `maximumWidth`, `maximumHeight`, resizable/native-title-bar/transparent/always-on-top/visible flags, initial state, parent, opacity, and `std::function<void(WindowHandle)> closeRequested`; `DialogOptions` contains `WindowOptions window`, `bool closeOnEscape`, and `std::function<void(int)> completion`; `WindowRegistration` contains a `WindowSnapshot snapshot`, a `WindowSizeLimits limits`, and `std::function<void(int)> completion`; `ClosedWindow` contains a `WindowHandle handle` and a move-only `std::function<void(int)> completion`.

Include only standard headers in this file: `<cstdint>`, `<functional>`, `<optional>`, `<string>`, and `<vector>`.

- [ ] **Step 2: Implement handle allocation, parent validation, and snapshots**

Create `WindowRegistry.h/.cpp`. Start `m_nextHandle` at `1`, allocate monotonically, reject a non-zero parent that is not already registered, and store records in `std::unordered_map<WindowHandle, Record>`. `snapshot()` returns a copy of the stored `WindowSnapshot`; all lookups use `find()` and never insert on read.

- [ ] **Step 3: Implement clamped bounds and state/opacity updates**

`resize()` clamps width and height to the registration limits, with a maximum of `0` meaning unlimited, and preserves the requested x/y. `setState()` writes only for an existing handle. `setOpacity()` clamps to `[0.0f, 1.0f]` and writes only for an existing handle.

- [ ] **Step 4: Implement close-tree and display math**

`removeTree()` collects descendants before parents, moves each modal completion callback into the returned `ClosedWindow`, erases every record, and returns an empty vector for an invalid handle. `moveToDisplay()` preserves width and height and clamps x/y into the target display's logical user bounds; if the window is larger than the user area, use the area's top-left and preserve the requested size.

Implement conversion with the display origins and scale:

```cpp
physical.x = display.physicalBounds.x
            + round((logical.x - display.logicalUserBounds.x) * display.scale);
physical.y = display.physicalBounds.y
            + round((logical.y - display.logicalUserBounds.y) * display.scale);
physical.width = round(logical.width * display.scale);
physical.height = round(logical.height * display.scale);
```

The inverse operation subtracts the physical origin, divides by the same scale, and rounds to the nearest integer. Treat a non-positive scale as `1.0`.

- [ ] **Step 5: Add the deterministic CTest target**

Add this block to `CMakeLists.txt` after the DAW target definition:

```cmake
option(DAWG_BUILD_TESTS "Build DAWg tests" OFF)

if(DAWG_BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_executable(dawg_window_registry_tests
        Tests/WindowRegistryTests.cpp
        Source/Window/WindowRegistry.cpp
    )
    target_include_directories(dawg_window_registry_tests PRIVATE Source)
    target_compile_features(dawg_window_registry_tests PRIVATE cxx_std_26)
    target_compile_options(dawg_window_registry_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    )
    add_test(NAME dawg.window_registry COMMAND dawg_window_registry_tests)
    set_tests_properties(dawg.window_registry PROPERTIES LABELS "window-manager")
endif()
```

- [ ] **Step 6: Run the focused test and build target**

Run:

```bash
cmake -S . -B build-window-tests -DCMAKE_BUILD_TYPE=Debug -DDAWG_BUILD_TESTS=ON
cmake --build build-window-tests --target dawg_window_registry_tests --parallel 2
ctest --test-dir build-window-tests -R dawg.window_registry --output-on-failure
```

Expected: the registry test passes and no warning is emitted by the test target.

- [ ] **Step 7: Commit the model and tests**

```bash
git add CMakeLists.txt Source/Window Tests/WindowRegistryTests.cpp
git commit -m "feat(window): add JUCE-free window registry model"
```

## Task 3: Write failing JUCE window-manager integration tests

**Files:**
- Create: `Tests/WindowManagerIntegrationTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `WindowTypes` and the public `WindowManager` API from the spec.
- Produces: failing integration tests for creation, resize, close, state, display/DPI, child, modal, opacity, and close-request behavior.

- [ ] **Step 1: Add the integration test executable skeleton**

Create a test executable with a local `expect()` helper and this main setup:

```cpp
int main() {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        return 77;

    testCreateResizeAndClose();
    testWindowStatesAndCapabilities();
    testChildCascadeAndModalCompletion();
    testDisplaySnapshotAndConversion();
    return failures == 0 ? 0 : 1;
}
```

The tests must return `77` only when no desktop display exists; CTest will mark that result skipped. All other failures return `1`.

- [ ] **Step 2: Write creation, resize, close, and callback tests**

Create `WindowOptions` with title `"Test Window"`, bounds `{ 80, 80, 640, 480 }`, `minimumWidth = 320`, `minimumHeight = 200`, native title bar enabled, and a close-request callback that sets a boolean and records the handle. Create a `std::unique_ptr<juce::Component>`, assert a handle is returned, assert the snapshot title/bounds/visibility, resize to `{ 120, 100, 800, 600 }`, assert the snapshot, close the handle, and assert the handle is no longer queryable. Construct a `ManagedWindow` directly in this test and call its public `userTriedToCloseWindow()` override to exercise the callback exactly once.

- [ ] **Step 3: Write state and capability tests**

Create a real window and test normal-to-minimized and minimized-to-normal with `setWindowState`. Test fullscreen and restore. For maximize, branch on `getCapabilities().supportsMaximize`: when true, `setWindowState(handle, WindowState::maximized)` must succeed and the snapshot state must be maximized; when false, the operation must return `false` and leave the state unchanged. Create frameless and transparent windows; unsupported transparency must return failure rather than silently creating an opaque window. Test opacity clamping through `setWindowOpacity(handle, 2.0f)` and `setWindowOpacity(handle, -1.0f)`.

- [ ] **Step 4: Write child, modal, and display/DPI tests**

Create a parent and child window with `childOptions.parent = *parent`, close the parent, and assert the child is gone. Create a dialog with `DialogOptions::window.parent = *parent`, completion incrementing a counter, close it through the manager, dispatch pending JUCE messages, and assert completion ran exactly once. Read `getDisplays()`, assert each display has positive scale and DPI, and when a display exists, assert `logicalToPhysical` and `physicalToLogical` round-trip a rectangle within one logical pixel.

- [ ] **Step 5: Run the focused integration build to verify it fails for the missing backend**

Add the test source to a temporary executable target linked to `juce::juce_gui_basics`, then run:

```bash
cmake -S . -B build-window-tests -DCMAKE_BUILD_TYPE=Debug -DDAWG_BUILD_TESTS=ON
cmake --build build-window-tests --target dawg_window_manager_integration_tests --parallel 2
```

Expected: compilation fails because `Source/Window/WindowManager.h` and the backend implementation do not exist yet. Do not implement the manager in this task.

- [ ] **Step 6: Commit the failing integration tests**

```bash
git add CMakeLists.txt Tests/WindowManagerIntegrationTests.cpp
git commit -m "test(window): define JUCE manager integration behavior"
```

## Task 4: Implement the JUCE manager, managed windows, and native maximize adapter

**Files:**
- Create: `Source/Window/ManagedWindow.h`
- Create: `Source/Window/ManagedWindow.cpp`
- Create: `Source/Window/WindowManager.h`
- Create: `Source/Window/WindowManager.cpp`
- Create: `Source/Window/NativeWindowState.h`
- Create: `Source/Window/NativeWindowState_windows.cpp`
- Create: `Source/Window/NativeWindowState_mac.mm`
- Create: `Source/Window/NativeWindowState_linux.cpp`
- Create: `Source/Window/NativeWindowState_fallback.cpp`
- Modify: `CMakeLists.txt`
- Test: `Tests/WindowManagerIntegrationTests.cpp`

**Interfaces:**
- Consumes: `WindowRegistry`, `WindowTypes`, and all failing integration tests from Task 3.
- Produces: a working `dawg::app::window::WindowManager` with the public methods in the approved spec.

- [ ] **Step 1: Define the native-state seam**

Create `NativeWindowState.h` with:

```cpp
namespace dawg::app::window::native {

[[nodiscard]] WindowCapabilities queryCapabilities(
    const juce::ComponentPeer*) noexcept;

[[nodiscard]] bool setMaximized(
    juce::ComponentPeer&, bool shouldBeMaximized) noexcept;

[[nodiscard]] bool isMaximized(const juce::ComponentPeer&) noexcept;

} // namespace dawg::app::window::native
```

Implement platform selection in CMake. Windows casts `peer.getNativeHandle()` to `HWND` and calls `ShowWindow(hwnd, SW_MAXIMIZE)` or `ShowWindow(hwnd, SW_RESTORE)`, then uses `IsZoomed(hwnd)`. macOS uses the `NSWindow*` native handle and `zoom:`/`isZoomed`. Linux uses an X11 `Window` handle and the EWMH `_NET_WM_STATE_MAXIMIZED_HORZ` and `_NET_WM_STATE_MAXIMIZED_VERT` atoms; return unsupported when no X11 peer is available. The fallback implementation returns `supportsMaximize = false` and `false` from the maximize operations.

- [ ] **Step 2: Define `ManagedWindow`**

Create a `juce::ResizableWindow` subclass with this constructor and callback:

```cpp
class ManagedWindow final : public juce::ResizableWindow {
public:
    using CloseRequested = std::function<void()>;

    ManagedWindow(const juce::String&, juce::Colour, WindowOptions,
                  std::unique_ptr<juce::Component>, CloseRequested);
    ~ManagedWindow() override;

    void userTriedToCloseWindow() override;
};
```

The constructor must set native-title-bar mode, opaque/transparent background, alpha, always-on-top, resizable limits, content ownership, bounds, and visibility from `WindowOptions`. `userTriedToCloseWindow()` invokes the stored callback once and never directly destroys the window.

- [ ] **Step 3: Define `WindowManager` ownership and message-thread checks**

Create `WindowManager.h` with the exact public methods from the spec and private members:

```cpp
WindowRegistry m_registry;
std::unordered_map<WindowHandle, std::unique_ptr<ManagedWindow>> m_windows;
WindowCapabilities m_capabilities;
std::vector<DisplayInfo> m_displays;
```

Every public mutating method calls a private `isMessageThread()` check using JUCE's `MessageManager`; on failure it returns `false`/`std::nullopt` and leaves state untouched. The constructor refreshes displays and queries native capabilities; the destructor closes all windows on the message thread.

- [ ] **Step 4: Implement top-level creation and close routing**

`createWindow()` rejects null content and missing parents, validates capabilities before applying transparent/frameless options, registers the model first, creates `ManagedWindow`, and rolls the registry entry back if peer creation fails. A native close request invokes `WindowOptions::closeRequested` when provided; otherwise it calls `closeWindow(handle)`.

`closeWindow()` removes non-modal descendants from the registry in child-first order, destroys the matching `ManagedWindow` objects, and invokes returned completion callbacks once. For modal windows, call `exitModalState(0)` and let the modal callback complete the removal through one guarded `finishModal()` path so callback delivery and destruction cannot happen twice.

- [ ] **Step 5: Implement resize, opacity, state, and monitor operations**

`resizeWindow()` delegates clamping to the registry, then calls `setBounds` on the managed JUCE window. `setWindowOpacity()` clamps through the registry and calls `setAlpha` when the backend supports per-window opacity. `setWindowState()` maps normal/minimized/fullscreen to JUCE's `ResizableWindow` methods and maximized to `native::setMaximized`; it updates the registry only after the operation succeeds.

`refreshDisplays()` maps JUCE `Displays::Display` records to `DisplayInfo` using logical user bounds, logical total bounds, physical bounds, scale, DPI, and a snapshot-local id. `moveToDisplay()` uses the registry's clamped logical bounds and applies them to the managed window. The conversion methods resolve the requested display from `m_displays` and call the registry conversion helpers.

- [ ] **Step 6: Add platform-aware CMake source selection**

Inside the `DAWG_BUILD_TESTS` block, add the integration target and sources:

```cmake
    add_executable(dawg_window_manager_integration_tests
        Tests/WindowManagerIntegrationTests.cpp
        Source/Window/WindowRegistry.cpp
        Source/Window/ManagedWindow.cpp
        Source/Window/WindowManager.cpp
    )
    target_include_directories(dawg_window_manager_integration_tests PRIVATE Source)
    target_compile_features(dawg_window_manager_integration_tests PRIVATE cxx_std_26)
    target_link_libraries(dawg_window_manager_integration_tests PRIVATE
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    if(WIN32)
        target_sources(dawg_window_manager_integration_tests PRIVATE Source/Window/NativeWindowState_windows.cpp)
    elseif(APPLE)
        target_sources(dawg_window_manager_integration_tests PRIVATE Source/Window/NativeWindowState_mac.mm)
    elseif(UNIX)
        target_sources(dawg_window_manager_integration_tests PRIVATE Source/Window/NativeWindowState_linux.cpp)
    else()
        target_sources(dawg_window_manager_integration_tests PRIVATE Source/Window/NativeWindowState_fallback.cpp)
    endif()
    add_test(NAME dawg.window_manager_integration COMMAND dawg_window_manager_integration_tests)
    set_tests_properties(dawg.window_manager_integration PROPERTIES
        LABELS "window-manager"
        SKIP_RETURN_CODE 77
    )
```

Add the same window and platform sources to `DAWg`'s `target_sources` list so the application and integration test compile the same implementation.

- [ ] **Step 7: Run focused tests and fix only implementation failures**

Run:

```bash
cmake --build build-window-tests --target dawg_window_manager_integration_tests --parallel 2
ctest --test-dir build-window-tests -R 'dawg.window_(registry|manager_integration)' --output-on-failure
```

Expected: deterministic tests pass; integration tests pass on a usable desktop or are reported as skipped with return code `77` on a headless environment.

- [ ] **Step 8: Commit the backend**

```bash
git add CMakeLists.txt Source/Window Tests/WindowManagerIntegrationTests.cpp
git commit -m "feat(window): add JUCE window manager and native state support"
```

## Task 5: Route the DAW main window through the manager

**Files:**
- Modify: `Source/App/Main.cpp`
- Modify: `CMakeLists.txt`
- Test: `Tests/WindowManagerIntegrationTests.cpp`

**Interfaces:**
- Consumes: `WindowManager::createWindow`, `WindowManager::closeWindow`, `WindowOptions::closeRequested`, and the existing `MainComponent`.
- Produces: a DAW application whose main window is managed by the new subsystem.

- [ ] **Step 1: Add the main-window close-routing test**

Extend the integration test with a `WindowOptions` callback that records the requested handle and calls `closeWindow` from the test-owned manager. Trigger the managed window's close-request path, dispatch messages, and assert the callback ran once and the handle is closed. This test proves the manager can delegate the application's close decision without bypassing the registry.

- [ ] **Step 2: Replace the nested `DocumentWindow` in `Main.cpp`**

Remove the private `MainWindow` subclass and add these members to `DawApplication`:

```cpp
dawg::app::window::WindowManager m_windowManager;
dawg::app::window::WindowHandle m_mainWindow {};
```

In `initialise()`, create `WindowOptions` with title `getApplicationName().toStdString()`, bounds `{ 0, 0, 900, 560 }`, `minimumWidth = 640`, `minimumHeight = 360`, native title bar enabled, resizable enabled, and a close callback that calls `systemRequestedQuit()`. Call `createWindow(std::make_unique<MainComponent>(), options)` and store the returned handle, asserting that creation succeeded.

In `shutdown()`, close `m_mainWindow` if it is valid, clear the handle, and let `WindowManager` release any remaining managed windows after `MainComponent` has stopped its timer and audio adapter.

- [ ] **Step 3: Build the DAW target and run the close-routing integration test**

Run:

```bash
cmake -S . -B build-window-tests -DCMAKE_BUILD_TYPE=Debug -DDAWG_BUILD_TESTS=ON
cmake --build build-window-tests --target DAWg dawg_window_manager_integration_tests --parallel 2
ctest --test-dir build-window-tests -R dawg.window_manager_integration --output-on-failure
```

Expected: DAWg compiles with no warnings, and the integration suite passes or skips only when no desktop peer is available.

- [ ] **Step 4: Commit DAW integration**

```bash
git add CMakeLists.txt Source/App/Main.cpp Tests/WindowManagerIntegrationTests.cpp
git commit -m "feat(app): create DAWg main window through window manager"
```

## Task 6: Full verification and handoff

**Files:**
- Modify only files required by verification fixes.

**Interfaces:**
- Consumes: all implementation and test targets from Tasks 1–5.
- Produces: verified window-manager feature with no changes to the audio callback or engine headers.

- [ ] **Step 1: Run the complete deterministic and integration test set**

```bash
ctest --test-dir build-window-tests --output-on-failure
```

Expected: every deterministic test passes; the integration test either passes or is explicitly skipped with return code `77`.

- [ ] **Step 2: Build the existing application configuration**

```bash
cmake --build build --config Debug --parallel 2
```

Expected: the existing DAWg build succeeds without new warnings or sanitizer diagnostics.

- [ ] **Step 3: Verify the architectural boundary**

```bash
rg -n "juce_|JUCE" ../VersaTone/engine/include ../VersaTone/engine/src || true
rg -n "DocumentWindow|MainWindow" Source/App Source/Window
git diff --check
git status --short
```

Expected: no new JUCE references under the engine, no direct `DocumentWindow` main-window implementation, no whitespace errors, and only intentional working-tree changes.

- [ ] **Step 4: Commit any verification-only correction**

If verification identifies a real implementation defect, add a focused regression test first, fix the smallest production change, rerun the affected test, then commit it with:

```bash
git add CMakeLists.txt Source Tests
git commit -m "fix(window): correct verified window manager behavior"
```

## Plan Self-Review

- Spec coverage: creation, resize, close, all requested states, transparency, frameless mode, multi-monitor snapshots, DPI conversion, child windows, modal dialogs, main-window integration, error handling, and tests are covered by Tasks 1–5.
- Placeholder scan: the plan contains no unfinished or deferred implementation step; every task has files, interfaces, commands, expected outcomes, and a commit boundary.
- Type consistency: `WindowHandle`, `DisplayId`, `WindowBounds`, `WindowState`, `WindowOptions`, `WindowRegistry`, `WindowManager`, and native-state functions retain the same names and signatures throughout the plan.
- Scope: the plan does not alter VersaTone engine code or introduce persistence, plugin embedding, or unrelated DAW features.
