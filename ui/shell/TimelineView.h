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
    using TrackSelectedCallback = std::function<void(project::TrackId trackId)>;

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
        TrackSelectedCallback trackSelected;
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
    void toggleAutomation() noexcept;
    [[nodiscard]] bool showAutomation() const noexcept { return m_showAutomation; }

    struct TimelineMarker
    {
        std::uint64_t samplePosition {0};
        std::string name;
        juce::Colour color {juce::Colour(0xff58a6ff)};
    };

    void addMarker(std::uint64_t samplePosition, std::string name, juce::Colour color = juce::Colour(0xff58a6ff));
    void jumpToNextMarker();
    void jumpToPrevMarker();
    [[nodiscard]] const std::vector<TimelineMarker>& markers() const noexcept { return m_markers; }
    [[nodiscard]] int tracksTopY() const noexcept { return m_minimapHeight + m_rulerHeight; }

private:
    enum class DragMode
    {
        none,
        seekRuler,
        panMinimap,
        resizeTrackHeight,
        moveClip,
        trimLeft,
        trimRight,
        slipEdit,
        fadeLeft,
        fadeRight,
        moveAutomationPoint,
        dragTensionHandle,
        dragWarpMarker,
        swipeComping
    };

    void drawMinimap(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawRuler(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTracksAndClips(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawAutomationOverlay(juce::Graphics& g, const project::Track& track, int currentY, int laneWidth);
    void drawRazorAndSlipOverlay(juce::Graphics& g);
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
    bool m_showAutomation {false};
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
    project::AutomationLaneId m_dragAutomationLaneId {0};
    project::AutomationPointId m_dragAutomationPointId {0};
    float m_dragInitialTension {0.0f};
    std::size_t m_dragWarpMarkerIndex {0};
    project::TrackId m_compTrackId {0};
    std::uint32_t m_compTakeId {0};
    std::int64_t m_compStartSample {0};
    std::int64_t m_compEndSample {0};
    juce::Point<int> m_dragStartPos;
    juce::Point<int> m_lastMousePos;

    juce::TextButton m_snapButton {"Snap: 1/16"};
    juce::TextButton m_autoButton {"Auto: OFF"};
    juce::TextButton m_razorButton {"Razor [S]"};
    juce::TextButton m_addMarkerButton {"+ Marker"};
    bool m_razorMode {false};

    std::vector<TimelineMarker> m_markers;

    double m_samplesPerPixel {256.0};
    int m_scrollOffsetX {0};
    int m_headerWidth {150};
    int m_minimapHeight {22};
    int m_rulerHeight {28};
    int m_trackLaneHeight {64};
    int m_dragInitialTrackHeight {64};
};

} // namespace dawg::ui
