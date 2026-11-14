#pragma once

#include <dawg/media/AudioAssetReader.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace dawg::platform::juce {

class JuceWavAssetReader final : public dawg::media::AudioAssetReader {
public:
    JuceWavAssetReader(std::unique_ptr<::juce::AudioFormatReader> reader,
                       dawg::media::AudioAssetMetadata metadata) noexcept;
    ~JuceWavAssetReader() override = default;

    JuceWavAssetReader(const JuceWavAssetReader&) = delete;
    JuceWavAssetReader& operator=(const JuceWavAssetReader&) = delete;

    [[nodiscard]] dawg::media::AudioAssetMetadata metadata() const noexcept override;
    [[nodiscard]] bool read(std::uint64_t sourceFrame,
                            std::span<float* const> destination,
                            std::size_t frameCount) noexcept override;

private:
    std::unique_ptr<::juce::AudioFormatReader> m_reader;
    dawg::media::AudioAssetMetadata m_metadata {};
};

class JuceWavAssetReaderFactory final : public dawg::media::AudioAssetReaderFactory {
public:
    JuceWavAssetReaderFactory() = default;
    ~JuceWavAssetReaderFactory() override = default;

    [[nodiscard]] std::expected<std::unique_ptr<dawg::media::AudioAssetReader>, dawg::media::MediaError>
    open(std::string_view path) override;
};

} // namespace dawg::platform::juce
