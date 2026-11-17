#pragma once

#include <dawg/media/AudioExportPreset.h>
#include <dawg/media/OfflineRenderService.h>
#include <dawg/project/ProjectSnapshot.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace dawg::ui {

class BounceComponent final : public juce::Component, private juce::Timer
{
public:
    using StartRenderFn = std::function<void(
        const media::RenderOptions& options,
        std::function<void(float progress, const std::string& status)>,
        std::atomic<bool>* cancelToken,
        std::function<void(bool success, const std::string& error)> onComplete)>;

    BounceComponent(project::ProjectSnapshot snapshot, StartRenderFn startRender);
    ~BounceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void startBounce();
    void cancelBounce();

    project::ProjectSnapshot m_snapshot;
    StartRenderFn m_startRender;

    juce::Label m_destLabel {"DestLabel", "Export WAV File:"};
    juce::TextEditor m_destPathEdit;
    juce::TextButton m_browseButton {"Browse..."};

    juce::Label m_presetLabel {"PresetLabel", "Streaming Preset:"};
    juce::ComboBox m_presetCombo;

    juce::Label m_isrcLabel {"IsrcLabel", "ISRC / ID3 Tag:"};
    juce::TextEditor m_isrcEdit;

    juce::Label m_rangeLabel {"RangeLabel", "Timeline Range:"};
    juce::ComboBox m_rangeCombo;

    juce::Label m_formatLabel {"FormatLabel", "Bit Depth:"};
    juce::ComboBox m_formatCombo;

    juce::Label m_ditherLabel {"DitherLabel", "Mastering Dither:"};
    juce::ComboBox m_ditherCombo;

    juce::Label m_srLabel {"SrLabel", "Sample Rate:"};
    juce::ComboBox m_srCombo;

    juce::Label m_modeLabel {"ModeLabel", "Render Target:"};
    juce::ComboBox m_modeCombo;

    juce::TextButton m_startButton {"Start Bounce"};
    juce::TextButton m_cancelButton {"Cancel"};
    juce::Label m_statusLabel {"StatusLabel", "Ready to render"};
    juce::Label m_lufsVerificationLabel {"LufsLabel", ""};
    juce::Label m_speedBenchmarkLabel {"SpeedLabel", ""};

    double m_progressVal {0.0};
    juce::ProgressBar m_progressBar {m_progressVal};

    std::vector<float> m_renderedWaveform;
    void drawWaveformPreview(juce::Graphics& g, juce::Rectangle<float> area);

    std::atomic<bool> m_cancelToken {false};
    std::atomic<bool> m_isRendering {false};
    std::unique_ptr<juce::FileChooser> m_fileChooser;
};

class BounceWindow final : public juce::DocumentWindow
{
public:
    BounceWindow(project::ProjectSnapshot snapshot, BounceComponent::StartRenderFn startRender);
    ~BounceWindow() override = default;

    void closeButtonPressed() override;

    static void open(project::ProjectSnapshot snapshot, BounceComponent::StartRenderFn startRender);
};

} // namespace dawg::ui
