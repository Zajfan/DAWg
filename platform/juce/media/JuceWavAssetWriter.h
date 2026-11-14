#pragma once

#include <dawg/media/AudioAssetWriter.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace dawg::platform::juce {

class JuceWavAssetWriter final : public dawg::media::AudioAssetWriter {
public:
    JuceWavAssetWriter(std::unique_ptr<::juce::AudioFormatWriter> writer,
                       std::uint16_t channelCount) noexcept;
    ~JuceWavAssetWriter() override = default;

    JuceWavAssetWriter(const JuceWavAssetWriter&) = delete;
    JuceWavAssetWriter& operator=(const JuceWavAssetWriter&) = delete;

    [[nodiscard]] bool write(std::span<const float* const> channels,
                             std::size_t frameCount) noexcept override;
    [[nodiscard]] bool finalize() noexcept override;

private:
    std::unique_ptr<::juce::AudioFormatWriter> m_writer;
    std::uint16_t m_channelCount {};
};

class JuceWavAssetWriterFactory final : public dawg::media::AudioAssetWriterFactory {
public:
    JuceWavAssetWriterFactory() = default;
    ~JuceWavAssetWriterFactory() override = default;

    [[nodiscard]] std::expected<std::unique_ptr<dawg::media::AudioAssetWriter>, dawg::media::MediaError>
    create(std::string_view path,
           std::uint32_t sampleRate,
           std::uint16_t channelCount) override;
};

} // namespace dawg::platform::juce
