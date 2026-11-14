#include "media/JuceWavAssetReader.h"

#include <cmath>
#include <limits>
#include <memory>

namespace dawg::platform::juce {

JuceWavAssetReader::JuceWavAssetReader(std::unique_ptr<::juce::AudioFormatReader> reader,
                                       dawg::media::AudioAssetMetadata metadata) noexcept
    : m_reader(std::move(reader))
    , m_metadata(metadata)
{
}

dawg::media::AudioAssetMetadata JuceWavAssetReader::metadata() const noexcept
{
    return m_metadata;
}

bool JuceWavAssetReader::read(const std::uint64_t sourceFrame,
                              const std::span<float* const> destination,
                              const std::size_t frameCount) noexcept
{
    if (!m_reader || destination.size() < m_metadata.channelCount)
        return false;

    if (sourceFrame > m_metadata.frameCount || frameCount > m_metadata.frameCount - sourceFrame)
        return false;

    if (frameCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;

    if (sourceFrame > static_cast<std::uint64_t>(std::numeric_limits<::juce::int64>::max()))
        return false;

    for (std::uint16_t ch = 0; ch < m_metadata.channelCount; ++ch) {
        if (destination[ch] == nullptr)
            return false;
    }

    if (frameCount == 0)
        return true;

    try {
        return m_reader->read(destination.data(),
                              static_cast<int>(m_metadata.channelCount),
                              static_cast<::juce::int64>(sourceFrame),
                              static_cast<int>(frameCount));
    } catch (...) {
        return false;
    }
}

std::expected<std::unique_ptr<dawg::media::AudioAssetReader>, dawg::media::MediaError>
JuceWavAssetReaderFactory::open(const std::string_view path)
{
    if (path.empty() || path.find('\0') != std::string_view::npos
        || path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::invalidPath,
            "Audio asset path must be a non-empty UTF-8 path"});
    }

    const auto jucePath = ::juce::String::fromUTF8(path.data(), static_cast<int>(path.size()));
    if (jucePath.isEmpty()) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::invalidPath,
            "Audio asset path must be a valid UTF-8 path"});
    }

    const ::juce::File file = ::juce::File::isAbsolutePath(jucePath)
        ? ::juce::File(jucePath)
        : ::juce::File::getCurrentWorkingDirectory().getChildFile(jucePath);

    if (!file.existsAsFile()) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::cannotOpen,
            "Audio asset file does not exist"});
    }

    std::unique_ptr<::juce::FileInputStream> stream(file.createInputStream());
    if (!stream || !stream->openedOk()) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::cannotOpen,
            "Cannot open audio asset input stream"});
    }

    ::juce::WavAudioFormat wavFormat;
    std::unique_ptr<::juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(stream.release(), true));
    if (!reader) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::unsupportedFormat,
            "Audio file is not a supported WAV asset"});
    }

    if (!std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0
        || reader->sampleRate > static_cast<double>(std::numeric_limits<std::uint32_t>::max())
        || reader->numChannels == 0
        || reader->numChannels > static_cast<unsigned int>(std::numeric_limits<std::uint16_t>::max())
        || reader->lengthInSamples <= 0) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::invalidMetadata,
            "WAV metadata has invalid sample rate, channel count, or frame length"});
    }

    const dawg::media::AudioAssetMetadata metadata {
        .sampleRate = static_cast<std::uint32_t>(std::llround(reader->sampleRate)),
        .channelCount = static_cast<std::uint16_t>(reader->numChannels),
        .frameCount = static_cast<std::uint64_t>(reader->lengthInSamples)
    };

    return std::make_unique<JuceWavAssetReader>(std::move(reader), metadata);
}

} // namespace dawg::platform::juce
