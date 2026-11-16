#include <dawg/engine/dsp/OpticalVariMuCompressorNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

OpticalVariMuCompressorNode::OpticalVariMuCompressorNode() noexcept
{
    updateSidechainCoeffs();
    setVariMuSpeed(10.0f, 100.0f);
}

bool OpticalVariMuCompressorNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    updateSidechainCoeffs();
    setVariMuSpeed(10.0f, 100.0f);
    return true;
}

void OpticalVariMuCompressorNode::reset() noexcept
{
    m_t4MemoryHistory = 0.0f;
    m_t4FastCell = 0.0f;
    m_t4SlowCell = 0.0f;
    m_variMuGridBias = 0.0f;
    m_scZ1L = m_scZ2L = 0.0f;
    m_scZ1R = m_scZ2R = 0.0f;
    m_measuredGainReductionDb = 0.0f;
}

void OpticalVariMuCompressorNode::setVariMuSpeed(float attackMs, float releaseMs) noexcept
{
    const float att = std::max(0.1f, attackMs);
    const float rel = std::max(1.0f, releaseMs);
    const float fs = static_cast<float>(m_sampleRate);
    m_variMuAttackCoeff = 1.0f - std::exp(-1.0f / (0.001f * att * fs));
    m_variMuReleaseCoeff = 1.0f - std::exp(-1.0f / (0.001f * rel * fs));
}

void OpticalVariMuCompressorNode::updateSidechainCoeffs() noexcept
{
    const float fs = static_cast<float>(m_sampleRate);
    if (m_sidechainMode == SidechainFilterMode::flat)
    {
        m_scB0 = 1.0f;
        m_scB1 = 0.0f;
        m_scB2 = 0.0f;
        m_scA1 = 0.0f;
        m_scA2 = 0.0f;
        return;
    }

    if (m_sidechainMode == SidechainFilterMode::highpass80 || m_sidechainMode == SidechainFilterMode::highpass150)
    {
        const float fc = (m_sidechainMode == SidechainFilterMode::highpass80) ? 80.0f : 150.0f;
        const float w0 = 2.0f * std::numbers::pi_v<float> * (fc / fs);
        const float cosW = std::cos(w0);
        const float sinW = std::sin(w0);
        const float alpha = sinW / (2.0f * 0.7071f);
        const float a0 = 1.0f + alpha;

        m_scB0 = (1.0f + cosW) * 0.5f / a0;
        m_scB1 = -(1.0f + cosW) / a0;
        m_scB2 = (1.0f + cosW) * 0.5f / a0;
        m_scA1 = (-2.0f * cosW) / a0;
        m_scA2 = (1.0f - alpha) / a0;
        return;
    }

    // Thrust mode: 6 dB/oct highpass low-shelf tilt up at 200 Hz
    const float fc = 200.0f;
    const float w0 = 2.0f * std::numbers::pi_v<float> * (fc / fs);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW * 0.5f;
    const float a0 = 1.0f + alpha;

    m_scB0 = (1.0f + cosW) * 0.5f / a0;
    m_scB1 = -(1.0f + cosW) / a0;
    m_scB2 = (1.0f + cosW) * 0.5f / a0;
    m_scA1 = (-2.0f * cosW) / a0;
    m_scA2 = (1.0f - alpha) / a0;
}

float OpticalVariMuCompressorNode::processSidechainFilter(float sample, float& z1, float& z2) noexcept
{
    const float out = m_scB0 * sample + z1;
    z1 = m_scB1 * sample - m_scA1 * out + z2;
    z2 = m_scB2 * sample - m_scA2 * out;
    return out;
}

void OpticalVariMuCompressorNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    const float fs = static_cast<float>(m_sampleRate);
    const float outputGainLin = std::pow(10.0f, m_outputGainDb * 0.05f);

    // T4 time constants
    const float t4AttackCoeff = 1.0f - std::exp(-1.0f / (0.010f * fs)); // ~10 ms attack
    const float t4RelFastCoeff = 1.0f - std::exp(-1.0f / (0.060f * fs)); // ~60 ms initial 50% release
    const float t4MemoryDecay = 1.0f - std::exp(-1.0f / (3.0f * fs));

    float maxBlockGRDb = 0.0f;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float dryL = outL[i];
        const float dryR = (outR != nullptr) ? outR[i] : dryL;

        // 1. Sidechain filtering
        const float scFilteredL = processSidechainFilter(dryL, m_scZ1L, m_scZ2L);
        const float scFilteredR = processSidechainFilter(dryR, m_scZ1R, m_scZ2R);

        // Peak envelope detector
        const float peakEnv = std::max(std::abs(scFilteredL), std::abs(scFilteredR));
        const float peakDb = 20.0f * std::log10(std::max(peakEnv, 1.0e-5f));

        float gainReductionDb = 0.0f;

        if (m_model == CompressorModel::opticalT4)
        {
            // LA-2A T4 cell behavior:
            // Peak reduction slider sets threshold from 0 dBFS down to -40 dBFS
            const float thresholdDb = -40.0f * m_peakReduction;
            const float overDb = std::max(0.0f, peakDb - thresholdDb);

            // Luminescent panel excitation
            if (overDb > m_t4FastCell)
            {
                m_t4FastCell += t4AttackCoeff * (overDb - m_t4FastCell);
                // Memory buildup when heavily compressed
                m_t4MemoryHistory = std::clamp(m_t4MemoryHistory + (overDb * 0.0005f), 0.0f, 1.0f);
            }
            else
            {
                m_t4FastCell += t4RelFastCoeff * (overDb - m_t4FastCell);
                m_t4MemoryHistory *= (1.0f - t4MemoryDecay);
            }

            // Secondary slow release tail (0.5s to 3.5s depending on memory)
            const float slowTimeSec = 0.5f + 3.0f * m_t4MemoryHistory;
            const float t4RelSlowCoeff = 1.0f - std::exp(-1.0f / (slowTimeSec * fs));
            if (m_t4FastCell > m_t4SlowCell)
            {
                m_t4SlowCell += t4AttackCoeff * (m_t4FastCell - m_t4SlowCell);
            }
            else
            {
                m_t4SlowCell += t4RelSlowCoeff * (m_t4FastCell - m_t4SlowCell);
            }

            // Dual-decay release combination: 50% fast, 50% slow
            gainReductionDb = 0.5f * m_t4FastCell + 0.5f * m_t4SlowCell;
        }
        else // CompressorModel::variMu
        {
            // Fairchild 670 Variable-Mu behavior:
            // Dynamic ratio that increases smoothly as input level rises above threshold
            const float thresholdDb = -32.0f * m_peakReduction;
            const float overDb = std::max(0.0f, peakDb - thresholdDb);

            // Program-dependent ratio: 1.5:1 at threshold, scaling continuously up to 20:1
            const float dynamicRatio = 1.5f + 18.5f * (overDb / (overDb + 8.0f));
            const float targetGrDb = (overDb > 0.0f) ? (overDb * (1.0f - 1.0f / dynamicRatio)) : 0.0f;

            if (targetGrDb > m_variMuGridBias)
            {
                m_variMuGridBias += m_variMuAttackCoeff * (targetGrDb - m_variMuGridBias);
            }
            else
            {
                m_variMuGridBias += m_variMuReleaseCoeff * (targetGrDb - m_variMuGridBias);
            }

            gainReductionDb = m_variMuGridBias;
        }

        maxBlockGRDb = std::max(maxBlockGRDb, gainReductionDb);

        // Apply gain reduction
        const float linearGR = std::pow(10.0f, -gainReductionDb * 0.05f);
        float procL = dryL * linearGR;
        float procR = dryR * linearGR;

        // Tube / Transformer harmonic warmth
        if (m_tubeWarmth > 0.001f)
        {
            const float drive = 1.0f + m_tubeWarmth * 0.8f;
            // Remote-cutoff triode asymmetrical soft saturation
            auto tubeTransfer = [drive, this](float x) noexcept -> float {
                const float scaled = x * drive;
                // Subtle 2nd harmonic asymmetry + hyperbolic tangent limiting
                const float asym = scaled + 0.15f * m_tubeWarmth * (scaled * std::abs(scaled));
                return std::tanh(asym) / drive;
            };

            procL = tubeTransfer(procL);
            procR = tubeTransfer(procR);
        }

        // Output make-up gain
        procL *= outputGainLin;
        procR *= outputGainLin;

        // Dry/Wet blend
        outL[i] = m_mix * procL + (1.0f - m_mix) * dryL;
        if (outR != nullptr)
        {
            outR[i] = m_mix * procR + (1.0f - m_mix) * dryR;
        }
    }

    m_measuredGainReductionDb = maxBlockGRDb;
}

} // namespace dawg::engine::dsp
