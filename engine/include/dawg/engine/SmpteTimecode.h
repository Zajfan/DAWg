#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

enum class SmpteFrameRate : std::uint8_t
{
    fps23_976,
    fps24,
    fps25,
    fps29_97_df,
    fps29_97_ndf,
    fps30,
    fps60
};

struct SmpteTimecode
{
    std::uint32_t hours {0};
    std::uint32_t minutes {0};
    std::uint32_t seconds {0};
    std::uint32_t frames {0};
    std::uint32_t subframes {0}; // 0..79 (80 subframes per frame per SMPTE spec)
    SmpteFrameRate frameRate {SmpteFrameRate::fps24};
    bool dropFrame {false};
};

struct CueSheetEntry
{
    std::size_t cueNumber {1};
    std::string cueName;
    std::string startTimecode;
    std::string endTimecode;
    std::string durationTimecode;
    std::string musicalKey {"C"};
    double bpm {120.0};
    std::string composer {"Film Composer"};
    std::string sceneDescription;
};

class SmpteTimecodeEngine final
{
public:
    SmpteTimecodeEngine() = delete;

    [[nodiscard]] static double getFramesPerSecond(SmpteFrameRate rate) noexcept;

    [[nodiscard]] static SmpteTimecode sampleToTimecode(
        std::uint64_t samplePosition,
        double sampleRate,
        SmpteFrameRate rate,
        std::int64_t timecodeOffsetSamples = 0) noexcept;

    [[nodiscard]] static std::uint64_t timecodeToSample(
        const SmpteTimecode& tc,
        double sampleRate) noexcept;

    [[nodiscard]] static std::string formatTimecode(
        const SmpteTimecode& tc,
        bool includeSubframes = false);

    [[nodiscard]] static std::string formatSampleAsTimecode(
        std::uint64_t samplePosition,
        double sampleRate,
        SmpteFrameRate rate,
        bool includeSubframes = false) noexcept;

    /// Formats a complete production Cue Sheet report in standard CSV or text format
    [[nodiscard]] static std::string generateCueSheetReportCsv(
        const std::string& projectTitle,
        const std::vector<CueSheetEntry>& cues);

    [[nodiscard]] static std::string generateCueSheetReportText(
        const std::string& projectTitle,
        const std::vector<CueSheetEntry>& cues);

    /// Calculates required tempo (BPM) so that musicalBeats precisely land on targetHitFrame
    [[nodiscard]] static double calculateTempoForHitPoint(
        std::uint64_t startFrame,
        std::uint64_t targetHitFrame,
        double fps,
        double musicalBeats) noexcept;
};

} // namespace dawg::engine
