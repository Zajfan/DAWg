#pragma once

#include <dawg/media/AudioAssetReader.h>
#include <dawg/media/AudioAssetWriter.h>
#include <dawg/project/ProjectSnapshot.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dawg::media {

enum class DitherNoiseShaping : std::uint8_t
{
    none,
    flatTpdf,
    lipshitz,
    psychoacoustic
};

struct RenderOptions
{
    std::string destinationPath;
    std::uint64_t startSample {0};
    std::uint64_t endSample {0}; // 0 = entire project arrangement
    std::uint32_t sampleRate {48000};
    ExportBitDepth bitDepth {ExportBitDepth::float32};
    bool applyTpdfDither {true};
    DitherNoiseShaping ditherNoiseShaping {DitherNoiseShaping::flatTpdf};
    bool exportStems {false};
    std::vector<project::TrackId> stemTrackIds {}; // empty = all tracks

    // Telemetry output populated during render pass
    mutable float measuredIntegratedLufs {-100.0f};
    mutable float measuredTruePeakDb {-100.0f};
    mutable float renderSpeedFactor {1.0f};
};

struct RenderProgress
{
    std::uint64_t framesRendered {0};
    std::uint64_t totalFrames {0};
    float fraction {0.0f}; // 0.0 to 1.0
};

using ProgressCallback = std::function<void(const RenderProgress&)>;

class OfflineRenderService final
{
public:
    explicit OfflineRenderService(AudioAssetWriterFactory& writerFactory,
                                  AudioAssetReaderFactory* readerFactory = nullptr) noexcept;

    [[nodiscard]] std::expected<void, MediaError> render(
        const project::ProjectSnapshot& project,
        const RenderOptions& options,
        std::atomic<bool>* cancelToken = nullptr,
        ProgressCallback progressCallback = {});

    // High quality TPDF and Noise-Shaped dither
    static void applyTpdfDither(float* samples, std::size_t count, std::uint32_t& rngState) noexcept;
    static void applyNoiseShapedDither(float* samples, std::size_t count, std::uint32_t& rngState, DitherNoiseShaping shaping) noexcept;

private:
    [[nodiscard]] std::expected<void, MediaError> renderPass(
        const project::ProjectSnapshot& project,
        const RenderOptions& options,
        const std::string& outputPath,
        const project::TrackId* isolatedTrackId,
        std::atomic<bool>* cancelToken,
        ProgressCallback progressCallback);

    AudioAssetWriterFactory& m_writerFactory;
    AudioAssetReaderFactory* m_readerFactory {nullptr};
};

} // namespace dawg::media
