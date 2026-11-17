#pragma once

#include "EditorViewModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct SpatialTrackObject
{
    std::uint64_t trackId {0};
    std::string name {"Track"};
    juce::Colour color {juce::Colour::fromRGB(65, 145, 225)};
    float azimuthDeg {0.0f};      // -180 to +180
    float elevationDeg {0.0f};    // -90 to +90
    float distanceMeters {1.5f};  // 0.2 to 10.0
    float rmsLevel {0.0f};
};

class SpatialRadarView final : public juce::Component, private juce::Timer
{
public:
    using OnPositionChanged = std::function<void(std::uint64_t trackId, float az, float el, float dist)>;

    enum class SpatialFormat : std::uint8_t
    {
        binauralHrtf,
        surround714,
        ambisonicsB
    };

    explicit SpatialRadarView(OnPositionChanged onPositionChanged = nullptr);
    ~SpatialRadarView() override = default;

    void setViewModel(const EditorViewModel& model);
    void updateTrackPosition(std::uint64_t trackId, float az, float el, float dist);
    void setSpatialFormat(SpatialFormat format) noexcept;
    [[nodiscard]] SpatialFormat spatialFormat() const noexcept { return m_format; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    void timerCallback() override;

    [[nodiscard]] juce::Point<float> polarToScreen(float azDeg, float distMeters, juce::Rectangle<float> radarArea) const noexcept;
    [[nodiscard]] std::pair<float, float> screenToPolar(juce::Point<float> screenPos, juce::Rectangle<float> radarArea) const noexcept;
    [[nodiscard]] int findPuckAt(juce::Point<int> pos, juce::Rectangle<float> radarArea) const noexcept;

    OnPositionChanged m_onPositionChanged;
    std::vector<SpatialTrackObject> m_objects;
    int m_selectedIdx {-1};
    bool m_draggingPuck {false};
    SpatialFormat m_format {SpatialFormat::binauralHrtf};

    juce::ComboBox m_formatCombo;
    juce::TextButton m_hrtfHeadphoneButton {"🎧 Binaural HRTF"};
    juce::ComboBox m_headSizeCombo;
    juce::Slider m_elevationSlider;
    juce::Label m_elevationLabel;
    juce::Label m_azimuthLabel;
    juce::Label m_distanceLabel;
};

} // namespace dawg::ui
