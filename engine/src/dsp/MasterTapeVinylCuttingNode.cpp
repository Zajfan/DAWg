#include <dawg/engine/dsp/MasterTapeVinylCuttingNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

MasterTapeVinylCuttingNode::MasterTapeVinylCuttingNode() noexcept
{
    updateFilters();
    reset();
}

void MasterTapeVinylCuttingNode::setEllipticalCrossover(EllipticalCrossoverFreq freq) noexcept
{
    m_eeqFreq = freq;
    switch (freq) {
    case EllipticalCrossoverFreq::hz70:  m_eeqCutoffHz = 70.0f; break;
    case EllipticalCrossoverFreq::hz150: m_eeqCutoffHz = 150.0f; break;
    case EllipticalCrossoverFreq::hz300: m_eeqCutoffHz = 300.0f; break;
    }
    updateFilters();
}

void MasterTapeVinylCuttingNode::updateFilters() noexcept
{
    const float w0 = 2.0f * std::numbers::pi_v<float> * m_eeqCutoffHz / static_cast<float>(m_sampleRate);
    const float cosW = std::cos(w0);
    const float alpha = std::sin(w0) / (2.0f * 0.70710678f); // Q = 1/sqrt(2)
    const float a0 = 1.0f + alpha;

    m_eeqB0 = (1.0f + cosW) * 0.5f / a0;
    m_eeqB1 = -(1.0f + cosW) / a0;
    m_eeqB2 = (1.0f + cosW) * 0.5f / a0;
    m_eeqA1 = (-2.0f * cosW) / a0;
    m_eeqA2 = (1.0f - alpha) / a0;
}

bool MasterTapeVinylCuttingNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    updateFilters();
    return true;
}

void MasterTapeVinylCuttingNode::reset() noexcept
{
    m_eeqZ1 = 0.0f;
    m_eeqZ2 = 0.0f;
    m_hfDeriPrevL = 0.0f;
    m_hfDeriPrevR = 0.0f;
    m_hfGainL = 1.0f;
    m_hfGainR = 1.0f;

    m_flangeRingL.fill(0.0f);
    m_flangeRingR.fill(0.0f);
    m_flangeWritePos = 0;
    m_flangeLfoPhase = 0.0f;

    m_preEmphasisL = 0.0f;
    m_preEmphasisR = 0.0f;
    m_deEmphasisL = 0.0f;
    m_deEmphasisR = 0.0f;
}

void MasterTapeVinylCuttingNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0) {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = numChannels > 1 ? audio.channels[1] : nullptr;

    const float flangePhaseInc = (2.0f * std::numbers::pi_v<float> * m_flangeRateHz) / static_cast<float>(m_sampleRate);
    const float hfAccelThresh = std::pow(10.0f, m_hfAccelThreshDb / 20.0f);

    constexpr double fixedPrimaryDelay = 64.0; // samples (~1.3 ms)

    for (std::size_t s = 0; s < numSamples; ++s) {
        const float inL = outL[s];
        const float inR = outR != nullptr ? outR[s] : inL;

        // Write input to flange delay ring
        m_flangeRingL[m_flangeWritePos] = inL;
        m_flangeRingR[m_flangeWritePos] = inR;

        // 1. Vinyl Cutting Lathe Simulation
        float vinylL = inL;
        float vinylR = inR;

        if (m_vinylLatheEnabled) {
            // A. Elliptical Equalizer (EEQ): 2nd-order Butterworth Highpass on Side channel
            const float mid = 0.5f * (inL + inR);
            const float side = 0.5f * (inL - inR);

            const float sideHp = m_eeqB0 * side + m_eeqZ1;
            m_eeqZ1 = m_eeqB1 * side - m_eeqA1 * sideHp + m_eeqZ2;
            m_eeqZ2 = m_eeqB2 * side - m_eeqA2 * sideHp;

            vinylL = mid + sideHp;
            vinylR = mid - sideHp;

            // B. High-Frequency Acceleration Limiter (stylus protection)
            const float accelL = std::abs(vinylL - 2.0f * m_hfDeriPrevL + m_preEmphasisL);
            const float accelR = std::abs(vinylR - 2.0f * m_hfDeriPrevR + m_preEmphasisR);
            m_preEmphasisL = m_hfDeriPrevL;
            m_preEmphasisR = m_hfDeriPrevR;
            m_hfDeriPrevL = vinylL;
            m_hfDeriPrevR = vinylR;

            if (accelL > hfAccelThresh) {
                m_hfGainL = std::min(m_hfGainL, hfAccelThresh / accelL);
            } else {
                m_hfGainL += 0.005f * (1.0f - m_hfGainL);
            }

            if (accelR > hfAccelThresh) {
                m_hfGainR = std::min(m_hfGainR, hfAccelThresh / accelR);
            } else {
                m_hfGainR += 0.005f * (1.0f - m_hfGainR);
            }

            vinylL = vinylL * m_hfGainL;
            vinylR = vinylR * m_hfGainR;
        }

        // 2. Analog Tape Flanging & Through-Zero Phasing (TZF)
        float flangeL = 0.0f;
        float flangeR = 0.0f;

        if (m_flangeEnabled) {
            auto readRing = [&](const std::array<float, maxDelayBuffer>& ring, double delay) -> float {
                const double readPos = static_cast<double>(m_flangeWritePos) - delay;
                double wrapped = std::fmod(readPos, static_cast<double>(maxDelayBuffer));
                if (wrapped < 0.0) wrapped += static_cast<double>(maxDelayBuffer);
                const auto idx0 = static_cast<std::size_t>(wrapped) % maxDelayBuffer;
                const auto idx1 = (idx0 + 1) % maxDelayBuffer;
                const float frac = static_cast<float>(wrapped - std::floor(wrapped));
                return ring[idx0] * (1.0f - frac) + ring[idx1] * frac;
            };

            // Through-Zero crossing: secondary delay sweeps symmetrically across primary delay
            const double lfoSin = std::sin(m_flangeLfoPhase);
            const double tzfOffset = (fixedPrimaryDelay * static_cast<double>(m_tzfDepth)) * lfoSin;
            const double dragOffset = static_cast<double>(m_tapeDrag) * 120.0;
            const double secondaryDelay = std::clamp(fixedPrimaryDelay + tzfOffset + dragOffset, 0.0, static_cast<double>(maxDelayBuffer - 2));

            const float primL = readRing(m_flangeRingL, fixedPrimaryDelay);
            const float primR = readRing(m_flangeRingR, fixedPrimaryDelay);
            const float secL = readRing(m_flangeRingL, secondaryDelay);
            const float secR = readRing(m_flangeRingR, secondaryDelay);

            // Destructive through-zero phase cancellation at zero delta delay
            flangeL = 0.5f * (primL - secL);
            flangeR = 0.5f * (primR - secR);

            m_flangeLfoPhase += flangePhaseInc;
            if (m_flangeLfoPhase >= 2.0f * std::numbers::pi_v<float>) {
                m_flangeLfoPhase -= 2.0f * std::numbers::pi_v<float>;
            }
        }

        // 3. Parallel Saturation Bus (Bias Air & Transformer)
        if (m_transformerSat > 0.001f || m_tapeBiasAir > 0.001f) {
            const float satStrength = m_transformerSat * 0.4f;
            vinylL = vinylL / (1.0f + satStrength * std::abs(vinylL));
            vinylR = vinylR / (1.0f + satStrength * std::abs(vinylR));
        }

        m_flangeWritePos = (m_flangeWritePos + 1) % maxDelayBuffer;

        // 4. Output summing
        outL[s] = inL * m_dryMix + vinylL * m_vinylMix + flangeL * m_flangeMix;
        if (outR != nullptr) {
            outR[s] = inR * m_dryMix + vinylR * m_vinylMix + flangeR * m_flangeMix;
        }
    }
}

} // namespace dawg::engine::dsp
