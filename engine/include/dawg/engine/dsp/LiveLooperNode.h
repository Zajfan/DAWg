#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class LooperState : std::uint8_t
{
    stopped,
    recording,
    playing,
    overdubbing
};

enum class OverdubMode : std::uint8_t
{
    add,      // Sound-on-sound tape overdubbing
    replace,  // Punch-in replaces underlying audio
    multiply  // Multiplies loop length by integer factor
};

// Live Performance Looper & Granular Live-Buffer Freeze (Ableton Live / Blooper style)
class LiveLooperNode final : public ProcessorNode
{
public:
    static constexpr std::size_t MaxLoopSeconds = 30;
    static constexpr std::size_t MaxLayers = 4; // Undo/Redo history layers

    LiveLooperNode() noexcept;
    ~LiveLooperNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Transport & Looper Controls
    void triggerRecord() noexcept;
    void triggerPlay() noexcept;
    void triggerOverdub() noexcept;
    void triggerStop() noexcept;
    void clear() noexcept;

    // Undo / Redo Layers
    void undo() noexcept;
    void redo() noexcept;
    [[nodiscard]] bool canUndo() const noexcept { return m_layerCount > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_redoAvailable; }

    // Overdub Configuration
    void setOverdubMode(OverdubMode mode) noexcept { m_overdubMode = mode; }
    [[nodiscard]] OverdubMode overdubMode() const noexcept { return m_overdubMode; }

    // Playback Speed & Pitch (-2.0 to +2.0, e.g. -1.0 = reverse, 0.5 = half speed)
    void setSpeed(float speed) noexcept { m_speed = std::clamp(speed, -2.0f, 2.0f); }
    [[nodiscard]] float speed() const noexcept { return m_speed; }

    void setPitchSemitones(int semitones) noexcept;
    [[nodiscard]] int pitchSemitones() const noexcept { return m_pitchSemitones; }

    // Buffer Degradation & Live Freeze
    // Feedback / Decay factor per loop cycle (0.0 to 1.0, 1.0 = infinite loop, 0.8 = analog tape decay)
    void setTapeDecay(float decay) noexcept { m_tapeDecay = std::clamp(decay, 0.0f, 1.0f); }
    [[nodiscard]] float tapeDecay() const noexcept { return m_tapeDecay; }

    // Wow & Flutter micro-pitch instability (0.0 to 1.0)
    void setWowFlutter(float amount) noexcept { m_wowFlutter = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float wowFlutter() const noexcept { return m_wowFlutter; }

    // Live Granular Buffer Freeze
    void setFreeze(bool freeze) noexcept { m_frozen = freeze; }
    [[nodiscard]] bool isFrozen() const noexcept { return m_frozen; }

    // Dry/Wet Mix (0.0 = Dry, 1.0 = 100% Looper Playback)
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

    // Telemetry
    [[nodiscard]] LooperState state() const noexcept { return m_state; }
    [[nodiscard]] std::size_t loopLengthSamples() const noexcept { return m_loopLength; }
    [[nodiscard]] float playbackPositionNormalized() const noexcept;

private:
    void commitCurrentLayerToHistory() noexcept;
    float readLoopSample(const std::vector<float>& buffer, double pos) const noexcept;

    double m_sampleRate {48000.0};
    LooperState m_state {LooperState::stopped};
    OverdubMode m_overdubMode {OverdubMode::add};

    float m_speed {1.0f};
    int m_pitchSemitones {0};
    float m_tapeDecay {1.0f};
    float m_wowFlutter {0.0f};
    float m_mix {1.0f};
    bool m_frozen {false};

    // Loop audio buffers (Active Layer)
    std::vector<float> m_loopBufferL;
    std::vector<float> m_loopBufferR;
    std::size_t m_loopLength {0};
    double m_playbackPos {0.0};

    // Undo / Redo History Stack (up to MaxLayers)
    struct LayerBackup
    {
        std::vector<float> bufferL;
        std::vector<float> bufferR;
        std::size_t length {0};
    };
    std::array<LayerBackup, MaxLayers> m_history {};
    std::size_t m_layerCount {0};
    LayerBackup m_redoBackup {};
    bool m_redoAvailable {false};

    // Wow & Flutter oscillator
    double m_wfPhase {0.0};

    // Crossfade smoothing state for anti-click boundaries
    static constexpr std::size_t kCrossfadeSamples = 256;
};

} // namespace dawg::engine::dsp
