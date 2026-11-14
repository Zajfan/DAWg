#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/TempoMap.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace dawg::engine {

class Metronome final
{
public:
    Metronome() noexcept = default;

    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    void setGain(float gainLinear) noexcept;
    [[nodiscard]] float gain() const noexcept;

    void reset() noexcept;

    // Synthesizes sample-accurate accented downbeat (1760 Hz) and beat (880 Hz)
    // sine clicks into output[blockOffset .. blockOffset + sliceFrames) with zero allocations.
    void renderSlice(AudioBufferView output,
                     std::size_t blockOffset,
                     std::size_t sliceFrames,
                     std::uint64_t timelineStartSample,
                     double sampleRate,
                     const TempoMap& tempoMap) noexcept;

private:
    std::atomic<bool> m_enabled {false};
    std::atomic<float> m_gainLinear {0.4f};
    double m_phase {0.0};
    double m_phaseIncrement {0.0};
    float m_envelope {0.0f};
    float m_envelopeDecay {0.995f};
};

} // namespace dawg::engine
