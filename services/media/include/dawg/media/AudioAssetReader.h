#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace dawg::media {

struct AudioAssetMetadata
{
    std::uint32_t sampleRate {};
    std::uint16_t channelCount {};
    std::uint64_t frameCount {};
};

struct MediaError
{
    enum class Code
    {
        invalidPath,
        cannotOpen,
        unsupportedFormat,
        invalidMetadata,
        readFailure,
        unavailable
    };

    Code code {};
    std::string message;
};

class AudioAssetReader
{
public:
    virtual ~AudioAssetReader() = default;
    [[nodiscard]] virtual AudioAssetMetadata metadata() const noexcept = 0;
    [[nodiscard]] virtual bool read(std::uint64_t sourceFrame,
                                    std::span<float* const> destination,
                                    std::size_t frameCount) noexcept = 0;
};

class AudioAssetReaderFactory
{
public:
    virtual ~AudioAssetReaderFactory() = default;
    [[nodiscard]] virtual std::expected<std::unique_ptr<AudioAssetReader>, MediaError>
    open(std::string_view path) = 0;
};

} // namespace dawg::media
