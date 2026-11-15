#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct MatrixRoute
{
    project::TrackId sourceTrackId {0};
    std::string destination; // "Master", "Drum Bus", "Sidechain: Comp", "Aux Reverb", etc.
    float sendGain {1.0f};
    bool isSidechain {false};
    bool active {true};
};

class RoutingMatrixView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using ReferenceToggleCallback = std::function<void(bool referenceAActive)>;

    explicit RoutingMatrixView(CommandHandler handler = nullptr, ReferenceToggleCallback refCb = nullptr);
    ~RoutingMatrixView() override = default;

    void setViewModel(const EditorViewModel& model);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;

    [[nodiscard]] bool isReferenceMixActive() const noexcept { return m_referenceActive; }
    void toggleReferenceMix();

private:
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds);

    CommandHandler m_commandHandler;
    ReferenceToggleCallback m_refCallback;
    EditorViewModel m_model;

    bool m_referenceActive {false};
    std::vector<std::string> m_destinations;
    std::vector<MatrixRoute> m_routes;

    // Header controls
    juce::Label m_titleLabel;
    juce::TextButton m_abRefButton {"[ REF MIX A/B: NORMAL ]"};
    juce::TextButton m_quickSidechainButton {"+ Patch Kick -> Bass SC"};

    int m_headerHeight {36};
    int m_rowHeight {26};
    int m_colWidth {85};
    int m_labelColWidth {140};
};

} // namespace dawg::ui
