#include <dawg/engine/SmpteTimecode.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace dawg::engine {

double SmpteTimecodeEngine::getFramesPerSecond(const SmpteFrameRate rate) noexcept
{
    switch (rate)
    {
    case SmpteFrameRate::fps23_976: return 24000.0 / 1001.0;
    case SmpteFrameRate::fps24: return 24.0;
    case SmpteFrameRate::fps25: return 25.0;
    case SmpteFrameRate::fps29_97_df:
    case SmpteFrameRate::fps29_97_ndf: return 30000.0 / 1001.0;
    case SmpteFrameRate::fps30: return 30.0;
    case SmpteFrameRate::fps60: return 60.0;
    }
    return 24.0;
}

SmpteTimecode SmpteTimecodeEngine::sampleToTimecode(
    const std::uint64_t samplePosition,
    const double sampleRate,
    const SmpteFrameRate rate,
    const std::int64_t timecodeOffsetSamples) noexcept
{
    SmpteTimecode tc {};
    tc.frameRate = rate;
    tc.dropFrame = (rate == SmpteFrameRate::fps29_97_df);

    if (sampleRate <= 0.0)
        return tc;

    const auto effectiveSample = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, static_cast<std::int64_t>(samplePosition) + timecodeOffsetSamples));

    const double timeSec = static_cast<double>(effectiveSample) / sampleRate;
    const double fps = getFramesPerSecond(rate);

    if (rate == SmpteFrameRate::fps29_97_df)
    {
        // 29.97 Drop-Frame algorithm (SMPTE 12M standard)
        // 108,000 frames per hour = 18,000 frames per 10-minute block (17,982 drop frames)
        // Drop 2 frames every minute except tenth minutes
        constexpr std::int64_t dropFrames = 2;
        constexpr std::int64_t framesPerMinute = 1800; // 30 fps * 60s
        constexpr std::int64_t framesPer10Minutes = 17982; // 1800 * 10 - (2 * 9)

        auto totalFrames = static_cast<std::int64_t>(std::round(timeSec * fps));
        const auto d = totalFrames / framesPer10Minutes;
        const auto m = totalFrames % framesPer10Minutes;

        if (m > dropFrames)
        {
            totalFrames += (dropFrames * 9 * d) + dropFrames * ((m - dropFrames) / (framesPerMinute - dropFrames));
        }
        else
        {
            totalFrames += dropFrames * 9 * d;
        }

        const auto framesPerSecNominal = 30;
        tc.subframes = static_cast<std::uint32_t>(std::fmod(timeSec * fps, 1.0) * 80.0);
        tc.frames = static_cast<std::uint32_t>(totalFrames % framesPerSecNominal);
        tc.seconds = static_cast<std::uint32_t>((totalFrames / framesPerSecNominal) % 60);
        tc.minutes = static_cast<std::uint32_t>(((totalFrames / framesPerSecNominal) / 60) % 60);
        tc.hours = static_cast<std::uint32_t>(((totalFrames / framesPerSecNominal) / 3600));
    }
    else
    {
        // Non-drop frame calculation
        const auto totalFrames = static_cast<std::uint64_t>(std::floor(timeSec * fps));
        const auto intFps = static_cast<std::uint32_t>(std::round(fps));
        const auto framesNominal = std::max<std::uint32_t>(1, intFps);

        tc.subframes = static_cast<std::uint32_t>(std::fmod(timeSec * fps, 1.0) * 80.0);
        tc.frames = static_cast<std::uint32_t>(totalFrames % framesNominal);
        tc.seconds = static_cast<std::uint32_t>((totalFrames / framesNominal) % 60);
        tc.minutes = static_cast<std::uint32_t>((totalFrames / (framesNominal * 60)) % 60);
        tc.hours = static_cast<std::uint32_t>(totalFrames / (framesNominal * 3600));
    }

    return tc;
}

std::uint64_t SmpteTimecodeEngine::timecodeToSample(
    const SmpteTimecode& tc,
    const double sampleRate) noexcept
{
    if (sampleRate <= 0.0)
        return 0;

    const double fps = getFramesPerSecond(tc.frameRate);
    std::uint64_t totalFrames = 0;

    if (tc.dropFrame)
    {
        const std::uint64_t totalMinutes = tc.hours * 60 + tc.minutes;
        totalFrames = (tc.hours * 3600 + tc.minutes * 60 + tc.seconds) * 30 + tc.frames;
        // Subtract dropped frames
        const std::uint64_t dropped = 2 * (totalMinutes - (totalMinutes / 10));
        if (totalFrames >= dropped)
            totalFrames -= dropped;
    }
    else
    {
        const auto intFps = static_cast<std::uint32_t>(std::round(fps));
        totalFrames = (static_cast<std::uint64_t>(tc.hours) * 3600 +
                       static_cast<std::uint64_t>(tc.minutes) * 60 +
                       static_cast<std::uint64_t>(tc.seconds)) * intFps + tc.frames;
    }

    const double timeSec = static_cast<double>(totalFrames) / fps + (static_cast<double>(tc.subframes) / (fps * 80.0));
    return static_cast<std::uint64_t>(std::round(timeSec * sampleRate));
}

std::string SmpteTimecodeEngine::formatTimecode(
    const SmpteTimecode& tc,
    const bool includeSubframes)
{
    std::ostringstream ss;
    // Standard SMPTE separator: ':' for non-drop, ';' before frame for drop-frame
    const char frameSep = tc.dropFrame ? ';' : ':';

    ss << std::setfill('0')
       << std::setw(2) << tc.hours << ":"
       << std::setw(2) << tc.minutes << ":"
       << std::setw(2) << tc.seconds << frameSep
       << std::setw(2) << tc.frames;

    if (includeSubframes)
    {
        ss << "." << std::setw(2) << tc.subframes;
    }
    return ss.str();
}

std::string SmpteTimecodeEngine::formatSampleAsTimecode(
    const std::uint64_t samplePosition,
    const double sampleRate,
    const SmpteFrameRate rate,
    const bool includeSubframes) noexcept
{
    const auto tc = sampleToTimecode(samplePosition, sampleRate, rate);
    return formatTimecode(tc, includeSubframes);
}

std::string SmpteTimecodeEngine::generateCueSheetReportCsv(
    const std::string& projectTitle,
    const std::vector<CueSheetEntry>& cues)
{
    std::ostringstream ss;
    ss << "# DAWg Production Cue Sheet: " << projectTitle << "\n";
    ss << "CueNumber,CueName,StartTimecode,EndTimecode,Duration,MusicalKey,BPM,Composer,SceneDescription\n";

    for (const auto& cue : cues)
    {
        ss << cue.cueNumber << ",\""
           << cue.cueName << "\",\""
           << cue.startTimecode << "\",\""
           << cue.endTimecode << "\",\""
           << cue.durationTimecode << "\",\""
           << cue.musicalKey << "\","
           << cue.bpm << ",\""
           << cue.composer << "\",\""
           << cue.sceneDescription << "\"\n";
    }
    return ss.str();
}

std::string SmpteTimecodeEngine::generateCueSheetReportText(
    const std::string& projectTitle,
    const std::vector<CueSheetEntry>& cues)
{
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "                      DAWg PRODUCTION CUE SHEET REPORT                         \n";
    ss << " Project: " << projectTitle << "\n";
    ss << " Total Cues: " << cues.size() << "\n";
    ss << "================================================================================\n";
    ss << " CUE | CUE NAME              | IN TC       | OUT TC      | DURATION | KEY  | BPM\n";
    ss << "-----+-----------------------+-------------+-------------+----------+------+----\n";

    for (const auto& cue : cues)
    {
        ss << " " << std::setw(3) << std::setfill(' ') << cue.cueNumber << " | "
           << std::left << std::setw(21) << cue.cueName.substr(0, 21) << " | "
           << std::setw(11) << cue.startTimecode << " | "
           << std::setw(11) << cue.endTimecode << " | "
           << std::setw(8) << cue.durationTimecode << " | "
           << std::setw(4) << cue.musicalKey << " | "
           << std::right << std::setw(4) << static_cast<int>(cue.bpm) << "\n";
        if (! cue.sceneDescription.empty())
        {
            ss << "     └─ Scene: " << cue.sceneDescription << "\n";
        }
    }
    ss << "================================================================================\n";
    return ss.str();
}

} // namespace dawg::engine
