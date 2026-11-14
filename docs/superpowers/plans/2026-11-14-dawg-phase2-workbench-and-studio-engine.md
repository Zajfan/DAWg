# DAWg Phase 2 — Workbench UI, Live Recording, Undo/Redo & Waveform Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the complete DAW studio suite: transactional Undo/Redo history, non-destructive timeline clip commands, multithreaded `WaveformCache`, live recording workflow with auto take placement, and the Configurable Workbench UI (Transport bar with BPM/time-sig/metronome/loop/record, Console Mixer with 60 FPS Peak/RMS meters, and Multi-Track Timeline with waveforms and playhead).

**Architecture:** 4 Sequential Vertical Slices executed with strict TDD (RED -> GREEN -> REFACTOR):
1. **Task 1: Transactional Undo/Redo & Timeline Editing Commands** (`dawg_project_model`, `dawg_services_commands`).
2. **Task 2: Multithreaded Waveform Decimation Cache (`WaveformCache`)** (`dawg_services_media`).
3. **Task 3: Live Recording Workflow & Automatic Take Placement** (`dawg_project_model`, `dawg_services_media`, `apps/dawg`).
4. **Task 4: Configurable Workbench UI (Transport Bar, Mixer Panel, & Multi-Track Timeline View)** (`ui/shell`, `apps/dawg`).

---

### Task 1: Transactional Undo/Redo & Timeline Editing Commands

**Files:**
- Modify: `project/include/dawg/project/ProjectTypes.h`
- Modify: `project/include/dawg/project/ProjectCommand.h`
- Modify: `project/include/dawg/project/Project.h`
- Modify: `project/src/Project.cpp`
- Modify: `project/src/ProjectCommand.cpp`
- Modify: `services/commands/include/dawg/commands/ProjectController.h`
- Modify: `services/commands/src/ProjectController.cpp`
- Modify: `tests/project/ProjectTests.cpp`
- Modify: `tests/services/ProjectControllerTests.cpp`

- [ ] **Step 1:** Write failing unit tests in `ProjectTests.cpp` and `ProjectControllerTests.cpp` testing `MoveClip`, `TrimClip`, `SplitClip`, `DuplicateClip`, `RemoveClip`, `SetClipGainFade`, `SetTrackRecordArm`, `ReorderTracks`, and full `undo()` / `redo()` journal equivalence.
- [ ] **Step 2:** Implement the commands in `Project` and the transactional undo/redo journal in `ProjectController`.
- [ ] **Step 3:** Run `dawg.project` and `dawg.project_controller` tests, verify 100% pass, and commit.

---

### Task 2: Multithreaded Waveform Decimation Cache (`WaveformCache`)

**Files:**
- Create: `services/media/include/dawg/media/WaveformCache.h`
- Create: `services/media/src/WaveformCache.cpp`
- Modify: `services/media/CMakeLists.txt`
- Modify: `tests/services/AudioMediaServiceTests.cpp`

- [ ] **Step 1:** Write failing unit tests in `AudioMediaServiceTests.cpp` verifying background generation of multi-LOD min/max peak pyramids and non-blocking visual querying.
- [ ] **Step 2:** Implement `WaveformCache` with background thread generation, multithreaded query synchronization, and fast min/max peak computation.
- [ ] **Step 3:** Run `dawg.media_service` tests, verify 100% pass, and commit.

---

### Task 3: Live Recording Workflow & Automatic Take Placement

**Files:**
- Modify: `apps/dawg/ApplicationState.h`
- Modify: `apps/dawg/Application.h`
- Modify: `apps/dawg/Application.cpp`
- Modify: `tests/app/ApplicationStateTests.cpp`
- Modify: `tests/integration/FoundationVerticalSliceTests.cpp`

- [ ] **Step 1:** Write failing tests in `ApplicationStateTests.cpp` and `FoundationVerticalSliceTests.cpp` testing armed recording transport toggle, take finalization, and immediate multi-track timeline playback.
- [ ] **Step 2:** Implement `ApplicationState` record arming and `Application` punch-in / punch-out take placement.
- [ ] **Step 3:** Run headless test suite, verify 100% pass, and commit.

---

### Task 4: Configurable Workbench UI (Transport Bar, Mixer Panel, & Multi-Track Timeline)

**Files:**
- Create: `ui/shell/TransportBar.h`
- Create: `ui/shell/TransportBar.cpp`
- Create: `ui/shell/MixerPanel.h`
- Create: `ui/shell/MixerPanel.cpp`
- Create: `ui/shell/TimelineView.h`
- Create: `ui/shell/TimelineView.cpp`
- Modify: `ui/shell/MainEditorView.h`
- Modify: `ui/shell/MainEditorView.cpp`
- Modify: `ui/shell/MainComponent.h`
- Modify: `ui/shell/MainComponent.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/app/MainComponentOwnershipTests.cpp`

- [ ] **Step 1:** Implement modular UI components:
  - `TransportBar`: Play, Stop, ReturnToStart, Record button, BPM slider/editor, Time Signature display, Metronome toggle, Loop toggle, Undo/Redo buttons.
  - `MixerPanel`: Channel strips (fader, pan, mute, solo, phase, arm) + Master bus + 60 FPS Peak/RMS level meters.
  - `TimelineView`: Beat/measure ruler, track lanes, audio clip blocks with waveform rendering from `WaveformCache`, playhead cursor line, loop brackets.
- [ ] **Step 2:** Wire into `MainEditorView` and `MainComponent` with responsive layout and 60 FPS timer updates.
- [ ] **Step 3:** Run full verification across `headless-debug` (`ASan+UBSan`), `headless-tsan` (`TSan+UBSan`), and `x11-release` (`DAWg` and platform tests), update documentation, and commit.
