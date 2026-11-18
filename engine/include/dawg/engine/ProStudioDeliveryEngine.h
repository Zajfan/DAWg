#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/dsp/MasteringStemExportEngine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine {

struct DeliveryTokenContext
{
    std::string projectName {"Project"};
    std::size_t trackNumber {1};
    std::string trackName {"AudioTrack"};
    double tempoBpm {120.0};
    double sampleRate {48000.0};
    std::uint16_t bitDepth {24};
    std::string dateString {"20261118"};
};

struct StemDeliverySpec
{
    std::size_t trackIndex {0};
    std::string trackName {"Stem"};
    bool isPreFader {false};
    bool isDry {false};
    float targetLufs {-14.0f};
    float targetTruePeakDb {-1.0f};
};

struct AafClipDescriptor
{
    std::string clipName;
    std::string sourceFilePath;
    std::uint64_t timelineStartSamples {0};
    std::uint64_t durationSamples {0};
    std::uint64_t sourceOffsetSamples {0};
    float fadeInSeconds {0.0f};
    float fadeOutSeconds {0.0f};
    float gainDb {0.0f};
    float pan {-0.0f};
};

struct AafTrackDescriptor
{
    std::string trackName;
    std::uint32_t trackIndex {0};
    bool isStereo {true};
    std::vector<AafClipDescriptor> clips;
};

struct FileChecksumEntry
{
    std::string fileName;
    std::uint64_t fileSizeBytes {0};
    std::string sha256Hex;
    float integratedLufs {-14.0f};
    float truePeakDb {-1.0f};
};

class ProStudioDeliveryEngine final
{
public:
    ProStudioDeliveryEngine() = default;
    ~ProStudioDeliveryEngine() = default;

    // =========================================================================
    // Naming Token Formatter
    // =========================================================================
    /// Replaces tokens: {Project}, {TrackNumber}, {TrackName}, {BPM}, {SampleRate}, {BitDepth}, {Date}
    [[nodiscard]] static std::string formatStemFilename(
        std::string_view pattern,
        const DeliveryTokenContext& context);

    // =========================================================================
    // AAF / OMF Timeline Interchange Generator
    // =========================================================================
    /// Generates standard AAF / OMF interchange XML session descriptor
    [[nodiscard]] static std::string generateAafXmlDescriptor(
        std::string_view projectName,
        double sampleRate,
        double tempoBpm,
        std::span<const AafTrackDescriptor> tracks);

    // =========================================================================
    // Cryptographic SHA-256 Checksum Generator (FIPS 180-4 compliant)
    // =========================================================================
    [[nodiscard]] static std::string computeSha256(std::span<const std::uint8_t> data);
    [[nodiscard]] static std::string computeSha256ForFloats(const float* data, std::size_t count);

    // =========================================================================
    // Master Delivery Bundle & Verification Manifest Generator
    // =========================================================================
    [[nodiscard]] static std::string generateDeliveryManifestJson(
        std::string_view projectName,
        std::string_view clientName,
        std::span<const FileChecksumEntry> entries);
};

} // namespace dawg::engine
