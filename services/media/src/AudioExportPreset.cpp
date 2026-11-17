#include <dawg/media/AudioExportPreset.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace dawg::media {

RenderOptions AudioExportPresetManager::createOptionsForPreset(
    StreamingExportPreset preset,
    const std::string& destinationPath,
    [[maybe_unused]] const AudioMetadataTag& metadata)
{
    RenderOptions options;
    options.destinationPath = destinationPath;

    switch (preset)
    {
    case StreamingExportPreset::appleDigitalMasters:
        options.sampleRate = 96000;
        options.bitDepth = ExportBitDepth::int24;
        options.applyTpdfDither = true;
        options.ditherNoiseShaping = DitherNoiseShaping::flatTpdf;
        break;

    case StreamingExportPreset::spotifyLoudnessMatch:
        options.sampleRate = 44100;
        options.bitDepth = ExportBitDepth::int24;
        options.applyTpdfDither = true;
        options.ditherNoiseShaping = DitherNoiseShaping::flatTpdf;
        break;

    case StreamingExportPreset::youtubeVideoStandard:
        options.sampleRate = 48000;
        options.bitDepth = ExportBitDepth::int24;
        options.applyTpdfDither = true;
        options.ditherNoiseShaping = DitherNoiseShaping::flatTpdf;
        break;

    case StreamingExportPreset::broadcastWavBext:
        options.sampleRate = 48000;
        options.bitDepth = ExportBitDepth::int24;
        options.applyTpdfDither = false;
        options.ditherNoiseShaping = DitherNoiseShaping::none;
        break;

    case StreamingExportPreset::cdRedBook:
        options.sampleRate = 44100;
        options.bitDepth = ExportBitDepth::int16;
        options.applyTpdfDither = true;
        options.ditherNoiseShaping = DitherNoiseShaping::lipshitz;
        break;

    case StreamingExportPreset::clubFestivalMaster:
        options.sampleRate = 44100;
        options.bitDepth = ExportBitDepth::int24;
        options.applyTpdfDither = true;
        options.ditherNoiseShaping = DitherNoiseShaping::psychoacoustic;
        break;

    case StreamingExportPreset::custom:
    default:
        options.sampleRate = 48000;
        options.bitDepth = ExportBitDepth::float32;
        options.applyTpdfDither = false;
        options.ditherNoiseShaping = DitherNoiseShaping::none;
        break;
    }

    return options;
}

std::string AudioExportPresetManager::getPresetName(StreamingExportPreset preset)
{
    switch (preset)
    {
    case StreamingExportPreset::appleDigitalMasters:  return "Apple Digital Masters (24-bit / 96kHz)";
    case StreamingExportPreset::spotifyLoudnessMatch: return "Spotify Loudness Target (-14 LUFS / 24-bit)";
    case StreamingExportPreset::youtubeVideoStandard: return "YouTube Video Standard (48kHz / 24-bit)";
    case StreamingExportPreset::broadcastWavBext:     return "Broadcast WAV (BWF) with SMPTE BEXT";
    case StreamingExportPreset::cdRedBook:            return "CD Red Book Standard (16-bit / 44.1kHz)";
    case StreamingExportPreset::clubFestivalMaster:   return "Club / Festival Master (-9 LUFS / 24-bit)";
    case StreamingExportPreset::custom:
    default:                                          return "Custom 32-bit Float";
    }
}

std::string AudioExportPresetManager::getPresetDescription(StreamingExportPreset preset)
{
    switch (preset)
    {
    case StreamingExportPreset::appleDigitalMasters:
        return "High-resolution 24-bit / 96kHz with >= 1.0 dB true-peak headroom compliant with Apple Music guidelines.";
    case StreamingExportPreset::spotifyLoudnessMatch:
        return "Standard 24-bit / 44.1kHz mastered to -14.0 LUFS with flat TPDF dither preventing codec distortion.";
    case StreamingExportPreset::youtubeVideoStandard:
        return "Broadcasting 24-bit / 48kHz audio aligned to video frame boundaries and YouTube normalization.";
    case StreamingExportPreset::broadcastWavBext:
        return "EBU Tech 3285 BWF with embedded origin, ISRC, and SMPTE sample-accurate timecode reference.";
    case StreamingExportPreset::cdRedBook:
        return "Standard 16-bit / 44.1kHz CD audio with Lipshitz psychoacoustic noise-shaped dither.";
    case StreamingExportPreset::clubFestivalMaster:
        return "High-impact DJ/venue festival master target (-9.0 LUFS) with -0.1 dBTP true-peak ceiling.";
    case StreamingExportPreset::custom:
    default:
        return "Uncompressed 32-bit floating point direct DAW audio output.";
    }
}

std::vector<std::uint8_t> AudioExportPresetManager::generateBextChunk(
    const AudioMetadataTag& metadata,
    [[maybe_unused]] std::uint32_t sampleRate)
{
    // BEXT chunk total payload size: 602 bytes
    std::vector<std::uint8_t> chunk(602, 0);

    auto copyFixedAscii = [&](std::size_t offset, const std::string& text, std::size_t maxLen) {
        const std::size_t len = std::min(text.length(), maxLen);
        std::memcpy(chunk.data() + offset, text.data(), len);
    };

    // Description (256 bytes)
    const std::string desc = metadata.title + " - " + metadata.artist;
    copyFixedAscii(0, desc, 256);

    // Originator (32 bytes)
    copyFixedAscii(256, metadata.artist.empty() ? "DAWg Workstation" : metadata.artist, 32);

    // OriginatorReference (32 bytes): e.g. ISRC
    copyFixedAscii(288, metadata.isrc, 32);

    // Date (10 bytes "YYYY:MM:DD") and Time (8 bytes "HH:MM:SS")
    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm timeInfo {};
#if defined(_MSC_VER)
    gmtime_s(&timeInfo, &timeT);
#else
    gmtime_r(&timeT, &timeInfo);
#endif
    std::ostringstream dateStr, timeStr;
    dateStr << std::put_time(&timeInfo, "%Y:%m:%d");
    timeStr << std::put_time(&timeInfo, "%H:%M:%S");

    copyFixedAscii(320, dateStr.str(), 10);
    copyFixedAscii(330, timeStr.str(), 8);

    // TimeReference (64-bit uint split into Low/High 32-bit): SMPTE samples
    const auto timeRef = metadata.smpteTimecodeSampleOffset;
    const auto low = static_cast<std::uint32_t>(timeRef & 0xFFFFFFFF);
    const auto high = static_cast<std::uint32_t>((timeRef >> 32) & 0xFFFFFFFF);
    std::memcpy(chunk.data() + 338, &low, 4);
    std::memcpy(chunk.data() + 342, &high, 4);

    // Version (uint16 = 1)
    const std::uint16_t version = 1;
    std::memcpy(chunk.data() + 346, &version, 2);

    return chunk;
}

std::vector<std::uint8_t> AudioExportPresetManager::generateId3v2Tag(
    const AudioMetadataTag& metadata)
{
    std::vector<std::uint8_t> tagData;

    auto appendFrame = [&](const char id[4], const std::string& value) {
        if (value.empty())
            return;

        const std::size_t payloadSize = 1 + value.length(); // 1 encoding byte (0x00 = ISO-8859-1) + string
        tagData.push_back(static_cast<std::uint8_t>(id[0]));
        tagData.push_back(static_cast<std::uint8_t>(id[1]));
        tagData.push_back(static_cast<std::uint8_t>(id[2]));
        tagData.push_back(static_cast<std::uint8_t>(id[3]));

        // 4 bytes big endian frame size
        tagData.push_back(static_cast<std::uint8_t>((payloadSize >> 24) & 0xFF));
        tagData.push_back(static_cast<std::uint8_t>((payloadSize >> 16) & 0xFF));
        tagData.push_back(static_cast<std::uint8_t>((payloadSize >> 8) & 0xFF));
        tagData.push_back(static_cast<std::uint8_t>(payloadSize & 0xFF));

        // 2 bytes flags (0)
        tagData.push_back(0);
        tagData.push_back(0);

        // Encoding byte: 0 = Latin1
        tagData.push_back(0);
        for (const char c : value)
            tagData.push_back(static_cast<std::uint8_t>(c));
    };

    appendFrame("TIT2", metadata.title);
    appendFrame("TPE1", metadata.artist);
    appendFrame("TALB", metadata.album);
    appendFrame("TSRC", metadata.isrc);
    appendFrame("TBPM", std::to_string(static_cast<int>(std::round(metadata.bpm))));
    appendFrame("TKEY", metadata.musicalKey);
    appendFrame("TCON", metadata.genre);
    appendFrame("TYER", metadata.year);
    appendFrame("COMM", metadata.comment);

    // Build ID3v2.3 header (10 bytes)
    const std::size_t bodySize = tagData.size();
    std::vector<std::uint8_t> result;
    result.reserve(10 + bodySize);

    result.push_back('I');
    result.push_back('D');
    result.push_back('3');
    result.push_back(3); // Version 2.3
    result.push_back(0); // Revision
    result.push_back(0); // Flags

    // Synchsafe 4-byte size (7 bits per byte)
    result.push_back(static_cast<std::uint8_t>((bodySize >> 21) & 0x7F));
    result.push_back(static_cast<std::uint8_t>((bodySize >> 14) & 0x7F));
    result.push_back(static_cast<std::uint8_t>((bodySize >> 7) & 0x7F));
    result.push_back(static_cast<std::uint8_t>(bodySize & 0x7F));

    result.insert(result.end(), tagData.begin(), tagData.end());
    return result;
}

} // namespace dawg::media
