# VersaTone Engine Integration — Architecture Spec
**Date:** 2026-10-30  
**Author:** Eric Håkansson  
**Status:** Approved — ready for implementation

---

## 1. Goal

Wire the VersaTone audio engine (`apps/VersaTone/engine`) into the DAWg JUCE application shell as its real-time audio processing core, replacing the current silent stub. Establish the namespace and class naming conventions that all subsequent DAWg development will follow.

---

## 2. Scope

This spec covers everything needed for DAWg **v0.2** (transport + single-track WAV playback), but the naming and layering it establishes extend cleanly through v0.5 (multitrack + mixing) without rearchitecting.

Out of scope: recording (v0.3), multitrack arrangement (v0.4), plugin hosting, MIDI.

---

## 3. Namespace & Naming Convention

### 3.1 Hierarchy

```
dawg::                       ← project root
dawg::engine::               ← VersaTone audio processing core (no UI/framework deps)
dawg::app::                  ← JUCE application layer
dawg::app::audio::           ← JUCE device adapter and bridge
```

The `engine` tier has **zero dependency** on JUCE, Qt, or any UI framework.  
The `app` tier owns framework lifecycle.  
Cross-tier communication flows **inward only**: `app` calls `engine`, never vice-versa.

### 3.2 Renamed Identifiers

| Before | After | Reason |
|--------|-------|--------|
| `namespace dawg { namespace core {` | `namespace dawg::engine {` | C++17 nested namespace, correct tier |
| `dawg::core::VoiceManager` | `dawg::engine::VoiceScheduler` | Concrete mechanism: schedules/allocates voices |
| `dawg::io::DeviceManager` | `dawg::app::audio::AudioDeviceAdapter` | Concrete: adapts JUCE device API |
| `daw::audio::AudioEngine` (stub) | `dawg::app::audio::AudioAdapter` | This class adapts; it is not an engine |

`dawg::engine::AudioEngine`, `Transport`, `Timeline`, `AudioBuffer` — names unchanged. "Engine", "Transport", "Timeline" are domain-vocabulary terms per the Zajfan Standard §2.3 Domain Vocabulary Exception.

---

## 4. CMake Build Integration

VersaTone's engine is added as a CMake subdirectory. Both projects are siblings in the Nexus Systems monorepo so a relative path is stable.

### 4.1 Changes to `DAWg/CMakeLists.txt`

```cmake
# VersaTone engine — monorepo sibling
set(DAWG_ENGINE_ENABLE_LTO OFF)   # overridden per-config below
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(DAWG_ENGINE_ENABLE_LTO ON)
endif()

add_subdirectory(../VersaTone/engine dawg_engine_build)

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
```

`DAWG_ENGINE_ENABLE_SIMD` stays ON.  
`DAWG_ENGINE_BUILD_SHARED` stays OFF (static link into DAWg binary).

### 4.2 Compiler Flags

Add to `DAWg/CMakeLists.txt`:

```cmake
# Sanitizers for debug builds (Zajfan Standard §10.2)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(DAWg PRIVATE -fsanitize=address,undefined,thread)
    target_link_options(DAWg PRIVATE -fsanitize=address,undefined,thread)
endif()

# Zero-warning policy (Zajfan Standard §8)
target_compile_options(DAWg PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
```

---

## 5. VersaTone Namespace Refactor

All 14 public headers under `engine/include/` and their corresponding `.cpp` files in `engine/src/` are updated:

1. `namespace dawg { namespace core {` → `namespace dawg::engine {`  
2. `} // namespace core` / `} // namespace dawg` → `} // namespace dawg::engine`
3. `class VoiceManager` → `class VoiceScheduler` (all occurrences in header + .cpp)
4. `dawg.h` master header: update includes and namespace references

No logic changes — this is a pure rename. After the refactor, `apps/VersaTone/engine/` compiles identically with the new names.

---

## 6. `AudioAdapter` Bridge Class

Replaces `daw::audio::AudioEngine`. Lives in `Source/Audio/`.

### 6.1 Header — `AudioAdapter.h`

```cpp
#pragma once

#include <dawg/core/AudioEngine.h>   // will be dawg/engine/AudioEngine.h post-refactor
#include <dawg/core/AudioBuffer.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>

namespace dawg::app::audio {

class AudioAdapter final : private juce::AudioIODeviceCallback {
public:
    AudioAdapter() = default;
    ~AudioAdapter() override;

    AudioAdapter(const AudioAdapter&) = delete;
    AudioAdapter& operator=(const AudioAdapter&) = delete;

    [[nodiscard]] juce::Result initialise();
    void shutdown() noexcept;

    [[nodiscard]] bool isDeviceReady() const noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] int getBlockSize() const noexcept;

    // Engine access for transport commands (play, stop, seek, tempo)
    [[nodiscard]] dawg::engine::Transport& getTransport() noexcept;
    [[nodiscard]] dawg::engine::VoiceScheduler& getVoiceScheduler() noexcept;

private:
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager m_deviceManager;
    dawg::engine::AudioEngine m_engine;

    // Pre-allocated at device start; never allocated/freed in callback
    std::unique_ptr<dawg::engine::AudioBuffer> m_processingBuffer;

    std::atomic<bool>   m_deviceReady { false };
    std::atomic<double> m_sampleRate  { 0.0 };
    std::atomic<int>    m_blockSize   { 0 };
};

} // namespace dawg::app::audio
```

### 6.2 Implementation — callback flow

**`audioDeviceAboutToStart`** (called once off the audio thread before first callback):
```
1. Build EngineConfig from device.getCurrentSampleRate() + getCurrentBufferSizeSamples()
2. engine.initialize(config)
3. m_processingBuffer = make_unique<AudioBuffer>(numOutputChannels, bufferSize)
4. engine.start()
5. sampleRate.store(...), blockSize.store(...), deviceReady.store(true)
```

**`audioDeviceIOCallbackWithContext`** (real-time hot path — zero allocation):
```
1. m_processingBuffer->clear()
2. engine.processAudio(*m_processingBuffer, nullptr)
3. for (int ch = 0; ch < numOutputChannels; ++ch)
       memcpy(outputChannelData[ch],
              m_processingBuffer->getChannelData(ch),
              numSamples * sizeof(float))
```

**`audioDeviceStopped`**:
```
1. engine.stop()
2. deviceReady.store(false)
```

### 6.3 Real-Time Contract

This implementation satisfies every constraint from `ARCHITECTURE.md` and Zajfan Standard §6.4:
- No heap allocation in callback (`m_processingBuffer` pre-allocated)
- No mutex — `AudioEngine::processAudio` uses internal atomics
- No I/O
- Single `memcpy` per channel per block (deterministic, SIMD-friendly)
- `m_deviceReady`, `m_sampleRate`, `m_blockSize` published as atomics

### 6.4 Future Optimization (not in this spec)

Add `AudioBuffer::wrapExternalChannels(float** channels, int numChannels, int numSamples)` factory — non-owning view that lets `processAudio` write directly into JUCE's output buffers, eliminating the `memcpy`. Target for v0.3 or when profiling identifies this as a bottleneck.

---

## 7. File Changes Summary

### `apps/VersaTone/engine/`
| File | Change |
|------|--------|
| `include/dawg/core/*.h` (14 files) | Namespace → `dawg::engine`, `VoiceManager` → `VoiceScheduler` |
| `include/dawg/dawg.h` | Update namespace refs |
| `src/**/*.cpp` (86 files) | Namespace → `dawg::engine`, `VoiceManager` → `VoiceScheduler` |

### `apps/DAWg/`
| File | Change |
|------|--------|
| `CMakeLists.txt` | Add `add_subdirectory`, link `dawg_engine`, add sanitizer/warning flags |
| `Source/Audio/AudioEngine.h` | Delete (replaced) |
| `Source/Audio/AudioEngine.cpp` | Delete (replaced) |
| `Source/Audio/AudioAdapter.h` | New — `dawg::app::audio::AudioAdapter` |
| `Source/Audio/AudioAdapter.cpp` | New — bridge implementation |
| `Source/App/MainComponent.h/.cpp` | Update reference from `AudioEngine` to `AudioAdapter` |

---

## 8. Verification

Build gate before merging:
```bash
cd apps/DAWg && cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)
# Must produce zero warnings, zero sanitizer errors on startup
```

Manual smoke test:
- App launches without crash
- Audio device initializes (status shows device name and sample rate in UI)
- No audible glitch or system audio dropout on startup/shutdown cycle

---

## 9. ADR Reference

This integration is recorded as DAWg ADR-0001. After implementation, add `docs/adr/0001-versaTone-engine-integration.md` following the Zajfan Standard §1.1 anatomy.
