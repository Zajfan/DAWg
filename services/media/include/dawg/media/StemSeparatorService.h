#pragma once

#include <dawg/project/ProjectTypes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::media {

struct SeparatedStems
{
    std::vector<float> drumsL;
    std::vector<float> drumsR;
    std::vector<float> bassL;
    std::vector<float> bassR;
    std::vector<float> vocalsL;
    std::vector<float> vocalsR;
    std::vector<float> otherL;
    std::vector<float> otherR;
};

class StemSeparatorService final
{
public:
    StemSeparatorService() = default;
    ~StemSeparatorService() = default;

    // 4-Stem Spectral Decomposition (Drums, Bass, Vocals, Other)
    [[nodiscard]] static SeparatedStems separate4Stems(
        const float* left,
        const float* right,
        std::size_t numSamples,
        double sampleRate);
};

struct ExtractedGrooveNote
{
    std::uint8_t pitch {36};
    std::uint8_t velocity {100};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {2400};
};

class GrooveExtractor final
{
public:
    GrooveExtractor() = default;
    ~GrooveExtractor() = default;

    [[nodiscard]] static std::vector<ExtractedGrooveNote> extractGroove(
        const float* audio,
        std::size_t numSamples,
        double sampleRate,
        double bpm);
};

} // namespace dawg::media
