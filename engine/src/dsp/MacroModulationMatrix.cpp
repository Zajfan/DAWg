#include <dawg/engine/dsp/MacroModulationMatrix.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

MacroModulationMatrix::MacroModulationMatrix() noexcept
{
    // Default 4-node ADSR-style MSEG
    m_msegNodes = {
        {0.0f,  0.0f, 0.0f},
        {0.2f,  1.0f, 0.4f},
        {0.6f,  0.4f, -0.3f},
        {1.0f,  0.0f, 0.0f}
    };
    reset();
}

void MacroModulationMatrix::prepare(double sampleRate) noexcept
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    setAudioFollowerAttack(5.0f);
    setAudioFollowerRelease(80.0f);
}

void MacroModulationMatrix::reset() noexcept
{
    m_macros.fill(0.5f);
    m_msegPhase = 0.0f;
    m_msegSustaining = false;
    m_currentMsegVal = 0.0f;

    m_lorenzX = 0.1f;
    m_lorenzY = 0.0f;
    m_lorenzZ = 0.0f;
    m_lorenzNormalizedX = 0.5f;
    m_lorenzNormalizedY = 0.5f;

    m_audioFollowerLevel = 0.0f;
    m_mpePressure = 0.0f;
    m_mpeTimbre = 0.5f;
    m_mpePitchBend = 0.0f;

    for (auto& r : m_routes)
    {
        r.smoothedValue = 0.0f;
    }
}

void MacroModulationMatrix::setMacro(std::size_t index, float value) noexcept
{
    if (index < MaxMacros)
    {
        m_macros[index] = std::clamp(value, 0.0f, 1.0f);
    }
}

float MacroModulationMatrix::macro(std::size_t index) const noexcept
{
    return (index < MaxMacros) ? m_macros[index] : 0.0f;
}

void MacroModulationMatrix::setMsegNodes(const std::vector<MsegNode>& nodes)
{
    if (nodes.empty())
    {
        return;
    }
    m_msegNodes = nodes;
    std::sort(m_msegNodes.begin(), m_msegNodes.end(), [](const MsegNode& a, const MsegNode& b) {
        return a.time < b.time;
    });
}

void MacroModulationMatrix::setAudioFollowerAttack(float ms) noexcept
{
    const float att = std::max(0.1f, ms);
    m_followerAttackCoeff = 1.0f - std::exp(-1.0f / (0.001f * att * static_cast<float>(m_sampleRate)));
}

void MacroModulationMatrix::setAudioFollowerRelease(float ms) noexcept
{
    const float rel = std::max(1.0f, ms);
    m_followerReleaseCoeff = 1.0f - std::exp(-1.0f / (0.001f * rel * static_cast<float>(m_sampleRate)));
}

void MacroModulationMatrix::processMidi(const MidiBufferView& midi) noexcept
{
    if (midi.events == nullptr || midi.eventCount == 0)
    {
        return;
    }

    for (std::size_t i = 0; i < midi.eventCount; ++i)
    {
        const auto& ev = midi.events[i];
        if (ev.size < 2)
        {
            continue;
        }

        const auto status = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
        const auto d1 = static_cast<std::uint8_t>(ev.data[1]) & 0x7F;
        const auto d2 = (ev.size >= 3) ? (static_cast<std::uint8_t>(ev.data[2]) & 0x7F) : 0;

        if (status == 0x90 && d2 > 0)
        {
            triggerMseg();
            m_mpePressure = static_cast<float>(d2) / 127.0f;
        }
        else if (status == 0x80 || (status == 0x90 && d2 == 0))
        {
            releaseMseg();
        }
        else if (status == 0xE0)
        {
            // 14-bit pitch bend: d1 = LSB, d2 = MSB
            const int bendVal = static_cast<int>(d1) | (static_cast<int>(d2) << 7);
            m_mpePitchBend = std::clamp(static_cast<float>(bendVal - 8192) / 8192.0f, -1.0f, 1.0f);
        }
        else if (status == 0xD0)
        {
            // Channel pressure
            m_mpePressure = static_cast<float>(d1) / 127.0f;
        }
        else if (status == 0xB0 && d1 == 74)
        {
            // CC74 MPE Timbre
            m_mpeTimbre = static_cast<float>(d2) / 127.0f;
        }
    }
}

bool MacroModulationMatrix::addRoute(const ModulationRoute& route)
{
    if (m_routes.size() >= MaxRoutes)
    {
        return false;
    }
    m_routes.push_back(route);
    return true;
}

void MacroModulationMatrix::clearRoutes() noexcept
{
    m_routes.clear();
}

float MacroModulationMatrix::evaluateMseg(float phase) const noexcept
{
    if (m_msegNodes.empty())
    {
        return 0.0f;
    }
    if (m_msegNodes.size() == 1)
    {
        return m_msegNodes[0].level;
    }

    const float p = std::clamp(phase, 0.0f, 1.0f);
    if (p <= m_msegNodes.front().time)
    {
        return m_msegNodes.front().level;
    }
    if (p >= m_msegNodes.back().time)
    {
        return m_msegNodes.back().level;
    }

    for (std::size_t i = 0; i < m_msegNodes.size() - 1; ++i)
    {
        const auto& n0 = m_msegNodes[i];
        const auto& n1 = m_msegNodes[i + 1];

        if (p >= n0.time && p <= n1.time)
        {
            const float span = std::max(1.0e-5f, n1.time - n0.time);
            float u = (p - n0.time) / span;

            // Apply curve tension
            if (n0.tension > 0.001f)
            {
                u = std::pow(u, 1.0f + 2.5f * n0.tension);
            }
            else if (n0.tension < -0.001f)
            {
                u = 1.0f - std::pow(1.0f - u, 1.0f - 2.5f * n0.tension);
            }

            return n0.level + u * (n1.level - n0.level);
        }
    }

    return m_msegNodes.back().level;
}

void MacroModulationMatrix::updateLorenzAttractor(float dt) noexcept
{
    // Canonical Lorenz parameters: sigma = 10, rho = 28, beta = 8/3
    constexpr float sigma = 10.0f;
    constexpr float rho = 28.0f;
    constexpr float beta = 8.0f / 3.0f;

    const float rate = m_lorenzSpeedHz * 0.08f;
    const float h = dt * rate;

    const float dx = sigma * (m_lorenzY - m_lorenzX);
    const float dy = m_lorenzX * (rho - m_lorenzZ) - m_lorenzY;
    const float dz = m_lorenzX * m_lorenzY - beta * m_lorenzZ;

    m_lorenzX = std::clamp(m_lorenzX + h * dx, -35.0f, 35.0f);
    m_lorenzY = std::clamp(m_lorenzY + h * dy, -45.0f, 45.0f);
    m_lorenzZ = std::clamp(m_lorenzZ + h * dz, 0.0f, 60.0f);

    m_lorenzNormalizedX = std::clamp((m_lorenzX + 30.0f) / 60.0f, 0.0f, 1.0f);
    m_lorenzNormalizedY = std::clamp((m_lorenzY + 35.0f) / 70.0f, 0.0f, 1.0f);
}

float MacroModulationMatrix::getSourceValue(ModSource source) const noexcept
{
    switch (source)
    {
    case ModSource::macro1: return m_macros[0];
    case ModSource::macro2: return m_macros[1];
    case ModSource::macro3: return m_macros[2];
    case ModSource::macro4: return m_macros[3];
    case ModSource::macro5: return m_macros[4];
    case ModSource::macro6: return m_macros[5];
    case ModSource::macro7: return m_macros[6];
    case ModSource::macro8: return m_macros[7];
    case ModSource::lfo1:   return m_lorenzNormalizedX; // Shared smooth LFO
    case ModSource::lfo2:   return m_lorenzNormalizedY;
    case ModSource::mseg:   return m_currentMsegVal;
    case ModSource::lorenzChaos: return m_lorenzNormalizedX;
    case ModSource::audioFollower: return m_audioFollowerLevel;
    case ModSource::mpePressure: return m_mpePressure;
    case ModSource::mpeTimbre: return m_mpeTimbre;
    case ModSource::mpePitchBend: return m_mpePitchBend;
    case ModSource::count: return 0.0f;
    }
    return 0.0f;
}

void MacroModulationMatrix::process(
    const float* const* audioChannels,
    std::size_t numChannels,
    std::size_t frameCount) noexcept
{
    const float dt = static_cast<float>(frameCount) / static_cast<float>(m_sampleRate);

    // 1. Progress MSEG
    if (!m_msegSustaining)
    {
        m_msegPhase += m_msegRateHz * dt;
        if (m_msegPhase >= 1.0f)
        {
            if (m_msegLoopMode == MsegLoopMode::loop)
            {
                m_msegPhase -= std::floor(m_msegPhase);
            }
            else
            {
                m_msegPhase = 1.0f;
            }
        }
    }
    m_currentMsegVal = evaluateMseg(m_msegPhase);

    // 2. Progress Lorenz Strange Attractor
    updateLorenzAttractor(dt);

    // 3. Audio Follower Level Detection
    if (audioChannels != nullptr && numChannels > 0 && frameCount > 0)
    {
        float peak = 0.0f;
        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            if (audioChannels[ch] != nullptr)
            {
                for (std::size_t i = 0; i < frameCount; ++i)
                {
                    peak = std::max(peak, std::abs(audioChannels[ch][i]));
                }
            }
        }
        const float coeff = (peak > m_audioFollowerLevel) ? m_followerAttackCoeff : m_followerReleaseCoeff;
        m_audioFollowerLevel += coeff * (peak - m_audioFollowerLevel);
    }

    // 4. Update smoothed values for all active routes
    for (auto& route : m_routes)
    {
        const float srcVal = getSourceValue(route.source);
        const float slewSec = std::max(0.0005f, route.slewMs * 0.001f);
        const float slewCoeff = 1.0f - std::exp(-dt / slewSec);
        route.smoothedValue += slewCoeff * (srcVal - route.smoothedValue);
    }
}

float MacroModulationMatrix::evaluateModulatedParam(std::uint32_t targetParamId, float baseValue) const noexcept
{
    float totalOffset = 0.0f;
    float minClamp = 0.0f;
    float maxClamp = 1.0f;
    bool hasRoute = false;

    for (const auto& r : m_routes)
    {
        if (r.targetParamId == targetParamId)
        {
            // Bipolar depth scaling: depth (-1.0 to 1.0) * value
            totalOffset += r.depth * r.smoothedValue;
            minClamp = r.minClamp;
            maxClamp = r.maxClamp;
            hasRoute = true;
        }
    }

    if (!hasRoute)
    {
        return baseValue;
    }

    return std::clamp(baseValue + totalOffset, minClamp, maxClamp);
}

ModulationRingState MacroModulationMatrix::getModulationRing(std::uint32_t targetParamId, float baseValue) const noexcept
{
    ModulationRingState ring;
    ring.baseValue = baseValue;
    ring.currentValue = baseValue;
    ring.arcMin = baseValue;
    ring.arcMax = baseValue;

    float totalOffset = 0.0f;
    float maxPosSpread = 0.0f;
    float maxNegSpread = 0.0f;
    float minClamp = 0.0f;
    float maxClamp = 1.0f;
    bool found = false;

    for (const auto& r : m_routes)
    {
        if (r.targetParamId == targetParamId)
        {
            totalOffset += r.depth * r.smoothedValue;
            if (r.depth > 0.0f)
            {
                maxPosSpread += r.depth;
            }
            else
            {
                maxNegSpread += std::abs(r.depth);
            }
            minClamp = r.minClamp;
            maxClamp = r.maxClamp;
            found = true;
        }
    }

    if (!found)
    {
        return ring;
    }

    ring.currentValue = std::clamp(baseValue + totalOffset, minClamp, maxClamp);
    ring.arcMin = std::clamp(baseValue - maxNegSpread, minClamp, maxClamp);
    ring.arcMax = std::clamp(baseValue + maxPosSpread, minClamp, maxClamp);

    return ring;
}

} // namespace dawg::engine::dsp
