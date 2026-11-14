#pragma once

#include <dawg/media/AudioAssetReader.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace dawg::media {

enum class ExportBitDepth : std::uint8_t
{
    int16,
    int24,
    float32
};

class AudioAssetWriter
{
public:
    virtual ~AudioAssetWriter() = default;

    [[nodiscard]] virtual bool write(std::span<const float* const> channels,
                                     std::size_t frameCount) noexcept = 0;
    [[nodiscard]] virtual bool finalize() noexcept = 0;
};

class AudioAssetWriterFactory
{
public:
    virtual ~AudioAssetWriterFactory() = default;

    [[nodiscard]] virtual std::expected<std::unique_ptr<AudioAssetWriter>, MediaError>
    create(std::string_view path,
           std::uint32_t sampleRate,
           std::uint16_t channelCount,
           ExportBitDepth bitDepth = ExportBitDepth::float32) = 0;
};

// In-memory writer factory for deterministic headless recording tests.
class MemoryAudioAssetWriterFactory final : public AudioAssetWriterFactory
{
public:
    std::vector<std::vector<float>> channels;
    bool finalized {false};

    [[nodiscard]] std::expected<std::unique_ptr<AudioAssetWriter>, MediaError>
    create(std::string_view path,
           std::uint32_t sampleRate,
           std::uint16_t channelCount,
           ExportBitDepth bitDepth = ExportBitDepth::float32) override;
};

} // namespace dawg::media
