#include "MainComponent.h"

#include "../Window/WindowManager.h"

#include <memory>
#include <utility>

namespace dawg::app
{
class DawApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "DAWg"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        dawg::app::window::WindowOptions options;
        options.title = getApplicationName().toStdString();
        options.bounds = { 0, 0, 900, 560 };
        options.minimumWidth = 640;
        options.minimumHeight = 360;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.closeRequested = [this](const dawg::app::window::WindowHandle) {
            systemRequestedQuit();
        };

        const auto handle =
            m_windowManager.createWindow(std::make_unique<MainComponent>(), std::move(options));
        jassert(handle.has_value());
        if (handle)
            m_mainWindow = *handle;
    }

    void shutdown() override
    {
        if (m_mainWindow != 0) {
            static_cast<void>(m_windowManager.closeWindow(m_mainWindow));
            m_mainWindow = {};
        }
    }

private:
    dawg::app::window::WindowManager m_windowManager;
    dawg::app::window::WindowHandle m_mainWindow {};
};
} // namespace dawg::app

START_JUCE_APPLICATION(dawg::app::DawApplication)
