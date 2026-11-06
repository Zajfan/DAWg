#include "Application.h"

#include "ApplicationState.h"
#include "audio/JuceAudioDeviceBridge.h"
#include "window/WindowManager.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>

#include <utility>

namespace dawg::app {

Application::Application() = default;
Application::~Application() = default;

const juce::String Application::getApplicationName() { return "DAWg"; }
const juce::String Application::getApplicationVersion() { return "0.1.0"; }
bool Application::moreThanOneInstanceAllowed() { return true; }

void Application::initialise(const juce::String&)
{
    m_engine = std::make_unique<engine::AudioEngine>();
    m_controller = std::make_unique<commands::ProjectController>(project::Project::create("Untitled", 1));
    m_state = std::make_unique<ApplicationState>(*m_controller, *m_engine);
    m_audioBridge = std::make_unique<platform::juce::JuceAudioDeviceBridge>(*m_engine);
    m_windowManager = std::make_unique<platform::juce::window::WindowManager>();
    restartAudio();
    static_cast<void>(m_state->flushRenderSnapshot());

    auto content = std::make_unique<ui::MainComponent>(
        m_state->editorViewModel().project,
        [this](project::ProjectCommand command) { dispatchProjectCommand(std::move(command)); },
        audioStatus(), [this] { restartAudio(); });
    m_mainComponent = content.get();

    platform::juce::window::WindowOptions options;
    options.title = getApplicationName().toStdString();
    options.bounds = {0, 0, 900, 560};
    options.minimumWidth = 640;
    options.minimumHeight = 360;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.closeRequested = [this](platform::juce::window::WindowHandle) { systemRequestedQuit(); };
    const auto handle = m_windowManager->createWindow(std::move(content), std::move(options));
    jassert(handle.has_value());
    if (! handle) {
        quit();
        return;
    }
    startTimerHz(2);
}

void Application::shutdown()
{
    stopTimer();
    // Removing the device callback synchronizes with processing before any
    // project/engine storage or command targets can be destroyed.
    if (m_audioBridge)
        m_audioBridge->shutdown();
    m_windowManager.reset();
    m_mainComponent = nullptr;
    m_audioBridge.reset();
    m_state.reset();
    m_controller.reset();
    m_engine.reset();
}

void Application::systemRequestedQuit() { quit(); }

void Application::dispatchProjectCommand(project::ProjectCommand command)
{
    if (m_state->dispatch(std::move(command)) && m_mainComponent != nullptr)
        m_mainComponent->setProjectSnapshot(m_state->editorViewModel().project);
}

void Application::restartAudio()
{
    const auto result = m_audioBridge->initialise();
    m_audioError = result.failed() ? result.getErrorMessage().toStdString() : std::string {};
    if (m_mainComponent != nullptr)
        m_mainComponent->setAudioStatus(audioStatus());
}

ui::AudioStatus Application::audioStatus() const
{
    return {m_audioBridge->isDeviceReady(), m_audioBridge->sampleRate(),
            m_audioBridge->blockSize(), m_audioError};
}

void Application::timerCallback()
{
    static_cast<void>(m_state->flushRenderSnapshot());
    if (m_mainComponent != nullptr)
        m_mainComponent->setAudioStatus(audioStatus());
}

} // namespace dawg::app
