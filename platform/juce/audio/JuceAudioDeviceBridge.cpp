#include "JuceAudioDeviceBridge.h"

#include <dawg/engine/AudioEngine.h>

namespace dawg::platform::juce {

JuceAudioDeviceBridge::JuceAudioDeviceBridge(dawg::engine::AudioEngine& engine)
    : m_engine(engine), m_callbackAdapter(engine)
{
}

JuceAudioDeviceBridge::~JuceAudioDeviceBridge()
{
    shutdown();
}

::juce::Result JuceAudioDeviceBridge::initialise()
{
    shutdown();
    const auto error = m_deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
        return ::juce::Result::fail(error);

    m_deviceManager.addAudioCallback(this);
    return ::juce::Result::ok();
}

void JuceAudioDeviceBridge::shutdown() noexcept
{
    // Removing the callback synchronizes with processing before releasing the
    // borrowed engine. Repeated shutdown (including destruction) is safe.
    m_deviceManager.removeAudioCallback(this);
    m_deviceManager.closeAudioDevice();
    audioDeviceStopped();
}

bool JuceAudioDeviceBridge::isDeviceReady() const noexcept
{
    return m_deviceReady.load(std::memory_order_acquire);
}

double JuceAudioDeviceBridge::sampleRate() const noexcept
{
    return m_sampleRate.load(std::memory_order_relaxed);
}

int JuceAudioDeviceBridge::blockSize() const noexcept
{
    return m_blockSize.load(std::memory_order_relaxed);
}

void JuceAudioDeviceBridge::audioDeviceAboutToStart(::juce::AudioIODevice* device)
{
    audioDeviceStopped();
    if (device == nullptr)
        return;

    const auto rate = device->getCurrentSampleRate();
    const auto frames = device->getCurrentBufferSizeSamples();
    const auto channels = device->getActiveOutputChannels().countNumberOfSetBits();
    if (frames <= 0 || channels <= 0
        || ! m_engine.prepare({rate, static_cast<std::size_t>(frames),
                               static_cast<std::size_t>(channels)}))
        return;

    m_sampleRate.store(rate, std::memory_order_relaxed);
    m_blockSize.store(frames, std::memory_order_relaxed);
    m_deviceReady.store(true, std::memory_order_release);
}

void JuceAudioDeviceBridge::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/, int /*numInputChannels*/,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const ::juce::AudioIODeviceCallbackContext& /*context*/)
{
    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    m_callbackAdapter.process(outputChannelData, static_cast<std::size_t>(numOutputChannels),
                              static_cast<std::size_t>(numSamples));
}

void JuceAudioDeviceBridge::audioDeviceStopped()
{
    m_engine.release();
    m_sampleRate.store(0.0, std::memory_order_relaxed);
    m_blockSize.store(0, std::memory_order_relaxed);
    m_deviceReady.store(false, std::memory_order_release);
}

} // namespace dawg::platform::juce
