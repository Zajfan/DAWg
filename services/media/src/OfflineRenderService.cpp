#include <dawg/media/OfflineRenderService.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/dsp/TnhcSynthNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <unordered_map>

namespace dawg::media {
namespace {

constexpr std::size_t kBlockSize = 512;

float calculateClipEnvelope(const std::uint64_t offset, const std::uint64_t totalLength,
                            const std::uint32_t fadeIn, const std::uint32_t fadeOut) noexcept
{
    float env = 1.0f;
    if (fadeIn > 0 && offset < fadeIn)
        env = std::min(env, static_cast<float>(offset) / static_cast<float>(fadeIn));
    if (fadeOut > 0 && offset + fadeOut > totalLength)
    {
        const auto remaining = (offset < totalLength) ? (totalLength - offset) : 0;
        env = std::min(env, static_cast<float>(remaining) / static_cast<float>(fadeOut));
    }
    return env;
}

} // namespace

void OfflineRenderService::applyTpdfDither(float* samples, const std::size_t count,
                                           std::uint32_t& rngState) noexcept
{
    if (samples == nullptr || count == 0)
        return;

    constexpr float scale = 32767.0f;
    constexpr float invScale = 1.0f / scale;

    for (std::size_t i = 0; i < count; ++i)
    {
        // Two uniform random variables subtracted produce triangular PDF
        rngState = rngState * 1664525u + 1013904223u;
        const float r1 = static_cast<float>(rngState >> 16) / 65536.0f;

        rngState = rngState * 1664525u + 1013904223u;
        const float r2 = static_cast<float>(rngState >> 16) / 65536.0f;

        const float ditherLsb = r1 - r2;
        const float scaled = samples[i] * scale + ditherLsb;
        const float clamped = std::clamp(std::round(scaled), -32768.0f, 32767.0f);
        samples[i] = clamped * invScale;
    }
}

OfflineRenderService::OfflineRenderService(AudioAssetWriterFactory& writerFactory,
                                           AudioAssetReaderFactory* readerFactory) noexcept
    : m_writerFactory(writerFactory)
    , m_readerFactory(readerFactory)
{
}

std::expected<void, MediaError> OfflineRenderService::renderPass(
    const project::ProjectSnapshot& project,
    const RenderOptions& options,
    const std::string& outputPath,
    const project::TrackId* isolatedTrackId,
    std::atomic<bool>* cancelToken,
    ProgressCallback progressCallback)
{
    auto writerExpected = m_writerFactory.create(outputPath, options.sampleRate, 2, options.bitDepth);
    if (! writerExpected)
        return std::unexpected(writerExpected.error());

    auto writer = std::move(*writerExpected);

    // Compute render timeline range
    std::uint64_t startSample = options.startSample;
    std::uint64_t endSample = options.endSample;

    if (endSample == 0)
    {
        for (const auto& clip : project.clips)
            endSample = std::max(endSample, clip.startSample + clip.lengthSamples);
        for (const auto& mClip : project.midiClips)
            endSample = std::max(endSample, mClip.startSample + mClip.lengthSamples);
        if (endSample <= startSample)
            endSample = startSample + static_cast<std::uint64_t>(options.sampleRate); // 1 second fallback
    }

    const std::uint64_t totalFrames = endSample - startSample;
    if (totalFrames == 0)
    {
        if (! writer->finalize())
        {
            return std::unexpected(MediaError {
                MediaError::Code::readFailure,
                "Failed to finalize output audio file"
            });
        }
        return {};
    }

    // Open readers for audio assets
    std::unordered_map<project::MediaAssetId, std::unique_ptr<AudioAssetReader>> readers;
    if (m_readerFactory != nullptr)
    {
        for (const auto& asset : project.assets)
        {
            auto readerExpected = m_readerFactory->open(asset.path);
            if (readerExpected)
                readers.emplace(asset.id, std::move(*readerExpected));
        }
    }

    // Initialize synths for instrument tracks
    std::unordered_map<project::TrackId, engine::TnhcSynthNode> synths;
    for (const auto& track : project.tracks)
    {
        if (track.kind == project::TrackKind::instrument)
        {
            (void) synths[track.id].prepare(static_cast<double>(options.sampleRate), kBlockSize, 2);
        }
    }

    // Preallocate buffers
    std::vector<float> masterLeft(kBlockSize, 0.0f);
    std::vector<float> masterRight(kBlockSize, 0.0f);
    std::vector<float> trackLeft(kBlockSize, 0.0f);
    std::vector<float> trackRight(kBlockSize, 0.0f);
    std::vector<float> clipLeft(kBlockSize, 0.0f);
    std::vector<float> clipRight(kBlockSize, 0.0f);

    const bool hasSolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                     [](const project::Track& t) { return t.solo; });

    std::uint32_t ditherRng = 987654321u;
    std::uint64_t currentTimeline = startSample;
    std::uint64_t framesRendered = 0;

    while (currentTimeline < endSample)
    {
        if (cancelToken != nullptr && cancelToken->load())
        {
            return std::unexpected(MediaError {
                MediaError::Code::unavailable,
                "Rendering cancelled by user"
            });
        }

        const auto framesToRender = static_cast<std::size_t>(
            std::min<std::uint64_t>(kBlockSize, endSample - currentTimeline));

        std::fill_n(masterLeft.begin(), framesToRender, 0.0f);
        std::fill_n(masterRight.begin(), framesToRender, 0.0f);

        for (const auto& track : project.tracks)
        {
            if (isolatedTrackId != nullptr && track.id != *isolatedTrackId)
                continue;
            if (isolatedTrackId == nullptr && (track.muted || (hasSolo && ! track.solo)))
                continue;

            std::fill_n(trackLeft.begin(), framesToRender, 0.0f);
            std::fill_n(trackRight.begin(), framesToRender, 0.0f);

            // Audio clips on track
            for (const auto& clip : project.clips)
            {
                if (clip.trackId != track.id)
                    continue;

                const auto clipStart = clip.startSample;
                const auto clipEnd = clipStart + clip.lengthSamples;
                const auto blockStart = currentTimeline;
                const auto blockEnd = currentTimeline + framesToRender;

                const auto overlapStart = std::max(clipStart, blockStart);
                const auto overlapEnd = std::min(clipEnd, blockEnd);

                if (overlapStart < overlapEnd)
                {
                    const auto overlapFrames = static_cast<std::size_t>(overlapEnd - overlapStart);
                    const auto blockOffset = static_cast<std::size_t>(overlapStart - blockStart);
                    const auto clipOffset = overlapStart - clipStart;
                    const auto sourceFrame = clip.sourceOffset + clipOffset;

                    std::fill_n(clipLeft.begin(), overlapFrames, 0.0f);
                    std::fill_n(clipRight.begin(), overlapFrames, 0.0f);

                    const auto readerIt = readers.find(clip.assetId);
                    if (readerIt != readers.end() && readerIt->second != nullptr)
                    {
                        std::array<float*, 2> dest {clipLeft.data(), clipRight.data()};
                        (void) readerIt->second->read(sourceFrame, dest, overlapFrames);
                    }

                    for (std::size_t f = 0; f < overlapFrames; ++f)
                    {
                        const float env = calculateClipEnvelope(clipOffset + f, clip.lengthSamples,
                                                                clip.fadeInSamples, clip.fadeOutSamples);
                        const float cGain = clip.gainLinear * env;
                        trackLeft[blockOffset + f] += clipLeft[f] * cGain;
                        trackRight[blockOffset + f] += clipRight[f] * cGain;
                    }
                }
            }

            // MIDI synthesis on instrument track
            if (track.kind == project::TrackKind::instrument)
            {
                auto& synth = synths[track.id];
                std::vector<engine::MidiEvent> midiEvents;

                for (const auto& mClip : project.midiClips)
                {
                    if (mClip.trackId != track.id)
                        continue;

                    for (const auto& note : mClip.notes)
                    {
                        const auto noteStart = mClip.startSample + note.startSample;
                        const auto noteEnd = noteStart + note.lengthSamples;

                        // Note On
                        if (noteStart >= currentTimeline && noteStart < currentTimeline + framesToRender)
                        {
                            const auto offset = static_cast<std::size_t>(noteStart - currentTimeline);
                            midiEvents.push_back({
                                .frameOffset = offset,
                                .data = {std::byte {0x90}, static_cast<std::byte>(note.pitch), static_cast<std::byte>(note.velocity)},
                                .size = 3
                            });
                        }
                        // Note Off
                        if (noteEnd >= currentTimeline && noteEnd < currentTimeline + framesToRender)
                        {
                            const auto offset = static_cast<std::size_t>(noteEnd - currentTimeline);
                            midiEvents.push_back({
                                .frameOffset = offset,
                                .data = {std::byte {0x80}, static_cast<std::byte>(note.pitch), std::byte {0}},
                                .size = 3
                            });
                        }
                    }
                }

                std::sort(midiEvents.begin(), midiEvents.end(), [](const auto& a, const auto& b) {
                    return a.frameOffset < b.frameOffset;
                });

                std::array<float*, 2> synthChannels {trackLeft.data(), trackRight.data()};
                engine::ProcessContext ctx {
                    .audio = engine::AudioBufferView {
                        .channels = synthChannels.data(),
                        .channelCount = 2,
                        .frameCount = framesToRender
                    },
                    .midi = engine::MidiBufferView {
                        .events = midiEvents.data(),
                        .eventCount = midiEvents.size()
                    },
                    .sampleRate = static_cast<double>(options.sampleRate)
                };
                synth.process(ctx);
            }

            // Panning and track gain
            const float pan = std::clamp(track.pan, -1.0f, 1.0f);
            constexpr float halfPi = 0.5f * std::numbers::pi_v<float>;
            const float panAngle = (pan + 1.0f) * 0.25f * halfPi;
            const float phase = track.phaseInvert ? -1.0f : 1.0f;
            const float leftGain = std::cos(panAngle) * track.gainLinear * phase;
            const float rightGain = std::sin(panAngle) * track.gainLinear * phase;

            for (std::size_t f = 0; f < framesToRender; ++f)
            {
                masterLeft[f] += trackLeft[f] * leftGain;
                masterRight[f] += trackRight[f] * rightGain;
            }
        }

        // Apply TPDF dither if 16-bit
        if (options.bitDepth == ExportBitDepth::int16 && options.applyTpdfDither)
        {
            applyTpdfDither(masterLeft.data(), framesToRender, ditherRng);
            applyTpdfDither(masterRight.data(), framesToRender, ditherRng);
        }

        // Write block
        const std::array<const float*, 2> outChannels {masterLeft.data(), masterRight.data()};
        if (! writer->write(outChannels, framesToRender))
        {
            return std::unexpected(MediaError {
                MediaError::Code::readFailure,
                "Failed to write audio block to output file"
            });
        }

        currentTimeline += framesToRender;
        framesRendered += framesToRender;

        if (progressCallback)
        {
            progressCallback({
                .framesRendered = framesRendered,
                .totalFrames = totalFrames,
                .fraction = std::clamp(static_cast<float>(framesRendered) / static_cast<float>(totalFrames), 0.0f, 1.0f)
            });
        }
    }

    if (! writer->finalize())
    {
        return std::unexpected(MediaError {
            MediaError::Code::readFailure,
            "Failed to finalize output audio file"
        });
    }

    return {};
}

std::expected<void, MediaError> OfflineRenderService::render(
    const project::ProjectSnapshot& project,
    const RenderOptions& options,
    std::atomic<bool>* cancelToken,
    ProgressCallback progressCallback)
{
    if (options.destinationPath.empty() || options.sampleRate == 0)
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidPath,
            "Destination path must not be empty and sample rate must be non-zero"
        });
    }

    if (! options.exportStems)
    {
        // Master bounce
        return renderPass(project, options, options.destinationPath, nullptr, cancelToken, progressCallback);
    }

    // Stem rendering
    namespace fs = std::filesystem;
    const fs::path dest(options.destinationPath);
    const auto stemBase = dest.parent_path() / dest.stem();
    const auto ext = dest.has_extension() ? dest.extension().string() : std::string(".wav");

    for (const auto& track : project.tracks)
    {
        if (! options.stemTrackIds.empty())
        {
            if (std::find(options.stemTrackIds.begin(), options.stemTrackIds.end(), track.id) == options.stemTrackIds.end())
                continue;
        }

        std::string safeTrackName = track.name;
        std::replace_if(safeTrackName.begin(), safeTrackName.end(), [](char c) {
            return c == '/' || c == '\\' || c == ' ' || c == ':' || c == '*';
        }, '_');

        const std::string stemPath = (stemBase.string() + "_stem_" + safeTrackName + ext);
        auto result = renderPass(project, options, stemPath, &track.id, cancelToken, progressCallback);
        if (! result)
            return result;
    }

    return {};
}

} // namespace dawg::media
