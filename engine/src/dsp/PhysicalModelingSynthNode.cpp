#include <dawg/engine/dsp/PhysicalModelingSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr std::array<float, 6> kMembraneRatios = {1.000f, 1.593f, 2.135f, 2.295f, 2.653f, 2.917f};
constexpr std::array<float, 6> kBarRatios       = {1.000f, 3.984f, 9.201f, 16.32f, 24.51f, 34.02f};
constexpr std::array<float, 6> kPlateRatios     = {1.000f, 1.732f, 2.291f, 2.784f, 3.512f, 4.163f};

float midiNoteToFreq(int note) noexcept
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

} // namespace

PhysicalModelingSynthNode::PhysicalModelingSynthNode() noexcept
{
    reset();
}

bool PhysicalModelingSynthNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void PhysicalModelingSynthNode::reset() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        v.noteNumber = -1;
        v.gate = false;
        v.delayLine.fill(0.0f);
        v.writeIdx = 0;
        v.lpState = 0.0f;
        v.exciterPhase = 0.0f;
        for (auto& f : v.modalFiltersL)
        {
            f.s1 = 0.0f;
            f.s2 = 0.0f;
        }
        for (auto& f : v.modalFiltersR)
        {
            f.s1 = 0.0f;
            f.s2 = 0.0f;
        }
    }
}

float PhysicalModelingSynthNode::generateNoise() noexcept
{
    m_noiseSeed = m_noiseSeed * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<std::int32_t>(m_noiseSeed)) / 2147483648.0f;
}

void PhysicalModelingSynthNode::noteOn(int noteNumber, float velocity) noexcept
{
    // Find free voice or steal oldest active voice
    Voice* targetVoice = nullptr;
    std::uint32_t oldestAge = 0;
    Voice* oldestVoice = nullptr;

    for (auto& v : m_voices)
    {
        if (!v.active)
        {
            targetVoice = &v;
            break;
        }
        if (oldestVoice == nullptr || v.age < oldestAge)
        {
            oldestAge = v.age;
            oldestVoice = &v;
        }
    }

    if (targetVoice == nullptr)
    {
        targetVoice = oldestVoice;
    }

    if (targetVoice == nullptr)
    {
        return;
    }

    targetVoice->active = true;
    targetVoice->noteNumber = noteNumber;
    targetVoice->frequencyHz = midiNoteToFreq(noteNumber);
    targetVoice->velocity = std::clamp(velocity, 0.05f, 1.0f);
    targetVoice->gate = true;
    targetVoice->age = ++m_voiceAgeCounter;
    targetVoice->writeIdx = 0;
    targetVoice->lpState = 0.0f;
    targetVoice->bowVelocity = 0.0f;

    // Exciter duration based on frequency and mallet hardness
    const float fs = static_cast<float>(m_sampleRate);
    const float basePeriod = fs / std::max(20.0f, targetVoice->frequencyHz);
    targetVoice->exciterPhase = 0.0f;
    targetVoice->exciterDurationSamples = std::clamp(basePeriod * (1.2f - 0.9f * m_malletHardness), 4.0f, 600.0f);

    // Seed waveguide delay line for Karplus-Strong string pluck
    targetVoice->delayLine.fill(0.0f);
    const auto seedLen = static_cast<std::size_t>(targetVoice->exciterDurationSamples);
    for (std::size_t i = 0; i < seedLen && i < targetVoice->delayLine.size(); ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(seedLen);
        const float window = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * phase));
        targetVoice->delayLine[i] = generateNoise() * targetVoice->velocity * window;
    }

    for (auto& f : targetVoice->modalFiltersL)
    {
        f.s1 = 0.0f;
        f.s2 = 0.0f;
    }
    for (auto& f : targetVoice->modalFiltersR)
    {
        f.s1 = 0.0f;
        f.s2 = 0.0f;
    }
}

void PhysicalModelingSynthNode::noteOff(int noteNumber) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.noteNumber == noteNumber)
        {
            v.gate = false;
        }
    }
}

void PhysicalModelingSynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.gate = false;
        v.active = false;
    }
}

std::size_t PhysicalModelingSynthNode::activeVoiceCount() const noexcept
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

void PhysicalModelingSynthNode::processMidi(const MidiBufferView& midi) noexcept
{
    if (midi.events == nullptr || midi.eventCount == 0)
    {
        return;
    }

    for (std::size_t i = 0; i < midi.eventCount; ++i)
    {
        const auto& ev = midi.events[i];
        if (ev.size < 2)
        {
            continue;
        }

        const auto status = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
        const auto note = static_cast<int>(static_cast<std::uint8_t>(ev.data[1]) & 0x7F);
        const auto vel = (ev.size >= 3) ? static_cast<float>(static_cast<std::uint8_t>(ev.data[2]) & 0x7F) / 127.0f : 0.0f;

        if (status == 0x90 && vel > 0.0f)
        {
            noteOn(note, vel);
        }
        else if (status == 0x80 || (status == 0x90 && vel <= 0.0f))
        {
            noteOff(note);
        }
        else if (status == 0xB0)
        {
            const auto ccNum = static_cast<std::uint8_t>(ev.data[1]) & 0x7F;
            if (ccNum == 120 || ccNum == 123)
            {
                allNotesOff();
            }
        }
    }
}

void PhysicalModelingSynthNode::renderVoice(
    Voice& voice,
    float* outL,
    float* outR,
    std::size_t numSamples) noexcept
{
    const float fs = static_cast<float>(m_sampleRate);
    const float dt = 1.0f / fs;

    // Waveguide delay parameters
    const float pitchPeriod = fs / std::max(20.0f, voice.frequencyHz);
    const float delaySamples = std::clamp(pitchPeriod, 4.0f, static_cast<float>(MaxDelayLineLength - 2));
    const float damping = std::exp(-3.0f / (voice.frequencyHz * m_decayTimeSec));
    const float lpCoeff = 0.5f - 0.45f * m_malletHardness;

    // Modal resonator frequencies and Q factors
    std::array<float, NumModalBands> modeFreqs {};
    std::array<float, NumModalBands> modeGains {};
    std::array<float, NumModalBands> modeQs {};

    for (std::size_t m = 0; m < NumModalBands; ++m)
    {
        float ratio = static_cast<float>(m + 1);
        switch (m_modelType)
        {
        case PhysicalModelType::waveguideString:
            // Stiff string inharmonicity: f_m = m * f0 * sqrt(1 + B * m^2)
            ratio = static_cast<float>(m + 1) * std::sqrt(1.0f + m_inharmonicity * 0.05f * static_cast<float>((m + 1) * (m + 1)));
            break;
        case PhysicalModelType::modalMembrane:
            ratio = kMembraneRatios[m];
            break;
        case PhysicalModelType::modalBar:
            ratio = kBarRatios[m];
            break;
        case PhysicalModelType::modalPlate:
            ratio = kPlateRatios[m];
            break;
        }

        const float f = std::clamp(voice.frequencyHz * ratio, 20.0f, fs * 0.45f);
        modeFreqs[m] = f;

        // Standing wave strike node distribution
        const float strikePhase = static_cast<float>(m + 1) * std::numbers::pi_v<float> * m_strikePosition;
        modeGains[m] = std::abs(std::sin(strikePhase)) / (1.0f + 0.4f * static_cast<float>(m));

        // Mode Q derived from decay time
        modeQs[m] = std::clamp(std::numbers::pi_v<float> * f * m_decayTimeSec / (1.0f + 0.35f * static_cast<float>(m)), 10.0f, 2000.0f);
    }

    float maxVoiceEnergy = 0.0f;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        // 1. Generate Physical Excitation
        float exciter = 0.0f;
        if (voice.exciterPhase < voice.exciterDurationSamples)
        {
            const float p = voice.exciterPhase / voice.exciterDurationSamples;
            if (m_exciterType == ExciterType::mallet)
            {
                // Raised cosine mallet strike pulse
                exciter = voice.velocity * 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * p));
            }
            else if (m_exciterType == ExciterType::breath)
            {
                // Aerodynamic air turbulence
                exciter = voice.velocity * generateNoise() * std::sin(std::numbers::pi_v<float> * p);
            }
            voice.exciterPhase += 1.0f;
        }

        if (m_exciterType == ExciterType::bow && voice.gate)
        {
            // Continuous Coulomb stick-slip bow friction
            const float bowPressure = voice.velocity * 0.8f;
            const float vRel = bowPressure - voice.bowVelocity;
            const float friction = std::tanh(vRel * 3.0f) * std::exp(-0.2f * std::abs(vRel));
            exciter = friction * bowPressure * 0.25f;
        }

        float sampleL = 0.0f;
        float sampleR = 0.0f;

        if (m_modelType == PhysicalModelType::waveguideString && m_exciterType != ExciterType::breath)
        {
            // Karplus-Strong Extended Waveguide
            float readPos = static_cast<float>(voice.writeIdx) - delaySamples;
            while (readPos < 0.0f)
            {
                readPos += static_cast<float>(MaxDelayLineLength);
            }
            const auto idx0 = static_cast<std::size_t>(readPos) % MaxDelayLineLength;
            const auto idx1 = (idx0 + 1) % MaxDelayLineLength;
            const float frac = readPos - static_cast<float>(static_cast<std::size_t>(readPos));
            const float delayedSample = voice.delayLine[idx0] + frac * (voice.delayLine[idx1] - voice.delayLine[idx0]);

            // Low-pass string internal friction
            voice.lpState += (1.0f - lpCoeff) * (delayedSample - voice.lpState);
            const float stringFeedback = voice.lpState * damping;

            voice.bowVelocity = delayedSample;
            const float nextDelayIn = exciter + stringFeedback;

            voice.delayLine[voice.writeIdx] = nextDelayIn;
            voice.writeIdx = (voice.writeIdx + 1) % MaxDelayLineLength;

            sampleL = delayedSample;
            sampleR = delayedSample;
        }
        else
        {
            // Coupled Modal Resonator Bank
            for (std::size_t m = 0; m < NumModalBands; ++m)
            {
                const float g = std::tan(std::numbers::pi_v<float> * modeFreqs[m] * dt);
                const float k = 1.0f / modeQs[m];
                const float denom = 1.0f / (1.0f + g * (g + k));

                auto& fL = voice.modalFiltersL[m];
                const float v1L = (exciter - fL.s1 * (g + k) - fL.s2) * denom;
                const float v0L = g * v1L;
                const float bpL = g * (v0L + fL.s1) + fL.s2;
                fL.s1 += 2.0f * v0L;
                fL.s2 += 2.0f * g * bpL;

                auto& fR = voice.modalFiltersR[m];
                const float v1R = (exciter - fR.s1 * (g + k) - fR.s2) * denom;
                const float v0R = g * v1R;
                const float bpR = g * (v0R + fR.s1) + fR.s2;
                fR.s1 += 2.0f * v0R;
                fR.s2 += 2.0f * g * bpR;

                sampleL += bpL * modeGains[m];
                sampleR += bpR * modeGains[m];
            }
        }

        const float voiceGain = m_masterGain * voice.velocity;
        outL[i] += sampleL * voiceGain;
        if (outR != nullptr)
        {
            outR[i] += sampleR * voiceGain;
        }

        maxVoiceEnergy = std::max(maxVoiceEnergy, std::abs(sampleL));
    }

    // Voice lifecycle termination check
    if (!voice.gate && maxVoiceEnergy < 1.0e-4f && voice.exciterPhase >= voice.exciterDurationSamples)
    {
        voice.active = false;
    }
}

void PhysicalModelingSynthNode::process(ProcessContext& context) noexcept
{
    processMidi(context.midi);

    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    std::fill_n(outL, numSamples, 0.0f);
    if (outR != nullptr)
    {
        std::fill_n(outR, numSamples, 0.0f);
    }

    for (auto& v : m_voices)
    {
        if (v.active)
        {
            renderVoice(v, outL, outR, numSamples);
        }
    }
}

} // namespace dawg::engine::dsp
