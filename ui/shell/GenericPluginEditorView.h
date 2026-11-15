#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct PluginParameterDesc
{
    std::uint32_t id {0};
    std::string name {"Parameter"};
    float minVal {0.0f};
    float maxVal {1.0f};
    float defaultVal {0.5f};
    float currentVal {0.5f};
    std::string unitLabel {"%"};
    bool isStepped {false};
};

class GenericPluginEditorView final : public juce::Component
{
public:
    using OnParameterChanged = std::function<void(std::uint32_t paramId, float value)>;

    explicit GenericPluginEditorView(
        std::string pluginName = "Plugin Editor",
        std::vector<PluginParameterDesc> params = {},
        OnParameterChanged onParamChanged = nullptr);
    ~GenericPluginEditorView() override = default;

    void setParameters(std::vector<PluginParameterDesc> params);
    void updateParameterValue(std::uint32_t paramId, float value);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct ParamRow
    {
        PluginParameterDesc desc;
        std::unique_ptr<juce::Label> nameLabel;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> valueLabel;
    };

    void rebuildControls();

    std::string m_pluginName;
    std::vector<PluginParameterDesc> m_params;
    OnParameterChanged m_onParamChanged;

    juce::TextEditor m_searchFilter;
    juce::Viewport m_viewport;
    std::unique_ptr<juce::Component> m_container;
    std::vector<ParamRow> m_rows;
};

} // namespace dawg::ui
