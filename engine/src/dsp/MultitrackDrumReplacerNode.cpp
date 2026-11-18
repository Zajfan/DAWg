#include <dawg/engine/dsp/MultitrackDrumReplacerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;

} // namespace

MultitrackDrumReplacerNode::MultitrackDrumReplacerNode() noexcept
{
    generateInternalSamples();
    updateDetectionFilters();
}

bool MultitrackDrumReplacerNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    setRetriggerHoldoffMs(m_holdoffMs);
    updateDetectionFilters();
    return true;
}

void MultitrackDrumReplacerNode::reset() noexcept
{
    m_bpZ1 = 0.0f;
    m_bpZ2 = 0.0f;
    m_prevDetectorSample = 0.0f;
    m_detectorEnv = 0.0f;
    m_holdoffCounter = 0;
    m_triggerCount.store(0, std::memory_order_relaxed);
    m_lastTriggerVelocity.store(0.0f, std::memory_order_relaxed);

    for (auto& v : m_voices)
    {
        v.playing = false;
        v.playbackCursor = 0;
    }
}

void MultitrackDrumReplacerNode::setInstrumentType(DrumInstrumentType type) noexcept
{
    m_instrumentType = type;
    switch (type)
    {
        case DrumInstrumentType::kick:
            m_detectionLowCut = 45.0f;
            m_detectionHighCut = 130.0f;
            m_midiNote = 36;
            break;
        case DrumInstrumentType::snare:
            m_detectionLowCut = 180.0f;
            m_detectionHighCut = 3200.0f;
            m_midiNote = 38;
            break;
        case DrumInstrumentType::tom:
            m_detectionLowCut = 80.0f;
            m_detectionHighCut = 400.0f;
            m_midiNote = 45;
            break;
        case DrumInstrumentType::custom:
            break;
    }
    updateDetectionFilters();
}

void MultitrackDrumReplacerNode::setDetectionBand(float lowCutHz, float highCutHz) noexcept
{
    m_detectionLowCut = std::clamp(lowCutHz, 20.0f, 10000.0f);
    m_detectionHighCut = std::clamp(highCutHz, m_detectionLowCut + 10.0f, static_cast<float>(m_sampleRate * 0.45));
    m_instrumentType = DrumInstrumentType::custom;
    updateDetectionFilters();
}

void MultitrackDrumReplacerNode::setThresholdDb(float threshDb) noexcept
{
    m_thresholdDb = std::clamp(threshDb, -60.0f, 0.0f);
    m_thresholdLinear = std::pow(10.0f, m_thresholdDb / 20.0f);
}

void MultitrackDrumReplacerNode::setRetriggerHoldoffMs(float ms) noexcept
{
    m_holdoffMs = std::clamp(ms, 5.0f, 200.0f);
    m_holdoffSamples = static_cast<std::size_t>((m_holdoffMs * 0.001f) * static_cast<float>(m_sampleRate));
}

void MultitrackDrumReplacerNode::updateDetectionFilters() noexcept
{
    // 2nd order bandpass filter centered between lowCut and highCut
    const float centerHz = std::sqrt(m_detectionLowCut * m_detectionHighCut);
    const float bw = std::max(10.0f, m_detectionHighCut - m_detectionLowCut);
    const float q = std::clamp(centerHz / bw, 0.2f, 10.0f);

    const float w0 = 2.0f * pi * centerHz / static_cast<float>(m_sampleRate);
    const float sinW = std::sin(w0);
    const float cosW = std::cos(w0);
    const float alpha = sinW / (2.0f * q);

    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW;
    const float a2 = 1.0f - alpha;

    const float invA0 = 1.0f / a0;
    m_bpB0 = b0 * invA0;
    m_bpB1 = b1 * invA0;
    m_bpB2 = b2 * invA0;
    m_bpA1 = a1 * invA0;
    m_bpA2 = a2 * invA0;
}

void MultitrackDrumReplacerNode::generateInternalSamples() noexcept
{
    // Generate punchy synthesized kick and snare layers (approx 4800 frames = 100 ms)
    constexpr std::size_t numFrames = 4800;

    // Layer 0: Soft
    m_layers[0].sampleLength = numFrames;
    m_layers[0].minVelocity = 0.0f;
    m_layers[0].maxVelocity = 0.5f;
    m_layers[0].sampleDataL.resize(numFrames, 0.0f);
    m_layers[0].sampleDataR.resize(numFrames, 0.0f);

    // Layer 1: Hard
    m_layers[1].sampleLength = numFrames;
    m_layers[1].minVelocity = 0.5f;
    m_layers[1].maxVelocity = 1.0f;
    m_layers[1].sampleDataL.resize(numFrames, 0.0f);
    m_layers[1].sampleDataR.resize(numFrames, 0.0f);

    for (std::size_t i = 0; i < numFrames; ++i)
    {
        const float t = static_cast<float>(i) / 48000.0f;
        const float env = std::exp(-t * 30.0f);

        // Soft kick/snare
        const float freqSoft = 65.0f + 120.0f * std::exp(-t * 60.0f);
        const float toneSoft = std::sin(2.0f * pi * freqSoft * t);
        const float noiseSoft = (static_cast<float>((i * 1103515245 + 12345) & 0x7FFF) / 16384.0f - 1.0f) * std::exp(-t * 40.0f);
        const float sampleSoft = (toneSoft * 0.7f + noiseSoft * 0.3f) * env * 0.6f;

        m_layers[0].sampleDataL[i] = sampleSoft;
        m_layers[0].sampleDataR[i] = sampleSoft;

        // Hard punchy kick/snare
        const float freqHard = 85.0f + 250.0f * std::exp(-t * 80.0f);
        const float toneHard = std::sin(2.0f * pi * freqHard * t);
        const float noiseHard = (static_cast<float>((i * 1103515245 + 12345) & 0x7FFF) / 16384.0f - 1.0f) * std::exp(-t * 50.0f);
        const float sampleHard = (toneHard * 0.8f + noiseHard * 0.4f) * env * 0.95f;

        m_layers[1].sampleDataL[i] = sampleHard;
        m_layers[1].sampleDataR[i] = sampleHard;
    }
}

void MultitrackDrumReplacerNode::loadSampleLayer(
    std::size_t layerIndex,
    const float* left,
    const float* right,
    std::size_t frames,
    float minVel,
    float maxVel)
{
    if (layerIndex >= maxLayers || !left || frames == 0)
    {
        return;
    }

    auto& l = m_layers[layerIndex];
    l.sampleLength = frames;
    l.minVelocity = minVel;
    l.maxVelocity = maxVel;
    l.sampleDataL.assign(left, left + frames);
    if (right)
    {
        l.sampleDataR.assign(right, right + frames);
    }
    else
    {
        l.sampleDataR = l.sampleDataL;
    }
}

void MultitrackDrumReplacerNode::triggerDrumHit(float peakLevel, std::size_t /*frameOffset*/) noexcept
{
    const float velocity = std::clamp(peakLevel / std::max(0.001f, m_thresholdLinear * 2.0f), 0.1f, 1.0f);
    m_lastTriggerVelocity.store(velocity, std::memory_order_relaxed);
    m_triggerCount.fetch_add(1, std::memory_order_relaxed);

    // Find layer matching velocity
    std::size_t targetLayer = 0;
    for (std::size_t i = 0; i < maxLayers; ++i)
    {
        if (m_layers[i].sampleLength > 0 && velocity >= m_layers[i].minVelocity && velocity <= m_layers[i].maxVelocity)
        {
            targetLayer = i;
            break;
        }
    }

    // Allocate voice (steal oldest or find free)
    ActiveDrumVoice* voice = &m_voices[0];
    for (auto& v : m_voices)
    {
        if (!v.playing)
        {
            voice = &v;
            break;
        }
    }

    voice->playing = true;
    voice->sampleIndex = targetLayer;
    voice->playbackCursor = 0;
    voice->gain = velocity;
}

void MultitrackDrumReplacerNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    float* left = context.audio.channels[0];
    float* right = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : left;

    for (std::size_t i = 0; i < blockFrames; ++i)
    {
        const float micL = left[i];
        const float micR = right[i];
        const float monoIn = 0.5f * (micL + micR);

        // 1. Bandpass filter for bleed rejection
        const float bpOut = m_bpB0 * monoIn + m_bpZ1;
        m_bpZ1 = m_bpB1 * monoIn - m_bpA1 * bpOut + m_bpZ2;
        m_bpZ2 = m_bpB2 * monoIn - m_bpA2 * bpOut;

        // Fast peak follower
        const float absBp = std::abs(bpOut);
        if (absBp > m_detectorEnv)
        {
            m_detectorEnv = absBp;
        }
        else
        {
            m_detectorEnv *= 0.995f; // Fast release
        }

        // 2. Transient Detection with Retrigger Holdoff
        if (m_holdoffCounter > 0)
        {
            --m_holdoffCounter;
        }
        else if (m_detectorEnv >= m_thresholdLinear && m_detectorEnv > m_prevDetectorSample)
        {
            // Detected onset transient! Trigger replacement sample with sample-accurate alignment
            triggerDrumHit(m_detectorEnv, i);
            m_holdoffCounter = m_holdoffSamples;
        }
        m_prevDetectorSample = m_detectorEnv;

        // 3. Render active replacement voices
        float repL = 0.0f;
        float repR = 0.0f;

        for (auto& voice : m_voices)
        {
            if (voice.playing)
            {
                const auto& layer = m_layers[voice.sampleIndex];
                if (voice.playbackCursor < layer.sampleLength)
                {
                    repL += layer.sampleDataL[voice.playbackCursor] * voice.gain;
                    repR += layer.sampleDataR[voice.playbackCursor] * voice.gain;
                    ++voice.playbackCursor;
                }
                else
                {
                    voice.playing = false;
                }
            }
        }

        // 4. Mix Original Acoustic Mic & Replacement Sample
        const float outL = micL * (1.0f - m_dryWetMix) + repL * m_dryWetMix;
        const float outR = micR * (1.0f - m_dryWetMix) + repR * m_dryWetMix;

        left[i] = outL;
        if (right != left)
        {
            right[i] = outR;
        }
    }
}

} // namespace dawg::engine::dsp
