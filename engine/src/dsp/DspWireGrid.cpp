#include <dawg/engine/dsp/DspWireGrid.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace dawg::engine::dsp {

DspWireGrid::DspWireGrid() noexcept
{
    loadPresetAcidBass();
}

bool DspWireGrid::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    reallocateScratchBuffers();
    reset();
    return true;
}

void DspWireGrid::reset() noexcept
{
    for (auto& mod : m_modules)
    {
        mod.phase = 0.0f;
        mod.filterS1 = 0.0f;
        mod.filterS2 = 0.0f;
        mod.envStage = 0.0f;
        mod.envValue = 0.0f;
        mod.feedbackSample = 0.0f;
        mod.dcBlockerPrevX = 0.0f;
        mod.dcBlockerPrevY = 0.0f;
        mod.shHeldValue = 0.0f;
        mod.shLastTrigger = 0.0f;
        if (! mod.delayBuffer.empty())
        {
            std::fill(mod.delayBuffer.begin(), mod.delayBuffer.end(), 0.0f);
            mod.delayWritePos = 0;
        }
    }

    m_probeRingBuffer.fill(0.0f);
    m_probeWritePos = 0;
}

void DspWireGrid::reallocateScratchBuffers()
{
    m_moduleScratchBuffers.resize(m_modules.size());
    for (std::size_t i = 0; i < m_modules.size(); ++i)
    {
        m_moduleScratchBuffers[i].assign(m_maxBlockSize, 0.0f);
        if (m_modules[i].type == GridModuleType::delay)
        {
            const auto delaySize = static_cast<std::size_t>(m_sampleRate * 2.0); // 2 sec max delay
            if (m_modules[i].delayBuffer.size() != delaySize)
            {
                m_modules[i].delayBuffer.assign(delaySize, 0.0f);
                m_modules[i].delayWritePos = 0;
            }
        }
    }
}

void DspWireGrid::setMacro(std::size_t index, float value) noexcept
{
    if (index < m_macros.size())
    {
        m_macros[index] = std::clamp(value, 0.0f, 1.0f);

        for (const auto& binding : m_macroBindings)
        {
            if (binding.macroIndex == index)
            {
                const float scaled = binding.minVal + m_macros[index] * (binding.maxVal - binding.minVal);
                setModuleParam(binding.moduleId, binding.paramIndex, scaled);
            }
        }
    }
}

float DspWireGrid::macro(std::size_t index) const noexcept
{
    return (index < m_macros.size()) ? m_macros[index] : 0.0f;
}

void DspWireGrid::bindMacro(
    std::size_t macroIdx,
    std::uint32_t moduleId,
    std::size_t paramIdx,
    float minVal,
    float maxVal,
    const std::string& label)
{
    if (macroIdx >= numMacros) return;
    unbindMacro(macroIdx, moduleId, paramIdx);
    m_macroBindings.push_back(GridMacroBinding{
        .macroIndex = macroIdx,
        .moduleId = moduleId,
        .paramIndex = paramIdx,
        .minVal = minVal,
        .maxVal = maxVal,
        .label = label.empty() ? ("Macro " + std::to_string(macroIdx + 1)) : label
    });
    setMacro(macroIdx, m_macros[macroIdx]);
}

void DspWireGrid::unbindMacro(std::size_t macroIdx, std::uint32_t moduleId, std::size_t paramIdx) noexcept
{
    std::erase_if(m_macroBindings, [=](const GridMacroBinding& b) {
        return b.macroIndex == macroIdx && b.moduleId == moduleId && b.paramIndex == paramIdx;
    });
}

std::uint32_t DspWireGrid::addModule(GridModuleType type, float x, float y, const std::string& name)
{
    GridModule mod;
    mod.id = m_nextModuleId++;
    mod.type = type;
    mod.posX = x;
    mod.posY = y;

    switch (type)
    {
    case GridModuleType::oscillator:
        mod.name = name.empty() ? "PolyBLEP Osc" : name;
        mod.inputs = { {"V/Oct CV", PortSignalType::cv, 0.0f, 0.0f}, {"Sync", PortSignalType::audio, 0.0f, 0.0f} };
        mod.outputs = { {"Audio Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 2.0f, 36.0f, 0.5f, 0.5f }; // Saw, Note 36 (C2), Fine, PW
        break;

    case GridModuleType::noise:
        mod.name = name.empty() ? "Noise Gen" : name;
        mod.inputs = {};
        mod.outputs = { {"Noise Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 0.2f, 0.8f, 0.0f, 0.0f }; // Color, Level
        break;

    case GridModuleType::svfFilter:
        mod.name = name.empty() ? "ZDF SVF Filter" : name;
        mod.inputs = { {"Audio In", PortSignalType::audio, 0.0f, 0.0f}, {"Cutoff CV", PortSignalType::cv, 0.0f, 0.0f} };
        mod.outputs = { {"LP Out", PortSignalType::audio, 0.0f, 0.0f}, {"BP Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 0.45f, 0.7f, 0.0f, 0.0f }; // Cutoff, Reso, Mode
        break;

    case GridModuleType::mathSaturation:
        mod.name = name.empty() ? "Tanh Saturation" : name;
        mod.inputs = { {"Audio In", PortSignalType::audio, 0.0f, 0.0f} };
        mod.outputs = { {"Audio Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 0.5f, 0.3f, 0.0f, 0.0f }; // Drive, Wavefold
        break;

    case GridModuleType::lfo:
        mod.name = name.empty() ? "LFO Modulator" : name;
        mod.inputs = { {"Rate CV", PortSignalType::cv, 0.0f, 0.0f} };
        mod.outputs = { {"CV Out", PortSignalType::cv, 0.0f, 0.0f} };
        mod.params = { 0.25f, 0.8f, 0.0f, 0.0f }; // Rate, Depth, Shape
        break;

    case GridModuleType::envelope:
        mod.name = name.empty() ? "ADSR Envelope" : name;
        mod.inputs = { {"Gate In", PortSignalType::cv, 0.0f, 0.0f} };
        mod.outputs = { {"Env Out", PortSignalType::cv, 0.0f, 0.0f} };
        mod.params = { 0.05f, 0.3f, 0.6f, 0.4f }; // A, D, S, R
        break;

    case GridModuleType::delay:
        mod.name = name.empty() ? "Tape Echo Delay" : name;
        mod.inputs = { {"Audio In", PortSignalType::audio, 0.0f, 0.0f} };
        mod.outputs = { {"Audio Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 0.35f, 0.45f, 0.35f, 0.0f }; // Time, Feedback, Mix
        break;

    case GridModuleType::audioOut:
        mod.name = name.empty() ? "Master Out" : name;
        mod.inputs = { {"Left In", PortSignalType::audio, 0.0f, 0.0f}, {"Right In", PortSignalType::audio, 0.0f, 0.0f} };
        mod.outputs = {};
        mod.params = { 0.8f, 0.5f, 0.0f, 0.0f }; // Gain, Pan
        break;

    case GridModuleType::unitDelayFeedback:
        mod.name = name.empty() ? "Unit Delay Feedback" : name;
        mod.inputs = { {"Signal In", PortSignalType::audio, 0.0f, 0.0f} };
        mod.outputs = { {"Feedback Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 0.75f, 0.0f, 0.0f, 0.0f }; // Gain, DC-Block
        break;

    case GridModuleType::wavefolder:
        mod.name = name.empty() ? "Buchla Wavefolder" : name;
        mod.inputs = { {"Audio In", PortSignalType::audio, 0.0f, 0.0f}, {"Fold CV", PortSignalType::cv, 0.0f, 0.0f} };
        mod.outputs = { {"Audio Out", PortSignalType::audio, 0.0f, 0.0f} };
        mod.params = { 0.6f, 0.5f, 0.0f, 0.0f }; // Fold Amount, Symmetry Bias
        break;

    case GridModuleType::xyPad:
        mod.name = name.empty() ? "XY Pad Macro" : name;
        mod.inputs = {};
        mod.outputs = { {"X CV", PortSignalType::cv, 0.5f, 0.5f}, {"Y CV", PortSignalType::cv, 0.5f, 0.5f} };
        mod.params = { 0.5f, 0.5f, 0.0f, 0.0f }; // X, Y
        break;

    case GridModuleType::sampleAndHold:
        mod.name = name.empty() ? "Sample & Hold" : name;
        mod.inputs = { {"Signal In", PortSignalType::cv, 0.0f, 0.0f}, {"Clock In", PortSignalType::cv, 0.0f, 0.0f} };
        mod.outputs = { {"Held Out", PortSignalType::cv, 0.0f, 0.0f} };
        mod.params = { 0.3f, 0.0f, 0.0f, 0.0f }; // Internal Clock Rate
        break;
    }

    m_modules.push_back(std::move(mod));
    reallocateScratchBuffers();
    return m_modules.back().id;
}

bool DspWireGrid::removeModule(std::uint32_t moduleId)
{
    std::erase_if(m_cables, [moduleId](const GridCable& c) {
        return c.srcModuleId == moduleId || c.dstModuleId == moduleId;
    });

    std::erase_if(m_macroBindings, [moduleId](const GridMacroBinding& b) {
        return b.moduleId == moduleId;
    });

    const auto it = std::find_if(m_modules.begin(), m_modules.end(), [moduleId](const GridModule& m) {
        return m.id == moduleId;
    });

    if (it != m_modules.end())
    {
        m_modules.erase(it);
        reallocateScratchBuffers();
        return true;
    }
    return false;
}

void DspWireGrid::setModuleParam(std::uint32_t moduleId, std::size_t paramIdx, float value)
{
    for (auto& mod : m_modules)
    {
        if (mod.id == moduleId && paramIdx < mod.params.size())
        {
            mod.params[paramIdx] = value;
            break;
        }
    }
}

std::uint32_t DspWireGrid::connectCable(
    std::uint32_t srcModuleId, std::uint8_t srcPort,
    std::uint32_t dstModuleId, std::uint8_t dstPort,
    std::uint32_t colorRgba)
{
    std::erase_if(m_cables, [dstModuleId, dstPort](const GridCable& c) {
        return c.dstModuleId == dstModuleId && c.dstPort == dstPort;
    });

    GridCable cable;
    cable.id = m_nextCableId++;
    cable.srcModuleId = srcModuleId;
    cable.srcPort = srcPort;
    cable.dstModuleId = dstModuleId;
    cable.dstPort = dstPort;
    cable.colorRgba = colorRgba;

    m_cables.push_back(cable);
    return cable.id;
}

bool DspWireGrid::disconnectCable(std::uint32_t cableId)
{
    const auto it = std::find_if(m_cables.begin(), m_cables.end(), [cableId](const GridCable& c) {
        return c.id == cableId;
    });

    if (it != m_cables.end())
    {
        if (m_probedCableId == cableId)
        {
            m_probedCableId = 0;
        }
        m_cables.erase(it);
        return true;
    }
    return false;
}

void DspWireGrid::getProbeData(float* destination, std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0) return;
    const auto copyCount = std::min(count, probeBufferSize);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        const std::size_t idx = (m_probeWritePos + probeBufferSize - copyCount + i) % probeBufferSize;
        destination[i] = m_probeRingBuffer[idx];
    }
}

void DspWireGrid::process(ProcessContext& context) noexcept
{
    if (context.audio.channelCount == 0 || context.audio.frameCount == 0) return;

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr) return;

    if (m_moduleScratchBuffers.size() != m_modules.size() ||
        (m_moduleScratchBuffers.size() > 0 && m_moduleScratchBuffers[0].size() < frameCount))
    {
        reallocateScratchBuffers();
    }

    const float dt = 1.0f / static_cast<float>(m_sampleRate);

    // Evaluate each module in sequence
    for (std::size_t modIdx = 0; modIdx < m_modules.size(); ++modIdx)
    {
        auto& mod = m_modules[modIdx];
        auto& outBuf = m_moduleScratchBuffers[modIdx];

        // Gather static input values from connected cables if any
        float inSample0 = 0.0f;
        float inSample1 = 0.0f;
        const float* inAudio0 = nullptr;
        const float* inAudio1 = nullptr;

        for (const auto& cable : m_cables)
        {
            if (cable.dstModuleId == mod.id)
            {
                for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                {
                    if (m_modules[srcIdx].id == cable.srcModuleId)
                    {
                        if (cable.dstPort == 0)
                        {
                            inAudio0 = m_moduleScratchBuffers[srcIdx].data();
                            inSample0 = inAudio0[0];
                        }
                        else if (cable.dstPort == 1)
                        {
                            inAudio1 = m_moduleScratchBuffers[srcIdx].data();
                            inSample1 = inAudio1[0];
                        }
                        break;
                    }
                }
            }
        }

        switch (mod.type)
        {
        case GridModuleType::oscillator:
        {
            const float note = mod.params[1] + inSample0 * 12.0f;
            const float freqHz = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
            const float phaseInc = freqHz * dt;
            const auto waveform = static_cast<int>(mod.params[0]);

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                float val = 0.0f;
                if (waveform == 0) // Sine
                {
                    val = std::sin(mod.phase * 2.0f * std::numbers::pi_v<float>);
                }
                else if (waveform == 1) // Triangle
                {
                    val = 2.0f * std::abs(2.0f * (mod.phase - std::floor(mod.phase + 0.5f))) - 1.0f;
                }
                else if (waveform == 2) // Saw
                {
                    val = 2.0f * mod.phase - 1.0f;
                }
                else // Pulse / Square
                {
                    const float pw = std::clamp(mod.params[3], 0.05f, 0.95f);
                    val = (mod.phase < pw) ? 1.0f : -1.0f;
                }

                outBuf[i] = val;
                mod.phase += phaseInc;
                if (mod.phase >= 1.0f) mod.phase -= 1.0f;
            }
            break;
        }

        case GridModuleType::noise:
        {
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                mod.rngState = mod.rngState * 1664525u + 1013904223u;
                const float white = static_cast<float>(static_cast<std::int32_t>(mod.rngState)) * 4.6566129e-10f;
                outBuf[i] = white * mod.params[1];
            }
            break;
        }

        case GridModuleType::svfFilter:
        {
            const float cutoffNorm = std::clamp(mod.params[0] + inSample1 * 0.5f, 0.01f, 0.98f);
            const float cutoffHz = 20.0f * std::pow(1000.0f, cutoffNorm);
            const float resonance = std::clamp(mod.params[1], 0.05f, 0.95f);
            const float g = std::tan(std::numbers::pi_v<float> * std::clamp(cutoffHz * dt, 0.001f, 0.45f));
            const float k = 2.0f * (1.0f - resonance);

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float x = (inAudio0 != nullptr) ? inAudio0[i] : inSample0;
                const float v1 = (x - mod.filterS1 * (g + k) - mod.filterS2) / (1.0f + g * (g + k));
                const float v0 = g * v1;
                const float lp = v0 + mod.filterS1;
                mod.filterS1 = lp + v0;
                const float bp = g * lp + mod.filterS2;
                mod.filterS2 = bp + g * lp;
                outBuf[i] = lp;
            }
            break;
        }

        case GridModuleType::mathSaturation:
        {
            const float drive = 1.0f + mod.params[0] * 12.0f;
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float x = (inAudio0 != nullptr) ? inAudio0[i] : inSample0;
                outBuf[i] = std::tanh(x * drive);
            }
            break;
        }

        case GridModuleType::unitDelayFeedback:
        {
            const float gain = 1.0f + mod.params[0] * 2.5f;
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                // Output previous feedback sample
                outBuf[i] = mod.feedbackSample;

                // Process current input through DC-blocker and soft-limiter for next step
                const float x = (inAudio0 != nullptr) ? inAudio0[i] : inSample0;
                const float dcBlocked = x - mod.dcBlockerPrevX + 0.995f * mod.dcBlockerPrevY;
                mod.dcBlockerPrevX = x;
                mod.dcBlockerPrevY = dcBlocked;

                mod.feedbackSample = std::tanh(dcBlocked * gain);
            }
            break;
        }

        case GridModuleType::wavefolder:
        {
            const float foldAmount = 1.0f + (mod.params[0] + inSample1 * 0.5f) * 8.0f;
            const float bias = (mod.params[1] - 0.5f) * 0.4f;

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                float x = ((inAudio0 != nullptr) ? inAudio0[i] : inSample0) * foldAmount + bias;
                // Buchla 3-stage wavefolding
                for (int stage = 0; stage < 3; ++stage)
                {
                    if (x > 1.0f) { x = 2.0f - x; }
                    else if (x < -1.0f) { x = -2.0f - x; }
                }
                outBuf[i] = std::tanh(x);
            }
            break;
        }

        case GridModuleType::xyPad:
        {
            const float x = mod.params[0];
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                outBuf[i] = x;
            }
            break;
        }

        case GridModuleType::sampleAndHold:
        {
            const float clockInc = (0.5f + mod.params[0] * 40.0f) * dt;
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                bool trigger = false;
                if (inAudio1 != nullptr)
                {
                    if (inAudio1[i] > 0.5f && mod.shLastTrigger <= 0.5f) { trigger = true; }
                    mod.shLastTrigger = inAudio1[i];
                }
                else
                {
                    mod.phase += clockInc;
                    if (mod.phase >= 1.0f)
                    {
                        mod.phase -= 1.0f;
                        trigger = true;
                    }
                }

                if (trigger)
                {
                    if (inAudio0 != nullptr)
                    {
                        mod.shHeldValue = inAudio0[i];
                    }
                    else
                    {
                        mod.rngState = mod.rngState * 1664525u + 1013904223u;
                        mod.shHeldValue = static_cast<float>(static_cast<std::int32_t>(mod.rngState)) * 4.6566129e-10f;
                    }
                }
                outBuf[i] = mod.shHeldValue;
            }
            break;
        }

        case GridModuleType::lfo:
        {
            const float rate = 0.1f + mod.params[0] * 15.0f;
            const float depth = mod.params[1];
            const float phaseInc = rate * dt;

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float lfoVal = std::sin(mod.phase * 2.0f * std::numbers::pi_v<float>);
                outBuf[i] = lfoVal * depth;
                mod.phase += phaseInc;
                if (mod.phase >= 1.0f) mod.phase -= 1.0f;
            }
            break;
        }

        case GridModuleType::envelope:
        {
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                mod.envValue += 0.002f;
                if (mod.envValue > 1.0f) mod.envValue = 0.0f;
                outBuf[i] = 1.0f - std::abs(mod.envValue * 2.0f - 1.0f);
            }
            break;
        }

        case GridModuleType::delay:
        {
            const std::size_t delaySamples = std::clamp(
                static_cast<std::size_t>(mod.params[0] * m_sampleRate * 0.8),
                std::size_t(1), mod.delayBuffer.size() - 1);
            const float feedback = std::clamp(mod.params[1], 0.0f, 0.95f);
            const float mix = mod.params[2];

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float x = (inAudio0 != nullptr) ? inAudio0[i] : inSample0;
                const std::size_t readPos = (mod.delayWritePos + mod.delayBuffer.size() - delaySamples) % mod.delayBuffer.size();
                const float delayed = mod.delayBuffer[readPos];

                mod.delayBuffer[mod.delayWritePos] = x + delayed * feedback;
                mod.delayWritePos = (mod.delayWritePos + 1) % mod.delayBuffer.size();

                outBuf[i] = (1.0f - mix) * x + mix * delayed;
            }
            break;
        }

        case GridModuleType::audioOut:
        {
            const float masterGain = mod.params[0];
            const float pan = mod.params[1];
            const float panL = std::cos(pan * 0.5f * std::numbers::pi_v<float>);
            const float panR = std::sin(pan * 0.5f * std::numbers::pi_v<float>);

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float leftIn = (inAudio0 != nullptr) ? inAudio0[i] : 0.0f;
                const float rightIn = (inAudio1 != nullptr) ? inAudio1[i] : leftIn;

                ch0[i] = leftIn * masterGain * panL;
                if (ch1 != nullptr)
                {
                    ch1[i] = rightIn * masterGain * panR;
                }
            }
            break;
        }
        }
    }

    // Write probed signal to oscilloscope ring buffer
    if (m_probedCableId != 0)
    {
        for (const auto& cable : m_cables)
        {
            if (cable.id == m_probedCableId)
            {
                for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                {
                    if (m_modules[srcIdx].id == cable.srcModuleId)
                    {
                        const auto& buf = m_moduleScratchBuffers[srcIdx];
                        for (std::size_t i = 0; i < frameCount; ++i)
                        {
                            m_probeRingBuffer[m_probeWritePos] = buf[i];
                            m_probeWritePos = (m_probeWritePos + 1) % probeBufferSize;
                        }
                        return;
                    }
                }
                break;
            }
        }
    }
    else
    {
        // Default probe: write output of ch0
        for (std::size_t i = 0; i < frameCount; ++i)
        {
            m_probeRingBuffer[m_probeWritePos] = ch0[i];
            m_probeWritePos = (m_probeWritePos + 1) % probeBufferSize;
        }
    }
}

void DspWireGrid::loadPresetAcidBass()
{
    m_modules.clear();
    m_cables.clear();
    m_macroBindings.clear();

    const auto oscId = addModule(GridModuleType::oscillator, 80.0f, 100.0f, "Bass Osc");
    const auto lfoId = addModule(GridModuleType::lfo, 80.0f, 260.0f, "Wobble LFO");
    const auto fltId = addModule(GridModuleType::svfFilter, 280.0f, 100.0f, "Acid Filter");
    const auto satId = addModule(GridModuleType::mathSaturation, 480.0f, 100.0f, "Overdrive");
    const auto outId = addModule(GridModuleType::audioOut, 680.0f, 100.0f, "Master Out");

    setModuleParam(oscId, 0, 2.0f); // Saw
    setModuleParam(oscId, 1, 36.0f); // C2
    setModuleParam(fltId, 0, 0.35f); // Cutoff
    setModuleParam(fltId, 1, 0.85f); // High resonance
    setModuleParam(satId, 0, 0.65f); // Drive
    setModuleParam(lfoId, 0, 0.2f); // Rate
    setModuleParam(lfoId, 1, 0.5f); // Depth

    const auto c1 = connectCable(oscId, 0, fltId, 0);
    connectCable(lfoId, 0, fltId, 1);
    connectCable(fltId, 0, satId, 0);
    connectCable(satId, 0, outId, 0);

    m_probedCableId = c1;

    bindMacro(0, fltId, 0, 0.1f, 0.9f, "Filter Cutoff");
    bindMacro(1, fltId, 1, 0.2f, 0.95f, "Resonance");
    bindMacro(2, satId, 0, 0.0f, 1.0f, "Drive");
}

void DspWireGrid::loadPresetAmbientDrone()
{
    m_modules.clear();
    m_cables.clear();
    m_macroBindings.clear();

    const auto osc1 = addModule(GridModuleType::oscillator, 80.0f, 80.0f, "Drone Osc 1");
    const auto osc2 = addModule(GridModuleType::oscillator, 80.0f, 240.0f, "Drone Osc 2");
    const auto fltId = addModule(GridModuleType::svfFilter, 280.0f, 160.0f, "Warm Filter");
    const auto dlyId = addModule(GridModuleType::delay, 480.0f, 160.0f, "Space Echo");
    const auto outId = addModule(GridModuleType::audioOut, 680.0f, 160.0f, "Master Out");

    setModuleParam(osc1, 0, 0.0f); // Sine
    setModuleParam(osc1, 1, 48.0f); // C3
    setModuleParam(osc2, 0, 1.0f); // Triangle
    setModuleParam(osc2, 1, 55.0f); // G3
    setModuleParam(fltId, 0, 0.40f);
    setModuleParam(dlyId, 0, 0.55f);
    setModuleParam(dlyId, 1, 0.65f);
    setModuleParam(dlyId, 2, 0.50f);

    connectCable(osc1, 0, fltId, 0);
    connectCable(fltId, 0, dlyId, 0);
    connectCable(dlyId, 0, outId, 0);

    bindMacro(0, fltId, 0, 0.1f, 0.8f, "Space Warmth");
    bindMacro(1, dlyId, 1, 0.0f, 0.9f, "Delay Feedback");
}

void DspWireGrid::loadPresetWestCoastWavefolder()
{
    m_modules.clear();
    m_cables.clear();
    m_macroBindings.clear();

    const auto oscId = addModule(GridModuleType::oscillator, 80.0f, 100.0f, "Sine Carrier");
    const auto fldId = addModule(GridModuleType::wavefolder, 280.0f, 100.0f, "Complex Wavefolder");
    const auto lfoId = addModule(GridModuleType::lfo, 80.0f, 260.0f, "Fold Modulator");
    const auto outId = addModule(GridModuleType::audioOut, 500.0f, 100.0f, "Master Out");

    setModuleParam(oscId, 0, 0.0f); // Pure Sine
    setModuleParam(oscId, 1, 38.0f); // D2
    setModuleParam(fldId, 0, 0.70f); // High folding harmonics
    setModuleParam(lfoId, 0, 0.15f);

    connectCable(oscId, 0, fldId, 0);
    connectCable(lfoId, 0, fldId, 1);
    connectCable(fldId, 0, outId, 0);

    bindMacro(0, fldId, 0, 0.1f, 1.0f, "Fold Harmonics");
    bindMacro(1, fldId, 1, 0.0f, 1.0f, "Symmetry Bias");
}

void DspWireGrid::loadPresetPercussionSynth()
{
    m_modules.clear();
    m_cables.clear();
    m_macroBindings.clear();

    const auto nseId = addModule(GridModuleType::noise, 80.0f, 100.0f, "Transient Noise");
    const auto fltId = addModule(GridModuleType::svfFilter, 280.0f, 100.0f, "Tight Resonator");
    const auto satId = addModule(GridModuleType::mathSaturation, 480.0f, 100.0f, "Punch Clipper");
    const auto outId = addModule(GridModuleType::audioOut, 680.0f, 100.0f, "Master Out");

    setModuleParam(nseId, 1, 0.9f);
    setModuleParam(fltId, 0, 0.65f);
    setModuleParam(fltId, 1, 0.92f);
    setModuleParam(satId, 0, 0.50f);

    connectCable(nseId, 0, fltId, 0);
    connectCable(fltId, 0, satId, 0);
    connectCable(satId, 0, outId, 0);
}

void DspWireGrid::loadPresetFeedbackResonator()
{
    m_modules.clear();
    m_cables.clear();
    m_macroBindings.clear();

    const auto nseId = addModule(GridModuleType::noise, 60.0f, 120.0f, "Exciter Burst");
    const auto fltId = addModule(GridModuleType::svfFilter, 240.0f, 120.0f, "Body Resonator");
    const auto fdbId = addModule(GridModuleType::unitDelayFeedback, 440.0f, 260.0f, "Unit Delay Feedback");
    const auto fldId = addModule(GridModuleType::wavefolder, 440.0f, 120.0f, "Non-Linear Stage");
    const auto outId = addModule(GridModuleType::audioOut, 660.0f, 120.0f, "Master Out");

    setModuleParam(nseId, 1, 0.6f);
    setModuleParam(fltId, 0, 0.45f);
    setModuleParam(fltId, 1, 0.70f);
    setModuleParam(fdbId, 0, 0.85f); // High feedback loop gain

    // Exciter into resonator
    connectCable(nseId, 0, fltId, 0);
    // Resonator output into non-linear wavefolder
    connectCable(fltId, 0, fldId, 0);
    // Non-linear stage into unit delay feedback loop
    connectCable(fldId, 0, fdbId, 0);
    // Feedback output returns to resonator cutoff modulation port!
    connectCable(fdbId, 0, fltId, 1);
    // Main output
    connectCable(fldId, 0, outId, 0);

    bindMacro(0, fdbId, 0, 0.2f, 0.98f, "Feedback Resonance");
    bindMacro(1, fltId, 0, 0.1f, 0.85f, "Resonator Pitch");
}

} // namespace dawg::engine::dsp
