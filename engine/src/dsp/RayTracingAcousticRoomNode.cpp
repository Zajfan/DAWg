#include <dawg/engine/dsp/RayTracingAcousticRoomNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;
constexpr float speedOfSound = 343.0f; // m/s
constexpr float headRadius = 0.0875f;  // ~8.75 cm

struct MaterialAbs
{
    float low;
    float mid;
    float high;
};

inline MaterialAbs getMaterialAbs(RoomMaterial mat) noexcept
{
    switch (mat)
    {
        case RoomMaterial::concrete:
            return MaterialAbs {.low = 0.01f, .mid = 0.02f, .high = 0.05f};
        case RoomMaterial::polishedWood:
            return MaterialAbs {.low = 0.15f, .mid = 0.10f, .high = 0.08f};
        case RoomMaterial::acousticFoam:
            return MaterialAbs {.low = 0.20f, .mid = 0.70f, .high = 0.95f};
        case RoomMaterial::glass:
            return MaterialAbs {.low = 0.18f, .mid = 0.06f, .high = 0.04f};
        case RoomMaterial::drywall:
            return MaterialAbs {.low = 0.29f, .mid = 0.10f, .high = 0.08f};
    }
    return MaterialAbs {.low = 0.1f, .mid = 0.1f, .high = 0.1f};
}

} // namespace

RayTracingAcousticRoomNode::RayTracingAcousticRoomNode() noexcept
{
    m_inputDelayLine.resize(maxDelayFrames, 0.0f);
    for (std::size_t i = 0; i < numFdnLines; ++i)
    {
        m_fdnDelayBuffers[i].resize(4096, 0.0f);
    }
    recalculateRays();
    updateFdnFeedback();
}

bool RayTracingAcousticRoomNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    recalculateRays();
    updateFdnFeedback();
    return true;
}

void RayTracingAcousticRoomNode::reset() noexcept
{
    std::fill(m_inputDelayLine.begin(), m_inputDelayLine.end(), 0.0f);
    m_delayWriteIdx = 0;

    for (std::size_t i = 0; i < numFdnLines; ++i)
    {
        std::fill(m_fdnDelayBuffers[i].begin(), m_fdnDelayBuffers[i].end(), 0.0f);
        m_fdnWriteIndices[i] = 0;
        m_fdnDampingZ[i] = 0.0f;
    }

    for (auto& ray : m_rays)
    {
        ray.filterZ = 0.0f;
    }
}

void RayTracingAcousticRoomNode::setRoomDimensions(float lengthM, float widthM, float heightM) noexcept
{
    m_roomLength = std::max(2.0f, lengthM);
    m_roomWidth = std::max(2.0f, widthM);
    m_roomHeight = std::max(2.0f, heightM);
    recalculateRays();
}

void RayTracingAcousticRoomNode::setSourcePosition(float x, float y, float z) noexcept
{
    m_sourceX = std::clamp(x, 0.1f, m_roomLength - 0.1f);
    m_sourceY = std::clamp(y, 0.1f, m_roomWidth - 0.1f);
    m_sourceZ = std::clamp(z, 0.1f, m_roomHeight - 0.1f);
    recalculateRays();
}

void RayTracingAcousticRoomNode::setListenerPosition(float x, float y, float z) noexcept
{
    m_listenerX = std::clamp(x, 0.1f, m_roomLength - 0.1f);
    m_listenerY = std::clamp(y, 0.1f, m_roomWidth - 0.1f);
    m_listenerZ = std::clamp(z, 0.1f, m_roomHeight - 0.1f);
    recalculateRays();
}

void RayTracingAcousticRoomNode::setWallMaterial(RoomMaterial material) noexcept
{
    m_wallMaterial = material;
    recalculateRays();
}

void RayTracingAcousticRoomNode::updateFdnFeedback() noexcept
{
    // Eyring / Sabine formula approximation for FDN decay feedback
    const float avgDelaySamples = 1500.0f;
    const float decaySamples = m_t60Decay * static_cast<float>(m_sampleRate);
    m_fdnFeedback = std::pow(10.0f, (-3.0f * avgDelaySamples) / decaySamples);
    m_fdnFeedback = std::clamp(m_fdnFeedback, 0.2f, 0.98f);
}

void RayTracingAcousticRoomNode::recalculateRays() noexcept
{
    const auto absCoeffs = getMaterialAbs(m_wallMaterial);
    const float refLow = 1.0f - absCoeffs.low;
    const float refHigh = 1.0f - absCoeffs.high;

    // Build image source coordinates:
    // (x, y, z) multipliers and bounce counts
    struct ImageSourceDef
    {
        float x;
        float y;
        float z;
        int bounces;
    };

    std::array<ImageSourceDef, maxRays> defs {};
    std::size_t defCount = 0;

    // Direct path
    defs[defCount++] = {m_sourceX, m_sourceY, m_sourceZ, 0};

    // 1st order reflections (6 walls)
    defs[defCount++] = {-m_sourceX, m_sourceY, m_sourceZ, 1};                     // -X
    defs[defCount++] = {2.0f * m_roomLength - m_sourceX, m_sourceY, m_sourceZ, 1}; // +X
    defs[defCount++] = {m_sourceX, -m_sourceY, m_sourceZ, 1};                     // -Y
    defs[defCount++] = {m_sourceX, 2.0f * m_roomWidth - m_sourceY, m_sourceZ, 1};  // +Y
    defs[defCount++] = {m_sourceX, m_sourceY, -m_sourceZ, 1};                     // Floor (-Z)
    defs[defCount++] = {m_sourceX, m_sourceY, 2.0f * m_roomHeight - m_sourceZ, 1}; // Ceiling (+Z)

    // 2nd order reflections
    defs[defCount++] = {-m_sourceX, -m_sourceY, m_sourceZ, 2};
    defs[defCount++] = {-m_sourceX, 2.0f * m_roomWidth - m_sourceY, m_sourceZ, 2};
    defs[defCount++] = {2.0f * m_roomLength - m_sourceX, -m_sourceY, m_sourceZ, 2};
    defs[defCount++] = {2.0f * m_roomLength - m_sourceX, 2.0f * m_roomWidth - m_sourceY, m_sourceZ, 2};
    defs[defCount++] = {-m_sourceX, m_sourceY, -m_sourceZ, 2};
    defs[defCount++] = {-m_sourceX, m_sourceY, 2.0f * m_roomHeight - m_sourceZ, 2};
    defs[defCount++] = {2.0f * m_roomLength - m_sourceX, m_sourceY, -m_sourceZ, 2};
    defs[defCount++] = {2.0f * m_roomLength - m_sourceX, m_sourceY, 2.0f * m_roomHeight - m_sourceZ, 2};
    defs[defCount++] = {m_sourceX, -m_sourceY, -m_sourceZ, 2};
    defs[defCount++] = {m_sourceX, -m_sourceY, 2.0f * m_roomHeight - m_sourceZ, 2};
    defs[defCount++] = {m_sourceX, 2.0f * m_roomWidth - m_sourceY, -m_sourceZ, 2};
    defs[defCount++] = {m_sourceX, 2.0f * m_roomWidth - m_sourceY, 2.0f * m_roomHeight - m_sourceZ, 2};

    m_activeRayCount = std::min(defCount, maxRays);

    for (std::size_t i = 0; i < m_activeRayCount; ++i)
    {
        const auto& d = defs[i];
        const float dx = d.x - m_listenerX;
        const float dy = d.y - m_listenerY;
        const float dz = d.z - m_listenerZ;

        const float dist = std::max(0.1f, std::sqrt(dx * dx + dy * dy + dz * dz));
        const float rXY = std::max(0.01f, std::sqrt(dx * dx + dy * dy));

        const float delaySeconds = dist / speedOfSound;
        const auto delaySamp = static_cast<std::size_t>(delaySeconds * static_cast<float>(m_sampleRate));

        // Geometric 1/r attenuation + material reflection + air absorption
        const float invDist = 1.0f / (1.0f + dist);
        const float bounceLow = std::pow(refLow, static_cast<float>(d.bounces));
        const float bounceHigh = std::pow(refHigh, static_cast<float>(d.bounces)) * std::exp(-0.0015f * dist);

        // Spherical angles (listener facing +X)
        const float azimuth = std::atan2(dy, dx);
        const float elevation = std::atan2(dz, rXY);

        auto& ray = m_rays[i];
        ray.active = true;
        ray.distanceMeters = dist;
        ray.delaySamples = std::min(delaySamp, maxDelayFrames - 1);
        ray.gainLow = invDist * bounceLow;
        ray.gainHigh = invDist * bounceHigh;

        // 1st order Ambisonics B-format (W, X, Y, Z)
        ray.bFormatW = 0.7071f * ray.gainLow;
        ray.bFormatX = std::cos(azimuth) * std::cos(elevation) * ray.gainLow;
        ray.bFormatY = std::sin(azimuth) * std::cos(elevation) * ray.gainLow;
        ray.bFormatZ = std::sin(elevation) * ray.gainLow;

        // Binaural ITD / ILD calculation
        const float sinAz = std::sin(azimuth);
        // Woodworth formula ITD
        const float itdSec = (headRadius / speedOfSound) * (sinAz + azimuth);
        ray.itdDelaySamples = itdSec * static_cast<float>(m_sampleRate);

        // ILD panning factors (left / right ear level difference)
        const float angleToLeft = azimuth - pi * 0.5f;
        const float angleToRight = azimuth + pi * 0.5f;
        ray.ildPanL = std::clamp(0.5f * (1.0f + std::cos(angleToLeft)), 0.1f, 1.0f);
        ray.ildPanR = std::clamp(0.5f * (1.0f + std::cos(angleToRight)), 0.1f, 1.0f);
    }
}

void RayTracingAcousticRoomNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    const float* inL = context.audio.channels[0];
    const float* inR = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr) ? context.audio.channels[1] : inL;

    float* out0 = context.audio.channels[0];
    float* out1 = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr) ? context.audio.channels[1] : out0;
    float* out2 = (context.audio.channelCount > 2 && context.audio.channels[2] != nullptr) ? context.audio.channels[2] : nullptr;
    float* out3 = (context.audio.channelCount > 3 && context.audio.channels[3] != nullptr) ? context.audio.channels[3] : nullptr;

    const bool bFormatOut = (m_outputMode == RoomOutputMode::bFormat4Channel);

    for (std::size_t f = 0; f < blockFrames; ++f)
    {
        const float monoIn = 0.5f * (inL[f] + inR[f]);

        // Write to circular delay line
        m_inputDelayLine[m_delayWriteIdx] = monoIn;

        float sumW = 0.0f;
        float sumX = 0.0f;
        float sumY = 0.0f;
        float sumZ = 0.0f;

        float binL = 0.0f;
        float binR = 0.0f;

        // Accumulate ray-traced early reflections
        for (std::size_t r = 0; r < m_activeRayCount; ++r)
        {
            auto& ray = m_rays[r];
            if (!ray.active) continue;

            // Read from circular delay line
            const std::size_t readIdx = (m_delayWriteIdx + maxDelayFrames - ray.delaySamples) % maxDelayFrames;
            const float rawSamp = m_inputDelayLine[readIdx];

            // Single-pole dampening filter for frequency-dependent boundary absorption
            const float dampCoeff = std::clamp(ray.gainHigh / std::max(0.001f, ray.gainLow), 0.05f, 0.95f);
            ray.filterZ += (rawSamp - ray.filterZ) * dampCoeff;
            const float filteredSamp = ray.filterZ * ray.gainLow;

            sumW += filteredSamp * ray.bFormatW;
            sumX += filteredSamp * ray.bFormatX;
            sumY += filteredSamp * ray.bFormatY;
            sumZ += filteredSamp * ray.bFormatZ;

            // Binaural ear accumulation
            binL += filteredSamp * ray.ildPanL;
            binR += filteredSamp * ray.ildPanR;
        }

        // FDN Diffuse Late Reverb Tank
        // Feed early reflection W energy into FDN
        const float fdnInput = sumW * 0.5f;

        std::array<float, numFdnLines> fdnOutputs {};
        for (std::size_t i = 0; i < numFdnLines; ++i)
        {
            const auto len = m_fdnLengths[i];
            const auto rIdx = (m_fdnWriteIndices[i] + 4096 - len) % 4096;
            const float lineOut = m_fdnDelayBuffers[i][rIdx];

            // High frequency dampening in FDN tank
            m_fdnDampingZ[i] += (lineOut - m_fdnDampingZ[i]) * 0.4f;
            fdnOutputs[i] = m_fdnDampingZ[i];
        }

        // 4x4 Hadamard Matrix for lossless unitary feedback mixing
        const float h0 = 0.5f * ( fdnOutputs[0] + fdnOutputs[1] + fdnOutputs[2] + fdnOutputs[3] );
        const float h1 = 0.5f * ( fdnOutputs[0] - fdnOutputs[1] + fdnOutputs[2] - fdnOutputs[3] );
        const float h2 = 0.5f * ( fdnOutputs[0] + fdnOutputs[1] - fdnOutputs[2] - fdnOutputs[3] );
        const float h3 = 0.5f * ( fdnOutputs[0] - fdnOutputs[1] - fdnOutputs[2] + fdnOutputs[3] );

        for (std::size_t i = 0; i < numFdnLines; ++i)
        {
            float mixedIn = fdnInput;
            if (i == 0) mixedIn += h0 * m_fdnFeedback;
            if (i == 1) mixedIn += h1 * m_fdnFeedback;
            if (i == 2) mixedIn += h2 * m_fdnFeedback;
            if (i == 3) mixedIn += h3 * m_fdnFeedback;

            m_fdnDelayBuffers[i][m_fdnWriteIndices[i]] = mixedIn;
            m_fdnWriteIndices[i] = (m_fdnWriteIndices[i] + 1) % 4096;
        }

        const float lateL = 0.5f * (fdnOutputs[0] + fdnOutputs[2]);
        const float lateR = 0.5f * (fdnOutputs[1] + fdnOutputs[3]);

        m_delayWriteIdx = (m_delayWriteIdx + 1) % maxDelayFrames;

        if (bFormatOut && out2 && out3)
        {
            // Output B-Format: ch0=W, ch1=X, ch2=Y, ch3=Z
            out0[f] = sumW + (lateL + lateR) * 0.5f * m_lateMix;
            out1[f] = sumX;
            out2[f] = sumY + (lateL - lateR) * 0.5f * m_lateMix;
            out3[f] = sumZ;
        }
        else
        {
            // Output Binaural Stereo
            out0[f] = binL + lateL * m_lateMix;
            if (out1 != out0)
            {
                out1[f] = binR + lateR * m_lateMix;
            }
        }
    }
}

} // namespace dawg::engine::dsp
