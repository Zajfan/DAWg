# DAWg Architecture

## Implemented foundation boundary

The DAW application owns composition and lifecycle. The reusable engine and
project services do not depend on JUCE, X11, or an audio device. The current
application opens the default output device and clears each output block to
deterministic silence; the foundation is intentionally not yet an audio
recorder or editor.

The target graph is:

```text
DAWg application
├── apps/dawg + ui/shell
├── platform/juce/audio       ──┐
└── platform/juce/window       │ JUCE/platform boundary
                               ▼
                    dawg_services_commands
                    ├── dawg_project_model
                    └── dawg_engine_core

dawg_services_persistence ──── dawg_project_model
```

`AudioEngine`, `Project`, `ProjectController`, and persistence headers expose
standard-library/value types only. `Application` is the composition root: it
owns the engine, controller, audio bridge, and window manager, while the UI
receives owned snapshots and sends commands through callbacks. `MainComponent`
is a view boundary, not storage for project or audio state.

The legacy VersaTone source tree is not a build dependency. DAWg's engine core
is now the embedded implementation, with the JUCE bridge kept at the platform
edge so the core can be reused by another future host without rewriting it.

## Foundation contracts

- `dawg_engine_core` is C++20 and can be built/tested with `DAWG_BUILD_APP=OFF`.
- `dawg_project_model` is C++20 and owns stable project/track IDs, ordered
  snapshots, and command validation.
- `dawg_services_commands` applies project commands on the message thread and
  produces a small immutable render snapshot.
- `dawg_services_persistence` is C++23, uses version-one JSON at the filesystem
  boundary, and saves through a sibling temporary file followed by replacement.
- `dawg_platform_juce` adapts JUCE audio callbacks and device lifecycle to the
  engine without exposing JUCE from the engine target.
- `platform/juce/window` owns native window operations: create/resize/close,
  child/modal relationships, fullscreen/maximized/minimized state, opacity,
  monitor selection, and logical/physical DPI conversions.

## Planned layers

- **Application shell:** windows, menus, commands, and editor panels.
- **Audio engine:** device lifecycle, transport clock, routing, and real-time rendering.
- **Project model:** tracks, clips, regions, routing, and automation.
- **Media services:** import, waveform analysis, background disk reads/writes, and export.
- **Persistence:** versioned project metadata and references to audio media.
- **Editors:** arrangement, track controls, meters, and later MIDI piano roll.

Keep the project model independent of JUCE widgets so it can be edited, serialized, and tested without a running UI.

## Real-time audio rules

The device callback must remain bounded and predictable:

- Do not allocate or free memory.
- Do not perform file, network, or console I/O.
- Do not wait on a mutex or condition variable.
- Do not construct or destroy UI objects.
- Pass commands and recorded samples through preallocated lock-free or wait-free queues where practical.
- Move disk reads, recording writes, waveform generation, and project loading to worker threads.
- Publish parameter changes using atomics or immutable snapshots with a clear lifetime.

The current callback only applies bounded transport commands, adopts the latest
three-slot render snapshot, and clears output buffers. It does not yet
implement recording or playback, but those future systems must preserve these
same callback guarantees.

The transport boundary is a fixed-capacity SPSC queue drained at the start of a
block. Render snapshots are published by the message thread and become visible
to the audio thread only at a process boundary. A full queue/publication slot
returns `false` to the producer; neither path waits or allocates.

## Initial audio path

1. Select an input and output device and configure sample rate and block size.
2. Capture input into preallocated buffers on the device thread.
3. Move buffered samples to a recording worker that writes audio files.
4. Play clips through a background reader and preallocated playback buffers.
5. Mix tracks and route the result to the selected output.
6. Save project structure and media references outside the callback.

## State ownership

The message thread owns editor components. The audio thread owns callback-time processing state. Cross-thread changes should be explicit commands or snapshots; UI components must never act as the audio engine's storage.

## Verification

The display-free `dawg.foundation_vertical_slice` test prepares the engine,
dispatches an audio-track command, persists and reloads the snapshot, publishes
the render snapshot, queues play, processes one block, and verifies deterministic
silence. Window-manager tests are separately labelled because they require a
real supported desktop host.


## Configurable workbench boundary

The editor is a view over the project model, not the owner of musical data. Arrangement, pattern/step editing, idea-grid launching, recording, and mixing should be separate panels that can be composed into named workspaces. Workspace persistence is versioned UI state and must never duplicate or mutate project audio/MIDI/routing by itself. See the [workbench and workspace proposal](research/DAWG_WORKBENCH_AND_WORKSPACES.md).
