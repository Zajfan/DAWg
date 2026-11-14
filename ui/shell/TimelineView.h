#pragma once

#include "EditorViewModel.h"

#include <dawg/media/WaveformCache.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawg::ui {

class TimelineView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using SeekCallback = std::function<void(std::int64_t samplePosition)>;

    TimelineView(CommandHandler handler,
                 media::WaveformCache* waveformCache = nullptr,
                 SeekCallback seekCallback = {});

    void setViewModel(const EditorViewModel& model);
    void setWaveformCache(media::WaveformCache* cache) noexcept { m_waveformCache = cache; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    void drawRuler(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTracksAndClips(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawLoopRegion(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] int sampleToPixel(std::int64_t sample) const noexcept;
    [[nodiscard]] std::int64_t pixelToSample(int x) const noexcept;

    CommandHandler m_handler;
    media::WaveformCache* m_waveformCache {nullptr};
    SeekCallback m_seekCallback;
    EditorViewModel m_model;

    double m_samplesPerPixel {256.0};
    int m_scrollOffsetX {0};
    int m_headerWidth {120};
    int m_rulerHeight {28};
    int m_trackLaneHeight {64};
};

} // namespace dawg::ui
