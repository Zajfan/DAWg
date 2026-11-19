#include <dawg/engine/dsp/OmniFmSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

const std::array<OmniFmPatch, 6> OmniFmSynthNode::kPresets = {{
    // 1. Solid DX7 Electric Piano
    {
        "Solid DX7 Electric Piano", "Keys",
        16, 0.45f,
        {{
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.90f, 2.0f, 250.0f, 600.0f, 400.0f, 0.70f, 0.0f},  // Op 1 (Carrier)
            {true, FmWaveform::sine, 1.0f,  0.5f,  0.65f, 1.0f, 180.0f, 450.0f, 350.0f, 0.50f, 0.0f},  // Op 2 (Carrier)
            {true, FmWaveform::sine, 3.0f, -0.4f,  0.75f, 1.0f, 120.0f, 300.0f, 250.0f, 0.20f, 0.0f},  // Op 3 (Mod)
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.85f, 2.0f, 220.0f, 500.0f, 380.0f, 0.65f, 0.0f},  // Op 4 (Carrier)
            {true, FmWaveform::sine, 14.0f, 1.2f,  0.40f, 1.0f,  60.0f, 150.0f, 150.0f, 0.05f, 0.0f},  // Op 5 (Tine mod)
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.55f, 1.0f, 100.0f, 250.0f, 200.0f, 0.15f, 0.45f}   // Op 6 (Feedback)
        }}
    },
    // 2. Lately / Solid FM Bass
    {
        "Lately / Solid FM Bass", "Bass",
        1, 0.65f,
        {{
            {true, FmWaveform::sine, 0.5f,  0.0f,  0.95f, 2.0f, 200.0f, 400.0f, 150.0f, 0.0f,  0.0f},  // Op 1 (Sub carrier)
            {true, FmWaveform::sine, 0.5f,  0.0f,  0.80f, 1.0f, 150.0f, 300.0f, 120.0f, 0.0f,  0.0f},  // Op 2
            {true, FmWaveform::sine, 1.0f,  0.2f,  0.70f, 1.0f, 100.0f, 200.0f, 100.0f, 0.0f,  0.0f},  // Op 3
            {true, FmWaveform::sine, 2.0f, -0.2f,  0.60f, 1.0f,  80.0f, 150.0f,  80.0f, 0.0f,  0.0f},  // Op 4
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.50f, 1.0f,  60.0f, 100.0f,  60.0f, 0.0f,  0.0f},  // Op 5
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.85f, 1.0f,  50.0f,  90.0f,  50.0f, 0.0f,  0.65f}  // Op 6 (Feedback)
        }}
    },
    // 3. Tubular Bell Chime
    {
        "Tubular Bell Chime", "Bells",
        5, 0.30f,
        {{
            {true, FmWaveform::sine, 1.0f,   0.0f,  0.90f, 1.0f, 800.0f, 1500.0f, 1800.0f, 0.20f, 0.0f},
            {true, FmWaveform::sine, 3.52f,  1.1f,  0.75f, 1.0f, 600.0f, 1200.0f, 1400.0f, 0.10f, 0.0f},
            {true, FmWaveform::sine, 2.76f, -1.0f,  0.65f, 1.0f, 400.0f,  800.0f, 1000.0f, 0.05f, 0.0f},
            {true, FmWaveform::sine, 5.40f,  2.2f,  0.55f, 1.0f, 300.0f,  600.0f,  700.0f, 0.02f, 0.0f},
            {true, FmWaveform::sine, 8.92f, -1.5f,  0.45f, 1.0f, 150.0f,  300.0f,  400.0f, 0.00f, 0.0f},
            {true, FmWaveform::sine, 1.0f,   0.0f,  0.35f, 1.0f, 500.0f, 1000.0f, 1200.0f, 0.10f, 0.30f}
        }}
    },
    // 4. Glass Mallet Marimba
    {
        "Glass Mallet Marimba", "Percussion",
        16, 0.15f,
        {{
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.95f, 0.5f,  80.0f, 160.0f, 120.0f, 0.0f, 0.0f},
            {true, FmWaveform::sine, 4.0f,  0.5f,  0.60f, 0.5f,  50.0f,  90.0f,  80.0f, 0.0f, 0.0f},
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.70f, 0.5f,  60.0f, 110.0f,  90.0f, 0.0f, 0.0f},
            {true, FmWaveform::sine, 7.0f, -0.8f,  0.40f, 0.5f,  30.0f,  60.0f,  50.0f, 0.0f, 0.0f},
            {true, FmWaveform::sine, 10.0f, 1.5f,  0.30f, 0.5f,  20.0f,  40.0f,  30.0f, 0.0f, 0.0f},
            {true, FmWaveform::sine, 1.0f,  0.0f,  0.25f, 0.5f,  40.0f,  80.0f,  60.0f, 0.0f, 0.20f}
        }}
    },
    // 5. Aggressive FM Growl
    {
        "Aggressive FM Growl", "Bass",
        1, 0.85f,
        {{
            {true, FmWaveform::saw,    0.5f,  0.0f, 0.95f, 10.0f, 180.0f, 350.0f, 200.0f, 0.40f, 0.0f},
            {true, FmWaveform::square, 1.5f,  0.5f, 0.85f,  5.0f, 150.0f, 250.0f, 180.0f, 0.30f, 0.0f},
            {true, FmWaveform::sine,   1.0f, -0.5f, 0.75f,  5.0f, 120.0f, 200.0f, 150.0f, 0.20f, 0.0f},
            {true, FmWaveform::sine,   2.5f,  1.0f, 0.65f,  5.0f, 100.0f, 160.0f, 120.0f, 0.15f, 0.0f},
            {true, FmWaveform::sine,   0.5f,  0.0f, 0.55f,  5.0f,  80.0f, 120.0f, 100.0f, 0.10f, 0.0f},
            {true, FmWaveform::sine,   1.0f,  0.0f, 0.90f,  5.0f,  60.0f, 100.0f,  80.0f, 0.05f, 0.85f}
        }}
    },
    // 6. Digital Harp Pluck
    {
        "Digital Harp Pluck", "Pluck",
        5, 0.20f,
        {{
            {true, FmWaveform::sine, 1.0f,  0.0f, 0.90f, 1.0f, 350.0f, 800.0f, 700.0f, 0.10f, 0.0f},
            {true, FmWaveform::sine, 2.0f,  0.3f, 0.70f, 1.0f, 250.0f, 500.0f, 500.0f, 0.05f, 0.0f},
            {true, FmWaveform::sine, 1.0f, -0.3f, 0.80f, 1.0f, 300.0f, 650.0f, 600.0f, 0.08f, 0.0f},
            {true, FmWaveform::sine, 3.0f,  0.5f, 0.50f, 1.0f, 180.0f, 350.0f, 350.0f, 0.02f, 0.0f},
            {true, FmWaveform::sine, 4.0f, -0.5f, 0.40f, 1.0f, 120.0f, 220.0f, 200.0f, 0.00f, 0.0f},
            {true, FmWaveform::sine, 1.0f,  0.0f, 0.30f, 1.0f, 200.0f, 400.0f, 400.0f, 0.05f, 0.25f}
        }}
    }
}};

OmniFmSynthNode::OmniFmSynthNode() noexcept
{
    loadPreset(0);
}

bool OmniFmSynthNode::prepare(const double sampleRate,
                             const std::size_t /*maxBlockSize*/,
                             const std::uint16_t /*channelCount*/)
{
    m_sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void OmniFmSynthNode::reset() noexcept
{
    allNotesOff();
}

void OmniFmSynthNode::loadPreset(const std::size_t index) noexcept
{
    if (index < kPresets.size())
    {
        m_patch = kPresets[index];
    }
}

const char* OmniFmSynthNode::presetName(const std::size_t index) const noexcept
{
    if (index < kPresets.size())
    {
        return kPresets[index].name.c_str();
    }
    return "Custom";
}

void OmniFmSynthNode::noteOn(const std::uint8_t pitch, const float velocity) noexcept
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
    target->baseFreq = 440.0f * std::pow(2.0f, static_cast<float>(pitch - 69) / 12.0f);

    for (std::size_t i = 0; i < 6; ++i)
    {
        target->ops[i].phase = 0.0f;
        target->ops[i].lastOutput = 0.0f;
        target->ops[i].prevOutput = 0.0f;
        target->ops[i].envStage = OpState::Stage::attack;
        target->ops[i].envLevel = 0.0f;
    }
}

void OmniFmSynthNode::noteOff(const std::uint8_t pitch) noexcept
{
    for (auto& v : m_voices)
    {
        if (v.active && v.pitch == pitch)
        {
            for (auto& op : v.ops)
            {
                op.envStage = OpState::Stage::release;
            }
        }
    }
}

void OmniFmSynthNode::allNotesOff() noexcept
{
    for (auto& v : m_voices)
    {
        v.active = false;
        for (auto& op : v.ops)
        {
            op.envStage = OpState::Stage::idle;
            op.envLevel = 0.0f;
        }
    }
}

float OmniFmSynthNode::evaluateOpWave(const FmWaveform wave, const float phase) const noexcept
{
    switch (wave)
    {
    case FmWaveform::sine:
        return std::sin(phase);
    case FmWaveform::triangle:
    {
        const float norm = phase / (2.0f * static_cast<float>(std::numbers::pi));
        return 4.0f * std::abs(norm - std::floor(norm + 0.75f) + 0.25f) - 1.0f;
    }
    case FmWaveform::saw:
    {
        const float norm = phase / (2.0f * static_cast<float>(std::numbers::pi));
        return 2.0f * (norm - std::floor(norm)) - 1.0f;
    }
    case FmWaveform::square:
        return std::sin(phase) >= 0.0f ? 0.7f : -0.7f;
    case FmWaveform::absSine:
        return std::abs(std::sin(phase)) * 2.0f - 1.0f;
    }
    return std::sin(phase);
}

void OmniFmSynthNode::processVoice(Voice& v, float& outL, float& outR) noexcept
{
    if (! v.active) return;

    bool anyActive = false;
    std::array<float, 6> opOutputs {};

    // 1. Advance envelopes and calculate raw operator outputs
    for (std::size_t i = 0; i < 6; ++i)
    {
        auto& opState = v.ops[i];
        const auto& opCfg = m_patch.operators[i];

        if (! opCfg.enabled) continue;

        const float attCoeff = 1.0f / (std::max(0.5f, opCfg.attackRateMs) * 0.001f * static_cast<float>(m_sampleRate));
        const float dec1Coeff = 1.0f / (std::max(1.0f, opCfg.decay1RateMs) * 0.001f * static_cast<float>(m_sampleRate));
        const float dec2Coeff = 1.0f / (std::max(1.0f, opCfg.decay2RateMs) * 0.001f * static_cast<float>(m_sampleRate));
        const float relCoeff = 1.0f / (std::max(1.0f, opCfg.releaseRateMs) * 0.001f * static_cast<float>(m_sampleRate));

        switch (opState.envStage)
        {
        case OpState::Stage::attack:
            opState.envLevel += attCoeff;
            if (opState.envLevel >= 1.0f)
            {
                opState.envLevel = 1.0f;
                opState.envStage = OpState::Stage::decay1;
            }
            anyActive = true;
            break;
        case OpState::Stage::decay1:
            opState.envLevel -= dec1Coeff * (opState.envLevel - (opCfg.sustainLevel + (1.0f - opCfg.sustainLevel) * 0.5f));
            if (opState.envLevel <= opCfg.sustainLevel + 0.2f)
            {
                opState.envStage = OpState::Stage::decay2;
            }
            anyActive = true;
            break;
        case OpState::Stage::decay2:
            opState.envLevel -= dec2Coeff * (opState.envLevel - opCfg.sustainLevel);
            if (opState.envLevel <= opCfg.sustainLevel + 0.001f)
            {
                opState.envLevel = opCfg.sustainLevel;
                opState.envStage = OpState::Stage::sustain;
            }
            anyActive = true;
            break;
        case OpState::Stage::sustain:
            opState.envLevel = opCfg.sustainLevel;
            if (opState.envLevel > 0.001f) anyActive = true;
            break;
        case OpState::Stage::release:
            opState.envLevel -= relCoeff * opState.envLevel;
            if (opState.envLevel <= 0.0005f)
            {
                opState.envLevel = 0.0f;
                opState.envStage = OpState::Stage::idle;
            }
            else
            {
                anyActive = true;
            }
            break;
        case OpState::Stage::idle:
            break;
        }

        const float opFreq = v.baseFreq * opCfg.ratio + opCfg.fineDetuneHz;
        const float phaseInc = (opFreq * 2.0f * static_cast<float>(std::numbers::pi)) / static_cast<float>(m_sampleRate);

        opState.phase += phaseInc;
        if (opState.phase >= 2.0f * static_cast<float>(std::numbers::pi))
            opState.phase -= 2.0f * static_cast<float>(std::numbers::pi);
    }

    if (! anyActive)
    {
        v.active = false;
        return;
    }

    // 2. Compute Operators based on Algorithm Matrix
    // Op 6 with self-feedback
    const float fbVal = (v.ops[5].lastOutput + v.ops[5].prevOutput) * 0.5f * (m_patch.masterFeedback * 4.0f);
    v.ops[5].prevOutput = v.ops[5].lastOutput;
    const float op6Raw = evaluateOpWave(m_patch.operators[5].waveform, v.ops[5].phase + fbVal);
    const float op6Amp = op6Raw * v.ops[5].envLevel * m_patch.operators[5].outputLevel;
    v.ops[5].lastOutput = op6Amp;
    opOutputs[5] = op6Amp;

    float voiceOut = 0.0f;

    // Route according to algorithm:
    // Algorithm 16 (Classic DX7 Piano: 6->5->4, 3->2, 1 carrier)
    if (m_patch.algorithm == 16)
    {
        // Modulator 5 modulated by 6
        const float op5Raw = evaluateOpWave(m_patch.operators[4].waveform, v.ops[4].phase + opOutputs[5] * 3.5f);
        opOutputs[4] = op5Raw * v.ops[4].envLevel * m_patch.operators[4].outputLevel;

        // Carrier 4 modulated by 5
        const float op4Raw = evaluateOpWave(m_patch.operators[3].waveform, v.ops[3].phase + opOutputs[4] * 3.5f);
        opOutputs[3] = op4Raw * v.ops[3].envLevel * m_patch.operators[3].outputLevel;

        // Modulator 3
        const float op3Raw = evaluateOpWave(m_patch.operators[2].waveform, v.ops[2].phase);
        opOutputs[2] = op3Raw * v.ops[2].envLevel * m_patch.operators[2].outputLevel;

        // Carrier 2 modulated by 3
        const float op2Raw = evaluateOpWave(m_patch.operators[1].waveform, v.ops[1].phase + opOutputs[2] * 3.5f);
        opOutputs[1] = op2Raw * v.ops[1].envLevel * m_patch.operators[1].outputLevel;

        // Carrier 1 (independent)
        const float op1Raw = evaluateOpWave(m_patch.operators[0].waveform, v.ops[0].phase);
        opOutputs[0] = op1Raw * v.ops[0].envLevel * m_patch.operators[0].outputLevel;

        // Carriers 1, 2, 4 output to mix
        voiceOut = (opOutputs[0] + opOutputs[1] + opOutputs[3]) * 0.40f;
    }
    // Algorithm 1 (Cascade 6 -> 5 -> 4 -> 3 -> 2 -> 1)
    else if (m_patch.algorithm == 1)
    {
        const float op5Raw = evaluateOpWave(m_patch.operators[4].waveform, v.ops[4].phase + opOutputs[5] * 3.0f);
        opOutputs[4] = op5Raw * v.ops[4].envLevel * m_patch.operators[4].outputLevel;

        const float op4Raw = evaluateOpWave(m_patch.operators[3].waveform, v.ops[3].phase + opOutputs[4] * 3.0f);
        opOutputs[3] = op4Raw * v.ops[3].envLevel * m_patch.operators[3].outputLevel;

        const float op3Raw = evaluateOpWave(m_patch.operators[2].waveform, v.ops[2].phase + opOutputs[3] * 3.0f);
        opOutputs[2] = op3Raw * v.ops[2].envLevel * m_patch.operators[2].outputLevel;

        const float op2Raw = evaluateOpWave(m_patch.operators[1].waveform, v.ops[1].phase + opOutputs[2] * 3.0f);
        opOutputs[1] = op2Raw * v.ops[1].envLevel * m_patch.operators[1].outputLevel;

        const float op1Raw = evaluateOpWave(m_patch.operators[0].waveform, v.ops[0].phase + opOutputs[1] * 3.5f);
        opOutputs[0] = op1Raw * v.ops[0].envLevel * m_patch.operators[0].outputLevel;

        voiceOut = opOutputs[0] * 0.85f;
    }
    // Algorithm 5 / Default (Branch: 6->5->4 to Carrier 1; 3->2 to Carrier 1)
    else
    {
        const float op5Raw = evaluateOpWave(m_patch.operators[4].waveform, v.ops[4].phase + opOutputs[5] * 3.0f);
        opOutputs[4] = op5Raw * v.ops[4].envLevel * m_patch.operators[4].outputLevel;

        const float op4Raw = evaluateOpWave(m_patch.operators[3].waveform, v.ops[3].phase + opOutputs[4] * 3.0f);
        opOutputs[3] = op4Raw * v.ops[3].envLevel * m_patch.operators[3].outputLevel;

        const float op3Raw = evaluateOpWave(m_patch.operators[2].waveform, v.ops[2].phase);
        opOutputs[2] = op3Raw * v.ops[2].envLevel * m_patch.operators[2].outputLevel;

        const float op2Raw = evaluateOpWave(m_patch.operators[1].waveform, v.ops[1].phase + opOutputs[2] * 3.0f);
        opOutputs[1] = op2Raw * v.ops[1].envLevel * m_patch.operators[1].outputLevel;

        const float op1Raw = evaluateOpWave(m_patch.operators[0].waveform, v.ops[0].phase + (opOutputs[3] + opOutputs[1]) * 2.5f);
        opOutputs[0] = op1Raw * v.ops[0].envLevel * m_patch.operators[0].outputLevel;

        voiceOut = opOutputs[0] * 0.85f;
    }

    const float finalLevel = voiceOut * v.velocity * 0.70f;
    outL += finalLevel;
    outR += finalLevel;
}

void OmniFmSynthNode::process(ProcessContext& context) noexcept
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
