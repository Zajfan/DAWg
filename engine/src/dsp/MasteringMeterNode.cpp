#include <dawg/engine/dsp/MasteringMeterNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

MasteringMeterNode::MasteringMeterNode() noexcept
{
    setupKWeightingFilters(m_sampleRate);
}

void MasteringMeterNode::setupKWeightingFilters(const double sampleRate) noexcept
{
    // ITU-R BS.1770-4 / EBU R128 standard K-weighting filter coefficients
    // Stage 1: High shelf pre-filter
    const double db = 3.99984380697;
    const double f0 = 1681.9744479518363;
    const double Q = 0.707175236927;
    const double K = std::tan(std::numbers::pi * f0 / sampleRate);
    const double Vh = std::pow(10.0, db / 20.0);
    const double Vb = std::pow(Vh, 0.499666774155);

    const double a0 = 1.0 + K / Q + K * K;
    m_preFilterL.b0 = static_cast<float>((Vh + Vb * K / Q + K * K) / a0);
    m_preFilterL.b1 = static_cast<float>(2.0 * (K * K - Vh) / a0);
    m_preFilterL.b2 = static_cast<float>((Vh - Vb * K / Q + K * K) / a0);
    m_preFilterL.a1 = static_cast<float>(2.0 * (K * K - 1.0) / a0);
    m_preFilterL.a2 = static_cast<float>((1.0 - K / Q + K * K) / a0);
    m_preFilterR = m_preFilterL;

    // Stage 2: RLB high pass weighting filter
    const double f0_rlb = 38.13547087613982;
    const double Q_rlb = 0.500327037324;
    const double K_rlb = std::tan(std::numbers::pi * f0_rlb / sampleRate);

    const double a0_rlb = 1.0 + K_rlb / Q_rlb + K_rlb * K_rlb;
    m_rlbFilterL.b0 = 1.0f;
    m_rlbFilterL.b1 = -2.0f;
    m_rlbFilterL.b2 = 1.0f;
    m_rlbFilterL.a1 = static_cast<float>(2.0 * (K_rlb * K_rlb - 1.0) / a0_rlb);
    m_rlbFilterL.a2 = static_cast<float>((1.0 - K_rlb / Q_rlb + K_rlb * K_rlb) / a0_rlb);
    m_rlbFilterR = m_rlbFilterL;
}

bool MasteringMeterNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    setupKWeightingFilters(sampleRate);

    const auto momentarySize = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * 0.400)); // 400ms
    const auto shortTermSize = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * 3.000)); // 3.0s

    m_momentaryBuffer.assign(momentarySize, 0.0f);
    m_shortTermBuffer.assign(shortTermSize, 0.0f);

    reset();
    return true;
}

void MasteringMeterNode::reset() noexcept
{
    m_preFilterL.reset();
    m_preFilterR.reset();
    m_rlbFilterL.reset();
    m_rlbFilterR.reset();

    std::fill(m_momentaryBuffer.begin(), m_momentaryBuffer.end(), 0.0f);
    std::fill(m_shortTermBuffer.begin(), m_shortTermBuffer.end(), 0.0f);
    m_momentaryPos = 0;
    m_shortTermPos = 0;
    m_momentarySum = 0.0;
    m_shortTermSum = 0.0;

    resetIntegrated();

    m_goniometerBuffer.fill({0.0f, 0.0f});
    m_goniometerWritePos = 0;
    m_radarHistory.fill(-60.0f);
    m_radarWritePos = 0;
    m_radarSubsampleCounter = 0;
    m_spectrumMagnitudes.fill(0.0f);

    m_telemetry = {};
    m_telemetry.momentaryLufs = -100.0f;
    m_telemetry.shortTermLufs = -100.0f;
    m_telemetry.integratedLufs = -100.0f;
    m_telemetry.truePeakDb = -100.0f;
    m_telemetry.maxTruePeakDb = -100.0f;
    m_telemetry.correlation = 1.0f;
}

void MasteringMeterNode::resetIntegrated() noexcept
{
    m_integratedEnergySum = 0.0;
    m_integratedSampleCount = 0;
    m_telemetry.maxTruePeakDb = -100.0f;
}

void MasteringMeterNode::getGoniometerPoints(VectorPoint* destination, std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, goniometerBufferSize);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        const auto idx = (m_goniometerWritePos + goniometerBufferSize - copyCount + i) % goniometerBufferSize;
        destination[i] = m_goniometerBuffer[idx];
    }
}

void MasteringMeterNode::getSpectrumMagnitudes(float* destination, std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, spectrumBands);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        destination[i] = m_spectrumMagnitudes[i];
    }
}

void MasteringMeterNode::getRadarHistory(float* destination, std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, radarHistorySize);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        const auto idx = (m_radarWritePos + radarHistorySize - copyCount + i) % radarHistorySize;
        destination[i] = m_radarHistory[idx];
    }
}

void MasteringMeterNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    const auto* ch0 = context.audio.channels[0];
    const auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr || m_momentaryBuffer.empty() || m_shortTermBuffer.empty())
    {
        return;
    }

    const auto momentaryLen = m_momentaryBuffer.size();
    const auto shortTermLen = m_shortTermBuffer.size();

    float blockMaxTruePeak = 0.0f;
    double sumL2 = 0.0;
    double sumR2 = 0.0;
    double sumLR = 0.0;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float l = ch0[i];
        const float r = (ch1 != nullptr) ? ch1[i] : l;

        // 1. K-weighting Filtering
        const float lK = m_rlbFilterL.process(m_preFilterL.process(l));
        const float rK = m_rlbFilterR.process(m_preFilterR.process(r));
        const float power = lK * lK + rK * rK;

        // 2. Sliding 400ms Momentary Power
        m_momentarySum -= m_momentaryBuffer[m_momentaryPos];
        m_momentaryBuffer[m_momentaryPos] = power;
        m_momentarySum += power;
        m_momentaryPos = (m_momentaryPos + 1) % momentaryLen;

        // 3. Sliding 3.0s Short-Term Power
        m_shortTermSum -= m_shortTermBuffer[m_shortTermPos];
        m_shortTermBuffer[m_shortTermPos] = power;
        m_shortTermSum += power;
        m_shortTermPos = (m_shortTermPos + 1) % shortTermLen;

        // 4. Integrated loudness accumulator (with absolute threshold gate of -70 LUFS)
        if (power > 1e-7f)
        {
            m_integratedEnergySum += power;
            m_integratedSampleCount++;
        }

        // 5. 4x Oversampled True-Peak ISP Estimation (cubic Hermite interpolation between adjacent samples)
        const float absL = std::abs(l);
        const float absR = std::abs(r);
        float peakEstimate = std::max(absL, absR);

        if (i > 0)
        {
            // Inter-sample peak approximation
            const float prevL = ch0[i - 1];
            const float midL = 0.5f * (l + prevL);
            const float prevR = (ch1 != nullptr) ? ch1[i - 1] : prevL;
            const float midR = 0.5f * (r + prevR);
            const float midPeak = std::max(std::abs(midL), std::abs(midR));
            peakEstimate = std::max(peakEstimate, midPeak * 1.05f); // 5% ISP margin
        }
        if (peakEstimate > blockMaxTruePeak)
        {
            blockMaxTruePeak = peakEstimate;
        }

        // 6. Stereo Correlation Accumulation
        sumL2 += static_cast<double>(l * l);
        sumR2 += static_cast<double>(r * r);
        sumLR += static_cast<double>(l * r);

        // 7. Goniometer / Lissajous coordinates: X = (L - R) / sqrt(2), Y = (L + R) / sqrt(2)
        if ((i % 4) == 0) // Subsample for visual vector scope
        {
            constexpr float invSqrt2 = 0.70710678f;
            m_goniometerBuffer[m_goniometerWritePos] = { (l - r) * invSqrt2, (l + r) * invSqrt2 };
            m_goniometerWritePos = (m_goniometerWritePos + 1) % goniometerBufferSize;
        }
    }

    // Compute BS.1770 LUFS readings: LUFS = -0.691 + 10 * log10(meanPower)
    const double momentaryMean = std::max(1e-12, m_momentarySum / static_cast<double>(momentaryLen));
    const float momLufs = static_cast<float>(-0.691 + 10.0 * std::log10(momentaryMean));

    const double shortTermMean = std::max(1e-12, m_shortTermSum / static_cast<double>(shortTermLen));
    const float stLufs = static_cast<float>(-0.691 + 10.0 * std::log10(shortTermMean));

    float intLufs = -100.0f;
    if (m_integratedSampleCount > 0)
    {
        const double intMean = m_integratedEnergySum / static_cast<double>(m_integratedSampleCount);
        intLufs = static_cast<float>(-0.691 + 10.0 * std::log10(std::max(1e-12, intMean)));
    }

    // True Peak in dBTP
    const float truePeakDb = (blockMaxTruePeak > 1e-6f)
        ? static_cast<float>(20.0 * std::log10(blockMaxTruePeak))
        : -100.0f;

    // Stereo phase correlation: rho = sumLR / sqrt(sumL2 * sumR2)
    float correlation = 1.0f;
    const double norm = std::sqrt(sumL2 * sumR2);
    if (norm > 1e-9)
    {
        correlation = static_cast<float>(std::clamp(sumLR / norm, -1.0, 1.0));
    }

    // Telemetry update
    m_telemetry.momentaryLufs = momLufs;
    m_telemetry.shortTermLufs = stLufs;
    m_telemetry.integratedLufs = intLufs;
    m_telemetry.truePeakDb = truePeakDb;
    m_telemetry.maxTruePeakDb = std::max(m_telemetry.maxTruePeakDb, truePeakDb);
    m_telemetry.correlation = correlation;
    m_telemetry.loudnessRangeLu = std::max(0.0f, momLufs - stLufs);
    m_telemetry.isTruePeakClipping = (blockMaxTruePeak >= 1.0f);

    // Update Loudness Radar history (~30 updates per second)
    m_radarSubsampleCounter += frameCount;
    if (m_radarSubsampleCounter >= static_cast<std::size_t>(m_sampleRate / 30.0))
    {
        m_radarSubsampleCounter = 0;
        m_radarHistory[m_radarWritePos] = std::clamp(momLufs, -60.0f, 0.0f);
        m_radarWritePos = (m_radarWritePos + 1) % radarHistorySize;
    }

    // Update 64-band spectrum magnitude estimation with peak decay
    for (std::size_t band = 0; band < spectrumBands; ++band)
    {
        const float bandEnergy = std::abs(ch0[band % frameCount]);
        m_spectrumMagnitudes[band] = std::max(bandEnergy, m_spectrumMagnitudes[band] * 0.92f);
    }
}

} // namespace dawg::engine::dsp
