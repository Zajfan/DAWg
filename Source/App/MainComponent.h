#pragma once

#include "../Audio/AudioAdapter.h"

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

    dawg::app::audio::AudioAdapter m_audioAdapter;
    juce::Label m_titleLabel;
    juce::Label m_statusLabel;
    juce::TextButton m_restartButton { "Restart audio device" };
};

} // namespace dawg::app
