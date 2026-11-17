#pragma once

#include <dawg/media/AudioAssetWriter.h>
#include <dawg/media/OfflineRenderService.h>

#include <cstdint>
#include <string>
#include <vector>

namespace dawg::media {

enum class StreamingExportPreset : std::uint8_t
{
    custom,
    appleDigitalMasters,    // 24-bit / 96kHz, <= -1.0 dBTP ceiling, Apple Music High-Res compliant
    spotifyLoudnessMatch,   // 24-bit / 44.1kHz, -14.0 LUFS target, -1.0 dBTP ceiling
    youtubeVideoStandard,   // 24-bit / 48kHz, -14.0 LUFS, -1.0 dBTP
    broadcastWavBext,       // 24-bit / 48kHz Broadcast WAV (BWF) with SMPTE BEXT metadata chunk
    cdRedBook,              // 16-bit / 44.1kHz, Lipshitz noise-shaped dither
    clubFestivalMaster      // 24-bit / 44.1kHz, -9.0 LUFS, -0.1 dBTP true peak
};

struct AudioMetadataTag
{
    std::string title {"Master Track"};
    std::string artist {"Producer"};
    std::string album {"Album"};
    std::string isrc {"US-S1Z-26-00001"}; // International Standard Recording Code
    double bpm {120.0};
    std::string musicalKey {"Cmaj"};
    std::string genre {"Electronic"};
    std::string year {"2026"};
    std::string comment {"Mastered in DAWg"};
    std::uint64_t smpteTimecodeSampleOffset {0};
};

class AudioExportPresetManager
{
public:
    [[nodiscard]] static RenderOptions createOptionsForPreset(
        StreamingExportPreset preset,
        const std::string& destinationPath,
        const AudioMetadataTag& metadata = {});

    [[nodiscard]] static std::string getPresetName(StreamingExportPreset preset);
    [[nodiscard]] static std::string getPresetDescription(StreamingExportPreset preset);

    // Broadcast WAV (BWF) 'bext' metadata chunk generator (EBU Tech 3285)
    [[nodiscard]] static std::vector<std::uint8_t> generateBextChunk(
        const AudioMetadataTag& metadata,
        std::uint32_t sampleRate = 48000);

    // ID3v2.3 tag generator (TIT2, TPE1, TALB, TSRC, TBPM, TKEY, TCON, TYER, COMM)
    [[nodiscard]] static std::vector<std::uint8_t> generateId3v2Tag(
        const AudioMetadataTag& metadata);
};

} // namespace dawg::media
