#pragma once

#include <dawg/plugins/PluginPresetManager.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
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
    std::string m_pluginId;
    std::vector<PluginParameterDesc> m_params;
    OnParameterChanged m_onParamChanged;

    std::unique_ptr<plugins::PluginPresetManager> m_presetManager;
    juce::ComboBox m_presetCombo;
    std::array<juce::TextButton, 5> m_starButtons;
    juce::TextButton m_abToggleBtn {"[ A ]"};
    juce::TextButton m_copyAbBtn {"A->B"};
    juce::TextButton m_savePresetBtn {"Save"};
    int m_currentRating {5};
    bool m_isSlotA {true};

    void populatePresets(const std::string& query = "");
    void selectPreset(int index);
    void updateStars(int rating);

    juce::TextEditor m_searchFilter;
    juce::Viewport m_viewport;
    std::unique_ptr<juce::Component> m_container;
    std::vector<ParamRow> m_rows;
};

} // namespace dawg::ui
