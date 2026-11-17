#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/SpectralRepairEngine.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace dawg::ui {

class SpectralRepairDialog final : public juce::DocumentWindow
{
public:
    static void open(const EditorViewModel& model);
    static void close();

    ~SpectralRepairDialog() override;

private:
    explicit SpectralRepairDialog(const EditorViewModel& model);

    void closeButtonPressed() override;

    class ContentComponent final : public juce::Component
    {
    public:
        explicit ContentComponent(const EditorViewModel& model);
        void paint(juce::Graphics& g) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;

    private:
        void runRepair();
        EditorViewModel m_model;
        juce::ComboBox m_modeSelector;
        juce::Slider m_attenSlider;
        juce::TextButton m_btnApply {"Apply Spectral Repair"};
        juce::Label m_lblStatus;

        std::vector<std::vector<float>> m_spectrogram;
        juce::Rectangle<int> m_selectionRect;
        bool m_isSelecting {false};
    };

    ContentComponent m_content;
    static std::unique_ptr<SpectralRepairDialog> s_instance;
};

} // namespace dawg::ui
