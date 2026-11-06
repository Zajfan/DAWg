#pragma once

#include "audio/JuceAudioDeviceBridge.h"

#include <dawg/engine/AudioEngine.h>

#include <juce_gui_extra/juce_gui_extra.h>

namespace dawg::app {

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAudioStatus();

    dawg::engine::AudioEngine m_engine;
    dawg::platform::juce::JuceAudioDeviceBridge m_audioBridge {m_engine};
    juce::Label m_titleLabel;
    juce::Label m_statusLabel;
    juce::TextButton m_restartButton { "Restart audio device" };
};

} // namespace dawg::app
