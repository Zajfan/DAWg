#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class SpectralVocoderCarrierSource : std::uint8_t
{
    internalPulse = 0, // Vintage analog pulse carrier with PWM & sub-oscillator
    internalSaw = 1,   // Rich sawtooth carrier with unison detune
    externalSidechain = 2 // External carrier routed from sidechain input
};

// Polyphonic Spectral Vocoder & Carrier-Modulator Resonator
// Modeled after EMS Vocoder 2000, Roland VP-330, and classic analog filter bank vocoders.
// Features 24-band zero-latency analysis/synthesis filter bank, unvoiced sibilance noise injection,
// spectral formant shifting, band spacing morphing, and vintage analog carrier oscillator.
class SpectralVocoderNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 24;

    SpectralVocoderNode() noexcept;
    ~SpectralVocoderNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Carrier Source
    void setCarrierSource(SpectralVocoderCarrierSource source) noexcept { m_carrierSource = source; }
    [[nodiscard]] SpectralVocoderCarrierSource carrierSource() const noexcept { return m_carrierSource; }

    // Internal Carrier Tuning
    void setCarrierFrequencyHz(float freqHz) noexcept { m_carrierFreqHz = std::clamp(freqHz, 20.0f, 2000.0f); }
    [[nodiscard]] float carrierFrequencyHz() const noexcept { return m_carrierFreqHz; }

    void setCarrierPulseWidth(float pw) noexcept { m_carrierPulseWidth = std::clamp(pw, 0.05f, 0.95f); }
    [[nodiscard]] float carrierPulseWidth() const noexcept { return m_carrierPulseWidth; }

    void setCarrierDetuneCents(float cents) noexcept { m_carrierDetuneCents = std::clamp(cents, 0.0f, 50.0f); }
    [[nodiscard]] float carrierDetuneCents() const noexcept { return m_carrierDetuneCents; }

    void setCarrierSubLevel(float sub) noexcept { m_carrierSubLevel = std::clamp(sub, 0.0f, 1.0f); }
    [[nodiscard]] float carrierSubLevel() const noexcept { return m_carrierSubLevel; }

    // Spectral Formant Shift: -12.0 to +12.0 semitones (morphs vocal tract size)
    void setFormantShiftSemitones(float semitones) noexcept;
    [[nodiscard]] float formantShiftSemitones() const noexcept { return m_formantShiftSemitones; }

    // Band Resonance / Filter Q: 1.0 to 18.0
    void setBandQ(float q) noexcept;
    [[nodiscard]] float bandQ() const noexcept { return m_bandQ; }

    // Band Spacing / Width: 0.5 (narrow IDM) to 1.5 (wide cinematic)
    void setBandSpacing(float spacing) noexcept;
    [[nodiscard]] float bandSpacing() const noexcept { return m_bandSpacing; }

    // Envelope Follower Attack & Release (milliseconds)
    void setEnvelopeAttackMs(float attackMs) noexcept;
    [[nodiscard]] float envelopeAttackMs() const noexcept { return m_attackMs; }

    void setEnvelopeReleaseMs(float releaseMs) noexcept;
    [[nodiscard]] float envelopeReleaseMs() const noexcept { return m_releaseMs; }

    // Unvoiced Sibilance Noise Injection
    void setSibilanceThreshold(float thresh) noexcept { m_sibilanceThresh = std::clamp(thresh, 0.05f, 0.90f); }
    [[nodiscard]] float sibilanceThreshold() const noexcept { return m_sibilanceThresh; }

    void setUnvoicedNoiseLevel(float level) noexcept { m_unvoicedNoiseLevel = std::clamp(level, 0.0f, 1.5f); }
    [[nodiscard]] float unvoicedNoiseLevel() const noexcept { return m_unvoicedNoiseLevel; }

    // Wet / Dry Mix: 0.0 to 1.0
    void setMix(float mix) noexcept { m_mix = std::clamp(mix, 0.0f, 1.0f); }
    [[nodiscard]] float mix() const noexcept { return m_mix; }

    // Band Telemetry for UI Spectral Spectrum Display
    void getBandLevels(float* destination, std::size_t count) const noexcept;
    [[nodiscard]] bool isSibilantActive() const noexcept { return m_sibilantDetected; }

private:
    // Cytomic SVF 2nd-order bandpass filter for zero-latency filter bank
    struct BandpassSvf
    {
        float ic1eq {0.0f};
        float ic2eq {0.0f};
        float g {0.0f};
        float k {1.0f};
        float a1 {0.0f};
        float a2 {0.0f};
        float a3 {0.0f};

        void setCoefficients(double sampleRate, float centerHz, float q) noexcept
        {
            const float clampedCenter = std::clamp(centerHz, 30.0f, static_cast<float>(sampleRate * 0.45));
            const float clampedQ = std::max(0.5f, q);
            g = std::tan(static_cast<float>(std::numbers::pi * clampedCenter / sampleRate));
            k = 1.0f / clampedQ;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        inline float process(float v0) noexcept
        {
            const float v3 = v0 - ic2eq;
            const float v1 = a1 * ic1eq + a2 * v3;
            const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            // Standard bandpass output normalized by 1/Q (k) for unity peak gain
            return v1 * k;
        }

        inline void reset() noexcept
        {
            ic1eq = 0.0f;
            ic2eq = 0.0f;
        }
    };

    struct FilterBand
    {
        float baseFreqHz {100.0f};
        float synthFreqHz {100.0f};
        BandpassSvf modFilterL;
        BandpassSvf modFilterR;
        BandpassSvf synthFilterL;
        BandpassSvf synthFilterR;
        float envelopeL {0.0f};
        float envelopeR {0.0f};
    };

    void recomputeFilterFrequencies() noexcept;

    double m_sampleRate {48000.0};
    SpectralVocoderCarrierSource m_carrierSource {SpectralVocoderCarrierSource::internalPulse};
    float m_carrierFreqHz {130.81f}; // C3 default
    float m_carrierPulseWidth {0.40f};
    float m_carrierDetuneCents {8.0f};
    float m_carrierSubLevel {0.35f};
    float m_formantShiftSemitones {0.0f};
    float m_bandQ {8.0f};
    float m_bandSpacing {1.0f};
    float m_attackMs {3.0f};
    float m_releaseMs {25.0f};
    float m_sibilanceThresh {0.30f};
    float m_unvoicedNoiseLevel {0.80f};
    float m_mix {1.0f};

    float m_attackCoeff {0.05f};
    float m_releaseCoeff {0.01f};

    std::array<FilterBand, numBands> m_bands {};
    bool m_sibilantDetected {false};

    // Internal Carrier Oscillator phases
    float m_carrierPhaseA {0.0f};
    float m_carrierPhaseB {0.0f};
    float m_carrierSubPhase {0.0f};

    // White/Pink noise PRNG state
    std::uint32_t m_noiseRng {0x12345678};
    float m_pinkNoiseB0 {0.0f};
    float m_pinkNoiseB1 {0.0f};
    float m_pinkNoiseB2 {0.0f};
};

} // namespace dawg::engine::dsp
