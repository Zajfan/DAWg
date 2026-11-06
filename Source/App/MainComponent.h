#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace dawg::platform::juce { class JuceAudioDeviceBridge; }

namespace dawg::app {

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent(dawg::platform::juce::JuceAudioDeviceBridge& audioBridge,
                  const juce::Result& initialisationResult);
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAudioStatus();

    // Borrows application-owned audio; the application outlives this component.
    dawg::platform::juce::JuceAudioDeviceBridge& m_audioBridge;
    juce::Label m_titleLabel;
    juce::Label m_statusLabel;
    juce::TextButton m_restartButton { "Restart audio device" };
};

} // namespace dawg::app
