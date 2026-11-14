#include <dawg/engine/dsp/ParametricEQNode.h>

#include <algorithm>

namespace dawg::engine::dsp {

ParametricEQNode::ParametricEQNode(ParametricEQSettings settings) noexcept
    : m_settings(std::move(settings))
{
    updateAllCoeffs();
}

void ParametricEQNode::setSettings(const ParametricEQSettings& settings) noexcept
{
    m_settings = settings;
    updateAllCoeffs();
}

bool ParametricEQNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    m_states.assign(channelCount, std::array<FilterState, 4> {});
    updateAllCoeffs();
    return true;
}

void ParametricEQNode::reset() noexcept
{
    for (auto& chStates : m_states)
    {
        chStates.fill(FilterState {});
    }
}

ParametricEQNode::BiquadCoeffs ParametricEQNode::computeCoeffs(
    const FilterBandType type,
    const double frequencyHz,
    const double q,
    const double gainDb,
    const double sampleRate) noexcept
{
    const double safeSampleRate = std::max(1000.0, sampleRate);
    const double clampedFreq = std::clamp(frequencyHz, 10.0, 0.49 * safeSampleRate);
    const double safeQ = std::clamp(q, 0.1, 30.0);

    const double omega = 2.0 * std::numbers::pi_v<double> * clampedFreq / safeSampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double alpha = sinOmega / (2.0 * safeQ);

    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;

    switch (type)
    {
    case FilterBandType::highPass:
    {
        b0 = (1.0 + cosOmega) * 0.5;
        b1 = -(1.0 + cosOmega);
        b2 = (1.0 + cosOmega) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosOmega;
        a2 = 1.0 - alpha;
        break;
    }
    case FilterBandType::lowPass:
    {
        b0 = (1.0 - cosOmega) * 0.5;
        b1 = 1.0 - cosOmega;
        b2 = (1.0 - cosOmega) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosOmega;
        a2 = 1.0 - alpha;
        break;
    }
    case FilterBandType::bell:
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosOmega;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosOmega;
        a2 = 1.0 - alpha / A;
        break;
    }
    case FilterBandType::lowShelf:
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double sqrtA = std::sqrt(A);
        const double twoSqrtAAlpha = 2.0 * sqrtA * alpha;

        b0 = A * ((A + 1.0) - (A - 1.0) * cosOmega + twoSqrtAAlpha);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosOmega);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosOmega - twoSqrtAAlpha);
        a0 = (A + 1.0) + (A - 1.0) * cosOmega + twoSqrtAAlpha;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosOmega);
        a2 = (A + 1.0) + (A - 1.0) * cosOmega - twoSqrtAAlpha;
        break;
    }
    case FilterBandType::highShelf:
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double sqrtA = std::sqrt(A);
        const double twoSqrtAAlpha = 2.0 * sqrtA * alpha;

        b0 = A * ((A + 1.0) + (A - 1.0) * cosOmega + twoSqrtAAlpha);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosOmega);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosOmega - twoSqrtAAlpha);
        a0 = (A + 1.0) - (A - 1.0) * cosOmega + twoSqrtAAlpha;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosOmega);
        a2 = (A + 1.0) - (A - 1.0) * cosOmega - twoSqrtAAlpha;
        break;
    }
    }

    const double invA0 = 1.0 / a0;
    return BiquadCoeffs {
        .b0 = b0 * invA0,
        .b1 = b1 * invA0,
        .b2 = b2 * invA0,
        .a1 = a1 * invA0,
        .a2 = a2 * invA0
    };
}

void ParametricEQNode::updateAllCoeffs() noexcept
{
    for (std::size_t i = 0; i < 4; ++i)
    {
        const auto& band = m_settings.bands[i];
        if (band.enabled)
        {
            m_coeffs[i] = computeCoeffs(
                band.type, band.frequencyHz, band.q, band.gainDb, m_sampleRate);
        }
        else
        {
            // Identity pass-through
            m_coeffs[i] = BiquadCoeffs {1.0, 0.0, 0.0, 0.0, 0.0};
        }
    }
}

void ParametricEQNode::process(ProcessContext& context) noexcept
{
    if (m_settings.bypassed || context.audio.channels == nullptr || context.audio.frameCount == 0)
    {
        return;
    }

    const auto activeChannels = std::min<std::size_t>(context.audio.channelCount, m_states.size());
    const std::size_t frameCount = context.audio.frameCount;

    for (std::size_t ch = 0; ch < activeChannels; ++ch)
    {
        float* channelData = context.audio.channels[ch];
        if (channelData == nullptr)
        {
            continue;
        }

        auto& chStates = m_states[ch];

        for (std::size_t bandIdx = 0; bandIdx < 4; ++bandIdx)
        {
            if (! m_settings.bands[bandIdx].enabled)
            {
                continue;
            }

            const auto& coeff = m_coeffs[bandIdx];
            auto& state = chStates[bandIdx];

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const double x = static_cast<double>(channelData[i]);
                const double y = coeff.b0 * x + state.z1;
                state.z1 = coeff.b1 * x - coeff.a1 * y + state.z2;
                state.z2 = coeff.b2 * x - coeff.a2 * y;
                channelData[i] = static_cast<float>(y);
            }
        }
    }
}

} // namespace dawg::engine::dsp
