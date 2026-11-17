#include <dawg/engine/dsp/CvPitchCalibrator.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

double CvPitchCalibrator::midiPitchToFrequency(const double midiNote) noexcept
{
    return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
}

double CvPitchCalibrator::frequencyToMidiPitch(const double frequencyHz) noexcept
{
    if (frequencyHz <= 0.0) return 0.0;
    return 69.0 + 12.0 * std::log2(frequencyHz / 440.0);
}

double CvPitchCalibrator::detectFundamentalFrequency(
    const float* audioData,
    const std::size_t numSamples,
    const double sampleRate) noexcept
{
    if (audioData == nullptr || numSamples < 64 || sampleRate <= 0.0)
        return 0.0;

    // Zero-crossing baseline detector
    std::size_t zeroCrossings = 0;
    for (std::size_t i = 1; i < numSamples; ++i)
    {
        if ((audioData[i - 1] < 0.0f && audioData[i] >= 0.0f) ||
            (audioData[i - 1] >= 0.0f && audioData[i] < 0.0f))
        {
            ++zeroCrossings;
        }
    }

    if (zeroCrossings < 2)
        return 0.0;

    const double coarseFreq = (static_cast<double>(zeroCrossings) * sampleRate) / (2.0 * static_cast<double>(numSamples));

    // Autocorrelation refinement within expected period window
    const auto minPeriod = static_cast<std::size_t>(std::max(2.0, sampleRate / 2000.0)); // up to 2kHz
    const auto maxPeriod = static_cast<std::size_t>(std::min(static_cast<double>(numSamples / 2), sampleRate / 30.0)); // down to 30Hz

    double bestCorrelation = -1.0;
    std::size_t bestLag = 0;

    for (std::size_t lag = minPeriod; lag < maxPeriod; ++lag)
    {
        double sum = 0.0;
        double norm1 = 0.0;
        double norm2 = 0.0;
        const std::size_t count = numSamples - lag;

        for (std::size_t i = 0; i < count; ++i)
        {
            const double s1 = audioData[i];
            const double s2 = audioData[i + lag];
            sum += s1 * s2;
            norm1 += s1 * s1;
            norm2 += s2 * s2;
        }

        const double denom = std::sqrt(norm1 * norm2);
        if (denom > 1e-9)
        {
            const double corr = sum / denom;
            if (corr > bestCorrelation)
            {
                bestCorrelation = corr;
                bestLag = lag;
            }
        }
    }

    if (bestCorrelation > 0.6 && bestLag > 0)
    {
        return sampleRate / static_cast<double>(bestLag);
    }

    return coarseFreq;
}

CalibrationMeasurement CvPitchCalibrator::calibrateNote(
    const std::uint8_t targetNote,
    const float* synthAudioResponse,
    const std::size_t numSamples,
    const double sampleRate) noexcept
{
    CalibrationMeasurement m;
    m.targetMidiNote = targetNote;
    m.targetFrequencyHz = midiPitchToFrequency(static_cast<double>(targetNote));

    m.measuredFrequencyHz = detectFundamentalFrequency(synthAudioResponse, numSamples, sampleRate);
    if (m.measuredFrequencyHz > 10.0)
    {
        const double measuredMidi = frequencyToMidiPitch(m.measuredFrequencyHz);
        // Voltage error in 1V/Oct standard: 1V = 12 semitones
        // If measured pitch is lower than target, we need positive voltage correction
        const double pitchDeltaSemitones = static_cast<double>(targetNote) - measuredMidi;
        m.voltageCorrectionOffset = static_cast<float>(pitchDeltaSemitones / 12.0);
    }
    else
    {
        m.voltageCorrectionOffset = 0.0f;
    }

    return m;
}

void CvPitchCalibrator::applyCalibrationCurve(
    CvGateEurorackNode& targetNode,
    const std::vector<CalibrationMeasurement>& measurements)
{
    if (measurements.empty())
        return;

    for (const auto& m : measurements)
    {
        targetNode.setCalibrationOffset(m.targetMidiNote, m.voltageCorrectionOffset);
    }

    // Linearly interpolate between calibrated anchor points
    for (std::size_t i = 0; i + 1 < measurements.size(); ++i)
    {
        const auto& m1 = measurements[i];
        const auto& m2 = measurements[i + 1];

        const int n1 = m1.targetMidiNote;
        const int n2 = m2.targetMidiNote;
        if (n2 > n1)
        {
            for (int n = n1 + 1; n < n2; ++n)
            {
                const float t = static_cast<float>(n - n1) / static_cast<float>(n2 - n1);
                const float interpolatedOffset = m1.voltageCorrectionOffset + (m2.voltageCorrectionOffset - m1.voltageCorrectionOffset) * t;
                targetNode.setCalibrationOffset(static_cast<std::uint8_t>(n), interpolatedOffset);
            }
        }
    }
}

} // namespace dawg::engine::dsp
