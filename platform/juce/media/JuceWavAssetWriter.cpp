#include "media/JuceWavAssetWriter.h"

#include <limits>
#include <utility>

namespace dawg::platform::juce {

JuceWavAssetWriter::JuceWavAssetWriter(std::unique_ptr<::juce::AudioFormatWriter> writer,
                                       const std::uint16_t channelCount) noexcept
    : m_writer(std::move(writer))
    , m_channelCount(channelCount)
{
}

bool JuceWavAssetWriter::write(const std::span<const float* const> channels,
                               const std::size_t frameCount) noexcept
{
    if (!m_writer || m_channelCount == 0 || channels.size() < m_channelCount)
        return false;

    if (frameCount > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;

    for (std::uint16_t ch = 0; ch < m_channelCount; ++ch) {
        if (channels[ch] == nullptr)
            return false;
    }

    if (frameCount == 0)
        return true;

    try {
        return m_writer->writeFromFloatArrays(
            channels.data(),
            static_cast<int>(m_channelCount),
            static_cast<int>(frameCount));
    } catch (...) {
        return false;
    }
}

bool JuceWavAssetWriter::finalize() noexcept
{
    if (!m_writer)
        return false;

    try {
        const bool flushed = m_writer->flush();
        m_writer.reset();
        return flushed;
    } catch (...) {
        return false;
    }
}

std::expected<std::unique_ptr<dawg::media::AudioAssetWriter>, dawg::media::MediaError>
JuceWavAssetWriterFactory::create(const std::string_view path,
                                  const std::uint32_t sampleRate,
                                  const std::uint16_t channelCount)
{
    if (path.empty() || path.find('\0') != std::string_view::npos
        || path.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::invalidPath,
            "Audio asset output path must be a non-empty UTF-8 path"});
    }

    if (sampleRate == 0 || channelCount == 0) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::invalidMetadata,
            "Sample rate and channel count must be positive"});
    }

    const auto jucePath = ::juce::String::fromUTF8(path.data(), static_cast<int>(path.size()));
    if (jucePath.isEmpty()) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::invalidPath,
            "Audio asset output path must be a valid UTF-8 path"});
    }

    const ::juce::File file = ::juce::File::isAbsolutePath(jucePath)
        ? ::juce::File(jucePath)
        : ::juce::File::getCurrentWorkingDirectory().getChildFile(jucePath);

    if (file.existsAsFile() && !file.deleteFile()) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::cannotOpen,
            "Cannot overwrite existing audio asset file"});
    }

    std::unique_ptr<::juce::OutputStream> stream(file.createOutputStream());
    if (!stream) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::cannotOpen,
            "Cannot open audio asset output stream"});
    }

    ::juce::WavAudioFormat wavFormat;
    const auto options = ::juce::AudioFormatWriterOptions {}
        .withSampleRate(static_cast<double>(sampleRate))
        .withNumChannels(static_cast<int>(channelCount))
        .withBitsPerSample(32)
        .withSampleFormat(::juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    auto writer = wavFormat.createWriterFor(stream, options);
    if (!writer) {
        return std::unexpected(dawg::media::MediaError {
            dawg::media::MediaError::Code::unsupportedFormat,
            "Cannot create 32-bit float WAV writer"});
    }

    return std::make_unique<JuceWavAssetWriter>(std::move(writer), channelCount);
}

} // namespace dawg::platform::juce
