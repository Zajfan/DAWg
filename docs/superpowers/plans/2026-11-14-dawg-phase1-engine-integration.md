# DAWg Phase 1 — Five-Pillar Engine Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the 5-pillar audio engine into `Project` / `ProjectFile` (Format v3 persistence), `dawg_platform_juce` (live input capture + `JuceWavAssetWriterFactory`), and `ApplicationState` / `Application` (`CompiledGraph` + `DiskStreamPool` + `MeterTelemetry` + `AudioRecordService`).

**Architecture:** Sequential vertical tasks with strict TDD (RED -> GREEN -> REFACTOR):
1. **Task 1:** Project Tempo/Loop Model & Format v3 Transactional Persistence (`dawg_project_model`, `dawg_services_persistence`).
2. **Task 2:** Hardware Input Channel Forwarding & `JuceWavAssetWriterFactory` (`dawg_engine_core`, `dawg_platform_juce`).
3. **Task 3:** Multi-Track `CompiledGraph`, `DiskStreamPool`, Metering, & Recording Wiring in `GraphCompiler`, `ApplicationState`, `Application`, and End-to-End Integration Tests.

---

### Task 1: Project Tempo/Loop Model & Format v3 Persistence

**Files:**
- Modify: `project/include/dawg/project/ProjectTypes.h`
- Modify: `project/include/dawg/project/ProjectSnapshot.h`
- Modify: `project/include/dawg/project/ProjectCommand.h`
- Modify: `project/include/dawg/project/Project.h`
- Modify: `project/src/Project.cpp`
- Modify: `project/src/ProjectCommand.cpp`
- Modify: `services/persistence/src/ProjectFile.cpp`
- Modify: `tests/project/ProjectTests.cpp`
- Modify: `tests/services/ProjectFileTests.cpp`

- [ ] **Step 1:** Add failing tests in `tests/project/ProjectTests.cpp` for `SetProjectTempo` and `SetProjectLoop` commands and validation, and in `tests/services/ProjectFileTests.cpp` for format v3 round-trip persistence (including track `gainLinear`/`pan`/`phaseInvert`, clip `trackId`/`gainLinear`/`fadeInSamples`/`fadeOutSamples`, `tempo`, and `loop`).
- [ ] **Step 2:** Implement `TempoSetting`, `LoopSetting`, `SetProjectTempo`, `SetProjectLoop`, `Project::fromSnapshot` v2/v3 validation, and `ProjectFile` v3 encode/decode.
- [ ] **Step 3:** Run `dawg.project` and `dawg.project_file` tests and commit.

---

### Task 2: Hardware Input Channel Forwarding & `JuceWavAssetWriterFactory`

**Files:**
- Modify: `engine/include/dawg/engine/AudioBufferView.h`
- Modify: `engine/include/dawg/engine/AudioEngine.h`
- Modify: `engine/src/AudioEngine.cpp`
- Modify: `platform/juce/audio/JuceAudioCallbackAdapter.h`
- Modify: `platform/juce/audio/JuceAudioCallbackAdapter.cpp`
- Modify: `platform/juce/audio/JuceAudioDeviceBridge.cpp`
- Create: `platform/juce/media/JuceWavAssetWriter.h`
- Create: `platform/juce/media/JuceWavAssetWriter.cpp`
- Modify: `platform/juce/CMakeLists.txt`
- Modify: `tests/platform/JuceAudioCallbackAdapterTests.cpp`
- Modify: `tests/platform/JuceWavAssetReaderTests.cpp`

- [ ] **Step 1:** Add failing tests in `tests/platform/JuceAudioCallbackAdapterTests.cpp` (verifying `const float* const*` input channel capture into `AudioEngine`'s `RecordRingBuffer`) and `tests/platform/JuceWavAssetReaderTests.cpp` (verifying `JuceWavAssetWriterFactory` writes a multi-channel WAV file and `JuceWavAssetReaderFactory` reads back the exact PCM frames).
- [ ] **Step 2:** Implement `AudioInputBufferView`, the 5-argument `JuceAudioCallbackAdapter::process` overload, `JuceWavAssetWriter`, and `JuceWavAssetWriterFactory`.
- [ ] **Step 3:** Run `dawg.juce_audio_callback_adapter` and `dawg.juce_wav_reader` tests and commit.

---

### Task 3: `GraphCompiler`, `ApplicationState`, `Application`, & Multi-Track Integration

**Files:**
- Modify: `services/commands/src/GraphCompiler.cpp`
- Modify: `ui/shell/EditorViewModel.h`
- Modify: `apps/dawg/ApplicationState.h`
- Modify: `apps/dawg/Application.h`
- Modify: `apps/dawg/Application.cpp`
- Modify: `tests/app/ApplicationStateTests.cpp`
- Modify: `tests/integration/FoundationVerticalSliceTests.cpp`

- [ ] **Step 1:** Add failing tests in `tests/app/ApplicationStateTests.cpp` and `tests/integration/FoundationVerticalSliceTests.cpp` verifying multi-track `CompiledGraph` publication from `ApplicationState`, live meter telemetry in `EditorViewModel`, tempo/loop propagation to `CompiledGraph`, and end-to-end multi-track playback + v3 persistence + recording take creation.
- [ ] **Step 2:** Implement `GraphCompiler` tempo/loop propagation, `ApplicationState` multi-clip bindings + `CompiledGraph` publication + meter readout, and `Application` `DiskStreamPool` + `AudioRecordService` wiring.
- [ ] **Step 3:** Run full verification across `headless-debug` (`ASan+UBSan`), `headless-tsan` (`TSan+UBSan`), and `x11-release`, update docs, and commit.
