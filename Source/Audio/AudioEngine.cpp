#include "AudioEngine.h"

namespace daw::audio
{
AudioEngine::~AudioEngine()
{
    shutdown();
}

juce::Result AudioEngine::initialise()
{
    shutdown();

    const auto error = deviceManager.initialiseWithDefaultDevices(0, 2);

    if (error.isNotEmpty())
        return juce::Result::fail(error);

    deviceManager.addAudioDeviceCallback(this);
    return juce::Result::ok();
}

void AudioEngine::shutdown() noexcept
{
    deviceManager.removeAudioDeviceCallback(this);
    deviceManager.closeAudioDevice();

    deviceReady.store(false, std::memory_order_release);
    sampleRate.store(0.0, std::memory_order_relaxed);
    blockSize.store(0, std::memory_order_relaxed);
}

bool AudioEngine::isDeviceReady() const noexcept
{
    return deviceReady.load(std::memory_order_acquire);
}

double AudioEngine::getSampleRate() const noexcept
{
    return sampleRate.load(std::memory_order_relaxed);
}

int AudioEngine::getBlockSize() const noexcept
{
    return blockSize.load(std::memory_order_relaxed);
}

void AudioEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(inputChannelData, numInputChannels, context);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    }
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    sampleRate.store(device->getCurrentSampleRate(), std::memory_order_relaxed);
    blockSize.store(device->getCurrentBufferSizeSamples(), std::memory_order_relaxed);
    deviceReady.store(true, std::memory_order_release);
}

void AudioEngine::audioDeviceStopped()
{
    deviceReady.store(false, std::memory_order_release);
}
} // namespace daw::audio
