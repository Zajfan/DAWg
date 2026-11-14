#include <dawg/engine/dsp/TnhcReverbNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

namespace {
// Base mutually prime delay lengths at 48 kHz
constexpr std::array<std::size_t, TnhcReverbNode::numFdnLines> kBaseFdnLengths = {
    557, 613, 677, 739, 811, 887, 971, 1061
};

constexpr std::array<std::size_t, TnhcReverbNode::numDiffusers> kBaseDiffuserLengths = {
    149, 211, 263, 317
};
} // namespace

TnhcReverbNode::TnhcReverbNode() noexcept
{
    updateFeedbackGains();
}

bool TnhcReverbNode::prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
        return false;

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;

    const double srRatio = m_sampleRate / 48000.0;
    const float sizeScale = std::clamp(m_roomSize, 0.1f, 2.0f);

    for (std::size_t i = 0; i < numDiffusers; ++i)
    {
        const auto len = static_cast<std::size_t>(
            std::round(static_cast<double>(kBaseDiffuserLengths[i]) * srRatio * sizeScale));
        m_diffusers[i].prepare(len);
    }

    for (std::size_t i = 0; i < numFdnLines; ++i)
    {
        const auto len = static_cast<std::size_t>(
            std::round(static_cast<double>(kBaseFdnLengths[i]) * srRatio * sizeScale));
        m_fdnLines[i].prepare(len);
    }

    updateFeedbackGains();
    return true;
}

void TnhcReverbNode::reset() noexcept
{
    for (auto& diff : m_diffusers)
        diff.reset();
    for (auto& line : m_fdnLines)
        line.reset();
}

void TnhcReverbNode::setRoomSize(float size) noexcept
{
    m_roomSize = std::clamp(size, 0.1f, 2.0f);
    (void)prepare(m_sampleRate, 1024, m_channelCount);
}

void TnhcReverbNode::setDecayTime(float seconds) noexcept
{
    m_decayTime = std::clamp(seconds, 0.1f, 20.0f);
    updateFeedbackGains();
}

void TnhcReverbNode::setDamping(float damp) noexcept
{
    m_damping = std::clamp(damp, 0.0f, 0.95f);
}

void TnhcReverbNode::setStereoWidth(float width) noexcept
{
    m_stereoWidth = std::clamp(width, 0.0f, 2.0f);
}

void TnhcReverbNode::setWetDry(float wet) noexcept
{
    m_wetDry = std::clamp(wet, 0.0f, 1.0f);
}

void TnhcReverbNode::updateFeedbackGains() noexcept
{
    // RT60 calculation: gain_i = 10^(-3 * delayTime_i / (decayTime * sampleRate))
    for (auto& line : m_fdnLines)
    {
        const double delayInSec = static_cast<double>(line.length) / m_sampleRate;
        const double g = std::pow(10.0, -3.0 * delayInSec / static_cast<double>(m_decayTime));
        line.feedbackGain = static_cast<float>(std::clamp(g, 0.0, 0.999));
    }
}

void TnhcReverbNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
        return;

    const auto frameCount = audio.frameCount;
    float* const chLeft = audio.channels[0];
    float* const chRight = (audio.channelCount > 1 && audio.channels[1] != nullptr)
        ? audio.channels[1]
        : chLeft;

    std::array<float, numFdnLines> fdnOutputs {};

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const float dryL = chLeft[f];
        const float dryR = chRight[f];
        const float monoIn = 0.5f * (dryL + dryR);

        // 1. Pass through early reflection diffusers in series
        float diffused = monoIn;
        for (auto& diff : m_diffusers)
            diffused = diff.process(diffused);

        // 2. Read outputs from all 8 FDN delay lines
        float sumFdn = 0.0f;
        for (std::size_t i = 0; i < numFdnLines; ++i)
        {
            fdnOutputs[i] = m_fdnLines[i].read();
            sumFdn += fdnOutputs[i];
        }

        // 3. Compute Householder reflection: y_i = fdnOutputs_i - (2 / N) * sum
        // Injection with input diffused signal into feedback network
        const float householderScale = 2.0f / static_cast<float>(numFdnLines);
        for (std::size_t i = 0; i < numFdnLines; ++i)
        {
            const float feedbackSig = fdnOutputs[i] - householderScale * sumFdn + diffused * 0.5f;
            m_fdnLines[i].write(feedbackSig, m_damping);
        }

        // 4. Sum odd lines to Left and even lines to Right
        float wetL = 0.0f;
        float wetR = 0.0f;
        for (std::size_t i = 0; i < numFdnLines; ++i)
        {
            if ((i % 2) == 0)
                wetL += fdnOutputs[i];
            else
                wetR += fdnOutputs[i];
        }

        wetL *= 0.5f;
        wetR *= 0.5f;

        // 5. Stereo Width adjustment via Mid/Side
        const float mid = 0.5f * (wetL + wetR);
        const float side = 0.5f * (wetL - wetR);
        const float finalWetL = mid + m_stereoWidth * side;
        const float finalWetR = mid - m_stereoWidth * side;

        // 6. Wet/Dry mix
        chLeft[f] = dryL * (1.0f - m_wetDry) + finalWetL * m_wetDry;
        if (chLeft != chRight)
            chRight[f] = dryR * (1.0f - m_wetDry) + finalWetR * m_wetDry;
    }
}

} // namespace dawg::engine
