#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/SmpteTimecode.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace dawg::ui {

class VideoPlaybackWindow final : public juce::DocumentWindow
{
public:
    static void open(const EditorViewModel& model);
    static void close();
    static void updatePlayhead(const EditorViewModel& model);
    [[nodiscard]] static bool isOpen() noexcept;

    ~VideoPlaybackWindow() override;

private:
    explicit VideoPlaybackWindow(const EditorViewModel& model);

    void closeButtonPressed() override;

    class ContentComponent final : public juce::Component
    {
    public:
        explicit ContentComponent(const EditorViewModel& model);
        void setViewModel(const EditorViewModel& model);
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        EditorViewModel m_model;
        engine::SmpteFrameRate m_frameRate {engine::SmpteFrameRate::fps24};
        juce::ComboBox m_fpsSelector;
        juce::Label m_lblTimecode;
        juce::Label m_lblFrameNumber;
        juce::Label m_lblSyncStatus;
    };

    ContentComponent m_content;
    static std::unique_ptr<VideoPlaybackWindow> s_instance;
};

} // namespace dawg::ui
