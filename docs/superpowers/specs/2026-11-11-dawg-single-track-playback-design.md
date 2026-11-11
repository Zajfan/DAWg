# DAWg Single-Track Playback — Design Specification

**Status:** Proposed for implementation

**Date:** 2026-11-11

## 1. Purpose

This milestone turns DAWg's silent realtime foundation into a verifiable
single-track playback path. A user will be able to load one WAV asset into a
project, place it on the first track, press play, and hear deterministic audio
through the existing JUCE device bridge.

The milestone is deliberately narrow. It establishes the contracts needed for
recording, multiple clips, waveform analysis, and mixing without coupling the
realtime engine to JUCE, filesystem APIs, or a particular decoder.

## 2. User-visible outcome

The DAW shell will provide:

- a project with one audio track;
- an explicit WAV asset/clip reference in the project model;
- a minimal clip display showing the asset name, duration, and placement;
- play, stop, and return-to-start commands;
- clean silence when playback is stopped, outside the clip, or when decoded
  data is temporarily unavailable;
- a visible error/status path when an asset cannot be opened or is incompatible.

The first implementation supports one active audio clip at a time. The data
model must not make that limitation permanent: clip identity, timeline
placement, source asset identity, and channel metadata are represented
explicitly.

## 3. Boundaries and ownership

```text
JUCE audio device callback
        │
        ▼
platform/juce/audio callback adapter
        │  bounded AudioBufferView
        ▼
dawg_engine_core
  ├── transport state and commands
  ├── immutable playback snapshot
  └── realtime clip mixer

message/application thread
  ├── ProjectController owns project edits
  ├── MediaService opens/decodes WAV off the audio thread
  └── PlaybackBufferPublisher publishes prepared PCM blocks
```

Ownership rules:

- `AudioEngine` owns only callback-time playback state and bounded buffers.
- `ProjectController` owns project metadata and publishes immutable playback
  descriptions; it does not own the audio engine or a decoder.
- A media service owns file handles, decoder objects, worker threads, and
  decoded storage. It never runs on the audio callback.
- The application composition root owns the media service and coordinates
  project edits, playback preparation, and shutdown ordering.
- UI components receive value snapshots and callbacks. They do not own clips,
  readers, worker threads, or audio buffers.

## 4. Core data model

The project model gains value types with stable identity:

```cpp
using MediaAssetId = std::uint64_t;
using ClipId = std::uint64_t;

struct AudioAsset {
    MediaAssetId id {};
    std::string path;
    std::uint32_t sampleRate {};
    std::uint16_t channelCount {};
    std::uint64_t frameCount {};
};

struct AudioClip {
    ClipId id {};
    MediaAssetId assetId {};
    std::uint64_t startSample {};
    std::uint64_t sourceOffset {};
    std::uint64_t lengthSamples {};
};
```

`ProjectSnapshot` serializes assets and clips in format version 2. Version 1
files remain loadable and produce an empty asset/clip collection. Invalid paths,
zero IDs, duplicate IDs, out-of-range offsets, and clips referencing missing
assets are rejected without mutating the current project.

The project model does not inspect WAV headers. Header discovery belongs to the
media service, which returns metadata before the application commits an asset
to the project.

## 5. Realtime playback contract

The engine receives a fixed-size immutable playback description and PCM data
through preallocated publication storage. The callback performs only:

1. block-boundary adoption of the newest transport and playback snapshot;
2. bounded sample-position advancement;
3. ring-buffer reads for the active clip;
4. gain-free channel copy/mix into the supplied output;
5. silence fill for stopped, unavailable, underrun, or out-of-range samples.

The callback must not allocate, block, access files, decode, log, or call JUCE.
An underrun is represented by a bounded atomic counter/status flag consumed by
the message thread; it is not printed from the callback.

The first playback renderer uses one clip and one source asset. It maps source
sample `sourceOffset + (transportSample - startSample)` into the published PCM
ring. When the transport reaches the clip end, it stops advancing the active
clip and renders silence until another command is received.

## 6. Media service and buffering

The media service exposes a JUCE-free control interface and a platform-owned
decoder implementation:

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

The control thread requests a reader and schedules bounded read jobs. A worker
fills a preallocated interleaved/deinterleaved PCM ring buffer sized during
preparation. The audio thread observes producer/consumer indices atomically;
it never waits for a worker. The initial implementation may use JUCE's
`AudioFormatManager`/`AudioFormatReader` behind `platform/juce/media`, but those
types must not cross into `engine/`, `project/`, or public service contracts.

Unsupported formats, malformed WAV files, sample-rate mismatch, channel-count
mismatch, and read failures produce typed control-thread errors. The app keeps
the last valid playback state and exposes the error in the UI.

## 7. Transport behavior

Existing bounded commands remain the only message-to-audio control path:

```cpp
enum class EngineCommandType { play, stop, seekSamples };
```

The application maps play/stop/return-to-start controls to these commands.
`seekSamples(0)` is the return-to-start operation. Commands are applied in FIFO
order at the beginning of a block. The engine reports the current sample
position and playback state through a message-thread-safe snapshot/telemetry
surface that does not expose mutable callback storage.

## 8. Persistence and compatibility

Project file format version 2 adds `assets` and `clips` arrays while preserving
all version 1 fields. Save remains transactional. Load validates the complete
document before replacing application state. A missing asset on disk does not
make the project file unreadable; the project loads with an unavailable-media
status and renders silence until the user relinks or replaces the asset.

Paths are stored as user-selected project-relative paths when possible, with
an explicit path-kind field reserved for future content-addressed media. The
implementation must not silently rewrite absolute paths or claim portability
that has not been verified.

## 9. Testing and acceptance

Headless tests will cover:

- project asset/clip identity, validation, version-1 compatibility, and
  round-trip persistence;
- WAV metadata validation and malformed/unsupported-file errors;
- ring-buffer producer/consumer behavior, underrun silence, and reuse;
- transport play/stop/seek at exact block boundaries;
- one active clip rendering expected PCM samples and silence outside its range;
- no allocation or blocking on the audio render path;
- a complete vertical slice that loads a fixture WAV, publishes a clip,
  processes blocks, and verifies output samples.

The supported Linux/X11 app test will additionally verify that the real DAWg
window can launch, expose the playback shell, and close cleanly. Device
availability remains an environment concern; headless playback tests must not
require ALSA or an X11 display.

## 10. Explicit non-goals

- recording or microphone capture;
- multiple simultaneous clips or tracks in the renderer;
- time stretching, resampling, fades, gain, pan, or mixing;
- waveform generation;
- plugin hosting, MIDI, automation, or undo/redo;
- replacing the JUCE device layer;
- making external media portable by copying or embedding it.

These are follow-on milestones that consume the contracts established here.
