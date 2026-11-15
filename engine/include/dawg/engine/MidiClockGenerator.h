#pragma once

#include <dawg/engine/MidiEvent.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine {

class MidiClockGenerator final
{
public:
    MidiClockGenerator() noexcept = default;
    ~MidiClockGenerator() = default;

    void reset() noexcept;

    // Process slice and generate sample-accurate MIDI clock events (24 PPQN)
    // Returns number of clock events generated in outEvents
    [[nodiscard]] std::size_t process(
        std::size_t frameCount,
        double sampleRate,
        double bpm,
        std::int64_t timelineSample,
        bool isPlaying,
        std::array<MidiEvent, 32>& outEvents) noexcept;

    [[nodiscard]] static MidiEvent makeStart() noexcept;
    [[nodiscard]] static MidiEvent makeStop() noexcept;
    [[nodiscard]] static MidiEvent makeContinue() noexcept;
    [[nodiscard]] static MidiEvent makeSongPositionPointer(std::uint16_t midiBeats) noexcept;

private:
    double m_phaseInQuarterNote {0.0};
    bool m_wasPlaying {false};
};

} // namespace dawg::engine
