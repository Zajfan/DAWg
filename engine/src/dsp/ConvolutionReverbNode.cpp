#include <dawg/engine/dsp/ConvolutionReverbNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

ConvolutionReverbNode::ConvolutionReverbNode() noexcept
{
    loadPreset(ConvolutionIrPreset::cathedralHall);
}

bool ConvolutionReverbNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    rebuildActiveImpulseResponse();

    const auto historySize = std::max<std::size_t>(16384, m_activeIrL.size() + maxBlockSize + static_cast<std::size_t>(sampleRate * 0.25));
    m_inputHistoryL.assign(historySize, 0.0f);
    m_inputHistoryR.assign(historySize, 0.0f);

    reset();
    return true;
}

void ConvolutionReverbNode::reset() noexcept
{
    std::fill(m_inputHistoryL.begin(), m_inputHistoryL.end(), 0.0f);
    std::fill(m_inputHistoryR.begin(), m_inputHistoryR.end(), 0.0f);
    m_historyWritePos = 0;
    m_dampFilterL = 0.0f;
    m_dampFilterR = 0.0f;
}

void ConvolutionReverbNode::setReverseIr(const bool reverse) noexcept
{
    if (m_reverseIr != reverse)
    {
        m_reverseIr = reverse;
        rebuildActiveImpulseResponse();
    }
}

void ConvolutionReverbNode::loadPreset(const ConvolutionIrPreset preset)
{
    m_preset = preset;
    std::size_t irLength = 48000; // default 1 second

    switch (preset)
    {
    case ConvolutionIrPreset::cathedralHall:
        irLength = static_cast<std::size_t>(m_sampleRate * 3.5);
        break;
    case ConvolutionIrPreset::vintagePlate:
        irLength = static_cast<std::size_t>(m_sampleRate * 2.0);
        break;
    case ConvolutionIrPreset::springTank:
        irLength = static_cast<std::size_t>(m_sampleRate * 1.5);
        break;
    case ConvolutionIrPreset::celestionV30Cab:
    case ConvolutionIrPreset::customTweedCab:
        irLength = 2048; // Cabinet IRs are short, precise impulse responses
        break;
    }

    m_rawIrL.assign(irLength, 0.0f);
    m_rawIrR.assign(irLength, 0.0f);

    std::uint32_t seedL = 0x1A2B3C4D;
    std::uint32_t seedR = 0x5E6F7A8B;

    auto nextRand = [](std::uint32_t& s) noexcept -> float {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s) / 4294967296.0f * 2.0f - 1.0f;
    };

    if (preset == ConvolutionIrPreset::celestionV30Cab || preset == ConvolutionIrPreset::customTweedCab)
    {
        // Synthesize physical speaker cone & cabinet resonance impulse response
        const float resFreq = (preset == ConvolutionIrPreset::celestionV30Cab) ? 85.0f : 110.0f;
        const float biteFreq = (preset == ConvolutionIrPreset::celestionV30Cab) ? 2800.0f : 2100.0f;

        for (std::size_t i = 0; i < irLength; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(m_sampleRate);
            const float env = std::exp(-t * 120.0f);
            const float thump = std::sin(2.0f * std::numbers::pi_v<float> * resFreq * t);
            const float bite = 0.5f * std::sin(2.0f * std::numbers::pi_v<float> * biteFreq * t) * std::exp(-t * 240.0f);
            const float direct = (i == 0) ? 0.9f : (i == 1 ? -0.4f : 0.0f);

            m_rawIrL[i] = (direct + thump * 0.4f + bite) * env;
            m_rawIrR[i] = m_rawIrL[i];
        }
    }
    else
    {
        // Synthesize acoustic space / reverb impulse response
        const float decayRate = (preset == ConvolutionIrPreset::cathedralHall) ? 2.5f : ((preset == ConvolutionIrPreset::vintagePlate) ? 4.5f : 6.0f);

        for (std::size_t i = 0; i < irLength; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(m_sampleRate);
            const float env = std::exp(-t * decayRate);

            // Early reflections (first 60ms) have discrete spikes
            float earlyL = 0.0f;
            float earlyR = 0.0f;
            if (i < static_cast<std::size_t>(m_sampleRate * 0.060))
            {
                if (i == 0) { earlyL = 0.95f; earlyR = 0.95f; }
                else if (i == 320) { earlyL = 0.65f; earlyR = 0.45f; }
                else if (i == 680) { earlyL = 0.50f; earlyR = 0.60f; }
                else if (i == 1150) { earlyL = 0.40f; earlyR = 0.35f; }
            }

            const float lateL = nextRand(seedL) * env * 0.45f;
            const float lateR = nextRand(seedR) * env * 0.45f;

            m_rawIrL[i] = earlyL + lateL;
            m_rawIrR[i] = earlyR + lateR;
        }
    }

    rebuildActiveImpulseResponse();
}

void ConvolutionReverbNode::loadCustomImpulseResponse(const float* samples, const std::size_t count, const double /*irSampleRate*/)
{
    if (samples == nullptr || count == 0)
    {
        return;
    }

    m_rawIrL.assign(samples, samples + count);
    m_rawIrR = m_rawIrL; // Dual mono default
    rebuildActiveImpulseResponse();
}

void ConvolutionReverbNode::rebuildActiveImpulseResponse()
{
    if (m_rawIrL.empty())
    {
        return;
    }

    const auto len = m_rawIrL.size();
    m_activeIrL.resize(len);
    m_activeIrR.resize(len);

    // Apply space shaping parameters: decay scale, damping, mic placement, and reverse
    float lpL = 0.0f;
    float lpR = 0.0f;
    const float alphaDamp = std::clamp(1.0f - m_hiDamping * 0.8f, 0.05f, 1.0f);

    // Cabinet mic modeling: on-axis increases high frequencies, distance rolls off highs and adds proximity low boost
    const float micAxisBoost = 1.0f + (m_micOnAxis - 0.5f) * 0.4f;
    const float distAtten = 1.0f / (1.0f + (m_micDistanceInches - 1.0f) * 0.1f);

    float sumEnergy = 0.0f;

    for (std::size_t i = 0; i < len; ++i)
    {
        const auto srcIdx = m_reverseIr ? (len - 1 - i) : i;
        float sL = m_rawIrL[srcIdx];
        float sR = m_rawIrR[srcIdx];

        // Apply decay scaling
        const float t = static_cast<float>(i) / static_cast<float>(m_sampleRate);
        const float decayMod = std::exp(-t * (1.0f / std::max(0.1f, m_decayScale) - 1.0f));
        sL *= decayMod;
        sR *= decayMod;

        // Apply high damping
        lpL += alphaDamp * (sL - lpL);
        lpR += alphaDamp * (sR - lpR);
        sL = lpL * micAxisBoost * distAtten;
        sR = lpR * micAxisBoost * distAtten;

        m_activeIrL[i] = sL;
        m_activeIrR[i] = sR;

        sumEnergy += (sL * sL + sR * sR);
    }

    // Energy normalization
    const float norm = (sumEnergy > 1e-6f) ? (1.0f / std::sqrt(sumEnergy * 0.5f)) : 1.0f;
    for (std::size_t i = 0; i < len; ++i)
    {
        m_activeIrL[i] *= norm;
        m_activeIrR[i] *= norm;
    }

    // Extract zero-latency head partition (first 64 samples)
    const auto headLen = std::min(len, headPartitionSize);
    m_headIrL.assign(m_activeIrL.begin(), m_activeIrL.begin() + headLen);
    m_headIrR.assign(m_activeIrR.begin(), m_activeIrR.begin() + headLen);
}

void ConvolutionReverbNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr || m_activeIrL.empty() || m_inputHistoryL.empty())
    {
        return;
    }

    const auto historySize = m_inputHistoryL.size();
    const auto irLen = std::min<std::size_t>(m_activeIrL.size(), 4096); // Real-time FIR partition limit
    const auto preDelaySamples = static_cast<std::size_t>(m_preDelayMs * 0.001f * static_cast<float>(m_sampleRate));

    const float wet = m_dryWet;
    const float dry = 1.0f - wet;
    const float width = m_stereoWidth;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float inL = ch0[i];
        const float inR = (ch1 != nullptr) ? ch1[i] : inL;

        // Write input to history buffer
        m_inputHistoryL[m_historyWritePos] = inL;
        m_inputHistoryR[m_historyWritePos] = inR;

        // Zero-Latency Direct Convolution FIR sum:
        // y[i] = sum_{k=0}^{irLen-1} h[k] * x[writePos - preDelay - k]
        float convL = 0.0f;
        float convR = 0.0f;

        const auto readBase = (m_historyWritePos + historySize - preDelaySamples) % historySize;

        // Head partition (0 latency direct FIR)
        const auto headCount = std::min(irLen, headPartitionSize);
        for (std::size_t k = 0; k < headCount; ++k)
        {
            const auto readPos = (readBase + historySize - k) % historySize;
            convL += m_headIrL[k] * m_inputHistoryL[readPos];
            convR += m_headIrR[k] * m_inputHistoryR[readPos];
        }

        // Tail partition (strided sample accumulation for real-time safety)
        constexpr std::size_t stride = 4;
        for (std::size_t k = headPartitionSize; k < irLen; k += stride)
        {
            const auto readPos = (readBase + historySize - k) % historySize;
            convL += m_activeIrL[k] * m_inputHistoryL[readPos] * static_cast<float>(stride);
            convR += m_activeIrR[k] * m_inputHistoryR[readPos] * static_cast<float>(stride);
        }

        // Stereo width adjustment: Mid/Side matrixing
        const float mid = 0.5f * (convL + convR);
        const float side = 0.5f * (convL - convR) * width;
        const float wetL = mid + side;
        const float wetR = mid - side;

        ch0[i] = inL * dry + wetL * wet;
        if (ch1 != nullptr)
        {
            ch1[i] = inR * dry + wetR * wet;
        }

        m_historyWritePos = (m_historyWritePos + 1) % historySize;
    }
}

} // namespace dawg::engine::dsp
