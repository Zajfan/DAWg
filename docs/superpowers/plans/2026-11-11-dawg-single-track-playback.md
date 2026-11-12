# DAWg Single-Track Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a tested single-track WAV playback vertical slice in which DAWg imports one external WAV asset, persists its clip metadata, streams decoded PCM through a bounded worker-owned buffer, and renders deterministic audio from the existing JUCE device callback.

**Architecture:** The JUCE-free project model owns stable asset and clip metadata and serializes project format v2; a v1 file is upgraded to v2 with empty media collections. The JUCE-free engine owns the lock-free bounded PCM ring and performs only snapshot adoption, transport command application, clip-range arithmetic, bounded reads, silence filling, and sample-position advancement on the audio thread. A media service owns the decoder, reader worker, scratch buffers, and ring preparation; the JUCE adapter only supplies the WAV reader and the application composes import, playback controls, status, and lifetime ordering.

**Tech Stack:** C++20 for engine/project/media libraries, C++23 where existing persistence uses `std::expected`, C++26 for the JUCE application target, CMake 3.25+, JUCE 9.0.4, nlohmann/json 3.11.3, CTest, and the repository's existing GCC/Clang warning and sanitizer policy.

**Spec:** `docs/superpowers/specs/2026-11-11-dawg-single-track-playback-design.md`

## Global Constraints

- The engine and project model remain JUCE-free; only `platform/juce` may include JUCE headers.
- Project format v2 contains `assets` and `clips`; format v1 remains loadable and is normalized to v2 with empty media collections.
- `AudioAssetReader::read()` is the only decoder read seam; decoder/file access never runs on the audio callback.
- The audio callback performs no allocation, file I/O, decoding, blocking, logging, or UI callback and uses only preallocated engine/media storage.
- The initial renderer supports one active audio clip on the first audio track; it does not implement mixing, fades, gain, pan, time-stretching, resampling, recording, plugins, MIDI, undo, waveform generation, or embedded media copies.
- Missing external media is a loadable project condition with an unavailable status and deterministic silence, not a project-file parse failure.
- All public value types crossing project, service, engine, and UI boundaries own their values or use explicitly documented stable pointers; no mutable `Project` is exposed to the audio or UI threads.
- Existing transport commands remain `play`, `stop`, and `seekSamples`; `seekSamples` with zero is the return-to-start action.
- Every task ends with a focused test run and a small commit; no task may leave the tree with known compiler warnings under the repository's `-Werror` policy.

---

### Task 1: Extend the JUCE-free project model for assets and clips

**Files:**
- Modify: `project/include/dawg/project/ProjectTypes.h`
- Modify: `project/include/dawg/project/ProjectSnapshot.h`
- Modify: `project/include/dawg/project/ProjectCommand.h`
- Modify: `project/include/dawg/project/Project.h`
- Modify: `project/src/Project.cpp`
- Modify: `project/src/ProjectCommand.cpp`
- Modify: `tests/project/ProjectTests.cpp`
- Modify: `services/commands/include/dawg/commands/ProjectController.h`
- Modify: `services/commands/src/ProjectController.cpp`
- Modify: `tests/services/ProjectControllerTests.cpp`

**Interfaces:**
- Consumes: existing `Project`, `ProjectSnapshot`, `ProjectCommand`, and `ProjectController` track-edit contracts.
- Produces: `MediaAssetId`, `ClipId`, `AudioAsset`, `AudioClip`, a version-2 `ProjectSnapshot`, `Project::addAudioAsset(AudioAsset)`, `Project::addAudioClip(AudioClip)`, `project::AddAudioClip`, and `ProjectController::addAudioAsset(AudioAsset)`.

- [ ] **Step 1: Write the failing value-type and snapshot tests.**

Add tests that compile and express the exact new values:

```cpp
static_assert(std::is_same_v<MediaAssetId, std::uint64_t>);
static_assert(std::is_same_v<ClipId, std::uint64_t>);

const ProjectSnapshot snapshot {
    2,
    1,
    "Playback",
    {{1, "fixtures/tone.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8}},
    {{1, 1, 0, 0, 8}}
};
require(snapshot.assets.front().path == "fixtures/tone.wav", "asset path is owned");
require(snapshot.clips.front().lengthSamples == 8, "clip length is expressed in samples");
```

Update the existing test serializer to include asset and clip IDs, paths, metadata, offsets, and lengths so snapshot isolation and rejected-operation tests compare every value.

- [ ] **Step 2: Run the project and controller tests to verify the new API fails.**

Run:

```bash
cmake --build build/headless --target dawg_project_tests dawg_project_controller_tests
ctest --test-dir build/headless -R 'dawg\.(project|project_controller)$' --output-on-failure
```

Expected: compilation fails because the new IDs, collections, and asset/clip operations do not exist yet.

- [ ] **Step 3: Add the stable project value types.**

In `ProjectTypes.h`, add:

```cpp
using MediaAssetId = std::uint64_t;
using ClipId = std::uint64_t;

enum class AudioAssetPathKind : std::uint8_t { projectRelative, absolute };

struct AudioAsset {
    MediaAssetId id {};
    std::string path;
    AudioAssetPathKind pathKind {AudioAssetPathKind::projectRelative};
    std::uint32_t sampleRate {};
    std::uint16_t channelCount {};
    std::uint64_t frameCount {};
    friend bool operator==(const AudioAsset&, const AudioAsset&) = default;
};

struct AudioClip {
    ClipId id {};
    MediaAssetId assetId {};
    std::uint64_t startSample {};
    std::uint64_t sourceOffset {};
    std::uint64_t lengthSamples {};
    friend bool operator==(const AudioClip&, const AudioClip&) = default;
};
```

Keep paths as owned UTF-8 `std::string` values at the project boundary; platform code converts them to a native path only when opening media.

- [ ] **Step 4: Make the snapshot current version 2 and add ordered media collections.**

Change `ProjectSnapshot::formatVersion` default to `2` and append:

```cpp
std::vector<AudioAsset> assets;
std::vector<AudioClip> clips;
```

Change `Project::create()` to create version 2. Keep the existing track ordering and ID behavior unchanged.

- [ ] **Step 5: Add project allocation and validation for assets and clips.**

Implement these exact methods:

```cpp
MediaAssetId addAudioAsset(AudioAsset asset);
ClipId addAudioClip(AudioClip clip);
```

The methods assign the next monotonic nonzero ID when the incoming ID is zero, reject an explicit nonzero ID that collides, reject empty paths, unknown path kinds, zero sample rate, zero channels, zero frame count, unknown asset IDs, source offsets beyond the asset, lengths of zero, and lengths that exceed `frameCount - sourceOffset`. Use checked subtraction before comparing the clip length. Continue allocating above the largest loaded asset/clip ID and return zero on rejection without mutating state.

Update `Project::fromSnapshot()` to accept only the normalized current version 2, validate unique nonzero track/asset/clip IDs, validate every clip asset reference and range, and preserve all vector order. Track, asset, and clip high-water marks must be computed independently.

- [ ] **Step 6: Add the clip command and controller asset registration seam.**

Add:

```cpp
struct AddAudioClip { AudioClip clip; };
using ProjectCommand = std::variant<AddAudioTrack, RemoveTrack, RenameTrack, AddAudioClip>;
```

Add:

```cpp
[[nodiscard]] std::optional<MediaAssetId> addAudioAsset(project::AudioAsset asset);
```

to `ProjectController`. The controller must publish a new generation after a successful asset registration or clip command. `latestRenderSnapshot()` must continue to expose track count and generation without exposing mutable project storage. The controller must reject generation overflow before either operation mutates the project.

- [ ] **Step 7: Expand model and controller tests for accepted and rejected media edits.**

Cover these cases explicitly:

```cpp
auto project = Project::create("Playback", 1);
const auto assetId = project.addAudioAsset({0, "tone.wav", AudioAssetPathKind::projectRelative,
                                           48000, 1, 8});
require(assetId == 1, "first asset receives ID one");
require(project.addAudioClip({0, assetId, 0, 0, 8}) == 1, "first clip receives ID one");
require(project.addAudioClip({0, 999, 0, 0, 8}) == 0, "unknown asset is rejected");
require(project.addAudioClip({0, assetId, 0, 7, 2}) == 0, "clip range cannot exceed asset frames");
```

Also verify snapshot copies do not alias paths or media vectors, loaded sparse IDs allocate above their maxima, and failed asset/clip operations do not consume IDs or generations.

- [ ] **Step 8: Run the focused tests and commit the model boundary.**

Run:

```bash
cmake --build build/headless --target dawg_project_tests dawg_project_controller_tests
ctest --test-dir build/headless -R 'dawg\.(project|project_controller)$' --output-on-failure
git diff --check
git add project services/commands tests/project/ProjectTests.cpp tests/services/ProjectControllerTests.cpp
git commit -m "feat(project): model audio assets and clips"
```

Expected: both tests pass and the commit contains no persistence, engine, JUCE, or UI changes.

### Task 2: Persist project format v2 and upgrade v1 files

**Files:**
- Modify: `services/persistence/include/dawg/persistence/ProjectFile.h`
- Modify: `services/persistence/src/ProjectFile.cpp`
- Modify: `tests/services/ProjectFileTests.cpp`
- Modify: `tests/integration/FoundationVerticalSliceTests.cpp`

**Interfaces:**
- Consumes: version-2 `ProjectSnapshot` and `Project::fromSnapshot()` from Task 1.
- Produces: v2 JSON with `assets` and `clips`; `loadProject()` that accepts v1 and v2, normalizing v1 to version 2 with empty media arrays.

- [ ] **Step 1: Write failing persistence tests for the v2 schema and v1 upgrade.**

Add a round-trip fixture with one asset and one clip, and a raw v1 fixture with no media fields:

```cpp
const ProjectSnapshot original {
    2, 42, "Round Trip", {{4, "audio/tone.wav", 48000, 2, 960}},
    {{9, 4, 48000, 0, 960}}
};
require(saveProject(original, path).has_value(), "v2 project saves");
const auto loaded = loadProject(path);
require(loaded.has_value() && loaded->formatVersion == 2, "v2 project loads");
require(loaded->assets == original.assets && loaded->clips == original.clips,
        "asset metadata and clip timing survive round trip");
```

For a v1 document, assert `formatVersion == 2`, `assets.empty()`, and `clips.empty()` after loading. Add malformed cases for duplicate asset IDs, duplicate clip IDs, missing clip asset references, zero metadata, out-of-range offsets, and oversized lengths.

- [ ] **Step 2: Run the persistence test to verify the schema tests fail.**

Run:

```bash
cmake --build build/headless --target dawg_project_file_tests
ctest --test-dir build/headless -R dawg.project_file --output-on-failure
```

Expected: the existing serializer rejects version 2 or drops the media collections, and the new invalid-document assertions fail.

- [ ] **Step 3: Implement v2 JSON encoding.**

In `ProjectFile.cpp`, emit exactly these public fields:

```json
{
  "formatVersion": 2,
  "projectId": 1,
  "name": "Playback",
  "tracks": [],
  "assets": [{"id": 1, "path": "audio/tone.wav", "pathKind": "projectRelative",
              "sampleRate": 48000, "channelCount": 1, "frameCount": 960}],
  "clips": [{"id": 1, "assetId": 1, "startSample": 0,
             "sourceOffset": 0, "lengthSamples": 960}]
}
```

Require `formatVersion == 2` for saves, validate the complete snapshot through `Project::fromSnapshot()`, and preserve the existing sibling-temporary-file transaction behavior.

- [ ] **Step 4: Implement strict v1/v2 decoding with normalization.**

Accept integer version 1 or 2. Decode the common project and track fields. For version 1, treat absent `assets` and `clips` as empty arrays and set `snapshot.formatVersion = 2` before model validation. For version 2, require both arrays, require every asset/clip field to have the expected JSON type, and decode `pathKind` as the exact strings `projectRelative` or `absolute`. Route duplicate IDs, unknown path kinds, references, and range failures through the existing `invalidFormat` error code with a nonempty diagnostic. Continue rejecting unsupported versions before narrowing values.

- [ ] **Step 5: Update persistence tests for replacement and failure atomicity.**

Keep the existing tests for invalid paths, malformed JSON, trailing data, and sibling temporary-file cleanup. Change round-trip expectations to version 2 and assert that saving a version-1 in-memory snapshot returns `unsupportedVersion`, while loading a version-1 file succeeds and can be saved back as version 2 after the caller receives the normalized snapshot.

- [ ] **Step 6: Update the foundation vertical slice to assert media persistence.**

Create one asset and clip in the existing headless integration test before saving; assert that a load returns the same asset and clip values. Keep the existing empty-graph silence assertion until Task 3 supplies the renderer.

- [ ] **Step 7: Run focused tests and commit persistence.**

Run:

```bash
cmake --build build/headless --target dawg_project_file_tests dawg_foundation_vertical_slice_tests
ctest --test-dir build/headless -R 'dawg\.(project_file|foundation_vertical_slice)$' --output-on-failure
git diff --check
git add services/persistence tests/services/ProjectFileTests.cpp tests/integration/FoundationVerticalSliceTests.cpp
git commit -m "feat(persistence): add project format v2 media metadata"
```

### Task 3: Add the JUCE-free bounded PCM ring and realtime clip renderer

**Files:**
- Create: `engine/include/dawg/engine/PlaybackRingBuffer.h`
- Create: `engine/src/PlaybackRingBuffer.cpp`
- Create: `engine/include/dawg/engine/PlaybackDescription.h`
- Modify: `engine/include/dawg/engine/RenderSnapshot.h`
- Modify: `engine/include/dawg/engine/TransportState.h`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `tests/engine/AudioEngineTests.cpp`
- Modify: `tests/integration/FoundationVerticalSliceTests.cpp`

**Interfaces:**
- Consumes: project clip values from Task 1 and existing transport/snapshot queues.
- Produces: `PlaybackRingBuffer`, `PlaybackDescription`, block-safe single-clip rendering, and `AudioEngine::underrunCount()` telemetry.

Use these exact public contracts:

```cpp
class PlaybackRingBuffer final {
public:
    bool prepare(std::size_t channelCount, std::size_t capacityFrames) noexcept;
    void reset() noexcept;
    void requestSourceFrame(std::uint64_t sourceFrame) noexcept;
    [[nodiscard]] std::uint64_t requestedSourceFrame() const noexcept;
    std::size_t write(std::uint64_t sourceFrame,
                      std::span<const float* const> source,
                      std::size_t frameCount) noexcept;
    std::size_t read(std::uint64_t sourceFrame,
                     std::span<float* const> destination,
                     std::size_t frameCount) noexcept;
    [[nodiscard]] std::size_t availableFrames() const noexcept;
};

struct PlaybackDescription {
    PlaybackRingBuffer* buffer {};
    std::uint64_t clipStartSample {};
    std::uint64_t clipLengthSamples {};
    std::uint64_t sourceOffset {};
    std::uint16_t channelCount {};
};
```

`write()` is called only by the media worker and `read()` only by the audio callback; the ring stores absolute source-frame coordinates so a seek or worker restart cannot make a stale chunk sound at the wrong timeline position. `requestSourceFrame()` is an atomic audio-to-worker hint and must not lock or allocate.

- [ ] **Step 1: Write failing ring and renderer tests.**

Add tests for a two-channel ring that writes eight frames and reads them by absolute source frame, rejects reads outside the buffered range, returns a bounded partial count when data is unavailable, and resets without retaining old samples. Add an engine test with a known ramp:

```cpp
PlaybackRingBuffer ring;
require(ring.prepare(1, 16), "ring prepares off the audio thread");
std::array<float, 4> source {0.25f, 0.5f, 0.75f, 1.0f};
std::array<const float*, 1> sourceChannels {source.data()};
require(ring.write(10, sourceChannels, source.size()) == source.size(),
        "worker writes absolute source frames");
```

Publish a snapshot containing a clip from timeline sample 2, source offset 10, length 4; enqueue play; process a block beginning at zero; assert silence for timeline samples 0–1 and exact ramp samples 2–5. Process after the clip and assert silence. Add a missing/empty ring test that asserts silence and increments `underrunCount()` without changing transport progression.

- [ ] **Step 2: Run engine tests to verify the renderer tests fail.**

Run:

```bash
cmake --build build/headless --target dawg_engine_core_tests
ctest --test-dir build/headless -R dawg.engine_core --output-on-failure
```

Expected: the ring and playback description symbols do not compile yet, and the existing transport tests remain the baseline to preserve.

- [ ] **Step 3: Implement the preallocated SPSC ring.**

Use a planar `std::vector<float>` allocated only by `prepare()`, power-of-two or modulo indexing with checked frame arithmetic, and atomics for the worker write range and audio read/request range. `read()` must copy only frames whose absolute positions are currently buffered, never wait for the writer, and return zero for a null destination channel. `write()` must never overwrite unread frames; return the number of frames accepted so the worker can retry later. `reset()` is callable during stopped lifecycle operations and clears all indices without deallocating prepared storage.

- [ ] **Step 4: Extend the fixed render snapshot and transport telemetry.**

Add `PlaybackDescription playback {}` to `RenderSnapshot`. Add a monotonic `std::uint64_t underrunCount {}` to `TransportState` and expose:

```cpp
[[nodiscard]] std::uint64_t underrunCount() const noexcept;
```

Keep `RenderSnapshot` trivially copyable in practice: it contains only scalar fields and a stable `PlaybackRingBuffer*`; it never owns a vector, string, reader, or project reference.

- [ ] **Step 5: Implement exact block rendering in `AudioEngine::process()`.**

Preserve the existing sequence: consume the latest snapshot, drain at most `TransportCommandQueue::capacity` commands, clear every supplied output channel, then render. Clamp negative seeks to zero. When `seekSamples` is zero, reset the playback ring request to the clip source offset. For a playing block, compute the overlap between `[samplePosition, samplePosition + frameCount)` and `[clipStartSample, clipStartSample + clipLengthSamples)`, using checked/saturating arithmetic. Request the corresponding absolute source frame, read only the overlap, map mono to every output channel and stereo channel `n` to output channel `n` where available, leave unavailable frames at the already-cleared zero, increment `underrunCount` for each missing frame range, and advance the transport by the entire block even outside the clip. Stop does not reset position; `reset()` resets transport position and underrun telemetry only where the existing lifecycle contract permits it.

- [ ] **Step 6: Add an allocation guard around the audio callback path.**

Extend the engine test executable with a scoped global allocation counter that is enabled only around `engine.process()`, including the required sized/aligned delete overloads. Prepare the engine, ring, and output before enabling the counter, then assert zero allocations while processing a block with a valid clip, an underrun, a null channel, and an empty output view. Keep the guard in the test executable so production ABI and allocator behavior are unchanged.

- [ ] **Step 7: Run all engine tests and commit the realtime renderer.**

Run:

```bash
cmake --build build/headless --target dawg_engine_core_tests dawg_foundation_vertical_slice_tests
ctest --test-dir build/headless -R 'dawg\.(engine_core|foundation_vertical_slice)$' --output-on-failure
git diff --check
git add engine tests/engine/AudioEngineTests.cpp tests/integration/FoundationVerticalSliceTests.cpp
git commit -m "feat(engine): render bounded single-clip playback"
```

### Task 4: Build the media service and JUCE WAV reader behind a decoder seam

**Files:**
- Create: `services/media/CMakeLists.txt`
- Create: `services/media/include/dawg/media/AudioAssetReader.h`
- Create: `services/media/include/dawg/media/AudioMediaService.h`
- Create: `services/media/src/AudioMediaService.cpp`
- Create: `platform/juce/media/JuceWavAssetReader.h`
- Create: `platform/juce/media/JuceWavAssetReader.cpp`
- Create: `tests/services/AudioMediaServiceTests.cpp`
- Create: `tests/platform/JuceWavAssetReaderTests.cpp`
- Modify: `CMakeLists.txt`
- Modify: `services/CMakeLists.txt`
- Modify: `platform/juce/CMakeLists.txt`

**Interfaces:**
- Consumes: `PlaybackRingBuffer` from Task 3 and project asset/clip metadata from Task 1.
- Produces: the decoder seam from the design spec, a worker-owned `AudioMediaService`, a JUCE WAV factory, typed media errors/status, and PCM ready for the engine without JUCE crossing the service boundary.

Define the decoder seam exactly as follows in `dawg/media/AudioAssetReader.h`:

```cpp
struct AudioAssetMetadata {
    std::uint32_t sampleRate {};
    std::uint16_t channelCount {};
    std::uint64_t frameCount {};
};

class AudioAssetReader {
public:
    virtual ~AudioAssetReader() = default;
    virtual AudioAssetMetadata metadata() const noexcept = 0;
    virtual bool read(std::uint64_t sourceFrame,
                      std::span<float* const> destination,
                      std::size_t frameCount) noexcept = 0;
};
```

Add `MediaError { enum class Code { invalidPath, cannotOpen, unsupportedFormat, invalidMetadata, readFailure, unavailable }; Code code; std::string message; }` and an abstract `AudioAssetReaderFactory::open(std::string_view)` returning `std::expected<std::unique_ptr<AudioAssetReader>, MediaError>`.

- [ ] **Step 1: Write failing fake-reader service tests.**

Create a deterministic fake reader with eight known mono frames and a factory that can return either the reader or a typed failure. Test that:

```cpp
AudioMediaService service(factory);
const auto inspected = service.inspect("tone.wav");
require(inspected.has_value() && inspected->sampleRate == 48000,
        "inspection returns WAV metadata");
auto asset = *inspected;
asset.id = 1;
require(service.prepare(asset, {1, asset.id, 0, 0, 8}, 16).has_value(),
        "service prepares a worker session");
service.start();
const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
while (service.buffer().availableFrames() != 8
       && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
require(service.buffer().availableFrames() == 8, "worker fills the bounded ring");
```

Cover worker stop/restart, reader-open failure, reader-read failure, missing assets, source offsets, and bounded capacity. Assert that the service exposes `unavailable` status and leaves the ring silent when the reader cannot open.

- [ ] **Step 2: Run the media service test to verify the seam is absent.**

Run:

```bash
cmake --build build/headless --target dawg_media_service_tests
ctest --test-dir build/headless -R dawg.media_service --output-on-failure
```

Expected: CMake does not yet define the media target and the test cannot compile.

- [ ] **Step 3: Add the JUCE-free media target and service ownership.**

Add `services/media` before the application target. Link it publicly to `dawg_engine_core`, compile it as C++20, and apply the existing warning/sanitizer options. Implement `AudioMediaService` so `inspect()` opens a reader on the message thread, validates metadata, and returns a value-owned `project::AudioAsset` draft; `prepare()` opens the reader once for the session, allocates the ring and worker scratch off the audio thread, and records the clip source range; `start()` and `stop()` own the worker lifecycle; and the destructor stops and joins before destroying the ring or reader.

- [ ] **Step 4: Implement the worker refill protocol.**

The worker must observe `PlaybackRingBuffer::requestedSourceFrame()`, reopen/reset the ring when the requested position jumps, read at most the free ring capacity into preallocated planar scratch, write accepted frames with absolute source coordinates, and publish `ready`, `unavailable`, or `error` status via atomics/value snapshots. The worker may wait or sleep; the audio thread may only write the atomic request and call the ring read. Do not call a condition-variable wait, filesystem API, decoder API, or logger from `AudioEngine::process()`.

- [ ] **Step 5: Implement the JUCE WAV reader and factory.**

In `platform/juce/media`, wrap `juce::AudioFormatReader` in `JuceWavAssetReader`, register `juce::WavAudioFormat` in a `JuceWavAssetReaderFactory`, convert the UTF-8 project path at the platform boundary, and return typed errors for an empty path, a missing file, an unsupported format, zero channels, zero sample rate, or zero length. The wrapper's `read()` must fill caller-owned float planes and return false on a decoder failure; it must not allocate on the audio thread because it is called only by the worker.

- [ ] **Step 6: Add the deterministic WAV fixture test.**

Use a test helper that writes a tiny RIFF/WAVE PCM file containing eight known mono 32-bit float-compatible samples to an isolated temporary directory. The platform test must inspect metadata through `JuceWavAssetReaderFactory`, read all eight frames, compare exact expected values, and assert typed errors for a missing file and a non-WAV document. Keep all fixture creation and deletion within the test directory guard.

- [ ] **Step 7: Register targets and run both media test layers.**

Add the headless service test to `CMakeLists.txt`. Add the JUCE reader test to the application-enabled test section, link it to `dawg_platform_juce`, and apply the existing strict warnings. Run:

```bash
cmake --build build/headless --target dawg_media_service_tests
ctest --test-dir build/headless -R dawg.media_service --output-on-failure
cmake --build build/app --target dawg_juce_wav_reader_tests
ctest --test-dir build/app -R dawg.juce_wav_reader --output-on-failure
git diff --check
git add services/media platform/juce/media tests/services/AudioMediaServiceTests.cpp tests/platform/JuceWavAssetReaderTests.cpp CMakeLists.txt services/CMakeLists.txt platform/juce/CMakeLists.txt
git commit -m "feat(media): stream WAV assets through a worker"
```

### Task 5: Compose import, transport controls, and status through the application/UI

**Files:**
- Modify: `apps/dawg/Application.h`
- Modify: `apps/dawg/Application.cpp`
- Modify: `apps/dawg/ApplicationState.h`
- Modify: `ui/shell/EditorViewModel.h`
- Modify: `ui/shell/MainEditorView.h`
- Modify: `ui/shell/MainEditorView.cpp`
- Modify: `ui/shell/MainComponent.h`
- Modify: `ui/shell/MainComponent.cpp`
- Modify: `apps/dawg/CMakeLists.txt`
- Modify: `tests/app/ApplicationStateTests.cpp`
- Modify: `tests/app/MainComponentOwnershipTests.cpp`

**Interfaces:**
- Consumes: controller/project APIs from Task 1, render/pipeline APIs from Task 3, `AudioMediaService` from Task 4, and the existing `JuceAudioDeviceBridge` lifecycle.
- Produces: message-thread-only import and transport callbacks, final render-snapshot composition, playback status, and deterministic shutdown ordering.

- [ ] **Step 1: Write failing application-state tests for playback commands and snapshot composition.**

Add a headless test that prepares an engine, creates a project with one asset and clip, binds a preprepared ring, and verifies:

```cpp
ApplicationState state(controller, engine);
state.setPlaybackBuffer(&ring);
require(state.flushRenderSnapshot(), "playback snapshot publishes");
require(engine.enqueueCommand({EngineCommandType::play, 0}), "play callback queues");
require(state.stop(), "stop callback queues");
require(state.returnToStart(), "return-to-start callback queues seek zero");
```

Process the block and assert FIFO semantics, updated project snapshots, and a copy-owned `EditorViewModel`. Add a status test for ready, unavailable media, and underrun count values.

- [ ] **Step 2: Run app-state tests to verify new controls are absent.**

Run:

```bash
cmake --build build/headless --target dawg_application_state_tests
ctest --test-dir build/headless -R dawg.application_state --output-on-failure
```

Expected: the new state methods and playback fields do not compile yet.

- [ ] **Step 3: Add application-state transport and render composition methods.**

Add these message-thread methods to `ApplicationState`:

```cpp
void setPlaybackBuffer(engine::PlaybackRingBuffer*) noexcept;
[[nodiscard]] bool play() noexcept;
[[nodiscard]] bool stop() noexcept;
[[nodiscard]] bool returnToStart() noexcept;
[[nodiscard]] std::optional<project::MediaAssetId> addAudioAsset(project::AudioAsset);
[[nodiscard]] std::optional<project::ClipId> addAudioClip(project::AudioClip);
```

`flushRenderSnapshot()` must copy the controller snapshot, choose the first valid clip and its asset for the single-track renderer, set `PlaybackDescription` fields, and publish the fixed value to the engine. It must publish an empty playback description when the project has no valid media or the media service has marked the asset unavailable. Queue-full results return false without changing UI-owned state.

- [ ] **Step 4: Add UI view-model values and controls.**

Extend `EditorViewModel` with owned playback fields: `hasAudioClip`, `clipLengthSamples`, `playbackPosition`, `playing`, `mediaAvailable`, and `mediaError`. Add `Import WAV`, `Play`, `Stop`, and `Return to start` buttons to `MainEditorView`; each button emits a value callback and never captures `Project`, `AudioEngine`, `AudioMediaService`, or a JUCE device object. Keep the existing add-track control and project label.

- [ ] **Step 5: Wire application import through JUCE's message thread.**

Make `Application` own `JuceWavAssetReaderFactory` and `AudioMediaService` in the destruction order `MainComponent`, media service, audio bridge, state, controller, engine, factory as required by actual references. The import callback opens a JUCE `FileChooser`, calls `inspect()`, registers the returned asset, creates a full-length clip at timeline sample zero, arms the media service with a ring capacity of at least four device blocks, binds the buffer in `ApplicationState`, starts the worker, and refreshes the view. Preserve the selected path as `absolute` when no project directory is known; use `projectRelative` only when a known project directory conversion is explicit, and never rewrite an absolute path silently. If any step fails, retain the prior project and show the typed error in the status value.

- [ ] **Step 6: Wire transport controls and timer status.**

Route the three UI actions to `ApplicationState::play()`, `stop()`, and `returnToStart()`. On the existing timer tick, flush pending render snapshots, read the engine transport state and media status, and update the view with position, playing state, media availability, and underrun count. Do not call engine telemetry from the audio callback into UI code.

- [ ] **Step 7: Preserve shutdown safety and update ownership tests.**

Stop the media worker before removing the JUCE audio callback, then destroy the window and UI, media service, audio bridge, state, controller, and engine in an order that prevents the audio callback or worker from retaining a ring pointer. Update `MainComponentOwnershipTests` to construct the new callbacks as no-op lambdas and verify the component still owns no service or engine references.

- [ ] **Step 8: Run headless app tests and commit composition/UI.**

Run:

```bash
cmake --build build/headless --target dawg_application_state_tests dawg_project_controller_tests
ctest --test-dir build/headless -R 'dawg\.(application_state|project_controller)$' --output-on-failure
cmake --build build/app --target DAWg
git diff --check
git add apps/dawg ui/shell tests/app
git commit -m "feat(app): expose WAV import and transport controls"
```

### Task 6: Complete the end-to-end vertical slice and document the delivery boundary

**Files:**
- Modify: `tests/integration/FoundationVerticalSliceTests.cpp`
- Modify: `tests/platform/JuceAudioCallbackAdapterTests.cpp`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/ROADMAP.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: the complete project/persistence/engine/media/application path from Tasks 1–5.
- Produces: a repeatable headless playback test, a supported-host JUCE/X11 integration run, and documentation that makes the DAWg/VersaTone boundary explicit.

- [ ] **Step 1: Replace the empty-graph integration assertion with an exact PCM scenario.**

Use a fake prefilled ring containing a known stereo or mono pattern, one project asset and one clip beginning at a nonzero timeline sample, and these assertions:

```cpp
require(engine.transportState().playing, "play command starts transport");
require(output[0][0] == 0.0f && output[0][1] == expected[0],
        "samples before and inside the clip render at exact offsets");
require(output[0][clipEnd] == 0.0f, "samples after the clip are silent");
require(engine.transportState().samplePosition == blockSize,
        "transport advances by the complete block");
```

Save and reload the same project during the test and verify that persistence does not alter the render description. Add a missing-media load case that publishes an empty playback source and asserts deterministic silence with a nonempty unavailable status.

- [ ] **Step 2: Extend the JUCE callback seam test for real transport-to-output behavior.**

Keep the adapter test device-free. Prepare the engine with a prefilled ring, call the existing adapter with planar output arrays, and assert that the callback passes the exact buffer count and produces the expected clip PCM without allocating or touching JUCE decoder state. Preserve tests for null output pointers and zero-length callbacks.

- [ ] **Step 3: Run the complete headless suite in Debug and ThreadSanitizer configurations.**

Run:

```bash
cmake -S . -B build/headless-debug -DDAWG_BUILD_APP=OFF -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/headless-debug -j2
ctest --test-dir build/headless-debug --output-on-failure
cmake -S . -B build/headless-tsan -DDAWG_BUILD_APP=OFF -DDAWG_BUILD_TESTS=ON -DDAWG_ENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/headless-tsan -j2
ctest --test-dir build/headless-tsan --output-on-failure
```

Expected: all headless tests pass; any existing ALSA LeakSanitizer baseline remains outside this playback plan because the app is not part of these configurations.

- [ ] **Step 4: Run the JUCE/X11 integration suite on a supported host.**

Configure and build with the application enabled, then run the window, callback, WAV, and app tests under the existing X11 host:

```bash
cmake -S . -B build/x11-release -DDAWG_BUILD_APP=ON -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/x11-release --target DAWg dawg_juce_audio_callback_adapter_tests dawg_juce_wav_reader_tests dawg_window_registry_tests dawg_window_manager_integration_tests -j2
ctest --test-dir build/x11-release -L 'platform|window-manager|app' --output-on-failure
```

Launch and close the built application explicitly, then record the result in the commit/hand-off:

```bash
test -n "${DISPLAY:-}" || { echo "DISPLAY is required for the DAWg launch smoke" >&2; exit 77; }
DAWG_BIN="$(find build/x11-release -type f -name DAWg -perm -111 -print -quit)"
test -n "$DAWG_BIN"
timeout 15s "$DAWG_BIN" &
DAWG_PID=$!
sleep 3
kill -TERM "$DAWG_PID"
wait "$DAWG_PID" || test "$?" -eq 143
```

If no DISPLAY is available, report the host limitation rather than treating a skipped X11 test as a passing integration result.

- [ ] **Step 5: Document the production boundaries and next slice.**

Update `docs/ARCHITECTURE.md` with the module graph `project -> persistence/commands -> application state -> engine/media -> JUCE adapter`, the snapshot pointer lifetime invariant, the worker/audio-thread contract, and v1-to-v2 normalization. Update `docs/ROADMAP.md` to mark the single-track playback milestone complete and leave multi-track mixing, recording, plugins, and waveform generation explicitly deferred. Update `README.md` with the headless and X11 build/test commands and the external-media behavior.

- [ ] **Step 6: Perform final verification and commit the vertical slice.**

Run:

```bash
git diff --check
git status --short
git log -8 --oneline
git add tests/integration/FoundationVerticalSliceTests.cpp tests/platform/JuceAudioCallbackAdapterTests.cpp docs/ARCHITECTURE.md docs/ROADMAP.md README.md
git commit -m "test(dawg): verify single-track playback vertical slice"
git status --short
```

Expected: the working tree is clean, the final commit is on `main`, and the hand-off names the exact headless and X11 test results rather than claiming unsupported-host coverage.
