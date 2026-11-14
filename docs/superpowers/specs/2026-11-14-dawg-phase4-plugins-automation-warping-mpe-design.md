# DAWg Phase 4 Design Specification: Plugins (CLAP/VST3), Automation Curves, Elastic Audio Warping & MPE Controller Mapping

## 1. Vision & Architecture

Phase 4 elevates DAWg from a self-contained multi-track studio into an extensible, industry-grade digital audio workstation capable of hosting external plugins with crash immunity, sample-accurate Bézier timeline automation, elastic time-stretching with transient detection, and MIDI Polyphonic Expression (MPE) with hardware controller integration.

In strict adherence to the **Zajfan Engineering Standard**:
- **Real-Time Audio Safety**: 100% lock-free, zero heap allocation, zero blocking syscalls, and zero exceptions in all realtime process paths.
- **Strict Separation of Concerns**: Clean JUCE-free domain models and DSP engines in `dawg::engine` and `dawg::project`.
- **Zero-Warning Policy**: All code compiled with `-Wall -Wextra -Wpedantic -Werror`.
- **Exhaustive Testing**: Strict TDD covering unit tests, ASan, UBSan, and TSan across headless and platform builds.

---

## 2. Pillar 1: CLAP & VST3 Plugin Hosting Architecture (with Crash Immunity)

### 2.1 The CLAP Plugin Standard
CLAP (CLever Audio Plugin) is an open-source, non-proprietary C ABI standard.
- Pure C types and structs (`clap_plugin`, `clap_host`, `clap_process`, `clap_event_list`).
- Native sample-accurate parameter modulation, voice-level modulation, and thread-safe extensions.
- Zero C++ compiler ABI incompatibility across dynamic libraries.

### 2.2 In-Process Plugin Hosting Adapter
- `ClapPluginNode` implements `engine::ProcessorNode`:
  - `prepare(sampleRate, maxBlockSize, channelCount)`: calls `plugin->activate(sampleRate, minFrames, maxFrames)`.
  - `reset()`: calls `plugin->reset()`.
  - `latencySamples()`: queries `clap_plugin_latency` extension for automatic Plugin Delay Compensation (PDC) integration in [`DspGraph`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/engine/include/dawg/engine/DspGraph.h).
  - `process(ProcessContext& context)`: maps `AudioBufferView` planar channels into `clap_audio_buffer` and `MidiBufferView` into `clap_input_events`.

### 2.3 Air-Gapped Crash-Proof Subprocess Isolation (The TNHC Way)
Third-party plugins are the #1 cause of DAW crashes. DAWg introduces an air-gapped process runner:
- Dynamic library loaded in a child worker process (`dawg-plugin-host-worker`).
- Audio and event interchange over POSIX Shared Memory (`shm_open`, `mmap`).
- Real-time synchronization via atomic state flags and Linux `futex`.
- Watchdog supervision: if a plugin segfaults (SIGSEGV/SIGBUS) or aborts, the audio engine continues processing uninterrupted with silence, flags the slot as `Crashed`, and notifies the UI with 1-click crash recovery.

---

## 3. Pillar 2: Sample-Accurate Timeline Automation Engine

### 3.1 Domain Model
- `AutomationPoint`: `{ uint64_t samplePosition, float value, float curveTension }`
  - `curveTension` in `[-1.0, 1.0]`:
    - `0.0`: Linear ramp
    - `> 0.0`: Convex exponential curve
    - `< 0.0`: Concave logarithmic curve
- `AutomationTarget`:
  - `enum class TargetType : uint8_t { trackVolume, trackPan, trackSend, dspParameter }`
  - `TrackId trackId`
  - `uint32_t slotIndex`
  - `uint32_t parameterIndex`
- `AutomationLane`: `{ uint64_t id, TrackId trackId, AutomationTarget target, std::vector<AutomationPoint> points }`

### 3.2 Real-Time Curve Evaluator
- `CompiledAutomationLane`: Flat cache-contiguous array of points.
- Continuous evaluation: binary search find active segment `[P_A, P_B]`.
- Normalized progression $t = \frac{s - s_A}{s_B - s_A} \in [0, 1]$.
- Curve shaping formula:
  $$t_{\text{shaped}} = \begin{cases} t & \text{if } |\text{tension}| < 10^{-4} \\ \frac{e^{\text{tension} \cdot t} - 1}{e^{\text{tension}} - 1} & \text{otherwise} \end{cases}$$
- Applied directly to track faders, pan coefficients, and DSP parameters on each process block.

---

## 4. Pillar 3: Elastic Time-Stretching, Transient Detection & Pitch Shifting

### 4.1 Transient & Onset Detection
- High-Frequency Content (HFC) and Spectral Difference onset detector.
- Detects percussive attack transients across short STFT windows (e.g. 512 samples, 50% overlap).
- Emits transient markers `{ uint64_t frame, float energy }`.

### 4.2 WSOLA (Waveform Similarity Overlap-Add) Time-Stretching
- Real-time, low-latency, phase-aligned time-stretching without frequency domain smearing or phasing.
- Stretch factors from $0.25\times$ (400% speed) to $4.0\times$ (25% speed) with zero pitch distortion.
- Allows audio loops to automatically lock to project tempo changes.

### 4.3 Pitch Transposition
- Independent semitone ($\pm 24$) and cents ($\pm 100$) pitch shifting via combined sinc resampling and WSOLA duration recovery.

---

## 5. Pillar 4: MIDI Polyphonic Expression (MPE) & Hardware Controller Mapping

### 5.1 MPE (MIDI Polyphonic Expression)
- Voice routing across MIDI Channels 2–16:
  - Per-note Pitch Bend (X-axis glide)
  - Channel Pressure / Polyphonic Aftertouch (Z-axis pressure)
  - CC74 Brightness / Timbre (Y-axis vertical slide)
- Fully routed into [`TnhcSynthNode`](file:///run/media/zajferx/Data1/dev/The-No-hands-Company/projects/DAWg/engine/include/dawg/engine/dsp/TnhcSynthNode.h) to modulate individual voice pitch, filter cutoff, and resonance dynamically.

### 5.2 Hardware Controller Mapping (MCU & MIDI Learn)
- Mackie Control Universal (MCU) protocol support:
  - Motorized fader feedback
  - V-Pot pan control
  - Transport buttons (Play, Stop, Rec, Rewind, Fast Forward)
- Real-time MIDI Learn table: binding physical MIDI CC messages to any `AutomationTarget` with lock-free atomic parameter smoothing.
