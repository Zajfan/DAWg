#include <dawg/engine/dsp/WavetableSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

float WavetableData::sample(std::size_t frameIdx, double phase) const noexcept
{
    if (samples.empty() || frameCount == 0) {
        return 0.0f;
    }

    const std::size_t f = std::min(frameIdx, frameCount - 1);
    const double p = phase - std::floor(phase);
    const double samplePos = p * static_cast<double>(frameSize - 1);
    const auto idx0 = static_cast<std::size_t>(samplePos);
    const auto idx1 = std::min(idx0 + 1, frameSize - 1);
    const float frac = static_cast<float>(samplePos - static_cast<double>(idx0));

    const std::size_t base = f * frameSize;
    const float s0 = samples[base + idx0];
    const float s1 = samples[base + idx1];
    return s0 + frac * (s1 - s0);
}

float WavetableData::interpolate(double framePos, double phase) const noexcept
{
    if (samples.empty() || frameCount == 0) {
        return 0.0f;
    }

    const double clampedFrame = std::clamp(framePos, 0.0, static_cast<double>(frameCount - 1));
    const auto f0 = static_cast<std::size_t>(clampedFrame);
    const auto f1 = std::min(f0 + 1, frameCount - 1);
    const float fracF = static_cast<float>(clampedFrame - static_cast<double>(f0));

    const float s0 = sample(f0, phase);
    const float s1 = sample(f1, phase);
    return s0 + fracF * (s1 - s0);
}

float WavetableSynthNode::applySpectralWarp(float rawPhase, SpectralWarpMode mode, float amount) noexcept
{
    const float p = rawPhase - std::floor(rawPhase);
    if (amount <= 0.001f || mode == SpectralWarpMode::none) {
        return p;
    }

    switch (mode) {
    case SpectralWarpMode::none:
        return p;

    case SpectralWarpMode::sync: {
        const float syncMultiplier = 1.0f + amount * 7.0f;
        const float synched = p * syncMultiplier;
        return synched - std::floor(synched);
    }

    case SpectralWarpMode::pwm: {
        const float split = std::clamp(0.5f - amount * 0.45f, 0.05f, 0.5f);
        if (p < split) {
            return (p / split) * 0.5f;
        }
        return 0.5f + ((p - split) / (1.0f - split)) * 0.5f;
    }

    case SpectralWarpMode::wavefolder: {
        // Fold back around edges
        const float folded = std::sin(p * 2.0f * std::numbers::pi_v<float>) * (1.0f + amount * 3.0f);
        return 0.5f + 0.5f * std::clamp(folded, -1.0f, 1.0f);
    }

    case SpectralWarpMode::spectralBend: {
        const float exponent = std::exp((amount - 0.5f) * 4.0f);
        return std::pow(p, exponent);
    }
    }

    return p;
}

WavetableSynthNode::WavetableSynthNode() noexcept
{
    generateFactoryTables();
    reset();
    setAmpAdsr(5.0f, 100.0f, 0.7f, 200.0f);
}

void WavetableSynthNode::generateFactoryTables()
{
    // Generate 4 distinct rich multi-frame wavetables
    constexpr std::size_t frames = 64;
    constexpr std::size_t size = WavetableData::frameSize;

    for (std::size_t o = 0; o < oscCount; ++o) {
        auto& wt = m_wavetables[o];
        wt.frameCount = frames;
        wt.samples.resize(frames * size, 0.0f);

        for (std::size_t f = 0; f < frames; ++f) {
            const float morph = static_cast<float>(f) / static_cast<float>(frames - 1);
            const std::size_t base = f * size;

            for (std::size_t s = 0; s < size; ++s) {
                const double phase = (2.0 * std::numbers::pi_v<double> * static_cast<double>(s)) / static_cast<double>(size);
                float val = 0.0f;

                if (o == 0) {
                    // Table A: Pure Sine -> Rich Sawtooth morph
                    const float sine = static_cast<float>(std::sin(phase));
                    float saw = 0.0f;
                    for (int h = 1; h <= 16; ++h) {
                        saw += static_cast<float>(std::sin(static_cast<double>(h) * phase) / static_cast<double>(h));
                    }
                    saw *= (2.0f / std::numbers::pi_v<float>);
                    val = (1.0f - morph) * sine + morph * saw;
                } else if (o == 1) {
                    // Table B: Triangle -> Hard Square morph
                    const float tri = static_cast<float>((2.0 / std::numbers::pi_v<double>) * std::asin(std::sin(phase)));
                    const float sqr = (phase < std::numbers::pi_v<double>) ? 1.0f : -1.0f;
                    val = (1.0f - morph) * tri + morph * sqr;
                } else if (o == 2) {
                    // Table C: Modern Metallic / Comb harmonic stack
                    float comb = 0.0f;
                    const int harmonicCount = 4 + static_cast<int>(morph * 24.0f);
                    for (int h = 1; h <= harmonicCount; h += 2) {
                        comb += static_cast<float>(std::sin(static_cast<double>(h) * phase));
                    }
                    val = comb / std::sqrt(static_cast<float>(harmonicCount));
                } else {
                    // Table D: Formant Vowel morph (A -> O -> I)
                    const float form1 = static_cast<float>(std::sin((2.0 + morph * 4.0) * phase));
                    const float form2 = static_cast<float>(std::sin((6.0 + (1.0f - morph) * 5.0) * phase));
                    val = 0.6f * form1 + 0.4f * form2;
                }

                wt.samples[base + s] = std::clamp(val, -1.0f, 1.0f);
            }
        }
    }
}

bool WavetableSynthNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    setAmpAdsr(5.0f, 100.0f, 0.7f, 200.0f);
    return true;
}

void WavetableSynthNode::reset() noexcept
{
    for (auto& v : m_voices) {
        v.active = false;
        v.ampStage = EnvStage::idle;
        v.ampLevel = 0.0f;
        v.oscPhases.fill(0.0);
        v.subPhase = 0.0;
        v.svfIc1eqL = 0.0f;
        v.svfIc2eqL = 0.0f;
        v.svfIc1eqR = 0.0f;
        v.svfIc2eqR = 0.0f;
    }
    m_orbitalPhase = 0.0f;
}

void WavetableSynthNode::setMorphCoordinates(float x, float y) noexcept
{
    m_morphX = std::clamp(x, -1.0f, 1.0f);
    m_morphY = std::clamp(y, -1.0f, 1.0f);
}

void WavetableSynthNode::setOrbitalModulation(bool enabled, float rateHz, float radius) noexcept
{
    m_orbitalEnabled = enabled;
    m_orbitalRateHz = std::clamp(rateHz, 0.01f, 20.0f);
    m_orbitalRadius = std::clamp(radius, 0.0f, 1.0f);
}

void WavetableSynthNode::setOscConfig(std::size_t oscIdx, const WavetableOscConfig& cfg) noexcept
{
    if (oscIdx < oscCount) {
        m_oscConfigs[oscIdx] = cfg;
    }
}

const WavetableOscConfig& WavetableSynthNode::oscConfig(std::size_t oscIdx) const noexcept
{
    static const WavetableOscConfig kDefaultCfg {};
    if (oscIdx < oscCount) {
        return m_oscConfigs[oscIdx];
    }
    return kDefaultCfg;
}

void WavetableSynthNode::setSubOsc(bool enabled, SubOscWaveform wave, int octaveDown, float level) noexcept
{
    m_subEnabled = enabled;
    m_subWave = wave;
    m_subOctaveDown = std::clamp(octaveDown, 1, 2);
    m_subLevel = std::clamp(level, 0.0f, 1.0f);
}

void WavetableSynthNode::setFilter(WavetableFilterMode mode, float cutoffHz, float resonance, float drive) noexcept
{
    m_filterMode = mode;
    m_cutoffHz = std::clamp(cutoffHz, 20.0f, 20000.0f);
    m_resonance = std::clamp(resonance, 0.5f, 20.0f);
    m_filterDrive = std::clamp(drive, 0.0f, 1.0f);
}

void WavetableSynthNode::setAmpAdsr(float aMs, float dMs, float sLevel, float rMs) noexcept
{
    const double aSamp = (std::max(aMs, 0.5f) * 0.001) * m_sampleRate;
    m_attackRate = static_cast<float>(1.0 / aSamp);

    const double dSamp = (std::max(dMs, 1.0f) * 0.001) * m_sampleRate;
    m_decayRate = static_cast<float>(1.0 / dSamp);

    m_sustainLevel = std::clamp(sLevel, 0.0f, 1.0f);

    const double rSamp = (std::max(rMs, 1.0f) * 0.001) * m_sampleRate;
    m_releaseRate = static_cast<float>(1.0 / rSamp);
}

void WavetableSynthNode::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept
{
    if (velocity == 0) {
        noteOff(note);
        return;
    }

    Voice* target = nullptr;
    for (auto& v : m_voices) {
        if (!v.active) {
            target = &v;
            break;
        }
    }
    if (target == nullptr) {
        target = &m_voices[0];
    }

    target->active = true;
    target->note = note;
    target->velocity = velocity;
    target->noteFrequency = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
    target->oscPhases.fill(0.0);
    target->subPhase = 0.0;
    target->ampStage = EnvStage::attack;
    target->ampLevel = 0.0f;
}

void WavetableSynthNode::noteOff(std::uint8_t note) noexcept
{
    for (auto& v : m_voices) {
        if (v.active && v.note == note && v.ampStage != EnvStage::release) {
            v.ampStage = EnvStage::release;
        }
    }
}

void WavetableSynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices) {
        if (v.active) {
            v.ampStage = EnvStage::release;
        }
    }
}

std::size_t WavetableSynthNode::activeVoiceCount() const noexcept
{
    std::size_t c = 0;
    for (const auto& v : m_voices) {
        if (v.active) ++c;
    }
    return c;
}

void WavetableSynthNode::process(ProcessContext& context) noexcept
{
    // Handle MIDI events
    if (context.midi.events != nullptr) {
        for (std::size_t i = 0; i < context.midi.eventCount; ++i) {
            const auto& ev = context.midi.events[i];
            if (ev.size >= 2) {
                const auto status = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
                const auto note = static_cast<std::uint8_t>(ev.data[1]) & 0x7F;
                const auto vel = (ev.size >= 3) ? (static_cast<std::uint8_t>(ev.data[2]) & 0x7F) : std::uint8_t {0};

                if (status == 0x90 && vel > 0) {
                    noteOn(note, vel);
                } else if (status == 0x80 || (status == 0x90 && vel == 0)) {
                    noteOff(note);
                }
            }
        }
    }

    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0) {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = numChannels > 1 ? audio.channels[1] : nullptr;

    const float orbitalPhaseInc = (2.0f * std::numbers::pi_v<float> * m_orbitalRateHz) / static_cast<float>(m_sampleRate);

    // SVF filter pre-calculations
    const float fcClamped = std::clamp(m_cutoffHz, 20.0f, static_cast<float>(m_sampleRate * 0.45));
    const float g = std::tan(std::numbers::pi_v<float> * fcClamped / static_cast<float>(m_sampleRate));
    const float k = 1.0f / m_resonance;
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;

    for (std::size_t s = 0; s < numSamples; ++s) {
        // Calculate dynamic 2D morph coordinates
        float effectiveX = m_morphX;
        float effectiveY = m_morphY;

        if (m_orbitalEnabled) {
            effectiveX += m_orbitalRadius * std::cos(m_orbitalPhase);
            effectiveY += m_orbitalRadius * std::sin(m_orbitalPhase);
            effectiveX = std::clamp(effectiveX, -1.0f, 1.0f);
            effectiveY = std::clamp(effectiveY, -1.0f, 1.0f);

            m_orbitalPhase += orbitalPhaseInc;
            if (m_orbitalPhase >= 2.0f * std::numbers::pi_v<float>) {
                m_orbitalPhase -= 2.0f * std::numbers::pi_v<float>;
            }
        }

        // 2D Quad plane bilinear weights
        const float u = 0.5f * (effectiveX + 1.0f);
        const float v = 0.5f * (effectiveY + 1.0f);
        const std::array<float, oscCount> morphWeights = {
            (1.0f - u) * v,          // Osc A: Top-Left
            u * v,                   // Osc B: Top-Right
            (1.0f - u) * (1.0f - v), // Osc C: Bottom-Left
            u * (1.0f - v)           // Osc D: Bottom-Right
        };

        float mixSampleL = 0.0f;
        float mixSampleR = 0.0f;

        for (auto& voice : m_voices) {
            if (!voice.active) {
                continue;
            }

            // Envelope advance
            switch (voice.ampStage) {
            case EnvStage::idle:
                voice.active = false;
                continue;
            case EnvStage::attack:
                voice.ampLevel += m_attackRate;
                if (voice.ampLevel >= 1.0f) {
                    voice.ampLevel = 1.0f;
                    voice.ampStage = EnvStage::decay;
                }
                break;
            case EnvStage::decay:
                voice.ampLevel -= m_decayRate;
                if (voice.ampLevel <= m_sustainLevel) {
                    voice.ampLevel = m_sustainLevel;
                    voice.ampStage = EnvStage::sustain;
                }
                break;
            case EnvStage::sustain:
                voice.ampLevel = m_sustainLevel;
                break;
            case EnvStage::release:
                voice.ampLevel -= m_releaseRate;
                if (voice.ampLevel <= 0.0f) {
                    voice.ampLevel = 0.0f;
                    voice.ampStage = EnvStage::idle;
                    voice.active = false;
                    continue;
                }
                break;
            }

            const float velNorm = static_cast<float>(voice.velocity) / 127.0f;
            const float voiceGain = voice.ampLevel * (0.3f + 0.7f * velNorm);

            float voiceAudioL = 0.0f;
            float voiceAudioR = 0.0f;

            // 1. Synthesize 4 Wavetable Oscillators
            for (std::size_t o = 0; o < oscCount; ++o) {
                const auto& cfg = m_oscConfigs[o];
                const float w = morphWeights[o];
                if (w <= 0.001f || cfg.level <= 0.001f) {
                    continue;
                }

                const double semitones = cfg.semitones + (cfg.fineTuneCents / 100.0);
                const double oscFreq = voice.noteFrequency * std::pow(2.0, semitones / 12.0);
                const double phaseInc = oscFreq / m_sampleRate;

                const float warpedPhase = applySpectralWarp(
                    static_cast<float>(voice.oscPhases[o]), cfg.warpMode, cfg.warpAmount);

                const auto& wt = m_wavetables[o];
                const double framePos = static_cast<double>(cfg.tablePos) * static_cast<double>(wt.frameCount - 1);
                const float oscSamp = wt.interpolate(framePos, warpedPhase) * cfg.level * w;

                const float panL = std::clamp(0.5f * (1.0f - cfg.pan), 0.0f, 1.0f);
                const float panR = std::clamp(0.5f * (1.0f + cfg.pan), 0.0f, 1.0f);

                voiceAudioL += oscSamp * panL;
                voiceAudioR += oscSamp * panR;

                voice.oscPhases[o] += phaseInc;
                if (voice.oscPhases[o] >= 1.0) voice.oscPhases[o] -= 1.0;
            }

            // 2. Sub-Oscillator
            if (m_subEnabled && m_subLevel > 0.001f) {
                const double subFreq = voice.noteFrequency / std::pow(2.0, m_subOctaveDown);
                const double subPhaseInc = subFreq / m_sampleRate;

                float subSamp = 0.0f;
                switch (m_subWave) {
                case SubOscWaveform::sine:
                    subSamp = static_cast<float>(std::sin(2.0 * std::numbers::pi_v<double> * voice.subPhase));
                    break;
                case SubOscWaveform::triangle:
                    subSamp = static_cast<float>((2.0 / std::numbers::pi_v<double>) * std::asin(std::sin(2.0 * std::numbers::pi_v<double> * voice.subPhase)));
                    break;
                case SubOscWaveform::square:
                    subSamp = (voice.subPhase < 0.5) ? 1.0f : -1.0f;
                    break;
                case SubOscWaveform::saw:
                    subSamp = static_cast<float>(2.0 * voice.subPhase - 1.0);
                    break;
                }

                subSamp *= m_subLevel;
                voiceAudioL += subSamp;
                voiceAudioR += subSamp;

                voice.subPhase += subPhaseInc;
                if (voice.subPhase >= 1.0) voice.subPhase -= 1.0;
            }

            // 3. Noise injection
            if (m_noiseLevel > 0.001f) {
                m_noiseSeed = m_noiseSeed * 1664525u + 1013904223u;
                const float noise = (static_cast<float>(static_cast<std::int32_t>(m_noiseSeed)) / 2147483648.0f) * m_noiseLevel;
                voiceAudioL += noise;
                voiceAudioR += noise;
            }

            // 4. State Variable Filter processing
            auto processSvf = [&](float in, float& ic1, float& ic2) -> float {
                if (m_filterDrive > 0.01f) {
                    in = std::tanh(in * (1.0f + m_filterDrive * 2.0f));
                }
                const float v3 = in - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;

                switch (m_filterMode) {
                case WavetableFilterMode::lowpass:  return v2;
                case WavetableFilterMode::bandpass: return v1;
                case WavetableFilterMode::highpass: return in - k * v1 - v2;
                case WavetableFilterMode::notch:    return in - k * v1;
                }
                return v2;
            };

            voiceAudioL = processSvf(voiceAudioL, voice.svfIc1eqL, voice.svfIc2eqL);
            voiceAudioR = processSvf(voiceAudioR, voice.svfIc1eqR, voice.svfIc2eqR);

            mixSampleL += voiceAudioL * voiceGain;
            mixSampleR += voiceAudioR * voiceGain;
        }

        outL[s] += mixSampleL;
        if (outR != nullptr) {
            outR[s] += mixSampleR;
        }
    }
}

} // namespace dawg::engine::dsp
