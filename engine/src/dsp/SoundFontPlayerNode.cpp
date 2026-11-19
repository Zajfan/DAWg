#include <dawg/engine/dsp/SoundFontPlayerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

const std::array<SoundFontInstrument, 4> SoundFontPlayerNode::kPresets = {{
    {"Acoustic Concert Grand", "Piano", 5.0f, 600.0f, 0.65f, 500.0f, 14000.0f, 0.15f},
    {"Studio Upright Bass",    "Bass",  2.0f, 350.0f, 0.40f, 250.0f,  4500.0f, 0.25f},
    {"Symphonic Strings",      "Pads", 65.0f, 800.0f, 0.85f, 900.0f,  8500.0f, 0.20f},
    {"Orchestral Warm Brass",  "Brass",15.0f, 450.0f, 0.75f, 400.0f,  7200.0f, 0.35f}
}};

SoundFontPlayerNode::SoundFontPlayerNode() noexcept
{
    loadPreset(0);
}

void SoundFontPlayerNode::generateInternalWaveform(const std::size_t instIdx)
{
    // Pre-calculate 1 second of high-fidelity reference instrument tone at C4 (261.63 Hz)
    const std::size_t numSamples = 48000;
    m_sampleWaveform.resize(numSamples, 0.0f);
    m_baseFrequency = 261.63;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const double t = static_cast<double>(i) / 48000.0;
        double sum = 0.0;

        if (instIdx == 0) // Concert Grand Piano
        {
            // First 12 harmonics with piano decay
            for (int h = 1; h <= 12; ++h)
            {
                const double hAmp = (1.0 / static_cast<double>(h)) * std::exp(-t * (1.5 + h * 0.4));
                sum += std::sin(t * m_baseFrequency * h * 2.0 * std::numbers::pi) * hAmp;
            }
            // Hammer transient
            if (t < 0.008) sum += std::sin(t * 3200.0 * 2.0 * std::numbers::pi) * 0.45 * std::exp(-t * 400.0);
        }
        else if (instIdx == 1) // Upright Bass (sub C2 = 65.4 Hz)
        {
            for (int h = 1; h <= 6; ++h)
            {
                const double hAmp = (1.0 / static_cast<double>(h * h)) * std::exp(-t * 2.2);
                sum += std::sin(t * m_baseFrequency * h * 2.0 * std::numbers::pi) * hAmp;
            }
        }
        else if (instIdx == 2) // Symphonic Strings (rich saw + detuned chorus)
        {
            for (int h = 1; h <= 16; ++h)
            {
                const double hAmp = 1.0 / static_cast<double>(h);
                sum += std::sin(t * m_baseFrequency * h * 2.0 * std::numbers::pi) * hAmp * 0.7;
                sum += std::sin(t * (m_baseFrequency * 1.003) * h * 2.0 * std::numbers::pi) * hAmp * 0.35;
            }
        }
        else // Orchestral Brass
        {
            for (int h = 1; h <= 14; ++h)
            {
                const double hAmp = (h % 2 == 1) ? (1.0 / static_cast<double>(h)) : (0.5 / static_cast<double>(h));
                sum += std::sin(t * m_baseFrequency * h * 2.0 * std::numbers::pi) * hAmp;
            }
        }

        m_sampleWaveform[i] = static_cast<float>(std::tanh(sum * 0.95));
    }
}

bool SoundFontPlayerNode::prepare(const double sampleRate,
                                 const std::size_t /*maxBlockSize*/,
                                 const std::uint16_t /*channelCount*/)
{
    m_sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void SoundFontPlayerNode::reset() noexcept
{
    allNotesOff();
}

void SoundFontPlayerNode::loadPreset(const std::size_t index) noexcept
{
    if (index < kPresets.size())
    {
        m_instrument = kPresets[index];
        generateInternalWaveform(index);
    }
}

const char* SoundFontPlayerNode::presetName(const std::size_t index) const noexcept
{
    if (index < kPresets.size())
    {
        return kPresets[index].name.c_str();
    }
    return "Custom";
}

void SoundFontPlayerNode::noteOn(const std::uint8_t pitch, const float velocity) noexcept
{
    Voice* target = nullptr;
    for (auto& v : m_voices)
    {
        if (! v.active)
        {
            target = &v;
            break;
        }
    }
    if (target == nullptr)
    {
        target = &m_voices[0];
    }

    target->active = true;
    target->pitch = pitch;
    target->velocity = std::clamp(velocity, 0.05f, 1.0f);
    target->samplePos = 0.0;

    const double targetFreq = 440.0 * std::pow(2.0, static_cast<double>(pitch - 69) / 12.0);
    target->pitchRatio = targetFreq / m_baseFrequency;

    target->envStage = Voice::Stage::attack;
    target->envLevel = 0.0f;
    target->filter.reset();
}

void SoundFontPlayerNode::noteOff(const std::uint8_t pitch) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.pitch == pitch)
        {
            v.envStage = Voice::Stage::release;
        }
    }
}

void SoundFontPlayerNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        v.envStage = Voice::Stage::idle;
        v.envLevel = 0.0f;
    }
}

void SoundFontPlayerNode::processVoice(Voice& v, float& outL, float& outR) noexcept
{
    if (! v.active || m_sampleWaveform.empty()) return;

    // Advance ADSR Envelope
    const float attCoeff = 1.0f / (std::max(1.0f, m_instrument.attackMs) * 0.001f * static_cast<float>(m_sampleRate));
    const float decCoeff = 1.0f / (std::max(1.0f, m_instrument.decayMs) * 0.001f * static_cast<float>(m_sampleRate));
    const float relCoeff = 1.0f / (std::max(1.0f, m_instrument.releaseMs) * 0.001f * static_cast<float>(m_sampleRate));

    switch (v.envStage)
    {
    case Voice::Stage::attack:
        v.envLevel += attCoeff;
        if (v.envLevel >= 1.0f)
        {
            v.envLevel = 1.0f;
            v.envStage = Voice::Stage::decay;
        }
        break;
    case Voice::Stage::decay:
        v.envLevel -= decCoeff * (v.envLevel - m_instrument.sustainLevel);
        if (v.envLevel <= m_instrument.sustainLevel + 0.001f)
        {
            v.envLevel = m_instrument.sustainLevel;
            v.envStage = Voice::Stage::sustain;
        }
        break;
    case Voice::Stage::sustain:
        v.envLevel = m_instrument.sustainLevel;
        break;
    case Voice::Stage::release:
        v.envLevel -= relCoeff * v.envLevel;
        if (v.envLevel <= 0.0005f)
        {
            v.envLevel = 0.0f;
            v.envStage = Voice::Stage::idle;
            v.active = false;
            return;
        }
        break;
    case Voice::Stage::idle:
        v.active = false;
        return;
    }

    // Hermite Interpolated Sample Playback with Loop Point
    const auto tableSize = m_sampleWaveform.size();
    const auto loopStart = static_cast<double>(tableSize) * 0.35;
    const auto loopEnd   = static_cast<double>(tableSize) * 0.95;

    if (v.samplePos >= loopEnd)
    {
        v.samplePos = loopStart + std::fmod(v.samplePos - loopStart, loopEnd - loopStart);
    }

    const auto idx0 = static_cast<std::size_t>(v.samplePos) % tableSize;
    const auto idx1 = (idx0 + 1) % tableSize;
    const float frac = static_cast<float>(v.samplePos - std::floor(v.samplePos));
    const float rawSample = m_sampleWaveform[idx0] * (1.0f - frac) + m_sampleWaveform[idx1] * frac;

    v.samplePos += v.pitchRatio;

    // Filter
    const float filtered = v.filter.process(rawSample, m_instrument.filterCutoff, m_instrument.filterResonance * 3.5f, m_sampleRate);
    const float finalLevel = filtered * v.envLevel * v.velocity * 0.85f;

    outL += finalLevel;
    outR += finalLevel;
}

void SoundFontPlayerNode::process(ProcessContext& context) noexcept
{
    if (context.midi.events != nullptr)
    {
        for (std::size_t i = 0; i < context.midi.eventCount; ++i)
        {
            const auto& ev = context.midi.events[i];
            if (ev.size >= 3)
            {
                const auto status = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
                const auto note = static_cast<std::uint8_t>(ev.data[1]);
                const auto vel = static_cast<std::uint8_t>(ev.data[2]);

                if (status == 0x90 && vel > 0)
                {
                    noteOn(note, static_cast<float>(vel) / 127.0f);
                }
                else if (status == 0x80 || (status == 0x90 && vel == 0))
                {
                    noteOff(note);
                }
                else if (status == 0xB0 && note == 123)
                {
                    allNotesOff();
                }
            }
        }
    }

    if (context.audio.channels == nullptr || context.audio.frameCount == 0) return;

    float* const outL = context.audio.channels[0];
    float* const outR = (context.audio.channelCount >= 2 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : context.audio.channels[0];

    for (std::size_t f = 0; f < context.audio.frameCount; ++f)
    {
        float left = 0.0f;
        float right = 0.0f;

        for (auto& v : m_voices)
        {
            processVoice(v, left, right);
        }

        outL[f] = left;
        if (context.audio.channelCount >= 2 && outR != outL)
        {
            outR[f] = right;
        }
    }
}

} // namespace dawg::engine
