#include "PluginWindow.h"

namespace dawg::ui {

PluginWindow::PluginWindow(
    const juce::String& name,
    std::unique_ptr<juce::Component> editorComponent,
    std::function<void()> onClose)
    : DocumentWindow(name,
                     juce::Colour::fromRGB(24, 26, 32),
                     DocumentWindow::allButtons)
    , m_onClose(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setResizeLimits(400, 250, 1920, 1080);
    setContentOwned(editorComponent.release(), true);
    centreWithSize(640, 480);
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
    auto editor = std::make_unique<GenericPluginEditorView>(
        pluginName,
        std::move(params),
        std::move(onParamChanged));

    new PluginWindow("Plugin: " + pluginName + " - DAWg", std::move(editor), std::move(onClose));
}

} // namespace dawg::ui
