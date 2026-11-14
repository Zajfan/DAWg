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

## 0.3.0 — Five-Pillar Production Engine Core

- [x] **Pillar 1 (Compiled Render Graph & Epoch Handoff):** `CompiledGraph` (`CompiledTrack`, `CompiledClip`), `GraphCompiler`, and lock-free garbage-carrying `GraphHandoff`
- [x] **Pillar 2 (Multi-Track Scheduler, Channel Strip Mixer, & Metering):** Multi-track clip summing, `SmoothedParameter`, compensated equal-power pan law, phase invert, solo-in-place/mute resolution, per-clip fade-in/fade-out ramps, `SubBlockSplitter`, and lock-free `MeterTelemetry` (peak + RMS)
- [x] **Pillar 3 (Musical Timebase, Sub-Block Loop Engine, & Metronome):** `TempoMap` (960 PPQ sample $\leftrightarrow$ tick/bar-beat-tick conversion across piecewise tempo and time-signature events), sample-accurate sub-block `LoopRegion` wrapping with `PlaybackRingBuffer` retention window, and synthesized `Metronome`
- [x] **Pillar 4 (Scalable Disk I/O Pool, Resampling, & Recording Pipeline):** Multiplexed `DiskStreamPool` prioritized by lowest buffered frames, 4-point Catmull-Rom cubic Hermite `AudioResampler`, and lock-free `RecordRingBuffer` + `AudioRecordService` + `AudioAssetWriter` with latency-compensated timeline placement
- [x] **Pillar 5 (DAG Processor Graph, Automatic PDC, & Multi-Core Work-Stealing):** `ProcessorNode` (`GainPanNode`, `BiquadFilterNode`, `DelayCompensatorNode`, air-gapped `ExternalPluginNode`), `DspGraph` topological compiler with cycle detection and automatic Plugin Delay Compensation (PDC), and lock-free `WorkStealingScheduler`

## 0.4.0 — Phase 1 Engine Runtime & Format v3 Integration

- [x] Project `TempoSetting` and `LoopSetting` model + `SetProjectTempo` and `SetProjectLoop` commands
- [x] Format version 3 transactional JSON persistence (track mixer strip, clip fades/track assignment, tempo, and loop region) with v1/v2 compatibility
- [x] Hardware input channel forwarding (`AudioInputBufferView`, `JuceAudioCallbackAdapter`, `JuceAudioDeviceBridge`) and 32-bit float `JuceWavAssetWriterFactory`
- [x] Multi-track `CompiledGraph` compilation and publication in `ApplicationState`, live lock-free `MeterReading` in `EditorViewModel`, and `DiskStreamPool` + `AudioRecordService` wiring in `Application`

## 0.5.0 — Configurable Workbench UI (Next)

- [ ] Expose multi-track timeline editing, faders, pan knobs, mute/solo buttons, and live peak/RMS meters in the desktop shell
- [ ] Connect input device selection, track record arming, and `AudioRecordService` take creation in the shell
- [ ] Render graphical waveform thumbnails
- [ ] Host external CLAP/VST3 plugins through `ExternalPluginNode` adapters
