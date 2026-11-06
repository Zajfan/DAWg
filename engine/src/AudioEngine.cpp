#include <dawg/engine/AudioEngine.h>

#include "TransportCommandQueue.h"

#include <algorithm>
#include <cmath>

namespace dawg::engine {

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

bool AudioEngine::prepare(const EngineConfig& config) noexcept
{
    m_prepared = std::isfinite(config.sampleRate) && config.sampleRate > 0.0
                 && config.maximumBlockSize > 0 && config.outputChannels > 0;
    return m_prepared;
}

void AudioEngine::release() noexcept
{
    m_prepared = false;
}

void AudioEngine::reset() noexcept
{
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
                m_transportState.samplePosition = command.samplePosition;
                break;
        }
    }

    if (output.channels == nullptr || output.frameCount == 0)
        return;

    for (std::size_t channel = 0; channel < output.channelCount; ++channel)
        if (output.channels[channel] != nullptr)
            std::fill_n(output.channels[channel], output.frameCount, 0.0f);
}

bool AudioEngine::isPrepared() const noexcept
{
    return m_prepared;
}

} // namespace dawg::engine
