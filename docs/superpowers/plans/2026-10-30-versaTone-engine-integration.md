# VersaTone Engine Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the VersaTone audio engine into DAWg as its real-time audio processing core, replacing the silent stub, and establish the `dawg::engine::` / `dawg::app::` namespace hierarchy.

**Architecture:** VersaTone's `engine/` is added as a CMake subdirectory of DAWg. A new `dawg::app::audio::AudioAdapter` class implements JUCE's `AudioIODeviceCallback` and bridges JUCE's device callbacks into `dawg::engine::AudioEngine::processAudio()` via a pre-allocated `AudioBuffer`. The VersaTone engine namespace is first refactored from `dawg::core` → `dawg::engine` and `VoiceManager` → `VoiceScheduler` so the public API is stable before any integration code is written.

**Tech Stack:** C++26, CMake 3.25+, JUCE 9.0.4 (FetchContent), VersaTone engine (static lib)

**Spec:** `docs/superpowers/specs/2026-10-30-versaTone-engine-integration-design.md`

## Global Constraints

- C++ standard: `cxx_std_26` — no C++23 or earlier features introduced
- Zero warnings policy: `-Wall -Wextra -Wpedantic -Wconversion -Werror`
- Debug sanitizers: `-fsanitize=address,undefined,thread`
- No heap allocation or mutex locking inside `audioDeviceIOCallbackWithContext`
- Engine tier (`dawg::engine::`) must have zero JUCE/Qt includes
- All buffer parameters use `std::span<float>` not raw `float* + int` pairs
- All fallible functions marked `[[nodiscard]]`
- Namespace style: `namespace dawg::engine {` (C++17 nested form, never old nested `namespace dawg { namespace core {`)
- VersaTone engine path relative to DAWg root: `../VersaTone/engine`

---

## File Map

### VersaTone (`apps/VersaTone/engine/`)
| Operation | Path |
|-----------|------|
| Rename dir | `include/dawg/core/` → `include/dawg/engine/` |
| Rename file | `include/dawg/engine/VoiceManager.h` → `include/dawg/engine/VoiceScheduler.h` |
| Rename file | `src/core/VoiceManager.cpp` → `src/core/VoiceScheduler.cpp` |
| Modify | `include/dawg/dawg.h` — update includes and namespace |
| Modify (all) | All `.h` and `.cpp` under `engine/` — namespace + name replacements |

### DAWg (`apps/DAWg/`)
| Operation | Path |
|-----------|------|
| Modify | `CMakeLists.txt` |
| Delete | `Source/Audio/AudioEngine.h` |
| Delete | `Source/Audio/AudioEngine.cpp` |
| Create | `Source/Audio/AudioAdapter.h` |
| Create | `Source/Audio/AudioAdapter.cpp` |
| Modify | `Source/App/MainComponent.h` |
| Modify | `Source/App/MainComponent.cpp` |

---

## Task 1: VersaTone Namespace & Name Refactor

**Files:**
- Rename: `apps/VersaTone/engine/include/dawg/core/` → `include/dawg/engine/`
- Rename: `VoiceManager.h` → `VoiceScheduler.h`, `VoiceManager.cpp` → `VoiceScheduler.cpp`
- Modify (script): all `.h` and `.cpp` under `apps/VersaTone/engine/`
- Modify: `apps/VersaTone/engine/include/dawg/dawg.h`
- Modify: `apps/VersaTone/engine/CMakeLists.txt`

**Interfaces:**
- Produces: `dawg::engine::AudioEngine`, `dawg::engine::VoiceScheduler`, `dawg::engine::Transport`, `dawg::engine::Timeline`, `dawg::engine::AudioBuffer` — all in `include/dawg/engine/` with `#include <dawg/engine/FileName.h>`

- [ ] **Step 1: Rename the `core` include directory to `engine`**

```bash
cd apps/VersaTone/engine
git mv include/dawg/core include/dawg/engine
```

- [ ] **Step 2: Rename VoiceManager files**

```bash
cd apps/VersaTone/engine
git mv include/dawg/engine/VoiceManager.h include/dawg/engine/VoiceScheduler.h
git mv src/core/VoiceManager.cpp src/core/VoiceScheduler.cpp
```

- [ ] **Step 3: Run the namespace/name refactor script**

Save the following as `scripts/refactor_namespaces.py` in `apps/VersaTone/engine/` and run it:

```python
#!/usr/bin/env python3
"""Refactor VersaTone engine: dawg::core -> dawg::engine, VoiceManager -> VoiceScheduler."""
import os, re
from pathlib import Path

ENGINE_ROOT = Path(__file__).parent.parent

REPLACEMENTS = [
    # Namespace declarations (old nested -> C++17 nested)
    (r'namespace dawg\s*\{\s*\n\s*namespace core\s*\{', 'namespace dawg::engine {'),
    # Closing namespace comments (two-line -> one-line)
    (r'\} // namespace core\s*\n\} // namespace dawg', '} // namespace dawg::engine'),
    # Single-level dawg namespace in core files (dawg.cpp etc.)
    # — only where file is under engine/src/core/ or references core types
    # Include path rewrites
    (r'#include\s*[<"]dawg/core/', '#include <dawg/engine/'),
    # Qualified name rewrites
    (r'\bdawg::core::', 'dawg::engine::'),
    # Class name rename
    (r'\bVoiceManager\b', 'VoiceScheduler'),
]

EXTENSIONS = {'.h', '.hpp', '.cpp', '.cc'}

def refactor_file(path: Path) -> bool:
    original = path.read_text(encoding='utf-8')
    text = original
    for pattern, replacement in REPLACEMENTS:
        text = re.sub(pattern, replacement, text)
    if text != original:
        path.write_text(text, encoding='utf-8')
        return True
    return False

changed = []
for ext in EXTENSIONS:
    for p in ENGINE_ROOT.rglob(f'*{ext}'):
        if 'build' in p.parts:
            continue
        if refactor_file(p):
            changed.append(p)

print(f"Modified {len(changed)} files:")
for p in sorted(changed):
    print(f"  {p.relative_to(ENGINE_ROOT)}")
```

```bash
cd apps/VersaTone/engine
python3 scripts/refactor_namespaces.py
```

Expected output: approximately 80–100 files modified.

- [ ] **Step 4: Update `dawg.h` master header includes manually**

The master header `include/dawg/dawg.h` includes core headers. After the script, verify the paths are correct:

```bash
grep -n "core\|VoiceManager" apps/VersaTone/engine/include/dawg/dawg.h
```

Expected: zero matches. If any remain, edit them:
```cpp
// Before (if any remain after script):
#include "dawg/core/AudioEngine.h"
#include "dawg/core/VoiceManager.h"

// After:
#include "dawg/engine/AudioEngine.h"
#include "dawg/engine/VoiceScheduler.h"
```

- [ ] **Step 5: Update CMakeLists.txt source list**

In `apps/VersaTone/engine/CMakeLists.txt`, `VoiceManager.cpp` was renamed to `VoiceScheduler.cpp`. Update the source list:

```cmake
# Before:
    src/core/VoiceManager.cpp

# After:
    src/core/VoiceScheduler.cpp
```

- [ ] **Step 6: Verify no old names remain**

```bash
cd apps/VersaTone/engine
grep -r "dawg::core" include/ src/ --include="*.h" --include="*.cpp"
grep -r "VoiceManager" include/ src/ --include="*.h" --include="*.cpp"
grep -r "namespace core" include/ src/ --include="*.h" --include="*.cpp"
```

Expected: zero matches in all three commands.

- [ ] **Step 7: Build VersaTone engine in isolation to verify zero errors**

```bash
cd apps/VersaTone
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc) 2>&1 | grep -E "error:|warning:" | head -30
```

Expected: zero errors, zero warnings. Fix any that appear before continuing.

- [ ] **Step 8: Commit the VersaTone refactor**

```bash
cd apps/VersaTone
git add -u
git add engine/include/dawg/engine/
git add engine/src/core/VoiceScheduler.cpp
git add engine/scripts/refactor_namespaces.py
git status  # verify nothing unexpected is staged
git commit -m "refactor(engine): rename dawg::core -> dawg::engine, VoiceManager -> VoiceScheduler

- Rename include/dawg/core/ -> include/dawg/engine/
- VoiceManager.h/.cpp -> VoiceScheduler.h/.cpp
- All namespace declarations use C++17 nested syntax
- All qualified names and include paths updated

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 2: DAWg CMake Build Integration

**Files:**
- Modify: `apps/DAWg/CMakeLists.txt`

**Interfaces:**
- Consumes: `dawg_engine` CMake target from `apps/VersaTone/engine/CMakeLists.txt`
- Produces: DAWg binary that links `dawg_engine`; headers `dawg/engine/*.h` available to all DAWg source files

- [ ] **Step 1: Add VersaTone engine as a subdirectory**

Open `apps/DAWg/CMakeLists.txt`. Replace:

```cmake
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)
```

with:

```cmake
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

# VersaTone engine — monorepo sibling, GUI-independent audio core
set(DAWG_ENGINE_ENABLE_SIMD ON  CACHE BOOL "" FORCE)
set(DAWG_ENGINE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(DAWG_ENGINE_ENABLE_LTO ON  CACHE BOOL "" FORCE)
else()
    set(DAWG_ENGINE_ENABLE_LTO OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(../VersaTone/engine dawg_engine_build)
```

- [ ] **Step 2: Link the engine and add compiler flags**

Replace the existing `target_link_libraries` block:

```cmake
target_link_libraries(DAWg
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
```

with:

```cmake
target_link_libraries(DAWg
    PRIVATE
        dawg_engine
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Zero-warning policy (Zajfan Standard §8)
target_compile_options(DAWg PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic -Wconversion -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# Sanitizers for debug builds (Zajfan Standard §10.2 + §6.3)
target_compile_options(DAWg PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:
        -fsanitize=address,undefined,thread>
)
target_link_options(DAWg PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:GNU,Clang,AppleClang>,$<CONFIG:Debug>>:
        -fsanitize=address,undefined,thread>
)
```

- [ ] **Step 3: Verify CMake configure succeeds**

```bash
cd apps/DAWg
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

Expected: `-- Configuring done` with no errors. If `FetchContent` for JUCE takes long, it is normal on first run.

- [ ] **Step 4: Verify the engine headers are accessible**

```bash
cd apps/DAWg
echo '#include <dawg/engine/AudioEngine.h>\nint main(){}' > /tmp/smoke.cpp
g++ -std=c++26 -I build/dawg_engine_build/include /tmp/smoke.cpp -c -o /tmp/smoke.o 2>&1 || \
    echo "header not found — check add_subdirectory path"
```

Expected: compiles without errors.

- [ ] **Step 5: Commit CMakeLists changes**

```bash
cd apps/DAWg
git add CMakeLists.txt
git commit -m "build: integrate VersaTone engine via add_subdirectory

Links dawg_engine static lib; adds zero-warning and sanitizer flags
per Zajfan Standard §6.3, §8, §10.2.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Task 3: AudioAdapter Bridge Class

**Files:**
- Delete: `Source/Audio/AudioEngine.h`
- Delete: `Source/Audio/AudioEngine.cpp`
- Create: `Source/Audio/AudioAdapter.h`
- Create: `Source/Audio/AudioAdapter.cpp`
- Modify: `Source/App/MainComponent.h`
- Modify: `Source/App/MainComponent.cpp`
- Modify: `CMakeLists.txt` (update `target_sources`)

**Interfaces:**
- Consumes: `dawg::engine::AudioEngine` (`dawg/engine/AudioEngine.h`), `dawg::engine::Transport`, `dawg::engine::AudioBuffer`
- Produces: `dawg::app::audio::AudioAdapter` with methods `initialise()`, `shutdown()`, `isDeviceReady()`, `getSampleRate()`, `getBlockSize()`, `getTransport()`, `getVoiceScheduler()`

- [ ] **Step 1: Delete the old stub files**

```bash
cd apps/DAWg
rm Source/Audio/AudioEngine.h Source/Audio/AudioEngine.cpp
```

- [ ] **Step 2: Create `AudioAdapter.h`**

Create `Source/Audio/AudioAdapter.h`:

```cpp
#pragma once

#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/AudioBuffer.h>
#include <dawg/engine/Transport.h>
#include <dawg/engine/VoiceScheduler.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace dawg::app::audio {

class AudioAdapter final : private juce::AudioIODeviceCallback
{
public:
    AudioAdapter() = default;
    ~AudioAdapter() override;

    AudioAdapter(const AudioAdapter&)            = delete;
    AudioAdapter& operator=(const AudioAdapter&) = delete;

    [[nodiscard]] juce::Result initialise();
    void shutdown() noexcept;

    [[nodiscard]] bool   isDeviceReady() const noexcept;
    [[nodiscard]] double getSampleRate()  const noexcept;
    [[nodiscard]] int    getBlockSize()   const noexcept;

    // Engine sub-system access for transport commands (play/stop/seek/tempo)
    [[nodiscard]] dawg::engine::Transport&       getTransport()       noexcept;
    [[nodiscard]] dawg::engine::VoiceScheduler&  getVoiceScheduler()  noexcept;

private:
    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const*                        inputChannelData,
        int                                        numInputChannels,
        float* const*                              outputChannelData,
        int                                        numOutputChannels,
        int                                        numSamples,
        const juce::AudioIODeviceCallbackContext&  context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager m_deviceManager;
    dawg::engine::AudioEngine m_engine;

    // Pre-allocated at device start; never touched inside the callback
    std::unique_ptr<dawg::engine::AudioBuffer> m_processingBuffer;

    std::atomic<bool>   m_deviceReady { false };
    std::atomic<double> m_sampleRate  { 0.0 };
    std::atomic<int>    m_blockSize   { 0 };
};

} // namespace dawg::app::audio
```

- [ ] **Step 3: Create `AudioAdapter.cpp`**

Create `Source/Audio/AudioAdapter.cpp`:

```cpp
#include "AudioAdapter.h"

#include <cstring>

namespace dawg::app::audio {

AudioAdapter::~AudioAdapter()
{
    shutdown();
}

juce::Result AudioAdapter::initialise()
{
    shutdown();

    const auto error = m_deviceManager.initialiseWithDefaultDevices(0, 2);

    if (error.isNotEmpty())
        return juce::Result::fail(error);

    m_deviceManager.addAudioDeviceCallback(this);
    return juce::Result::ok();
}

void AudioAdapter::shutdown() noexcept
{
    m_deviceManager.removeAudioDeviceCallback(this);
    m_engine.stop();
    m_engine.shutdown();
    m_deviceManager.closeAudioDevice();

    m_processingBuffer.reset();
    m_deviceReady.store(false, std::memory_order_release);
    m_sampleRate.store(0.0,   std::memory_order_relaxed);
    m_blockSize.store(0,      std::memory_order_relaxed);
}

bool AudioAdapter::isDeviceReady() const noexcept
{
    return m_deviceReady.load(std::memory_order_acquire);
}

double AudioAdapter::getSampleRate() const noexcept
{
    return m_sampleRate.load(std::memory_order_relaxed);
}

int AudioAdapter::getBlockSize() const noexcept
{
    return m_blockSize.load(std::memory_order_relaxed);
}

dawg::engine::Transport& AudioAdapter::getTransport() noexcept
{
    return m_engine.getTransport();
}

dawg::engine::VoiceScheduler& AudioAdapter::getVoiceScheduler() noexcept
{
    return m_engine.getVoiceManager();  // update to getVoiceScheduler() once VersaTone getter is renamed
}

// --- Device callbacks --------------------------------------------------------

void AudioAdapter::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const auto sr        = device->getCurrentSampleRate();
    const auto blockSize = static_cast<uint32_t>(device->getCurrentBufferSizeSamples());
    const auto channels  = static_cast<uint16_t>(2);

    dawg::engine::EngineConfig config;
    config.sampleRate  = static_cast<uint32_t>(sr);
    config.bufferSize  = blockSize;
    config.channels    = channels;

    m_engine.initialize(config);
    m_processingBuffer = std::make_unique<dawg::engine::AudioBuffer>(channels, blockSize);
    m_engine.start();

    m_sampleRate.store(sr,                        std::memory_order_relaxed);
    m_blockSize.store(static_cast<int>(blockSize), std::memory_order_relaxed);
    m_deviceReady.store(true,                     std::memory_order_release);
}

void AudioAdapter::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int                 /*numInputChannels*/,
    float* const*       outputChannelData,
    int                 numOutputChannels,
    int                 numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Real-time safe: no alloc, no mutex, no I/O.
    if (m_processingBuffer == nullptr)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                std::memset(outputChannelData[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    m_processingBuffer->clear();
    m_engine.processAudio(*m_processingBuffer, nullptr);

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;
        const float* src = m_processingBuffer->getChannelData(static_cast<uint16_t>(ch));
        if (src != nullptr)
            std::memcpy(outputChannelData[ch], src, sizeof(float) * static_cast<size_t>(numSamples));
        else
            std::memset(outputChannelData[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
    }
}

void AudioAdapter::audioDeviceStopped()
{
    m_engine.stop();
    m_deviceReady.store(false, std::memory_order_release);
}

} // namespace dawg::app::audio
```

- [ ] **Step 4: Update `CMakeLists.txt` source list**

In `apps/DAWg/CMakeLists.txt`, replace:

```cmake
target_sources(DAWg
    PRIVATE
        Source/App/Main.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponent.h
        Source/Audio/AudioEngine.cpp
        Source/Audio/AudioEngine.h
)
```

with:

```cmake
target_sources(DAWg
    PRIVATE
        Source/App/Main.cpp
        Source/App/MainComponent.cpp
        Source/App/MainComponent.h
        Source/Audio/AudioAdapter.cpp
        Source/Audio/AudioAdapter.h
)
```

- [ ] **Step 5: Update `MainComponent.h`**

Replace the full content of `Source/App/MainComponent.h`:

```cpp
#pragma once

#include "../Audio/AudioAdapter.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace dawg::app {

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAudioStatus();

    dawg::app::audio::AudioAdapter m_audioAdapter;
    juce::Label m_titleLabel;
    juce::Label m_statusLabel;
    juce::TextButton m_restartButton { "Restart audio device" };
};

} // namespace dawg::app
```

- [ ] **Step 6: Update `MainComponent.cpp`**

Replace the full content of `Source/App/MainComponent.cpp`:

```cpp
#include "MainComponent.h"

namespace dawg::app {

MainComponent::MainComponent()
{
    setSize(900, 560);

    m_titleLabel.setText("DAWg", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    addAndMakeVisible(m_titleLabel);

    m_statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_statusLabel);

    m_restartButton.onClick = [this]
    {
        const auto result = m_audioAdapter.initialise();

        if (result.failed())
            m_statusLabel.setText("Audio device error: " + result.getErrorMessage(),
                                  juce::dontSendNotification);
        else
            refreshAudioStatus();
    };
    addAndMakeVisible(m_restartButton);

    const auto result = m_audioAdapter.initialise();

    if (result.failed())
        m_statusLabel.setText("Audio device error: " + result.getErrorMessage(),
                              juce::dontSendNotification);
    else
        refreshAudioStatus();

    startTimerHz(2);
}

MainComponent::~MainComponent()
{
    stopTimer();
    m_audioAdapter.shutdown();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(25, 28, 34));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(28);

    m_titleLabel.setBounds(area.removeFromTop(52));
    area.removeFromTop(20);
    m_statusLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(12);
    m_restartButton.setBounds(area.removeFromTop(36).removeFromLeft(200));
}

void MainComponent::timerCallback()
{
    refreshAudioStatus();
}

void MainComponent::refreshAudioStatus()
{
    if (!m_audioAdapter.isDeviceReady())
    {
        m_statusLabel.setText("No audio device is ready. Check the device settings and retry.",
                              juce::dontSendNotification);
        return;
    }

    const auto rate  = juce::String(m_audioAdapter.getSampleRate(), 0);
    const auto block = juce::String(m_audioAdapter.getBlockSize());
    m_statusLabel.setText("Audio device ready  |  " + rate + " Hz  |  block " + block + " samples",
                          juce::dontSendNotification);
}

} // namespace dawg::app
```

- [ ] **Step 7: Check `Main.cpp` for the old namespace reference**

```bash
grep -n "daw::" apps/DAWg/Source/App/Main.cpp
```

If `daw::app::MainComponent` appears, update it to `dawg::app::MainComponent`.

- [ ] **Step 8: Build DAWg and verify zero errors/warnings**

```bash
cd apps/DAWg
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc) 2>&1 | grep -E "error:|warning:" | head -40
```

Expected: zero errors, zero warnings. Common issues and fixes:

| Error | Fix |
|-------|-----|
| `getVoiceManager` not found on `AudioEngine` | Check if VersaTone getter was renamed; use the current getter name |
| `EngineConfig` not found | Add `#include <dawg/engine/AudioEngine.h>` to AudioAdapter.cpp |
| `initialize` not declared | Verify VersaTone still uses `initialize` (not `init`) in AudioEngine.h |
| Sanitizer link errors | Ensure `-fsanitize=...` appears in both compile and link flags |

- [ ] **Step 9: Smoke test — launch the app**

```bash
cd apps/DAWg/build
./DAWg
```

Expected behaviour:
- Window opens (900 × 560, dark background)
- Status label shows: `Audio device ready  |  44100 Hz  |  block 512 samples` (exact values depend on system)
- No audible glitch or system audio dropout
- No crash on close

- [ ] **Step 10: Commit the AudioAdapter and MainComponent changes**

```bash
cd apps/DAWg
git add Source/Audio/AudioAdapter.h Source/Audio/AudioAdapter.cpp
git add Source/App/MainComponent.h Source/App/MainComponent.cpp
git add CMakeLists.txt
git status  # confirm AudioEngine.h/.cpp are deleted (shown as 'deleted:')
git commit -m "feat(audio): replace stub AudioEngine with dawg::app::audio::AudioAdapter

AudioAdapter bridges JUCE AudioIODeviceCallback to dawg::engine::AudioEngine.
Pre-allocated AudioBuffer; zero allocation/mutex in callback hot path.
MainComponent updated to dawg::app namespace with m_ member prefix.

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>"
```

---

## Self-Review Checklist

- [x] **Spec §3 (namespace):** Task 1 renames `dawg::core` → `dawg::engine`, `VoiceManager` → `VoiceScheduler`. ✅
- [x] **Spec §4 (CMake):** Task 2 adds `add_subdirectory`, links `dawg_engine`, adds sanitizer/warning flags. ✅
- [x] **Spec §5 (VersaTone refactor):** 86 .cpp and 84 headers covered by script in Task 1. ✅
- [x] **Spec §6 (AudioAdapter):** Task 3 creates `AudioAdapter.h/.cpp` with pre-allocated buffer, real-time safe callback. ✅
- [x] **Spec §6.3 (real-time contract):** Callback has no alloc, no mutex, no I/O. Null-buffer guard added. ✅
- [x] **Spec §6.4 (future optimization note):** Acknowledged in spec; not implemented here. ✅
- [x] **Spec §7 (file changes):** All listed files covered across Tasks 1–3. ✅
- [x] **Spec §8 (verification):** `cmake --build` + smoke test steps in Task 3 Steps 8–9. ✅
- [x] **Global constraints:** `[[nodiscard]]` on all falliable methods, `m_` prefix on members, C++17 namespace syntax, no JUCE includes in engine headers. ✅
- [x] **Type consistency:** `dawg::engine::AudioBuffer`, `dawg::engine::EngineConfig`, `dawg::engine::Transport`, `dawg::engine::VoiceScheduler` used consistently across all tasks. ✅

---

*Plan complete. Three tasks, sequential (each depends on the previous).*
