#include "AudioAdapter.h"

#include <cstring>

namespace dawg::app::audio {

AudioAdapter::~AudioAdapter()
{
    shutdown();
}

juce::Result AudioAdapter::initialise()
{
    shutdown();

    const auto error = m_deviceManager.initialiseWithDefaultDevices(0, 2);

    if (error.isNotEmpty())
        return juce::Result::fail(error);

    m_deviceManager.addAudioCallback(this);
    return juce::Result::ok();
}

void AudioAdapter::shutdown() noexcept
{
    m_deviceManager.removeAudioCallback(this);
    m_engine.stop();
    m_engine.shutdown();
    m_deviceManager.closeAudioDevice();

    m_processingBuffer.reset();
    m_deviceReady.store(false, std::memory_order_release);
    m_sampleRate.store(0.0,   std::memory_order_relaxed);
    m_blockSize.store(0,      std::memory_order_relaxed);
}

bool AudioAdapter::isDeviceReady() const noexcept
{
    return m_deviceReady.load(std::memory_order_acquire);
}

double AudioAdapter::getSampleRate() const noexcept
{
    return m_sampleRate.load(std::memory_order_relaxed);
}

int AudioAdapter::getBlockSize() const noexcept
{
    return m_blockSize.load(std::memory_order_relaxed);
}

dawg::engine::Transport& AudioAdapter::getTransport() noexcept
{
    return m_engine.getTransport();
}

dawg::engine::VoiceScheduler& AudioAdapter::getVoiceScheduler() noexcept
{
    return m_engine.getVoiceScheduler();
}

// --- Device callbacks --------------------------------------------------------

void AudioAdapter::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    const auto sr        = device->getCurrentSampleRate();
    const auto blockSize = static_cast<uint32_t>(device->getCurrentBufferSizeSamples());
    const auto channels  = static_cast<uint16_t>(2);

    dawg::engine::EngineConfig config;
    config.sampleRate  = static_cast<uint32_t>(sr);
    config.bufferSize  = blockSize;
    config.channels    = channels;

    m_engine.initialize(config);
    m_processingBuffer = std::make_unique<dawg::engine::AudioBuffer>(channels, blockSize);
    m_engine.start();

    m_sampleRate.store(sr,                        std::memory_order_relaxed);
    m_blockSize.store(static_cast<int>(blockSize), std::memory_order_relaxed);
    m_deviceReady.store(true,                     std::memory_order_release);
}

void AudioAdapter::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int                 /*numInputChannels*/,
    float* const*       outputChannelData,
    int                 numOutputChannels,
    int                 numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Real-time safe: no alloc, no mutex, no I/O.
    if (m_processingBuffer == nullptr)
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (outputChannelData[ch] != nullptr)
                std::memset(outputChannelData[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    m_processingBuffer->clear();
    m_engine.processAudio(*m_processingBuffer, nullptr);

    for (int ch = 0; ch < numOutputChannels; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;
        const float* src = m_processingBuffer->getChannelData(static_cast<uint16_t>(ch));
        if (src != nullptr)
            std::memcpy(outputChannelData[ch], src, sizeof(float) * static_cast<size_t>(numSamples));
        else
            std::memset(outputChannelData[ch], 0, sizeof(float) * static_cast<size_t>(numSamples));
    }
}

void AudioAdapter::audioDeviceStopped()
{
    m_engine.stop();
    m_deviceReady.store(false, std::memory_order_release);
}

} // namespace dawg::app::audio
