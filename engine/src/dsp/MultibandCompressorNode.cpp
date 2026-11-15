#include <dawg/engine/dsp/MultibandCompressorNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

void MultibandCompressorNode::CrossoverPair::updateCoeffs(const float freqHz, const double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
    {
        return;
    }
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    const float f = std::clamp(freqHz, 10.0f, nyquist * 0.95f);
    const float omega = 2.0f * std::numbers::pi_v<float> * f / static_cast<float>(sampleRate);
    const float cosW = std::cos(omega);
    const float sinW = std::sin(omega);

    // Butterworth Q = 1/sqrt(2) => alpha = sin(w) / (2 * Q) = sin(w) * sqrt(0.5)
    constexpr float qInv = 0.7071067811865475f;
    const float alpha = sinW * 0.5f / qInv;

    const float a0 = 1.0f + alpha;
    const float invA0 = 1.0f / a0;
    const float a1 = -2.0f * cosW;
    const float a2 = 1.0f - alpha;

    // Lowpass Butterworth 2nd order
    const float lpB0 = (1.0f - cosW) * 0.5f;
    const float lpB1 = 1.0f - cosW;
    const float lpB2 = (1.0f - cosW) * 0.5f;

    lp.stageCoeffs.b0 = lpB0 * invA0;
    lp.stageCoeffs.b1 = lpB1 * invA0;
    lp.stageCoeffs.b2 = lpB2 * invA0;
    lp.stageCoeffs.a1 = a1 * invA0;
    lp.stageCoeffs.a2 = a2 * invA0;

    // Highpass Butterworth 2nd order
    const float hpB0 = (1.0f + cosW) * 0.5f;
    const float hpB1 = -(1.0f + cosW);
    const float hpB2 = (1.0f + cosW) * 0.5f;

    hp.stageCoeffs.b0 = hpB0 * invA0;
    hp.stageCoeffs.b1 = hpB1 * invA0;
    hp.stageCoeffs.b2 = hpB2 * invA0;
    hp.stageCoeffs.a1 = a1 * invA0;
    hp.stageCoeffs.a2 = a2 * invA0;
}

MultibandCompressorNode::MultibandCompressorNode() noexcept
{
    // Default band parameters: Low, Low-Mid, High-Mid, High
    m_bands[0].settings = {.thresholdDb = -16.0f, .ratio = 4.0f, .attackMs = 15.0f, .releaseMs = 120.0f, .makeupGainDb = 0.0f};
    m_bands[1].settings = {.thresholdDb = -18.0f, .ratio = 3.0f, .attackMs = 10.0f, .releaseMs = 90.0f, .makeupGainDb = 0.0f};
    m_bands[2].settings = {.thresholdDb = -20.0f, .ratio = 3.0f, .attackMs = 8.0f, .releaseMs = 70.0f, .makeupGainDb = 0.0f};
    m_bands[3].settings = {.thresholdDb = -22.0f, .ratio = 2.5f, .attackMs = 5.0f, .releaseMs = 50.0f, .makeupGainDb = 0.0f};

    for (std::size_t b = 0; b < numBands; ++b)
    {
        m_gainReductionMeters[b].store(0.0f, std::memory_order_relaxed);
        updateBallistics(b);
    }
    updateCrossovers();
}

bool MultibandCompressorNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    updateCrossovers();
    for (std::size_t b = 0; b < numBands; ++b)
    {
        updateBallistics(b);
    }
    reset();
    return true;
}

void MultibandCompressorNode::reset() noexcept
{
    for (auto& co : m_crossoversL)
    {
        co.reset();
    }
    for (auto& co : m_crossoversR)
    {
        co.reset();
    }
    for (std::size_t b = 0; b < numBands; ++b)
    {
        m_bands[b].grDb = 0.0f;
        m_gainReductionMeters[b].store(0.0f, std::memory_order_relaxed);
    }
}

void MultibandCompressorNode::setCrossoverFrequencies(const float f1Hz, const float f2Hz, const float f3Hz) noexcept
{
    m_crossover1Hz = std::clamp(f1Hz, 20.0f, 500.0f);
    m_crossover2Hz = std::clamp(f2Hz, m_crossover1Hz + 20.0f, 4000.0f);
    m_crossover3Hz = std::clamp(f3Hz, m_crossover2Hz + 50.0f, 18000.0f);
    updateCrossovers();
}

void MultibandCompressorNode::updateCrossovers() noexcept
{
    // Crossover 0: split Low-Half vs High-Half at f2
    m_crossoversL[0].updateCoeffs(m_crossover2Hz, m_sampleRate);
    m_crossoversR[0].updateCoeffs(m_crossover2Hz, m_sampleRate);

    // Crossover 1: split Low-Half into Band 0 (Low) and Band 1 (Low-Mid) at f1
    m_crossoversL[1].updateCoeffs(m_crossover1Hz, m_sampleRate);
    m_crossoversR[1].updateCoeffs(m_crossover1Hz, m_sampleRate);

    // Crossover 2: split High-Half into Band 2 (High-Mid) and Band 3 (High) at f3
    m_crossoversL[2].updateCoeffs(m_crossover3Hz, m_sampleRate);
    m_crossoversR[2].updateCoeffs(m_crossover3Hz, m_sampleRate);
}

void MultibandCompressorNode::setBandSettings(const std::size_t bandIdx, const MultibandBandSettings& settings) noexcept
{
    if (bandIdx >= numBands)
    {
        return;
    }
    m_bands[bandIdx].settings = settings;
    updateBallistics(bandIdx);
}

const MultibandBandSettings& MultibandCompressorNode::bandSettings(const std::size_t bandIdx) const noexcept
{
    static constexpr MultibandBandSettings defaultSettings {};
    if (bandIdx >= numBands)
    {
        return defaultSettings;
    }
    return m_bands[bandIdx].settings;
}

void MultibandCompressorNode::updateBallistics(const std::size_t bandIdx) noexcept
{
    if (bandIdx >= numBands || m_sampleRate <= 0.0)
    {
        return;
    }

    auto& b = m_bands[bandIdx];
    const float attSec = std::max(b.settings.attackMs * 0.001f, 0.0001f);
    const float relSec = std::max(b.settings.releaseMs * 0.001f, 0.001f);

    b.attackCoeff = 1.0f - std::exp(-1.0f / (attSec * static_cast<float>(m_sampleRate)));
    b.releaseCoeff = 1.0f - std::exp(-1.0f / (relSec * static_cast<float>(m_sampleRate)));
    b.makeupLinear = std::pow(10.0f, b.settings.makeupGainDb * 0.05f);
}

void MultibandCompressorNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return;
    }

    const bool hasSolo = std::any_of(m_bands.begin(), m_bands.end(), [](const BandRuntime& b) {
        return b.settings.solo;
    });

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float inL = ch0[i];
        const float inR = ch1[i];

        // 1. Split into Low-Half and High-Half using crossover 2
        const float lowHalfL = m_crossoversL[0].lp.process(inL);
        const float highHalfL = m_crossoversL[0].hp.process(inL);
        const float lowHalfR = m_crossoversR[0].lp.process(inR);
        const float highHalfR = m_crossoversR[0].hp.process(inR);

        // 2. Split Low-Half into Band 0 & Band 1 using crossover 1
        std::array<float, numBands> bandSampsL {};
        std::array<float, numBands> bandSampsR {};

        bandSampsL[0] = m_crossoversL[1].lp.process(lowHalfL);
        bandSampsL[1] = m_crossoversL[1].hp.process(lowHalfL);
        bandSampsR[0] = m_crossoversR[1].lp.process(lowHalfR);
        bandSampsR[1] = m_crossoversR[1].hp.process(lowHalfR);

        // 3. Split High-Half into Band 2 & Band 3 using crossover 3
        bandSampsL[2] = m_crossoversL[2].lp.process(highHalfL);
        bandSampsL[3] = m_crossoversL[2].hp.process(highHalfL);
        bandSampsR[2] = m_crossoversR[2].lp.process(highHalfR);
        bandSampsR[3] = m_crossoversR[2].hp.process(highHalfR);

        // 4. Compress each band independently
        float outL = 0.0f;
        float outR = 0.0f;

        for (std::size_t b = 0; b < numBands; ++b)
        {
            auto& band = m_bands[b];
            const auto& st = band.settings;

            // Check solo/mute state
            const bool isMuted = st.mute || (hasSolo && !st.solo);

            if (isMuted)
            {
                continue;
            }

            if (st.bypassed)
            {
                outL += bandSampsL[b] * band.makeupLinear;
                outR += bandSampsR[b] * band.makeupLinear;
                continue;
            }

            // Peak level detection
            const float peakLevel = std::max(std::abs(bandSampsL[b]), std::abs(bandSampsR[b]));
            const float levelDb = 20.0f * std::log10(std::max(peakLevel, 1e-5f));

            float targetGrDb = 0.0f;
            if (levelDb > st.thresholdDb && st.ratio > 1.0f)
            {
                const float overDb = levelDb - st.thresholdDb;
                targetGrDb = -overDb * (1.0f - (1.0f / st.ratio));
            }

            // Ballistics smoothing
            if (targetGrDb < band.grDb)
            {
                band.grDb += band.attackCoeff * (targetGrDb - band.grDb);
            }
            else
            {
                band.grDb += band.releaseCoeff * (targetGrDb - band.grDb);
            }

            // Combined linear gain: GR + makeup gain
            const float totalGainDb = band.grDb + st.makeupGainDb;
            const float linearGain = std::pow(10.0f, totalGainDb * 0.05f);

            outL += bandSampsL[b] * linearGain;
            outR += bandSampsR[b] * linearGain;
        }

        ch0[i] = outL;
        if (context.audio.channelCount >= 2)
        {
            ch1[i] = outR;
        }
    }

    // Publish gain reduction telemetry (positive value in dB: 0 to 30)
    for (std::size_t b = 0; b < numBands; ++b)
    {
        const float grDisplay = std::max(-m_bands[b].grDb, 0.0f);
        m_gainReductionMeters[b].store(grDisplay, std::memory_order_relaxed);
    }
}

} // namespace dawg::engine::dsp
