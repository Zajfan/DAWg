#pragma once

#include <dawg/plugins/ClapPluginLoader.h>
#include <dawg/project/ProjectTypes.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dawg::ui {

class ClapParameterEditorComponent final : public juce::Component
{
public:
    using ParamChangeCallback = std::function<void(std::uint32_t paramId, double value)>;
    using RestartCallback = std::function<void()>;

    ClapParameterEditorComponent(std::string pluginName,
                                 std::string pluginVendor,
                                 std::vector<plugins::ClapParamDescriptor> params,
                                 bool isCrashed,
                                 ParamChangeCallback paramChangeCb = nullptr,
                                 RestartCallback restartCb = nullptr);
    ~ClapParameterEditorComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setCrashed(bool crashed);
    [[nodiscard]] bool isCrashed() const noexcept { return m_crashed; }

private:
    struct ParamRow
    {
        std::uint32_t id {0};
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> valueDisplay;
    };

    class ParameterContainer final : public juce::Component
    {
    public:
        ParameterContainer() = default;
        ~ParameterContainer() override = default;
        void resized() override;
        std::vector<std::unique_ptr<ParamRow>> rows;
    };

    void filterParameters(const juce::String& filterText);

    std::string m_pluginName;
    std::string m_pluginVendor;
    std::vector<plugins::ClapParamDescriptor> m_descriptors;
    bool m_crashed {false};
    ParamChangeCallback m_paramChangeCb;
    RestartCallback m_restartCb;

    juce::Label m_titleLabel;
    juce::Label m_vendorLabel;
    juce::TextEditor m_searchFilter;
    juce::TextButton m_restartButton {"Restart Plugin"};
    juce::Viewport m_viewport;
    ParameterContainer m_container;
};

class ClapParameterEditorWindow final : public juce::DocumentWindow
{
public:
    ClapParameterEditorWindow(const juce::String& title,
                              std::unique_ptr<ClapParameterEditorComponent> content,
                              std::function<void()> onClose = nullptr);
    ~ClapParameterEditorWindow() override = default;

    void closeButtonPressed() override;

    static void open(const std::string& pluginName,
                     const std::string& pluginVendor,
                     std::vector<plugins::ClapParamDescriptor> params,
                     bool isCrashed,
                     ClapParameterEditorComponent::ParamChangeCallback paramChangeCb = nullptr,
                     ClapParameterEditorComponent::RestartCallback restartCb = nullptr,
                     std::function<void()> onClose = nullptr);

private:
    std::function<void()> m_onClose;
};

} // namespace dawg::ui
