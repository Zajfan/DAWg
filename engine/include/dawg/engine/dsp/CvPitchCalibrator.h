#pragma once

#include <dawg/engine/dsp/CvGateEurorackNode.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

struct CalibrationMeasurement
{
    std::uint8_t targetMidiNote {60};
    double targetFrequencyHz {261.625565};
    double measuredFrequencyHz {0.0};
    float voltageCorrectionOffset {0.0f};
};

class CvPitchCalibrator final
{
public:
    CvPitchCalibrator() = default;
    ~CvPitchCalibrator() = default;

    /// Detects fundamental frequency (Hz) using zero-crossing & autocorrelation
    [[nodiscard]] static double detectFundamentalFrequency(
        const float* audioData,
        std::size_t numSamples,
        double sampleRate) noexcept;

    /// Converts frequency in Hz to fractional MIDI pitch
    [[nodiscard]] static double frequencyToMidiPitch(double frequencyHz) noexcept;

    /// Converts MIDI note to standard equal temperament frequency in Hz (A4 = 440Hz)
    [[nodiscard]] static double midiPitchToFrequency(double midiNote) noexcept;

    /// Measures an audio response for a target note and computes the voltage correction
    [[nodiscard]] static CalibrationMeasurement calibrateNote(
        std::uint8_t targetNote,
        const float* synthAudioResponse,
        std::size_t numSamples,
        double sampleRate) noexcept;

    /// Performs automatic pitch calibration across a series of octave notes
    /// and uploads the non-linear voltage correction table to the target CvGateEurorackNode.
    static void applyCalibrationCurve(
        CvGateEurorackNode& targetNode,
        const std::vector<CalibrationMeasurement>& measurements);
};

} // namespace dawg::engine::dsp
