# DAWg Phase 1 — Five-Pillar Engine Integration, JUCE Input/WAV Writer, & Format v3 Persistence Design Spec

**Date:** 2026-11-14  
**Status:** Approved for Implementation  
**Author:** Antigravity & User (The-No-hands-Company)

---

## 1. Executive Summary

With all five core DAW engine pillars implemented and verified in `dawg_engine_core`, `dawg_project_model`, `dawg_services_commands`, and `dawg_services_media`, Phase 1 bridges those capabilities into the project persistence format, the JUCE platform I/O layer, and the desktop application state coordinator:

1. **Project Model & Format v3 Persistence (`dawg_project_model`, `dawg_services_persistence`):**
   - Extend `ProjectSnapshot` and `Project` with project-level musical timebase (`TempoSetting`) and loop region (`LoopSetting`) commands (`SetProjectTempo`, `SetProjectLoop`).
   - Upgrade `ProjectFile` to support **format version 3** (while preserving full backward compatibility with format versions 1 and 2), persisting track mixer parameters (`gainLinear`, `pan`, `phaseInvert`, `muted`, `solo`), clip parameters (`trackId`, `gainLinear`, `fadeInSamples`, `fadeOutSamples`), `tempo`, and `loop`.
2. **JUCE Platform Input Capture & WAV Asset Writer (`dawg_platform_juce`):**
   - Add `AudioInputBufferView` to `dawg_engine_core` so `const float* const*` hardware input buffers flow into `AudioEngine::process` without `const_cast`.
   - Extend `JuceAudioCallbackAdapter` and `JuceAudioDeviceBridge` to forward hardware input channels into `AudioEngine::process`.
   - Implement `JuceWavAssetWriter` and `JuceWavAssetWriterFactory` (conforming to `dawg::media::AudioAssetWriter` and `AudioAssetWriterFactory`) using `juce::WavAudioFormat` for background recording take creation.
3. **ApplicationState & Application Runtime Wiring (`apps/dawg`, `ui/shell`):**
   - Upgrade `GraphCompiler::compile` to copy project `TempoSetting` and `LoopSetting` into the resulting `CompiledGraph`.
   - Upgrade `ApplicationState` to manage per-clip `ClipBufferBinding`s, compile and publish `CompiledGraph` snapshots via `AudioEngine::publishCompiledGraph`, and populate `EditorViewModel` with live lock-free `MeterLevels` (master and per-track), `TempoSetting`, and `LoopSetting`.
   - Upgrade `Application` to stream multi-clip projects through `DiskStreamPool` and wire `JuceWavAssetWriterFactory`, `RecordRingBuffer`, and `AudioRecordService` to `AudioEngine`.

---

## 2. Real-Time & Architectural Invariants

- `dawg_engine_core`, `dawg_project_model`, `dawg_services_commands`, `dawg_services_persistence`, and `dawg_services_media` remain 100% JUCE-free.
- `AudioEngine::process` performs zero heap allocations, locks, or blocking I/O when capturing input channels and rendering `CompiledGraph`.
- Transactional file saves in `ProjectFile` continue to write via an exclusive sibling temporary file and atomic `rename()`.
