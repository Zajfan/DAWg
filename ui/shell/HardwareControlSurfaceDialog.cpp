#include "HardwareControlSurfaceDialog.h"

namespace dawg::ui {

HardwareControlSurfaceComponent::HardwareControlSurfaceComponent(engine::HardwareControlSurfaceManager& manager)
    : m_manager(manager)
{
    m_titleLabel.setText("Tactile Studio Control Surface Setup", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff58a6ff));
    addAndMakeVisible(m_titleLabel);

    m_enableToggle.setToggleState(m_manager.config().enabled, juce::dontSendNotification);
    m_enableToggle.onClick = [this] { applySettings(); };
    addAndMakeVisible(m_enableToggle);

    m_profileLabel.setText("Controller Protocol:", juce::dontSendNotification);
    m_profileLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc9d1d9));
    addAndMakeVisible(m_profileLabel);

    m_profileCombo.addItem("Mackie Control Universal (MCU)", 1);
    m_profileCombo.addItem("Avid HUI", 2);
    m_profileCombo.addItem("Novation Launchpad Pro", 3);
    m_profileCombo.addItem("Ableton Push 2", 4);
    m_profileCombo.addItem("Akai APC40 mkII", 5);
    m_profileCombo.addItem("Generic RGB Grid Controller", 6);

    switch (m_manager.config().profile)
    {
    case engine::ControllerProfile::mackieControlUniversal: m_profileCombo.setSelectedId(1, juce::dontSendNotification); break;
    case engine::ControllerProfile::hui:                   m_profileCombo.setSelectedId(2, juce::dontSendNotification); break;
    case engine::ControllerProfile::launchpadPro:           m_profileCombo.setSelectedId(3, juce::dontSendNotification); break;
    case engine::ControllerProfile::push2:                  m_profileCombo.setSelectedId(4, juce::dontSendNotification); break;
    case engine::ControllerProfile::apc40Mk2:               m_profileCombo.setSelectedId(5, juce::dontSendNotification); break;
    case engine::ControllerProfile::genericRgbGrid:         m_profileCombo.setSelectedId(6, juce::dontSendNotification); break;
    }
    m_profileCombo.onChange = [this] { applySettings(); };
    addAndMakeVisible(m_profileCombo);

    m_inputPortLabel.setText("MIDI Input Device:", juce::dontSendNotification);
    m_inputPortLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc9d1d9));
    addAndMakeVisible(m_inputPortLabel);
    m_inputPortCombo.onChange = [this] { applySettings(); };
    addAndMakeVisible(m_inputPortCombo);

    m_outputPortLabel.setText("MIDI Output Device:", juce::dontSendNotification);
    m_outputPortLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc9d1d9));
    addAndMakeVisible(m_outputPortLabel);
    m_outputPortCombo.onChange = [this] { applySettings(); };
    addAndMakeVisible(m_outputPortCombo);

    m_pickupModeLabel.setText("Parameter Pickup Mode:", juce::dontSendNotification);
    m_pickupModeLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc9d1d9));
    addAndMakeVisible(m_pickupModeLabel);

    m_pickupModeCombo.addItem("Pickup / Soft Takeover (Catch threshold)", 1);
    m_pickupModeCombo.addItem("Jump (Immediate physical snap)", 2);
    m_pickupModeCombo.addItem("Value Scaling (Smooth convergence)", 3);

    switch (m_manager.config().pickupMode)
    {
    case engine::ParameterPickupMode::pickup:     m_pickupModeCombo.setSelectedId(1, juce::dontSendNotification); break;
    case engine::ParameterPickupMode::jump:       m_pickupModeCombo.setSelectedId(2, juce::dontSendNotification); break;
    case engine::ParameterPickupMode::valueScale: m_pickupModeCombo.setSelectedId(3, juce::dontSendNotification); break;
    }
    m_pickupModeCombo.onChange = [this] { applySettings(); };
    addAndMakeVisible(m_pickupModeCombo);

    m_autoDetectButton.onClick = [this] {
        const auto inName = m_inputPortCombo.getText();
        if (!inName.isEmpty())
        {
            const auto detected = engine::HardwareControlSurfaceManager::autoDetectProfile(inName.toStdString());
            switch (detected)
            {
            case engine::ControllerProfile::mackieControlUniversal: m_profileCombo.setSelectedId(1); break;
            case engine::ControllerProfile::hui:                   m_profileCombo.setSelectedId(2); break;
            case engine::ControllerProfile::launchpadPro:           m_profileCombo.setSelectedId(3); break;
            case engine::ControllerProfile::push2:                  m_profileCombo.setSelectedId(4); break;
            case engine::ControllerProfile::apc40Mk2:               m_profileCombo.setSelectedId(5); break;
            case engine::ControllerProfile::genericRgbGrid:         m_profileCombo.setSelectedId(6); break;
            }
            m_statusLabel.setText("Auto-detected device profile: " + m_profileCombo.getText(), juce::dontSendNotification);
        }
    };
    addAndMakeVisible(m_autoDetectButton);

    m_pingButton.onClick = [this] {
        m_manager.syncCycle();
        m_statusLabel.setText("Sent hardware ping / refresh packets.", juce::dontSendNotification);
    };
    addAndMakeVisible(m_pingButton);

    m_statusLabel.setText("Bi-directional sync active (30 Hz refresh loop)", juce::dontSendNotification);
    m_statusLabel.setFont(juce::FontOptions(12.0f));
    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff3fb950));
    addAndMakeVisible(m_statusLabel);

    refreshMidiPorts();
    startTimer(1000); // Poll for device plug/unplug every second
}

HardwareControlSurfaceComponent::~HardwareControlSurfaceComponent()
{
    stopTimer();
}

void HardwareControlSurfaceComponent::timerCallback()
{
    // Auto-update status
    if (m_manager.isSyncing())
    {
        m_statusLabel.setText("Bi-directional sync active (30 Hz refresh loop)", juce::dontSendNotification);
        m_statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff3fb950));
    }
    else
    {
        m_statusLabel.setText("Hardware sync offline", juce::dontSendNotification);
        m_statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
    }
}

void HardwareControlSurfaceComponent::refreshMidiPorts()
{
    const auto inDevices = juce::MidiInput::getAvailableDevices();
    const auto outDevices = juce::MidiOutput::getAvailableDevices();

    m_inputPortCombo.clear();
    m_inputPortCombo.addItem("None", 1);
    for (int i = 0; i < inDevices.size(); ++i)
    {
        m_inputPortCombo.addItem(inDevices[i].name, i + 2);
    }
    if (m_inputPortCombo.getSelectedId() == 0)
        m_inputPortCombo.setSelectedId(inDevices.isEmpty() ? 1 : 2, juce::dontSendNotification);

    m_outputPortCombo.clear();
    m_outputPortCombo.addItem("None", 1);
    for (int i = 0; i < outDevices.size(); ++i)
    {
        m_outputPortCombo.addItem(outDevices[i].name, i + 2);
    }
    if (m_outputPortCombo.getSelectedId() == 0)
        m_outputPortCombo.setSelectedId(outDevices.isEmpty() ? 1 : 2, juce::dontSendNotification);
}

void HardwareControlSurfaceComponent::applySettings()
{
    engine::SurfaceConfig cfg {};
    cfg.enabled = m_enableToggle.getToggleState();

    switch (m_profileCombo.getSelectedId())
    {
    case 1: cfg.profile = engine::ControllerProfile::mackieControlUniversal; break;
    case 2: cfg.profile = engine::ControllerProfile::hui;                   break;
    case 3: cfg.profile = engine::ControllerProfile::launchpadPro;           break;
    case 4: cfg.profile = engine::ControllerProfile::push2;                  break;
    case 5: cfg.profile = engine::ControllerProfile::apc40Mk2;               break;
    default: cfg.profile = engine::ControllerProfile::genericRgbGrid;         break;
    }

    switch (m_pickupModeCombo.getSelectedId())
    {
    case 2: cfg.pickupMode = engine::ParameterPickupMode::jump;       break;
    case 3: cfg.pickupMode = engine::ParameterPickupMode::valueScale; break;
    default: cfg.pickupMode = engine::ParameterPickupMode::pickup;     break;
    }

    cfg.inputPortId = m_inputPortCombo.getText().toStdString();
    cfg.outputPortId = m_outputPortCombo.getText().toStdString();

    m_manager.setConfig(cfg);
}

void HardwareControlSurfaceComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161b22));
    g.setColour(juce::Colour(0xff30363d));
    g.drawRect(getLocalBounds(), 1);
}

void HardwareControlSurfaceComponent::resized()
{
    auto area = getLocalBounds().reduced(16);

    m_titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);

    m_enableToggle.setBounds(area.removeFromTop(24));
    area.removeFromTop(12);

    auto row1 = area.removeFromTop(26);
    m_profileLabel.setBounds(row1.removeFromLeft(160));
    m_profileCombo.setBounds(row1);
    area.removeFromTop(8);

    auto row2 = area.removeFromTop(26);
    m_inputPortLabel.setBounds(row2.removeFromLeft(160));
    m_inputPortCombo.setBounds(row2);
    area.removeFromTop(8);

    auto row3 = area.removeFromTop(26);
    m_outputPortLabel.setBounds(row3.removeFromLeft(160));
    m_outputPortCombo.setBounds(row3);
    area.removeFromTop(8);

    auto row4 = area.removeFromTop(26);
    m_pickupModeLabel.setBounds(row4.removeFromLeft(160));
    m_pickupModeCombo.setBounds(row4);
    area.removeFromTop(16);

    auto btnRow = area.removeFromTop(30);
    m_autoDetectButton.setBounds(btnRow.removeFromLeft(160));
    btnRow.removeFromLeft(12);
    m_pingButton.setBounds(btnRow.removeFromLeft(200));

    area.removeFromTop(12);
    m_statusLabel.setBounds(area.removeFromTop(24));
}

HardwareControlSurfaceWindow::HardwareControlSurfaceWindow(engine::HardwareControlSurfaceManager& manager)
    : juce::DocumentWindow("Hardware Control Surface Setup",
                           juce::Colour(0xff161b22),
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new HardwareControlSurfaceComponent(manager), true);
    setResizable(false, false);
    centreWithSize(520, 360);
    setVisible(true);
}

void HardwareControlSurfaceWindow::closeButtonPressed()
{
    delete this;
}

void HardwareControlSurfaceWindow::open(engine::HardwareControlSurfaceManager& manager)
{
    new HardwareControlSurfaceWindow(manager);
}

} // namespace dawg::ui
