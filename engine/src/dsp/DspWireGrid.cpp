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
    }
}

float DspWireGrid::macro(std::size_t index) const noexcept
{
    return (index < m_macros.size()) ? m_macros[index] : 0.0f;
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
    }

    m_modules.push_back(std::move(mod));
    reallocateScratchBuffers();
    return m_modules.back().id;
}

bool DspWireGrid::removeModule(std::uint32_t moduleId)
{
    // Remove connected cables
    std::erase_if(m_cables, [moduleId](const GridCable& c) {
        return c.srcModuleId == moduleId || c.dstModuleId == moduleId;
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
    // Disconnect any existing cable connected to the same destination input port
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
        m_cables.erase(it);
        if (m_probedCableId == cableId)
        {
            m_probedCableId = 0;
        }
        return true;
    }
    return false;
}

void DspWireGrid::getProbeData(float* destination, std::size_t count) const noexcept
{
    if (destination == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, probeBufferSize);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        const auto idx = (m_probeWritePos + probeBufferSize - copyCount + i) % probeBufferSize;
        destination[i] = m_probeRingBuffer[idx];
    }
}

void DspWireGrid::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = std::min(context.audio.frameCount, m_maxBlockSize);
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return;
    }

    // Zero scratch buffers
    for (auto& buf : m_moduleScratchBuffers)
    {
        std::fill(buf.begin(), buf.begin() + frameCount, 0.0f);
    }

    // Process modules
    const float dt = 1.0f / static_cast<float>(m_sampleRate);

    for (std::size_t modIdx = 0; modIdx < m_modules.size(); ++modIdx)
    {
        auto& mod = m_modules[modIdx];
        auto& outBuf = m_moduleScratchBuffers[modIdx];

        // Gather inputs from connected cables
        float inSample0 = 0.0f;
        float inSample1 = 0.0f;

        for (const auto& cable : m_cables)
        {
            if (cable.dstModuleId == mod.id)
            {
                // Find source module index
                for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                {
                    if (m_modules[srcIdx].id == cable.srcModuleId)
                    {
                        // Use latest value from src scratch buffer
                        const float val = m_moduleScratchBuffers[srcIdx][0];
                        if (cable.dstPort == 0) inSample0 += val;
                        else if (cable.dstPort == 1) inSample1 += val;
                        break;
                    }
                }
            }
        }

        switch (mod.type)
        {
        case GridModuleType::oscillator:
        {
            const float note = mod.params[1] + inSample0 * 12.0f; // 1V/Oct CV
            const float freq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
            const float phaseInc = std::clamp(freq * dt, 0.0001f, 0.45f);

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                float sig = 0.0f;
                const int wave = static_cast<int>(mod.params[0]);
                if (wave == 0) // Sine
                {
                    sig = std::sin(mod.phase * 2.0f * std::numbers::pi_v<float>);
                }
                else if (wave == 1) // Triangle
                {
                    sig = 4.0f * std::abs(mod.phase - 0.5f) - 1.0f;
                }
                else if (wave == 2) // Saw
                {
                    sig = 2.0f * mod.phase - 1.0f;
                }
                else // Pulse
                {
                    const float pw = std::clamp(mod.params[3], 0.05f, 0.95f);
                    sig = (mod.phase < pw) ? 0.9f : -0.9f;
                }

                outBuf[i] = sig;
                mod.phase += phaseInc;
                if (mod.phase >= 1.0f) mod.phase -= 1.0f;
            }
            break;
        }

        case GridModuleType::noise:
        {
            // Simple fast PRNG
            static std::uint32_t seed = 0x12345678;
            const float level = mod.params[1];
            for (std::size_t i = 0; i < frameCount; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float white = static_cast<float>(seed) / 4294967296.0f * 2.0f - 1.0f;
                // Simple low-pass smoothing for pink color
                mod.filterS1 = 0.8f * mod.filterS1 + 0.2f * white;
                outBuf[i] = (mod.params[0] * mod.filterS1 + (1.0f - mod.params[0]) * white) * level;
            }
            break;
        }

        case GridModuleType::svfFilter:
        {
            // Find input audio source buffer
            const float* inAudio = nullptr;
            for (const auto& cable : m_cables)
            {
                if (cable.dstModuleId == mod.id && cable.dstPort == 0)
                {
                    for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                    {
                        if (m_modules[srcIdx].id == cable.srcModuleId)
                        {
                            inAudio = m_moduleScratchBuffers[srcIdx].data();
                            break;
                        }
                    }
                    break;
                }
            }

            const float cutoffNorm = std::clamp(mod.params[0] + inSample1 * 0.5f, 0.01f, 0.98f);
            const float cutoffHz = 20.0f * std::pow(1000.0f, cutoffNorm);
            const float resonance = std::clamp(mod.params[1], 0.05f, 0.95f);
            const float g = std::tan(std::numbers::pi_v<float> * std::clamp(cutoffHz * dt, 0.001f, 0.45f));
            const float k = 2.0f * (1.0f - resonance);

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float x = (inAudio != nullptr) ? inAudio[i] : inSample0;
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
            const float* inAudio = nullptr;
            for (const auto& cable : m_cables)
            {
                if (cable.dstModuleId == mod.id && cable.dstPort == 0)
                {
                    for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                    {
                        if (m_modules[srcIdx].id == cable.srcModuleId)
                        {
                            inAudio = m_moduleScratchBuffers[srcIdx].data();
                            break;
                        }
                    }
                    break;
                }
            }

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float x = (inAudio != nullptr) ? inAudio[i] : inSample0;
                outBuf[i] = std::tanh(x * drive);
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
                // Auto-cycling envelope for demonstration
                mod.envValue += 0.002f;
                if (mod.envValue > 1.0f) mod.envValue = 0.0f;
                outBuf[i] = 1.0f - std::abs(mod.envValue * 2.0f - 1.0f);
            }
            break;
        }

        case GridModuleType::delay:
        {
            const float* inAudio = nullptr;
            for (const auto& cable : m_cables)
            {
                if (cable.dstModuleId == mod.id && cable.dstPort == 0)
                {
                    for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                    {
                        if (m_modules[srcIdx].id == cable.srcModuleId)
                        {
                            inAudio = m_moduleScratchBuffers[srcIdx].data();
                            break;
                        }
                    }
                    break;
                }
            }

            if (! mod.delayBuffer.empty())
            {
                const auto delaySamples = static_cast<std::size_t>(std::clamp(mod.params[0] * 0.5f * static_cast<float>(m_sampleRate), 10.0f, static_cast<float>(mod.delayBuffer.size() - 1)));
                const float fb = std::clamp(mod.params[1], 0.0f, 0.85f);
                const float mix = std::clamp(mod.params[2], 0.0f, 1.0f);
                const auto bufSize = mod.delayBuffer.size();

                for (std::size_t i = 0; i < frameCount; ++i)
                {
                    const float inSig = (inAudio != nullptr) ? inAudio[i] : inSample0;
                    const auto readPos = (mod.delayWritePos + bufSize - delaySamples) % bufSize;
                    const float delayed = mod.delayBuffer[readPos];

                    mod.delayBuffer[mod.delayWritePos] = inSig + delayed * fb;
                    mod.delayWritePos = (mod.delayWritePos + 1) % bufSize;

                    outBuf[i] = inSig * (1.0f - mix) + delayed * mix;
                }
            }
            break;
        }

        case GridModuleType::audioOut:
        {
            // Sum connected signals to master output
            const float gain = mod.params[0];
            const float* inL = nullptr;
            const float* inR = nullptr;

            for (const auto& cable : m_cables)
            {
                if (cable.dstModuleId == mod.id)
                {
                    for (std::size_t srcIdx = 0; srcIdx < m_modules.size(); ++srcIdx)
                    {
                        if (m_modules[srcIdx].id == cable.srcModuleId)
                        {
                            if (cable.dstPort == 0) inL = m_moduleScratchBuffers[srcIdx].data();
                            else if (cable.dstPort == 1) inR = m_moduleScratchBuffers[srcIdx].data();
                            break;
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < frameCount; ++i)
            {
                const float l = (inL != nullptr) ? inL[i] * gain : 0.0f;
                const float r = (inR != nullptr) ? inR[i] * gain : l;
                ch0[i] += l;
                if (ch1 != nullptr)
                {
                    ch1[i] += r;
                }
            }
            break;
        }
        }
    }

    // Write probed cable data to probe ring buffer
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
                        break;
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
    m_nextModuleId = 1;
    m_nextCableId = 1;

    // 1. Oscillator (Saw wave, Bass C2)
    const auto oscId = addModule(GridModuleType::oscillator, 40.0f, 60.0f, "Bass 303 Saw");
    setModuleParam(oscId, 0, 2.0f); // Saw
    setModuleParam(oscId, 1, 36.0f); // C2

    // 2. LFO
    const auto lfoId = addModule(GridModuleType::lfo, 40.0f, 260.0f, "Filter LFO");
    setModuleParam(lfoId, 0, 0.35f);
    setModuleParam(lfoId, 1, 0.45f);

    // 3. SVF Filter
    const auto fltId = addModule(GridModuleType::svfFilter, 260.0f, 60.0f, "Resonant SVF");
    setModuleParam(fltId, 0, 0.38f);
    setModuleParam(fltId, 1, 0.82f);

    // 4. Tanh Saturation
    const auto satId = addModule(GridModuleType::mathSaturation, 480.0f, 60.0f, "Acid Drive");
    setModuleParam(satId, 0, 0.65f);

    // 5. Master Out
    const auto outId = addModule(GridModuleType::audioOut, 700.0f, 60.0f, "Main Output");
    setModuleParam(outId, 0, 0.75f);

    // Connections
    connectCable(oscId, 0, fltId, 0, 0x00E5FFFF); // Osc Out -> Filter Audio In
    connectCable(lfoId, 0, fltId, 1, 0xFF9100FF); // LFO Out -> Filter Cutoff CV
    connectCable(fltId, 0, satId, 0, 0x00E5FFFF); // Filter LP -> Saturation In
    connectCable(satId, 0, outId, 0, 0x33FF66FF); // Saturation Out -> Master Left
    connectCable(satId, 0, outId, 1, 0x33FF66FF); // Saturation Out -> Master Right

    m_probedCableId = 1; // Probe Osc Out cable
}

void DspWireGrid::loadPresetAmbientDrone()
{
    m_modules.clear();
    m_cables.clear();
    m_nextModuleId = 1;
    m_nextCableId = 1;

    const auto oscId = addModule(GridModuleType::oscillator, 40.0f, 60.0f, "Drone Tri");
    setModuleParam(oscId, 0, 1.0f); // Tri
    setModuleParam(oscId, 1, 48.0f); // C3

    const auto noiseId = addModule(GridModuleType::noise, 40.0f, 240.0f, "Pink Air");
    setModuleParam(noiseId, 0, 0.6f);
    setModuleParam(noiseId, 1, 0.3f);

    const auto delayId = addModule(GridModuleType::delay, 260.0f, 100.0f, "Space Delay");
    setModuleParam(delayId, 0, 0.45f);
    setModuleParam(delayId, 1, 0.75f);
    setModuleParam(delayId, 2, 0.5f);

    const auto fltId = addModule(GridModuleType::svfFilter, 480.0f, 100.0f, "Warm Filter");
    setModuleParam(fltId, 0, 0.5f);
    setModuleParam(fltId, 1, 0.4f);

    const auto outId = addModule(GridModuleType::audioOut, 700.0f, 100.0f, "Main Output");

    connectCable(oscId, 0, delayId, 0, 0x00E5FFFF);
    connectCable(delayId, 0, fltId, 0, 0xFF9100FF);
    connectCable(fltId, 0, outId, 0, 0x33FF66FF);
    connectCable(fltId, 0, outId, 1, 0x33FF66FF);
}

void DspWireGrid::loadPresetWestCoastWavefolder()
{
    m_modules.clear();
    m_cables.clear();
    m_nextModuleId = 1;
    m_nextCableId = 1;

    const auto oscId = addModule(GridModuleType::oscillator, 40.0f, 60.0f, "Sine Complex");
    setModuleParam(oscId, 0, 0.0f); // Sine
    setModuleParam(oscId, 1, 40.0f); // E2

    const auto lfoId = addModule(GridModuleType::lfo, 40.0f, 240.0f, "Wavefold Mod");
    setModuleParam(lfoId, 0, 0.2f);
    setModuleParam(lfoId, 1, 0.8f);

    const auto foldId = addModule(GridModuleType::mathSaturation, 260.0f, 100.0f, "Wavefolder");
    setModuleParam(foldId, 0, 0.85f);

    const auto outId = addModule(GridModuleType::audioOut, 500.0f, 100.0f, "Main Output");

    connectCable(oscId, 0, foldId, 0, 0x00E5FFFF);
    connectCable(foldId, 0, outId, 0, 0x33FF66FF);
    connectCable(foldId, 0, outId, 1, 0x33FF66FF);
}

void DspWireGrid::loadPresetPercussionSynth()
{
    m_modules.clear();
    m_cables.clear();
    m_nextModuleId = 1;
    m_nextCableId = 1;

    const auto noiseId = addModule(GridModuleType::noise, 40.0f, 60.0f, "Snare Noise");
    const auto oscId = addModule(GridModuleType::oscillator, 40.0f, 240.0f, "Kick Body");
    setModuleParam(oscId, 0, 0.0f); // Sine
    setModuleParam(oscId, 1, 33.0f); // A1

    const auto fltId = addModule(GridModuleType::svfFilter, 260.0f, 100.0f, "Snappy SVF");
    setModuleParam(fltId, 0, 0.7f);
    setModuleParam(fltId, 1, 0.5f);

    const auto outId = addModule(GridModuleType::audioOut, 500.0f, 100.0f, "Main Output");

    connectCable(noiseId, 0, fltId, 0, 0x00E5FFFF);
    connectCable(fltId, 0, outId, 0, 0x33FF66FF);
    connectCable(oscId, 0, outId, 1, 0xFF9100FF);
}

} // namespace dawg::engine::dsp
