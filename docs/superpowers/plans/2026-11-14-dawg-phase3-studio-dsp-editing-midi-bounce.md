# DAWg Phase 3 Implementation Plan: Studio DSP Suite, Interactive Editing, MIDI Synthesis & Offline Master Bounce

## Objective
Deliver Phase 3 to complete the core studio DAW capabilities:
- **Task 1:** Native Studio DSP Suite (4-Band Parametric EQ, Dynamics Compressor, Lookahead Limiter, and Mixer Rack UI)
- **Task 2:** Direct Timeline Mouse Editing, Edge Trimming, Corner Fades, Musical Grid Snapping & Keyboard Shortcuts
- **Task 3:** MIDI Sequencing Engine, Polyphonic Synthesizer (`TnhcSynth`), and Piano Roll Editor
- **Task 4:** Faster-Than-Realtime Offline Master Bounce, Multi-Track Stem Export, and TPDF Dithering

---

## Task 1: Native Studio DSP Suite (Pillar 5 Activation)

### 1.1 DSP Processor Nodes in `engine/include/dawg/engine/dsp/`
- Implement `ParametricEQNode`:
  - 4 bands: Band 1 (HP/Low-Shelf), Band 2 (Peak Bell), Band 3 (Peak Bell), Band 4 (High-Shelf/LP).
  - Direct Form II Transposed biquad with coefficient smoothing.
- Implement `DynamicsCompressorNode`:
  - Threshold, ratio, attack, release, makeup gain, soft knee.
  - Level detection and gain reduction telemetry.
- Implement `BrickwallLimiterNode`:
  - Lookahead buffer with sample latency reporting (`latencySamples() == lookaheadSamples`).
  - Transparent peak limiting with ceiling -0.1 dBFS.

### 1.2 GraphCompiler & AudioEngine Integration
- Add DSP processor chain to `CompiledGraph` track execution pipeline.
- Automatic PDC delay compensation across parallel tracks when lookahead limiter or latency-inducing processors are active.

### 1.3 Mixer DSP Rack UI
- Expand `MixerPanel` to host DSP rack slots per channel strip and on the master strip.
- Interactive parameter controls for EQ, compressor, and limiter.

### 1.4 Verification
- Unit tests: frequency response checks for EQ biquads, compression curve tests for compressor, lookahead & ceiling tests for limiter.
- Sanitizer checks: ASan, UBSan, TSan.

---

## Task 2: Interactive Timeline Mouse Editing & Musical Snapping

### 2.1 Direct Clip Manipulation in `TimelineView`
- Mouse hit-testing for clip body, left/right edges (resize handles), and top corners (fade handles).
- Dragging clip body: moves clip along timeline or between tracks (`MoveClip`).
- Dragging left/right edge: trims `sourceOffset` and `lengthSamples` (`TrimClip`).
- Dragging corner handles: adjusts `fadeInSamples` and `fadeOutSamples` (`SetClipGainFade`).

### 2.2 Musical Grid Snapping
- Snap calculations locked to tempo map (Bar, 1/2, 1/4, 1/8, 1/16, Triplet, Off).
- Razor split tool and keyboard shortcuts (`S` to split, `Delete` to remove, `Ctrl+D` to duplicate, `Space` for play/stop, `Ctrl+Z` / `Ctrl+Y` for undo/redo).

### 2.3 Verification
- Headless mouse simulation and command dispatch tests.
- Verify undo/redo restores exact clip positions, trims, and fades.

---

## Task 3: MIDI Engine, Polyphonic Synth (`TnhcSynth`) & Piano Roll

### 3.1 MIDI Project Types & Commands
- Add `MidiNote`, `MidiClip`, and `TrackKind` to `ProjectTypes.h`.
- Add `AddMidiClip`, `AddMidiNote`, `RemoveMidiNote`, `MoveMidiNote`, `ResizeMidiNote` commands.

### 3.2 Built-in Polyphonic Synthesizer (`TnhcSynthNode`)
- 16-voice polyphonic synthesizer with voice stealing.
- Dual band-limited morphable oscillators (Saw, Square, Triangle, Sine, Noise).
- 4-pole resonant State Variable Filter (SVF).
- Dual ADSR envelopes + LFO.

### 3.3 Piano Roll Editor Component
- Note grid display (C-2 to G8), velocity lane, drawing/erasing/resizing notes.
- Integrated into workbench layout.

### 3.4 Verification
- Unit tests: polyphonic voice allocation, note-on/note-off processing, audio rendering determinism.
- Sanitizers: ASan, UBSan, TSan.

---

## Task 4: Offline Master Bounce & Multi-Track Stem Export

### 4.1 `OfflineRenderService`
- Standalone offline engine driving `AudioEngine::process` faster than real time.
- Direct output to 24-bit / 32-bit float WAV or FLAC.

### 4.2 Stem Export & Dithering
- Multi-track parallel stem rendering.
- Bandlimited sinc resampler and TPDF dithering for 16-bit output.

### 4.3 Export Dialog UI
- Workbench export dialog with range, format, sample rate, bit depth, and stem choices.

### 4.4 Verification
- Full offline render bit-exact comparison tests against real-time playback render.
