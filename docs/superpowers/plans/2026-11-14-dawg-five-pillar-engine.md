# DAWg Five-Pillar Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 5 production DAW engine pillars sequentially with strict TDD (RED -> GREEN), zero audio-thread allocations/deallocations, zero warnings (`-Wall -Wextra -Wpedantic -Werror`), and clean ASan+UBSan and TSan+UBSan runs.

**Architecture:** Build each pillar as an additive, air-gapped C++20 vertical slice inside `dawg_engine_core`, `dawg_project_model`, and `services/*`, preserving backward compatibility with existing single-clip and foundation tests while expanding the engine into a compiled multi-track DAG with musical timebase, loop wrapping, multiplexed disk I/O + recording + SRC, and PDC-compensated multi-core DSP execution.

---

### Task 1 (Pillar 1): Immutable Compiled Render Graph & Lock-Free Garbage-Carrying Handoff

**Files:**
- Create: `engine/include/dawg/engine/CompiledGraph.h`
- Create: `engine/include/dawg/engine/GraphHandoff.h`
- Create: `engine/src/GraphHandoff.cpp`
- Create: `services/commands/include/dawg/commands/GraphCompiler.h`
- Create: `services/commands/src/GraphCompiler.cpp`
- Modify: `project/include/dawg/project/ProjectTypes.h`
- Modify: `project/src/Project.cpp`
- Modify: `services/persistence/src/ProjectFile.cpp`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `services/CMakeLists.txt`
- Modify: `tests/engine/AudioEngineTests.cpp`
- Modify: `tests/services/ProjectControllerTests.cpp`

- [ ] **Step 1:** Write failing tests in `tests/engine/AudioEngineTests.cpp` and `tests/services/ProjectControllerTests.cpp` for `GraphHandoff` (publish, audio-thread `adopt` with zero allocations/deallocations, control-thread `reclaimRetired`, coalescing overwrite) and `GraphCompiler` compiling multi-track, multi-clip `ProjectSnapshot`s into `CompiledGraph`s.
- [ ] **Step 2:** Run `dawg_engine_core_tests` and `dawg_project_controller_tests` to verify RED.
- [ ] **Step 3:** Implement `CompiledGraph`, `GraphHandoff`, and `GraphCompiler`, and wire `AudioEngine::publishCompiledGraph` / `AudioEngine::reclaimRetiredGraphs` so `AudioEngine::process` adopts compiled graphs with zero audio-thread allocations or deallocations.
- [ ] **Step 4:** Verify GREEN across `dawg.engine_core` and `dawg.project_controller` and commit Pillar 1 (`feat(engine): add compiled render graph and lock-free epoch handoff`).

---

### Task 2 (Pillar 2): Multi-Track Timeline Scheduler, Channel Strip Mixer, Smooth Parameters, Fade Ramps, & Metering

**Files:**
- Create: `engine/include/dawg/engine/ChannelStrip.h`
- Create: `engine/include/dawg/engine/MeterTelemetry.h`
- Create: `engine/include/dawg/engine/SubBlockSplitter.h`
- Modify: `project/include/dawg/project/ProjectTypes.h`
- Modify: `project/include/dawg/project/ProjectCommand.h`
- Modify: `project/src/Project.cpp`
- Modify: `services/persistence/src/ProjectFile.cpp`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `tests/engine/AudioEngineTests.cpp`
- Modify: `tests/project/ProjectTests.cpp`

- [ ] **Step 1:** Write failing tests for multi-track summing, overlapping clips on a single track, clip fade-in/fade-out ramps, mute/solo-in-place matrix, equal-power stereo panning, phase inversion, smooth gain ramping (`SmoothedParameter`), sub-block event splitting (`SubBlockSplitter`), and lock-free master peak/RMS metering (`MeterTelemetry`).
- [ ] **Step 2:** Run `dawg_engine_core_tests` and `dawg_project_tests` to verify RED.
- [ ] **Step 3:** Implement `ChannelStrip`, `MeterTelemetry`, `SubBlockSplitter`, track/clip gain/pan/fade parameters in `ProjectTypes` and `CompiledGraph`, and multi-track rendering in `AudioEngine::process` with zero audio-thread allocations.
- [ ] **Step 4:** Verify GREEN and commit Pillar 2 (`feat(engine): add multi-track scheduler, channel strip mixer, and metering`).

---

### Task 3 (Pillar 3): Musical Timebase, Tempo Map, Sample-Accurate Loop Engine & Metronome

**Files:**
- Create: `engine/include/dawg/engine/TempoMap.h`
- Create: `engine/src/TempoMap.cpp`
- Create: `engine/include/dawg/engine/LoopRegion.h`
- Create: `engine/include/dawg/engine/Metronome.h`
- Create: `engine/src/Metronome.cpp`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `tests/engine/AudioEngineTests.cpp`

- [ ] **Step 1:** Write failing tests for `TempoMap` (sample $\leftrightarrow$ tick conversions across tempo and time-signature changes, `BarBeatTick` breakdown), mid-block loop wrapping (`[loopStart, loopEnd)` splitting a 64-frame block at an arbitrary sample boundary and wrapping clip playback seamlessly), and sample-accurate `Metronome` click synthesis on beat boundaries.
- [ ] **Step 2:** Run `dawg_engine_core_tests` to verify RED.
- [ ] **Step 3:** Implement `TempoMap`, `LoopRegion`, `Metronome`, and sub-block loop wrapping inside `AudioEngine::process`.
- [ ] **Step 4:** Verify GREEN (including zero-allocation guard around looped + metronome blocks) and commit Pillar 3 (`feat(engine): add tempo map, sub-block loop engine, and metronome`).

---

### Task 4 (Pillar 4): Scalable Disk I/O Pool (Multi-Voice Streamer, Recording Pipeline, & Sample-Rate Conversion)

**Files:**
- Create: `services/media/include/dawg/media/AudioResampler.h`
- Create: `services/media/src/AudioResampler.cpp`
- Create: `services/media/include/dawg/media/DiskStreamPool.h`
- Create: `services/media/src/DiskStreamPool.cpp`
- Create: `engine/include/dawg/engine/RecordRingBuffer.h`
- Create: `engine/src/RecordRingBuffer.cpp`
- Create: `services/media/include/dawg/media/AudioAssetWriter.h`
- Create: `services/media/include/dawg/media/AudioRecordService.h`
- Create: `services/media/src/AudioRecordService.cpp`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `services/media/CMakeLists.txt`
- Modify: `tests/services/AudioMediaServiceTests.cpp`
- Modify: `tests/engine/AudioEngineTests.cpp`

- [ ] **Step 1:** Write failing tests for `AudioResampler` (44.1 kHz and 96 kHz $\rightarrow$ 48 kHz streaming conversion with phase continuity), `DiskStreamPool` (streaming multiple voices concurrently with urgency-based refill and loop-seam lookahead), `RecordRingBuffer`, and `AudioRecordService` (capturing input audio from `AudioEngine::process` into a WAV writer on a worker thread with zero audio-thread allocations).
- [ ] **Step 2:** Run `dawg_media_service_tests` and `dawg_engine_core_tests` to verify RED.
- [ ] **Step 3:** Implement `AudioResampler`, `DiskStreamPool`, `RecordRingBuffer`, `AudioAssetWriter`, and `AudioRecordService`, and wire input capture in `AudioEngine::process`.
- [ ] **Step 4:** Verify GREEN and commit Pillar 4 (`feat(media): add multiplexed disk pool, streaming resampler, and recording pipeline`).

---

### Task 5 (Pillar 5): DAG Processor Node Graph, Automatic PDC, Built-In DSP, & Multi-Core Work-Stealing

**Files:**
- Create: `engine/include/dawg/engine/ProcessorNode.h`
- Create: `engine/include/dawg/engine/DspGraph.h`
- Create: `engine/src/DspGraph.cpp`
- Create: `engine/include/dawg/engine/WorkStealingScheduler.h`
- Create: `engine/src/WorkStealingScheduler.cpp`
- Modify: `engine/CMakeLists.txt`
- Modify: `tests/engine/AudioEngineTests.cpp`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/ROADMAP.md`
- Modify: `README.md`

- [ ] **Step 1:** Write failing tests for `ProcessorNode` (`GainPanNode`, `BiquadFilterNode`, `DelayCompensatorNode`, `ExternalPluginNode`), `DspGraph` topological compilation, cycle rejection, automatic Plugin Delay Compensation (PDC) aligning parallel branches with different node latencies to sample-exact phase cancellation/summing, and `WorkStealingScheduler` executing multi-branch wavefronts across worker threads with zero audio-thread allocations.
- [ ] **Step 2:** Run `dawg_engine_core_tests` to verify RED.
- [ ] **Step 3:** Implement `ProcessorNode`, `DspGraph`, and `WorkStealingScheduler`.
- [ ] **Step 4:** Run full verification across Debug (`ASan+UBSan`), ThreadSanitizer (`TSan+UBSan`), and X11 Release suites, update `docs/ARCHITECTURE.md`, `docs/ROADMAP.md`, and `README.md`, and commit Pillar 5 (`feat(engine): add DAG processor graph, automatic PDC, and work-stealing scheduler`).
