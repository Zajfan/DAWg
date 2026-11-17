#pragma once

#include <dawg/engine/MidiEvent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

// 5D MPE per-note expression state
struct MpeNoteState
{
    bool active {false};
    std::uint8_t channel {1};   // Member channel (typically 2-15)
    std::uint8_t noteNumber {60};
    
    // 5 Dimensions of Expression
    float strike {0.8f};        // Dimension 1: Initial Note-On Velocity (0.0 to 1.0)
    float press {0.0f};         // Dimension 2: Pressure / Polyphonic Aftertouch (0.0 to 1.0)
    float glide {0.0f};         // Dimension 3: Pitch Bend in continuous semitones (-48.0 to +48.0)
    float slide {0.5f};         // Dimension 4: CC74 Timbre / Brightness (0.0 to 1.0)
    float lift {0.0f};          // Dimension 5: Release Velocity (0.0 to 1.0)

    std::uint64_t noteAge {0};  // Voice allocation / stealing timestamp
};

// Complete 5D MIDI Polyphonic Expression (MPE) Engine
// Compliant with the MPE Specification (MMA/AMEI).
// Manages per-note 5D continuous parameter streams with voice tracking,
// pitch bend range configuration, and real-time expression modulation dispatch.
class MpeVoiceManager final
{
public:
    static constexpr std::size_t maxVoices = 16;
    static constexpr std::size_t midiChannels = 16;

    MpeVoiceManager() noexcept;
    ~MpeVoiceManager() = default;

    void reset() noexcept;

    // Pitch Bend Configuration
    void setPitchBendRangeSemitones(float semitones) noexcept { m_pitchBendRange = std::clamp(semitones, 1.0f, 96.0f); }
    [[nodiscard]] float pitchBendRangeSemitones() const noexcept { return m_pitchBendRange; }

    // Zone Configuration
    void setMasterChannel(std::uint8_t channel1Based) noexcept { m_masterChannel = std::clamp<std::uint8_t>(channel1Based, 1, 16); }
    [[nodiscard]] std::uint8_t masterChannel() const noexcept { return m_masterChannel; }

    // MIDI Event Processing (Sample-Accurate / Block-Accurate)
    void processMidiEvent(const MidiEvent& event) noexcept;

    // Direct Voice Manipulation
    void noteOn(std::uint8_t channel, std::uint8_t noteNumber, float velocity) noexcept;
    void noteOff(std::uint8_t channel, std::uint8_t noteNumber, float releaseVelocity) noexcept;
    void setPressure(std::uint8_t channel, float pressure) noexcept;
    void setPitchBend(std::uint8_t channel, std::uint16_t raw14Bit) noexcept;
    void setTimbreSlide(std::uint8_t channel, float slide0To1) noexcept;

    // Active Voice Queries
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] const std::array<MpeNoteState, maxVoices>& voices() const noexcept { return m_voices; }
    [[nodiscard]] const MpeNoteState* findVoice(std::uint8_t channel, std::uint8_t noteNumber) const noexcept;

private:
    [[nodiscard]] int allocateVoice(std::uint8_t channel, std::uint8_t noteNumber) noexcept;

    std::array<MpeNoteState, maxVoices> m_voices {};
    float m_pitchBendRange {48.0f}; // Standard 48 semitones MPE bend
    std::uint8_t m_masterChannel {1};
    std::uint64_t m_ageCounter {0};

    // Per-channel smoothing / latest controller values
    std::array<float, midiChannels> m_channelPressure {};
    std::array<float, midiChannels> m_channelSlide {};
    std::array<float, midiChannels> m_channelGlide {};
};

} // namespace dawg::engine::dsp
