#pragma once

#include "../Audio/AudioEngine.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace daw::app
{
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

    daw::audio::AudioEngine audioEngine;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton restartButton { "Restart audio device" };
};
} // namespace daw::app
