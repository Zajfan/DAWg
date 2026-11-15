#include <dawg/engine/dsp/DrumPadSamplerNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

DrumPadSamplerNode::DrumPadSamplerNode() noexcept
{
    initFactorySounds();
}

bool DrumPadSamplerNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void DrumPadSamplerNode::reset() noexcept
{
    for (auto& voice : m_voices)
    {
        voice.active = false;
        voice.stage = EnvStage::idle;
        voice.envLevel = 0.0f;
        voice.lpPrevL = 0.0f;
        voice.lpPrevR = 0.0f;
    }
}

void DrumPadSamplerNode::stopAll() noexcept
{
    reset();
}

bool DrumPadSamplerNode::isPadActive(std::size_t padIdx) const noexcept
{
    for (const auto& v : m_voices)
    {
        if (v.active && v.padIndex == padIdx)
            return true;
    }
    return false;
}

void DrumPadSamplerNode::triggerPad(std::size_t padIdx, float velocity) noexcept
{
    if (padIdx >= kNumPads)
        return;

    const auto& padData = m_pads[padIdx];
    if (padData.sampleL.empty())
        return;

    // Handle choke groups (choke other playing pads in the same non-zero choke group)
    if (padData.chokeGroup > 0)
    {
        for (auto& v : m_voices)
        {
            if (v.active && m_pads[v.padIndex].chokeGroup == padData.chokeGroup)
            {
                v.stage = EnvStage::release;
            }
        }
    }

    // Find free voice or steal oldest/quietest
    int chosenIdx = -1;
    float minLevel = 2.0f;
    for (std::size_t i = 0; i < kMaxVoices; ++i)
    {
        if (! m_voices[i].active)
        {
            chosenIdx = static_cast<int>(i);
            break;
        }
        if (m_voices[i].envLevel < minLevel)
        {
            minLevel = m_voices[i].envLevel;
            chosenIdx = static_cast<int>(i);
        }
    }

    if (chosenIdx < 0)
        chosenIdx = 0;

    auto& voice = m_voices[static_cast<std::size_t>(chosenIdx)];
    voice.active = true;
    voice.padIndex = padIdx;
    voice.velocity = std::clamp(velocity, 0.01f, 1.0f);
    voice.stage = EnvStage::attack;
    voice.envLevel = 0.0f;
    voice.lpPrevL = 0.0f;
    voice.lpPrevR = 0.0f;

    // Calculate playback step rate based on semitone tuning
    const double pitchRatio = std::pow(2.0, static_cast<double>(padData.pitchSemi) / 12.0);
    voice.stepRate = pitchRatio;

    const auto len = static_cast<double>(padData.sampleL.size());
    const double startSample = std::clamp(static_cast<double>(padData.startNorm), 0.0, 0.999) * len;
    const double endSample = std::clamp(static_cast<double>(padData.endNorm), 0.001, 1.0) * len;

    if (padData.reverse)
    {
        voice.readPos = std::max(startSample, endSample - 1.0);
    }
    else
    {
        voice.readPos = startSample;
    }
}

void DrumPadSamplerNode::releasePad(std::size_t padIdx) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.padIndex == padIdx && v.stage != EnvStage::release)
        {
            v.stage = EnvStage::release;
        }
    }
}

void DrumPadSamplerNode::noteOn(std::uint8_t note, float velocity) noexcept
{
    // MIDI note 36 (C1) maps to Pad 0, note 51 (D#2) maps to Pad 15
    if (note >= 36 && note < 36 + kNumPads)
    {
        triggerPad(static_cast<std::size_t>(note - 36), velocity);
    }
}

void DrumPadSamplerNode::noteOff(std::uint8_t note) noexcept
{
    if (note >= 36 && note < 36 + kNumPads)
    {
        releasePad(static_cast<std::size_t>(note - 36));
    }
}

void DrumPadSamplerNode::loadSampleIntoPad(std::size_t padIdx, std::vector<float> left, std::vector<float> right, std::string name)
{
    if (padIdx >= kNumPads)
        return;

    auto& padData = m_pads[padIdx];
    padData.name = std::move(name);
    padData.sampleL = std::move(left);
    padData.sampleR = right.empty() ? padData.sampleL : std::move(right);
    padData.startNorm = 0.0f;
    padData.endNorm = 1.0f;
}

void DrumPadSamplerNode::sliceSampleAcrossPads(const float* data, std::size_t lengthSamples, double sampleRate, int numSlices)
{
    if (data == nullptr || lengthSamples < 1000)
        return;

    const int targetSlices = std::clamp(numSlices, 2, static_cast<int>(kNumPads));
    std::vector<std::size_t> slicePoints;
    slicePoints.push_back(0);

    // Compute short-time energy flux to find transient onsets
    constexpr std::size_t windowSize = 512;
    std::vector<float> energy;
    energy.reserve(lengthSamples / windowSize);

    for (std::size_t i = 0; i + windowSize < lengthSamples; i += windowSize)
    {
        float rms = 0.0f;
        for (std::size_t j = 0; j < windowSize; ++j)
        {
            const float s = data[i + j];
            rms += s * s;
        }
        energy.push_back(std::sqrt(rms / static_cast<float>(windowSize)));
    }

    // Find transient peaks with positive slope
    for (std::size_t i = 1; i + 1 < energy.size(); ++i)
    {
        const float diff = energy[i] - energy[i - 1];
        if (diff > 0.04f && energy[i] > energy[i + 1])
        {
            const std::size_t samplePos = i * windowSize;
            if (samplePos > slicePoints.back() + static_cast<std::size_t>(sampleRate * 0.08)) // at least 80ms apart
            {
                slicePoints.push_back(samplePos);
                if (static_cast<int>(slicePoints.size()) >= targetSlices)
                    break;
            }
        }
    }

    // If transient detection found fewer than targetSlices, fall back to equal division
    while (static_cast<int>(slicePoints.size()) < targetSlices)
    {
        const std::size_t sliceSize = lengthSamples / static_cast<std::size_t>(targetSlices);
        slicePoints.clear();
        for (int s = 0; s < targetSlices; ++s)
        {
            slicePoints.push_back(static_cast<std::size_t>(s) * sliceSize);
        }
        break;
    }
    slicePoints.push_back(lengthSamples);

    for (std::size_t p = 0; p < static_cast<std::size_t>(targetSlices) && p < kNumPads; ++p)
    {
        const std::size_t start = slicePoints[p];
        const std::size_t end = (p + 1 < slicePoints.size()) ? slicePoints[p + 1] : lengthSamples;
        if (end > start)
        {
            std::vector<float> sliceL(data + start, data + end);
            loadSampleIntoPad(p, sliceL, sliceL, "Slice " + std::to_string(p + 1));
        }
    }
}

void DrumPadSamplerNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
        return;

    const std::size_t numSamples = context.audio.frameCount;
    const double dt = 1.0 / m_sampleRate;

    // Handle incoming MIDI events for drum note triggering
    if (context.midi.events != nullptr)
    {
        for (std::size_t m = 0; m < context.midi.eventCount; ++m)
        {
            const auto& ev = context.midi.events[m];
            if (ev.size >= 2)
            {
                const auto statusType = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
                const auto note = static_cast<std::uint8_t>(ev.data[1]) & 0x7F;
                const auto vel = (ev.size >= 3) ? (static_cast<std::uint8_t>(ev.data[2]) & 0x7F) : std::uint8_t {0};

                if (statusType == 0x90 && vel > 0)
                {
                    noteOn(note, static_cast<float>(vel) / 127.0f);
                }
                else if (statusType == 0x80 || (statusType == 0x90 && vel == 0))
                {
                    noteOff(note);
                }
            }
        }
    }

    for (auto& voice : m_voices)
    {
        if (! voice.active)
            continue;

        const auto& padData = m_pads[voice.padIndex];
        const auto sampleLen = static_cast<double>(padData.sampleL.size());
        if (sampleLen < 2.0)
        {
            voice.active = false;
            continue;
        }

        const double startPos = std::clamp(static_cast<double>(padData.startNorm), 0.0, 0.999) * sampleLen;
        const double endPos = std::clamp(static_cast<double>(padData.endNorm), 0.001, 1.0) * sampleLen;

        const float attackRate = (padData.attackSec > 0.0005f) ? static_cast<float>(dt / static_cast<double>(padData.attackSec)) : 1.0f;
        const float decayRate = (padData.decaySec > 0.001f) ? static_cast<float>(dt / static_cast<double>(padData.decaySec)) : 0.05f;
        const float releaseRate = (padData.releaseSec > 0.001f) ? static_cast<float>(dt / static_cast<double>(padData.releaseSec)) : 0.05f;

        // Filter coefficient: 1-pole low-pass
        const float normCutoff = std::clamp(padData.filterCutoffHz / static_cast<float>(m_sampleRate), 0.001f, 0.499f);
        const float lpAlpha = std::clamp(2.0f * 3.14159265f * normCutoff, 0.01f, 1.0f);

        for (std::size_t s = 0; s < numSamples; ++s)
        {
            // Update ADSR envelope
            switch (voice.stage)
            {
            case EnvStage::attack:
                voice.envLevel += attackRate;
                if (voice.envLevel >= 1.0f)
                {
                    voice.envLevel = 1.0f;
                    voice.stage = EnvStage::decay;
                }
                break;
            case EnvStage::decay:
                voice.envLevel -= decayRate * (1.0f - padData.sustainLevel);
                if (voice.envLevel <= padData.sustainLevel)
                {
                    voice.envLevel = padData.sustainLevel;
                    voice.stage = EnvStage::sustain;
                }
                break;
            case EnvStage::sustain:
                voice.envLevel = padData.sustainLevel;
                break;
            case EnvStage::release:
                voice.envLevel -= releaseRate;
                if (voice.envLevel <= 0.0001f)
                {
                    voice.envLevel = 0.0f;
                    voice.active = false;
                    voice.stage = EnvStage::idle;
                }
                break;
            case EnvStage::idle:
                voice.active = false;
                break;
            }

            if (! voice.active)
                break;

            // Check boundary
            if (! padData.reverse)
            {
                if (voice.readPos >= endPos)
                {
                    voice.active = false;
                    break;
                }
            }
            else
            {
                if (voice.readPos <= startPos)
                {
                    voice.active = false;
                    break;
                }
            }

            // Interpolated sample reading
            const auto idx0 = static_cast<std::size_t>(voice.readPos);
            const auto idx1 = std::min(idx0 + 1, padData.sampleL.size() - 1);
            const float frac = static_cast<float>(voice.readPos - static_cast<double>(idx0));

            const float rawL = (1.0f - frac) * padData.sampleL[idx0] + frac * padData.sampleL[idx1];
            const float rawR = (padData.sampleR.size() > idx1) ? ((1.0f - frac) * padData.sampleR[idx0] + frac * padData.sampleR[idx1]) : rawL;

            // Low-pass filter
            voice.lpPrevL += lpAlpha * (rawL - voice.lpPrevL);
            voice.lpPrevR += lpAlpha * (rawR - voice.lpPrevR);

            const float gain = voice.velocity * voice.envLevel * padData.gainLinear;
            if (context.audio.channelCount >= 1 && context.audio.channels[0] != nullptr)
                context.audio.channels[0][s] += voice.lpPrevL * gain;
            if (context.audio.channelCount >= 2 && context.audio.channels[1] != nullptr)
                context.audio.channels[1][s] += voice.lpPrevR * gain;

            // Advance playhead
            if (! padData.reverse)
                voice.readPos += voice.stepRate;
            else
                voice.readPos -= voice.stepRate;
        }
    }
}

void DrumPadSamplerNode::initFactorySounds()
{
    constexpr double sr = 48000.0;

    auto makeKick = [](double durationSec, float startPitch, float endPitch, float punch) {
        const auto numSamples = static_cast<std::size_t>(sr * durationSec);
        std::vector<float> b(numSamples);
        double phase = 0.0;
        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const double progress = t / durationSec;
            const double currentPitch = endPitch + (startPitch - endPitch) * std::exp(-progress * 18.0);
            phase += 2.0 * 3.1415926535 * currentPitch / sr;
            float val = static_cast<float>(std::sin(phase));
            // punch saturation
            val = std::tanh(val * (1.0f + punch * (1.0f - static_cast<float>(progress))));
            const float amp = static_cast<float>(1.0 - progress);
            b[i] = val * amp * amp;
        }
        return b;
    };

    auto makeSnare = [](double durationSec, float pitchHz, float noiseMix) {
        const auto numSamples = static_cast<std::size_t>(sr * durationSec);
        std::vector<float> b(numSamples);
        double phase = 0.0;
        std::uint32_t seed = 1234567;
        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const double progress = t / durationSec;
            phase += 2.0 * 3.1415926535 * pitchHz / sr;
            const float tone = static_cast<float>(std::sin(phase));

            seed = seed * 1664525u + 1013904223u;
            const float noise = (static_cast<float>(seed & 0xFFFF) / 32768.0f) - 1.0f;

            const float body = (1.0f - noiseMix) * tone + noiseMix * noise;
            const float amp = static_cast<float>(std::exp(-progress * 8.0));
            b[i] = body * amp * 0.85f;
        }
        return b;
    };

    auto makeHiHat = [](double durationSec, float brightness) {
        const auto numSamples = static_cast<std::size_t>(sr * durationSec);
        std::vector<float> b(numSamples);
        std::uint32_t seed = 9876543;
        float prev = 0.0f;
        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const double progress = t / durationSec;
            seed = seed * 1664525u + 1013904223u;
            const float rawNoise = (static_cast<float>(seed & 0xFFFF) / 32768.0f) - 1.0f;
            // High-pass filter for metallic sizzle
            const float hp = rawNoise - prev * brightness;
            prev = rawNoise;

            const float amp = static_cast<float>(std::exp(-progress * 22.0));
            b[i] = hp * amp * 0.7f;
        }
        return b;
    };

    // 0: Kick 808
    loadSampleIntoPad(0, makeKick(0.5, 160.0f, 48.0f, 1.2f), {}, "808 Kick");
    // 1: Kick Punch
    loadSampleIntoPad(1, makeKick(0.35, 220.0f, 55.0f, 2.5f), {}, "Punch Kick");
    // 2: Snare Clap
    loadSampleIntoPad(2, makeSnare(0.28, 190.0f, 0.75f), {}, "Snare Clap");
    // 3: Snare Studio
    loadSampleIntoPad(3, makeSnare(0.32, 220.0f, 0.55f), {}, "Studio Snare");
    // 4: Clap Studio
    loadSampleIntoPad(4, makeSnare(0.25, 300.0f, 0.90f), {}, "Clap Hit");
    // 5: Closed HiHat (Choke group 1)
    loadSampleIntoPad(5, makeHiHat(0.08, 0.85f), {}, "Closed Hat");
    m_pads[5].chokeGroup = 1;
    // 6: Open HiHat (Choke group 1)
    loadSampleIntoPad(6, makeHiHat(0.45, 0.85f), {}, "Open Hat");
    m_pads[6].chokeGroup = 1;
    // 7: Pedal Hat (Choke group 1)
    loadSampleIntoPad(7, makeHiHat(0.12, 0.90f), {}, "Pedal Hat");
    m_pads[7].chokeGroup = 1;

    // 8: Low Tom
    loadSampleIntoPad(8, makeKick(0.38, 140.0f, 85.0f, 0.8f), {}, "Low Tom");
    // 9: Mid Tom
    loadSampleIntoPad(9, makeKick(0.32, 190.0f, 120.0f, 0.8f), {}, "Mid Tom");
    // 10: High Tom
    loadSampleIntoPad(10, makeKick(0.26, 250.0f, 160.0f, 0.8f), {}, "High Tom");
    // 11: Crash Cymbal
    loadSampleIntoPad(11, makeHiHat(0.95, 0.78f), {}, "Crash Cymbal");
    // 12: Ride Cymbal
    loadSampleIntoPad(12, makeHiHat(0.70, 0.65f), {}, "Ride Cymbal");
    // 13: Rimshot
    loadSampleIntoPad(13, makeSnare(0.09, 450.0f, 0.35f), {}, "Rimshot");
    // 14: Perc Shaker
    loadSampleIntoPad(14, makeHiHat(0.15, 0.95f), {}, "Perc Shaker");
    // 15: Sub 808
    loadSampleIntoPad(15, makeKick(0.85, 90.0f, 42.0f, 0.5f), {}, "Sub 808");
}

} // namespace dawg::engine
