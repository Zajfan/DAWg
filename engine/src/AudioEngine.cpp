#include <dawg/engine/AudioEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

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
