# DAWg Five-Pillar Engine Architecture Design

- **Date:** 2026-11-14
- **Status:** Approved for sequential vertical-slice implementation
- **Scope:** `dawg_engine_core`, `dawg_project_model`, `dawg_services_commands`, `dawg_services_media`, `dawg_services_persistence`, `dawg_platform_juce`

## 1. Objective

Upgrade `DAWg` from its single-clip playback proof-of-life into a production-grade, commercial-caliber DAW audio engine while strictly preserving the TNHC / Zajfan Engineering Standard:

- **100% JUCE-free and display-free `dawg_engine_core`, `dawg_project_model`, and `services/*`.**
- **Zero heap allocations or deallocations on the audio thread** (verified by global `operator new`/`operator delete` interception in `dawg.engine_core`).
- **Zero locks, condition variables, exceptions, or I/O on the audio thread.**
- **Zero compiler warnings** under `-Wall -Wextra -Wpedantic -Werror`.
- **Clean ASan+UBSan and TSan+UBSan verification** across all headless test suites.

---

## 2. The Five Engine Pillars

### Pillar 1: Immutable Compiled Render Graph & Lock-Free Garbage-Carrying Handoff

1. **`CompiledGraph` (`engine/include/dawg/engine/CompiledGraph.h`):**
   - Replaces fixed single-clip scalar assumptions with a flat, cache-contiguous execution plan owned by a heap-allocated immutable object constructed exclusively on the control/message thread.
   - Contains contiguous vectors of `CompiledTrack` and `CompiledClip`, plus preallocated planar scratch storage sized to `EngineConfig::maximumBlockSize`.
   - Never mutated or resized once published to the handoff channel.

2. **`GraphHandoff` (`engine/include/dawg/engine/GraphHandoff.h`):**
   - Implements a wait-free, garbage-carrying pointer handoff between the single control-thread producer and the single audio-thread consumer.
   - `publish(std::unique_ptr<const CompiledGraph> graph) noexcept`:
     - First drains and destroys any graphs in the retired list on the control thread.
     - Atomically exchanges `m_pending` with the newly compiled graph; if an unconsumed pending graph was overwritten, destroys it immediately on the control thread.
   - `adopt() noexcept` (called only on the audio thread at block start):
     - Atomically takes `m_pending` (`exchange(nullptr, acq_rel)`).
     - If non-null, pushes the previously active `CompiledGraph*` into a lock-free retired stack/slot so the control thread will deallocate it on its next `reclaimRetired()` or `publish()` call.
   - Destructor on the control thread cleans up active, pending, and retired graphs after the audio device is stopped.

3. **`GraphCompiler` (`services/commands/include/dawg/commands/GraphCompiler.h`):**
   - Compiles `project::ProjectSnapshot` + active media ring bindings + `EngineConfig` into a `std::unique_ptr<const engine::CompiledGraph>`.

---

### Pillar 2: Multi-Track Timeline Scheduler, Mixing, Channel Strips, Metering & Sub-Block Splitting

1. **Multi-Track Clip Scheduling & Boundary Ramps:**
   - Each `CompiledTrack` references a contiguous slice `[clipOffset, clipOffset + clipCount)` in `CompiledGraph::clips`.
   - Each `CompiledClip` carries `gainLinear`, `fadeInSamples`, and `fadeOutSamples`.
   - Samples inside `[0, fadeInSamples)` and `[clipLengthSamples - fadeOutSamples, clipLengthSamples)` apply click-free linear/equal-power gain ramps on the audio thread without branching allocations.
   - Multiple overlapping clips on the same track sum deterministically into the track's scratch buffer.

2. **Channel Strip & Bus Summing (`ChannelStrip.h`):**
   - Solo-in-place resolution: when any track has `solo == true`, only tracks with `solo && !muted` render to the master mix; otherwise all tracks with `!muted` render.
   - Equal-power stereo pan law: for pan $p \in [-1, +1]$, angle $\theta = \frac{\pi}{4}(p + 1)$, left gain $\cos\theta$, right gain $\sin\theta$.
   - Phase inversion (`phaseInvert ? -1.0f : 1.0f`).
   - Sample-accurate parameter smoothing (`SmoothedParameter`) so track gain and pan transitions ramp smoothly without zipper noise.

3. **Lock-Free Metering (`MeterTelemetry.h`):**
   - Audio thread computes stereo master peak and RMS values per block and stores them with `std::memory_order_relaxed` / `release` into `MeterSnapshot` atomics readable by the UI thread.

4. **Sample-Accurate Sub-Block Event Splitter (`SubBlockSplitter.h`):**
   - Partitions a callback block `[0, frameCount)` at exact sample boundaries (loop wrap points, sample-accurate parameter/MIDI events) into contiguous sub-slices `[sliceOffset, sliceFrames)` with zero allocations.

---

### Pillar 3: Musical Timebase, Tempo Map, Sample-Accurate Loop Engine & Metronome

1. **Dual-Domain Musical Timebase (`TempoMap.h`):**
   - `kTicksPerQuarterNote = 960`.
   - Supports tempo map segments (`TempoEvent { std::uint64_t tick, double bpm }`) and time signature events (`TimeSignatureEvent { std::uint64_t tick, std::uint16_t numerator, std::uint16_t denominator }`).
   - Exact bidirectional conversion: `sampleToTick(sample, sampleRate)`, `tickToSample(tick, sampleRate)`, and `sampleToBarBeatTick(sample, sampleRate)`.

2. **Sample-Accurate Loop Engine (`LoopRegion`):**
   - `LoopRegion { bool enabled, std::uint64_t startSample, std::uint64_t endSample }`.
   - Enqueued via `EngineCommand` or published in `CompiledGraph`.
   - When `playing && loop.enabled && loop.startSample < loop.endSample`, `AudioEngine::process` uses `SubBlockSplitter` to split any block that crosses `loop.endSample`, renders the pre-wrap sub-block, wraps `samplePosition` to `loop.startSample`, updates ring buffer read requests to the loop start source frame, and renders the post-wrap sub-block in the same callback invocation.

3. **Synthesized Metronome (`Metronome.h`):**
   - Sample-accurate phase-accumulated sine click with exponential decay envelope (downbeat accent 1760 Hz, beat 880 Hz) triggered at exact beat sample boundaries computed from `TempoMap`.

---

### Pillar 4: Scalable Disk I/O Pool (Multi-Voice Streamer, Recording Pipeline, & Resampling)

1. **Streaming Sample-Rate Converter (`AudioResampler.h`):**
   - 4-point Catmull-Rom cubic Hermite streaming interpolator with fractional phase continuity across chunk boundaries, converting source WAV sample rates (e.g., 44.1 kHz, 96 kHz) to the engine sample rate inside the media worker thread before writing into `PlaybackRingBuffer`.

2. **Multiplexed Disk Streamer Pool (`DiskStreamPool.h`):**
   - Coordinates a fixed pool of background worker threads across $K$ registered voice streams (`VoiceStreamHandle`), scheduling refills in priority order of lowest `availableFrames()` and supporting loop-region lookahead wrapping.

3. **Lock-Free Input Capture & Recording Pipeline (`RecordRingBuffer.h`, `AudioRecordService.h`):**
   - `RecordRingBuffer`: SPSC planar float ring buffer preallocated off the audio thread; `AudioEngine::process(input, output, midi, transport)` pushes captured input frames without locking or allocating.
   - `AudioRecordService`: background writer thread that drains `RecordRingBuffer` into an `AudioAssetWriter` seam (including a RIFF/WAVE 32-bit IEEE float file writer) and produces a finalized `project::AudioAsset` and latency-compensated `project::AudioClip`.

---

### Pillar 5: DAG Processor Node Graph, Automatic PDC, Built-In DSP, & Multi-Core Work-Stealing

1. **`ProcessorNode` & Air-Gapped Plugin Host Seam (`ProcessorNode.h`):**
   - Uniform realtime interface:
     - `prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channels) noexcept`
     - `reset() noexcept`
     - `latencySamples() const noexcept`
     - `process(ProcessContext& context) noexcept`
   - Built-in nodes:
     - `GainPanNode`
     - `BiquadFilterNode` (peaking / shelf / lowpass / highpass)
     - `DelayCompensatorNode` (preallocated circular buffer for Plugin Delay Compensation)
     - `ExternalPluginNode` (wraps a C-style or value-typed `ExternalPluginCallbacks` table so VST3/CLAP/LV2 hosts in `platform/` plug into the DAG without leaking SDK headers into `dawg_engine_core`).

2. **DAG Topological Compiler & Automatic PDC (`DspGraph.h`):**
   - Validates arbitrary routing graphs for cycles (Kahn's algorithm).
   - Computes per-node arrival latency and automatically inserts preallocated `DelayCompensatorNode`s on lower-latency parallel branches at every summing junction so all signals sum phase- and sample-aligned.
   - Partitions nodes into topological wavefronts (`ExecutionWavefront`) for parallel execution.

3. **Multi-Core Work-Stealing DSP Scheduler (`WorkStealingScheduler.h`):**
   - Executes independent nodes within each topological wavefront across preallocated worker threads using lock-free atomic task counters (`std::atomic<std::uint32_t>`), falling back to inline single-thread execution on 1-node wavefronts or when worker count is zero.
