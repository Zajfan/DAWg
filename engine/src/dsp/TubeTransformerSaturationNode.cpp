#include <dawg/engine/dsp/TubeTransformerSaturationNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

TubeTransformerSaturationNode::TubeTransformerSaturationNode() noexcept
{
    setDriveDb(m_driveDb);
    setOutputTrimDb(m_outputTrimDb);
}

bool TubeTransformerSaturationNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    return true;
}

void TubeTransformerSaturationNode::reset() noexcept
{
    m_sagEnvelopeL = 0.0f;
    m_sagEnvelopeR = 0.0f;
    m_transHysteresisZL = 0.0f;
    m_transHysteresisZR = 0.0f;
    m_transPhaseZL = 0.0f;
    m_transPhaseZR = 0.0f;
}

void TubeTransformerSaturationNode::setDriveDb(float driveDb) noexcept
{
    m_driveDb = std::clamp(driveDb, -12.0f, 36.0f);
    m_driveLinear = std::pow(10.0f, m_driveDb / 20.0f);
}

void TubeTransformerSaturationNode::setOutputTrimDb(float trimDb) noexcept
{
    m_outputTrimDb = std::clamp(trimDb, -24.0f, 24.0f);
    m_outputTrimLinear = std::pow(10.0f, m_outputTrimDb / 20.0f);
}

float TubeTransformerSaturationNode::processTubeNonlinearity(float in, float driveLin, float bias, float sag) noexcept
{
    const float driven = in * driveLin;

    switch (m_tubeType)
    {
        case TubeStageType::triode12AX7:
        {
            // Asymmetric triode transfer function with rich even-order harmonics
            const float v = driven + bias * 0.35f;
            float out = 0.0f;
            if (v >= 0.0f)
            {
                out = v / (1.0f + 0.85f * v);
            }
            else
            {
                out = v / (1.0f - 0.45f * v) - 0.12f * (v * v);
            }
            // Remove DC bias
            const float dcBias = (bias * 0.35f) / (1.0f + 0.85f * bias * 0.35f);
            return (out - dcBias);
        }
        case TubeStageType::pentodeEL34:
        {
            // Symmetric pentode with power supply sag
            const float effectiveCeiling = std::max(0.2f, 1.0f - (m_powerSagAmount * 0.45f * sag));
            const float scaled = driven / effectiveCeiling;
            return std::tanh(scaled) * effectiveCeiling;
        }
        case TubeStageType::consoleOpAmp2520:
        {
            // Discrete op-amp hard-knee saturation with slew limiting
            const float x = driven * 0.85f;
            const float clipped = x - 0.08f * (x * x * x);
            return std::clamp(clipped, -1.15f, 1.15f);
        }
        case TubeStageType::cleanAnalog:
        {
            // Mild tape/opamp soft saturation
            return std::tanh(driven * 0.7f) * 1.4f;
        }
    }
    return driven;
}

float TubeTransformerSaturationNode::processTransformer(
    float in,
    float& histZ,
    float& phaseZ,
    TransformerCoreType core,
    float drive) noexcept
{
    if (core == TransformerCoreType::bypass)
    {
        return in;
    }

    const float H = in * (1.0f + drive);
    float B = 0.0f;

    if (core == TransformerCoreType::steelCore)
    {
        // Steel core: pronounced magnetic hysteresis loop and 3rd harmonic
        const float memory = histZ * 0.38f * drive;
        B = std::tanh(H + memory);
        histZ = B;

        // Core inductance low-frequency phase distortion (allpass/shelf at 45 Hz)
        const float phaseCoeff = 0.94f;
        const float phaseOut = phaseCoeff * B + phaseZ;
        phaseZ = B - phaseCoeff * phaseOut;

        // Blend inductive low-end warmth
        return B * (1.0f - m_lowEndWarmth * 0.3f) + phaseOut * (m_lowEndWarmth * 0.3f);
    }
    else // nickelCore
    {
        // Nickel core: ultra-clean linear range with soft high-level magnetic saturation
        const float memory = histZ * 0.15f * drive;
        B = (H < 1.0f && H > -1.0f) ? (H - 0.05f * (H * H * H)) : std::tanh(H + memory);
        histZ = B;
        return B;
    }
}

void TubeTransformerSaturationNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    float* left = context.audio.channels[0];
    float* right = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : left;

    // Power sag envelope tracking coefficient (~60 ms time constant)
    const float sagAlpha = 1.0f - std::exp(-1.0f / (0.060f * static_cast<float>(m_sampleRate)));

    for (std::size_t i = 0; i < blockFrames; ++i)
    {
        const float dryL = left[i];
        const float dryR = right[i];

        // 1. Update power sag envelope from audio signal level
        m_sagEnvelopeL += (std::abs(dryL) - m_sagEnvelopeL) * sagAlpha;
        m_sagEnvelopeR += (std::abs(dryR) - m_sagEnvelopeR) * sagAlpha;

        // 2. Tube stage processing
        float tubeL = processTubeNonlinearity(dryL, m_driveLinear, m_tubeBias, m_sagEnvelopeL);
        float tubeR = processTubeNonlinearity(dryR, m_driveLinear, m_tubeBias, m_sagEnvelopeR);

        // 3. Magnetic transformer core saturation
        float saturatedL = processTransformer(tubeL, m_transHysteresisZL, m_transPhaseZL, m_transformerCore, m_transformerDrive);
        float saturatedR = processTransformer(tubeR, m_transHysteresisZR, m_transPhaseZR, m_transformerCore, m_transformerDrive);

        // 4. Output trim and dry/wet mix
        saturatedL *= m_outputTrimLinear;
        saturatedR *= m_outputTrimLinear;

        left[i] = dryL * (1.0f - m_dryWetMix) + saturatedL * m_dryWetMix;
        if (right != left)
        {
            right[i] = dryR * (1.0f - m_dryWetMix) + saturatedR * m_dryWetMix;
        }
    }
}

} // namespace dawg::engine::dsp
