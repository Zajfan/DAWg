#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace dawg::ui {

class FloatingPanelWindow final : public juce::DocumentWindow
{
public:
    FloatingPanelWindow(const juce::String& name,
                        juce::Component* panelComponent,
                        std::function<void()> onClose)
        : juce::DocumentWindow(name,
                               juce::Colour::fromRGB(24, 25, 29),
                               juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton | juce::DocumentWindow::maximiseButton)
        , m_content(panelComponent)
        , m_onClose(std::move(onClose))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        if (m_content != nullptr)
        {
            setContentNonOwned(m_content, false);
            m_content->setVisible(true);
        }
    }

    ~FloatingPanelWindow() override
    {
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        if (m_onClose)
            m_onClose();
    }

    [[nodiscard]] juce::Component* getContent() const noexcept { return m_content; }

private:
    juce::Component* m_content {nullptr};
    std::function<void()> m_onClose;
};

} // namespace dawg::ui
