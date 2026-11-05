#include "AudioAdapter.h"

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
    m_deviceManager.closeAudioDevice();
    m_engine.release();
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

// --- Device callbacks --------------------------------------------------------

void AudioAdapter::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    m_deviceReady.store(false, std::memory_order_release);
    m_engine.release();
    if (device == nullptr)
        return;

    const auto sr = device->getCurrentSampleRate();
    const auto blockSize = device->getCurrentBufferSizeSamples();
    const auto channels = device->getActiveOutputChannels().countNumberOfSetBits();
    if (blockSize <= 0 || channels <= 0
        || ! m_engine.prepare({sr, static_cast<std::size_t>(blockSize),
                               static_cast<std::size_t>(channels)}))
        return;

    m_sampleRate.store(sr, std::memory_order_relaxed);
    m_blockSize.store(blockSize, std::memory_order_relaxed);
    m_deviceReady.store(true, std::memory_order_release);
}

void AudioAdapter::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/,
    int                 /*numInputChannels*/,
    float* const*       outputChannelData,
    int                 numOutputChannels,
    int                 numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/)
{
    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    m_engine.process({outputChannelData, static_cast<std::size_t>(numOutputChannels),
                      static_cast<std::size_t>(numSamples)}, {}, {});
}

void AudioAdapter::audioDeviceStopped()
{
    m_engine.release();
    m_deviceReady.store(false, std::memory_order_release);
}

} // namespace dawg::app::audio
