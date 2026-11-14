# DAWg Roadmap

## 0.1.0 — Foundation

- [x] JUCE application shell and CMake build entry point
- [x] Explicit application composition with UI state/view boundaries
- [x] Reusable JUCE-free engine target and deterministic silent callback
- [x] Bounded transport command queue and block-boundary render snapshots
- [x] Stable project model, typed commands, and versioned transactional persistence
- [x] Audio-device/callback bridge and window manager for native window state
- [x] Display-free project/audio foundation vertical-slice test
- [x] Audio-device initialization and status display
- [x] Define real-time audio constraints
- [ ] Confirm builds on the target operating systems and compilers
- [ ] Add audio device settings UI

The foundation is complete on the supported Linux/X11 development host. The
remaining 0.1.0 checks are cross-platform execution and product-facing device
configuration UI, not prerequisites for the JUCE-free engine boundary.

## 0.2.0 — Transport and single-track audio playback

- [x] Connect play, stop, and return-to-start controls to the application command surface
- [x] Import external WAV files (`projectRelative` and `absolute` asset references)
- [x] Upgrade project persistence to format v2 with automatic v1-to-v2 normalization on load
- [x] Stream WAV PCM through a JUCE-free media worker and bounded SPSC ring buffer
- [x] Single-track clip playback with deterministic seeking and underrun telemetry
- [x] Show clip duration, playback position, underrun count, and media availability in the shell
- [ ] Render graphical waveform thumbnails (explicitly deferred to the next UI slice)

Single-track WAV playback is complete across the project model, v2 persistence,
worker-backed media service, JUCE WAV reader adapter, realtime audio engine, and
application shell. Multi-track mixing, audio recording, plugin hosting, and
waveform thumbnail generation remain explicitly deferred to subsequent milestones.

## 0.3.0 — Recording (Deferred)

- [ ] Select input device and channels
- [ ] Record input to WAV through a preallocated buffer and worker thread
- [ ] Place takes on the arrangement timeline
- [ ] Handle device loss and disk-write errors clearly

## 0.4.0 — Multitrack arrangement (Deferred)

- [ ] Create, rename, reorder, and remove audio tracks
- [ ] Move, trim, split, and duplicate clips
- [ ] Add timeline zoom and horizontal scrolling
- [ ] Save and reopen projects from the application shell

## 0.5.0 — Basic mixing (Deferred)

- [ ] Multi-track summing, track gain, and pan
- [ ] Mute and solo processing on the audio thread
- [ ] Master output and level meters
- [ ] Export stereo mix to WAV

## Later (Deferred)

- Non-destructive edits, fades, automation, plugin hosting, MIDI, virtual instruments, undo/redo, and expanded format support.
