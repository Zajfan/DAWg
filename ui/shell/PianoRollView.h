#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace dawg::ui {

class PianoRollView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using AuditionCallback = std::function<void(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn)>;

    explicit PianoRollView(CommandHandler handler, AuditionCallback auditionCb = nullptr);
    ~PianoRollView() override = default;

    void setViewModel(const EditorViewModel& model);
    void setActiveClipId(std::optional<project::MidiClipId> clipId);
    [[nodiscard]] std::optional<project::MidiClipId> activeClipId() const noexcept { return m_activeClipId; }
    void setSelectedTrackId(project::TrackId trkId);
    [[nodiscard]] std::optional<project::TrackId> selectedTrackId() const noexcept { return m_selectedTrackId; }
    void setAuditionCallback(AuditionCallback cb) noexcept { m_auditionCallback = std::move(cb); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

    void quantizeNotes();
    void toggleStepRecord();
    [[nodiscard]] bool isStepRecordEnabled() const noexcept { return m_stepRecordEnabled; }
    void handleIncomingStepNote(std::uint8_t pitch, std::uint8_t velocity);

private:
    enum class QuantizeGrid : std::uint8_t
    {
        quarter,
        eighth,
        sixteenth,
        thirtySecond,
        eighthTriplet,
        sixteenthTriplet
    };

    enum class DragMode
    {
        none,
        moveNote,
        resizeNote,
        selectLasso,
        adjustVelocity
    };

    void drawPianoKeys(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawNotes(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawLasso(juce::Graphics& g, juce::Rectangle<int> bounds);
    enum class ExpressionMode : std::uint8_t
    {
        velocity,
        mpePitch,
        mpePressure,
        mpeTimbre,
        mpeVibrato,
        mpeSlide,
        vocalPitchTune
    };

    void drawVelocityLane(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] int sampleToPixel(std::int64_t sample) const noexcept;
    [[nodiscard]] std::int64_t pixelToSample(int x) const noexcept;
    [[nodiscard]] int pitchToPixel(std::uint8_t pitch) const noexcept;
    [[nodiscard]] std::uint8_t pixelToPitch(int y) const noexcept;
    [[nodiscard]] double gridIntervalSamples() const noexcept;
    [[nodiscard]] std::int64_t snapSample(std::int64_t sample) const noexcept;
    [[nodiscard]] bool isNoteSelected(project::NoteId id) const noexcept;
    [[nodiscard]] const project::MidiClip* findActiveClip() const noexcept;
    [[nodiscard]] const project::MidiNote* findNoteAt(juce::Point<int> pos, bool& outNearRightEdge) const noexcept;
    void selectAllNotes();

    CommandHandler m_handler;
    AuditionCallback m_auditionCallback;
    EditorViewModel m_model;
    std::optional<project::MidiClipId> m_activeClipId;
    std::optional<project::TrackId> m_selectedTrackId;
    std::optional<std::uint8_t> m_auditioningPitch;
    int m_virtualKeyboardOctave {4};

    std::vector<project::NoteId> m_selectedNoteIds;
    std::optional<project::NoteId> m_draggedNoteId;
    project::MidiNote m_dragInitialNote {};
    std::uint64_t m_dragTargetStartSample {0};
    std::uint8_t m_dragTargetPitch {60};
    std::uint64_t m_dragTargetLengthSamples {0};
    DragMode m_dragMode {DragMode::none};
    juce::Point<int> m_dragStartPos;
    juce::Rectangle<int> m_lassoRect;

    QuantizeGrid m_grid {QuantizeGrid::sixteenth};
    float m_swing {0.0f};

    juce::ComboBox m_gridCombo;
    juce::ComboBox m_swingCombo;
    juce::TextButton m_quantizeButton {"Quantize [Q]"};
    juce::TextButton m_btnEuclidean {"Euclidean"};
    juce::TextButton m_btnMarkov {"Chords"};
    juce::TextButton m_btnHumanize {"Humanize"};
    juce::TextButton m_btnStepRecord {"Step Rec"};
    bool m_stepRecordEnabled {false};
    std::uint64_t m_stepCursorSample {0};

    ExpressionMode m_expressionMode {ExpressionMode::velocity};
    juce::TextButton m_btnVelocity {"Vel"};
    juce::TextButton m_btnPitch {"MPE Pitch"};
    juce::TextButton m_btnPressure {"Pressure"};
    juce::TextButton m_btnTimbre {"Timbre CC74"};
    juce::TextButton m_btnVibrato {"Vibrato"};
    juce::TextButton m_btnSlide {"Slide"};
    juce::TextButton m_btnVocalTune {"Vocal Tune"};

    int m_toolbarHeight {28};
    int m_keyWidth {50};
    int m_noteRowHeight {14};
    int m_velocityHeight {60};
    double m_samplesPerPixel {128.0};
    int m_scrollY {60 * 14}; // Centered near C4 (MIDI note 60)
};

} // namespace dawg::ui
