#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace dawg::engine {

enum class SpeakerPair : uint8_t
{
    MainA = 0,
    NearfieldB = 1,
    AuratoneC = 2
};

struct CueMixSettings
{
    float masterLevel {1.0f};   // 0.0 to 1.0 (or > 1.0)
    float talkbackLevel {1.0f}; // 0.0 to 1.0
    bool mute {false};
};

class StudioControlRoomMonitorConsole
{
public:
    static constexpr std::size_t NumSpeakers = 3;
    static constexpr std::size_t NumCues = 4;

    StudioControlRoomMonitorConsole() noexcept;
    ~StudioControlRoomMonitorConsole() = default;

    void setSampleRate(float sampleRate) noexcept;
    [[nodiscard]] float sampleRate() const noexcept { return m_sampleRate; }

    void reset() noexcept;

    // Speaker Selection & Calibration
    void selectSpeaker(SpeakerPair speaker) noexcept { m_activeSpeaker = speaker; }
    [[nodiscard]] SpeakerPair activeSpeaker() const noexcept { return m_activeSpeaker; }

    void setSpeakerTrimDb(SpeakerPair speaker, float trimDb) noexcept;
    [[nodiscard]] float speakerTrimDb(SpeakerPair speaker) const noexcept;

    // Master Audition Controls
    void setMasterVolumeDb(float volumeDb) noexcept { m_masterVolumeDb = volumeDb; }
    [[nodiscard]] float masterVolumeDb() const noexcept { return m_masterVolumeDb; }

    void setMute(bool mute) noexcept { m_mute = mute; }
    [[nodiscard]] bool isMuted() const noexcept { return m_mute; }

    void setDim(bool dim) noexcept { m_dim = dim; }
    [[nodiscard]] bool isDimmed() const noexcept { return m_dim; }

    void setDimAttenuationDb(float attenDb) noexcept { m_dimAttenuationDb = attenDb; }
    [[nodiscard]] float dimAttenuationDb() const noexcept { return m_dimAttenuationDb; }

    void setMono(bool mono) noexcept { m_mono = mono; }
    [[nodiscard]] bool isMono() const noexcept { return m_mono; }

    void setDifference(bool diff) noexcept { m_difference = diff; }
    [[nodiscard]] bool isDifference() const noexcept { return m_difference; }

    void setMuteLeft(bool mute) noexcept { m_muteLeft = mute; }
    [[nodiscard]] bool isLeftMuted() const noexcept { return m_muteLeft; }

    void setMuteRight(bool mute) noexcept { m_muteRight = mute; }
    [[nodiscard]] bool isRightMuted() const noexcept { return m_muteRight; }

    void setPhaseInvertLeft(bool invert) noexcept { m_phaseInvertLeft = invert; }
    [[nodiscard]] bool isPhaseInvertLeft() const noexcept { return m_phaseInvertLeft; }

    void setPhaseInvertRight(bool invert) noexcept { m_phaseInvertRight = invert; }
    [[nodiscard]] bool isPhaseInvertRight() const noexcept { return m_phaseInvertRight; }

    // Talkback & Slate Engine
    void setTalkbackActive(bool active) noexcept { m_talkbackActive = active; }
    [[nodiscard]] bool isTalkbackActive() const noexcept { return m_talkbackActive; }

    void setTalkbackMicGainDb(float gainDb) noexcept { m_talkbackMicGainDb = gainDb; }
    [[nodiscard]] float talkbackMicGainDb() const noexcept { return m_talkbackMicGainDb; }

    void setTalkbackDuckingDb(float duckDb) noexcept { m_talkbackDuckingDb = duckDb; }
    [[nodiscard]] float talkbackDuckingDb() const noexcept { return m_talkbackDuckingDb; }

    void setSlateToneActive(bool active) noexcept { m_slateActive = active; }
    [[nodiscard]] bool isSlateToneActive() const noexcept { return m_slateActive; }

    void setSlateFrequencyHz(float freqHz) noexcept { m_slateFrequencyHz = std::clamp(freqHz, 20.0f, 10000.0f); }
    [[nodiscard]] float slateFrequencyHz() const noexcept { return m_slateFrequencyHz; }

    void setSlateGainDb(float gainDb) noexcept { m_slateGainDb = gainDb; }
    [[nodiscard]] float slateGainDb() const noexcept { return m_slateGainDb; }

    // Performer Cue Mix Sends
    void setCueSettings(std::size_t cueIndex, const CueMixSettings& settings) noexcept;
    [[nodiscard]] const CueMixSettings& cueSettings(std::size_t cueIndex) const noexcept;

    // Real-Time Console Audio Process (RT-safe, zero allocations)
    void process(
        const float* masterInL,
        const float* masterInR,
        const float* talkbackMicIn, // nullable
        float* monitorOutL,
        float* monitorOutR,
        std::array<float*, NumCues> cueOutL,
        std::array<float*, NumCues> cueOutR,
        std::size_t frameCount) noexcept;

private:
    float m_sampleRate {44100.0f};
    SpeakerPair m_activeSpeaker {SpeakerPair::MainA};
    std::array<float, NumSpeakers> m_speakerTrimDb {0.0f, 0.0f, 0.0f};

    float m_masterVolumeDb {0.0f};
    bool m_mute {false};
    bool m_dim {false};
    float m_dimAttenuationDb {-20.0f};
    bool m_mono {false};
    bool m_difference {false};
    bool m_muteLeft {false};
    bool m_muteRight {false};
    bool m_phaseInvertLeft {false};
    bool m_phaseInvertRight {false};

    bool m_talkbackActive {false};
    float m_talkbackMicGainDb {0.0f};
    float m_talkbackDuckingDb {-18.0f};

    bool m_slateActive {false};
    float m_slateFrequencyHz {1000.0f};
    float m_slateGainDb {-12.0f};
    float m_slatePhase {0.0f};

    std::array<CueMixSettings, NumCues> m_cues {};
};

} // namespace dawg::engine
