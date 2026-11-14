#include <dawg/engine/dsp/TnhcSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {
namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

float midiNoteToHz(const std::uint8_t note, const float detuneCents = 0.0f) noexcept
{
    const float semitones = static_cast<float>(note) - 69.0f + (detuneCents / 100.0f);
    return 440.0f * std::pow(2.0f, semitones / 12.0f);
}

// PolyBLEP residual for anti-aliasing steps in bandlimited waveforms
float polyBlep(const double t, const double dt) noexcept
{
    if (dt <= 0.0)
        return 0.0f;

    if (t < dt)
    {
        const double norm = t / dt;
        return static_cast<float>(2.0 * norm - norm * norm - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double norm = (t - 1.0) / dt;
        return static_cast<float>(2.0 * norm + norm * norm + 1.0);
    }
    return 0.0f;
}

// Simple fast PRNG (LCG)
float nextNoise(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state) / 2147483648.0f - 1.0f;
}

} // namespace

float TnhcSynthNode::AdsrEnvelope::advance(const AdsrParams& params, const double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return currentLevel;

    switch (stage)
    {
    case EnvStage::idle:
        currentLevel = 0.0f;
        break;

    case EnvStage::attack:
    {
        const float rate = params.attackSec > 0.0001f
            ? static_cast<float>(1.0 / (static_cast<double>(params.attackSec) * sampleRate))
            : 1.0f;
        currentLevel += rate;
        if (currentLevel >= 1.0f)
        {
            currentLevel = 1.0f;
            stage = EnvStage::decay;
        }
        break;
    }

    case EnvStage::decay:
    {
        const float target = std::clamp(params.sustainLevel, 0.0f, 1.0f);
        const float rate = params.decaySec > 0.0001f
            ? static_cast<float>(1.0 / (static_cast<double>(params.decaySec) * sampleRate))
            : 1.0f;
        currentLevel -= rate;
        if (currentLevel <= target)
        {
            currentLevel = target;
            stage = EnvStage::sustain;
        }
        break;
    }

    case EnvStage::sustain:
        currentLevel = std::clamp(params.sustainLevel, 0.0f, 1.0f);
        break;

    case EnvStage::release:
    {
        const float rate = params.releaseSec > 0.0001f
            ? static_cast<float>(1.0 / (static_cast<double>(params.releaseSec) * sampleRate))
            : 1.0f;
        currentLevel -= rate;
        if (currentLevel <= 0.0f)
        {
            currentLevel = 0.0f;
            stage = EnvStage::idle;
        }
        break;
    }
    }

    return currentLevel;
}

float TnhcSynthNode::TptsSvf::process(const float in, const float cutoffHz,
                                      const float q, const double sampleRate,
                                      const SynthFilterMode mode) noexcept
{
    if (sampleRate <= 0.0)
        return in;

    const double nyquist = sampleRate * 0.49;
    const double clampedCutoff = std::clamp(static_cast<double>(cutoffHz), 10.0, nyquist);
    const double g = std::tan(std::numbers::pi * clampedCutoff / sampleRate);
    const double damping = 1.0 / std::max(0.1, static_cast<double>(q));

    const double a1 = 1.0 / (1.0 + g * (g + damping));
    const double a2 = g * a1;
    const double a3 = g * a2;

    const double v3 = static_cast<double>(in) - s2;
    const double v1 = a1 * s1 + a2 * v3;
    const double v2 = s2 + a2 * s1 + a3 * v3;

    s1 = static_cast<float>(2.0 * v1 - s1);
    s2 = static_cast<float>(2.0 * v2 - s2);

    switch (mode)
    {
    case SynthFilterMode::lowpass:
        return static_cast<float>(v2);
    case SynthFilterMode::bandpass:
        return static_cast<float>(v1);
    case SynthFilterMode::highpass:
        return static_cast<float>(static_cast<double>(in) - damping * v1 - v2);
    }
    return static_cast<float>(v2);
}

TnhcSynthNode::TnhcSynthNode() noexcept
{
    reset();
}

bool TnhcSynthNode::prepare(const double sampleRate, const std::size_t,
                            const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || channelCount == 0)
        return false;

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    reset();
    return true;
}

void TnhcSynthNode::reset() noexcept
{
    for (auto& voice : m_voices)
        voice.reset();
    m_lfoPhase = 0.0;
    m_voiceAgeCounter = 0;
    m_channelPitchBend.fill(0.0f);
    m_channelPressure.fill(0.0f);
    m_channelTimbre.fill(0.5f);
}

void TnhcSynthNode::allNotesOff() noexcept
{
    for (auto& voice : m_voices)
    {
        if (voice.active)
        {
            voice.ampEnv.release();
            voice.filterEnv.release();
        }
    }
}

TnhcSynthNode::Voice* TnhcSynthNode::allocateVoice() noexcept
{
    // First find inactive voice
    for (auto& voice : m_voices)
    {
        if (! voice.active)
            return &voice;
    }

    // Voice stealing: steal oldest voice
    Voice* oldest = &m_voices[0];
    for (auto& voice : m_voices)
    {
        if (voice.age < oldest->age)
            oldest = &voice;
    }
    return oldest;
}

void TnhcSynthNode::noteOn(const std::uint8_t channel, const std::uint8_t note, const float velocity) noexcept
{
    Voice* voice = allocateVoice();
    if (voice == nullptr)
        return;

    const std::uint8_t ch = channel & 0x0F;
    voice->active = true;
    voice->channel = ch;
    voice->note = note;
    voice->velocity = std::clamp(velocity, 0.0f, 1.0f);
    voice->age = ++m_voiceAgeCounter;
    voice->pitchBendSemitones = m_mpeEnabled ? m_channelPitchBend[ch] : m_channelPitchBend[0];
    voice->pressure = m_mpeEnabled ? m_channelPressure[ch] : m_channelPressure[0];
    voice->timbre = m_mpeEnabled ? m_channelTimbre[ch] : m_channelTimbre[0];
    voice->osc1Phase = 0.0;
    voice->osc2Phase = 0.0;

    voice->ampEnv.trigger();
    voice->filterEnv.trigger();
    voice->svfStage1.reset();
    voice->svfStage2.reset();
}

void TnhcSynthNode::noteOff(const std::uint8_t channel, const std::uint8_t note) noexcept
{
    const std::uint8_t ch = channel & 0x0F;
    for (auto& voice : m_voices)
    {
        if (voice.active && voice.note == note && (!m_mpeEnabled || voice.channel == ch || ch == 0))
        {
            voice.ampEnv.release();
            voice.filterEnv.release();
        }
    }
}

void TnhcSynthNode::setChannelPitchBend(const std::uint8_t channel, const float semitones) noexcept
{
    const std::uint8_t ch = channel & 0x0F;
    m_channelPitchBend[ch] = semitones;

    for (auto& voice : m_voices)
    {
        if (voice.active && (!m_mpeEnabled || voice.channel == ch || ch == 0))
            voice.pitchBendSemitones = semitones;
    }
}

void TnhcSynthNode::setChannelPressure(const std::uint8_t channel, const float pressure) noexcept
{
    const std::uint8_t ch = channel & 0x0F;
    const float p = std::clamp(pressure, 0.0f, 1.0f);
    m_channelPressure[ch] = p;

    for (auto& voice : m_voices)
    {
        if (voice.active && (!m_mpeEnabled || voice.channel == ch || ch == 0))
            voice.pressure = p;
    }
}

void TnhcSynthNode::setChannelTimbre(const std::uint8_t channel, const float timbre) noexcept
{
    const std::uint8_t ch = channel & 0x0F;
    const float t = std::clamp(timbre, 0.0f, 1.0f);
    m_channelTimbre[ch] = t;

    for (auto& voice : m_voices)
    {
        if (voice.active && (!m_mpeEnabled || voice.channel == ch || ch == 0))
            voice.timbre = t;
    }
}

std::size_t TnhcSynthNode::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& voice : m_voices)
    {
        if (voice.active)
            ++count;
    }
    return count;
}

float TnhcSynthNode::evaluateOsc(const SynthWaveform wave, const double phase,
                                 const double phaseInc, const float pulseWidth,
                                 std::uint32_t& rng) noexcept
{
    switch (wave)
    {
    case SynthWaveform::sine:
        return static_cast<float>(std::sin(twoPi * phase));

    case SynthWaveform::saw:
    {
        const double naive = 2.0 * phase - 1.0;
        return static_cast<float>(naive) - polyBlep(phase, phaseInc);
    }

    case SynthWaveform::pulse:
    {
        const double w = static_cast<double>(pulseWidth);
        const double naive = (phase < w) ? 1.0 : -1.0;
        const double wrapPhase = std::fmod(phase + 1.0 - w, 1.0);
        return static_cast<float>(naive) + polyBlep(phase, phaseInc) - polyBlep(wrapPhase, phaseInc);
    }

    case SynthWaveform::triangle:
    {
        const double saw = 2.0 * phase - 1.0;
        const double tri = 2.0 * std::abs(saw) - 1.0;
        return static_cast<float>(tri);
    }

    case SynthWaveform::noise:
        return nextNoise(rng);
    }
    return 0.0f;
}

float TnhcSynthNode::renderVoiceSample(Voice& voice, const float lfoVal) noexcept
{
    if (! voice.active)
        return 0.0f;

    const float amp = voice.ampEnv.advance(m_ampParams, m_sampleRate);
    if (voice.ampEnv.stage == EnvStage::idle)
    {
        voice.active = false;
        return 0.0f;
    }

    const float filterEnvVal = voice.filterEnv.advance(m_filterParams, m_sampleRate);

    // Frequency with LFO pitch modulation and MPE per-note pitch bend
    const float totalPitchModSemitones = lfoVal * m_lfoPitchDepth + voice.pitchBendSemitones;
    const float f1 = midiNoteToHz(voice.note, totalPitchModSemitones * 100.0f);
    const float f2 = midiNoteToHz(voice.note, m_osc2DetuneCents + (totalPitchModSemitones * 100.0f));

    const double inc1 = std::clamp(static_cast<double>(f1) / m_sampleRate, 0.0, 0.49);
    const double inc2 = std::clamp(static_cast<double>(f2) / m_sampleRate, 0.0, 0.49);

    const float osc1 = evaluateOsc(m_osc1Wave, voice.osc1Phase, inc1, m_pulseWidth, voice.rngState);
    const float osc2 = evaluateOsc(m_osc2Wave, voice.osc2Phase, inc2, m_pulseWidth, voice.rngState);

    voice.osc1Phase = std::fmod(voice.osc1Phase + inc1, 1.0);
    voice.osc2Phase = std::fmod(voice.osc2Phase + inc2, 1.0);

    const float mixedOsc = (1.0f - m_oscMix) * osc1 + m_oscMix * osc2;

    // Filter frequency modulation: envelope + LFO + MPE timbre (CC74) + MPE pressure
    const float timbreOctaves = (voice.timbre - 0.5f) * 3.0f;
    const float pressureCutoffOctaves = voice.pressure * 1.5f;
    const float cutoffModOctaves = filterEnvVal * m_filterEnvAmount * 4.0f + lfoVal * m_lfoFilterDepth + timbreOctaves + pressureCutoffOctaves;
    const float targetCutoff = std::clamp(m_filterCutoff * std::pow(2.0f, cutoffModOctaves), 20.0f, 20000.0f);

    // 4-pole cascaded SVF (24 dB/oct)
    const float s1 = voice.svfStage1.process(mixedOsc, targetCutoff, m_filterResonance, m_sampleRate, m_filterMode);
    const float filtered = voice.svfStage2.process(s1, targetCutoff, m_filterResonance, m_sampleRate, m_filterMode);

    const float pressureAmpMod = 1.0f + voice.pressure * 0.5f;
    return filtered * amp * voice.velocity * pressureAmpMod;
}

void TnhcSynthNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
        return;

    const std::size_t frameCount = context.audio.frameCount;
    std::size_t nextMidiIdx = 0;

    const double lfoInc = (m_sampleRate > 0.0) ? (static_cast<double>(m_lfoRate) / m_sampleRate) : 0.0;

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        // Sample-accurate MIDI & MPE event dispatching
        while (context.midi.events != nullptr && nextMidiIdx < context.midi.eventCount
               && context.midi.events[nextMidiIdx].frameOffset <= f)
        {
            const auto& event = context.midi.events[nextMidiIdx];
            if (event.size >= 2)
            {
                const auto rawStatus = static_cast<std::uint8_t>(event.data[0]);
                const auto statusType = rawStatus & 0xF0;
                const auto channel = rawStatus & 0x0F;
                const auto byte1 = static_cast<std::uint8_t>(event.data[1]) & 0x7F;
                const auto byte2 = (event.size >= 3) ? (static_cast<std::uint8_t>(event.data[2]) & 0x7F) : std::uint8_t {0};

                if (statusType == 0x90 && byte2 > 0)
                {
                    noteOn(channel, byte1, static_cast<float>(byte2) / 127.0f);
                }
                else if (statusType == 0x80 || (statusType == 0x90 && byte2 == 0))
                {
                    noteOff(channel, byte1);
                }
                else if (statusType == 0xE0 && event.size >= 3) // Pitch Bend (14-bit)
                {
                    const int rawBend = (static_cast<int>(byte2) << 7) | static_cast<int>(byte1);
                    const float normalizedBend = (static_cast<float>(rawBend) - 8192.0f) / 8192.0f;
                    setChannelPitchBend(channel, normalizedBend * m_pitchBendRange);
                }
                else if (statusType == 0xD0) // Channel Pressure / Aftertouch
                {
                    setChannelPressure(channel, static_cast<float>(byte1) / 127.0f);
                }
                else if (statusType == 0xA0 && event.size >= 3) // Polyphonic Key Pressure
                {
                    const float press = static_cast<float>(byte2) / 127.0f;
                    for (auto& voice : m_voices)
                    {
                        if (voice.active && voice.note == byte1 && (!m_mpeEnabled || voice.channel == channel))
                            voice.pressure = press;
                    }
                }
                else if (statusType == 0xB0 && event.size >= 3) // Control Change
                {
                    if (byte1 == 74) // CC 74: Timbre / Brightness
                    {
                        setChannelTimbre(channel, static_cast<float>(byte2) / 127.0f);
                    }
                    else if (byte1 == 123) // All Notes Off
                    {
                        allNotesOff();
                    }
                    else if (byte1 == 120) // All Sound Off
                    {
                        reset();
                    }
                }
            }
            ++nextMidiIdx;
        }

        // Advance LFO
        const float lfoVal = static_cast<float>(std::sin(twoPi * m_lfoPhase));
        m_lfoPhase = std::fmod(m_lfoPhase + lfoInc, 1.0);

        // Sum active voices
        float sampleSum = 0.0f;
        for (auto& voice : m_voices)
        {
            if (voice.active)
                sampleSum += renderVoiceSample(voice, lfoVal);
        }

        // Add to audio channels
        if (context.audio.channelCount >= 1 && context.audio.channels[0] != nullptr)
            context.audio.channels[0][f] += sampleSum;
        if (context.audio.channelCount >= 2 && context.audio.channels[1] != nullptr)
            context.audio.channels[1][f] += sampleSum;
    }
}

} // namespace dawg::engine
