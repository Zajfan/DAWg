#pragma once

#include "EditorViewModel.h"

#include <dawg/media/WaveformCache.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace dawg::ui {

class TimelineView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using SeekCallback = std::function<void(std::int64_t samplePosition)>;
    using ActionCallback = std::function<void()>;

    enum class SnapMode : std::uint8_t
    {
        sixteenth,
        eighth,
        quarter,
        bar,
        off
    };

    struct Callbacks
    {
        CommandHandler commandHandler;
        SeekCallback seek;
        ActionCallback togglePlay;
        ActionCallback undo;
        ActionCallback redo;
    };

    explicit TimelineView(Callbacks callbacks, media::WaveformCache* waveformCache = nullptr);

    void setViewModel(const EditorViewModel& model);
    void setWaveformCache(media::WaveformCache* cache) noexcept { m_waveformCache = cache; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void cycleSnapMode();
    [[nodiscard]] SnapMode snapMode() const noexcept { return m_snapMode; }

private:
    enum class DragMode
    {
        none,
        seekRuler,
        moveClip,
        trimLeft,
        trimRight,
        fadeLeft,
        fadeRight
    };

    void drawRuler(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTracksAndClips(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawLoopRegion(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] int sampleToPixel(std::int64_t sample) const noexcept;
    [[nodiscard]] std::int64_t pixelToSample(int x) const noexcept;
    [[nodiscard]] std::int64_t snapSample(std::int64_t sample) const noexcept;
    [[nodiscard]] juce::Rectangle<int> getClipBounds(const project::AudioClip& clip, int laneY) const noexcept;
    [[nodiscard]] const project::AudioClip* findClipAt(juce::Point<int> pos, int& outLaneY) const noexcept;

    void seekToX(int x);

    Callbacks m_callbacks;
    media::WaveformCache* m_waveformCache {nullptr};
    EditorViewModel m_model;

    SnapMode m_snapMode {SnapMode::sixteenth};
    DragMode m_dragMode {DragMode::none};
    std::optional<project::ClipId> m_selectedClipId;
    std::optional<project::ClipId> m_draggedClipId;
    project::AudioClip m_dragInitialClip {};
    project::TrackId m_dragTargetTrackId {0};
    std::int64_t m_dragTargetStartSample {0};
    std::uint64_t m_dragTrimSourceOffset {0};
    std::uint64_t m_dragTrimLengthSamples {0};
    std::uint32_t m_dragFadeInSamples {0};
    std::uint32_t m_dragFadeOutSamples {0};
    juce::Point<int> m_dragStartPos;

    juce::TextButton m_snapButton {"Snap: 1/16"};

    double m_samplesPerPixel {256.0};
    int m_scrollOffsetX {0};
    int m_headerWidth {124};
    int m_rulerHeight {28};
    int m_trackLaneHeight {64};
};

} // namespace dawg::ui
