#include <dawg/engine/dsp/TransientSlicerEngine.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dawg::engine::dsp {

std::size_t TransientSlicerEngine::findNearestZeroCrossing(
    const float* audio,
    std::size_t sampleCount,
    std::size_t targetIndex,
    std::size_t searchRadius) noexcept
{
    if (audio == nullptr || sampleCount == 0)
        return 0;

    const std::size_t start = (targetIndex > searchRadius) ? targetIndex - searchRadius : 0;
    const std::size_t end = std::min(targetIndex + searchRadius, sampleCount - 1);

    std::size_t bestIdx = targetIndex;
    float minAbs = std::abs(audio[targetIndex]);

    for (std::size_t i = start; i < end; ++i)
    {
        // Direct zero crossing check: sign flip
        if ((audio[i] <= 0.0f && audio[i + 1] >= 0.0f) || (audio[i] >= 0.0f && audio[i + 1] <= 0.0f))
        {
            return (std::abs(audio[i]) < std::abs(audio[i + 1])) ? i : i + 1;
        }

        if (std::abs(audio[i]) < minAbs)
        {
            minAbs = std::abs(audio[i]);
            bestIdx = i;
        }
    }

    return bestIdx;
}

std::vector<SlicedBeatSegment> TransientSlicerEngine::detectSlices(
    const float* audioData,
    std::size_t sampleCount,
    double sampleRate,
    std::size_t maxSlices,
    float sensitivity)
{
    std::vector<SlicedBeatSegment> slices;
    if (audioData == nullptr || sampleCount < 1000)
        return slices;

    const std::size_t windowSize = 512;
    const std::size_t numWindows = sampleCount / windowSize;
    if (numWindows < 4)
        return slices;

    std::vector<float> energy(numWindows, 0.0f);
    for (std::size_t w = 0; w < numWindows; ++w)
    {
        float sum = 0.0f;
        for (std::size_t i = 0; i < windowSize; ++i)
        {
            const float s = audioData[w * windowSize + i];
            sum += s * s;
        }
        energy[w] = std::sqrt(sum / static_cast<float>(windowSize));
    }

    std::vector<std::size_t> onsets;
    onsets.push_back(0);

    const auto minDistanceSamples = static_cast<std::size_t>(sampleRate * 0.05); // 50ms min slice
    float runningMean = energy[0];

    for (std::size_t w = 1; w + 1 < numWindows; ++w)
    {
        const float diff = energy[w] - energy[w - 1];
        runningMean = 0.9f * runningMean + 0.1f * energy[w];

        if (diff > (runningMean * (sensitivity * 0.3f)) && energy[w] > energy[w + 1])
        {
            const std::size_t rawPos = w * windowSize;
            if (rawPos >= onsets.back() + minDistanceSamples)
            {
                const std::size_t zeroCrossingPos = findNearestZeroCrossing(audioData, sampleCount, rawPos);
                onsets.push_back(zeroCrossingPos);
                if (onsets.size() >= maxSlices)
                    break;
            }
        }
    }

    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        SlicedBeatSegment seg;
        seg.startSample = onsets[i];
        seg.endSample = (i + 1 < onsets.size()) ? onsets[i + 1] : sampleCount;

        float peak = 0.0f;
        for (std::size_t s = seg.startSample; s < seg.endSample; ++s)
            peak = std::max(peak, std::abs(audioData[s]));

        seg.peakAmplitude = peak;
        std::ostringstream ss;
        ss << "Slice " << std::setfill('0') << std::setw(2) << (i + 1);
        seg.label = ss.str();
        slices.push_back(std::move(seg));
    }

    return slices;
}

std::size_t TransientSlicerEngine::sliceToDrumPadSampler(
    const float* audioL,
    const float* audioR,
    std::size_t sampleCount,
    double sampleRate,
    DrumPadSamplerNode& drumPadSampler,
    bool reverse,
    bool granularScrub)
{
    if (audioL == nullptr || sampleCount < 1000)
        return 0;

    const auto slices = detectSlices(audioL, sampleCount, sampleRate, DrumPadSamplerNode::kNumPads);
    if (slices.empty())
        return 0;

    for (std::size_t p = 0; p < std::min(slices.size(), DrumPadSamplerNode::kNumPads); ++p)
    {
        const auto& seg = slices[p];
        const std::size_t sliceLen = seg.endSample - seg.startSample;
        std::vector<float> sliceL(sliceLen);
        std::vector<float> sliceR(sliceLen);

        std::copy_n(audioL + seg.startSample, sliceLen, sliceL.data());
        if (audioR != nullptr)
            std::copy_n(audioR + seg.startSample, sliceLen, sliceR.data());
        else
            sliceR = sliceL;

        drumPadSampler.loadSampleIntoPad(p, std::move(sliceL), std::move(sliceR), seg.label);

        auto& pad = drumPadSampler.pad(p);
        pad.reverse = reverse;
        pad.granularScrub = granularScrub;
        pad.grainSizeMs = 45.0f;
        pad.attackSec = 0.002f;
        pad.decaySec = 0.25f;
        pad.releaseSec = 0.15f;
    }

    return std::min(slices.size(), DrumPadSamplerNode::kNumPads);
}

std::size_t TransientSlicerEngine::sliceToMultiSampleInstrument(
    const float* audioL,
    const float* audioR,
    std::size_t sampleCount,
    double sampleRate,
    MultiSampleInstrumentNode& instrument,
    std::uint8_t startMidiNote)
{
    if (audioL == nullptr || sampleCount < 1000)
        return 0;

    const auto slices = detectSlices(audioL, sampleCount, sampleRate, 32);
    if (slices.empty())
        return 0;

    instrument.clearRegions();

    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        const auto note = static_cast<std::uint8_t>(std::min(127, startMidiNote + static_cast<int>(i)));
        const auto& seg = slices[i];
        const std::size_t sliceLen = seg.endSample - seg.startSample;

        MultiSampleRegion reg;
        reg.rootNote = note;
        reg.lowNote = note;
        reg.highNote = note;
        reg.lowVel = 0;
        reg.highVel = 127;
        reg.articulationName = seg.label;
        reg.sampleDataL.resize(sliceLen);
        reg.sampleDataR.resize(sliceLen);

        std::copy_n(audioL + seg.startSample, sliceLen, reg.sampleDataL.data());
        if (audioR != nullptr)
            std::copy_n(audioR + seg.startSample, sliceLen, reg.sampleDataR.data());
        else
            reg.sampleDataR = reg.sampleDataL;

        instrument.addRegion(std::move(reg));
    }

    return slices.size();
}

ReusableGrooveTemplate TransientSlicerEngine::extractGrooveTemplate(
    const float* audioData,
    std::size_t sampleCount,
    double sampleRate,
    double bpm)
{
    ReusableGrooveTemplate templateOut;
    templateOut.name = "Extracted Groove";
    templateOut.stepOffsets.assign(16, 0.0f);

    const auto rawGroove = AudioTranscriber::extractGroove(audioData, sampleCount, sampleRate, bpm);
    templateOut.swingPercentage = std::clamp(rawGroove.swingPercent, 45.0f, 85.0f);
    templateOut.velocityVariance = rawGroove.humanVelocityVariance;
    templateOut.microTimingDeviationMs = rawGroove.microTimingDeviationMs;

    if (!rawGroove.stepOffsets.empty())
    {
        for (std::size_t s = 0; s < std::min(templateOut.stepOffsets.size(), rawGroove.stepOffsets.size()); ++s)
            templateOut.stepOffsets[s] = rawGroove.stepOffsets[s];
    }

    return templateOut;
}

ReusableGrooveTemplate TransientSlicerEngine::getFactoryMpc60Swing(int percent)
{
    const int p = std::clamp(percent, 50, 75);
    ReusableGrooveTemplate t;
    std::ostringstream ss;
    ss << "MPC60 " << p << "%";
    t.name = ss.str();
    t.swingPercentage = static_cast<float>(p);
    t.velocityVariance = 0.08f;
    t.microTimingDeviationMs = 1.2f;
    t.stepOffsets.assign(16, 0.0f);

    // MPC60 delays even 16th notes (steps 1, 3, 5, ...) proportional to (percent - 50)
    const float offset = static_cast<float>(p - 50) / 100.0f;
    for (std::size_t i = 1; i < 16; i += 2)
        t.stepOffsets[i] = offset;

    return t;
}

ReusableGrooveTemplate TransientSlicerEngine::getFactorySp1200Swing(int swingIndex)
{
    const int idx = std::clamp(swingIndex, 1, 6);
    // SP-1200 Swing 1..6 maps approximately to 54%, 58%, 62%, 66%, 71%, 75%
    static constexpr std::array<int, 6> kSpPercents {54, 58, 62, 66, 71, 75};
    const int p = kSpPercents[static_cast<std::size_t>(idx - 1)];

    ReusableGrooveTemplate t;
    std::ostringstream ss;
    ss << "E-mu SP-1200 Swing " << idx;
    t.name = ss.str();
    t.swingPercentage = static_cast<float>(p);
    t.velocityVariance = 0.12f;
    t.microTimingDeviationMs = 2.1f;
    t.stepOffsets.assign(16, 0.0f);

    const float offset = static_cast<float>(p - 50) / 100.0f;
    for (std::size_t i = 1; i < 16; i += 2)
        t.stepOffsets[i] = offset * 0.95f;

    return t;
}

} // namespace dawg::engine::dsp
