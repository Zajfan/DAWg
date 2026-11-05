# DAWg Foundation Architecture Design

**Status:** Proposed foundation for implementation

**Date:** 2026-11-05

## Goal

Build DAWg as a professional, cross-platform digital audio workstation whose
realtime audio engine is independently usable by future products, while the
DAW application owns project editing, workspaces, commands, and presentation.

The first implementation milestone establishes the boundaries and a complete
testable vertical slice: create or open a project, create a track, configure
audio I/O, run a realtime processing graph, control transport, expose meters,
and save/reload the project.

## Current-state findings

The repository currently contains a JUCE application shell, a window manager,
and an audio adapter. The DAW target consumes the VersaTone engine through a
relative sibling path in CMake. The sibling engine contains useful existing
audio primitives, but its public surface currently mixes core engine, DSP,
file I/O, MIDI, and placeholder UI headers. The current DAW therefore has a
working shell but not yet a stable product architecture.

The foundation work will not copy the entire legacy engine into the app or
allow JUCE widgets to become engine dependencies. Existing VersaTone code will
be migrated behind explicit contracts as each subsystem gains tests.

## Architecture decision

DAWg becomes the owning repository for the clean engine boundary during the
initial product build. The engine is built as one or more JUCE-free CMake
targets under `engine/`. DAWg-specific application composition lives under
`apps/dawg/`, and platform integrations live under `platform/`. VersaTone can
later consume the exported engine target or a packaged release without a
rewrite.

The dependency direction is one-way:

```text
engine core  <-  project/model  <-  app services  <-  DAWg UI
      ^              ^                 ^             ^
      |              |                 |             |
 platform audio  persistence       JUCE adapter   window/platform
```

The engine never includes JUCE, window classes, UI components, filesystem
dialogs, or application singletons. The UI never owns musical or audio state.

## Repository structure

The target structure is:

```text
DAWg/
├── apps/
│   └── dawg/
│       ├── Application.cpp
│       ├── Application.h
│       └── Main.cpp
├── engine/
│   ├── include/dawg/engine/
│   │   ├── AudioBufferView.h
│   │   ├── AudioEngine.h
│   │   ├── EngineConfig.h
│   │   ├── EngineCommand.h
│   │   ├── RenderSnapshot.h
│   │   └── TransportSnapshot.h
│   ├── src/
│   │   ├── AudioEngine.cpp
│   │   ├── RenderGraph.cpp
│   │   └── Transport.cpp
│   └── CMakeLists.txt
├── project/
│   ├── include/dawg/project/
│   │   ├── Project.h
│   │   ├── ProjectCommand.h
│   │   ├── ProjectSnapshot.h
│   │   └── Track.h
│   ├── src/
│   └── CMakeLists.txt
├── platform/
│   └── juce/
│       ├── audio/JuceAudioDeviceBridge.cpp
│       ├── audio/JuceAudioDeviceBridge.h
│       ├── window/
│       └── CMakeLists.txt
├── services/
│   ├── media/
│   ├── persistence/
│   └── commands/
├── ui/
│   ├── shell/
│   ├── arrangement/
│   ├── mixer/
│   └── common/
├── tests/
│   ├── engine/
│   ├── project/
│   ├── services/
│   ├── platform/
│   └── integration/
└── docs/architecture/
```

The migration may temporarily retain compatibility files, but every new
feature must use the target structure. A source file has one primary reason to
change; files are split by responsibility rather than by arbitrary size.

## CMake target boundaries

The initial target graph is:

```text
dawg_engine_core       # C++ only, no JUCE or platform libraries
dawg_project_model     # C++ only, project state and commands
dawg_services         # background work and persistence interfaces
dawg_platform_juce    # JUCE device/window adapters
dawg_ui               # JUCE components and views
DAWg                  # application composition
```

Tests link to the smallest target that exercises the behavior. The top-level
build must not use an implicit sibling path to find VersaTone. The engine
target must be buildable and testable without a display server. Optional
platform targets may add JUCE or OS libraries explicitly.

## Realtime audio contract

`dawg_engine_core` exposes a prepared engine with a callback-facing operation
equivalent to:

```cpp
void process(AudioBufferView output, MidiBufferView midi,
             const TransportSnapshot& transport) noexcept;
```

The callback-facing path has these guarantees:

- no heap allocation or deallocation;
- no filesystem, network, console, or GUI I/O;
- no blocking mutex, condition variable, or unbounded wait;
- no ownership changes for project or plugin objects;
- bounded work proportional to the configured graph and block size;
- all resources needed by the current graph are prepared before processing;
- parameter changes arrive through preallocated commands or published
  immutable snapshots;
- errors are recorded in atomics or bounded diagnostic state for later
  inspection by a non-realtime thread.

The JUCE device callback only adapts channel pointers and block metadata into
the engine contract. Device enumeration, restart, permission errors, and
device-loss recovery remain platform/service responsibilities.

## State and data flow

The message thread and background workers own editable project state. A
command coordinator validates edits, records undo information, and publishes a
new immutable render snapshot. The audio thread consumes the latest prepared
snapshot at a block boundary. It never reads mutable UI objects or walks the
project model directly.

```text
UI gesture
   -> ProjectCommand
   -> command coordinator / undo journal
   -> ProjectSnapshot
   -> render preparation
   -> atomic snapshot publication
   -> realtime audio process()
```

The project model contains tracks, clips, routing, automation, tempo, and
metadata. It does not contain JUCE components. Workspace layout and window
state are separate versioned UI state and never duplicate project audio,
MIDI, or routing data.

## Project and persistence rules

Projects use an explicit format version and stable identifiers for tracks,
clips, media references, and plugins. Saving is transactional: write a new
temporary document, flush it, then replace the destination. Loading and media
discovery happen off the message and audio threads. A failed load leaves the
current project untouched and returns a typed diagnostic suitable for the UI.

The first persistence slice stores project metadata and an empty-track model;
audio media copying, waveform caches, and plugin state are added only after
the versioned document contract is tested.

## Window and UI boundary

The existing window manager becomes a platform/UI service. It owns native
window lifetime, child/modal relationships, monitor selection, DPI behavior,
and native state synchronization. It does not own project or engine state.

The main editor is composed from views that read immutable view models and
send commands through application services. Arrangement, mixer, transport,
browser, and editor panels are independently replaceable components. UI
rendering must never be required to keep audio running.

## Migration strategy

Migration is incremental and test-led:

1. Create the clean engine and project CMake targets with JUCE-free contract
   tests.
2. Move the current audio callback adapter behind
   `JuceAudioDeviceBridge`, preserving silent output behavior while the new
   engine contract is introduced.
3. Introduce project state, track creation, transport commands, and immutable
   render snapshots as a complete vertical slice.
4. Migrate stable VersaTone DSP primitives behind engine-owned interfaces with
   behavior tests; do not expose legacy headers directly to the application.
5. Add media services, recording, playback, mixing, plugins, MIDI, and
   rendering as separate milestones with their own contracts and integration
   tests.

Legacy VersaTone code is not deleted as part of the foundation migration. It
is either wrapped by a tested adapter or retired only after equivalent engine
tests and a clean replacement exist.

## Testing and quality gates

Every module has fast unit tests and the application has integration tests.
The foundation gates are:

- engine tests run without JUCE or X11;
- project command and serialization tests cover valid, invalid, and
  version-mismatch documents;
- realtime tests verify processing does not allocate on the callback path;
- platform tests cover device lifecycle and the existing window-manager
  behavior on supported hosts;
- an integration test creates a project, adds a track, prepares the engine,
  renders deterministic silence, saves, reloads, and compares snapshots;
- Debug builds run AddressSanitizer/UndefinedBehaviorSanitizer, with a
  separate ThreadSanitizer configuration;
- Release builds use the same public headers and warning policy as the app;
- no task is complete without a clean build, relevant tests, and a documented
  realtime or ownership decision where applicable.

## Foundation acceptance criteria

The foundation is complete when:

1. `DAWg` builds without a hardcoded relative VersaTone source directory.
2. `dawg_engine_core` builds and tests without JUCE or a display server.
3. The JUCE adapter can start and stop a device and forward one bounded audio
   block to the engine.
4. A project can be created, given a track, saved, loaded, and compared by
   stable snapshot data.
5. Transport commands change state without direct UI-to-audio-thread access.
6. The main window remains a consumer of application state rather than a
   storage location for project data.
7. The directory layout, target graph, realtime rules, and migration decisions
   are represented in code, tests, and this document.
