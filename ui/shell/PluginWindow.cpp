#include "PluginWindow.h"
#include "VirusTiPolarEditorView.h"
#include "Juno106EditorView.h"
#include "OmniFmEditorView.h"
#include "DspEditorDialog.h"

namespace dawg::ui {

PluginWindow::PluginWindow(
    const juce::String& name,
    std::unique_ptr<juce::Component> editorComponent,
    std::function<void()> onClose)
    : DocumentWindow(name,
                     juce::Colour::fromRGB(20, 22, 28),
                     DocumentWindow::allButtons)
    , m_onClose(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(480, 320, 1920, 1080);
    setContentOwned(editorComponent.release(), true);
    centreWithSize(720, 560);
    setVisible(true);
}

void PluginWindow::closeButtonPressed()
{
    if (m_onClose)
    {
        m_onClose();
    }
    delete this;
}

void PluginWindow::open(
    const std::string& pluginName,
    std::vector<PluginParameterDesc> params,
    std::function<void(std::uint32_t, float)> onParamChanged,
    std::function<void()> onClose)
{
    // High-priority redirect for flagship instruments & processors
    const auto lower = juce::String(pluginName).toLowerCase();
    if (lower.contains("virus") || lower.contains("polar"))
    {
        VirusTiPolarWindow::open();
        return;
    }
    if (lower.contains("juno"))
    {
        Juno106Window::open();
        return;
    }
    if (lower.contains("omnifm") || lower.contains("omni fm") || lower.contains("yamaha"))
    {
        OmniFmWindow::open();
        return;
    }
    if (lower.contains("buchla") || lower.contains("complex 259") || lower.contains("complex synth"))
    {
        DspEditorWindow::open(DspEditorKind::buchlaComplexSynth, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("resotamer") || lower.contains("resonance"))
    {
        DspEditorWindow::open(DspEditorKind::resonanceSuppressor, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("stereoscope") || lower.contains("multiband imager") || lower.contains("stereo imager"))
    {
        DspEditorWindow::open(DspEditorKind::multibandImager, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("masterphase") || lower.contains("linear phase") || lower.contains("linear-phase"))
    {
        DspEditorWindow::open(DspEditorKind::linearPhaseEq, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("parametric eq") || lower.contains("4-band"))
    {
        DspEditorWindow::open(DspEditorKind::parametricEq, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("compressor") || lower.contains("dynamics"))
    {
        DspEditorWindow::open(DspEditorKind::compressor, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("reverb") || lower.contains("plate"))
    {
        DspEditorWindow::open(DspEditorKind::reverb, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("delay") || lower.contains("echo"))
    {
        DspEditorWindow::open(DspEditorKind::delay, pluginName, std::move(onClose));
        return;
    }
    if (lower.contains("poly synth") || lower.contains("tnhc synth") || lower.contains("soundfont"))
    {
        DspEditorWindow::open(DspEditorKind::synth, pluginName, std::move(onClose));
        return;
    }

    auto editor = std::make_unique<GenericPluginEditorView>(
        pluginName,
        std::move(params),
        std::move(onParamChanged));

    new PluginWindow("Studio Rack: " + pluginName + " - DAWg", std::move(editor), std::move(onClose));
}

} // namespace dawg::ui
