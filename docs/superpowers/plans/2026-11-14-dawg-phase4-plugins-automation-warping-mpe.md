# DAWg Phase 4 Implementation Plan: Plugins, Automation Curves, Elastic Warping & MPE Controller Mapping

## Objectives
Execute the 4 pillars of Phase 4 to advance DAWg toward market leadership:
- **Task 1:** CLAP Plugin Hosting Architecture with Out-of-Process Crash Immunity
- **Task 2:** Sample-Accurate Timeline Automation Engine with Bézier Curves
- **Task 3:** Elastic Time-Stretching (WSOLA), Transient Detection & Pitch Shifting
- **Task 4:** MIDI Polyphonic Expression (MPE) & Hardware Controller Mapping (MCU / MIDI Learn)

---

## Task 1: CLAP Plugin Hosting Architecture with Crash Immunity

### 1.1 CLAP Standard Definitions & Interfaces
- Introduce minimal, standard CLAP C ABI definitions in `engine/include/dawg/engine/clap/clap.h`.
- Implement `ClapPluginNode` adhering to `dawg::engine::ProcessorNode`:
  - `prepare()`, `reset()`, `latencySamples()`, `process(ProcessContext& context)`.
  - Event translation between `MidiBufferView` and `clap_input_events`.
  - Planar float channel buffers mapped to `clap_audio_buffer`.

### 1.2 Out-of-Process Crash Sandboxing
- Implement `ClapIsolatedProcess`:
  - Shared memory ring buffer (`shm_open`, `mmap`) for planar audio streams and parameter queues.
  - Subprocess runner with heartbeat watchdog.
  - On child termination / segfault: node outputs silence, marks state `Crashed`, avoids any audio engine freeze.

### 1.3 Verification
- Headless unit tests: mock CLAP plugin loading, latency PDC declaration, parameter modulation, and crash recovery stress test.
- Sanitizer passes: ASan, UBSan, TSan.

---

## Task 2: Sample-Accurate Timeline Automation Engine with Bézier Curves

### 2.1 Automation Domain Model
- In `project/include/dawg/project/ProjectTypes.h`:
  - Define `AutomationPoint`, `AutomationTarget`, `AutomationLane`.
  - Add `automationLanes` to `ProjectSnapshot`.
- In `project/include/dawg/project/ProjectCommand.h`:
  - Add `AddAutomationLane`, `RemoveAutomationLane`, `AddAutomationPoint`, `RemoveAutomationPoint`, `MoveAutomationPoint`.

### 2.2 Automation Engine & Curve Evaluator
- Implement `AutomationCurveEvaluator` in `engine/include/dawg/engine/AutomationCurveEvaluator.h` & `cpp`:
  - Binary search for active segment.
  - S-curve / exponential tension formula.
  - Sub-block slope calculation for click-free smoothing.

### 2.3 Verification
- Unit tests: curve interpolation accuracy, tension slope behavior, undo/redo serialization.
- Sanitizer passes: ASan, UBSan, TSan.

---

## Task 3: Elastic Time-Stretching, Transient Detection & Pitch Shifting

### 3.1 Transient / Onset Detector
- Implement `TransientDetector` in `services/media/include/dawg/media/TransientDetector.h` & `cpp`:
  - High-frequency content / spectral flux energy metric.
  - Peak thresholding generating transient sample offsets.

### 3.2 WSOLA Elastic Time-Stretcher
- Implement `WsolaTimeStretcher` in `engine/include/dawg/engine/WsolaTimeStretcher.h` & `cpp`:
  - Correlation cross-fading for phase alignment without pitch shifts.
  - Variable stretch ratio from 0.25x to 4.0x.
  - Independent pitch transposition ($\pm 24$ semitones) via combined resampling + stretching.

### 3.3 Verification
- Unit tests: transient detection on synthetic drum bursts, stretch preservation without pitch drift, and pitch shifting verification.
- Sanitizer passes: ASan, UBSan, TSan.

---

## Task 4: MIDI Polyphonic Expression (MPE) & Hardware Controller Mapping

### 4.1 MPE Routing in `TnhcSynthNode`
- Extend `TnhcSynthNode` to support MPE:
  - Per-note pitch bend sensitivity (up to $\pm 48$ semitones).
  - Channel pressure / aftertouch routing to voice volume/cutoff.
  - CC74 timbre modulation per active voice.

### 4.2 Hardware Controller Mapping & MCU
- Implement `MidiControllerManager` in `engine/include/dawg/engine/MidiControllerManager.h` & `cpp`:
  - Mackie Control Universal protocol parser.
  - MIDI Learn binding table for physical faders, knobs, and transport buttons.

### 4.3 Verification
- Unit tests: MPE 3D expressivity tests, MCU fader feedback tests, MIDI Learn dispatch tests.
- Sanitizer passes: ASan, UBSan, TSan.
