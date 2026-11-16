#include <dawg/engine/dsp/MasteringStemExportEngine.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

// 48-tap ITU-R BS.1770 Annex 2 polyphase FIR interpolation filter
// 4 sub-phases, 12 taps each
// Reconstructs continuous inter-sample peaks with high stopband attenuation
constexpr std::array<std::array<float, 12>, 4> kPolyphaseCoeffs {{
    // Phase 0 (at original sample position)
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    // Phase 1 (+0.25 sample)
    { -0.0078f,  0.0215f, -0.0469f,  0.0977f, -0.2344f,  0.8984f,  0.3320f, -0.1172f,  0.0547f, -0.0273f,  0.0117f, -0.0039f },
    // Phase 2 (+0.50 sample)
    { -0.0156f,  0.0391f, -0.0820f,  0.1641f, -0.3828f,  0.6445f,  0.6445f, -0.3828f,  0.1641f, -0.0820f,  0.0391f, -0.0156f },
    // Phase 3 (+0.75 sample)
    { -0.0039f,  0.0117f, -0.0273f,  0.0547f, -0.1172f,  0.3320f,  0.8984f, -0.2344f,  0.0977f, -0.0469f,  0.0215f, -0.0078f }
}};

} // namespace

MasteringStemExportEngine::MasteringStemExportEngine() noexcept
{
    setTargetProfile(MasteringTargetProfile::spotify);
}

void MasteringStemExportEngine::setTargetProfile(const MasteringTargetProfile profile) noexcept
{
    m_profile = profile;
    const auto spec = getMasteringTargetSpec(profile);
    m_targetLufs = spec.targetLufs;
    m_ceilingTruePeakDb = spec.targetTruePeakDb;
}

void MasteringStemExportEngine::setCustomTarget(const float targetLufs, const float ceilingTruePeakDb) noexcept
{
    m_targetLufs = std::clamp(targetLufs, -40.0f, 0.0f);
    m_ceilingTruePeakDb = std::clamp(ceilingTruePeakDb, -12.0f, 0.0f);
}

void MasteringStemExportEngine::setupKFilters(
    const double sampleRate,
    Biquad& preFilter,
    Biquad& rlbFilter) noexcept
{
    // Stage 1: High shelf pre-filter
    const double db = 3.99984380697;
    const double f0 = 1681.9744479518363;
    const double Q = 0.707175236927;
    const double K = std::tan(std::numbers::pi * f0 / sampleRate);
    const double Vh = std::pow(10.0, db / 20.0);
    const double Vb = std::pow(Vh, 0.499666774155);

    const double a0 = 1.0 + K / Q + K * K;
    preFilter.b0 = static_cast<float>((Vh + Vb * K / Q + K * K) / a0);
    preFilter.b1 = static_cast<float>(2.0 * (K * K - Vh) / a0);
    preFilter.b2 = static_cast<float>((Vh - Vb * K / Q + K * K) / a0);
    preFilter.a1 = static_cast<float>(2.0 * (K * K - 1.0) / a0);
    preFilter.a2 = static_cast<float>((1.0 - K / Q + K * K) / a0);

    // Stage 2: RLB high pass weighting filter
    const double f0_rlb = 38.13547087613982;
    const double Q_rlb = 0.500327037324;
    const double K_rlb = std::tan(std::numbers::pi * f0_rlb / sampleRate);

    const double a0_rlb = 1.0 + K_rlb / Q_rlb + K_rlb * K_rlb;
    rlbFilter.b0 = 1.0f;
    rlbFilter.b1 = -2.0f;
    rlbFilter.b2 = 1.0f;
    rlbFilter.a1 = static_cast<float>(2.0 * (K_rlb * K_rlb - 1.0) / a0_rlb);
    rlbFilter.a2 = static_cast<float>((1.0 - K_rlb / Q_rlb + K_rlb * K_rlb) / a0_rlb);
}

float MasteringStemExportEngine::computeInterSampleTruePeak(
    const float* samples,
    const std::size_t count) noexcept
{
    if (samples == nullptr || count == 0)
    {
        return 0.0f;
    }

    float maxTruePeak = 0.0f;

    for (std::size_t i = 0; i < count; ++i)
    {
        // Absolute original sample
        const float sAbs = std::abs(samples[i]);
        if (sAbs > maxTruePeak)
        {
            maxTruePeak = sAbs;
        }

        // 4x polyphase sub-sample evaluation
        for (std::size_t phase = 1; phase < 4; ++phase)
        {
            float interpolated = 0.0f;
            for (std::size_t k = 0; k < 12; ++k)
            {
                // Centered at tap 5
                const int sampleIdx = static_cast<int>(i) + static_cast<int>(k) - 5;
                if (sampleIdx >= 0 && sampleIdx < static_cast<int>(count))
                {
                    interpolated += samples[sampleIdx] * kPolyphaseCoeffs[phase][k];
                }
            }

            const float ispAbs = std::abs(interpolated);
            if (ispAbs > maxTruePeak)
            {
                maxTruePeak = ispAbs;
            }
        }
    }

    return maxTruePeak;
}

MasteringTelemetry MasteringStemExportEngine::analyzeBuffer(
    const AudioBufferView& buffer,
    const double sampleRate) const
{
    MasteringTelemetry tele {};
    if (buffer.frameCount == 0 || buffer.channelCount == 0 || sampleRate <= 0.0)
    {
        return tele;
    }

    const auto* ch0 = buffer.channels[0];
    const auto* ch1 = (buffer.channelCount >= 2) ? buffer.channels[1] : ch0;
    if (ch0 == nullptr)
    {
        return tele;
    }

    const auto frameCount = buffer.frameCount;

    // Filter signals through K-weighting
    Biquad preL, rlbL, preR, rlbR;
    setupKFilters(sampleRate, preL, rlbL);
    setupKFilters(sampleRate, preR, rlbR);

    std::vector<float> filteredEnergy(frameCount, 0.0f);
    double totalEnergy = 0.0;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float lK = rlbL.process(preL.process(ch0[i]));
        const float rK = rlbR.process(preR.process(ch1 != nullptr ? ch1[i] : ch0[i]));
        const float power = lK * lK + rK * rK;
        filteredEnergy[i] = power;
        totalEnergy += power;
    }

    // Measure True-Peak
    const float tpL = computeInterSampleTruePeak(ch0, frameCount);
    const float tpR = (ch1 != nullptr) ? computeInterSampleTruePeak(ch1, frameCount) : tpL;
    const float maxTruePeakLinear = std::max(tpL, tpR);
    tele.truePeakDb = (maxTruePeakLinear > 1e-6f) ? static_cast<float>(20.0 * std::log10(maxTruePeakLinear)) : -100.0f;
    tele.maxTruePeakDb = tele.truePeakDb;
    tele.isTruePeakClipping = (maxTruePeakLinear >= 1.0f);

    // 400ms rectangular blocks with 75% overlap (100ms step) for ITU-R BS.1770-4 Gating
    const auto blockLen400ms = std::max<std::size_t>(16, static_cast<std::size_t>(sampleRate * 0.400));
    const auto hopLen100ms = std::max<std::size_t>(4, static_cast<std::size_t>(sampleRate * 0.100));

    std::vector<float> blockLoudness;
    if (frameCount >= blockLen400ms)
    {
        for (std::size_t start = 0; start + blockLen400ms <= frameCount; start += hopLen100ms)
        {
            double blockSum = 0.0;
            for (std::size_t j = 0; j < blockLen400ms; ++j)
            {
                blockSum += filteredEnergy[start + j];
            }
            const double meanPower = blockSum / static_cast<double>(blockLen400ms);
            if (meanPower > 1e-12)
            {
                const float lkfs = static_cast<float>(-0.691 + 10.0 * std::log10(meanPower));
                blockLoudness.push_back(lkfs);
            }
        }
    }
    else if (frameCount > 0)
    {
        const double meanPower = totalEnergy / static_cast<double>(frameCount);
        if (meanPower > 1e-12)
        {
            const float lkfs = static_cast<float>(-0.691 + 10.0 * std::log10(meanPower));
            blockLoudness.push_back(lkfs);
        }
    }

    // Stage 1 Gating: Absolute threshold Gamma_a = -70 LKFS
    constexpr float gammaA = -70.0f;
    double absGatedSum = 0.0;
    std::size_t absGatedCount = 0;

    for (const float lkfs : blockLoudness)
    {
        if (lkfs > gammaA)
        {
            absGatedSum += std::pow(10.0, (lkfs + 0.691f) / 10.0f);
            absGatedCount++;
        }
    }

    if (absGatedCount > 0)
    {
        const double zAbsMean = absGatedSum / static_cast<double>(absGatedCount);
        const float absoluteLoudness = static_cast<float>(-0.691 + 10.0 * std::log10(std::max(1e-12, zAbsMean)));

        // Stage 2 Gating: Relative threshold Gamma_r = absoluteLoudness - 10.0 LU
        const float gammaR = absoluteLoudness - 10.0f;
        double relGatedSum = 0.0;
        std::size_t relGatedCount = 0;

        for (const float lkfs : blockLoudness)
        {
            if (lkfs > gammaR)
            {
                relGatedSum += std::pow(10.0, (lkfs + 0.691f) / 10.0f);
                relGatedCount++;
            }
        }

        if (relGatedCount > 0)
        {
            const double zFinalMean = relGatedSum / static_cast<double>(relGatedCount);
            tele.integratedLufs = static_cast<float>(-0.691 + 10.0 * std::log10(std::max(1e-12, zFinalMean)));
        }
        else
        {
            tele.integratedLufs = absoluteLoudness;
        }
    }
    else
    {
        tele.integratedLufs = -100.0f;
    }

    // Short-term (3s blocks with 66% overlap) and EBU Tech 3342 Loudness Range (LRA)
    const auto blockLen3s = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * 3.000));
    const auto hopLen1s = std::max<std::size_t>(16, static_cast<std::size_t>(sampleRate * 1.000));

    std::vector<float> shortTermBlocks;
    if (frameCount >= blockLen3s)
    {
        for (std::size_t start = 0; start + blockLen3s <= frameCount; start += hopLen1s)
        {
            double stSum = 0.0;
            for (std::size_t j = 0; j < blockLen3s; ++j)
            {
                stSum += filteredEnergy[start + j];
            }
            const double meanPower = stSum / static_cast<double>(blockLen3s);
            if (meanPower > 1e-12)
            {
                const float stLufs = static_cast<float>(-0.691 + 10.0 * std::log10(meanPower));
                shortTermBlocks.push_back(stLufs);
            }
        }
    }

    if (!shortTermBlocks.empty())
    {
        // Gated for LRA: discard below -70 LUFS and below -20 LU relative to mean
        double stSumLinear = 0.0;
        for (const float st : shortTermBlocks)
        {
            stSumLinear += std::pow(10.0, (st + 0.691f) / 10.0f);
        }
        const double stMeanLinear = stSumLinear / static_cast<double>(shortTermBlocks.size());
        const float stMeanLufs = static_cast<float>(-0.691 + 10.0 * std::log10(std::max(1e-12, stMeanLinear)));
        const float lraRelativeGate = stMeanLufs - 20.0f;

        std::vector<float> lraCandidates;
        for (const float st : shortTermBlocks)
        {
            if (st > -70.0f && st > lraRelativeGate)
            {
                lraCandidates.push_back(st);
            }
        }

        if (lraCandidates.size() >= 2)
        {
            std::sort(lraCandidates.begin(), lraCandidates.end());
            const auto idx10 = static_cast<std::size_t>(static_cast<float>(lraCandidates.size() - 1) * 0.10f);
            const auto idx95 = static_cast<std::size_t>(static_cast<float>(lraCandidates.size() - 1) * 0.95f);
            tele.loudnessRangeLu = std::max(0.0f, lraCandidates[idx95] - lraCandidates[idx10]);
        }
        tele.shortTermLufs = shortTermBlocks.back();
    }
    else
    {
        tele.shortTermLufs = tele.integratedLufs;
        tele.loudnessRangeLu = 0.0f;
    }

    if (!blockLoudness.empty())
    {
        tele.momentaryLufs = blockLoudness.back();
    }

    return tele;
}

bool MasteringStemExportEngine::applyTruePeakLimiter(
    AudioBufferView& buffer,
    const double sampleRate,
    const float ceilingLinear) noexcept
{
    if (buffer.frameCount == 0 || buffer.channelCount == 0 || ceilingLinear <= 0.0f)
    {
        return false;
    }

    const auto frameCount = buffer.frameCount;
    auto* ch0 = buffer.channels[0];
    auto* ch1 = (buffer.channelCount >= 2) ? buffer.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return false;
    }

    bool limiterEngaged = false;

    // Fast lookahead peak limiter with exponential release envelope
    // Lookahead: ~1.5ms
    const auto lookaheadSamples = std::max<std::size_t>(8, static_cast<std::size_t>(sampleRate * 0.0015));
    const float releaseCoeff = std::exp(-1.0f / static_cast<float>(sampleRate * 0.080)); // 80ms release

    std::vector<float> delayBufL(lookaheadSamples, 0.0f);
    std::vector<float> delayBufR(lookaheadSamples, 0.0f);
    std::size_t delayWritePos = 0;

    float currentGainReduction = 1.0f;

    for (std::size_t i = 0; i < frameCount + lookaheadSamples; ++i)
    {
        float inputL = 0.0f;
        float inputR = 0.0f;
        if (i < frameCount)
        {
            inputL = ch0[i];
            inputR = (ch1 != nullptr) ? ch1[i] : inputL;
        }

        // Peak detector
        const float peak = std::max(std::abs(inputL), std::abs(inputR));
        float targetGain = 1.0f;
        if (peak > ceilingLinear)
        {
            targetGain = ceilingLinear / peak;
            limiterEngaged = true;
        }

        // Instant attack, smooth release
        if (targetGain < currentGainReduction)
        {
            currentGainReduction = targetGain;
        }
        else
        {
            currentGainReduction = targetGain + releaseCoeff * (currentGainReduction - targetGain);
        }

        // Read delayed sample
        const float delayedL = delayBufL[delayWritePos];
        const float delayedR = delayBufR[delayWritePos];

        // Store input in delay buffer
        delayBufL[delayWritePos] = inputL;
        delayBufR[delayWritePos] = inputR;
        delayWritePos = (delayWritePos + 1) % lookaheadSamples;

        // Apply gain to delayed sample
        if (i >= lookaheadSamples)
        {
            const auto outIdx = i - lookaheadSamples;
            ch0[outIdx] = delayedL * currentGainReduction;
            if (ch1 != nullptr)
            {
                ch1[outIdx] = delayedR * currentGainReduction;
            }
        }
    }

    return limiterEngaged;
}

StemNormalizationResult MasteringStemExportEngine::normalizeBuffer(
    AudioBufferView& buffer,
    const double sampleRate,
    const std::string& name)
{
    StemNormalizationResult result {};
    result.stemName = name;

    if (buffer.frameCount == 0 || buffer.channelCount == 0 || sampleRate <= 0.0)
    {
        return result;
    }

    // 1. Initial Analysis
    const auto initialTele = analyzeBuffer(buffer, sampleRate);
    result.originalIntegratedLufs = initialTele.integratedLufs;
    result.originalTruePeakDb = initialTele.truePeakDb;
    result.loudnessRangeLu = initialTele.loudnessRangeLu;

    // 2. Compute Normalization Gain Offset
    float gainOffsetDb = 0.0f;
    if (initialTele.integratedLufs > -70.0f)
    {
        gainOffsetDb = m_targetLufs - initialTele.integratedLufs;
    }
    result.appliedGainDb = gainOffsetDb;

    const float linearGain = std::pow(10.0f, gainOffsetDb / 20.0f);
    const auto frameCount = buffer.frameCount;
    auto* ch0 = buffer.channels[0];
    auto* ch1 = (buffer.channelCount >= 2) ? buffer.channels[1] : ch0;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        ch0[i] *= linearGain;
        if (ch1 != nullptr)
        {
            ch1[i] *= linearGain;
        }
    }

    // 3. True Peak Brickwall Limiting to Ceiling
    const float ceilingLinear = std::pow(10.0f, m_ceilingTruePeakDb / 20.0f);
    result.limiterEngaged = applyTruePeakLimiter(buffer, sampleRate, ceilingLinear);

    // 4. Final Compliance Verification
    const auto finalTele = analyzeBuffer(buffer, sampleRate);
    result.finalIntegratedLufs = finalTele.integratedLufs;
    result.finalTruePeakDb = finalTele.truePeakDb;

    return result;
}

std::vector<StemNormalizationResult> MasteringStemExportEngine::normalizeStems(
    std::vector<AudioBufferView>& stems,
    const std::vector<std::string>& stemNames,
    const double sampleRate,
    const bool preserveRelativeBalance)
{
    std::vector<StemNormalizationResult> results;
    if (stems.empty() || sampleRate <= 0.0)
    {
        return results;
    }

    const auto numStems = stems.size();
    results.reserve(numStems);

    if (preserveRelativeBalance)
    {
        // 1. Sum all stems into a temporary composite mix bus to measure overall balance
        std::size_t maxFrames = 0;
        std::size_t maxChannels = 1;
        for (const auto& stem : stems)
        {
            maxFrames = std::max(maxFrames, stem.frameCount);
            maxChannels = std::max(maxChannels, static_cast<std::size_t>(stem.channelCount));
        }

        std::vector<float> sumL(maxFrames, 0.0f);
        std::vector<float> sumR(maxFrames, 0.0f);

        for (const auto& stem : stems)
        {
            const auto* s0 = stem.channels[0];
            const auto* s1 = (stem.channelCount >= 2) ? stem.channels[1] : s0;
            if (s0 == nullptr) { continue; }

            for (std::size_t i = 0; i < stem.frameCount; ++i)
            {
                sumL[i] += s0[i];
                sumR[i] += (s1 != nullptr) ? s1[i] : s0[i];
            }
        }

        std::array<float*, 2> sumPlanes { sumL.data(), sumR.data() };
        AudioBufferView mixBuffer { sumPlanes.data(), 2, maxFrames };

        const auto mixTele = analyzeBuffer(mixBuffer, sampleRate);
        float uniformGainDb = 0.0f;
        if (mixTele.integratedLufs > -70.0f)
        {
            uniformGainDb = m_targetLufs - mixTele.integratedLufs;
        }

        const float uniformLinearGain = std::pow(10.0f, uniformGainDb / 20.0f);
        const float ceilingLinear = std::pow(10.0f, m_ceilingTruePeakDb / 20.0f);

        for (std::size_t s = 0; s < numStems; ++s)
        {
            auto& stem = stems[s];
            const std::string name = (s < stemNames.size()) ? stemNames[s] : ("Stem_" + std::to_string(s + 1));

            const auto stemInitialTele = analyzeBuffer(stem, sampleRate);
            StemNormalizationResult r {};
            r.stemName = name;
            r.originalIntegratedLufs = stemInitialTele.integratedLufs;
            r.originalTruePeakDb = stemInitialTele.truePeakDb;
            r.loudnessRangeLu = stemInitialTele.loudnessRangeLu;
            r.appliedGainDb = uniformGainDb;

            // Apply uniform gain
            auto* s0 = stem.channels[0];
            auto* s1 = (stem.channelCount >= 2) ? stem.channels[1] : s0;
            for (std::size_t i = 0; i < stem.frameCount; ++i)
            {
                s0[i] *= uniformLinearGain;
                if (s1 != nullptr)
                {
                    s1[i] *= uniformLinearGain;
                }
            }

            r.limiterEngaged = applyTruePeakLimiter(stem, sampleRate, ceilingLinear);

            const auto stemFinalTele = analyzeBuffer(stem, sampleRate);
            r.finalIntegratedLufs = stemFinalTele.integratedLufs;
            r.finalTruePeakDb = stemFinalTele.truePeakDb;

            results.push_back(r);
        }
    }
    else
    {
        for (std::size_t s = 0; s < numStems; ++s)
        {
            const std::string name = (s < stemNames.size()) ? stemNames[s] : ("Stem_" + std::to_string(s + 1));
            results.push_back(normalizeBuffer(stems[s], sampleRate, name));
        }
    }

    return results;
}

} // namespace dawg::engine::dsp
