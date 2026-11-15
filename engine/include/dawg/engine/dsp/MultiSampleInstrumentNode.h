#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

struct MultiSampleRegion
{
    std::uint8_t rootNote {60};
    std::uint8_t lowNote {0};
    std::uint8_t highNote {127};
    std::uint8_t lowVel {0};
    std::uint8_t highVel {127};
    std::uint8_t roundRobinIndex {0};
    std::int8_t fineTuneCents {0};

    std::vector<float> sampleDataL;
    std::vector<float> sampleDataR;

    bool loopEnabled {false};
    std::size_t loopStart {0};
    std::size_t loopEnd {0};
};

// Professional Multi-Sample Layered Instrument Engine (Kontakt / EXS24 style)
// Features multi-velocity keyzone layering, velocity crossfading, up to 8-part round-robin
// alternation, sample loop/articulation playback, and SFZ instrument parsing.
class MultiSampleInstrumentNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxVoices = 32;

    MultiSampleInstrumentNode() noexcept;
    ~MultiSampleInstrumentNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Region Management & SFZ Parser
    void addRegion(MultiSampleRegion region);
    void clearRegions() noexcept;
    [[nodiscard]] std::size_t regionCount() const noexcept { return m_regions.size(); }

    // Simple SFZ format descriptor parser
    bool loadFromSfzString(const std::string& sfzContent);

    // Instrument Envelope Controls
    void setAttackMs(float ms) noexcept { m_attackMs = std::clamp(ms, 0.1f, 5000.0f); updateEnvelopeCoeffs(); }
    [[nodiscard]] float attackMs() const noexcept { return m_attackMs; }

    void setReleaseMs(float ms) noexcept { m_releaseMs = std::clamp(ms, 1.0f, 10000.0f); updateEnvelopeCoeffs(); }
    [[nodiscard]] float releaseMs() const noexcept { return m_releaseMs; }

    // Manual Note Triggering API
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] std::uint8_t currentRoundRobin(std::uint8_t note) const noexcept;

private:
    enum class EnvStage : std::uint8_t
    {
        idle,
        attack,
        sustain,
        release
    };

    struct Voice
    {
        bool active {false};
        std::uint8_t midiNote {0};
        std::uint8_t velocity {0};
        const MultiSampleRegion* region {nullptr};
        double playhead {0.0};
        double speed {1.0};
        float gain {1.0f};

        EnvStage envStage {EnvStage::idle};
        float envLevel {0.0f};
    };

    void updateEnvelopeCoeffs() noexcept;
    [[nodiscard]] const MultiSampleRegion* findMatchingRegion(std::uint8_t note, std::uint8_t vel) noexcept;

    double m_sampleRate {48000.0};
    float m_attackMs {5.0f};
    float m_releaseMs {150.0f};
    float m_attackCoeff {0.1f};
    float m_releaseCoeff {0.01f};

    std::vector<MultiSampleRegion> m_regions;
    std::array<std::uint8_t, 128> m_roundRobinTracker {};
    std::array<Voice, maxVoices> m_voices {};
};

} // namespace dawg::engine::dsp
