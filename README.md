# DAWg

DAWg is a cross-platform desktop digital audio workstation built with JUCE and C++26. Its reusable engine, project model, persistence, command coordinator, and media streaming services are kept independent of JUCE so the realtime core can serve future products as well as the DAW application.

## Current capabilities (Five-Pillar Production Engine + Single-Track Shell Slice)

- JUCE desktop application shell with explicit message-thread composition (`Application`, `ApplicationState`, `EditorViewModel`)
- A JUCE-free C++20 engine (`dawg_engine_core`) implementing all five core DAW engine pillars:
  1. **Compiled Render Graph & Lock-Free Epoch Handoff:** `CompiledGraph`, `GraphCompiler`, and garbage-carrying `GraphHandoff` (Treiber retirement stack for zero-allocation audio-thread handoff)
  2. **Multi-Track Scheduler, Channel Strip Mixer, & Metering:** Multi-track clip summing, `SmoothedParameter`, compensated equal-power pan law, phase invert, solo-in-place/mute resolution, per-clip fade-in/fade-out ramps, `SubBlockSplitter`, and lock-free `MeterTelemetry` (peak + RMS)
  3. **Musical Timebase, Sub-Block Loop Engine, & Metronome:** `TempoMap` (960 PPQ sample $\leftrightarrow$ tick/bar-beat-tick conversion across tempo and time-signature events), sample-accurate sub-block `LoopRegion` wrapping with `PlaybackRingBuffer` retention window, and synthesized `Metronome`
  4. **Scalable Disk I/O Pool, Resampling, & Recording Pipeline:** Multiplexed `DiskStreamPool` prioritized by lowest buffered frames, 4-point Catmull-Rom cubic Hermite `AudioResampler`, and lock-free `RecordRingBuffer` + `AudioRecordService` + `AudioAssetWriter` with latency-compensated timeline placement
  5. **DAG Processor Graph, Automatic PDC, & Multi-Core Work-Stealing:** `ProcessorNode` (`GainPanNode`, `BiquadFilterNode`, `DelayCompensatorNode`, air-gapped `ExternalPluginNode`), `DspGraph` topological compiler with cycle detection and automatic Plugin Delay Compensation (PDC), and lock-free `WorkStealingScheduler`
- A JUCE-free C++20 project model (`dawg_project_model`) with stable track, audio asset, and clip identity, channel strip parameters, clip fade/gain envelopes, tempo/loop settings, and bounds validation
- Format version 2 and version 3 transactional JSON persistence (`dawg_services_persistence`) that stores external media references (`projectRelative` or `absolute`), track mixer state, clip fades, tempo, and loop regions, and transparently upgrades version 1 files to version 2 on load
- A JUCE-free media streaming and recording layer (`dawg_services_media`) paired with JUCE WAV reader and writer adapters (`JuceWavAssetReaderFactory`, `JuceWavAssetWriterFactory`) that streams multi-voice float PCM and records 32-bit float WAV takes on background worker threads
- Shell transport controls (`Import WAV`, `Play`, `Stop`, `Return to start`) and live status telemetry (playback position, clip length, media availability, and underrun count)
- Display-free unit and integration tests covering engine, project, persistence, media worker, disk pool, resampler, recording pipeline, DAG/PDC/work-stealing, application state, callback adapter, and the end-to-end playback vertical slice

External WAV files are never embedded into `.dawg` project files; projects persist asset metadata and path references (`projectRelative` or `absolute`). If an external WAV file is missing or unreadable when a project or session is prepared, DAWg reports the media as unavailable and renders deterministic silence without crashing or blocking the audio thread.

## Requirements

- CMake 3.25 or newer
- A C++26-capable compiler and platform SDK for the application
- A C++23 standard library for persistence and a C++20 compiler for the reusable engine, project, command, and media targets
- Git and network access for the first configure (or cached FetchContent directories), so CMake can provide JUCE 9.0.4 and `nlohmann/json`

## Build and Verification

### Headless suite (no JUCE or display server required)

```sh
cmake -S . -B build/headless-debug -DDAWG_BUILD_APP=OFF -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/headless-debug -j2
ctest --test-dir build/headless-debug --output-on-failure

cmake -S . -B build/headless-tsan -DDAWG_BUILD_APP=OFF -DDAWG_BUILD_TESTS=ON -DDAWG_ENABLE_TSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/headless-tsan -j2
ctest --test-dir build/headless-tsan --output-on-failure
```

### Application and X11/platform suite

```sh
cmake -S . -B build/x11-release -DDAWG_BUILD_APP=ON -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/x11-release --target DAWg dawg_juce_audio_callback_adapter_tests dawg_juce_wav_reader_tests dawg_window_registry_tests dawg_window_manager_integration_tests -j2
ctest --test-dir build/x11-release -L 'platform|window-manager|app' --output-on-failure
```

The window-manager integration test (`dawg.window_manager_integration`) and application launch smoke test require a supported desktop display (`DISPLAY` on Linux/X11).

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md), and [docs/DEPENDENCIES_AND_LICENSING.md](docs/DEPENDENCIES_AND_LICENSING.md).

## Project status

Single-track WAV playback vertical slice complete. Multi-track mixing, audio recording, waveform thumbnails, timeline editing, and plugin hosting are explicitly deferred to the next milestones in [docs/ROADMAP.md](docs/ROADMAP.md).
