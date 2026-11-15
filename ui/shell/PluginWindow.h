#pragma once

#include "GenericPluginEditorView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>

namespace dawg::ui {

class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow(
        const juce::String& name,
        std::unique_ptr<juce::Component> editorComponent,
        std::function<void()> onClose = nullptr);
    ~PluginWindow() override = default;

    void closeButtonPressed() override;

    static void open(
        const std::string& pluginName,
        std::vector<PluginParameterDesc> params = {},
        std::function<void(std::uint32_t, float)> onParamChanged = nullptr,
        std::function<void()> onClose = nullptr);

private:
    std::function<void()> m_onClose;
};

} // namespace dawg::ui
