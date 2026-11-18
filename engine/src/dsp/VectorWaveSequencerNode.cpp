#include <dawg/engine/dsp/VectorWaveSequencerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;
constexpr double twoPi = 2.0 * std::numbers::pi_v<double>;

[[nodiscard]] inline float noteToFreq(int note) noexcept
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

} // namespace

VectorWaveSequencerNode::VectorWaveSequencerNode() noexcept
{
    for (std::size_t i = 0; i < maxSteps; ++i)
    {
        m_steps[i].active = true;
        m_steps[i].durationBeats = 0.25f;
        m_steps[i].crossfadeRatio = 0.2f;
        m_steps[i].gateRatio = 0.8f;
        m_steps[i].semitoneOffset = 0.0f;
        m_steps[i].cutoffOffsetHz = 0.0f;
        m_steps[i].waveSource = static_cast<VectorWaveSource>(i % 6);
    }
}

bool VectorWaveSequencerNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    return true;
}

void VectorWaveSequencerNode::reset() noexcept
{
    allNotesOff();
    m_trajectoryPhase = 0.0;
    m_bounceX = 0.0f;
    m_bounceY = 0.0f;
    m_currentStep = 0;
    m_stepSampleCounter = 0.0;
}

void VectorWaveSequencerNode::setVectorPosition(float x, float y) noexcept
{
    m_vectorX.store(std::clamp(x, -1.0f, 1.0f), std::memory_order_relaxed);
    m_vectorY.store(std::clamp(y, -1.0f, 1.0f), std::memory_order_relaxed);
}

void VectorWaveSequencerNode::setStepCount(std::size_t count) noexcept
{
    m_stepCount = std::clamp(count, std::size_t{1}, maxSteps);
    if (m_currentStep >= m_stepCount)
    {
        m_currentStep = 0;
    }
}

void VectorWaveSequencerNode::setStep(std::size_t index, const VectorStep& step) noexcept
{
    if (index < maxSteps)
    {
        m_steps[index] = step;
    }
}

const VectorStep& VectorWaveSequencerNode::step(std::size_t index) const noexcept
{
    static const VectorStep defaultStep {};
    if (index < maxSteps)
    {
        return m_steps[index];
    }
    return defaultStep;
}

void VectorWaveSequencerNode::setQuadrantWave(std::size_t quadrantIndex, VectorWaveSource source) noexcept
{
    if (quadrantIndex < m_quadrantSources.size())
    {
        m_quadrantSources[quadrantIndex] = source;
    }
}

VectorWaveSource VectorWaveSequencerNode::quadrantWave(std::size_t quadrantIndex) const noexcept
{
    if (quadrantIndex < m_quadrantSources.size())
    {
        return m_quadrantSources[quadrantIndex];
    }
    return VectorWaveSource::virtualAnalogSaw;
}

void VectorWaveSequencerNode::noteOn(int noteNumber, float velocity) noexcept
{
    // Find free voice or steal oldest/releasing voice
    VectorVoice* target = nullptr;
    for (auto& v : m_voices)
    {
        if (!v.active)
        {
            target = &v;
            break;
        }
    }
    if (!target)
    {
        for (auto& v : m_voices)
        {
            if (v.inRelease)
            {
                target = &v;
                break;
            }
        }
    }
    if (!target)
    {
        target = &m_voices[0];
    }

    target->active = true;
    target->noteNumber = noteNumber;
    target->velocity = std::clamp(velocity, 0.0f, 1.0f);
    target->baseFrequency = noteToFreq(noteNumber);
    target->inRelease = false;
    target->phaseA = 0.0;
    target->phaseB = 0.0;
    target->phaseC = 0.0;
    target->phaseModC = 0.0;
    target->phaseD = 0.0;
    target->envAmp = 0.0f;
    target->envFilter = 0.0f;
    target->filterZ1L = 0.0f;
    target->filterZ2L = 0.0f;
    target->filterZ1R = 0.0f;
    target->filterZ2R = 0.0f;
}

void VectorWaveSequencerNode::noteOff(int noteNumber) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.noteNumber == noteNumber)
        {
            v.inRelease = true;
        }
    }
}

void VectorWaveSequencerNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        v.inRelease = false;
        v.noteNumber = -1;
    }
}

std::size_t VectorWaveSequencerNode::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& v : m_voices)
    {
        if (v.active)
        {
            ++count;
        }
    }
    return count;
}

void VectorWaveSequencerNode::computeWeights(float x, float y, float& wA, float& wB, float& wC, float& wD) noexcept
{
    const float u = std::clamp((x + 1.0f) * 0.5f, 0.0f, 1.0f);
    const float v = std::clamp((y + 1.0f) * 0.5f, 0.0f, 1.0f);

    wA = (1.0f - u) * v;          // Top-Left (A)
    wB = u * v;                   // Top-Right (B)
    wC = (1.0f - u) * (1.0f - v); // Bottom-Left (C)
    wD = u * (1.0f - v);          // Bottom-Right (D)
}

void VectorWaveSequencerNode::updateTrajectory(std::size_t numFrames) noexcept
{
    if (m_trajectoryMode == VectorTrajectoryMode::manual)
    {
        return;
    }

    const double delta = (static_cast<double>(numFrames) / m_sampleRate) * static_cast<double>(m_trajectoryRateHz);
    m_trajectoryPhase = std::fmod(m_trajectoryPhase + delta * twoPi, twoPi);

    float posX = 0.0f;
    float posY = 0.0f;

    switch (m_trajectoryMode)
    {
        case VectorTrajectoryMode::orbit:
        {
            posX = m_trajectoryDepth * static_cast<float>(std::cos(m_trajectoryPhase));
            posY = m_trajectoryDepth * static_cast<float>(std::sin(m_trajectoryPhase));
            break;
        }
        case VectorTrajectoryMode::lissajous:
        {
            posX = m_trajectoryDepth * static_cast<float>(std::sin(m_trajectoryPhase * 2.0));
            posY = m_trajectoryDepth * static_cast<float>(std::sin(m_trajectoryPhase * 3.0 + pi * 0.25f));
            break;
        }
        case VectorTrajectoryMode::spiral:
        {
            const float r = m_trajectoryDepth * static_cast<float>(0.3 + 0.7 * (0.5 + 0.5 * std::sin(m_trajectoryPhase * 0.5)));
            posX = r * static_cast<float>(std::cos(m_trajectoryPhase * 3.0));
            posY = r * static_cast<float>(std::sin(m_trajectoryPhase * 3.0));
            break;
        }
        case VectorTrajectoryMode::bounce:
        {
            const float dt = static_cast<float>(numFrames / m_sampleRate) * m_trajectoryRateHz * 2.0f;
            m_bounceX += m_bounceVelX * dt;
            m_bounceY += m_bounceVelY * dt;
            if (m_bounceX > 1.0f)  { m_bounceX = 1.0f;  m_bounceVelX = -std::abs(m_bounceVelX); }
            if (m_bounceX < -1.0f) { m_bounceX = -1.0f; m_bounceVelX = std::abs(m_bounceVelX); }
            if (m_bounceY > 1.0f)  { m_bounceY = 1.0f;  m_bounceVelY = -std::abs(m_bounceVelY); }
            if (m_bounceY < -1.0f) { m_bounceY = -1.0f; m_bounceVelY = std::abs(m_bounceVelY); }
            posX = m_bounceX * m_trajectoryDepth;
            posY = m_bounceY * m_trajectoryDepth;
            break;
        }
        default:
            return;
    }

    setVectorPosition(posX, posY);
}

float VectorWaveSequencerNode::synthesizeSource(
    VectorWaveSource source,
    double& phase,
    double& phaseMod,
    float freq,
    float pw,
    float fmIndex) noexcept
{
    const double phaseInc = (static_cast<double>(freq) / m_sampleRate);

    float sample = 0.0f;
    switch (source)
    {
        case VectorWaveSource::virtualAnalogSaw:
        {
            // PolyBLEP-style or linear saw
            sample = static_cast<float>(2.0 * phase - 1.0);
            break;
        }
        case VectorWaveSource::virtualAnalogPulse:
        {
            sample = (phase < pw) ? 1.0f : -1.0f;
            break;
        }
        case VectorWaveSource::additiveHarmonicBell:
        {
            const double p = phase * twoPi;
            sample = static_cast<float>(
                0.6 * std::sin(p) +
                0.3 * std::sin(p * 2.76) +
                0.2 * std::sin(p * 5.4) +
                0.1 * std::sin(p * 8.9)
            );
            break;
        }
        case VectorWaveSource::frequencyModulation:
        {
            const double modInc = phaseInc * 2.0; // 1:2 modulator
            phaseMod = std::fmod(phaseMod + modInc, 1.0);
            const float modVal = static_cast<float>(std::sin(phaseMod * twoPi)) * fmIndex;
            sample = static_cast<float>(std::sin(phase * twoPi + modVal));
            break;
        }
        case VectorWaveSource::texturedNoise:
        {
            // Simple deterministic noise based on phase hash
            const std::uint32_t bits = static_cast<std::uint32_t>(phase * 4294967295.0);
            const float white = static_cast<float>(bits ^ (bits >> 13)) / 2147483648.0f - 1.0f;
            sample = std::clamp(white, -1.0f, 1.0f);
            break;
        }
        case VectorWaveSource::wavetableOrgan:
        {
            const double p = phase * twoPi;
            sample = static_cast<float>(
                0.5 * std::sin(p) +
                0.35 * std::sin(p * 2.0) +
                0.25 * std::sin(p * 3.0) +
                0.15 * std::sin(p * 4.0)
            );
            break;
        }
    }

    phase = std::fmod(phase + phaseInc, 1.0);
    return sample;
}

void VectorWaveSequencerNode::renderVoiceSample(
    VectorVoice& voice,
    float wA, float wB, float wC, float wD,
    float& outL, float& outR) noexcept
{
    // Apply envelope
    const float attackRate = 1.0f / (m_attackTime * static_cast<float>(m_sampleRate));
    const float releaseRate = 1.0f / (m_releaseTime * static_cast<float>(m_sampleRate));

    if (voice.inRelease)
    {
        voice.envAmp -= releaseRate;
        if (voice.envAmp <= 0.0001f)
        {
            voice.envAmp = 0.0f;
            voice.active = false;
            return;
        }
    }
    else
    {
        if (voice.envAmp < 1.0f)
        {
            voice.envAmp = std::min(1.0f, voice.envAmp + attackRate);
        }
    }

    // Step sequencer pitch & cutoff modulation
    float pitchTrans = 0.0f;
    float cutoffMod = 0.0f;
    if (m_sequencerEnabled && m_stepCount > 0)
    {
        const auto& currentSt = m_steps[m_currentStep];
        if (currentSt.active)
        {
            pitchTrans = currentSt.semitoneOffset;
            cutoffMod = currentSt.cutoffOffsetHz;
        }
    }

    const float effectiveFreq = voice.baseFrequency * std::pow(2.0f, pitchTrans / 12.0f);

    const float sA = synthesizeSource(m_quadrantSources[0], voice.phaseA, voice.phaseModC, effectiveFreq, m_pulseWidthA, m_fmIndexC);
    const float sB = synthesizeSource(m_quadrantSources[1], voice.phaseB, voice.phaseModC, effectiveFreq, m_pulseWidthA, m_fmIndexC);
    const float sC = synthesizeSource(m_quadrantSources[2], voice.phaseC, voice.phaseModC, effectiveFreq, m_pulseWidthA, m_fmIndexC);
    const float sD = synthesizeSource(m_quadrantSources[3], voice.phaseD, voice.phaseModC, effectiveFreq, m_pulseWidthA, m_fmIndexC);

    const float mixed = (sA * wA + sB * wB + sC * wC + sD * wD) * voice.envAmp * voice.velocity;

    // Resonant Biquad Filter (Lowpass)
    const float effectiveCutoff = std::clamp(m_filterCutoff + cutoffMod, 20.0f, static_cast<float>(m_sampleRate * 0.45));
    const float omega = 2.0f * pi * effectiveCutoff / static_cast<float>(m_sampleRate);
    const float alpha = std::sin(omega) / (2.0f * m_filterResonance);
    const float cosW = std::cos(omega);
    const float b0 = (1.0f - cosW) * 0.5f;
    const float b1 = 1.0f - cosW;
    const float b2 = (1.0f - cosW) * 0.5f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW;
    const float a2 = 1.0f - alpha;

    const float invA0 = 1.0f / a0;
    const float normB0 = b0 * invA0;
    const float normB1 = b1 * invA0;
    const float normB2 = b2 * invA0;
    const float normA1 = a1 * invA0;
    const float normA2 = a2 * invA0;

    // Direct Form II Transposed filter
    const float filtL = normB0 * mixed + voice.filterZ1L;
    voice.filterZ1L = normB1 * mixed - normA1 * filtL + voice.filterZ2L;
    voice.filterZ2L = normB2 * mixed - normA2 * filtL;

    outL += filtL;
    outR += filtL;
}

void VectorWaveSequencerNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    // Process incoming MIDI events
    if (context.midi.events != nullptr)
    {
        for (std::size_t i = 0; i < context.midi.eventCount; ++i)
        {
            const auto& ev = context.midi.events[i];
            if (ev.size >= 3)
            {
                const auto status = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
                const auto note = static_cast<int>(ev.data[1]);
                const auto vel = static_cast<float>(static_cast<std::uint8_t>(ev.data[2])) / 127.0f;

                if (status == 0x90 && vel > 0.0f)
                {
                    noteOn(note, vel);
                }
                else if (status == 0x80 || (status == 0x90 && vel == 0.0f))
                {
                    noteOff(note);
                }
            }
        }
    }

    updateTrajectory(blockFrames);

    // Compute active weights
    float wA = 0.25f, wB = 0.25f, wC = 0.25f, wD = 0.25f;
    computeWeights(m_vectorX.load(std::memory_order_relaxed), m_vectorY.load(std::memory_order_relaxed), wA, wB, wC, wD);

    // Step sequencer advancement
    if (m_sequencerEnabled && m_stepCount > 0)
    {
        m_stepSampleCounter += static_cast<double>(blockFrames);
        const double samplesPerBeat = (m_sampleRate * 60.0) / static_cast<double>(m_tempoBpm);
        const double stepDurationSamples = samplesPerBeat * static_cast<double>(m_steps[m_currentStep].durationBeats);

        if (m_stepSampleCounter >= stepDurationSamples)
        {
            m_stepSampleCounter -= stepDurationSamples;
            m_currentStep = (m_currentStep + 1) % m_stepCount;
        }
    }

    float* leftOut = context.audio.channels[0];
    float* rightOut = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : leftOut;

    // Clear output buffer for synth rendering
    std::fill_n(leftOut, blockFrames, 0.0f);
    if (rightOut != leftOut)
    {
        std::fill_n(rightOut, blockFrames, 0.0f);
    }

    for (std::size_t f = 0; f < blockFrames; ++f)
    {
        float frameL = 0.0f;
        float frameR = 0.0f;

        for (auto& voice : m_voices)
        {
            if (voice.active)
            {
                renderVoiceSample(voice, wA, wB, wC, wD, frameL, frameR);
            }
        }

        leftOut[f] = frameL;
        if (rightOut != leftOut)
        {
            rightOut[f] = frameR;
        }
    }
}

} // namespace dawg::engine::dsp
