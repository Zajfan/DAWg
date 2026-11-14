# DAWg Phase 3 Design Specification: Studio DSP Suite, Interactive Editing, MIDI Synthesis & Offline Master Bounce

## 1. Executive Summary

Phase 3 transitions DAWg from a working multi-track playback/recording workstation into a full-scale, professional creative DAW studio. It activates our Pillar 5 DAG Processor Graph with native, real-time studio DSP processors (Parametric EQ, Dynamics Compressor, Lookahead Limiter), equips the timeline with fluid mouse manipulation and musical snapping, introduces a complete MIDI sequencing engine with a polyphonic synthesizer (`TnhcSynth`) and Piano Roll, and implements a high-throughput faster-than-realtime offline export and multi-track stem bouncing pipeline.

---

## 2. Architecture & Design Principles

In strict adherence to the **Zajfan Engineering Standard**:
- **100% JUCE-Free DSP, Engine & Project Core**: All DSP algorithms (biquads, envelopes, compressors, limiters, synths) and offline bounce services reside in non-JUCE modules (`dawg_engine_core`, `dawg_project_model`, `dawg_services_media`).
- **Real-Time Safety**: Zero dynamic memory allocations, zero locks, and zero blocking calls on the audio processing path.
- **Transactional Determinism**: All timeline edits, track manipulations, parameter changes, and MIDI note events are dispatched through immutable commands into [`ProjectController`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/services/commands/include/dawg/commands/ProjectController.h) with bit-exact undo/redo history.
- **PDC Compliance**: Lookahead processors (such as the Brickwall Limiter) report exact `latencySamples()`; the [`GraphCompiler`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/services/commands/include/dawg/commands/GraphCompiler.h) automatically aligns parallel signal paths and sidechains.

---

## 3. Four Core Pillars of Phase 3

### 3.1 Task 1: Native Studio DSP Suite (Pillar 5 Activation)
1. **`ProcessorNode` Interface**:
   ```cpp
   class ProcessorNode {
   public:
       virtual ~ProcessorNode() = default;
       virtual void prepare(double sampleRate, std::size_t maxBlockSize) noexcept = 0;
       virtual void reset() noexcept = 0;
       virtual void process(std::span<float* const> channels, std::size_t frameCount) noexcept = 0;
       [[nodiscard]] virtual std::uint32_t latencySamples() const noexcept = 0;
   };
   ```
2. **`ParametricEQNode`**:
   - 4-Band Direct Form II Transposed biquad filter.
   - Low-Shelf / High-Pass (Band 1), 2 Parametric Bell filters with Q control (Bands 2 & 3), High-Shelf / Low-Pass (Band 4).
   - Single-pole coefficient smoothing to eliminate zipper noise during knob movement.
3. **`DynamicsCompressorNode`**:
   - Feed-forward VCA compressor architecture.
   - Peak / RMS level detector, soft/hard knee interpolation, attack/release ballistics.
   - Make-up gain, bypass toggle, and lock-free gain reduction telemetry (`gainReductionDb`).
4. **`BrickwallLimiterNode`**:
   - Lookahead buffer (e.g. 64-128 samples = 1.3-2.6 ms at 48 kHz).
   - Declares `latencySamples() == lookaheadSamples` for automatic PDC compensation.
   - Peak detection and transparent attack release for ceiling enforcement (-0.1 dBFS).
5. **Console Mixer DSP Rack UI**:
   - Insert slots in [`MixerPanel`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/ui/shell/MixerPanel.h) to view, bypass, and adjust parameters for EQ, Compressor, and Limiter.

### 3.2 Task 2: Interactive Timeline Mouse Editing, Fades & Snapping
1. **Interactive Direct Manipulation in [`TimelineView`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/ui/shell/TimelineView.h)**:
   - **Clip Drag & Move**: Drag clip bodies horizontally along the timeline or vertically across tracks (`MoveClip`).
   - **Edge Trimming**: Hover near left or right edge turns cursor to resize cursor; dragging trims start (`sourceOffset` and `startSample`) or end (`lengthSamples`) non-destructively (`TrimClip`).
   - **Fade Handles**: Corner handles adjust `fadeInSamples` and `fadeOutSamples` with smooth cosine/linear curves (`SetClipGainFade`).
2. **Musical Grid Snapping**:
   - Quantized positioning options: Bar, 1/2, 1/4, 1/8, 1/16, 1/32, Triplet, or Free (Off).
   - Razor Split shortcut / tool (`S` key splits clip at playhead or click position via `SplitClip`).
3. **Keyboard Shortcuts**:
   - `Space`: Toggle Play/Stop.
   - `Delete` / `Backspace`: Remove selected clip (`RemoveClip`).
   - `Ctrl+D`: Duplicate selected clip (`DuplicateClip`).
   - `Ctrl+Z` / `Ctrl+Y`: Undo / Redo.

### 3.3 Task 3: MIDI Sequencing Engine, Polyphonic Synth (`TnhcSynth`) & Piano Roll
1. **MIDI Domain Types in [`ProjectTypes.h`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/project/include/dawg/project/ProjectTypes.h)**:
   - `MidiNote`: `{ id, pitch, startSample, lengthSamples, velocity }`.
   - `MidiClip`: `{ id, trackId, startSample, lengthSamples, std::vector<MidiNote> notes }`.
   - `TrackKind`: `audio` vs `instrument`.
2. **`TnhcSynthNode` (Built-in Polyphonic Subtractive Synthesizer)**:
   - 16-voice allocator with round-robin voice stealing.
   - Dual morphable bandlimited oscillators (Saw, Square/Pulse with PWM, Triangle, Sine, White Noise).
   - 4-pole State Variable Filter (SVF) with 24 dB/oct Lowpass, Bandpass, and Highpass with self-oscillation resonance.
   - Dual ADSR envelopes (Amp & Filter) + Modulation LFO.
3. **Piano Roll Editor Component**:
   - Note grid spanning C-2 to G8 (MIDI 0-127).
   - Bottom velocity lane for sample-accurate velocity editing.
   - Note creation, resizing, dragging, and deletion.

### 3.4 Task 4: Offline Master Bounce & Multi-Track Stem Export
1. **`OfflineRenderService`**:
   - Drives `AudioEngine::process` faster than real time without hardware clock throttling.
   - Exports the entire project timeline or active loop region to 24-bit / 32-bit float WAV or FLAC.
2. **Multi-Track Stem Rendering**:
   - Parallel offline rendering isolating each track while preserving channel effects, sends, and master summing behavior.
3. **Sample Rate Conversion & Dithering**:
   - Bandlimited sinc resampler (e.g. 48 kHz project → 44.1 kHz master).
   - TPDF (Triangular Probability Density Function) dither for 16-bit CD-quality deliverables.
4. **Export Dialog UI**:
   - Workbench dialog to configure filename, time range, stem selection, format, bit depth, and sample rate.
