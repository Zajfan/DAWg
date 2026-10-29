# DAWg Architecture

## Current boundary

The current code has a small desktop application shell and an audio-device adapter. It opens the default output device and clears each output block to silence. It is a bootstrap, not yet an audio recorder or editor.

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

The current callback only clears output buffers, so it does not yet implement these future systems.

## Initial audio path

1. Select an input and output device and configure sample rate and block size.
2. Capture input into preallocated buffers on the device thread.
3. Move buffered samples to a recording worker that writes audio files.
4. Play clips through a background reader and preallocated playback buffers.
5. Mix tracks and route the result to the selected output.
6. Save project structure and media references outside the callback.

## State ownership

The message thread owns editor components. The audio thread owns callback-time processing state. Cross-thread changes should be explicit commands or snapshots; UI components must never act as the audio engine's storage.
