# DAWg Architecture

## Implemented playback vertical-slice boundary

The DAW application owns composition and lifecycle. The reusable engine, project
model, command coordinator, persistence service, and media streaming service do
not depend on JUCE, X11, or an audio device. The current vertical slice imports
an external WAV asset, persists its clip metadata in format v2 (transparently
normalizing legacy v1 project documents to v2 on load), streams decoded PCM
through a bounded worker-owned ring buffer, and renders deterministic single-clip
audio from the JUCE device callback.

The module graph is:

```text
DAWg application
├── apps/dawg + ui/shell (Application, ApplicationState, EditorViewModel)
├── platform/juce/audio  (JuceAudioDeviceBridge, JuceAudioCallbackAdapter)
├── platform/juce/media  (JuceWavAssetReaderFactory, JuceWavAssetReader)
└── platform/juce/window (WindowManager, ManagedWindow, WindowRegistry)
              │
              ▼
    ┌─────────┴───────────────────────────────────────┐
    ▼                                                 ▼
dawg_services_commands                      dawg_services_media
├── dawg_project_model                      ├── dawg_project_model
└── dawg_engine_core                        └── dawg_engine_core

dawg_services_persistence ──── dawg_project_model
```

Data and control flow follow the strict dependency chain:

`project -> persistence/commands -> application state -> engine/media -> JUCE adapter`

`AudioEngine`, `PlaybackRingBuffer`, `Project`, `ProjectController`,
`AudioMediaService`, and persistence headers expose standard-library/value types
only. `Application` is the composition root: it owns the reader factory, engine,
controller, application state, audio device bridge, media service, and window
manager, while the UI receives owned `EditorViewModel` snapshots and emits
commands/actions through callbacks. `MainComponent` and `MainEditorView` are pure
view boundaries and never own project, engine, media service, or device state.

The legacy VersaTone source tree is not a build dependency. DAWg's engine core
is the embedded implementation, with the JUCE audio and WAV decoder bridges kept
at the platform edge so the core and services can be reused by another host
without rewriting them.

## Module contracts

- `dawg_engine_core` is C++20 and can be built/tested with `DAWG_BUILD_APP=OFF`.
  It owns `AudioEngine`, `RenderSnapshotPublisher`, `TransportCommandQueue`,
  `PlaybackRingBuffer`, and `PlaybackDescription`.
- `dawg_project_model` is C++20 and owns stable project, track, media asset, and
  clip IDs, ordered value snapshots, clip bounds validation, and version 2
  snapshot reconstruction.
- `dawg_services_commands` applies project commands and media asset registration
  on the message thread and advances the render snapshot generation.
- `dawg_services_persistence` is C++23, writes format version 2 JSON documents
  (including `assets` and `clips`) through a sibling temporary file followed by
  atomic replacement, and normalizes legacy format version 1 files to version 2
  with empty media collections on load. Audio media stays external to `.dawg`
  project files (`projectRelative` or `absolute` path references).
- `dawg_services_media` is C++20 and JUCE-free. It defines the `AudioAssetReader`
  and `AudioAssetReaderFactory` decoder seam and owns `AudioMediaService`, which
  inspects WAV metadata on the message thread and runs a single background worker
  to refill a preallocated `PlaybackRingBuffer`.
- `dawg_platform_juce` adapts JUCE audio callbacks, device lifecycle, and
  `juce::WavAudioFormat` decoding (`JuceWavAssetReaderFactory`) to the engine and
  media service without exposing JUCE headers across the core or service layers.
- `platform/juce/window` owns native window operations: create/resize/close,
  child/modal relationships, fullscreen/maximized/minimized state, opacity,
  monitor selection, and logical/physical DPI conversions.

## Real-time audio and worker contracts

The device callback (`AudioEngine::process`) must remain bounded and predictable:

- Do not allocate or free memory.
- Do not perform file, network, or console I/O.
- Do not wait on a mutex or condition variable.
- Do not call decoder APIs or construct/destroy UI objects.
- Pass transport commands through the fixed-capacity SPSC `TransportCommandQueue`
  drained at the start of each block.
- Adopt render changes at block boundaries via the three-slot wait-free
  `RenderSnapshotPublisher`.

### Snapshot pointer lifetime invariant

`RenderSnapshot` is a fixed-size value containing `PlaybackDescription`, which
holds a raw `PlaybackRingBuffer*`. This pointer is owned by `AudioMediaService`
(or a test fixture) and must remain valid for every audio callback that can
observe the snapshot. During shutdown or media re-preparation, the application
stops the media worker and shuts down the JUCE audio device callback before
destroying `AudioMediaService`, `ApplicationState`, `ProjectController`, or
`AudioEngine`.

### Worker / audio-thread SPSC ring protocol

- `PlaybackRingBuffer::prepare()` allocates planar float storage off the audio
  thread before streaming starts.
- The audio callback computes the overlap between `[samplePosition, samplePosition + frameCount)`
  and `[clipStartSample, clipStartSample + clipLengthSamples)`, writes the needed
  absolute source frame via `requestSourceFrame()` (atomic store), and calls
  `read()` to copy only buffered frames matching that exact absolute source coordinate.
- Mono clips copy channel 0 to all output channels; stereo clips map channel `n`
  to output channel `n` where available. Missing or unbuffered frames remain silent
  (`0.0f`) and increment `AudioEngine::underrunCount()` without stalling transport
  progression.
- The background worker in `AudioMediaService` polls `requestedSourceFrame()`,
  resets the ring if the requested frame jumps outside the buffered window, reads
  decoded float planes through `AudioAssetReader`, and pushes chunks tagged with
  their absolute source frame.

## State ownership

The message thread owns editor components, `ProjectController`, `ApplicationState`,
and `AudioMediaService` lifecycle calls (`inspect`, `prepare`, `start`, `stop`).
The media worker thread owns streaming reads from `AudioAssetReader` and writes
into `PlaybackRingBuffer`. The audio thread owns callback-time transport and
ring consumption. UI components never act as storage for project, media, or
engine state.

## Verification

- `dawg.foundation_vertical_slice` prepares the engine and ring buffer, registers
  a track, WAV asset, and clip, saves and reloads the v2 project file, verifies
  that persistence preserves the `PlaybackDescription`, renders a block at a
  nonzero clip start offset, verifies exact PCM output and zero underruns, and
  verifies that missing/unavailable media renders deterministic silence.
- Headless tests (`dawg.engine_core`, `dawg.project`, `dawg.project_file`,
  `dawg.project_controller`, `dawg.media_service`, `dawg.application_state`,
  `dawg.juce_audio_callback_adapter`, `dawg.foundation_vertical_slice`) run with
  `DAWG_BUILD_APP=OFF` under Debug (`ASan+UBSan`) and ThreadSanitizer (`TSan+UBSan`).
- Platform and window-manager tests (`dawg.juce_wav_reader`,
  `dawg.window_registry`, `dawg.window_manager_integration`) run with
  `DAWG_BUILD_APP=ON` on a supported desktop display host.

## Configurable workbench boundary

The editor is a view over the project model, not the owner of musical data. Arrangement, pattern/step editing, idea-grid launching, recording, and mixing should be separate panels that can be composed into named workspaces. Workspace persistence is versioned UI state and must never duplicate or mutate project audio/MIDI/routing by itself. See the [workbench and workspace proposal](research/DAWG_WORKBENCH_AND_WORKSPACES.md).
