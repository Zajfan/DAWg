#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/PlaybackRingBuffer.h>

#include "TransportCommandQueue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

namespace dawg::engine {
namespace {
std::uint64_t saturatingAdd(const std::uint64_t lhs, const std::uint64_t rhs) noexcept
{
    return lhs > std::numeric_limits<std::uint64_t>::max() - rhs
        ? std::numeric_limits<std::uint64_t>::max()
        : lhs + rhs;
}

std::int64_t saturatingAdvance(const std::int64_t current, const std::size_t frames) noexcept
{
    const auto clampedCurrent = std::max<std::int64_t>(0, current);
    const auto maxAdvance = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max() - clampedCurrent);
    if (frames >= maxAdvance)
        return std::numeric_limits<std::int64_t>::max();
    return clampedCurrent + static_cast<std::int64_t>(frames);
}
} // namespace

AudioEngine::AudioEngine()
    : m_commandQueue(std::make_unique<TransportCommandQueue>())
{
}

AudioEngine::~AudioEngine() = default;

bool AudioEngine::enqueueCommand(const EngineCommand& command) noexcept
{
    return m_commandQueue->enqueue(command);
}

bool AudioEngine::publishRenderSnapshot(const RenderSnapshot snapshot) noexcept
{
    return m_snapshotPublisher.publish(snapshot);
}

std::uint64_t AudioEngine::renderSnapshotGeneration() const noexcept
{
    return m_renderSnapshot.generation;
}

TransportState AudioEngine::transportState() const noexcept
{
    return m_transportState;
}

std::uint64_t AudioEngine::underrunCount() const noexcept
{
    return m_transportState.underrunCount;
}

bool AudioEngine::prepare(const EngineConfig& config) noexcept
{
    if (! std::isfinite(config.sampleRate) || config.sampleRate <= 0.0
        || config.maximumBlockSize == 0 || config.outputChannels == 0)
    {
        m_discardChannel.clear();
        m_prepared = false;
        return false;
    }

    try
    {
        m_discardChannel.assign(config.maximumBlockSize, 0.0f);
    }
    catch (...)
    {
        m_discardChannel.clear();
        m_prepared = false;
        return false;
    }

    m_prepared = true;
    return true;
}

void AudioEngine::release() noexcept
{
    m_prepared = false;
}

void AudioEngine::reset() noexcept
{
    m_transportState = {};
}

void AudioEngine::process(const AudioBufferView output, const MidiBufferView,
                          const TransportSnapshot&) noexcept
{
    m_renderSnapshot = m_snapshotPublisher.consume();

    EngineCommand command {};
    for (std::size_t count = 0; count < TransportCommandQueue::capacity; ++count)
    {
        if (! m_commandQueue->dequeue(command))
            break;

        switch (command.type)
        {
            case EngineCommandType::play:
                m_transportState.playing = true;
                break;
            case EngineCommandType::stop:
                m_transportState.playing = false;
                break;
            case EngineCommandType::seekSamples:
                m_transportState.samplePosition = std::max<std::int64_t>(0, command.samplePosition);
                if (m_transportState.samplePosition == 0
                    && m_renderSnapshot.playback.buffer != nullptr)
                {
                    m_renderSnapshot.playback.buffer->requestSourceFrame(
                        m_renderSnapshot.playback.sourceOffset);
                }
                break;
        }
    }

    if (output.channels == nullptr || output.frameCount == 0)
        return;

    for (std::size_t channel = 0; channel < output.channelCount; ++channel)
        if (output.channels[channel] != nullptr)
            std::fill_n(output.channels[channel], output.frameCount, 0.0f);

    if (! m_prepared || ! m_transportState.playing)
        return;

    const auto& playback = m_renderSnapshot.playback;
    const auto blockStart = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, m_transportState.samplePosition));
    const auto blockEnd = saturatingAdd(blockStart, output.frameCount);

    if (playback.clipLengthSamples > 0 && playback.channelCount > 0)
    {
        const auto clipStart = playback.clipStartSample;
        const auto clipEnd = saturatingAdd(clipStart, playback.clipLengthSamples);
        const auto overlapStart = std::max(blockStart, clipStart);
        const auto overlapEnd = std::min(blockEnd, clipEnd);

        if (overlapStart < overlapEnd)
        {
            const auto overlapFrames = static_cast<std::size_t>(overlapEnd - overlapStart);
            const auto blockOffset = static_cast<std::size_t>(overlapStart - blockStart);
            const auto clipOffset = overlapStart - clipStart;
            const auto sourceFrame = saturatingAdd(playback.sourceOffset, clipOffset);

            if (playback.buffer == nullptr)
            {
                ++m_transportState.underrunCount;
            }
            else
            {
                playback.buffer->requestSourceFrame(sourceFrame);
                std::size_t framesRead = 0;

                if (playback.channelCount == 1)
                {
                    std::size_t primaryChannel = output.channelCount;
                    for (std::size_t channel = 0; channel < output.channelCount; ++channel)
                    {
                        if (output.channels[channel] != nullptr)
                        {
                            primaryChannel = channel;
                            break;
                        }
                    }

                    if (primaryChannel < output.channelCount)
                    {
                        float* primaryDest = output.channels[primaryChannel] + blockOffset;
                        std::array<float*, 1> dest {primaryDest};
                        framesRead = playback.buffer->read(sourceFrame, dest, overlapFrames);
                        for (std::size_t channel = primaryChannel + 1;
                             channel < output.channelCount; ++channel)
                        {
                            if (output.channels[channel] != nullptr)
                            {
                                std::copy_n(primaryDest, framesRead,
                                            output.channels[channel] + blockOffset);
                            }
                        }
                    }
                }
                else if (playback.channelCount == 2)
                {
                    float* const leftDest =
                        (output.channelCount >= 1 && output.channels[0] != nullptr)
                            ? output.channels[0] + blockOffset
                            : (overlapFrames <= m_discardChannel.size()
                                   ? m_discardChannel.data()
                                   : nullptr);
                    float* const rightDest =
                        (output.channelCount >= 2 && output.channels[1] != nullptr)
                            ? output.channels[1] + blockOffset
                            : (overlapFrames <= m_discardChannel.size()
                                   ? m_discardChannel.data()
                                   : nullptr);
                    if (leftDest != nullptr && rightDest != nullptr
                        && (leftDest != m_discardChannel.data()
                            || rightDest != m_discardChannel.data()))
                    {
                        std::array<float*, 2> dest {leftDest, rightDest};
                        framesRead = playback.buffer->read(sourceFrame, dest, overlapFrames);
                    }
                }

                if (framesRead < overlapFrames)
                    ++m_transportState.underrunCount;
            }
        }
    }

    m_transportState.samplePosition = saturatingAdvance(
        m_transportState.samplePosition, output.frameCount);
}

bool AudioEngine::isPrepared() const noexcept
{
    return m_prepared;
}

} // namespace dawg::engine
