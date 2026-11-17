#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class ChordScale : std::uint8_t
{
    major,
    naturalMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    harmonicMinor
};

enum class ChordVoicing : std::uint8_t
{
    rootPosition,
    drop2,
    drop3,
    firstInversion,
    secondInversion,
    openSpread
};

struct ChordPad
{
    std::size_t padIndex {0}; // 0..15 (4x4 matrix)
    std::string romanNumeral {"I"};
    std::string chordName {"Cmaj7"};
    std::uint8_t rootMidi {60};
    std::vector<std::uint8_t> midiNotes;
};

struct StrummedNote
{
    std::uint8_t pitch {60};
    double timeOffsetMs {0.0};
    std::uint8_t velocity {100};
};

class ChordPadProgressionEngine final
{
public:
    ChordPadProgressionEngine() = default;
    ~ChordPadProgressionEngine() = default;

    /// Generates a 4x4 grid of 16 chord pads based on root key (0..11) and scale mode
    [[nodiscard]] static std::vector<ChordPad> generateProgressionMatrix(
        std::uint8_t rootKey, // 0 = C, 1 = C#, 2 = D ... 11 = B
        ChordScale scale,
        ChordVoicing voicing = ChordVoicing::rootPosition);

    /// Applies custom voicing (drop-2, inversions, open spread) to chord notes
    [[nodiscard]] static std::vector<std::uint8_t> applyVoicing(
        const std::vector<std::uint8_t>& notes,
        ChordVoicing voicing);

    /// Generates strummed notes with time-staggered onsets and humanized velocity falloff
    /// strumDurationMs: overall duration of strum sweep (e.g. 15ms - 60ms)
    /// upStrum: true = highest to lowest pitch, false = lowest to highest (down-strum)
    [[nodiscard]] static std::vector<StrummedNote> generateStrum(
        const std::vector<std::uint8_t>& notes,
        double strumDurationMs = 25.0,
        bool upStrum = false,
        std::uint8_t baseVelocity = 100);
};

} // namespace dawg::engine::dsp
