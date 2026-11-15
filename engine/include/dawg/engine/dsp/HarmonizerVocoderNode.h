#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/dsp/HarmonicAssistant.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawg::engine::dsp {

enum class HarmonyInterval : std::int8_t
{
    unison = 0,
    minorSecond = 1,
    majorSecond = 2,
    minorThird = 3,
    majorThird = 4,
    perfectFourth = 5,
    tritone = 6,
    perfectFifth = 7,
    minorSixth = 8,
    majorSixth = 9,
    minorSeventh = 10,
    majorSeventh = 11,
    octaveUp = 12,
    octaveDown = -12,
    custom = 127
};

enum class VocoderCarrierSource : std::uint8_t
{
    internalPulseSaw,
    internalNoise,
    externalSidechain
};

struct HarmonyVoice
{
    bool enabled {false};
    float semitones {0.0f};       // -24.0 to +24.0 semitones
    float fineTuneCents {0.0f};   // -100.0 to +100.0 cents
    float gain {0.7f};            // 0.0 to 1.0
    float pan {0.0f};             // -1.0 (left) to +1.0 (right)
    float formantMorph {1.0f};    // 0.5 (deep/masculine) to 2.0 (bright/feminine)
};

// Real-Time Pitch-Shifting Harmonizer & Formant-Locked Vocoder Engine (Eventide / Antares style)
// Features 4-voice intelligent chord-tracking harmonization, formant envelope preservation/morphing,
// and a 32-band analog vocoder with unvoiced sibilance/whisper gate.
class HarmonizerVocoderNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxHarmonyVoices = 4;
    static constexpr std::size_t vocoderBandCount = 32;
    static constexpr std::size_t delayBufferSize = 4096;

    HarmonizerVocoderNode() noexcept;
    ~HarmonizerVocoderNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Harmonizer Configuration
    void setHarmonizerEnabled(bool enabled) noexcept { m_harmonizerEnabled = enabled; }
    [[nodiscard]] bool isHarmonizerEnabled() const noexcept { return m_harmonizerEnabled; }

    void setVoice(std::size_t voiceIdx, const HarmonyVoice& voice) noexcept;
    [[nodiscard]] const HarmonyVoice& voice(std::size_t voiceIdx) const noexcept;

    void setChordContext(std::uint8_t rootMidiNote, ChordQuality quality) noexcept;
    void setScaleSnapEnabled(bool enabled) noexcept { m_scaleSnapEnabled = enabled; }
    [[nodiscard]] bool isScaleSnapEnabled() const noexcept { return m_scaleSnapEnabled; }

    void setDryMix(float dry) noexcept { m_dryMix = std::clamp(dry, 0.0f, 1.0f); }
    [[nodiscard]] float dryMix() const noexcept { return m_dryMix; }

    void setHarmonizerWetMix(float wet) noexcept { m_harmonizerWetMix = std::clamp(wet, 0.0f, 1.0f); }
    [[nodiscard]] float harmonizerWetMix() const noexcept { return m_harmonizerWetMix; }

    // Formant Preservation & Throat Length Morphing
    void setGlobalFormantMorph(float morph) noexcept { m_globalFormantMorph = std::clamp(morph, 0.5f, 2.0f); }
    [[nodiscard]] float globalFormantMorph() const noexcept { return m_globalFormantMorph; }

    // 32-Band Analog Vocoder Configuration
    void setVocoderEnabled(bool enabled) noexcept { m_vocoderEnabled = enabled; }
    [[nodiscard]] bool isVocoderEnabled() const noexcept { return m_vocoderEnabled; }

    void setVocoderCarrierSource(VocoderCarrierSource source) noexcept { m_carrierSource = source; }
    [[nodiscard]] VocoderCarrierSource vocoderCarrierSource() const noexcept { return m_carrierSource; }

    void setVocoderWetMix(float wet) noexcept { m_vocoderWetMix = std::clamp(wet, 0.0f, 1.0f); }
    [[nodiscard]] float vocoderWetMix() const noexcept { return m_vocoderWetMix; }

    void setVocoderFormantShift(float shift) noexcept { m_vocoderFormantShift = std::clamp(shift, 0.5f, 2.0f); updateVocoderFilters(); }
    [[nodiscard]] float vocoderFormantShift() const noexcept { return m_vocoderFormantShift; }

    void setSibilanceGateThreshold(float thresh) noexcept { m_sibilanceThreshold = std::clamp(thresh, 0.01f, 1.0f); }
    [[nodiscard]] float sibilanceGateThreshold() const noexcept { return m_sibilanceThreshold; }

    struct BiquadBand
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
        float z1 {0.0f}, z2 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b2 * in - a2 * out;
            return out;
        }

        void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
    };

private:

    struct PitchVoiceState
    {
        double playheadA {0.0};
        double playheadB {0.0};
        float grainPhase {0.0f};
        BiquadBand formantFilterL {};
        BiquadBand formantFilterR {};
    };

    void updateVocoderFilters() noexcept;
    void updateFormantFilters() noexcept;
    [[nodiscard]] float snapIntervalToChord(float semitones) const noexcept;

    double m_sampleRate {48000.0};
    bool m_harmonizerEnabled {true};
    bool m_scaleSnapEnabled {true};
    float m_dryMix {1.0f};
    float m_harmonizerWetMix {0.8f};
    float m_globalFormantMorph {1.0f};

    std::uint8_t m_chordRoot {60};
    ChordQuality m_chordQuality {ChordQuality::major};

    std::array<HarmonyVoice, maxHarmonyVoices> m_voices {};
    std::array<PitchVoiceState, maxHarmonyVoices> m_voiceStates {};

    // Harmonizer input delay ring buffer
    std::array<float, delayBufferSize> m_pitchRingL {};
    std::array<float, delayBufferSize> m_pitchRingR {};
    std::size_t m_pitchWritePos {0};

    // 32-Band Vocoder Engine
    bool m_vocoderEnabled {false};
    VocoderCarrierSource m_carrierSource {VocoderCarrierSource::internalPulseSaw};
    float m_vocoderWetMix {0.0f};
    float m_vocoderFormantShift {1.0f};
    float m_sibilanceThreshold {0.15f};

    // Modulator & Carrier filterbanks and envelope detectors
    std::array<BiquadBand, vocoderBandCount> m_modulatorFilters {};
    std::array<BiquadBand, vocoderBandCount> m_carrierFiltersL {};
    std::array<BiquadBand, vocoderBandCount> m_carrierFiltersR {};
    std::array<float, vocoderBandCount> m_bandEnvelopes {};

    // Internal carrier oscillator
    float m_carrierPhase {0.0f};
    std::uint32_t m_noiseSeed {1234567};
    float m_prevModSample {0.0f};
};

} // namespace dawg::engine::dsp
