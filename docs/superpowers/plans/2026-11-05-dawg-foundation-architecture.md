# DAWg Foundation Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish DAWg's reusable JUCE-free engine, project model, realtime command boundary, persistence, JUCE adapter, and first end-to-end project/audio vertical slice.

**Architecture:** DAWg owns a clean engine and project boundary under `engine/` and `project/`. JUCE device/window integration moves to `platform/juce/`, application composition moves to `apps/dawg/`, and UI stays above application services. The existing VersaTone sibling target is removed from the DAWg build; stable legacy engine behavior is migrated behind tested interfaces rather than exposed directly.

**Tech Stack:** C++20 for reusable engine/project targets, C++23 for persistence services that use `std::expected`, C++26 for the DAW application where supported, CMake 3.25+, JUCE 9.0.4 for platform/UI integration, CTest, and the existing GCC/Clang warning and sanitizer policy.

**Spec:** `docs/superpowers/specs/2026-11-05-dawg-foundation-architecture-design.md`

## Global Constraints

- `dawg_engine_core` is C++ only, with no JUCE or platform libraries.
- The top-level build must not use an implicit sibling path to find VersaTone.
- The callback-facing path performs no heap allocation, filesystem I/O, network I/O, console I/O, GUI I/O, blocking wait, or ownership change.
- UI components never own project or audio state; they issue commands and consume snapshots.
- Engine tests run without JUCE or X11; platform tests cover JUCE and supported display hosts separately.
- Project files use explicit format versions and stable identifiers.
- Saving is transactional and loading failures leave the current project unchanged.
- Debug builds run AddressSanitizer/UndefinedBehaviorSanitizer, with ThreadSanitizer configured separately.
- Every task ends with focused tests, a clean diff check, and a small commit.

---

### Task 1: Create the JUCE-free engine target and realtime contract

**Files:**
- Modify: `CMakeLists.txt`
- Create: `engine/CMakeLists.txt`
- Create: `engine/include/dawg/engine/AudioBufferView.h`
- Create: `engine/include/dawg/engine/EngineConfig.h`
- Create: `engine/include/dawg/engine/MidiEvent.h`
- Create: `engine/include/dawg/engine/TransportSnapshot.h`
- Create: `engine/include/dawg/engine/AudioEngine.h`
- Create: `engine/src/AudioEngine.cpp`
- Create: `tests/engine/AudioEngineTests.cpp`
- Modify: `.gitignore` if the repository does not already ignore generated build directories

**Interfaces:**
- Produces CMake target `dawg_engine_core`.
- Produces `dawg::engine::AudioEngine::prepare(const EngineConfig&) noexcept -> bool`.
- Produces `dawg::engine::AudioEngine::release() noexcept -> void`.
- Produces `dawg::engine::AudioEngine::reset() noexcept -> void`.
- Produces `dawg::engine::AudioEngine::process(AudioBufferView, MidiBufferView, const TransportSnapshot&) noexcept -> void`.
- Produces `dawg::engine::AudioEngine::isPrepared() const noexcept -> bool`.

- [ ] **Step 1: Write the failing engine contract test**

Create a test executable with a local `require(bool, const char*)` helper so
the engine test has no third-party test framework dependency:

```cpp
#include <dawg/engine/AudioEngine.h>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\\n", message);
        std::exit(EXIT_FAILURE);
    }
}
}

int main()
{
    dawg::engine::AudioEngine engine;
    require(engine.prepare({48000.0, 32, 2}), "engine prepares valid configuration");
    require(engine.isPrepared(), "prepared state is observable");

    std::array<std::array<float, 32>, 2> samples {};
    for (auto& channel : samples)
        channel.fill(1.0f);
    std::array<float*, 2> channels { samples[0].data(), samples[1].data() };

    engine.process({channels.data(), channels.size(), samples[0].size()}, {}, {});
    for (const auto& channel : samples)
        for (const auto sample : channel)
            require(sample == 0.0f, "initial renderer clears output");

    engine.release();
    require(! engine.isPrepared(), "release clears prepared state");
}
```

Add the test target and CTest registration before adding the production
headers. Run:

```bash
cmake -S . -B /tmp/dawg-foundation-red -G Ninja -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/dawg-foundation-red --target dawg_engine_core_tests
```

Expected: configuration or compilation fails because the new engine target and
contract types do not exist yet. Record the failure as the RED evidence.

- [ ] **Step 2: Implement the minimal JUCE-free contract**

Define the value types exactly as follows:

```cpp
struct AudioBufferView
{
    float* const* channels {};
    std::size_t channelCount {};
    std::size_t frameCount {};
};

struct EngineConfig
{
    double sampleRate {};
    std::size_t maximumBlockSize {};
    std::size_t outputChannels {};
};

struct MidiEvent
{
    std::size_t frameOffset {};
    std::array<std::byte, 3> data {};
    std::uint8_t size {};
};

struct MidiBufferView
{
    const MidiEvent* events {};
    std::size_t eventCount {};
};

struct TransportSnapshot
{
    bool playing {};
    std::int64_t samplePosition {};
    double tempoBpm { 120.0 };
};
```

`AudioEngine::prepare` accepts only positive sample rate, block size, and
channel count. `process` clears only the supplied output channels and frames;
it does not allocate, log, or inspect MIDI yet. `release` returns the engine to
the unprepared state. The CMake target exposes only `engine/include` and uses
C++20.

- [ ] **Step 3: Run the focused test and inspect its output**

Run:

```bash
cmake --build /tmp/dawg-foundation-red --target dawg_engine_core_tests
/tmp/dawg-foundation-red/dawg_engine_core_tests
ctest --test-dir /tmp/dawg-foundation-red -R dawg.engine_core --output-on-failure
```

Expected: the test passes and no JUCE or X11 target is required. Configure a
second build with `-DCMAKE_DISABLE_FIND_PACKAGE_X11=TRUE` to prove the engine
target remains independent of the display server.

- [ ] **Step 4: Remove the sibling VersaTone target from the top-level build**

Replace the `add_subdirectory(../VersaTone/engine dawg_engine_build)` block and
its engine cache options with:

```cmake
add_subdirectory(engine)
```

Link `DAWg` to `dawg_engine_core` temporarily while the adapter migration is
completed. Do not add a fallback relative path. Run the existing registry and
window-manager tests to catch accidental JUCE target regressions.

- [ ] **Step 5: Commit the engine boundary**

```bash
git add CMakeLists.txt engine tests/engine .gitignore
git diff --cached --check
git commit -m "feat(engine): establish JUCE-free realtime core"
```

### Task 2: Introduce a deterministic transport command boundary

**Files:**
- Create: `engine/include/dawg/engine/EngineCommand.h`
- Create: `engine/include/dawg/engine/TransportState.h`
- Create: `engine/src/TransportCommandQueue.h`
- Create: `engine/src/TransportCommandQueue.cpp`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `tests/engine/AudioEngineTests.cpp`

**Interfaces:**
- Produces `enum class EngineCommandType { play, stop, seekSamples }`.
- Produces `struct EngineCommand { EngineCommandType type; std::int64_t samplePosition; }`.
- Produces `AudioEngine::enqueueCommand(const EngineCommand&) noexcept -> bool`.
- Produces `AudioEngine::transportState() const noexcept -> TransportState`.
- Consumes the `EngineConfig` contract from Task 1.

- [ ] **Step 1: Add failing transport tests**

Extend the engine test with this behavior:

```cpp
require(engine.enqueueCommand({ EngineCommandType::play, 0 }),
        "play command is accepted");
engine.process(output, {}, {});
require(engine.transportState().playing, "audio thread applies play command");

require(engine.enqueueCommand({ EngineCommandType::seekSamples, 96000 }),
        "seek command is accepted");
engine.process(output, {}, {});
require(engine.transportState().samplePosition == 96000,
        "audio thread applies seek command");

require(engine.enqueueCommand({ EngineCommandType::stop, 0 }),
        "stop command is accepted");
engine.process(output, {}, {});
require(! engine.transportState().playing, "audio thread applies stop command");
```

Run the engine test before implementing the queue. Expected: compilation fails
because the command types and methods do not exist.

- [ ] **Step 2: Implement a bounded SPSC command queue**

Implement a fixed-capacity queue with `std::array<EngineCommand, 64>` and
acquire/release atomics. The producer is the application/message thread and
the consumer is the audio thread. `enqueue` returns `false` when full; it
never waits and never allocates. Drain commands at the beginning of
`AudioEngine::process` in FIFO order, update the private transport state, then
render the current block.

- [ ] **Step 3: Verify ordering and overflow behavior**

Add a test that queues play, seek, and stop before one process call and asserts
that the final state reflects the last command. Fill the queue with 64 valid
commands and assert the 65th enqueue returns `false`. Run:

```bash
cmake --build /tmp/dawg-foundation-red --target dawg_engine_core_tests
/tmp/dawg-foundation-red/dawg_engine_core_tests
```

Expected: all transport tests pass with no sleeps or timing assumptions.

- [ ] **Step 4: Commit the command boundary**

```bash
git add engine tests/engine
git diff --cached --check
git commit -m "feat(engine): add bounded transport command queue"
```

### Task 3: Build the JUCE-free project model and command coordinator

**Files:**
- Create: `project/CMakeLists.txt`
- Create: `project/include/dawg/project/ProjectTypes.h`
- Create: `project/include/dawg/project/ProjectSnapshot.h`
- Create: `project/include/dawg/project/ProjectCommand.h`
- Create: `project/include/dawg/project/Project.h`
- Create: `project/src/Project.cpp`
- Create: `project/src/ProjectCommand.cpp`
- Create: `tests/project/ProjectTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces target `dawg_project_model`.
- Produces `using ProjectId = std::uint64_t` and `using TrackId = std::uint64_t`.
- Produces `struct Track { TrackId id; std::string name; bool muted; bool solo; friend bool operator==(const Track&, const Track&) = default; }`.
- Produces `struct ProjectSnapshot { std::uint32_t formatVersion; ProjectId id; std::string name; std::vector<Track> tracks; }`.
- Produces `class Project` with `create(std::string, ProjectId)`, `snapshot() const`, `addAudioTrack(std::string)`, `removeTrack(TrackId)`, and `renameTrack(TrackId, std::string)`.
- Produces `ProjectCommand` variants for add, remove, and rename operations.
- Produces `Project::apply(const ProjectCommand&) -> bool`.

- [ ] **Step 1: Write failing project behavior tests**

Create tests for stable identity and command behavior:

```cpp
auto project = Project::create("Untitled", 1);
const auto first = project.addAudioTrack("Audio 1");
const auto second = project.addAudioTrack("Audio 2");
require(first != second, "tracks have distinct stable IDs");
require(project.renameTrack(first, "Vocal"), "existing track can be renamed");
require(project.removeTrack(second), "existing track can be removed");
require(! project.removeTrack(second), "removed track cannot be removed twice");

const auto snapshot = project.snapshot();
require(snapshot.formatVersion == 1, "project snapshot has version one");
require(snapshot.tracks.size() == 1, "snapshot contains remaining track");
require(snapshot.tracks.front().name == "Vocal", "snapshot contains renamed track");
```

Run the project test before adding the target. Expected: compilation fails
because the project API does not exist.

- [ ] **Step 2: Implement the model with message-thread ownership**

Store project state in `Project` using an ordered `std::vector<Track>` and a
monotonic `TrackId` counter initialized above all loaded IDs. Reject empty
track names and unknown IDs without mutating state. `snapshot()` returns a
value copy that contains no JUCE types or pointers into the project.

- [ ] **Step 3: Implement commands through the same public behavior**

Define:

```cpp
struct AddAudioTrack { std::string name; };
struct RemoveTrack { TrackId id {}; };
struct RenameTrack { TrackId id {}; std::string name; };
using ProjectCommand = std::variant<AddAudioTrack, RemoveTrack, RenameTrack>;
```

Dispatch each variant to the corresponding model method. Add tests proving a
failed command leaves the previous snapshot byte-for-byte equivalent when
serialized by the test helper.

- [ ] **Step 4: Verify the target is JUCE-free**

Build and run only the project test with JUCE and X11 unavailable:

```bash
cmake -S . -B /tmp/dawg-project-test -G Ninja -DDAWG_BUILD_TESTS=ON -DCMAKE_DISABLE_FIND_PACKAGE_X11=TRUE
cmake --build /tmp/dawg-project-test --target dawg_project_tests
/tmp/dawg-project-test/dawg_project_tests
```

- [ ] **Step 5: Commit the project model**

```bash
git add CMakeLists.txt project tests/project
git diff --cached --check
git commit -m "feat(project): add versioned project model and commands"
```

### Task 4: Add versioned transactional project persistence

**Files:**
- Modify: `CMakeLists.txt`
- Create: `services/CMakeLists.txt`
- Create: `services/persistence/CMakeLists.txt`
- Create: `services/persistence/include/dawg/persistence/ProjectFile.h`
- Create: `services/persistence/src/ProjectFile.cpp`
- Create: `tests/services/ProjectFileTests.cpp`
- Modify: `project/include/dawg/project/ProjectSnapshot.h` if serialization accessors are needed
- Modify: `docs/DEPENDENCIES_AND_LICENSING.md`

**Interfaces:**
- Produces target `dawg_services_persistence`.
- Produces `struct ProjectFileError { enum class Code { invalidPath, ioFailure, invalidFormat, unsupportedVersion }; Code code; std::string message; }`.
- Produces `std::expected<void, ProjectFileError> saveProject(const ProjectSnapshot&, const std::filesystem::path&)`.
- Produces `std::expected<ProjectSnapshot, ProjectFileError> loadProject(const std::filesystem::path&)`.
- The serialized document contains `formatVersion`, `projectId`, `name`, and an array of tracks with `id`, `name`, `muted`, and `solo`.

- [ ] **Step 1: Write failing round-trip and failure tests**

Cover these behaviors before implementing the serializer:

```cpp
const auto root = std::filesystem::temp_directory_path() / "dawg-project-file-tests";
std::filesystem::create_directories(root);
const auto path = root / "round-trip.dawg";
auto project = Project::create("Round Trip", 42);
project.addAudioTrack("Audio 1");
const auto original = project.snapshot();
require(saveProject(original, path).has_value(), "save succeeds");
const auto loaded = loadProject(path);
require(loaded.has_value(), "load succeeds");
require(loaded->formatVersion == original.formatVersion, "version survives round trip");
require(loaded->tracks == original.tracks, "tracks survive round trip");

{
    std::ofstream document(path, std::ios::trunc);
    document << R"({"formatVersion":999,"projectId":42,"name":"Invalid","tracks":[]})";
}
const auto unsupported = loadProject(path);
require(! unsupported.has_value(), "unsupported version is rejected");
require(unsupported.error().code == ProjectFileError::Code::unsupportedVersion,
        "unsupported version has typed error");
```

Run the test and observe the expected missing-symbol failure.

- [ ] **Step 2: Implement the versioned document format**

Declare `nlohmann/json` `v3.11.3` in `services/persistence/CMakeLists.txt`:

```cmake
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
target_link_libraries(dawg_services_persistence PRIVATE nlohmann_json::nlohmann_json)
```

Keep the dependency out of `dawg_engine_core` and `dawg_project_model` by
converting to/from `ProjectSnapshot` at the persistence boundary. Reject
missing fields, duplicate track IDs, empty names, and versions other than `1`.

- [ ] **Step 3: Implement transactional saving**

Write to a sibling temporary file with a process-unique suffix, flush and
close it, then replace the destination using `std::filesystem::rename`. On
failure, remove only the temporary file and leave the existing destination
unchanged. Add a test that starts with a valid destination, forces an invalid
parent path, and verifies the original document remains readable.

- [ ] **Step 4: Run persistence tests and commit**

```bash
cmake --build /tmp/dawg-project-test --target dawg_project_file_tests
ctest --test-dir /tmp/dawg-project-test -R dawg.project --output-on-failure
git add CMakeLists.txt services/persistence project tests/services docs/DEPENDENCIES_AND_LICENSING.md
git diff --cached --check
git commit -m "feat(project): add versioned transactional persistence"
```

### Task 5: Publish immutable render snapshots from application services

**Files:**
- Create: `engine/include/dawg/engine/RenderSnapshot.h`
- Create: `engine/include/dawg/engine/RenderSnapshotPublisher.h`
- Create: `engine/src/RenderSnapshotPublisher.cpp`
- Create: `services/commands/include/dawg/commands/ProjectController.h`
- Create: `services/commands/src/ProjectController.cpp`
- Create: `tests/services/ProjectControllerTests.cpp`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `services/CMakeLists.txt`

**Interfaces:**
- Produces `struct RenderSnapshot { std::uint64_t generation; std::uint32_t trackCount; }`.
- Produces `AudioEngine::publishRenderSnapshot(RenderSnapshot) noexcept -> bool`.
- Produces `AudioEngine::renderSnapshotGeneration() const noexcept -> std::uint64_t`.
- Produces `ProjectController(Project)`.
- Produces `ProjectController::dispatch(ProjectCommand) -> bool`.
- Produces `ProjectController::snapshot() const -> ProjectSnapshot`.
- Produces `ProjectController::latestRenderSnapshot() const noexcept -> RenderSnapshot`.

- [ ] **Step 1: Add failing snapshot-publication tests**

Test that dispatching a valid project command increments the generation and
that the audio engine observes the new generation only at a process boundary:

```cpp
AudioEngine engine;
require(engine.prepare({48000.0, 32, 2}), "engine prepares");
ProjectController controller { Project::create("Untitled", 1) };
const auto before = controller.latestRenderSnapshot();
require(controller.dispatch(AddAudioTrack { "Audio 1" }), "command succeeds");
const auto after = controller.latestRenderSnapshot();
require(after.generation > before.generation, "edit publishes newer generation");

require(engine.publishRenderSnapshot(after), "prepared engine accepts snapshot");
require(engine.renderSnapshotGeneration() == 0, "publication waits for block boundary");
engine.process(output, {}, {});
require(engine.renderSnapshotGeneration() == after.generation,
        "audio thread adopts snapshot at block boundary");
```

- [ ] **Step 2: Implement bounded publication storage**

Use three preallocated render snapshot slots, an atomic published index, and
an atomic audio-current index. The message thread writes only a slot that is
neither currently published nor currently consumed by audio; if no slot is
available it returns `false` without waiting. It then publishes the slot index
with release ordering. The audio thread loads the published index with acquire
ordering at the start of `process`, adopts it, and stores its current index
for the producer. No `std::shared_ptr`, allocation, or lock is allowed on the
audio path.

- [ ] **Step 3: Implement `ProjectController`**

Own a `Project` and a monotonically increasing generation on the message
thread. After a successful command, build a fixed-size `RenderSnapshot` that
the application service publishes to `AudioEngine`. Keeping the controller
independent of `AudioEngine` lets the project model remain JUCE-free and makes
commands testable without an audio device. Failed commands do not increment
the generation or change the project snapshot.

- [ ] **Step 4: Verify and commit**

Run the engine and project tests together, then:

```bash
git add engine services tests/services
git diff --cached --check
git commit -m "feat(engine): publish immutable render snapshots"
```

### Task 6: Replace the direct audio adapter with a JUCE platform bridge

**Files:**
- Create: `platform/juce/CMakeLists.txt`
- Create: `platform/juce/audio/JuceAudioDeviceBridge.h`
- Create: `platform/juce/audio/JuceAudioDeviceBridge.cpp`
- Create: `platform/juce/audio/JuceAudioCallbackAdapter.h`
- Create: `platform/juce/audio/JuceAudioCallbackAdapter.cpp`
- Create: `tests/platform/JuceAudioCallbackAdapterTests.cpp`
- Remove after migration: `Source/Audio/AudioAdapter.h`
- Remove after migration: `Source/Audio/AudioAdapter.cpp`
- Modify: `CMakeLists.txt`
- Modify: `apps/dawg/CMakeLists.txt`

**Interfaces:**
- Produces target `dawg_platform_juce`.
- Produces `class JuceAudioDeviceBridge` with `initialise()`, `shutdown()`, `isDeviceReady()`, `sampleRate()`, and `blockSize()`.
- Produces `class JuceAudioCallbackAdapter` with `process(float* const*, std::size_t, std::size_t) noexcept`.
- The bridge owns `juce::AudioDeviceManager` and `dawg::engine::AudioEngine` references but exposes no JUCE types from the engine target.
- The JUCE callback adapts channel pointers into `AudioBufferView`, calls `AudioEngine::process`, and clears unused output channels.

- [ ] **Step 1: Add the bridge contract test seam**

Define the bridge constructor to accept an `AudioEngine&` and put the pointer
adaptation in `JuceAudioCallbackAdapter`, which has no JUCE dependency. The
test calls `JuceAudioCallbackAdapter::process` with a supplied block and
asserts that a prepared engine outputs silence. Run before moving the existing
adapter; expected failure is a missing adapter symbol.

- [ ] **Step 2: Implement the JUCE bridge**

Move device lifecycle code from `Source/Audio/AudioAdapter.cpp` into the
bridge. In `audioDeviceAboutToStart`, prepare the engine from the device's
sample rate, block size, and output channel count. In the callback, create
non-owning views only; do not allocate `AudioBuffer`, call `getTransport()`
directly, or touch project/UI objects. On stop, release the engine and publish
an atomic device-not-ready state.

- [ ] **Step 3: Link the platform target and run non-GUI tests**

Link `dawg_platform_juce` only to JUCE and `dawg_engine_core`; keep engine and
project tests independent. Build the adapter target and run the existing
window-manager suite on the supported X11 host.

- [ ] **Step 4: Commit the bridge migration**

```bash
git add CMakeLists.txt apps/dawg platform/juce tests/platform
git rm Source/Audio/AudioAdapter.h Source/Audio/AudioAdapter.cpp
git diff --cached --check
git commit -m "refactor(platform): isolate JUCE audio device bridge"
```

### Task 7: Re-home application, UI, and window-manager composition

**Files:**
- Create: `apps/dawg/Application.h`
- Create: `apps/dawg/Application.cpp`
- Move: `Source/App/Main.cpp` to `apps/dawg/Main.cpp`
- Move: `Source/App/MainComponent.h` to `ui/shell/MainComponent.h`
- Move: `Source/App/MainComponent.cpp` to `ui/shell/MainComponent.cpp`
- Move: `Source/Window/*.h` and supported platform sources to `platform/juce/window/`
- Create: `ui/shell/MainEditorView.h`
- Create: `ui/shell/MainEditorView.cpp`
- Modify: `CMakeLists.txt`
- Modify: `Tests/WindowRegistryTests.cpp`
- Modify: `Tests/WindowManagerIntegrationTests.cpp`

**Interfaces:**
- `apps/dawg/Application` owns `AudioEngine`, `ProjectController`, `JuceAudioDeviceBridge`, and `WindowManager`.
- `ui::MainEditorView` receives a `ProjectSnapshot` view value and command callback; it does not include `AudioEngine` or mutate `Project` directly.
- `platform::juce::window::WindowManager` remains responsible only for native windows, child/modal relationships, monitor/DPI handling, and native state synchronization.

- [ ] **Step 1: Add a failing composition test**

Add a non-GUI application-state test that constructs a controller, dispatches
`AddAudioTrack { "Audio 1" }`, and verifies the editor view model reports one
track without including a JUCE component. This test fails until the application
composition types exist.

- [ ] **Step 2: Move files without changing window behavior**

Use `git mv` for the existing application and window files, update include
paths and namespaces, and preserve the already-tested window-manager behavior.
Do not add project state to `ManagedWindow`, `WindowManager`, or any JUCE
component.

- [ ] **Step 3: Implement application ownership**

Construct the controller, engine, bridge, and window manager in
`Application::initialise`. Create the main window with a `MainEditorView`
connected to snapshot updates and command callbacks. On shutdown, stop the
audio bridge before destroying project/controller state, then close windows.

- [ ] **Step 4: Run the complete supported-host suite**

Build `DAWg`, `dawg_engine_core_tests`, `dawg_project_tests`, persistence,
platform, registry, and integration tests. Run the window-manager CTest suite
on the real X11 host and perform a launch/close smoke test for the Release
application.

- [ ] **Step 5: Commit the application boundary**

```bash
git add apps ui platform/juce/window CMakeLists.txt Tests
git diff --cached --check
git commit -m "refactor(app): separate DAW composition from engine and platform"
```

### Task 8: Add the foundation end-to-end integration test and documentation

**Files:**
- Create: `tests/integration/FoundationVerticalSliceTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/ROADMAP.md`
- Modify: `docs/DEPENDENCIES_AND_LICENSING.md`
- Modify: `docs/superpowers/specs/2026-11-05-dawg-foundation-architecture-design.md` only if an implemented interface differs from the approved contract

**Interfaces:**
- Produces CTest test `dawg.foundation_vertical_slice`.
- The test uses no display server and exercises `ProjectController`, project persistence, engine preparation, render-snapshot publication, transport command delivery, and deterministic silent rendering.

- [ ] **Step 1: Write the failing vertical-slice test**

The test sequence is fixed:

```cpp
AudioEngine engine;
require(engine.prepare({48000.0, 64, 2}), "engine prepares");
ProjectController controller { Project::create("Untitled", 1) };
require(controller.dispatch(AddAudioTrack { "Audio 1" }), "track command succeeds");
const auto saved = controller.snapshot();
require(saveProject(saved, path).has_value(), "project saves");

require(engine.publishRenderSnapshot(controller.latestRenderSnapshot()),
        "render snapshot publishes");
require(engine.enqueueCommand({ EngineCommandType::play, 0 }), "play queues");
std::array<std::array<float, 64>, 2> samples {};
std::array<float*, 2> channels { samples[0].data(), samples[1].data() };
const AudioBufferView output { channels.data(), channels.size(), samples[0].size() };
engine.process(output, {}, {});
require(engine.transportState().playing, "transport is playing");
for (const auto& channel : samples)
    for (const auto sample : channel)
        require(sample == 0.0f, "empty graph renders deterministic silence");

const auto loaded = loadProject(path);
require(loaded.has_value(), "saved project reloads");
require(loaded->tracks == saved.tracks, "reloaded project equals saved snapshot");
```

Run before registering the integration target. Expected: missing composition
symbols or target failure.

- [ ] **Step 2: Register and implement the integration target**

Link only the JUCE-free engine, project, services, and test utility targets.
Keep this test independent from X11. Add a second CTest property for the
window-manager integration test so display-dependent tests remain clearly
labelled.

- [ ] **Step 3: Update documentation to match the built structure**

Document the actual target graph, build commands, realtime contract, and
foundation capabilities. Replace the statement that DAWg directly consumes a
relative VersaTone sibling with the implemented engine ownership and migration
boundary. Update the roadmap so later recording/playback work depends on this
vertical slice.

- [ ] **Step 4: Run final verification**

Run all of the following from a clean checkout:

```bash
cmake -S . -B /tmp/dawg-foundation-final -G Ninja -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/dawg-foundation-final --parallel 2
ctest --test-dir /tmp/dawg-foundation-final --output-on-failure
git diff --check
git status --short --branch
```

On the supported X11 host, rerun the display-dependent window-manager tests
with `DISPLAY=:0` and launch/close the Release DAWg executable. Run a Debug
ASan/UBSan build and a separate TSan build before declaring the foundation
complete.

- [ ] **Step 5: Commit and push the vertical slice**

```bash
git add CMakeLists.txt README.md docs tests
git diff --cached --check
git commit -m "test(dawg): verify foundation project and audio vertical slice"
git push origin main
```

## Plan self-review

- **Spec coverage:** engine boundary and realtime rules are covered by Tasks 1–2; project state and commands by Task 3; persistence by Task 4; immutable snapshots by Task 5; JUCE/device integration by Task 6; window/UI ownership by Task 7; end-to-end acceptance and documentation by Task 8.
- **Scope:** plugin hosting, recording, waveform generation, MIDI editing, non-destructive clip editing, mixing, and offline rendering remain later milestones; this plan only establishes the contracts those milestones will consume.
- **Placeholder scan:** no task depends on an unspecified function, file, or vague implementation step; each task names its files, interfaces, red test, implementation behavior, verification command, and commit.
- **Type consistency:** `AudioEngine`, `EngineConfig`, `AudioBufferView`, `MidiBufferView`, `TransportSnapshot`, `EngineCommand`, `ProjectCommand`, `ProjectSnapshot`, `ProjectController`, and persistence result types are introduced before their consumers.
