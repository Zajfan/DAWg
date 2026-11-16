#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

enum class PhysicalModelType : std::uint8_t
{
    waveguideString, // Karplus-Strong extended digital waveguide with pluck/bow
    modalMembrane,   // 2D circular drumhead membrane with Bessel ratio modes
    modalBar,        // Tuned acoustic marimba/vibraphone bar
    modalPlate       // Non-linear metallic plate / gong
};

enum class ExciterType : std::uint8_t
{
    mallet, // Dynamic strike impulse with adjustable mallet hardness
    bow,    // Non-linear Coulomb stick-slip friction curve
    breath  // Aerodynamic turbulence noise
};

// Physical Modeling String, Resonator & Membrane Synthesizer (Karplus-Strong / Modal / Chromaphone style)
class PhysicalModelingSynthNode final : public ProcessorNode
{
public:
    static constexpr std::size_t MaxPolyphony = 8;
    static constexpr std::size_t NumModalBands = 6;
    static constexpr std::size_t MaxDelayLineLength = 4096;

    PhysicalModelingSynthNode() noexcept;
    ~PhysicalModelingSynthNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Physical Acoustic Parameters
    void setModelType(PhysicalModelType type) noexcept { m_modelType = type; }
    [[nodiscard]] PhysicalModelType modelType() const noexcept { return m_modelType; }

    void setExciterType(ExciterType exciter) noexcept { m_exciterType = exciter; }
    [[nodiscard]] ExciterType exciterType() const noexcept { return m_exciterType; }

    // Material damping / decay time in seconds (0.05s to 8.0s)
    void setDecayTimeSeconds(float sec) noexcept { m_decayTimeSec = std::clamp(sec, 0.05f, 10.0f); }
    [[nodiscard]] float decayTimeSeconds() const noexcept { return m_decayTimeSec; }

    // Strike / Pluck position along string/membrane (0.01 to 0.99, e.g. 0.5 = center, 0.1 = bridge)
    void setStrikePosition(float pos) noexcept { m_strikePosition = std::clamp(pos, 0.01f, 0.99f); }
    [[nodiscard]] float strikePosition() const noexcept { return m_strikePosition; }

    // Mallet / Pick Hardness (0.0 = soft felt, 1.0 = hard wood/metal)
    void setMalletHardness(float hardness) noexcept { m_malletHardness = std::clamp(hardness, 0.0f, 1.0f); }
    [[nodiscard]] float malletHardness() const noexcept { return m_malletHardness; }

    // String Inharmonicity / Stiffness (0.0 = pure harmonic, 1.0 = stiff steel wire/bell)
    void setInharmonicity(float inharmonicity) noexcept { m_inharmonicity = std::clamp(inharmonicity, 0.0f, 1.0f); }
    [[nodiscard]] float inharmonicity() const noexcept { return m_inharmonicity; }

    // Master Output Level
    void setMasterGain(float gain) noexcept { m_masterGain = std::clamp(gain, 0.0f, 2.0f); }
    [[nodiscard]] float masterGain() const noexcept { return m_masterGain; }

    // Direct Voice Triggering
    void noteOn(int noteNumber, float velocity) noexcept;
    void noteOff(int noteNumber) noexcept;
    void allNotesOff() noexcept;

    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;

private:
    struct ModalFilterState
    {
        float s1 {0.0f};
        float s2 {0.0f};
    };

    struct Voice
    {
        bool active {false};
        int noteNumber {-1};
        float frequencyHz {440.0f};
        float velocity {0.8f};
        bool gate {false};
        std::uint32_t age {0};

        // Karplus-Strong Waveguide State
        std::array<float, MaxDelayLineLength> delayLine {};
        std::size_t writeIdx {0};
        float lpState {0.0f};
        float bowVelocity {0.0f};

        // Exciter Pulse State
        float exciterPhase {0.0f};
        float exciterDurationSamples {100.0f};

        // Modal Resonator States (6 Modes)
        std::array<ModalFilterState, NumModalBands> modalFiltersL {};
        std::array<ModalFilterState, NumModalBands> modalFiltersR {};
    };

    void processMidi(const MidiBufferView& midi) noexcept;
    void renderVoice(Voice& voice, float* outL, float* outR, std::size_t numSamples) noexcept;

    double m_sampleRate {48000.0};
    PhysicalModelType m_modelType {PhysicalModelType::waveguideString};
    ExciterType m_exciterType {ExciterType::mallet};

    float m_decayTimeSec {1.5f};
    float m_strikePosition {0.25f};
    float m_malletHardness {0.5f};
    float m_inharmonicity {0.05f};
    float m_masterGain {0.8f};

    std::uint32_t m_voiceAgeCounter {0};
    std::array<Voice, MaxPolyphony> m_voices {};

    std::uint32_t m_noiseSeed {54321};
    float generateNoise() noexcept;
};

} // namespace dawg::engine::dsp
