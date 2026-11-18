#include "dawg/engine/StudioControlRoomMonitorConsole.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

StudioControlRoomMonitorConsole::StudioControlRoomMonitorConsole() noexcept
{
    m_speakerTrimDb.fill(0.0f);
    for (auto& cue : m_cues) {
        cue.masterLevel = 1.0f;
        cue.talkbackLevel = 1.0f;
        cue.mute = false;
    }
}

void StudioControlRoomMonitorConsole::setSampleRate(float sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0.0f ? sampleRate : 44100.0f;
}

void StudioControlRoomMonitorConsole::reset() noexcept
{
    m_slatePhase = 0.0f;
}

void StudioControlRoomMonitorConsole::setSpeakerTrimDb(SpeakerPair speaker, float trimDb) noexcept
{
    const auto idx = static_cast<std::size_t>(speaker);
    if (idx < NumSpeakers) {
        m_speakerTrimDb[idx] = std::clamp(trimDb, -24.0f, 12.0f);
    }
}

float StudioControlRoomMonitorConsole::speakerTrimDb(SpeakerPair speaker) const noexcept
{
    const auto idx = static_cast<std::size_t>(speaker);
    if (idx < NumSpeakers) {
        return m_speakerTrimDb[idx];
    }
    return 0.0f;
}

void StudioControlRoomMonitorConsole::setCueSettings(std::size_t cueIndex, const CueMixSettings& settings) noexcept
{
    if (cueIndex < NumCues) {
        m_cues[cueIndex] = settings;
    }
}

const CueMixSettings& StudioControlRoomMonitorConsole::cueSettings(std::size_t cueIndex) const noexcept
{
    const std::size_t safeIdx = std::min(cueIndex, NumCues - 1);
    return m_cues[safeIdx];
}

void StudioControlRoomMonitorConsole::process(
    const float* masterInL,
    const float* masterInR,
    const float* talkbackMicIn,
    float* monitorOutL,
    float* monitorOutR,
    std::array<float*, NumCues> cueOutL,
    std::array<float*, NumCues> cueOutR,
    std::size_t frameCount) noexcept
{
    if (masterInL == nullptr || masterInR == nullptr || frameCount == 0) {
        return;
    }

    // Monitor Gain stages
    const float masterLinear = std::pow(10.0f, m_masterVolumeDb * 0.05f);
    const auto speakerIdx = static_cast<std::size_t>(m_activeSpeaker);
    const float trimLinear = std::pow(10.0f, m_speakerTrimDb[speakerIdx] * 0.05f);
    const float dimLinear = m_dim ? std::pow(10.0f, m_dimAttenuationDb * 0.05f) : 1.0f;
    const float duckLinear = m_talkbackActive ? std::pow(10.0f, m_talkbackDuckingDb * 0.05f) : 1.0f;
    const float effectiveMonitorGain = m_mute ? 0.0f : (masterLinear * trimLinear * dimLinear * duckLinear);

    const float talkbackGain = std::pow(10.0f, m_talkbackMicGainDb * 0.05f);
    const float slateGain = std::pow(10.0f, m_slateGainDb * 0.05f);
    const float slatePhaseInc = 2.0f * std::numbers::pi_v<float> * m_slateFrequencyHz / m_sampleRate;

    for (std::size_t i = 0; i < frameCount; ++i) {
        float inL = masterInL[i];
        float inR = masterInR[i];

        // Master audition routing
        if (m_mono) {
            const float monoSum = 0.5f * (inL + inR);
            inL = monoSum;
            inR = monoSum;
        } else if (m_difference) {
            const float diffSum = 0.5f * (inL - inR);
            inL = diffSum;
            inR = diffSum;
        }

        if (m_muteLeft) {
            inL = 0.0f;
        }
        if (m_muteRight) {
            inR = 0.0f;
        }

        if (m_phaseInvertLeft) {
            inL = -inL;
        }
        if (m_phaseInvertRight) {
            inR = -inR;
        }

        if (monitorOutL != nullptr) {
            monitorOutL[i] = inL * effectiveMonitorGain;
        }
        if (monitorOutR != nullptr) {
            monitorOutR[i] = inR * effectiveMonitorGain;
        }

        // Generate slate tone
        float slateSignal = 0.0f;
        if (m_slateActive) {
            slateSignal = std::sin(m_slatePhase) * slateGain;
            m_slatePhase += slatePhaseInc;
            if (m_slatePhase >= 2.0f * std::numbers::pi_v<float>) {
                m_slatePhase -= 2.0f * std::numbers::pi_v<float>;
            }
        }

        // Talkback input
        const float tbSignal = (talkbackMicIn != nullptr && m_talkbackActive) ? (talkbackMicIn[i] * talkbackGain) : 0.0f;
        const float cueOverlay = tbSignal + slateSignal;

        // Route to performer cue mix sends
        for (std::size_t c = 0; c < NumCues; ++c) {
            if (cueOutL[c] == nullptr || cueOutR[c] == nullptr) {
                continue;
            }
            if (m_cues[c].mute) {
                cueOutL[c][i] = 0.0f;
                cueOutR[c][i] = 0.0f;
            } else {
                cueOutL[c][i] = masterInL[i] * m_cues[c].masterLevel + cueOverlay * m_cues[c].talkbackLevel;
                cueOutR[c][i] = masterInR[i] * m_cues[c].masterLevel + cueOverlay * m_cues[c].talkbackLevel;
            }
        }
    }
}

} // namespace dawg::engine
