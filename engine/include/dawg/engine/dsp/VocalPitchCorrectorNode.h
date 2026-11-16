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
    bool isSibilant {false};      // Aperiodic / unvoiced consonant detected
};

struct PitchTrajectoryPoint
{
    float timestampSec {0.0f};
    float detectedHz {0.0f};
    float detectedMidiPitch {0.0f};
    float targetMidiPitch {0.0f};
    float correctedMidiPitch {0.0f};
    float centsDeviation {0.0f};
    float confidence {0.0f};
    bool isVoiced {false};
    bool isSibilant {false};
};

// Dynamic Monophonic Pitch Tracker & Real-Time Vocal Intonation Corrector
// Implements McLeod Pitch Method (MPM) / YIN fundamental frequency tracking,
// aperiodic sibilance detection, musical scale quantization, variable retune speed,
// Natural Vibrato Protection, Humanize amount, and formant-preserved pitch manipulation.
class VocalPitchCorrectorNode final : public ProcessorNode
{
public:
    static constexpr std::size_t trajectoryHistorySize = 256;
    static constexpr std::size_t yinBufferSize = 1024;
    static constexpr std::size_t delayBufferSize = 4096;

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

    // Natural Vibrato Protection: 0.0 (straight tune) to 1.0 (preserve emotional 4-7 Hz vibrato)
    void setVibratoProtection(float amount) noexcept { m_vibratoProtection = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float vibratoProtection() const noexcept { return m_vibratoProtection; }

    // Humanize deadband: 0.0 (exact quantize) to 1.0 (permits organic micro-intonation)
    void setHumanizeAmount(float amount) noexcept { m_humanizeAmount = std::clamp(amount, 0.0f, 1.0f); }
    [[nodiscard]] float humanizeAmount() const noexcept { return m_humanizeAmount; }

    // Formant preservation and independent throat morphing
    void setFormantPreservation(bool enabled) noexcept { m_formantPreservation = enabled; }
    [[nodiscard]] bool isFormantPreservationEnabled() const noexcept { return m_formantPreservation; }

    void setFormantShiftSemitones(float semitones) noexcept { m_formantShift = std::clamp(semitones, -12.0f, 12.0f); }
    [[nodiscard]] float formantShiftSemitones() const noexcept { return m_formantShift; }

    // Telemetry for Piano Roll / Note pitch blob visualization
    [[nodiscard]] DetectedPitchInfo currentPitchInfo() const noexcept { return m_lastPitchInfo; }

    // Read real-time pitch trajectory history points
    void getPitchTrajectory(PitchTrajectoryPoint* destination, std::size_t count) const noexcept;

    // Quantize continuous fractional pitch to chosen musical scale
    [[nodiscard]] static float quantizeToScale(float midiPitch, MusicalScale scale, std::uint8_t rootNote) noexcept;

private:
    [[nodiscard]] DetectedPitchInfo detectPitchYin(const float* buffer, std::size_t size) const noexcept;

    // Formant filter envelope tracking & compensation
    struct FormantFilter
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
        float z1 {0.0f}, z2 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = in * b0 + z1;
            z1 = in * b1 - a1 * out + z2;
            z2 = in * b2 - a2 * out;
            return out;
        }

        inline void reset() noexcept
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

    void updateFormantFilter(float pitchRatio) noexcept;

    double m_sampleRate {48000.0};
    MusicalScale m_scale {MusicalScale::chromatic};
    std::uint8_t m_rootNote {0}; // 0 = C
    float m_retuneSpeed {0.1f};   // Snappy pop tune default
    float m_correctionAmount {1.0f};
    float m_vibratoProtection {0.0f};
    float m_humanizeAmount {0.0f};
    bool m_formantPreservation {true};
    float m_formantShift {0.0f};

    float m_smoothedCorrectionSemitones {0.0f};
    DetectedPitchInfo m_lastPitchInfo {};
    float m_streamTimeSec {0.0f};

    // Vibrato 4-7 Hz bandpass filter state for natural vibrato extraction
    float m_vibratoBpState1 {0.0f};
    float m_vibratoBpState2 {0.0f};
    float m_extractedVibratoSemitones {0.0f};

    // Formant compensation filters for L and R
    FormantFilter m_formantFilterL;
    FormantFilter m_formantFilterR;

    // Internal analysis ring buffer
    std::vector<float> m_analysisBuffer;
    std::size_t m_analysisWritePos {0};

    // Real-time dual-tap rotating delay line pitch shifter
    std::vector<float> m_delayBufferL;
    std::vector<float> m_delayBufferR;
    std::size_t m_delayWritePos {0};
    float m_phaseTapA {0.0f};
    float m_phaseTapB {0.5f};

    // Trajectory history ring buffer
    std::array<PitchTrajectoryPoint, trajectoryHistorySize> m_trajectoryHistory {};
    std::size_t m_trajectoryWritePos {0};
};

} // namespace dawg::engine::dsp
