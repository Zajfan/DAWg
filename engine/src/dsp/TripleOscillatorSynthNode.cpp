#include <dawg/engine/dsp/TripleOscillatorSynthNode.h>

#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

[[nodiscard]] double midiToFreq(std::uint8_t note) noexcept
{
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

} // namespace

float TripleOscillatorSynthNode::renderWaveform(double phase, TripleOscWaveform wave, std::uint32_t& noiseSeed) noexcept
{
    const double p = phase - std::floor(phase);
    switch (wave)
    {
    case TripleOscWaveform::sine:
        return static_cast<float>(std::sin(p * twoPi));
    case TripleOscWaveform::triangle:
        return static_cast<float>(p < 0.5 ? (4.0 * p - 1.0) : (3.0 - 4.0 * p));
    case TripleOscWaveform::sawtooth:
        return static_cast<float>(2.0 * p - 1.0);
    case TripleOscWaveform::square:
        return p < 0.5 ? 1.0f : -1.0f;
    case TripleOscWaveform::noise:
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(noiseSeed)) / 2147483648.0f;
    }
    return 0.0f;
}

TripleOscillatorSynthNode::TripleOscillatorSynthNode() noexcept
{
    // Osc 1 default: Sawtooth, full volume
    m_oscConfigs[0].waveform = TripleOscWaveform::sawtooth;
    m_oscConfigs[0].volume = 1.0f;
    m_oscConfigs[0].coarsePitchSemitones = 0;
    m_oscConfigs[0].finePitchCents = 0.0f;

    // Osc 2 default: Sawtooth, detuned, 80% volume
    m_oscConfigs[1].waveform = TripleOscWaveform::sawtooth;
    m_oscConfigs[1].volume = 0.8f;
    m_oscConfigs[1].coarsePitchSemitones = 0;
    m_oscConfigs[1].finePitchCents = 12.0f;

    // Osc 3 default: Square, -12 semitones (sub), 60% volume
    m_oscConfigs[2].waveform = TripleOscWaveform::square;
    m_oscConfigs[2].volume = 0.6f;
    m_oscConfigs[2].coarsePitchSemitones = -12;
    m_oscConfigs[2].finePitchCents = -8.0f;

    setAmpAdsr(5.0f, 150.0f, 0.8f, 250.0f);
    setFilterAdsr(10.0f, 300.0f, 0.3f, 300.0f, 0.5f);
}

bool TripleOscillatorSynthNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void TripleOscillatorSynthNode::reset() noexcept
{
    allNotesOff();
    for (auto& v : m_voices)
    {
        v.svfLowL = 0.0f;
        v.svfBandL = 0.0f;
        v.svfLowR = 0.0f;
        v.svfBandR = 0.0f;
    }
}

void TripleOscillatorSynthNode::setOscConfig(std::size_t oscIdx, const TripleOscConfig& cfg) noexcept
{
    if (oscIdx < oscCount)
    {
        m_oscConfigs[oscIdx] = cfg;
    }
}

const TripleOscConfig& TripleOscillatorSynthNode::oscConfig(std::size_t oscIdx) const noexcept
{
    static const TripleOscConfig fallback {};
    return (oscIdx < oscCount) ? m_oscConfigs[oscIdx] : fallback;
}

void TripleOscillatorSynthNode::setAmpAdsr(float aMs, float dMs, float sLevel, float rMs) noexcept
{
    const double sr = m_sampleRate;
    m_ampAttackRate = static_cast<float>(1.0 / std::max(1.0, static_cast<double>(aMs) * 0.001 * sr));
    m_ampDecayRate = static_cast<float>(1.0 / std::max(1.0, static_cast<double>(dMs) * 0.001 * sr));
    m_ampSustainLevel = std::clamp(sLevel, 0.0f, 1.0f);
    m_ampReleaseRate = static_cast<float>(1.0 / std::max(1.0, static_cast<double>(rMs) * 0.001 * sr));
}

void TripleOscillatorSynthNode::setFilterAdsr(float aMs, float dMs, float sLevel, float rMs, float envAmount) noexcept
{
    const double sr = m_sampleRate;
    m_filtAttackRate = static_cast<float>(1.0 / std::max(1.0, static_cast<double>(aMs) * 0.001 * sr));
    m_filtDecayRate = static_cast<float>(1.0 / std::max(1.0, static_cast<double>(dMs) * 0.001 * sr));
    m_filtSustainLevel = std::clamp(sLevel, 0.0f, 1.0f);
    m_filtReleaseRate = static_cast<float>(1.0 / std::max(1.0, static_cast<double>(rMs) * 0.001 * sr));
    m_filtEnvAmount = std::clamp(envAmount, -1.0f, 1.0f);
}

void TripleOscillatorSynthNode::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept
{
    if (velocity == 0)
    {
        noteOff(note);
        return;
    }

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
        // Steal voice with lowest ampLevel
        float lowest = 2.0f;
        for (auto& v : m_voices)
        {
            if (v.ampLevel < lowest)
            {
                lowest = v.ampLevel;
                target = &v;
            }
        }
    }

    if (target != nullptr)
    {
        target->active = true;
        target->note = note;
        target->velocity = velocity;
        target->noteFreq = midiToFreq(note);
        target->pitchBendSemitones = 0.0f;
        target->pressure = 0.0f;
        target->slide = 0.0f;
        target->phases.fill(0.0);
        target->ampStage = EnvStage::attack;
        target->ampLevel = 0.0f;
        target->filtStage = EnvStage::attack;
        target->filtLevel = 0.0f;
    }
}

void TripleOscillatorSynthNode::noteOff(std::uint8_t note) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.note == note)
        {
            v.ampStage = EnvStage::release;
            v.filtStage = EnvStage::release;
        }
    }
}

void TripleOscillatorSynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        v.ampStage = EnvStage::idle;
        v.filtStage = EnvStage::idle;
        v.ampLevel = 0.0f;
        v.filtLevel = 0.0f;
    }
}

void TripleOscillatorSynthNode::notePitchBend(std::uint8_t note, float semitones) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.note == note)
            v.pitchBendSemitones = semitones;
    }
}

void TripleOscillatorSynthNode::notePressure(std::uint8_t note, float pressure) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.note == note)
            v.pressure = pressure;
    }
}

void TripleOscillatorSynthNode::noteSlide(std::uint8_t note, float slide) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.note == note)
            v.slide = slide;
    }
}

std::size_t TripleOscillatorSynthNode::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& v : m_voices)
    {
        if (v.active)
            ++count;
    }
    return count;
}

void TripleOscillatorSynthNode::process(ProcessContext& context) noexcept
{
    // Consume MIDI events
    if (context.midi.events != nullptr)
    {
        for (std::size_t i = 0; i < context.midi.eventCount; ++i)
        {
            const auto& ev = context.midi.events[i];
            if (ev.size >= 2)
            {
                const auto status = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
                const auto note = static_cast<std::uint8_t>(ev.data[1]) & 0x7F;
                const auto vel = (ev.size >= 3) ? (static_cast<std::uint8_t>(ev.data[2]) & 0x7F) : std::uint8_t {0};

                if (status == 0x90 && vel > 0)
                {
                    noteOn(note, vel);
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

    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
        return;

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    // Clear output buffer
    std::fill_n(outL, numSamples, 0.0f);
    if (outR != nullptr)
        std::fill_n(outR, numSamples, 0.0f);

    const double invSr = 1.0 / m_sampleRate;

    for (auto& v : m_voices)
    {
        if (! v.active)
            continue;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            // Update Amp Envelope
            switch (v.ampStage)
            {
            case EnvStage::attack:
                v.ampLevel += m_ampAttackRate;
                if (v.ampLevel >= 1.0f)
                {
                    v.ampLevel = 1.0f;
                    v.ampStage = EnvStage::decay;
                }
                break;
            case EnvStage::decay:
                v.ampLevel = std::max(m_ampSustainLevel, v.ampLevel - m_ampDecayRate);
                if (v.ampLevel <= m_ampSustainLevel)
                {
                    v.ampLevel = m_ampSustainLevel;
                    v.ampStage = EnvStage::sustain;
                }
                break;
            case EnvStage::sustain:
                break;
            case EnvStage::release:
                v.ampLevel = std::max(0.0f, v.ampLevel - m_ampReleaseRate);
                if (v.ampLevel <= 0.0f)
                {
                    v.active = false;
                    v.ampStage = EnvStage::idle;
                }
                break;
            case EnvStage::idle:
                break;
            }

            if (! v.active)
                break;

            // Update Filter Envelope
            switch (v.filtStage)
            {
            case EnvStage::attack:
                v.filtLevel += m_filtAttackRate;
                if (v.filtLevel >= 1.0f)
                {
                    v.filtLevel = 1.0f;
                    v.filtStage = EnvStage::decay;
                }
                break;
            case EnvStage::decay:
                v.filtLevel = std::max(m_filtSustainLevel, v.filtLevel - m_filtDecayRate);
                if (v.filtLevel <= m_filtSustainLevel)
                {
                    v.filtLevel = m_filtSustainLevel;
                    v.filtStage = EnvStage::sustain;
                }
                break;
            case EnvStage::sustain:
                break;
            case EnvStage::release:
                v.filtLevel = std::max(0.0f, v.filtLevel - m_filtReleaseRate);
                break;
            case EnvStage::idle:
                break;
            }

            // Frequency calculation including pitch bend & slide
            const double totalPitchOffset = static_cast<double>(v.pitchBendSemitones) + static_cast<double>(v.slide) * 12.0;
            const double baseFreq = v.noteFreq * std::pow(2.0, totalPitchOffset / 12.0);

            // Compute per-oscillator frequencies
            std::array<double, oscCount> freqs {};
            for (std::size_t o = 0; o < oscCount; ++o)
            {
                const auto& cfg = m_oscConfigs[o];
                const double semitones = static_cast<double>(cfg.coarsePitchSemitones) + static_cast<double>(cfg.finePitchCents) * 0.01;
                freqs[o] = baseFreq * std::pow(2.0, semitones / 12.0);
            }

            // Cross-Modulation and Synthesis:
            // Osc 1
            float osc1Out = renderWaveform(v.phases[0], m_oscConfigs[0].waveform, m_noiseSeed);

            // Osc 2
            float osc2Out = renderWaveform(v.phases[1], m_oscConfigs[1].waveform, m_noiseSeed);
            if (m_oscConfigs[1].modMode == TripleOscModulation::am)
            {
                osc1Out *= (0.5f + 0.5f * osc2Out * m_oscConfigs[1].volume);
            }
            else if (m_oscConfigs[1].modMode == TripleOscModulation::fm)
            {
                freqs[0] += static_cast<double>(osc2Out * m_oscConfigs[1].volume) * 2000.0;
            }

            // Osc 3
            float osc3Out = renderWaveform(v.phases[2], m_oscConfigs[2].waveform, m_noiseSeed);
            if (m_oscConfigs[2].modMode == TripleOscModulation::am)
            {
                osc1Out *= (0.5f + 0.5f * osc3Out * m_oscConfigs[2].volume);
            }
            else if (m_oscConfigs[2].modMode == TripleOscModulation::fm)
            {
                freqs[0] += static_cast<double>(osc3Out * m_oscConfigs[2].volume) * 2000.0;
                freqs[1] += static_cast<double>(osc3Out * m_oscConfigs[2].volume) * 2000.0;
            }

            // Advance oscillator phases
            for (std::size_t o = 0; o < oscCount; ++o)
            {
                v.phases[o] += std::max(0.0, freqs[o]) * invSr;
                if (v.phases[o] >= 1.0)
                    v.phases[o] -= std::floor(v.phases[o]);
            }

            // Mix oscillators to stereo
            float sigL = 0.0f;
            float sigR = 0.0f;

            auto accumulateOsc = [&](float sample, const TripleOscConfig& cfg) {
                if (cfg.modMode != TripleOscModulation::mix && &cfg != &m_oscConfigs[0])
                    return; // AM/FM oscillators do not mix directly to master

                const float vol = cfg.volume;
                const float pL = std::clamp(1.0f - cfg.pan, 0.0f, 1.0f);
                const float pR = std::clamp(1.0f + cfg.pan, 0.0f, 1.0f);
                const float phaseScale = cfg.invertPhase ? -1.0f : 1.0f;

                sigL += sample * vol * pL;
                sigR += sample * vol * pR * phaseScale;
            };

            accumulateOsc(osc1Out, m_oscConfigs[0]);
            accumulateOsc(osc2Out, m_oscConfigs[1]);
            accumulateOsc(osc3Out, m_oscConfigs[2]);

            // SVF Filter cutoff with ADSR modulation
            const float modCutoff = std::clamp(
                m_filterCutoffHz * std::pow(2.0f, v.filtLevel * m_filtEnvAmount * 4.0f),
                20.0f,
                static_cast<float>(m_sampleRate * 0.45));

            const float f = 2.0f * std::sin(static_cast<float>(std::numbers::pi) * modCutoff * static_cast<float>(invSr));
            const float q = 1.0f / m_filterResonance;

            // Filter Left
            const float highL = sigL - v.svfLowL - q * v.svfBandL;
            v.svfBandL += f * highL;
            v.svfLowL += f * v.svfBandL;
            const float notchL = highL + v.svfLowL;

            // Filter Right
            const float highR = sigR - v.svfLowR - q * v.svfBandR;
            v.svfBandR += f * highR;
            v.svfLowR += f * v.svfBandR;
            const float notchR = highR + v.svfLowR;

            float filtOutL = v.svfLowL;
            float filtOutR = v.svfLowR;
            switch (m_filterType)
            {
            case TripleOscFilterType::lowpass:
                filtOutL = v.svfLowL;
                filtOutR = v.svfLowR;
                break;
            case TripleOscFilterType::highpass:
                filtOutL = highL;
                filtOutR = highR;
                break;
            case TripleOscFilterType::bandpass:
                filtOutL = v.svfBandL;
                filtOutR = v.svfBandR;
                break;
            case TripleOscFilterType::notch:
                filtOutL = notchL;
                filtOutR = notchR;
                break;
            }

            const float voiceGain = v.ampLevel * (static_cast<float>(v.velocity) / 127.0f) * 0.4f;
            outL[i] += filtOutL * voiceGain;
            if (outR != nullptr)
                outR[i] += filtOutR * voiceGain;
        }
    }
}

} // namespace dawg::engine::dsp
