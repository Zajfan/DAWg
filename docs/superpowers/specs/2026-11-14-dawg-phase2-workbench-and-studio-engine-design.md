# DAWg Phase 2 — Workbench UI, Live Recording, Undo/Redo & Waveform Engine Design Spec

**Date:** 2026-11-14  
**Status:** Approved for Implementation  
**Author:** Antigravity & User (The-No-hands-Company)

---

## 1. Overview & Architecture Goals

This document specifies the complete transition of **DAWg** from an engine foundation into a fully interactive, commercial-grade Digital Audio Workstation adhering to the **TNHC / Zajfan Engineering Standard**:

1. **Transactional Undo/Redo History & Timeline Editing Commands (`dawg_project_model`, `dawg_services_commands`):**
   - Comprehensive undo/redo journal in `ProjectController` (`undo()`, `redo()`, `canUndo()`, `canRedo()`).
   - Rich non-destructive clip and track commands: `MoveClip`, `TrimClip`, `SplitClip`, `DuplicateClip`, `RemoveClip`, `SetClipGainFade`, `SetTrackRecordArm`, `ReorderTracks`.
2. **Waveform Thumbnail Cache (`dawg_services_media`):**
   - Multithreaded background decimation worker calculating min/max peak pyramids across multiple Levels of Detail (LOD: 256, 64, 16, 1 samples/pixel).
   - Thread-safe, non-blocking visual query API returning cached peak buckets for 60 FPS timeline rendering without audio dropouts or UI stalls.
3. **Live Hardware Recording Workflow (`dawg_services_media`, `apps/dawg`):**
   - Track record arming (`Track::recordArmed`).
   - Transport record punch-in/out: hardware input captured by `AudioEngine::process` into `RecordRingBuffer`, written to a timestamped WAV file by `AudioRecordService`.
   - On transport stop, take is automatically finalized, registered as an `AudioAsset` and `AudioClip` on the armed track, bound to `DiskStreamPool`, and immediately ready for playback.
4. **Configurable Workbench UI & Mixer Console (`ui/shell`):**
   - **Musical Transport Bar:** Play, Stop, ReturnToStart, Record button, BPM and Time Signature (`4/4`, `3/4`, `6/8`, etc.), Metronome click toggle, Loop region toggle and range display, Undo / Redo buttons.
   - **Console Mixer Panel:** Channel strips for each track (fader with dB readout, pan pot, mute, solo, phase invert, record arm) + Master Bus strip with 60 FPS real-time Peak & RMS LED-style level meters with smooth decay.
   - **Multi-Track Timeline View:** Measure/Beat ruler computed from `TempoMap`, track lanes, audio clip blocks with waveform rendering from `WaveformCache`, scrubbable playhead cursor, and loop region bracket overlays.

---

## 2. Invariants & Real-Time Safety

- Zero heap allocations, locks, or system calls on the audio callback thread (`AudioEngine::process`).
- `dawg_engine_core`, `dawg_project_model`, `dawg_services_commands`, `dawg_services_persistence`, and `dawg_services_media` remain 100% JUCE-free.
- Waveform generation runs asynchronously in background workers, never blocking the UI or audio threads.
- All timeline edits are non-destructive and fully reversible via the undo/redo stack.
