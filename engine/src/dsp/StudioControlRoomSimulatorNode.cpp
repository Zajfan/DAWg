#include <dawg/engine/dsp/StudioControlRoomSimulatorNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

StudioControlRoomSimulatorNode::StudioControlRoomSimulatorNode() noexcept
{
    updateAcousticParameters();
}

bool StudioControlRoomSimulatorNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    updateAcousticParameters();
    return true;
}

void StudioControlRoomSimulatorNode::reset() noexcept
{
    m_delayBufferL.fill(0.0f);
    m_delayBufferR.fill(0.0f);
    m_writeIndex = 0;
    m_shadowStateL = 0.0f;
    m_shadowStateR = 0.0f;
    m_combBufferL.fill(0.0f);
    m_combBufferR.fill(0.0f);
    m_combIdxL = 0;
    m_combIdxR = 0;
}

void StudioControlRoomSimulatorNode::setEnvironment(StudioEnvironment env) noexcept
{
    m_environment = env;
    updateAcousticParameters();
}

void StudioControlRoomSimulatorNode::setHeadYawDegrees(float yawDeg) noexcept
{
    m_headYawDeg = std::clamp(yawDeg, -45.0f, 45.0f);
    updateAcousticParameters();
}

void StudioControlRoomSimulatorNode::setMonitorDistanceMeters(float distanceM) noexcept
{
    m_distanceMeters = std::clamp(distanceM, 0.8f, 3.5f);
    updateAcousticParameters();
}

void StudioControlRoomSimulatorNode::updateAcousticParameters() noexcept
{
    const float fs = static_cast<float>(m_sampleRate);
    constexpr float speedOfSound = 343.0f; // m/s
    constexpr float headRadius = 0.0875f;  // 8.75 cm
    constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;

    // Monitor angles from listener front: standard equilateral triangle (30 deg left, 30 deg right)
    const float leftSpeakerAngleRad = (30.0f + m_headYawDeg) * degToRad;
    const float rightSpeakerAngleRad = (30.0f - m_headYawDeg) * degToRad;

    // Woodworth ITD formula: delta_t = (a / c) * (sin(theta) + theta)
    const float itdSecLtoR = (headRadius / speedOfSound) *
        (std::sin(std::abs(leftSpeakerAngleRad)) + std::abs(leftSpeakerAngleRad));
    const float itdSecRtoL = (headRadius / speedOfSound) *
        (std::sin(std::abs(rightSpeakerAngleRad)) + std::abs(rightSpeakerAngleRad));

    m_itdSamplesLeftToRight = std::clamp(itdSecLtoR * fs, 1.0f, 40.0f);
    m_itdSamplesRightToLeft = std::clamp(itdSecRtoL * fs, 1.0f, 40.0f);

    updateHeadShadowFilters();

    // Configure early reflections based on selected environment
    const float distScale = m_distanceMeters / 1.8f;
    switch (m_environment)
    {
    case StudioEnvironment::masteringSuite:
        // Pristine, heavily treated mastering control room (RT60 ~ 0.25s)
        m_earlyTaps[0] = {static_cast<std::size_t>(std::clamp(0.008f * distScale * fs, 1.0f, 2000.0f)), 0.12f, 0.08f};
        m_earlyTaps[1] = {static_cast<std::size_t>(std::clamp(0.016f * distScale * fs, 1.0f, 2500.0f)), 0.08f, 0.11f};
        m_earlyTaps[2] = {static_cast<std::size_t>(std::clamp(0.024f * distScale * fs, 1.0f, 3000.0f)), 0.06f, 0.05f};
        m_earlyTaps[3] = {static_cast<std::size_t>(std::clamp(0.035f * distScale * fs, 1.0f, 3800.0f)), 0.04f, 0.04f};
        m_combFeedback = 0.20f;
        break;

    case StudioEnvironment::nearfieldStudio:
        // Intimate control room with nearfield reference monitors (RT60 ~ 0.4s)
        m_earlyTaps[0] = {static_cast<std::size_t>(std::clamp(0.005f * distScale * fs, 1.0f, 1500.0f)), 0.18f, 0.12f};
        m_earlyTaps[1] = {static_cast<std::size_t>(std::clamp(0.012f * distScale * fs, 1.0f, 2000.0f)), 0.14f, 0.16f};
        m_earlyTaps[2] = {static_cast<std::size_t>(std::clamp(0.020f * distScale * fs, 1.0f, 2800.0f)), 0.09f, 0.08f};
        m_earlyTaps[3] = {static_cast<std::size_t>(std::clamp(0.030f * distScale * fs, 1.0f, 3500.0f)), 0.06f, 0.07f};
        m_combFeedback = 0.38f;
        break;

    case StudioEnvironment::largeClubPA:
        // Large acoustics, boundary reflections, sub energy (RT60 ~ 1.2s)
        m_earlyTaps[0] = {static_cast<std::size_t>(std::clamp(0.018f * distScale * fs, 1.0f, 2000.0f)), 0.28f, 0.22f};
        m_earlyTaps[1] = {static_cast<std::size_t>(std::clamp(0.035f * distScale * fs, 1.0f, 2800.0f)), 0.24f, 0.26f};
        m_earlyTaps[2] = {static_cast<std::size_t>(std::clamp(0.055f * distScale * fs, 1.0f, 3500.0f)), 0.18f, 0.16f};
        m_earlyTaps[3] = {static_cast<std::size_t>(std::clamp(0.080f * distScale * fs, 1.0f, 4000.0f)), 0.14f, 0.15f};
        m_combFeedback = 0.65f;
        break;
    }
}

void StudioControlRoomSimulatorNode::updateHeadShadowFilters() noexcept
{
    const float fs = static_cast<float>(m_sampleRate);

    // Contralateral head shadow low-pass cutoff (~2200 Hz baseline modulated by yaw)
    const float cutoffL = std::clamp(2200.0f - m_headYawDeg * 25.0f, 600.0f, 6000.0f);
    const float cutoffR = std::clamp(2200.0f + m_headYawDeg * 25.0f, 600.0f, 6000.0f);

    m_shadowCoeffL = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * cutoffL / fs);
    m_shadowCoeffR = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * cutoffR / fs);
}

void StudioControlRoomSimulatorNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    // Direct sound distance attenuation
    const float directGain = 1.0f / std::sqrt(std::max(0.5f, m_distanceMeters));
    const float crossfeedGain = m_crossfeedAmount * 0.7071f * directGain;

    const auto readDelay = [this](const std::array<float, DelayBufferSize>& buffer, float delaySamples) noexcept -> float {
        const float readPos = static_cast<float>(m_writeIndex) - delaySamples;
        float wrappedPos = readPos;
        while (wrappedPos < 0.0f)
        {
            wrappedPos += static_cast<float>(DelayBufferSize);
        }
        const auto idx0 = static_cast<std::size_t>(wrappedPos) % DelayBufferSize;
        const auto idx1 = (idx0 + 1) % DelayBufferSize;
        const float frac = wrappedPos - static_cast<float>(static_cast<std::size_t>(wrappedPos));
        return buffer[idx0] + frac * (buffer[idx1] - buffer[idx0]);
    };

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float dryL = outL[i];
        const float dryR = (outR != nullptr) ? outR[i] : dryL;

        // Store into circular delay line
        m_delayBufferL[m_writeIndex] = dryL;
        m_delayBufferR[m_writeIndex] = dryR;

        // 1. Direct ipsilateral signal
        const float directL = dryL * directGain;
        const float directR = dryR * directGain;

        // 2. Contralateral crossfeed with ITD delay
        const float delayedLforR = readDelay(m_delayBufferL, m_itdSamplesLeftToRight);
        const float delayedRforL = readDelay(m_delayBufferR, m_itdSamplesRightToLeft);

        // 3. Head shadow low-pass filtering on crossfeed signals
        m_shadowStateL += m_shadowCoeffL * (delayedRforL - m_shadowStateL);
        m_shadowStateR += m_shadowCoeffR * (delayedLforR - m_shadowStateR);

        const float crossL = m_shadowStateL * crossfeedGain;
        const float crossR = m_shadowStateR * crossfeedGain;

        // 4. Room early reflections
        float earlyL = 0.0f;
        float earlyR = 0.0f;
        for (const auto& tap : m_earlyTaps)
        {
            const float reflL = readDelay(m_delayBufferL, static_cast<float>(tap.delaySamples));
            const float reflR = readDelay(m_delayBufferR, static_cast<float>(tap.delaySamples));
            earlyL += reflL * tap.gainL + reflR * (tap.gainR * 0.5f);
            earlyR += reflR * tap.gainR + reflL * (tap.gainL * 0.5f);
        }

        // 5. Ambient room reverberant field
        const float combInL = (directL + crossL) * 0.3f;
        const float combInR = (directR + crossR) * 0.3f;
        const float combOutL = m_combBufferL[m_combIdxL];
        const float combOutR = m_combBufferR[m_combIdxR];

        m_combBufferL[m_combIdxL] = combInL + combOutL * m_combFeedback;
        m_combBufferR[m_combIdxR] = combInR + combOutR * m_combFeedback;

        m_combIdxL = (m_combIdxL + 1) % m_combBufferL.size();
        m_combIdxR = (m_combIdxR + 1) % m_combBufferR.size();

        const float ambienceL = (earlyL + combOutL * 0.5f) * m_roomAmbienceAmount;
        const float ambienceR = (earlyR + combOutR * 0.5f) * m_roomAmbienceAmount;

        // Composite binaural room signal
        const float simL = directL + crossL + ambienceL;
        const float simR = directR + crossR + ambienceR;

        // Dry/Wet blend
        outL[i] = m_mix * simL + (1.0f - m_mix) * dryL;
        if (outR != nullptr)
        {
            outR[i] = m_mix * simR + (1.0f - m_mix) * dryR;
        }

        m_writeIndex = (m_writeIndex + 1) % DelayBufferSize;
    }
}

} // namespace dawg::engine::dsp
