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
    auto error = m_deviceManager.initialiseWithDefaultDevices(2, 2);
    if (error.isNotEmpty())
        error = m_deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty())
        return ::juce::Result::fail(error);

    m_deviceManager.addAudioCallback(this);
    m_deviceManager.addMidiInputDeviceCallback({}, this);

    const auto midiDevices = ::juce::MidiInput::getAvailableDevices();
    for (const auto& dev : midiDevices)
        m_deviceManager.setMidiInputDeviceEnabled(dev.identifier, true);

    return ::juce::Result::ok();
}

void JuceAudioDeviceBridge::shutdown() noexcept
{
    // Removing the callback synchronizes with processing before releasing the
    // borrowed engine. Repeated shutdown (including destruction) is safe.
    m_deviceManager.removeMidiInputDeviceCallback({}, this);
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

    m_callbackAdapter.prepare(rate, static_cast<std::size_t>(frames));
    m_sampleRate.store(rate, std::memory_order_relaxed);
    m_blockSize.store(frames, std::memory_order_relaxed);
    m_deviceReady.store(true, std::memory_order_release);
}

void JuceAudioDeviceBridge::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numInputChannels,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const ::juce::AudioIODeviceCallbackContext& /*context*/)
{
    if (numOutputChannels <= 0 || numSamples <= 0)
        return;

    const auto inChannels = numInputChannels > 0 ? static_cast<std::size_t>(numInputChannels) : 0u;
    m_callbackAdapter.process(
        inputChannelData, inChannels,
        outputChannelData, static_cast<std::size_t>(numOutputChannels),
        static_cast<std::size_t>(numSamples));
}

void JuceAudioDeviceBridge::audioDeviceStopped()
{
    m_engine.release();
    m_sampleRate.store(0.0, std::memory_order_relaxed);
    m_blockSize.store(0, std::memory_order_relaxed);
    m_deviceReady.store(false, std::memory_order_release);
}

void JuceAudioDeviceBridge::triggerAuditionNote(const std::uint8_t pitch,
                                                const std::uint8_t velocity,
                                                const bool isNoteOn) noexcept
{
    m_callbackAdapter.triggerLiveSynthNote(pitch, velocity, isNoteOn);
}

void JuceAudioDeviceBridge::handleIncomingMidiMessage(::juce::MidiInput* /*source*/,
                                                      const ::juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        const auto pitch = static_cast<std::uint8_t>(message.getNoteNumber());
        const auto vel = static_cast<std::uint8_t>(message.getVelocity());
        m_callbackAdapter.triggerLiveSynthNote(pitch, vel, true);
        if (m_midiNoteCallback)
            m_midiNoteCallback(pitch, vel, true);
    }
    else if (message.isNoteOff())
    {
        const auto pitch = static_cast<std::uint8_t>(message.getNoteNumber());
        m_callbackAdapter.triggerLiveSynthNote(pitch, 0, false);
        if (m_midiNoteCallback)
            m_midiNoteCallback(pitch, 0, false);
    }

    dawg::engine::MidiEvent ev {};
    ev.size = std::min<std::uint8_t>(3, static_cast<std::uint8_t>(message.getRawDataSize()));
    for (std::uint8_t i = 0; i < ev.size; ++i)
        ev.data[i] = static_cast<std::byte>(message.getRawData()[i]);

    const auto processed = m_controllerManager.processEvent(ev);
    if (m_midiControlCallback)
        m_midiControlCallback(processed);
}

} // namespace dawg::platform::juce
