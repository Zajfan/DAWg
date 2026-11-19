#include <dawg/engine/dsp/Juno106SynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

const std::array<Juno106Patch, 6> Juno106SynthNode::kPresets = {{
    {
        "Juno Lush Strings", "Pads",
        1, 1.8f, 100.0f, 0.0f, JunoPwmMode::lfo, 0.60f, true, true, 0.35f, 0.0f,
        1, 2800.0f, 0.30f, 0.55f, true, 0.10f, 0.70f,
        JunoVcaMode::envelope, 45.0f, 350.0f, 0.85f, 650.0f,
        JunoChorusMode::chorusI, 0.005f
    },
    {
        "80s Poly Brass", "Brass",
        1, 3.5f, 150.0f, 0.0f, JunoPwmMode::manual, 0.40f, true, true, 0.20f, 0.0f,
        1, 1600.0f, 0.45f, 0.75f, true, 0.05f, 0.85f,
        JunoVcaMode::envelope, 12.0f, 400.0f, 0.50f, 300.0f,
        JunoChorusMode::chorusII, 0.005f
    },
    {
        "Rubber Bassline", "Bass",
        0, 5.0f, 50.0f, 0.0f, JunoPwmMode::manual, 0.50f, true, false, 0.90f, 0.0f,
        0, 850.0f, 0.65f, 0.80f, true, 0.0f, 0.40f,
        JunoVcaMode::envelope, 2.0f, 220.0f, 0.10f, 120.0f,
        JunoChorusMode::off, 0.0f
    },
    {
        "Chorused Pluck", "Pluck",
        1, 2.2f, 100.0f, 0.0f, JunoPwmMode::lfo, 0.45f, true, true, 0.40f, 0.0f,
        1, 3200.0f, 0.50f, 0.70f, true, 0.0f, 0.75f,
        JunoVcaMode::envelope, 1.0f, 280.0f, 0.0f, 250.0f,
        JunoChorusMode::chorusI, 0.006f
    },
    {
        "Analog PWM Pad", "Pads",
        1, 1.2f, 150.0f, 0.0f, JunoPwmMode::lfo, 0.85f, true, false, 0.50f, 0.002f,
        1, 2200.0f, 0.25f, 0.45f, true, 0.20f, 0.60f,
        JunoVcaMode::envelope, 80.0f, 500.0f, 0.90f, 900.0f,
        JunoChorusMode::chorusII, 0.008f
    },
    {
        "Space Hover Leslie", "FX",
        1, 7.5f, 20.0f, 0.15f, JunoPwmMode::lfo, 0.70f, true, true, 0.60f, 0.02f,
        2, 4500.0f, 0.70f, 0.40f, false, 0.35f, 0.50f,
        JunoVcaMode::envelope, 15.0f, 600.0f, 0.60f, 500.0f,
        JunoChorusMode::chorusIandII, 0.012f
    }
}};

Juno106SynthNode::Juno106SynthNode() noexcept
{
    loadPreset(0);
}

bool Juno106SynthNode::prepare(const double sampleRate,
                              const std::size_t /*maxBlockSize*/,
                              const std::uint16_t /*channelCount*/)
{
    m_sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void Juno106SynthNode::reset() noexcept
{
    allNotesOff();
    m_chorusWriteIdx = 0;
    m_chorusBufferL.fill(0.0f);
    m_chorusBufferR.fill(0.0f);
    m_lfoPhase = 0.0f;
    m_chorusLfoPhaseL = 0.0f;
    m_chorusLfoPhaseR = 0.0f;
}

void Juno106SynthNode::loadPreset(const std::size_t index) noexcept
{
    if (index < kPresets.size())
    {
        m_patch = kPresets[index];
    }
}

const char* Juno106SynthNode::presetName(const std::size_t index) const noexcept
{
    if (index < kPresets.size())
    {
        return kPresets[index].name.c_str();
    }
    return "Custom";
}

void Juno106SynthNode::noteOn(const std::uint8_t pitch, const float velocity) noexcept
{
    Voice* target = nullptr;
    // Find free voice
    for (auto& v : m_voices)
    {
        if (! v.active)
        {
            target = &v;
            break;
        }
    }

    // Voice stealing if all 6 voices are active: pick voice in release or oldest
    if (target == nullptr)
    {
        float minEnv = 1000.0f;
        for (auto& v : m_voices)
        {
            if (v.envStage == Voice::EnvStage::release && v.envLevel < minEnv)
            {
                minEnv = v.envLevel;
                target = &v;
            }
        }
        if (target == nullptr)
        {
            target = &m_voices[0];
        }
    }

    target->active = true;
    target->pitch = pitch;
    target->velocity = std::clamp(velocity, 0.05f, 1.0f);

    // Compute target pitch frequency with DCO range transpose (-1, 0, +1 octaves)
    const int octaveShift = (m_patch.dcoRange - 1) * 12;
    const int effectivePitch = std::clamp(pitch + octaveShift, 12, 127);
    target->targetFreq = 440.0f * std::pow(2.0f, static_cast<float>(effectivePitch - 69) / 12.0f);

    target->phase = 0.0f;
    target->subPhase = 0.0f;
    target->envStage = Voice::EnvStage::attack;
    target->envLevel = 0.0f;
    target->vcf.reset();
    target->hpfState = 0.0f;
}

void Juno106SynthNode::noteOff(const std::uint8_t pitch) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.pitch == pitch)
        {
            v.envStage = Voice::EnvStage::release;
        }
    }
}

void Juno106SynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        v.envStage = Voice::EnvStage::idle;
        v.envLevel = 0.0f;
    }
}

void Juno106SynthNode::processVoice(Voice& v, float& outMono) noexcept
{
    if (! v.active) return;

    // 1. Envelope progression
    const float attCoeff = 1.0f / (std::max(1.0f, m_patch.attackMs) * 0.001f * static_cast<float>(m_sampleRate));
    const float decCoeff = 1.0f / (std::max(1.0f, m_patch.decayMs) * 0.001f * static_cast<float>(m_sampleRate));
    const float relCoeff = 1.0f / (std::max(1.0f, m_patch.releaseMs) * 0.001f * static_cast<float>(m_sampleRate));

    switch (v.envStage)
    {
    case Voice::EnvStage::attack:
        v.envLevel += attCoeff;
        if (v.envLevel >= 1.0f)
        {
            v.envLevel = 1.0f;
            v.envStage = Voice::EnvStage::decay;
        }
        break;
    case Voice::EnvStage::decay:
        v.envLevel -= decCoeff * (v.envLevel - m_patch.sustainLevel);
        if (v.envLevel <= m_patch.sustainLevel + 0.001f)
        {
            v.envLevel = m_patch.sustainLevel;
            v.envStage = Voice::EnvStage::sustain;
        }
        break;
    case Voice::EnvStage::sustain:
        v.envLevel = m_patch.sustainLevel;
        break;
    case Voice::EnvStage::release:
        v.envLevel -= relCoeff * v.envLevel;
        if (v.envLevel <= 0.0005f)
        {
            v.envLevel = 0.0f;
            v.envStage = Voice::EnvStage::idle;
            v.active = false;
            return;
        }
        break;
    case Voice::EnvStage::idle:
        v.active = false;
        return;
    }

    // 2. DCO Oscillator calculation
    const float lfoVal = std::sin(m_lfoPhase);
    const float pitchModRatio = std::pow(2.0f, (lfoVal * m_patch.dcoLfoDepth * 0.5f) / 12.0f);
    const float freq = v.targetFreq * pitchModRatio;
    const float phaseInc = freq / static_cast<float>(m_sampleRate);

    v.phase += phaseInc;
    if (v.phase >= 1.0f) v.phase -= 1.0f;

    v.subPhase += phaseInc * 0.5f;
    if (v.subPhase >= 1.0f) v.subPhase -= 1.0f;

    float oscSig = 0.0f;

    // Sawtooth Wave
    if (m_patch.sawEnabled)
    {
        const float saw = 2.0f * v.phase - 1.0f;
        oscSig += saw * 0.5f;
    }

    // Pulse Wave with PWM
    if (m_patch.pulseEnabled)
    {
        float pulseWidth = 0.5f;
        if (m_patch.pwmMode == JunoPwmMode::manual)
        {
            pulseWidth = 0.5f + (m_patch.pwmAmount - 0.5f) * 0.45f;
        }
        else // LFO PWM
        {
            pulseWidth = 0.5f + (lfoVal * m_patch.pwmAmount) * 0.45f;
        }
        pulseWidth = std::clamp(pulseWidth, 0.05f, 0.95f);
        const float pulse = (v.phase < pulseWidth) ? 0.5f : -0.5f;
        oscSig += pulse;
    }

    // Sub-Oscillator (Square wave -1 octave)
    if (m_patch.subLevel > 0.01f)
    {
        const float sub = (v.subPhase < 0.5f) ? 0.5f : -0.5f;
        oscSig += sub * m_patch.subLevel;
    }

    // Noise Generator
    if (m_patch.noiseLevel > 0.005f)
    {
        oscSig += nextNoise() * m_patch.noiseLevel * 0.5f;
    }

    // 3. 4-Position Stepped HPF
    // 0: Bass boost (leaky integrator blend)
    // 1: Flat (DC block only)
    // 2: Medium cut (cutoff ~220 Hz)
    // 3: High cut (cutoff ~700 Hz)
    float hpfCutoff = 20.0f;
    if (m_patch.hpfSetting == 0) hpfCutoff = 10.0f;
    else if (m_patch.hpfSetting == 1) hpfCutoff = 35.0f;
    else if (m_patch.hpfSetting == 2) hpfCutoff = 220.0f;
    else if (m_patch.hpfSetting == 3) hpfCutoff = 720.0f;

    const float hpfRc = 1.0f / (2.0f * static_cast<float>(std::numbers::pi) * hpfCutoff);
    const float dt = 1.0f / static_cast<float>(m_sampleRate);
    const float alpha = hpfRc / (hpfRc + dt);
    const float hpfOut = alpha * (v.hpfState + oscSig - (v.hpfState));
    v.hpfState = hpfOut;

    // For HPF setting 0 (Juno bass boost), add direct low shelf back in
    const float preVcf = (m_patch.hpfSetting == 0) ? (hpfOut + oscSig * 0.35f) : hpfOut;

    // 4. VCF (IR3109 4-pole resonant low pass)
    const float keyTrackSemitones = (static_cast<float>(v.pitch) - 60.0f) * m_patch.vcfKeyFollow;
    const float envPolarity = m_patch.vcfEnvPolarityPos ? 1.0f : -1.0f;
    const float envModSemitones = v.envLevel * m_patch.vcfEnvAmount * 48.0f * envPolarity;
    const float lfoModSemitones = lfoVal * m_patch.vcfLfoDepth * 18.0f;

    const float vcfTotalSemitones = keyTrackSemitones + envModSemitones + lfoModSemitones;
    const float effectiveCutoff = std::clamp(m_patch.vcfCutoff * std::pow(2.0f, vcfTotalSemitones / 12.0f), 15.0f, 20000.0f);

    const float filtered = v.vcf.process(preVcf, effectiveCutoff, m_patch.vcfResonance * 3.85f, m_sampleRate);

    // 5. VCA
    const float vcaGain = (m_patch.vcaMode == JunoVcaMode::envelope) ? (v.envLevel * v.velocity) : v.velocity;
    outMono += filtered * vcaGain;
}

void Juno106SynthNode::applyChorus(const float inMono, float& outL, float& outR) noexcept
{
    if (m_patch.chorusMode == JunoChorusMode::off)
    {
        outL = inMono;
        outR = inMono;
        return;
    }

    // Write input into circular delay buffer
    m_chorusBufferL[m_chorusWriteIdx] = inMono;
    m_chorusBufferR[m_chorusWriteIdx] = inMono;

    // Rate and depth configuration for Juno Chorus
    float rateHz = 0.40f;
    float depthMs = 2.0f;
    float baseDelayMs = 5.0f;

    if (m_patch.chorusMode == JunoChorusMode::chorusI)
    {
        rateHz = 0.40f;
        depthMs = 1.8f;
        baseDelayMs = 4.5f;
    }
    else if (m_patch.chorusMode == JunoChorusMode::chorusII)
    {
        rateHz = 0.62f;
        depthMs = 2.6f;
        baseDelayMs = 5.8f;
    }
    else if (m_patch.chorusMode == JunoChorusMode::chorusIandII)
    {
        // Secret button mode: Fast rotary vibrato / swirl
        rateHz = 8.2f;
        depthMs = 1.2f;
        baseDelayMs = 3.5f;
    }

    // Advance chorus LFOs (Left and Right out of phase by 180 degrees)
    const float lfoStep = (rateHz * 2.0f * static_cast<float>(std::numbers::pi)) / static_cast<float>(m_sampleRate);
    m_chorusLfoPhaseL += lfoStep;
    if (m_chorusLfoPhaseL >= 2.0f * static_cast<float>(std::numbers::pi))
        m_chorusLfoPhaseL -= 2.0f * static_cast<float>(std::numbers::pi);

    m_chorusLfoPhaseR = m_chorusLfoPhaseL + static_cast<float>(std::numbers::pi);

    const float modL = std::sin(m_chorusLfoPhaseL);
    const float modR = std::sin(m_chorusLfoPhaseR);

    const float delaySamplesL = (baseDelayMs + modL * depthMs) * 0.001f * static_cast<float>(m_sampleRate);
    const float delaySamplesR = (baseDelayMs + modR * depthMs) * 0.001f * static_cast<float>(m_sampleRate);

    // Hermite / linear interpolation from circular buffer
    const auto readDelay = [this](const std::array<float, kChorusBufferLength>& buf, float delaySamp) {
        const float rPos = static_cast<float>(m_chorusWriteIdx) - delaySamp;
        const int idx0 = (static_cast<int>(std::floor(rPos)) % static_cast<int>(kChorusBufferLength) + static_cast<int>(kChorusBufferLength)) % static_cast<int>(kChorusBufferLength);
        const int idx1 = (idx0 + 1) % static_cast<int>(kChorusBufferLength);
        const float frac = rPos - std::floor(rPos);
        return buf[static_cast<std::size_t>(idx0)] * (1.0f - frac) + buf[static_cast<std::size_t>(idx1)] * frac;
    };

    const float wetL = readDelay(m_chorusBufferL, delaySamplesL);
    const float wetR = readDelay(m_chorusBufferR, delaySamplesR);

    // Authentic BBD noise simulation
    const float noiseL = nextNoise() * m_patch.bbdNoiseLevel;
    const float noiseR = nextNoise() * m_patch.bbdNoiseLevel;

    outL = inMono * 0.65f + (wetL + noiseL) * 0.55f;
    outR = inMono * 0.65f + (wetR + noiseR) * 0.55f;

    m_chorusWriteIdx = (m_chorusWriteIdx + 1) % kChorusBufferLength;
}

void Juno106SynthNode::process(ProcessContext& context) noexcept
{
    // 1. Process MIDI Events
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

    // Advance global LFO
    const float lfoInc = (m_patch.lfoRateHz * 2.0f * static_cast<float>(std::numbers::pi)) / static_cast<float>(m_sampleRate);

    float* const outL = context.audio.channels[0];
    float* const outR = (context.audio.channelCount >= 2 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : context.audio.channels[0];

    for (std::size_t f = 0; f < context.audio.frameCount; ++f)
    {
        m_lfoPhase += lfoInc;
        if (m_lfoPhase >= 2.0f * static_cast<float>(std::numbers::pi))
            m_lfoPhase -= 2.0f * static_cast<float>(std::numbers::pi);

        float monoSum = 0.0f;
        for (auto& v : m_voices)
        {
            processVoice(v, monoSum);
        }

        // Apply BBD stereo chorus
        float sampleL = 0.0f;
        float sampleR = 0.0f;
        applyChorus(monoSum, sampleL, sampleR);

        outL[f] = sampleL;
        if (context.audio.channelCount >= 2 && outR != outL)
        {
            outR[f] = sampleR;
        }
    }
}

} // namespace dawg::engine
