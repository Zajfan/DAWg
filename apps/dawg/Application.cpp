#include "Application.h"

#include "ApplicationState.h"
#include "audio/JuceAudioDeviceBridge.h"
#include "media/JuceWavAssetReader.h"
#include "window/WindowManager.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>
#include <dawg/media/AudioMediaService.h>

#include <algorithm>
#include <utility>

namespace dawg::app {

Application::Application() = default;
Application::~Application() = default;

const juce::String Application::getApplicationName() { return "DAWg"; }
const juce::String Application::getApplicationVersion() { return "0.1.0"; }
bool Application::moreThanOneInstanceAllowed() { return true; }

void Application::initialise(const juce::String&)
{
    m_assetReaderFactory = std::make_unique<platform::juce::JuceWavAssetReaderFactory>();
    m_engine = std::make_unique<engine::AudioEngine>();
    m_controller = std::make_unique<commands::ProjectController>(project::Project::create("Untitled", 1));
    m_state = std::make_unique<ApplicationState>(*m_controller, *m_engine);
    m_audioBridge = std::make_unique<platform::juce::JuceAudioDeviceBridge>(*m_engine);
    m_mediaService = std::make_unique<media::AudioMediaService>(*m_assetReaderFactory);
    m_windowManager = std::make_unique<platform::juce::window::WindowManager>();
    restartAudio();
    static_cast<void>(m_state->flushRenderSnapshot());

    ui::MainEditorView::TransportCallbacks transportCallbacks {
        .importWav = [this] { promptImportWav(); },
        .play = [this] { playTransport(); },
        .stop = [this] { stopTransport(); },
        .returnToStart = [this] { returnTransportToStart(); }
    };

    auto content = std::make_unique<ui::MainComponent>(
        m_state->editorViewModel(),
        [this](project::ProjectCommand command) { dispatchProjectCommand(std::move(command)); },
        std::move(transportCallbacks),
        audioStatus(),
        [this] { restartAudio(); });
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
    startTimerHz(15);
}

void Application::shutdown()
{
    stopTimer();
    m_fileChooser.reset();
    // Stop the media worker before removing the device callback, then tear down
    // UI, media service, audio bridge, state, controller, engine, and factory.
    if (m_mediaService)
        m_mediaService->stop();
    if (m_audioBridge)
        m_audioBridge->shutdown();
    m_windowManager.reset();
    m_mainComponent = nullptr;
    m_mediaService.reset();
    m_audioBridge.reset();
    m_state.reset();
    m_controller.reset();
    m_engine.reset();
    m_assetReaderFactory.reset();
}

void Application::systemRequestedQuit() { quit(); }

void Application::dispatchProjectCommand(project::ProjectCommand command)
{
    if (m_state->dispatch(std::move(command)))
        refreshMainComponent();
}

void Application::promptImportWav()
{
    m_fileChooser = std::make_unique<juce::FileChooser>(
        "Import WAV Audio", juce::File {}, "*.wav");
    m_fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto selected = chooser.getResult();
            if (selected != juce::File {})
                importWavFile(selected);
        });
}

void Application::importWavFile(const juce::File& file)
{
    if (!m_mediaService || !m_state)
        return;

    const auto fullPath = file.getFullPathName().toStdString();
    const auto inspected = m_mediaService->inspect(fullPath);
    if (!inspected.has_value()) {
        m_state->setMediaStatus(false, inspected.error().message);
        static_cast<void>(m_state->flushRenderSnapshot());
        refreshMainComponent();
        return;
    }

    auto asset = *inspected;
    asset.path = fullPath;
    asset.pathKind = project::AudioAssetPathKind::absolute;

    const auto blockSize = m_audioBridge ? std::max(1, m_audioBridge->blockSize()) : 512;
    const auto ringCapacityFrames = std::max<std::size_t>(
        4096u, static_cast<std::size_t>(blockSize) * 4u);

    auto draftAsset = asset;
    draftAsset.id = 1;
    const project::AudioClip draftClip {
        .id = 1,
        .assetId = draftAsset.id,
        .startSample = 0,
        .sourceOffset = 0,
        .lengthSamples = draftAsset.frameCount
    };

    const auto prepared = m_mediaService->prepare(draftAsset, draftClip, ringCapacityFrames);
    if (!prepared.has_value()) {
        m_state->setMediaStatus(false, prepared.error().message);
        static_cast<void>(m_state->flushRenderSnapshot());
        refreshMainComponent();
        return;
    }

    if (m_state->editorViewModel().trackCount() == 0) {
        if (!m_state->dispatch(project::AddAudioTrack {"Audio 1"})) {
            m_state->setMediaStatus(false, "Cannot create audio track for imported WAV");
            refreshMainComponent();
            return;
        }
    }

    const auto assetId = m_state->addAudioAsset(std::move(asset));
    if (!assetId.has_value()) {
        m_state->setMediaStatus(false, "Cannot register imported WAV asset in project");
        refreshMainComponent();
        return;
    }

    const auto clipId = m_state->addAudioClip(project::AudioClip {
        .id = 0,
        .assetId = *assetId,
        .startSample = 0,
        .sourceOffset = 0,
        .lengthSamples = draftAsset.frameCount
    });
    if (!clipId.has_value()) {
        m_state->setMediaStatus(false, "Cannot create audio clip for imported WAV");
        refreshMainComponent();
        return;
    }

    m_state->setMediaStatus(true, {});
    m_state->setPlaybackBuffer(&m_mediaService->buffer());
    static_cast<void>(m_state->flushRenderSnapshot());
    m_mediaService->start();
    refreshMainComponent();
}

void Application::playTransport()
{
    if (m_state && m_state->play())
        refreshMainComponent();
}

void Application::stopTransport()
{
    if (m_state && m_state->stop())
        refreshMainComponent();
}

void Application::returnTransportToStart()
{
    if (m_state && m_state->returnToStart())
        refreshMainComponent();
}

void Application::restartAudio()
{
    const auto result = m_audioBridge->initialise();
    m_audioError = result.failed() ? result.getErrorMessage().toStdString() : std::string {};
    if (m_mainComponent != nullptr)
        m_mainComponent->setAudioStatus(audioStatus());
}

void Application::syncMediaStatus()
{
    if (!m_mediaService || !m_state)
        return;

    const auto mediaStatus = m_mediaService->status();
    if (mediaStatus.state == media::MediaStatusState::unavailable
        || mediaStatus.state == media::MediaStatusState::error) {
        m_state->setMediaStatus(
            false,
            mediaStatus.errorMessage.empty() ? std::string {"Media unavailable"}
                                             : mediaStatus.errorMessage);
    }
}

void Application::refreshMainComponent()
{
    if (m_mainComponent != nullptr && m_state != nullptr)
        m_mainComponent->setViewModel(m_state->editorViewModel());
}

ui::AudioStatus Application::audioStatus() const
{
    return {m_audioBridge->isDeviceReady(), m_audioBridge->sampleRate(),
            m_audioBridge->blockSize(), m_audioError};
}

void Application::timerCallback()
{
    syncMediaStatus();
    static_cast<void>(m_state->flushRenderSnapshot());
    if (m_mainComponent != nullptr) {
        m_mainComponent->setViewModel(m_state->editorViewModel());
        m_mainComponent->setAudioStatus(audioStatus());
    }
}

} // namespace dawg::app
