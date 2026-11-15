#include <dawg/engine/dsp/SpatialSurroundPannerNode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <sstream>

namespace dawg::engine::dsp {

SpatialSurroundPannerNode::SpatialSurroundPannerNode() noexcept
{
    setupSpeakerPositions();
    updateGains();
}

void SpatialSurroundPannerNode::setupSpeakerPositions() noexcept
{
    // 7.1.4 Dolby Atmos speaker 3D coordinate layout
    // 0: Left, 1: Right, 2: Center, 3: LFE
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::left)] = {-0.707f, 0.707f, 0.0f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::right)] = {0.707f, 0.707f, 0.0f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::center)] = {0.0f, 1.0f, 0.0f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::lfe)] = {0.0f, 0.0f, -0.5f};

    // 4: Left Side Surround, 5: Right Side Surround
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::leftSideSurround)] = {-1.0f, 0.0f, 0.0f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::rightSideSurround)] = {1.0f, 0.0f, 0.0f};

    // 6: Left Rear Surround, 7: Right Rear Surround
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::leftRearSurround)] = {-0.707f, -0.707f, 0.0f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::rightRearSurround)] = {0.707f, -0.707f, 0.0f};

    // 8: Top Front Left, 9: Top Front Right
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::topFrontLeft)] = {-0.707f, 0.707f, 0.85f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::topFrontRight)] = {0.707f, 0.707f, 0.85f};

    // 10: Top Rear Left, 11: Top Rear Right
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::topRearLeft)] = {-0.707f, -0.707f, 0.85f};
    m_speakerPositions[static_cast<std::size_t>(SurroundChannel714::topRearRight)] = {0.707f, -0.707f, 0.85f};
}

bool SpatialSurroundPannerNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void SpatialSurroundPannerNode::reset() noexcept
{
    for (auto& st : m_binauralStates)
    {
        st.filterL = 0.0f;
        st.filterR = 0.0f;
        std::fill(std::begin(st.delayLineL), std::end(st.delayLineL), 0.0f);
        std::fill(std::begin(st.delayLineR), std::end(st.delayLineR), 0.0f);
        st.writePos = 0;
    }
}

void SpatialSurroundPannerNode::setCoordinates(const float x, const float y, const float z) noexcept
{
    m_x = std::clamp(x, -1.0f, 1.0f);
    m_y = std::clamp(y, -1.0f, 1.0f);
    m_z = std::clamp(z, -1.0f, 1.0f);
    updateGains();
}

void SpatialSurroundPannerNode::updateGains() noexcept
{
    // Equal-power 3D distance weight distribution across 11 main speakers
    float sumPower = 0.0f;

    for (std::size_t i = 0; i < numSpeakers; ++i)
    {
        if (i == static_cast<std::size_t>(SurroundChannel714::lfe))
        {
            continue; // LFE handled independently
        }

        const auto& spk = m_speakerPositions[i];
        const float dx = m_x - spk.x;
        const float dy = m_y - spk.y;
        const float dz = m_z - spk.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        // Inverse distance with proximity radius and size spread
        const float radius = 0.15f + m_sizeSpread * 0.45f;
        const float weight = 1.0f / (dist * dist + radius);
        m_speakerGains[i] = weight;
        sumPower += weight * weight;
    }

    const float norm = (sumPower > 1e-6f) ? (1.0f / std::sqrt(sumPower)) : 1.0f;
    for (std::size_t i = 0; i < numSpeakers; ++i)
    {
        if (i != static_cast<std::size_t>(SurroundChannel714::lfe))
        {
            m_speakerGains[i] *= norm;
        }
    }

    // LFE subwoofer send gain
    m_lfeGainLinear = (m_lfeSendDb > -59.0f) ? std::pow(10.0f, m_lfeSendDb * 0.05f) : 0.0f;
    m_speakerGains[static_cast<std::size_t>(SurroundChannel714::lfe)] = m_lfeGainLinear;
}

float SpatialSurroundPannerNode::speakerGain(const SurroundChannel714 ch) const noexcept
{
    const auto idx = static_cast<std::size_t>(ch);
    if (idx < numSpeakers)
    {
        return m_speakerGains[idx];
    }
    return 0.0f;
}

void SpatialSurroundPannerNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return;
    }

    // If channelCount >= 12, this is a native multi-channel surround bus
    if (context.audio.channelCount >= numSpeakers)
    {
        for (std::size_t f = 0; f < frameCount; ++f)
        {
            const float inMono = (ch1 != nullptr) ? (ch0[f] + ch1[f]) * 0.5f : ch0[f];
            for (std::size_t s = 0; s < numSpeakers; ++s)
            {
                if (context.audio.channels[s] != nullptr)
                {
                    context.audio.channels[s][f] = inMono * m_speakerGains[s];
                }
            }
        }
        return;
    }

    // Otherwise downmix to Stereo / Binaural Headphone monitoring (channels 0 & 1)
    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const float inMono = (ch1 != nullptr) ? (ch0[f] + ch1[f]) * 0.5f : ch0[f];

        float binL = 0.0f;
        float binR = 0.0f;

        for (std::size_t s = 0; s < numSpeakers; ++s)
        {
            const float spkSignal = inMono * m_speakerGains[s];
            if (s == static_cast<std::size_t>(SurroundChannel714::lfe))
            {
                binL += spkSignal * 0.5f;
                binR += spkSignal * 0.5f;
                continue;
            }

            const auto& spk = m_speakerPositions[s];
            auto& state = m_binauralStates[s];

            // Calculate ear distances for ITD and ILD
            // Left ear (-0.0875, 0, 0), Right ear (+0.0875, 0, 0)
            const float dL = std::sqrt((spk.x + 0.0875f) * (spk.x + 0.0875f) + spk.y * spk.y + spk.z * spk.z);
            const float dR = std::sqrt((spk.x - 0.0875f) * (spk.x - 0.0875f) + spk.y * spk.y + spk.z * spk.z);

            // Delay in samples (speed of sound 343 m/s)
            const auto delayL = std::clamp(static_cast<std::size_t>((dL / 343.0f) * static_cast<float>(m_sampleRate)), std::size_t(0), std::size_t(63));
            const auto delayR = std::clamp(static_cast<std::size_t>((dR / 343.0f) * static_cast<float>(m_sampleRate)), std::size_t(0), std::size_t(63));

            state.delayLineL[state.writePos] = spkSignal;
            state.delayLineR[state.writePos] = spkSignal;

            const auto readL = (state.writePos + 64 - delayL) % 64;
            const auto readR = (state.writePos + 64 - delayR) % 64;

            const float sampL = state.delayLineL[readL];
            const float sampR = state.delayLineR[readR];

            // Head shadow filter (high frequencies attenuated on opposite ear)
            const float ildShadowCoeff = 0.35f;
            const float gainL = (spk.x > 0.0f) ? (1.0f - spk.x * ildShadowCoeff) : 1.0f;
            const float gainR = (spk.x < 0.0f) ? (1.0f + spk.x * ildShadowCoeff) : 1.0f;

            binL += sampL * gainL;
            binR += sampR * gainR;

            state.writePos = (state.writePos + 1) % 64;
        }

        ch0[f] = binL;
        if (context.audio.channelCount >= 2)
        {
            ch1[f] = binR;
        }
    }
}

std::string SpatialSurroundPannerNode::generateAdmMetadataXml(const std::string& objectName, const double durationSec) const
{
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    ss << "<ituADM xmlns=\"urn:itu:bs:2076:2\" version=\"ITU-R_BS.2076-2\">\n";
    ss << "  <coreMetadata>\n";
    ss << "    <format>\n";
    ss << "      <audioFormatExtended version=\"ITU-R_BS.2094-1\">\n";
    ss << "        <audioObject audioObjectID=\"AO_1001\" audioObjectName=\"" << objectName << "\" duration=\"" << durationSec << "S\">\n";
    ss << "          <audioPackFormatIDRef>AP_0001</audioPackFormatIDRef>\n";
    ss << "          <audioTrackUIDRef>ATU_0001</audioTrackUIDRef>\n";
    ss << "        </audioObject>\n";
    ss << "        <audioPackFormat audioPackFormatID=\"AP_0001\" audioPackFormatName=\"7.1.4_Dolby_Atmos_Bed\" typeLabel=\"0001\">\n";
    ss << "          <audioChannelFormatIDRef>AC_00011001</audioChannelFormatIDRef>\n";
    ss << "        </audioPackFormat>\n";
    ss << "        <audioBlockFormat audioBlockFormatID=\"AB_00011001_00000001\" rtime=\"00:00:00.00000\" duration=\"" << durationSec << "S\">\n";
    ss << "          <cartesian>1</cartesian>\n";
    ss << "          <position coordinate=\"X\">" << m_x << "</position>\n";
    ss << "          <position coordinate=\"Y\">" << m_y << "</position>\n";
    ss << "          <position coordinate=\"Z\">" << m_z << "</position>\n";
    ss << "          <width>" << m_sizeSpread << "</width>\n";
    ss << "          <gain>1.0</gain>\n";
    ss << "          <diffuse>0.0</diffuse>\n";
    ss << "        </audioBlockFormat>\n";
    ss << "      </audioFormatExtended>\n";
    ss << "    </format>\n";
    ss << "  </coreMetadata>\n";
    ss << "</ituADM>\n";
    return ss.str();
}

} // namespace dawg::engine::dsp
