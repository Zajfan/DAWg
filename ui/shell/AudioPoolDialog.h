#pragma once

#include <dawg/persistence/AudioPoolManager.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dawg::ui {

class AudioPoolComponent final : public juce::Component, public juce::ListBoxModel
{
public:
    explicit AudioPoolComponent(persistence::AudioPoolManager& poolManager,
                                std::function<void()> onProjectChanged = nullptr);
    ~AudioPoolComponent() override = default;

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshPoolView();

    persistence::AudioPoolManager& m_poolManager;
    std::function<void()> m_onProjectChanged;

    juce::Label m_titleLabel;
    juce::Label m_statsLabel;

    juce::ListBox m_listBox;

    juce::TextButton m_relinkButton {"Relink Missing Audio..."};
    juce::TextButton m_purgeButton {"Purge Unused Audio"};
    juce::TextButton m_consolidateButton {"Consolidate to Project Audio Folder"};
};

class AudioPoolWindow final : public juce::DocumentWindow
{
public:
    explicit AudioPoolWindow(persistence::AudioPoolManager& poolManager,
                             std::function<void()> onProjectChanged = nullptr);
    ~AudioPoolWindow() override = default;

    void closeButtonPressed() override;
    static void open(persistence::AudioPoolManager& poolManager,
                     std::function<void()> onProjectChanged = nullptr);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPoolWindow)
};

} // namespace dawg::ui
