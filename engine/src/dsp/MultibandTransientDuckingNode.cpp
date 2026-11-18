#include <dawg/engine/dsp/MultibandTransientDuckingNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = 3.14159265358979323846f;

void designButterworthLp(float cutoffHz, double sampleRate, MultibandTransientDuckingNode::SimpleBiquad& bq) noexcept
{
    const float omega = 2.0f * pi * std::clamp(cutoffHz, 20.0f, static_cast<float>(sampleRate * 0.45f)) / static_cast<float>(sampleRate);
    const float sn = std::sin(omega);
    const float cs = std::cos(omega);
    const float alpha = sn / (2.0f * 0.7071f);

    const float a0 = 1.0f + alpha;
    bq.b0 = ((1.0f - cs) / 2.0f) / a0;
    bq.b1 = (1.0f - cs) / a0;
    bq.b2 = ((1.0f - cs) / 2.0f) / a0;
    bq.a1 = (-2.0f * cs) / a0;
    bq.a2 = (1.0f - alpha) / a0;
}

void designButterworthHp(float cutoffHz, double sampleRate, MultibandTransientDuckingNode::SimpleBiquad& bq) noexcept
{
    const float omega = 2.0f * pi * std::clamp(cutoffHz, 20.0f, static_cast<float>(sampleRate * 0.45f)) / static_cast<float>(sampleRate);
    const float sn = std::sin(omega);
    const float cs = std::cos(omega);
    const float alpha = sn / (2.0f * 0.7071f);

    const float a0 = 1.0f + alpha;
    bq.b0 = ((1.0f + cs) / 2.0f) / a0;
    bq.b1 = (-(1.0f + cs)) / a0;
    bq.b2 = ((1.0f + cs) / 2.0f) / a0;
    bq.a1 = (-2.0f * cs) / a0;
    bq.a2 = (1.0f - alpha) / a0;
}

inline float dbToGain(float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

} // namespace

MultibandTransientDuckingNode::MultibandTransientDuckingNode() noexcept
{
    for (auto& a : m_attackGainsDb) a.store(0.0f, std::memory_order_relaxed);
    for (auto& s : m_sustainGainsDb) s.store(0.0f, std::memory_order_relaxed);
    for (auto& d : m_duckingAmountsDb) d.store(-6.0f, std::memory_order_relaxed);

    updateCrossoverCoefficients();
}

void MultibandTransientDuckingNode::updateCrossoverCoefficients() noexcept
{
    const float fc1 = m_lowMidHz.load(std::memory_order_relaxed);
    const float fc2 = m_midHighHz.load(std::memory_order_relaxed);

    for (std::size_t c = 0; c < maxChannels; ++c)
    {
        designButterworthLp(fc1, m_sampleRate, m_lp1[c]);
        designButterworthHp(fc1, m_sampleRate, m_hp1[c]);
        designButterworthLp(fc2, m_sampleRate, m_lp2[c]);
        designButterworthHp(fc2, m_sampleRate, m_hp2[c]);

        designButterworthLp(fc1, m_sampleRate, m_scLp1[c]);
        designButterworthHp(fc1, m_sampleRate, m_scHp1[c]);
        designButterworthLp(fc2, m_sampleRate, m_scLp2[c]);
        designButterworthHp(fc2, m_sampleRate, m_scHp2[c]);
    }

    designButterworthLp(80.0f, m_sampleRate, m_subLp);
}

bool MultibandTransientDuckingNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    updateCrossoverCoefficients();
    return true;
}

void MultibandTransientDuckingNode::reset() noexcept
{
    for (std::size_t c = 0; c < maxChannels; ++c)
    {
        m_lp1[c].reset();
        m_hp1[c].reset();
        m_lp2[c].reset();
        m_hp2[c].reset();
        m_scLp1[c].reset();
        m_scHp1[c].reset();
        m_scLp2[c].reset();
        m_scHp2[c].reset();

        for (std::size_t b = 0; b < numBands; ++b)
        {
            m_fastEnv[c][b] = 0.0f;
            m_slowEnv[c][b] = 0.0f;
        }
    }
    m_sidechainEnergy.fill(0.0f);
    m_subPhase = 0.0f;
    m_subPrevSample = 0.0f;
    m_subLp.reset();
}

void MultibandTransientDuckingNode::setAttackGainDb(const TransientBand band, const float gainDb) noexcept
{
    m_attackGainsDb[static_cast<std::size_t>(band)].store(std::clamp(gainDb, -24.0f, 24.0f), std::memory_order_release);
}

float MultibandTransientDuckingNode::attackGainDb(const TransientBand band) const noexcept
{
    return m_attackGainsDb[static_cast<std::size_t>(band)].load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setSustainGainDb(const TransientBand band, const float gainDb) noexcept
{
    m_sustainGainsDb[static_cast<std::size_t>(band)].store(std::clamp(gainDb, -24.0f, 24.0f), std::memory_order_release);
}

float MultibandTransientDuckingNode::sustainGainDb(const TransientBand band) const noexcept
{
    return m_sustainGainsDb[static_cast<std::size_t>(band)].load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setCrossoverFrequencies(const float lowMidHz, const float midHighHz) noexcept
{
    m_lowMidHz.store(std::clamp(lowMidHz, 40.0f, 1000.0f), std::memory_order_release);
    m_midHighHz.store(std::clamp(midHighHz, 1000.0f, 12000.0f), std::memory_order_release);
    updateCrossoverCoefficients();
}

float MultibandTransientDuckingNode::lowMidCrossoverHz() const noexcept
{
    return m_lowMidHz.load(std::memory_order_acquire);
}

float MultibandTransientDuckingNode::midHighCrossoverHz() const noexcept
{
    return m_midHighHz.load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setSpectralDuckingEnabled(const bool enabled) noexcept
{
    m_duckingEnabled.store(enabled, std::memory_order_release);
}

bool MultibandTransientDuckingNode::isSpectralDuckingEnabled() const noexcept
{
    return m_duckingEnabled.load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setDuckingThresholdDb(const float thresholdDb) noexcept
{
    m_duckingThresholdDb.store(std::clamp(thresholdDb, -60.0f, 0.0f), std::memory_order_release);
}

float MultibandTransientDuckingNode::duckingThresholdDb() const noexcept
{
    return m_duckingThresholdDb.load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setDuckingAmountDb(const TransientBand band, const float duckDb) noexcept
{
    m_duckingAmountsDb[static_cast<std::size_t>(band)].store(std::clamp(duckDb, -36.0f, 0.0f), std::memory_order_release);
}

float MultibandTransientDuckingNode::duckingAmountDb(const TransientBand band) const noexcept
{
    return m_duckingAmountsDb[static_cast<std::size_t>(band)].load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setSubHarmonicEnabled(const bool enabled) noexcept
{
    m_subHarmonicEnabled.store(enabled, std::memory_order_release);
}

bool MultibandTransientDuckingNode::isSubHarmonicEnabled() const noexcept
{
    return m_subHarmonicEnabled.load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::setSubHarmonicMix(const float mix0to1) noexcept
{
    m_subHarmonicMix.store(std::clamp(mix0to1, 0.0f, 1.0f), std::memory_order_release);
}

float MultibandTransientDuckingNode::subHarmonicMix() const noexcept
{
    return m_subHarmonicMix.load(std::memory_order_acquire);
}

void MultibandTransientDuckingNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const std::size_t numChannels = std::min(audio.channelCount, maxChannels);
    auto& sidechain = context.sidechain;
    const bool hasSidechain = (sidechain.channels != nullptr && sidechain.channelCount > 0 && sidechain.frameCount == audio.frameCount);

    // Fast envelope filter coeff (2ms attack) & Slow envelope filter coeff (40ms sustain)
    const float fastCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (m_sampleRate * 0.002)));
    const float slowCoeff = static_cast<float>(1.0 - std::exp(-1.0 / (m_sampleRate * 0.040)));

    std::array<float, numBands> attGains {};
    std::array<float, numBands> susGains {};
    std::array<float, numBands> duckGains {};

    for (std::size_t b = 0; b < numBands; ++b)
    {
        attGains[b] = dbToGain(m_attackGainsDb[b].load(std::memory_order_relaxed));
        susGains[b] = dbToGain(m_sustainGainsDb[b].load(std::memory_order_relaxed));
        duckGains[b] = dbToGain(m_duckingAmountsDb[b].load(std::memory_order_relaxed));
    }

    const bool ducking = m_duckingEnabled.load(std::memory_order_relaxed);
    const float duckThreshLin = dbToGain(m_duckingThresholdDb.load(std::memory_order_relaxed));
    const bool subEnabled = m_subHarmonicEnabled.load(std::memory_order_relaxed);
    const float subMix = m_subHarmonicMix.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        // 1. Analyze sidechain frequency bands if present
        if (hasSidechain && ducking)
        {
            float scIn = sidechain.channels[0][i];
            if (sidechain.channelCount > 1 && sidechain.channels[1] != nullptr)
                scIn = 0.5f * (scIn + sidechain.channels[1][i]);

            const float scLow = m_scLp1[0].process(scIn);
            const float scRest = m_scHp1[0].process(scIn);
            const float scMid = m_scLp2[0].process(scRest);
            const float scHigh = m_scHp2[0].process(scRest);

            m_sidechainEnergy[0] += 0.01f * (std::abs(scLow) - m_sidechainEnergy[0]);
            m_sidechainEnergy[1] += 0.01f * (std::abs(scMid) - m_sidechainEnergy[1]);
            m_sidechainEnergy[2] += 0.01f * (std::abs(scHigh) - m_sidechainEnergy[2]);
        }

        // 2. Process audio channels through 3-band split
        for (std::size_t c = 0; c < numChannels; ++c)
        {
            float* ch = audio.channels[c];
            if (ch == nullptr) continue;

            const float in = ch[i];

            // 3-Band Split: Low (< fc1), Mid (fc1 .. fc2), High (> fc2)
            const float low = m_lp1[c].process(in);
            const float rest = m_hp1[c].process(in);
            const float mid = m_lp2[c].process(rest);
            const float high = m_hp2[c].process(rest);

            const std::array<float, numBands> bands = { low, mid, high };
            float outSum = 0.0f;

            for (std::size_t b = 0; b < numBands; ++b)
            {
                const float absSig = std::abs(bands[b]);
                m_fastEnv[c][b] += fastCoeff * (absSig - m_fastEnv[c][b]);
                m_slowEnv[c][b] += slowCoeff * (absSig - m_slowEnv[c][b]);

                // Transient difference: positive = attack burst, negative = sustain tail
                const float transDiff = m_fastEnv[c][b] - m_slowEnv[c][b];

                float bandGain = 1.0f;
                if (transDiff > 0.001f)
                {
                    // Attack transient
                    bandGain += (attGains[b] - 1.0f) * std::clamp(transDiff * 5.0f, 0.0f, 1.0f);
                }
                else if (transDiff < -0.001f)
                {
                    // Sustain body
                    bandGain += (susGains[b] - 1.0f) * std::clamp(-transDiff * 5.0f, 0.0f, 1.0f);
                }

                // Spectral Clashing Ducking: duck ONLY colliding bands
                if (hasSidechain && ducking && m_sidechainEnergy[b] > duckThreshLin)
                {
                    const float duckFactor = std::clamp((m_sidechainEnergy[b] - duckThreshLin) / duckThreshLin, 0.0f, 1.0f);
                    bandGain *= ((1.0f - duckFactor) + duckFactor * duckGains[b]);
                }

                outSum += bands[b] * bandGain;
            }

            // 3. Sub-harmonic Reinforcer (half-frequency low octave generator)
            if (subEnabled && c == 0)
            {
                // Zero crossing detection on low-band
                if (m_subPrevSample <= 0.0f && low > 0.0f)
                {
                    m_subPhase = (m_subPhase > 0.0f) ? -1.0f : 1.0f;
                }
                m_subPrevSample = low;

                const float rawSub = m_subPhase * std::abs(low);
                const float filteredSub = m_subLp.process(rawSub);
                outSum += filteredSub * subMix;
            }

            ch[i] = outSum;
        }
    }
}

} // namespace dawg::engine::dsp
