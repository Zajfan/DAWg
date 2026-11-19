#include <dawg/engine/dsp/VirusTiPolarSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

namespace {

float midiToHz(const std::uint8_t note) noexcept
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

} // namespace

VirusTiPolarSynthNode::VirusTiPolarSynthNode() noexcept
{
    m_delayBufferL.assign(kDelayBufSize, 0.0f);
    m_delayBufferR.assign(kDelayBufSize, 0.0f);
    loadPatch(m_patch);
}

bool VirusTiPolarSynthNode::prepare(const double sampleRate,
                                    const std::size_t /*maxBlockSize*/,
                                    const std::uint16_t /*channelCount*/)
{
    m_sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    for (auto& v : m_voices)
    {
        v.filter1.reset();
        v.filter2.reset();
    }
    reset();
    return true;
}

void VirusTiPolarSynthNode::reset() noexcept
{
    allNotesOff();
    m_delayWritePos = 0;
    std::fill(m_delayBufferL.begin(), m_delayBufferL.end(), 0.0f);
    std::fill(m_delayBufferR.begin(), m_delayBufferR.end(), 0.0f);
    m_reverbCombsL.fill(0.0f);
    m_reverbCombsR.fill(0.0f);
    m_lfo1Phase = 0.0f;
    m_lfo2Phase = 0.0f;
    m_lastOutputSample = 0.0f;
}

std::size_t VirusTiPolarSynthNode::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& v : m_voices)
    {
        if (v.active) ++count;
    }
    return count;
}

void VirusTiPolarSynthNode::noteOn(const std::uint8_t note, const float velocity) noexcept
{
    // Find free voice or steal oldest/quietest
    Voice* target = nullptr;
    for (auto& v : m_voices)
    {
        if (! v.active) { target = &v; break; }
    }
    if (target == nullptr)
    {
        target = &m_voices[0];
    }

    target->active = true;
    target->note = note;
    target->velocity = std::clamp(velocity, 0.05f, 1.0f);
    target->targetFreq = midiToHz(note);
    target->ampStage = Voice::Stage::attack;
    target->ampLevel = 0.0f;
    target->filtStage = Voice::Stage::attack;
    target->filtLevel = 0.0f;

    // Distribute 9 Hypersaw sub-oscillators across stereo field with detune
    const float detuneNorm = m_patch.hypersawDetune * 0.06f;
    const float spread = m_patch.hypersawSpread;

    constexpr std::array<float, 9> kDetuneRatios = {
        0.0f, -0.4f, 0.4f, -0.7f, 0.7f, -1.0f, 1.0f, -1.35f, 1.35f
    };
    constexpr std::array<float, 9> kPanRatios = {
        0.0f, -0.3f, 0.3f, -0.6f, 0.6f, -0.9f, 0.9f, -1.0f, 1.0f
    };

    for (std::size_t s = 0; s < 9; ++s)
    {
        target->osc1Phases[s] = static_cast<float>(s) * 0.11f;
        target->osc1FreqOffsets[s] = 1.0f + kDetuneRatios[s] * detuneNorm;
        target->osc1PanWeights[s] = kPanRatios[s] * spread;
    }

    target->osc2Phase = 0.0f;
    target->subPhase = 0.0f;
    target->analogDriftOffset = ((static_cast<float>(note % 7) - 3.0f) * 0.001f) * m_patch.analogDrift;

    target->filter1.reset();
    target->filter2.reset();
}

void VirusTiPolarSynthNode::noteOff(const std::uint8_t note) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.note == note && v.ampStage != Voice::Stage::release)
        {
            v.ampStage = Voice::Stage::release;
            v.filtStage = Voice::Stage::release;
        }
    }
}

void VirusTiPolarSynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        v.ampStage = Voice::Stage::idle;
        v.filtStage = Voice::Stage::idle;
    }
}

float VirusTiPolarSynthNode::evaluateShape(const float phase, const float shape) const noexcept
{
    // 0 = Saw, 1 = Square/Pulse
    const float saw = 2.0f * phase - 1.0f;
    const float pulse = (phase < 0.5f) ? 1.0f : -1.0f;
    return saw * (1.0f - shape) + pulse * shape;
}

float VirusTiPolarSynthNode::applyDistortion(const float in, const VirusDistortionType type, const float drive) const noexcept
{
    if (type == VirusDistortionType::off) return in;

    const float driven = in * (1.0f + drive * 4.0f);
    switch (type)
    {
    case VirusDistortionType::off:
        return in;
    case VirusDistortionType::softClip:
        return std::tanh(driven);
    case VirusDistortionType::hardClip:
        return std::clamp(driven, -0.95f, 0.95f);
    case VirusDistortionType::rectifier:
        return std::abs(driven) * 1.4f - 0.7f;
    case VirusDistortionType::tubeCurve:
        return (driven > 0.0f) ? std::tanh(driven) : std::tanh(driven * 0.6f);
    case VirusDistortionType::bitCrush:
    {
        constexpr float levels = 16.0f;
        return std::round(driven * levels) / levels;
    }
    }
    return std::tanh(driven);
}

void VirusTiPolarSynthNode::processVoice(Voice& v, float& outL, float& outR) noexcept
{
    if (! v.active) return;

    const float dt = 1.0f / static_cast<float>(m_sampleRate);

    // 1. Envelopes processing
    // Amp Envelope
    switch (v.ampStage)
    {
    case Voice::Stage::idle:
        v.active = false;
        return;
    case Voice::Stage::attack:
        v.ampLevel += dt / std::max(0.001f, m_patch.ampAttack);
        if (v.ampLevel >= 1.0f) { v.ampLevel = 1.0f; v.ampStage = Voice::Stage::decay; }
        break;
    case Voice::Stage::decay:
        v.ampLevel -= (dt / std::max(0.001f, m_patch.ampDecay)) * (1.0f - m_patch.ampSustain);
        if (v.ampLevel <= m_patch.ampSustain) { v.ampLevel = m_patch.ampSustain; v.ampStage = Voice::Stage::sustain; }
        break;
    case Voice::Stage::sustain:
        v.ampLevel = m_patch.ampSustain;
        break;
    case Voice::Stage::release:
        v.ampLevel -= dt / std::max(0.001f, m_patch.ampRelease);
        if (v.ampLevel <= 0.0001f) { v.ampLevel = 0.0f; v.ampStage = Voice::Stage::idle; v.active = false; return; }
        break;
    }

    // Filter Envelope
    switch (v.filtStage)
    {
    case Voice::Stage::idle:
        v.filtLevel = 0.0f;
        break;
    case Voice::Stage::attack:
        v.filtLevel += dt / std::max(0.001f, m_patch.filtAttack);
        if (v.filtLevel >= 1.0f) { v.filtLevel = 1.0f; v.filtStage = Voice::Stage::decay; }
        break;
    case Voice::Stage::decay:
        v.filtLevel -= (dt / std::max(0.001f, m_patch.filtDecay)) * (1.0f - m_patch.filtSustain);
        if (v.filtLevel <= m_patch.filtSustain) { v.filtLevel = m_patch.filtSustain; v.filtStage = Voice::Stage::sustain; }
        break;
    case Voice::Stage::sustain:
        v.filtLevel = m_patch.filtSustain;
        break;
    case Voice::Stage::release:
        v.filtLevel -= dt / std::max(0.001f, m_patch.filtRelease);
        if (v.filtLevel <= 0.0f) v.filtLevel = 0.0f;
        break;
    }

    // 2. Oscillators
    const float baseFreq = v.targetFreq * (1.0f + v.analogDriftOffset);
    float sigL = 0.0f;
    float sigR = 0.0f;

    // Oscillator 1: Hypersaw mode
    const int numSaws = std::clamp(static_cast<int>(m_patch.hypersawDensity), 1, 9);
    for (int s = 0; s < numSaws; ++s)
    {
        const float sawFreq = baseFreq * v.osc1FreqOffsets[static_cast<std::size_t>(s)];
        v.osc1Phases[static_cast<std::size_t>(s)] += sawFreq * dt;
        if (v.osc1Phases[static_cast<std::size_t>(s)] >= 1.0f)
            v.osc1Phases[static_cast<std::size_t>(s)] -= 1.0f;

        const float saw = 2.0f * v.osc1Phases[static_cast<std::size_t>(s)] - 1.0f;
        const float pan = v.osc1PanWeights[static_cast<std::size_t>(s)];
        sigL += saw * (1.0f - pan * 0.5f);
        sigR += saw * (1.0f + pan * 0.5f);
    }
    const float osc1Norm = 1.0f / std::sqrt(static_cast<float>(numSaws));
    sigL *= osc1Norm;
    sigR *= osc1Norm;

    // Oscillator 2: with detune & FM
    const float osc2Freq = baseFreq * std::pow(2.0f, (m_patch.osc2Semitone + m_patch.osc2DetuneCents / 100.0f) / 12.0f);
    v.osc2Phase += osc2Freq * dt;
    if (v.osc2Phase >= 1.0f) v.osc2Phase -= 1.0f;
    const float osc2Sig = evaluateShape(v.osc2Phase, 0.0f);

    // Sub-Oscillator: 1 octave below
    v.subPhase += (baseFreq * 0.5f) * dt;
    if (v.subPhase >= 1.0f) v.subPhase -= 1.0f;
    const float subSig = (v.subPhase < 0.5f) ? 0.7f : -0.7f;

    // Mix Oscillators
    const float mixOsc = (sigL + sigR) * 0.5f * (1.0f - m_patch.oscMix)
                       + osc2Sig * m_patch.oscMix
                       + subSig * m_patch.subOscLevel;

    // 3. Dual Filters (Virus TI Architecture)
    // Filter 1 Cutoff modulated by Filter ADSR and LFO 2
    const float lfo2Val = std::sin(m_lfo2Phase);
    const float envCutoff = m_patch.filter1Cutoff * std::pow(2.0f, v.filtLevel * m_patch.filterEnvAmount * 4.0f + lfo2Val * m_patch.lfo2FilterDepth);
    const float f1Cutoff = std::clamp(envCutoff, 20.0f, 20000.0f);

    const float f1Out = v.filter1.process(mixOsc, f1Cutoff, m_patch.filter1Resonance * 3.8f, m_sampleRate);
    const float f2Out = v.filter2.process(f1Out, m_patch.filter2Cutoff, m_patch.filter2Resonance * 3.8f, m_sampleRate); // Serial cascade

    // Filter balance (-1 = Filt 1, +1 = Filt 2)
    const float filtBalanceNorm = (m_patch.filterBalance + 1.0f) * 0.5f;
    const float filtered = f1Out * (1.0f - filtBalanceNorm) + f2Out * filtBalanceNorm;

    // 4. Drive & Distortion
    const float driven = applyDistortion(filtered, m_patch.distortion, m_patch.drive);

    // 5. Amp & Panning
    const float finalLevel = driven * v.ampLevel * v.velocity;
    outL += finalLevel;
    outR += finalLevel;
}

void VirusTiPolarSynthNode::process(ProcessContext& context) noexcept
{
    // Process incoming MIDI events
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

    if (context.audio.channelCount == 0 || context.audio.frameCount == 0) return;

    float* outL = context.audio.channels[0];
    float* outR = (context.audio.channelCount > 1) ? context.audio.channels[1] : outL;
    const std::size_t numFrames = context.audio.frameCount;
    const float dt = 1.0f / static_cast<float>(m_sampleRate);

    // Delay time in samples
    const auto delaySamples = static_cast<std::size_t>(
        std::clamp(m_patch.delayTimeSec * static_cast<float>(m_sampleRate), 100.0f, static_cast<float>(kDelayBufSize - 1)));

    for (std::size_t f = 0; f < numFrames; ++f)
    {
        // Advance global LFOs
        m_lfo1Phase += m_patch.lfo1Rate * 2.0f * std::numbers::pi_v<float> * dt;
        if (m_lfo1Phase >= 2.0f * std::numbers::pi_v<float>)
            m_lfo1Phase -= 2.0f * std::numbers::pi_v<float>;

        m_lfo2Phase += m_patch.lfo2Rate * 2.0f * std::numbers::pi_v<float> * dt;
        if (m_lfo2Phase >= 2.0f * std::numbers::pi_v<float>)
            m_lfo2Phase -= 2.0f * std::numbers::pi_v<float>;

        float voiceL = 0.0f;
        float voiceR = 0.0f;

        for (auto& v : m_voices)
        {
            processVoice(v, voiceL, voiceR);
        }

        // Stereo Tape Delay Effect
        if (m_patch.delayEnabled)
        {
            const std::size_t readPos = (m_delayWritePos + kDelayBufSize - delaySamples) % kDelayBufSize;
            const float delayedL = m_delayBufferL[readPos];
            const float delayedR = m_delayBufferR[readPos];

            // Ping-pong cross feedback with soft saturation
            m_delayBufferL[m_delayWritePos] = std::tanh(voiceL + delayedR * m_patch.delayFeedback);
            m_delayBufferR[m_delayWritePos] = std::tanh(voiceR + delayedL * m_patch.delayFeedback);
            m_delayWritePos = (m_delayWritePos + 1) % kDelayBufSize;

            voiceL = voiceL * (1.0f - m_patch.delayMix) + delayedL * m_patch.delayMix;
            voiceR = voiceR * (1.0f - m_patch.delayMix) + delayedR * m_patch.delayMix;
        }

        // Simple Stereo Reverb Tank
        if (m_patch.reverbEnabled)
        {
            const float combInput = (voiceL + voiceR) * 0.5f;
            m_reverbCombsL[0] = m_reverbCombsL[0] * m_patch.reverbDecay + combInput * 0.35f;
            m_reverbCombsR[0] = m_reverbCombsR[0] * m_patch.reverbDecay + combInput * 0.35f;

            voiceL = voiceL * (1.0f - m_patch.reverbMix) + m_reverbCombsL[0] * m_patch.reverbMix;
            voiceR = voiceR * (1.0f - m_patch.reverbMix) + m_reverbCombsR[0] * m_patch.reverbMix;
        }

        outL[f] += voiceL;
        if (outR != outL) outR[f] += voiceR;
        m_lastOutputSample = (voiceL + voiceR) * 0.5f;
    }
}

void VirusTiPolarSynthNode::setFilter1Cutoff(const float hz) noexcept
{
    m_patch.filter1Cutoff = std::clamp(hz, 20.0f, 20000.0f);
}

void VirusTiPolarSynthNode::setFilter1Resonance(const float q) noexcept
{
    m_patch.filter1Resonance = std::clamp(q, 0.5f, 20.0f);
}

void VirusTiPolarSynthNode::setDrive(const float drive) noexcept
{
    m_patch.drive = std::clamp(drive, 0.0f, 1.0f);
}

void VirusTiPolarSynthNode::setHypersawDetune(const float detune) noexcept
{
    m_patch.hypersawDetune = std::clamp(detune, 0.0f, 1.0f);
}

void VirusTiPolarSynthNode::setDelayMix(const float mix) noexcept
{
    m_patch.delayMix = std::clamp(mix, 0.0f, 1.0f);
}

void VirusTiPolarSynthNode::setReverbMix(const float mix) noexcept
{
    m_patch.reverbMix = std::clamp(mix, 0.0f, 1.0f);
}

void VirusTiPolarSynthNode::loadPatch(const VirusTiPatch& patch) noexcept
{
    m_patch = patch;
}

std::vector<VirusTiPatch> VirusTiPolarSynthNode::factoryPresets()
{
    return {
        // 1. Polar Hypersaw Lead (Iconic Access Virus TI sound)
        VirusTiPatch {
            .name = "Polar Hypersaw Lead",
            .category = "Lead",
            .osc1Mode = VirusOscMode::hypersaw,
            .hypersawDensity = 9.0f,
            .hypersawDetune = 0.42f,
            .hypersawSpread = 0.90f,
            .filter1Cutoff = 4800.0f,
            .filter1Resonance = 2.4f,
            .distortion = VirusDistortionType::softClip,
            .drive = 0.35f,
            .ampAttack = 0.004f,
            .ampDecay = 0.15f,
            .ampSustain = 0.70f,
            .ampRelease = 0.30f,
            .delayEnabled = true,
            .delayMix = 0.35f,
            .reverbEnabled = true,
            .reverbMix = 0.28f
        },
        // 2. TrancePluck TI
        VirusTiPatch {
            .name = "TrancePluck TI",
            .category = "Pluck",
            .osc1Mode = VirusOscMode::hypersaw,
            .hypersawDensity = 7.0f,
            .hypersawDetune = 0.28f,
            .hypersawSpread = 0.85f,
            .filter1Cutoff = 1200.0f,
            .filter1Resonance = 4.2f,
            .filterEnvAmount = 0.85f,
            .ampAttack = 0.001f,
            .ampDecay = 0.22f,
            .ampSustain = 0.0f,
            .ampRelease = 0.20f,
            .delayEnabled = true,
            .delayMix = 0.40f,
            .reverbEnabled = true,
            .reverbMix = 0.35f
        },
        // 3. Cyberpunk Reese Bass
        VirusTiPatch {
            .name = "Cyberpunk Reese Bass",
            .category = "Bass",
            .osc1Mode = VirusOscMode::hypersaw,
            .hypersawDensity = 5.0f,
            .hypersawDetune = 0.30f,
            .hypersawSpread = 0.50f,
            .subOscLevel = 0.75f,
            .filter1Cutoff = 650.0f,
            .filter1Resonance = 3.8f,
            .distortion = VirusDistortionType::tubeCurve,
            .drive = 0.65f,
            .ampAttack = 0.01f,
            .ampDecay = 0.40f,
            .ampSustain = 0.85f,
            .ampRelease = 0.15f,
            .delayEnabled = false,
            .reverbEnabled = false
        },
        // 4. Crystal Ambient Bells
        VirusTiPatch {
            .name = "Crystal Ambient Bells",
            .category = "Pad",
            .osc1Mode = VirusOscMode::classicVa,
            .osc2Semitone = 12.0f,
            .osc2FmAmount = 0.45f,
            .filter1Cutoff = 6000.0f,
            .filter1Resonance = 1.2f,
            .ampAttack = 0.08f,
            .ampDecay = 0.60f,
            .ampSustain = 0.50f,
            .ampRelease = 0.80f,
            .delayEnabled = true,
            .delayMix = 0.45f,
            .reverbEnabled = true,
            .reverbMix = 0.50f
        },
        // 5. Analog Poly Brass
        VirusTiPatch {
            .name = "Analog Poly Brass",
            .category = "Brass",
            .osc1Mode = VirusOscMode::classicVa,
            .osc2DetuneCents = 12.0f,
            .filter1Cutoff = 2200.0f,
            .filter1Resonance = 2.0f,
            .filterEnvAmount = 0.55f,
            .ampAttack = 0.04f,
            .ampDecay = 0.25f,
            .ampSustain = 0.75f,
            .ampRelease = 0.35f,
            .delayEnabled = false,
            .reverbEnabled = true,
            .reverbMix = 0.25f
        },
        // 6. Vicious Acid 303
        VirusTiPatch {
            .name = "Vicious Acid 303",
            .category = "Acid",
            .osc1Mode = VirusOscMode::classicVa,
            .filter1Cutoff = 800.0f,
            .filter1Resonance = 12.0f,
            .filterEnvAmount = 0.90f,
            .distortion = VirusDistortionType::hardClip,
            .drive = 0.75f,
            .ampAttack = 0.002f,
            .ampDecay = 0.18f,
            .ampSustain = 0.20f,
            .ampRelease = 0.10f,
            .delayEnabled = true,
            .delayMix = 0.30f,
            .reverbEnabled = false
        }
    };
}

} // namespace dawg::engine
