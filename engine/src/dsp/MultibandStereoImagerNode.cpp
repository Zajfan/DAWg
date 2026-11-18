#include "dawg/engine/dsp/MultibandStereoImagerNode.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

MultibandStereoImagerNode::MultibandStereoImagerNode() noexcept
{
    for (auto& band : m_bands) {
        band.width = 1.0f;
        band.midGainDb = 0.0f;
        band.sideGainDb = 0.0f;
        band.solo = false;
        band.bypass = false;
    }
    updateCrossoverFilters();
    updateMonoBassFilter();
}

void MultibandStereoImagerNode::calculateButterworthLowpass(float fc, BiquadCoeffs& c) const noexcept
{
    const float safeFc = std::clamp(fc, 10.0f, m_sampleRate * 0.49f);
    const float omega = 2.0f * std::numbers::pi_v<float> * safeFc / m_sampleRate;
    const float k = std::tan(omega * 0.5f);
    const float k2 = k * k;
    const float sqrt2 = std::numbers::sqrt2_v<float>;
    const float norm = 1.0f / (1.0f + sqrt2 * k + k2);

    c.b0 = k2 * norm;
    c.b1 = 2.0f * c.b0;
    c.b2 = c.b0;
    c.a1 = 2.0f * (k2 - 1.0f) * norm;
    c.a2 = (1.0f - sqrt2 * k + k2) * norm;
}

void MultibandStereoImagerNode::calculateButterworthHighpass(float fc, BiquadCoeffs& c) const noexcept
{
    const float safeFc = std::clamp(fc, 10.0f, m_sampleRate * 0.49f);
    const float omega = 2.0f * std::numbers::pi_v<float> * safeFc / m_sampleRate;
    const float k = std::tan(omega * 0.5f);
    const float k2 = k * k;
    const float sqrt2 = std::numbers::sqrt2_v<float>;
    const float norm = 1.0f / (1.0f + sqrt2 * k + k2);

    c.b0 = norm;
    c.b1 = -2.0f * c.b0;
    c.b2 = c.b0;
    c.a1 = 2.0f * (k2 - 1.0f) * norm;
    c.a2 = (1.0f - sqrt2 * k + k2) * norm;
}

void MultibandStereoImagerNode::updateCrossoverFilters() noexcept
{
    for (std::size_t i = 0; i < 3; ++i) {
        BiquadCoeffs lpCoeffs {};
        BiquadCoeffs hpCoeffs {};
        calculateButterworthLowpass(m_crossoverFreqs[i], lpCoeffs);
        calculateButterworthHighpass(m_crossoverFreqs[i], hpCoeffs);

        m_lowpassCrossover[i].stage1 = lpCoeffs;
        m_lowpassCrossover[i].stage2 = lpCoeffs;

        m_highpassCrossover[i].stage1 = hpCoeffs;
        m_highpassCrossover[i].stage2 = hpCoeffs;
    }
}

void MultibandStereoImagerNode::updateMonoBassFilter() noexcept
{
    calculateButterworthHighpass(m_monoBassCutoff, m_monoBassCoeffs);
}

bool MultibandStereoImagerNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? static_cast<float>(sampleRate) : 44100.0f;
    updateCrossoverFilters();
    updateMonoBassFilter();
    setHaasDelayMs(m_haasDelayMs);
    reset();
    return true;
}

void MultibandStereoImagerNode::reset() noexcept
{
    for (std::size_t i = 0; i < 3; ++i) {
        m_lowpassCrossover[i].reset();
        m_highpassCrossover[i].reset();
    }
    m_monoBassState.reset();
    m_haasBuffer.fill(0.0f);
    m_haasWriteIdx = 0;
    m_smoothedCorrelation = 1.0f;
    m_smoothedBalance = 0.0f;
}

void MultibandStereoImagerNode::setCrossoverFrequencies(float f1, float f2, float f3) noexcept
{
    m_crossoverFreqs[0] = std::clamp(f1, 20.0f, 500.0f);
    m_crossoverFreqs[1] = std::clamp(f2, m_crossoverFreqs[0] + 50.0f, 5000.0f);
    m_crossoverFreqs[2] = std::clamp(f3, m_crossoverFreqs[1] + 100.0f, 18000.0f);
    updateCrossoverFilters();
}

void MultibandStereoImagerNode::setBandSettings(std::size_t bandIdx, const BandSettings& settings) noexcept
{
    if (bandIdx < NumBands) {
        m_bands[bandIdx] = settings;
    }
}

const MultibandStereoImagerNode::BandSettings& MultibandStereoImagerNode::bandSettings(std::size_t bandIdx) const noexcept
{
    const std::size_t safeIdx = std::min(bandIdx, NumBands - 1);
    return m_bands[safeIdx];
}

void MultibandStereoImagerNode::setBandWidth(std::size_t bandIdx, float width) noexcept
{
    if (bandIdx < NumBands) {
        m_bands[bandIdx].width = std::clamp(width, 0.0f, 3.0f);
    }
}

void MultibandStereoImagerNode::setBandMidGainDb(std::size_t bandIdx, float db) noexcept
{
    if (bandIdx < NumBands) {
        m_bands[bandIdx].midGainDb = std::clamp(db, -24.0f, 24.0f);
    }
}

void MultibandStereoImagerNode::setBandSideGainDb(std::size_t bandIdx, float db) noexcept
{
    if (bandIdx < NumBands) {
        m_bands[bandIdx].sideGainDb = std::clamp(db, -24.0f, 24.0f);
    }
}

void MultibandStereoImagerNode::setBandSolo(std::size_t bandIdx, bool solo) noexcept
{
    if (bandIdx < NumBands) {
        m_bands[bandIdx].solo = solo;
    }
}

void MultibandStereoImagerNode::setBandBypass(std::size_t bandIdx, bool bypass) noexcept
{
    if (bandIdx < NumBands) {
        m_bands[bandIdx].bypass = bypass;
    }
}

void MultibandStereoImagerNode::setMonoBassCutoff(float freqHz) noexcept
{
    m_monoBassCutoff = std::clamp(freqHz, 20.0f, 500.0f);
    updateMonoBassFilter();
}

void MultibandStereoImagerNode::setHaasDelayMs(float ms) noexcept
{
    m_haasDelayMs = std::clamp(ms, 0.1f, 30.0f);
    const float samples = m_haasDelayMs * 0.001f * m_sampleRate;
    m_haasDelaySamples = std::clamp<std::size_t>(static_cast<std::size_t>(samples), 1, MaxHaasBuffer - 1);
}

void MultibandStereoImagerNode::process(ProcessContext& ctx) noexcept
{
    auto& audio = ctx.audio;
    if (audio.channelCount == 0 || audio.frameCount == 0 || audio.channels[0] == nullptr) {
        return;
    }

    float* left = audio.channels[0];
    float* right = audio.channelCount > 1 && audio.channels[1] != nullptr ? audio.channels[1] : audio.channels[0];

    const bool anySolo = std::any_of(m_bands.begin(), m_bands.end(), [](const auto& b) { return b.solo; });

    double sumLR = 0.0;
    double sumL2 = 0.0;
    double sumR2 = 0.0;

    for (std::size_t i = 0; i < audio.frameCount; ++i) {
        const float inL = left[i];
        const float inR = right[i];

        // 4-Band Linkwitz-Riley Tree Crossover Decomposition
        // Split 0: Low (Band 0) vs HighRem0
        float band0L = 0.0f, band0R = 0.0f;
        float highRem0L = 0.0f, highRem0R = 0.0f;
        m_lowpassCrossover[0].processStereo(inL, inR, band0L, band0R);
        m_highpassCrossover[0].processStereo(inL, inR, highRem0L, highRem0R);

        // Split 1: Low-Mid (Band 1) vs HighRem1
        float band1L = 0.0f, band1R = 0.0f;
        float highRem1L = 0.0f, highRem1R = 0.0f;
        m_lowpassCrossover[1].processStereo(highRem0L, highRem0R, band1L, band1R);
        m_highpassCrossover[1].processStereo(highRem0L, highRem0R, highRem1L, highRem1R);

        // Split 2: High-Mid (Band 2) vs High (Band 3)
        float band2L = 0.0f, band2R = 0.0f;
        float band3L = 0.0f, band3R = 0.0f;
        m_lowpassCrossover[2].processStereo(highRem1L, highRem1R, band2L, band2R);
        m_highpassCrossover[2].processStereo(highRem1L, highRem1R, band3L, band3R);

        std::array<float, 4> bL {band0L, band1L, band2L, band3L};
        std::array<float, 4> bR {band0R, band1R, band2R, band3R};

        float outL = 0.0f;
        float outR = 0.0f;

        for (std::size_t b = 0; b < NumBands; ++b) {
            if (anySolo && !m_bands[b].solo) {
                continue;
            }

            float mid = 0.5f * (bL[b] + bR[b]);
            float side = 0.5f * (bL[b] - bR[b]);

            if (!m_bands[b].bypass) {
                const float midGain = std::pow(10.0f, m_bands[b].midGainDb * 0.05f);
                const float sideGain = std::pow(10.0f, m_bands[b].sideGainDb * 0.05f);

                mid *= midGain;
                side *= (sideGain * m_bands[b].width);

                // Elliptic Mono Bass: filter side signal of low band (Band 0)
                if (b == 0 && m_monoBassEnabled) {
                    side = m_monoBassState.process(side, m_monoBassCoeffs);
                }
            }

            outL += (mid + side);
            outR += (mid - side);
        }

        // Haas All-Pass Spatial Widening on Side Signal (Comb-free in mono)
        if (m_haasEnabled) {
            float finalMid = 0.5f * (outL + outR);
            float finalSide = 0.5f * (outL - outR);

            const std::size_t readIdx = (m_haasWriteIdx + MaxHaasBuffer - m_haasDelaySamples) % MaxHaasBuffer;
            const float delayedSide = m_haasBuffer[readIdx];
            m_haasBuffer[m_haasWriteIdx] = finalSide;
            m_haasWriteIdx = (m_haasWriteIdx + 1) % MaxHaasBuffer;

            finalSide = 0.7f * finalSide + 0.3f * delayedSide;
            outL = finalMid + finalSide;
            outR = finalMid - finalSide;
        }

        left[i] = outL;
        right[i] = outR;

        sumLR += static_cast<double>(outL) * static_cast<double>(outR);
        sumL2 += static_cast<double>(outL) * static_cast<double>(outL);
        sumR2 += static_cast<double>(outR) * static_cast<double>(outR);
    }

    // Phase correlation radar ballistics
    const double denom = std::sqrt(sumL2 * sumR2);
    float instantCorr = 1.0f;
    if (denom > 1e-12) {
        instantCorr = static_cast<float>(std::clamp(sumLR / denom, -1.0, 1.0));
    }
    m_smoothedCorrelation = 0.9f * m_smoothedCorrelation + 0.1f * instantCorr;

    // Balance ballistics
    const double rmsL = std::sqrt(sumL2 / static_cast<double>(audio.frameCount));
    const double rmsR = std::sqrt(sumR2 / static_cast<double>(audio.frameCount));
    const double balDenom = rmsL + rmsR;
    float instantBal = 0.0f;
    if (balDenom > 1e-9) {
        instantBal = static_cast<float>((rmsR - rmsL) / balDenom);
    }
    m_smoothedBalance = 0.9f * m_smoothedBalance + 0.1f * instantBal;
}

} // namespace dawg::engine::dsp
