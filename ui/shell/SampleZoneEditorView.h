#pragma once

#include <dawg/engine/dsp/MultiSampleInstrumentNode.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace dawg::ui {

class SampleZoneEditorView final : public juce::Component
{
public:
    using AuditionCallback = std::function<void(std::uint8_t note, std::uint8_t vel)>;

    explicit SampleZoneEditorView(AuditionCallback auditionCb = nullptr);
    ~SampleZoneEditorView() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void addSampleZone(engine::dsp::MultiSampleRegion region);
    void clearZones() noexcept;
    void setAuditionCallback(AuditionCallback cb) noexcept { m_auditionCb = std::move(cb); }

    [[nodiscard]] const std::vector<engine::dsp::MultiSampleRegion>& regions() const noexcept { return m_regions; }

private:
    [[nodiscard]] juce::Rectangle<int> zoneToRect(const engine::dsp::MultiSampleRegion& reg, juce::Rectangle<int> canvas) const noexcept;
    [[nodiscard]] int noteToX(std::uint8_t note, juce::Rectangle<int> canvas) const noexcept;
    [[nodiscard]] std::uint8_t xToNote(int x, juce::Rectangle<int> canvas) const noexcept;
    [[nodiscard]] int velToY(std::uint8_t vel, juce::Rectangle<int> canvas) const noexcept;
    [[nodiscard]] std::uint8_t yToVel(int y, juce::Rectangle<int> canvas) const noexcept;

    AuditionCallback m_auditionCb;
    std::vector<engine::dsp::MultiSampleRegion> m_regions;
    int m_selectedZoneIdx {-1};
    bool m_isDraggingZone {false};

    juce::TextButton m_addZoneButton {"+ Add Zone"};
    juce::TextButton m_removeZoneButton {"- Remove"};
    juce::TextButton m_loadSfzButton {"Load SFZ"};
    juce::Label m_infoLabel;
    juce::ComboBox m_articulationCombo;
    std::unique_ptr<juce::FileChooser> m_fileChooser;

    int m_toolbarHeight {28};
    int m_rulerWidth {36};
    int m_bottomKeyboardHeight {24};
};

} // namespace dawg::ui
