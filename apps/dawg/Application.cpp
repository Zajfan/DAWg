#include "Application.h"

#include "ApplicationState.h"
#include "audio/JuceAudioDeviceBridge.h"
#include "media/JuceWavAssetReader.h"
#include "media/JuceWavAssetWriter.h"
#include "window/WindowManager.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/RecordRingBuffer.h>
#include <dawg/media/AudioMediaService.h>
#include <dawg/media/AudioRecordService.h>
#include <dawg/media/DiskStreamPool.h>
#include <dawg/media/WaveformCache.h>

#include <algorithm>
#include <cmath>
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
    m_assetWriterFactory = std::make_unique<platform::juce::JuceWavAssetWriterFactory>();
    m_engine = std::make_unique<engine::AudioEngine>();
    m_controller = std::make_unique<commands::ProjectController>(project::Project::create("Untitled", 1));
    m_state = std::make_unique<ApplicationState>(*m_controller, *m_engine);
    m_audioBridge = std::make_unique<platform::juce::JuceAudioDeviceBridge>(*m_engine);
    m_mediaService = std::make_unique<media::AudioMediaService>(*m_assetReaderFactory);
    m_diskStreamPool = std::make_unique<media::DiskStreamPool>(*m_assetReaderFactory, 2);
    m_recordService = std::make_unique<media::AudioRecordService>(*m_assetWriterFactory);
    m_waveformCache = std::make_unique<media::WaveformCache>(*m_assetReaderFactory, 2);
    m_engine->setRecordBuffer(&m_recordService->buffer());
    m_windowManager = std::make_unique<platform::juce::window::WindowManager>();
    restartAudio();
    static_cast<void>(m_state->flushRenderSnapshot());

    ui::MainEditorView::TransportCallbacks transportCallbacks {
        .importWav = [this] { promptImportWav(); },
        .play = [this] { playTransport(); },
        .stop = [this] { stopTransport(); },
        .returnToStart = [this] { returnTransportToStart(); },
        .record = [this] { toggleTransportRecord(); },
        .undo = [this] { undo(); },
        .redo = [this] { redo(); },
        .seek = [this](const std::int64_t pos) {
            if (m_state && m_state->seekSamples(pos))
                refreshMainComponent();
        },
        .setMasterLimiter = [this](const bool enabled) {
            if (m_state)
            {
                m_state->setMasterLimiterEnabled(enabled);
                refreshMainComponent();
            }
        }
    };

    auto content = std::make_unique<ui::MainComponent>(
        m_state->editorViewModel(),
        [this](project::ProjectCommand command) { dispatchProjectCommand(std::move(command)); },
        std::move(transportCallbacks),
        audioStatus(),
        [this] { restartAudio(); },
        m_waveformCache.get());
    m_mainComponent = content.get();

    platform::juce::window::WindowOptions options;
    options.title = getApplicationName().toStdString();
    options.bounds = {0, 0, 1024, 680};
    options.minimumWidth = 720;
    options.minimumHeight = 440;
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
    // Stop the media workers before removing the device callback, then tear down
    // UI, media services, audio bridge, state, controller, engine, and factories.
    if (m_waveformCache)
        m_waveformCache->clear();
    if (m_recordService)
        m_recordService->stop();
    if (m_diskStreamPool)
        m_diskStreamPool->stop();
    if (m_mediaService)
        m_mediaService->stop();
    if (m_audioBridge)
        m_audioBridge->shutdown();
    m_windowManager.reset();
    m_mainComponent = nullptr;
    m_waveformCache.reset();
    m_recordService.reset();
    m_diskStreamPool.reset();
    m_mediaService.reset();
    m_audioBridge.reset();
    m_state.reset();
    m_controller.reset();
    m_engine.reset();
    m_assetWriterFactory.reset();
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
    if (!m_mediaService || !m_diskStreamPool || !m_state)
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

    const auto trackId = m_state->editorViewModel().project.tracks.back().id;
    const auto assetId = m_state->addAudioAsset(asset);
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
        .lengthSamples = draftAsset.frameCount,
        .trackId = trackId
    });
    if (!clipId.has_value()) {
        m_state->setMediaStatus(false, "Cannot create audio clip for imported WAV");
        refreshMainComponent();
        return;
    }

    auto registeredAsset = asset;
    registeredAsset.id = *assetId;
    const project::AudioClip registeredClip {
        .id = *clipId,
        .assetId = *assetId,
        .startSample = 0,
        .sourceOffset = 0,
        .lengthSamples = draftAsset.frameCount,
        .trackId = trackId
    };
    const auto deviceRate = (m_audioBridge && m_audioBridge->sampleRate() > 0.0)
        ? static_cast<std::uint32_t>(std::llround(m_audioBridge->sampleRate()))
        : registeredAsset.sampleRate;
    const auto voiceId = m_diskStreamPool->addVoice(
        registeredAsset, registeredClip, ringCapacityFrames, deviceRate);
    if (!voiceId.has_value()) {
        m_state->setMediaStatus(false, voiceId.error().message);
        static_cast<void>(m_state->flushRenderSnapshot());
        refreshMainComponent();
        return;
    }

    auto* voiceBuffer = m_diskStreamPool->buffer(*voiceId);
    m_state->setMediaStatus(true, {});
    m_state->setClipBuffer(*clipId, voiceBuffer);
    m_state->setPlaybackBuffer(voiceBuffer);
    if (m_waveformCache)
        m_waveformCache->requestThumbnail(registeredAsset);
    static_cast<void>(m_state->flushRenderSnapshot());
    m_diskStreamPool->start();
    refreshMainComponent();
}

void Application::playTransport()
{
    if (! m_state || ! m_engine)
        return;

    if (m_state->isTransportRecordArmed() && ! m_engine->isRecording())
    {
        startRecordingSession();
    }

    if (m_state->play())
        refreshMainComponent();
}

void Application::stopTransport()
{
    if (! m_state || ! m_engine)
        return;

    if (m_engine->isRecording())
    {
        stopRecordingSession();
    }
    m_state->setTransportRecordArmed(false);

    if (m_state->stop())
        refreshMainComponent();
}

void Application::returnTransportToStart()
{
    if (m_state && m_state->returnToStart())
        refreshMainComponent();
}

void Application::toggleTransportRecord()
{
    if (! m_state || ! m_engine)
        return;

    const bool currentlyRecording = m_engine->isRecording();
    if (currentlyRecording)
    {
        stopRecordingSession();
        m_state->setTransportRecordArmed(false);
    }
    else
    {
        const bool willArm = ! m_state->isTransportRecordArmed();
        m_state->setTransportRecordArmed(willArm);
        if (willArm && m_engine->transportState().playing)
        {
            startRecordingSession();
        }
    }
    refreshMainComponent();
}

void Application::startRecordingSession()
{
    if (! m_recordService || ! m_state || ! m_engine)
        return;

    const auto armedTrack = m_state->firstArmedTrackId();
    if (! armedTrack.has_value())
        return;

    const auto takesDir = juce::File::getCurrentWorkingDirectory().getChildFile("takes");
    if (! takesDir.isDirectory())
        takesDir.createDirectory();

    const auto takeFile = takesDir.getChildFile("take_" + juce::String(++m_takeCounter) + ".wav");
    const auto fullPath = takeFile.getFullPathName().toStdString();

    const auto deviceRate = (m_audioBridge && m_audioBridge->sampleRate() > 0.0)
        ? static_cast<std::uint32_t>(std::llround(m_audioBridge->sampleRate()))
        : 48000;

    const auto prep = m_recordService->prepare(fullPath, deviceRate, 2, 65536, project::AudioAssetPathKind::absolute);
    if (! prep.has_value())
    {
        m_state->setMediaStatus(false, prep.error().message);
        return;
    }

    m_recordingTrackId = *armedTrack;
    m_recordStartTimelineSample = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, m_engine->transportState().samplePosition));

    m_recordService->start();
    m_engine->setRecording(true);
}

void Application::stopRecordingSession()
{
    if (! m_recordService || ! m_engine || ! m_state)
        return;

    m_engine->setRecording(false);
    auto takeResult = m_recordService->stopAndFinalize(m_recordingTrackId, m_recordStartTimelineSample);
    if (! takeResult.has_value())
        return;

    auto recordedTake = std::move(takeResult.value());
    if (recordedTake.asset.frameCount == 0)
        return;

    const auto assetId = m_state->addAudioAsset(recordedTake.asset);
    if (! assetId.has_value())
        return;

    recordedTake.clip.assetId = *assetId;
    const auto clipId = m_state->addAudioClip(recordedTake.clip);
    if (! clipId.has_value())
        return;

    if (m_diskStreamPool)
    {
        recordedTake.clip.id = *clipId;
        const auto deviceRate = (m_audioBridge && m_audioBridge->sampleRate() > 0.0)
            ? static_cast<std::uint32_t>(std::llround(m_audioBridge->sampleRate()))
            : recordedTake.asset.sampleRate;
        const auto voiceId = m_diskStreamPool->addVoice(
            recordedTake.asset, recordedTake.clip, 65536, deviceRate);
        if (voiceId.has_value())
        {
            auto* voiceBuffer = m_diskStreamPool->buffer(*voiceId);
            m_state->setClipBuffer(*clipId, voiceBuffer);
            m_diskStreamPool->start();
        }
    }

    if (m_waveformCache)
        m_waveformCache->requestThumbnail(recordedTake.asset);

    static_cast<void>(m_state->flushRenderSnapshot());
}

void Application::undo()
{
    if (m_state && m_state->undo())
        refreshMainComponent();
}

void Application::redo()
{
    if (m_state && m_state->redo())
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
