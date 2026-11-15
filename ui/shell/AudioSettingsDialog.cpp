#include "AudioSettingsDialog.h"

#include <iomanip>
#include <sstream>

namespace dawg::ui {

AudioSettingsComponent::AudioSettingsComponent(juce::AudioDeviceManager& deviceManager,
                                               platform::juce::LowLatencyAudioDriver* driver)
    : m_selector(deviceManager,
                 1, 8,  // min/max input channels (support multi-track recording interfaces)
                 1, 8,  // min/max output channels
                 true,  // show audio input options
                 true,  // show audio output options
                 true,  // show MIDI input options
                 true)  // show MIDI output options
    , m_driver(driver ? driver : &m_fallbackDriver)
{
    addAndMakeVisible(m_selector);

    // Backend Selector
    m_backendLabel.setText("Pro-Audio Driver Backend:", juce::dontSendNotification);
    m_backendLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_backendLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 220, 240));
    addAndMakeVisible(m_backendLabel);

    m_backendCombo.addItem("PipeWire-JACK Native Pro-Audio Client (Sub-1.5ms RTL)", 1);
    m_backendCombo.addItem("Direct ALSA MMAP Hardware Driver (Direct Ring Buffers)", 2);
    m_backendCombo.addItem("Standard Platform Audio Engine", 3);
    m_backendCombo.setSelectedId(1, juce::dontSendNotification);
    m_backendCombo.onChange = [this] {
        const int id = m_backendCombo.getSelectedId();
        if (id == 1) m_driver->setBackend(platform::juce::AudioDriverBackend::pipewireJack);
        else if (id == 2) m_driver->setBackend(platform::juce::AudioDriverBackend::alsaMmap);
        else if (id == 3) m_driver->setBackend(platform::juce::AudioDriverBackend::standardPlatform);
        repaint();
    };
    addAndMakeVisible(m_backendCombo);

    // Buffer Presets
    m_bufferPresetLabel.setText("Buffer Size & Latency Preset:", juce::dontSendNotification);
    m_bufferPresetLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_bufferPresetLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 220, 240));
    addAndMakeVisible(m_bufferPresetLabel);

    m_bufferPresetCombo.addItem("32 frames (0.67 ms @ 48kHz — Ultra-Low Latency)", 1);
    m_bufferPresetCombo.addItem("64 frames (1.33 ms @ 48kHz — Commercial Studio Standard)", 2);
    m_bufferPresetCombo.addItem("128 frames (2.67 ms @ 48kHz — Safe Monitoring)", 3);
    m_bufferPresetCombo.addItem("256 frames (5.33 ms @ 48kHz — Moderate Load)", 4);
    m_bufferPresetCombo.addItem("512 frames (10.67 ms @ 48kHz — Heavy Mixing)", 5);
    m_bufferPresetCombo.setSelectedId(2, juce::dontSendNotification);
    m_bufferPresetCombo.onChange = [this] {
        static constexpr std::array<int, 5> sizes {32, 64, 128, 256, 512};
        const int idx = m_bufferPresetCombo.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(sizes.size()))
        {
            m_driver->configure(m_driver->sampleRate(), sizes[static_cast<std::size_t>(idx)]);
            repaint();
        }
    };
    addAndMakeVisible(m_bufferPresetCombo);

    // RTL HUD Labels
    m_rtlHudTitle.setText("ROUND-TRIP LATENCY (RTL)", juce::dontSendNotification);
    m_rtlHudTitle.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    m_rtlHudTitle.setColour(juce::Label::textColourId, juce::Colour::fromRGB(0, 230, 255));
    addAndMakeVisible(m_rtlHudTitle);

    m_rtlHudValue.setText("1.33 ms", juce::dontSendNotification);
    m_rtlHudValue.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    m_rtlHudValue.setColour(juce::Label::textColourId, juce::Colour::fromRGB(50, 220, 140));
    addAndMakeVisible(m_rtlHudValue);

    m_rtlDetailsLabel.setFont(juce::FontOptions(9.5f));
    m_rtlDetailsLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 170, 190));
    addAndMakeVisible(m_rtlDetailsLabel);

    m_xrunLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    addAndMakeVisible(m_xrunLabel);

    m_resetXrunButton.onClick = [this] {
        m_driver->resetXrunCount();
        repaint();
    };
    addAndMakeVisible(m_resetXrunButton);

    // Hardware MIDI
    m_controllerLabel.setText("Hardware Control Surface Profile:", juce::dontSendNotification);
    m_controllerLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_controllerLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 205, 215));
    addAndMakeVisible(m_controllerLabel);

    m_controllerProfileCombo.addItem("Generic MIDI Keyboard / MCU Surface", 1);
    m_controllerProfileCombo.addItem("Novation Launchpad (RGB Clip Launcher Grid)", 2);
    m_controllerProfileCombo.addItem("AKAI MPK Mini (8 Pads + 8 Knobs Auto-Mapped)", 3);
    m_controllerProfileCombo.addItem("Arturia KeyLab / Minilab", 4);
    m_controllerProfileCombo.addItem("Behringer X-Touch (Motorized Faders + V-Pots)", 5);
    m_controllerProfileCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_controllerProfileCombo);

    m_midiClockOutToggle.setToggleState(true, juce::dontSendNotification);
    m_midiClockOutToggle.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(100, 210, 240));
    addAndMakeVisible(m_midiClockOutToggle);

    setSize(620, 640);
    startTimerHz(10); // 10 Hz refresh for telemetry HUD
}

AudioSettingsComponent::~AudioSettingsComponent()
{
    stopTimer();
}

void AudioSettingsComponent::timerCallback()
{
    const auto tel = m_driver->telemetry();

    std::ostringstream ssRtl;
    ssRtl << std::fixed << std::setprecision(2) << tel.roundTripLatencyMs << " ms";
    m_rtlHudValue.setText(ssRtl.str(), juce::dontSendNotification);

    std::ostringstream ssDet;
    ssDet << "In: " << std::fixed << std::setprecision(2) << tel.inputLatencyMs << "ms | "
          << "DSP: " << tel.dspLatencyMs << "ms (" << tel.bufferSizeFrames << " spls) | "
          << "Out: " << tel.outputLatencyMs << "ms @ " << static_cast<int>(tel.sampleRate) << "Hz";
    m_rtlDetailsLabel.setText(ssDet.str(), juce::dontSendNotification);

    if (tel.xrunCount == 0)
    {
        m_xrunLabel.setText("Buffer Health: CLEAN (0 XRUNs)", juce::dontSendNotification);
        m_xrunLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(45, 215, 125));
    }
    else
    {
        m_xrunLabel.setText("Buffer Warning: " + std::to_string(tel.xrunCount) + " XRUNs detected!", juce::dontSendNotification);
        m_xrunLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(255, 70, 70));
    }
}

void AudioSettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(20, 23, 28));

    // Telemetry HUD Card background
    const auto hudArea = juce::Rectangle<float>(12.0f, static_cast<float>(getHeight() - 175), static_cast<float>(getWidth() - 24), 85.0f);
    g.setColour(juce::Colour::fromRGB(15, 18, 22));
    g.fillRoundedRectangle(hudArea, 6.0f);
    g.setColour(juce::Colour::fromRGB(36, 44, 56));
    g.drawRoundedRectangle(hudArea, 6.0f, 1.0f);
}

void AudioSettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(12);

    // Bottom section: Controls & Telemetry HUD
    auto bottomSection = bounds.removeFromBottom(250);

    // Audio Device Selector (native JUCE component) occupies upper area
    m_selector.setBounds(bounds);

    // 1. Backend row
    auto backendRow = bottomSection.removeFromTop(28);
    m_backendLabel.setBounds(backendRow.removeFromLeft(180));
    m_backendCombo.setBounds(backendRow);

    bottomSection.removeFromTop(4);

    // 2. Buffer preset row
    auto bufferRow = bottomSection.removeFromTop(28);
    m_bufferPresetLabel.setBounds(bufferRow.removeFromLeft(180));
    m_bufferPresetCombo.setBounds(bufferRow);

    bottomSection.removeFromTop(8);

    // 3. Telemetry HUD Area (85px high)
    auto hudArea = bottomSection.removeFromTop(85).reduced(10, 6);
    auto topHudRow = hudArea.removeFromTop(18);
    m_rtlHudTitle.setBounds(topHudRow.removeFromLeft(200));

    auto valRow = hudArea.removeFromTop(28);
    m_rtlHudValue.setBounds(valRow.removeFromLeft(140));
    m_xrunLabel.setBounds(valRow.removeFromLeft(220));
    m_resetXrunButton.setBounds(valRow.removeFromRight(100).reduced(2));

    m_rtlDetailsLabel.setBounds(hudArea.removeFromTop(16));

    bottomSection.removeFromTop(6);

    // 4. MIDI controller profile
    auto ctrlRow = bottomSection.removeFromTop(28);
    m_controllerLabel.setBounds(ctrlRow.removeFromLeft(200));
    m_controllerProfileCombo.setBounds(ctrlRow);

    m_midiClockOutToggle.setBounds(bottomSection.removeFromTop(24));
}

AudioSettingsWindow::AudioSettingsWindow(juce::AudioDeviceManager& deviceManager,
                                         platform::juce::LowLatencyAudioDriver* driver)
    : DocumentWindow("Audio Hardware & Low-Latency Pro-Audio Driver",
                     juce::Colour::fromRGB(20, 23, 28),
                     DocumentWindow::closeButton)
{
    setContentOwned(new AudioSettingsComponent(deviceManager, driver), true);
    setResizable(false, false);
    centreWithSize(getWidth(), getHeight());
}

void AudioSettingsWindow::closeButtonPressed()
{
    delete this;
}

void AudioSettingsWindow::open(juce::AudioDeviceManager& deviceManager,
                              platform::juce::LowLatencyAudioDriver* driver)
{
    auto* window = new AudioSettingsWindow(deviceManager, driver);
    window->setVisible(true);
}

} // namespace dawg::ui
