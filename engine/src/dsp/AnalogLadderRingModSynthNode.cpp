#include <dawg/engine/dsp/AnalogLadderRingModSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

float AnalogLadderRingModSynthNode::renderWaveform(double phase, AnalogOscWaveform wave, float pw) noexcept
{
    const double p = phase - std::floor(phase);

    switch (wave) {
    case AnalogOscWaveform::triangle: {
        return static_cast<float>((2.0 / std::numbers::pi_v<double>) * std::asin(std::sin(2.0 * std::numbers::pi_v<double> * p)));
    }
    case AnalogOscWaveform::sawtooth: {
        return static_cast<float>(2.0 * p - 1.0);
    }
    case AnalogOscWaveform::square: {
        return (p < 0.5) ? 1.0f : -1.0f;
    }
    case AnalogOscWaveform::pulse: {
        const double clampedPw = std::clamp(static_cast<double>(pw), 0.05, 0.95);
        return (p < clampedPw) ? 1.0f : -1.0f;
    }
    case AnalogOscWaveform::sine: {
        return static_cast<float>(std::sin(2.0 * std::numbers::pi_v<double> * p));
    }
    }

    return 0.0f;
}

float AnalogLadderRingModSynthNode::diodeRingModulate(float carrier, float modulator, float diodeThresh) noexcept
{
    // Germanium diode bridge model
    const float vA = carrier + modulator;
    const float vB = carrier - modulator;

    auto diodeConduct = [diodeThresh](float v) -> float {
        const float sign = v >= 0.0f ? 1.0f : -1.0f;
        const float absV = std::abs(v);
        if (absV <= diodeThresh) {
            return 0.0f;
        }
        return sign * (absV - diodeThresh);
    };

    const float out = 0.5f * (diodeConduct(vA) - diodeConduct(vB));
    return std::tanh(out * 1.5f);
}

AnalogLadderRingModSynthNode::AnalogLadderRingModSynthNode() noexcept
{
    m_oscConfigs[0] = AnalogOscConfig {.waveform = AnalogOscWaveform::sawtooth, .octave = 0, .semitones = 0.0f, .fineTuneCents = 0.0f, .pulseWidth = 0.5f, .level = 0.8f, .syncToOsc1 = false};
    m_oscConfigs[1] = AnalogOscConfig {.waveform = AnalogOscWaveform::square,   .octave = 0, .semitones = 7.0f, .fineTuneCents = 4.0f, .pulseWidth = 0.5f, .level = 0.7f, .syncToOsc1 = true};
    m_oscConfigs[2] = AnalogOscConfig {.waveform = AnalogOscWaveform::triangle, .octave = -1, .semitones = 0.0f, .fineTuneCents = -4.0f, .pulseWidth = 0.5f, .level = 0.6f, .syncToOsc1 = false};

    setAmpAdsr(5.0f, 150.0f, 0.7f, 250.0f);
    setFilterAdsr(10.0f, 200.0f, 0.3f, 300.0f, 0.5f);
    reset();
}

bool AnalogLadderRingModSynthNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    setAmpAdsr(5.0f, 150.0f, 0.7f, 250.0f);
    setFilterAdsr(10.0f, 200.0f, 0.3f, 300.0f, 0.5f);
    return true;
}

void AnalogLadderRingModSynthNode::reset() noexcept
{
    for (auto& v : m_voices) {
        v.active = false;
        v.ampStage = EnvStage::idle;
        v.filtStage = EnvStage::idle;
        v.ampLevel = 0.0f;
        v.filtLevel = 0.0f;
        v.phases.fill(0.0);
        v.transStatesL.fill(0.0f);
        v.transStatesR.fill(0.0f);
        v.diodeStatesL.fill(0.0f);
        v.diodeStatesR.fill(0.0f);
        v.feedbackPrevL = 0.0f;
        v.feedbackPrevR = 0.0f;
    }
}

void AnalogLadderRingModSynthNode::setOscConfig(std::size_t oscIdx, const AnalogOscConfig& cfg) noexcept
{
    if (oscIdx < oscCount) {
        m_oscConfigs[oscIdx] = cfg;
    }
}

const AnalogOscConfig& AnalogLadderRingModSynthNode::oscConfig(std::size_t oscIdx) const noexcept
{
    static const AnalogOscConfig kDef {};
    if (oscIdx < oscCount) {
        return m_oscConfigs[oscIdx];
    }
    return kDef;
}

void AnalogLadderRingModSynthNode::setTransistorLadder(float cutoffHz, float resonance, float drive) noexcept
{
    m_transistorCutoffHz = std::clamp(cutoffHz, 20.0f, 20000.0f);
    m_transistorResonance = std::clamp(resonance, 0.0f, 3.8f);
    m_transistorDrive = std::clamp(drive, 0.0f, 2.0f);
}

void AnalogLadderRingModSynthNode::setDiodeLadder(float cutoffHz, float resonance) noexcept
{
    m_diodeCutoffHz = std::clamp(cutoffHz, 20.0f, 20000.0f);
    m_diodeResonance = std::clamp(resonance, 0.0f, 3.5f);
}

void AnalogLadderRingModSynthNode::setAmpAdsr(float aMs, float dMs, float sLevel, float rMs) noexcept
{
    const double aSamp = (std::max(aMs, 0.5f) * 0.001) * m_sampleRate;
    m_ampAttackRate = static_cast<float>(1.0 / aSamp);

    const double dSamp = (std::max(dMs, 1.0f) * 0.001) * m_sampleRate;
    m_ampDecayRate = static_cast<float>(1.0 / dSamp);

    m_ampSustainLevel = std::clamp(sLevel, 0.0f, 1.0f);

    const double rSamp = (std::max(rMs, 1.0f) * 0.001) * m_sampleRate;
    m_ampReleaseRate = static_cast<float>(1.0 / rSamp);
}

void AnalogLadderRingModSynthNode::setFilterAdsr(float aMs, float dMs, float sLevel, float rMs, float envAmount) noexcept
{
    const double aSamp = (std::max(aMs, 0.5f) * 0.001) * m_sampleRate;
    m_filtAttackRate = static_cast<float>(1.0 / aSamp);

    const double dSamp = (std::max(dMs, 1.0f) * 0.001) * m_sampleRate;
    m_filtDecayRate = static_cast<float>(1.0 / dSamp);

    m_filtSustainLevel = std::clamp(sLevel, 0.0f, 1.0f);

    const double rSamp = (std::max(rMs, 1.0f) * 0.001) * m_sampleRate;
    m_filtReleaseRate = static_cast<float>(1.0 / rSamp);

    m_filtEnvAmount = std::clamp(envAmount, -1.0f, 1.0f);
}

void AnalogLadderRingModSynthNode::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept
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
    target->noteFreq = 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
    target->phases.fill(0.0);

    target->ampStage = EnvStage::attack;
    target->ampLevel = 0.0f;

    target->filtStage = EnvStage::attack;
    target->filtLevel = 0.0f;
}

void AnalogLadderRingModSynthNode::noteOff(std::uint8_t note) noexcept
{
    for (auto& v : m_voices) {
        if (v.active && v.note == note) {
            if (v.ampStage != EnvStage::release) v.ampStage = EnvStage::release;
            if (v.filtStage != EnvStage::release) v.filtStage = EnvStage::release;
        }
    }
}

void AnalogLadderRingModSynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices) {
        if (v.active) {
            v.ampStage = EnvStage::release;
            v.filtStage = EnvStage::release;
        }
    }
}

std::size_t AnalogLadderRingModSynthNode::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& v : m_voices) {
        if (v.active) ++count;
    }
    return count;
}

void AnalogLadderRingModSynthNode::process(ProcessContext& context) noexcept
{
    // MIDI events
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

    for (std::size_t s = 0; s < numSamples; ++s) {
        float mixL = 0.0f;
        float mixR = 0.0f;

        for (auto& voice : m_voices) {
            if (!voice.active) {
                continue;
            }

            // Amp Envelope
            switch (voice.ampStage) {
            case EnvStage::idle:
                voice.active = false;
                continue;
            case EnvStage::attack:
                voice.ampLevel += m_ampAttackRate;
                if (voice.ampLevel >= 1.0f) {
                    voice.ampLevel = 1.0f;
                    voice.ampStage = EnvStage::decay;
                }
                break;
            case EnvStage::decay:
                voice.ampLevel -= m_ampDecayRate;
                if (voice.ampLevel <= m_ampSustainLevel) {
                    voice.ampLevel = m_ampSustainLevel;
                    voice.ampStage = EnvStage::sustain;
                }
                break;
            case EnvStage::sustain:
                voice.ampLevel = m_ampSustainLevel;
                break;
            case EnvStage::release:
                voice.ampLevel -= m_ampReleaseRate;
                if (voice.ampLevel <= 0.0f) {
                    voice.ampLevel = 0.0f;
                    voice.ampStage = EnvStage::idle;
                    voice.active = false;
                    continue;
                }
                break;
            }

            // Filter Envelope
            switch (voice.filtStage) {
            case EnvStage::idle:
                voice.filtLevel = 0.0f;
                break;
            case EnvStage::attack:
                voice.filtLevel += m_filtAttackRate;
                if (voice.filtLevel >= 1.0f) {
                    voice.filtLevel = 1.0f;
                    voice.filtStage = EnvStage::decay;
                }
                break;
            case EnvStage::decay:
                voice.filtLevel -= m_filtDecayRate;
                if (voice.filtLevel <= m_filtSustainLevel) {
                    voice.filtLevel = m_filtSustainLevel;
                    voice.filtStage = EnvStage::sustain;
                }
                break;
            case EnvStage::sustain:
                voice.filtLevel = m_filtSustainLevel;
                break;
            case EnvStage::release:
                voice.filtLevel -= m_filtReleaseRate;
                if (voice.filtLevel <= 0.0f) {
                    voice.filtLevel = 0.0f;
                    voice.filtStage = EnvStage::idle;
                }
                break;
            }

            // 1. Synthesize 3 Oscillators
            std::array<float, oscCount> oscSamps {};

            for (std::size_t o = 0; o < oscCount; ++o) {
                const auto& cfg = m_oscConfigs[o];
                const double octMultiplier = std::pow(2.0, cfg.octave);
                const double semiMultiplier = std::pow(2.0, (cfg.semitones + cfg.fineTuneCents / 100.0) / 12.0);
                const double freq = voice.noteFreq * octMultiplier * semiMultiplier;
                const double inc = freq / m_sampleRate;

                // Hard sync Osc 2 to Osc 1
                if (cfg.syncToOsc1 && o == 1) {
                    if (voice.phases[0] < inc) {
                        voice.phases[1] = 0.0;
                    }
                }

                oscSamps[o] = renderWaveform(voice.phases[o], cfg.waveform, cfg.pulseWidth) * cfg.level;

                voice.phases[o] += inc;
                if (voice.phases[o] >= 1.0) voice.phases[o] -= 1.0;
            }

            // 2. Germanium Diode Ring Modulation (Osc 1 Carrier x Osc 2 Modulator)
            float preFilterAudio = oscSamps[0] + oscSamps[1] + oscSamps[2];
            if (m_ringModMix > 0.001f) {
                const float ringModSignal = diodeRingModulate(oscSamps[0], oscSamps[1], m_diodeThreshold);
                preFilterAudio = (1.0f - m_ringModMix) * preFilterAudio + m_ringModMix * ringModSignal;
            }

            // 3. Headphone Overdrive Feedback Loop
            preFilterAudio += m_feedbackAmount * std::tanh(voice.feedbackPrevL * 1.5f);

            // 4. Modulated Filter Cutoffs
            const float envOffset = voice.filtLevel * m_filtEnvAmount * 6000.0f;
            const float fcTrans = std::clamp(m_transistorCutoffHz + envOffset, 20.0f, static_cast<float>(m_sampleRate * 0.45));
            const float fcDiode = std::clamp(m_diodeCutoffHz + envOffset, 20.0f, static_cast<float>(m_sampleRate * 0.45));

            // Transistor Ladder (Moog 4-Pole 24 dB/oct non-linear)
            auto processTransLadder = [&](float in, std::array<float, 4>& states) -> float {
                const float g = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fcTrans / static_cast<float>(m_sampleRate));
                const float k = m_transistorResonance * 1.05f;

                const float driveFactor = 1.0f + m_transistorDrive * 2.0f;
                const float u = std::tanh((in * driveFactor) - k * states[3]);

                states[0] += g * (std::tanh(u) - std::tanh(states[0]));
                states[1] += g * (std::tanh(states[0]) - std::tanh(states[1]));
                states[2] += g * (std::tanh(states[1]) - std::tanh(states[2]));
                states[3] += g * (std::tanh(states[2]) - std::tanh(states[3]));

                return states[3];
            };

            // Diode Ladder (TB-303 / EMS 3-Pole 18 dB/oct)
            auto processDiodeLadder = [&](float in, std::array<float, 3>& states) -> float {
                const float g = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * fcDiode / static_cast<float>(m_sampleRate) * 0.9f);
                const float k = m_diodeResonance * 0.95f;

                const float u = std::tanh(in - k * states[2]);
                states[0] += g * (u - states[0]);
                states[1] += g * (states[0] - states[1]);
                states[2] += g * (states[1] - states[2]);

                return states[2];
            };

            float filteredL = 0.0f;
            float filteredR = 0.0f;

            if (m_routing == DualFilterRouting::series) {
                const float stage1L = processTransLadder(preFilterAudio, voice.transStatesL);
                const float stage1R = processTransLadder(preFilterAudio, voice.transStatesR);

                filteredL = processDiodeLadder(stage1L, voice.diodeStatesL);
                filteredR = processDiodeLadder(stage1R, voice.diodeStatesR);
            } else {
                // Parallel
                const float transL = processTransLadder(preFilterAudio, voice.transStatesL);
                const float transR = processTransLadder(preFilterAudio, voice.transStatesR);

                const float diodeL = processDiodeLadder(preFilterAudio, voice.diodeStatesL);
                const float diodeR = processDiodeLadder(preFilterAudio, voice.diodeStatesR);

                filteredL = (1.0f - m_filterBlend) * transL + m_filterBlend * diodeL;
                filteredR = (1.0f - m_filterBlend) * transR + m_filterBlend * diodeR;
            }

            voice.feedbackPrevL = filteredL;
            voice.feedbackPrevR = filteredR;

            const float velNorm = static_cast<float>(voice.velocity) / 127.0f;
            const float voiceVol = voice.ampLevel * (0.3f + 0.7f * velNorm);

            mixL += filteredL * voiceVol;
            mixR += filteredR * voiceVol;
        }

        outL[s] += mixL;
        if (outR != nullptr) {
            outR[s] += mixR;
        }
    }
}

} // namespace dawg::engine::dsp
