#include <dawg/engine/dsp/SpatialImmersive916Node.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;
constexpr double twoPi = 2.0 * std::numbers::pi_v<double>;

} // namespace

SpatialImmersive916Node::SpatialImmersive916Node() noexcept
{
    // Initialize 9.1.6 3D speaker coordinates
    m_speakerPositions = {{
        {-0.7f,  0.7f,  0.0f, false, false}, // 0: Left
        { 0.7f,  0.7f,  0.0f, false, false}, // 1: Right
        { 0.0f,  1.0f,  0.0f, false, false}, // 2: Center
        { 0.0f,  0.0f, -0.8f, false, true }, // 3: LFE
        {-1.0f,  0.0f,  0.0f, false, false}, // 4: Left Side Surround (Lss)
        { 1.0f,  0.0f,  0.0f, false, false}, // 5: Right Side Surround (Rss)
        {-0.7f, -0.7f,  0.0f, false, false}, // 6: Left Rear Surround (Lsr)
        { 0.7f, -0.7f,  0.0f, false, false}, // 7: Right Rear Surround (Rsr)
        {-0.9f,  0.4f,  0.0f, false, false}, // 8: Left Wide (Lw)
        { 0.9f,  0.4f,  0.0f, false, false}, // 9: Right Wide (Rw)
        {-0.6f,  0.6f,  0.8f, true,  false}, // 10: Left Top Front (Ltf)
        { 0.6f,  0.6f,  0.8f, true,  false}, // 11: Right Top Front (Rtf)
        {-0.7f,  0.0f,  0.9f, true,  false}, // 12: Left Top Middle (Ltm)
        { 0.7f,  0.0f,  0.9f, true,  false}, // 13: Right Top Middle (Rtm)
        {-0.6f, -0.6f,  0.8f, true,  false}, // 14: Left Top Rear (Ltr)
        { 0.6f, -0.6f,  0.8f, true,  false}  // 15: Right Top Rear (Rtr)
    }};

    updateSpeakerGains();
}

bool SpatialImmersive916Node::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    // 80 Hz LFE lowpass filter coefficient
    const float fc = 80.0f;
    m_lfeCoeff = 1.0f - std::exp(-2.0f * pi * fc / static_cast<float>(m_sampleRate));
    reset();
    updateSpeakerGains();
    return true;
}

void SpatialImmersive916Node::reset() noexcept
{
    m_trajectoryPhase = 0.0;
    m_lfeFilterZ = 0.0f;
}

void SpatialImmersive916Node::setPosition(float x, float y, float z) noexcept
{
    m_posX.store(std::clamp(x, -1.0f, 1.0f), std::memory_order_relaxed);
    m_posY.store(std::clamp(y, -1.0f, 1.0f), std::memory_order_relaxed);
    m_posZ.store(std::clamp(z, -1.0f, 1.0f), std::memory_order_relaxed);
    updateSpeakerGains();
}

void SpatialImmersive916Node::setSpherical(float azimuthDeg, float elevationDeg, float distanceMeters) noexcept
{
    const float azRad = azimuthDeg * (pi / 180.0f);
    const float elRad = elevationDeg * (pi / 180.0f);
    const float d = std::clamp(distanceMeters / 10.0f, 0.1f, 1.0f);

    const float x = d * std::sin(azRad) * std::cos(elRad);
    const float y = d * std::cos(azRad) * std::cos(elRad);
    const float z = d * std::sin(elRad);

    setPosition(x, y, z);
}

float SpatialImmersive916Node::speakerGain(std::size_t channelIndex) const noexcept
{
    if (channelIndex < numSpeakers)
    {
        return m_speakerGains[channelIndex];
    }
    return 0.0f;
}

void SpatialImmersive916Node::updateSpeakerGains() noexcept
{
    const float sx = m_posX.load(std::memory_order_relaxed);
    const float sy = m_posY.load(std::memory_order_relaxed);
    const float sz = m_posZ.load(std::memory_order_relaxed);

    float sumSq = 0.0f;

    for (std::size_t i = 0; i < numSpeakers; ++i)
    {
        if (m_speakerPositions[i].isLfe)
        {
            m_speakerGains[i] = m_lfeSend;
            continue;
        }

        const float dx = sx - m_speakerPositions[i].x;
        const float dy = sy - m_speakerPositions[i].y;
        const float dz = sz - m_speakerPositions[i].z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Normalized 3D inverse proximity falloff
        const float proximity = std::max(0.0f, 1.0f - (dist * 0.45f));
        const float weight = proximity * proximity;
        m_speakerGains[i] = weight;
        sumSq += weight * weight;
    }

    // Energy-preserving normalization
    const float norm = 1.0f / std::max(0.001f, std::sqrt(sumSq));
    for (std::size_t i = 0; i < numSpeakers; ++i)
    {
        if (!m_speakerPositions[i].isLfe)
        {
            m_speakerGains[i] *= norm;
        }
    }
}

void SpatialImmersive916Node::updateTrajectory(std::size_t numFrames) noexcept
{
    if (m_trajectory == SpatialTrajectory916::manual)
    {
        return;
    }

    const double delta = (static_cast<double>(numFrames) / m_sampleRate) * static_cast<double>(m_trajectorySpeedHz);
    m_trajectoryPhase = std::fmod(m_trajectoryPhase + delta * twoPi, twoPi);

    float px = 0.0f, py = 0.0f, pz = 0.0f;

    switch (m_trajectory)
    {
        case SpatialTrajectory916::orbit:
            px = 0.85f * static_cast<float>(std::cos(m_trajectoryPhase));
            py = 0.85f * static_cast<float>(std::sin(m_trajectoryPhase));
            pz = 0.2f * static_cast<float>(std::sin(m_trajectoryPhase * 0.5));
            break;
        case SpatialTrajectory916::spiral:
        {
            const float r = 0.3f + 0.6f * static_cast<float>(0.5 + 0.5 * std::sin(m_trajectoryPhase * 0.25));
            px = r * static_cast<float>(std::cos(m_trajectoryPhase * 2.0));
            py = r * static_cast<float>(std::sin(m_trajectoryPhase * 2.0));
            pz = static_cast<float>(std::sin(m_trajectoryPhase));
            break;
        }
        case SpatialTrajectory916::ceilingDive:
            px = 0.7f * static_cast<float>(std::sin(m_trajectoryPhase));
            py = 0.7f * static_cast<float>(std::cos(m_trajectoryPhase));
            pz = 0.8f * static_cast<float>(std::cos(m_trajectoryPhase * 2.0));
            break;
        case SpatialTrajectory916::figure8:
            px = 0.8f * static_cast<float>(std::sin(m_trajectoryPhase));
            py = 0.8f * static_cast<float>(std::sin(m_trajectoryPhase * 2.0));
            pz = 0.4f * static_cast<float>(std::cos(m_trajectoryPhase));
            break;
        case SpatialTrajectory916::manual:
            break;
    }

    m_posX.store(px, std::memory_order_relaxed);
    m_posY.store(py, std::memory_order_relaxed);
    m_posZ.store(pz, std::memory_order_relaxed);
    updateSpeakerGains();
}

void SpatialImmersive916Node::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    updateTrajectory(blockFrames);

    const float* inL = context.audio.channels[0];
    const float* inR = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : inL;

    // Buffer pointers for up to 16 channels
    std::array<float*, numSpeakers> outs {};
    for (std::size_t c = 0; c < numSpeakers; ++c)
    {
        outs[c] = (c < context.audio.channelCount && context.audio.channels[c] != nullptr)
            ? context.audio.channels[c]
            : nullptr;
    }

    for (std::size_t f = 0; f < blockFrames; ++f)
    {
        const float monoIn = 0.5f * (inL[f] + inR[f]);

        // LFE low-pass filtered signal
        m_lfeFilterZ += (monoIn - m_lfeFilterZ) * m_lfeCoeff;

        std::array<float, numSpeakers> spkSignal {};
        for (std::size_t s = 0; s < numSpeakers; ++s)
        {
            if (m_speakerPositions[s].isLfe)
            {
                spkSignal[s] = m_lfeFilterZ * m_speakerGains[s];
            }
            else
            {
                spkSignal[s] = monoIn * m_speakerGains[s];
            }
        }

        switch (m_outputFormat)
        {
            case SpatialOutputFormat::format916:
            {
                for (std::size_t s = 0; s < numSpeakers; ++s)
                {
                    if (outs[s])
                    {
                        outs[s][f] = spkSignal[s];
                    }
                }
                break;
            }
            case SpatialOutputFormat::format714:
            {
                // Fold Wide into L/R and Lss/Rss
                const float foldLw = spkSignal[8] * 0.7071f;
                const float foldRw = spkSignal[9] * 0.7071f;
                // Fold Top Middle into Front/Rear height
                const float foldLtm = spkSignal[12] * 0.7071f;
                const float foldRtm = spkSignal[13] * 0.7071f;

                if (outs[0]) outs[0][f] = spkSignal[0] + foldLw; // L
                if (outs[1]) outs[1][f] = spkSignal[1] + foldRw; // R
                if (outs[2]) outs[2][f] = spkSignal[2];          // C
                if (outs[3]) outs[3][f] = spkSignal[3];          // LFE
                if (outs[4]) outs[4][f] = spkSignal[4] + foldLw; // Lss
                if (outs[5]) outs[5][f] = spkSignal[5] + foldRw; // Rss
                if (outs[6]) outs[6][f] = spkSignal[6];          // Lsr
                if (outs[7]) outs[7][f] = spkSignal[7];          // Rsr
                if (outs[10]) outs[10][f] = spkSignal[10] + foldLtm; // Ltf
                if (outs[11]) outs[11][f] = spkSignal[11] + foldRtm; // Rtf
                if (outs[14]) outs[14][f] = spkSignal[14] + foldLtm; // Ltr
                if (outs[15]) outs[15][f] = spkSignal[15] + foldRtm; // Rtr
                break;
            }
            case SpatialOutputFormat::format51:
            {
                // Standard 5.1 fold-down
                const float leftTotal = spkSignal[0] + spkSignal[8] * 0.7f + spkSignal[10] * 0.7f;
                const float rightTotal = spkSignal[1] + spkSignal[9] * 0.7f + spkSignal[11] * 0.7f;
                const float surrL = spkSignal[4] + spkSignal[6] + spkSignal[12] * 0.7f + spkSignal[14] * 0.7f;
                const float surrR = spkSignal[5] + spkSignal[7] + spkSignal[13] * 0.7f + spkSignal[15] * 0.7f;

                if (outs[0]) outs[0][f] = leftTotal;
                if (outs[1]) outs[1][f] = rightTotal;
                if (outs[2]) outs[2][f] = spkSignal[2];
                if (outs[3]) outs[3][f] = spkSignal[3];
                if (outs[4]) outs[4][f] = surrL;
                if (outs[5]) outs[5][f] = surrR;
                break;
            }
            case SpatialOutputFormat::formatBinauralStereo:
            {
                // Binaural Headphone fold-down with ITD/ILD ear panning
                float binL = 0.0f;
                float binR = 0.0f;
                for (std::size_t s = 0; s < numSpeakers; ++s)
                {
                    const float pan = (m_speakerPositions[s].x + 1.0f) * 0.5f; // 0 (Left) to 1 (Right)
                    const float gainL = std::cos(pan * pi * 0.5f);
                    const float gainR = std::sin(pan * pi * 0.5f);
                    binL += spkSignal[s] * gainL;
                    binR += spkSignal[s] * gainR;
                }

                if (outs[0]) outs[0][f] = binL;
                if (outs[1] && outs[1] != outs[0]) outs[1][f] = binR;
                break;
            }
        }
    }
}

} // namespace dawg::engine::dsp
