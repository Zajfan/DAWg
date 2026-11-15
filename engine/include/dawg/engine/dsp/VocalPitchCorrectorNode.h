#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/dsp/MusicalScale.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

struct DetectedPitchInfo
{
    float pitchHz {0.0f};
    float midiPitch {0.0f};       // Fractional MIDI note (e.g. 60.35)
    float targetMidiPitch {0.0f}; // Quantized target note
    float confidence {0.0f};      // 0.0 to 1.0
    bool isVoiced {false};
};

// Pro Vocal Pitch Correction & Tuning Engine (Melodyne / Auto-Tune style)
// Implements YIN/pYIN sub-sample fundamental pitch tracking,
// musical scale quantization, variable retune speed (Hard-Tune vs Transparent),
// and formant-locked pitch correction.
class VocalPitchCorrectorNode final : public ProcessorNode
{
public:
    VocalPitchCorrectorNode() noexcept;
    ~VocalPitchCorrectorNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Scale & Tuning parameters
    void setScale(MusicalScale scale) noexcept { m_scale = scale; }
    [[nodiscard]] MusicalScale scale() const noexcept { return m_scale; }

    void setRootNote(std::uint8_t rootMidiNote) noexcept { m_rootNote = rootMidiNote % 12; }
    [[nodiscard]] std::uint8_t rootNote() const noexcept { return m_rootNote; }

    // Retune Speed: 0.0 = Hard-Tune (Travis Scott/T-Pain instant snap),
    // 0.01 - 0.20 = Commercial Pop, 0.5 - 1.0 = Transparent natural stabilization
    void setRetuneSpeed(float speed) noexcept { m_retuneSpeed = std::clamp(speed, 0.0f, 1.0f); }
    [[nodiscard]] float retuneSpeed() const noexcept { return m_retuneSpeed; }

    // Pitch Correction Amount: 0% (Bypass) to 100% (Full lock)
    void setCorrectionAmount(float amount) noexcept { m_correctionAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float correctionAmount() const noexcept { return m_correctionAmount; }

    // Manual Formant Shift: -12 to +12 semitones
    void setFormantShiftSemitones(float semitones) noexcept { m_formantShift = std::clamp(semitones, -12.0f, 12.0f); }
    [[nodiscard]] float formantShiftSemitones() const noexcept { return m_formantShift; }

    // Telemetry for Piano Roll pitch blob display
    [[nodiscard]] DetectedPitchInfo currentPitchInfo() const noexcept { return m_lastPitchInfo; }

    // Quantize continuous fractional pitch to chosen musical scale
    [[nodiscard]] static float quantizeToScale(float midiPitch, MusicalScale scale, std::uint8_t rootNote) noexcept;

private:
    [[nodiscard]] DetectedPitchInfo detectPitchYin(const float* buffer, std::size_t size) const noexcept;

    double m_sampleRate {48000.0};
    MusicalScale m_scale {MusicalScale::chromatic};
    std::uint8_t m_rootNote {0}; // 0 = C
    float m_retuneSpeed {0.1f};   // Snappy pop tune default
    float m_correctionAmount {1.0f};
    float m_formantShift {0.0f};

    float m_smoothedCorrectionSemitones {0.0f};
    DetectedPitchInfo m_lastPitchInfo {};

    // Internal analysis ring buffer
    static constexpr std::size_t yinBufferSize = 1024;
    std::vector<float> m_analysisBuffer;
    std::size_t m_analysisWritePos {0};

    // Real-time dual-tap rotating delay line pitch shifter
    static constexpr std::size_t delayBufferSize = 4096;
    std::vector<float> m_delayBufferL;
    std::vector<float> m_delayBufferR;
    std::size_t m_delayWritePos {0};
    float m_phaseTapA {0.0f};
    float m_phaseTapB {0.5f};
};

} // namespace dawg::engine::dsp
