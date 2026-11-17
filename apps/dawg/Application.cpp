#include "Application.h"

#include "ApplicationState.h"
#include "AudioPoolDialog.h"
#include "AudioSettingsDialog.h"
#include "BounceDialog.h"
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
#include <dawg/media/OfflineRenderService.h>
#include <dawg/media/WaveformCache.h>
#include <dawg/persistence/AudioPoolManager.h>
#include <dawg/persistence/ProjectFile.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <thread>
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
    m_controller = std::make_unique<commands::ProjectController>(createStudioStarterProject());
    m_state = std::make_unique<ApplicationState>(*m_controller, *m_engine);
    m_audioBridge = std::make_unique<platform::juce::JuceAudioDeviceBridge>(*m_engine);
    m_mediaService = std::make_unique<media::AudioMediaService>(*m_assetReaderFactory);
    m_diskStreamPool = std::make_unique<media::DiskStreamPool>(*m_assetReaderFactory, 2);
    m_recordService = std::make_unique<media::AudioRecordService>(*m_assetWriterFactory);
    m_waveformCache = std::make_unique<media::WaveformCache>(*m_assetReaderFactory, 2);
    m_renderService = std::make_unique<media::OfflineRenderService>(*m_assetWriterFactory, m_assetReaderFactory.get());
    m_engine->setRecordBuffer(&m_recordService->buffer());
    m_engine->setMasterLimiterEnabled(true);
    m_audioPoolManager = std::make_unique<persistence::AudioPoolManager>();
    m_windowManager = std::make_unique<platform::juce::window::WindowManager>();
    restartAudio();

    m_audioBridge->setMidiControlCallback([this](const engine::ProcessedMidiControl& ctrl) {
        if (ctrl.mcuEvent.type == engine::McuType::transport)
        {
            switch (ctrl.mcuEvent.transport)
            {
            case engine::McuTransport::play:
                if (ctrl.mcuEvent.buttonState) playTransport();
                break;
            case engine::McuTransport::stop:
                if (ctrl.mcuEvent.buttonState) stopTransport();
                break;
            case engine::McuTransport::record:
                if (ctrl.mcuEvent.buttonState) toggleTransportRecord();
                break;
            case engine::McuTransport::rewind:
                if (ctrl.mcuEvent.buttonState) returnTransportToStart();
                break;
            case engine::McuTransport::fastForward:
                break;
            case engine::McuTransport::cycleLoop:
                if (ctrl.mcuEvent.buttonState && m_controller)
                {
                    const auto snap = m_controller->snapshot();
                    dispatchProjectCommand(project::SetProjectLoop {
                        ! snap.loop.enabled, snap.loop.startSample, snap.loop.endSample
                    });
                }
                break;
            case engine::McuTransport::none:
                break;
            }
        }
        else if (ctrl.mcuEvent.type == engine::McuType::fader)
        {
            if (m_controller && ctrl.mcuEvent.trackIndex < 8)
            {
                const auto snap = m_controller->snapshot();
                if (ctrl.mcuEvent.trackIndex < snap.tracks.size())
                {
                    const auto& trk = snap.tracks[ctrl.mcuEvent.trackIndex];
                    dispatchProjectCommand(project::SetTrackGainPan {
                        trk.id, ctrl.mcuEvent.value * 2.0f, trk.pan, trk.phaseInvert
                    });
                }
            }
        }
    });

    m_audioBridge->setMidiNoteCallback([this](const std::uint8_t pitch, const std::uint8_t velocity, const bool isNoteOn) {
        if (m_state && m_engine && m_engine->isRecording())
        {
            handleIncomingLiveMidiNote(pitch, velocity, isNoteOn);
        }
    });

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
        },
        .audition = [this](const std::uint8_t pitch, const std::uint8_t velocity, const bool isNoteOn) {
            if (m_audioBridge)
                m_audioBridge->triggerAuditionNote(pitch, velocity, isNoteOn);
        },
        .importSample = [this](const std::filesystem::path& path) {
            importWavFile(juce::File(juce::String(path.string())));
        }
    };

    ui::MainEditorView::MenuCallbacks menuCallbacks {
        .newProject = [this] { newProject(); },
        .openProject = [this] { promptOpenProject(); },
        .saveProject = [this] { saveProject(false); },
        .saveProjectAs = [this] { saveProject(true); },
        .exportBounce = [this] { promptExportBounce(false); },
        .exportStems = [this] { promptExportBounce(true); },
        .showAudioPool = [this] { openAudioPool(); },
        .audioPreferences = [this] { openAudioPreferences(); },
        .showShortcuts = [this] { showKeyboardShortcuts(); },
        .showAbout = [this] { showAboutDialog(); }
    };

    auto content = std::make_unique<ui::MainComponent>(
        m_state->editorViewModel(),
        [this](project::ProjectCommand command) { dispatchProjectCommand(std::move(command)); },
        std::move(transportCallbacks),
        std::move(menuCallbacks),
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
    m_renderService.reset();
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

    // Finalize any recorded live MIDI notes into a MidiClip on the armed track
    if (! m_recordedMidiNotes.empty())
    {
        const auto armedTrack = m_recordingTrackId != 0 ? m_recordingTrackId : (m_state->firstArmedTrackId().value_or(1));
        const auto curTimelineSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_engine->transportState().samplePosition));
        const auto clipLen = std::max<std::uint64_t>(48000, curTimelineSample > m_recordStartTimelineSample ? curTimelineSample - m_recordStartTimelineSample : 48000);

        project::MidiClip recordedClip {
            .id = 0,
            .trackId = armedTrack,
            .startSample = m_recordStartTimelineSample,
            .lengthSamples = clipLen,
            .notes = std::move(m_recordedMidiNotes)
        };
        m_recordedMidiNotes.clear();
        dispatchProjectCommand(project::AddMidiClip { std::move(recordedClip) });
    }

    static_cast<void>(m_state->flushRenderSnapshot());
}

void Application::handleIncomingLiveMidiNote(const std::uint8_t pitch, const std::uint8_t velocity, const bool isNoteOn)
{
    if (pitch >= 128 || ! m_state)
        return;

    const auto curTimeline = static_cast<std::uint64_t>(
        std::max<std::int64_t>(0, m_engine ? m_engine->transportState().samplePosition : 0));

    if (isNoteOn)
    {
        m_activeMidiNotes[pitch] = {
            .startTimelineSample = curTimeline,
            .velocity = velocity,
            .active = true
        };
    }
    else
    {
        auto& note = m_activeMidiNotes[pitch];
        if (note.active)
        {
            const auto noteStart = (note.startTimelineSample >= m_recordStartTimelineSample)
                ? (note.startTimelineSample - m_recordStartTimelineSample)
                : 0;
            const auto noteLen = std::max<std::uint64_t>(
                2400, (curTimeline > note.startTimelineSample) ? (curTimeline - note.startTimelineSample) : 2400);

            m_recordedMidiNotes.push_back(project::MidiNote {
                .id = static_cast<project::NoteId>(m_recordedMidiNotes.size() + 1),
                .pitch = pitch,
                .velocity = note.velocity,
                .startSample = noteStart,
                .lengthSamples = noteLen
            });
            note.active = false;
        }
    }
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

void Application::newProject()
{
    m_currentProjectFilePath.clear();
    if (m_diskStreamPool)
        m_diskStreamPool->stop();
    if (m_state)
        m_state->clearClipBuffers();

    m_controller = std::make_unique<commands::ProjectController>(createStudioStarterProject());
    m_state = std::make_unique<ApplicationState>(*m_controller, *m_engine);
    static_cast<void>(m_state->flushRenderSnapshot());
    refreshMainComponent();
}

void Application::promptOpenProject()
{
    m_fileChooser = std::make_unique<juce::FileChooser>(
        "Open DAWg Project", juce::File {}, "*.dawg");
    m_fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto selected = chooser.getResult();
            if (selected.existsAsFile())
                openProjectFile(selected);
        });
}

void Application::openProjectFile(const juce::File& file)
{
    const std::filesystem::path stdPath(file.getFullPathName().toStdString());
    auto loaded = persistence::loadProject(stdPath);
    if (! loaded.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Error Opening Project",
            "Failed to load project: " + juce::String(loaded.error().message));
        return;
    }

    auto projOpt = project::Project::fromSnapshot(std::move(loaded.value()));
    if (! projOpt.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Error Opening Project",
            "Project file structure is invalid or unsupported.");
        return;
    }

    m_currentProjectFilePath = file.getFullPathName().toStdString();
    if (m_diskStreamPool)
        m_diskStreamPool->stop();
    if (m_state)
        m_state->clearClipBuffers();

    m_controller = std::make_unique<commands::ProjectController>(std::move(*projOpt));
    m_state = std::make_unique<ApplicationState>(*m_controller, *m_engine);

    const auto snap = m_controller->snapshot();
    const auto blockSize = m_audioBridge ? std::max(1, m_audioBridge->blockSize()) : 512;
    const auto ringCapacityFrames = std::max<std::size_t>(4096u, static_cast<std::size_t>(blockSize) * 4u);
    const auto deviceRate = (m_audioBridge && m_audioBridge->sampleRate() > 0.0)
        ? static_cast<std::uint32_t>(std::llround(m_audioBridge->sampleRate()))
        : 48000;

    for (const auto& clip : snap.clips)
    {
        const auto assetIt = std::find_if(snap.assets.begin(), snap.assets.end(),
            [&](const project::AudioAsset& a) { return a.id == clip.assetId; });
        if (assetIt != snap.assets.end() && m_diskStreamPool)
        {
            auto voiceId = m_diskStreamPool->addVoice(*assetIt, clip, ringCapacityFrames, deviceRate);
            if (voiceId.has_value())
            {
                auto* voiceBuffer = m_diskStreamPool->buffer(*voiceId);
                m_state->setClipBuffer(clip.id, voiceBuffer);
            }
            if (m_waveformCache)
                m_waveformCache->requestThumbnail(*assetIt);
        }
    }
    if (m_diskStreamPool)
        m_diskStreamPool->start();

    static_cast<void>(m_state->flushRenderSnapshot());
    refreshMainComponent();
}

void Application::saveProject(bool promptSaveAs)
{
    if (! m_controller)
        return;

    if (m_currentProjectFilePath.empty() || promptSaveAs)
    {
        m_fileChooser = std::make_unique<juce::FileChooser>(
            "Save DAWg Project", juce::File {}, "*.dawg");
        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& chooser) {
                const auto selected = chooser.getResult();
                if (selected != juce::File {})
                {
                    auto path = selected.getFullPathName();
                    if (! path.endsWithIgnoreCase(".dawg"))
                        path += ".dawg";
                    m_currentProjectFilePath = path.toStdString();
                    saveProject(false);
                }
            });
        return;
    }

    const auto snap = m_controller->snapshot();
    const std::filesystem::path stdPath(m_currentProjectFilePath);
    auto result = persistence::saveProject(snap, stdPath);
    if (! result.has_value())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Error Saving Project",
            "Failed to save project: " + juce::String(result.error().message));
    }
}

void Application::promptExportBounce(bool stemsOnly)
{
    if (! m_controller || ! m_renderService)
        return;

    const auto snap = m_controller->snapshot();
    ui::BounceWindow::open(snap, [this, stemsOnly](
        media::RenderOptions options,
        std::function<void(float, const std::string&)> onProgress,
        std::atomic<bool>* cancelToken,
        std::function<void(bool, const std::string&)> onComplete)
    {
        if (stemsOnly)
            options.exportStems = true;

        std::thread([this, options, onProgress = std::move(onProgress), cancelToken, onComplete = std::move(onComplete)]() mutable {
            const auto currentSnap = m_controller ? m_controller->snapshot() : project::ProjectSnapshot {};
            auto result = m_renderService->render(
                currentSnap, options, cancelToken,
                [onProgress](const media::RenderProgress& p) {
                    if (onProgress)
                    {
                        const auto pct = std::llround(p.fraction * 100.0f);
                        onProgress(p.fraction, "Rendering: " + std::to_string(pct) + "% ("
                            + std::to_string(p.framesRendered) + " / " + std::to_string(p.totalFrames) + " frames)");
                    }
                });

            const bool ok = result.has_value();
            std::string errMsg = ok ? "" : result.error().message;

            juce::MessageManager::callAsync([onComplete = std::move(onComplete), ok, errMsg = std::move(errMsg)] {
                if (onComplete)
                    onComplete(ok, errMsg);
            });
        }).detach();
    });
}

void Application::openAudioPreferences()
{
    if (m_audioBridge)
        ui::AudioSettingsWindow::open(m_audioBridge->deviceManager());
}

void Application::openAudioPool()
{
    if (m_audioPoolManager && m_controller)
    {
        const std::filesystem::path projectDir = m_currentProjectFilePath.empty()
            ? std::filesystem::current_path()
            : std::filesystem::path(m_currentProjectFilePath).parent_path();

        m_audioPoolManager->scanProject(m_controller->snapshot(), projectDir);

        ui::AudioPoolWindow::open(*m_audioPoolManager, [this] {
            refreshMainComponent();
        });
    }
}

void Application::showKeyboardShortcuts()
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "DAWg Keyboard Shortcuts",
        "Space: Play / Stop Transport\n"
        "Return: Return to Timeline Start\n"
        "R: Toggle Record Arm\n"
        "M: Toggle Mixer Console Dock\n"
        "P: Toggle Piano Roll Dock\n"
        "A: Toggle Automation Lanes\n"
        "S: Split Clip at Playhead\n"
        "Del: Delete Selected Item\n"
        "Ctrl+Z: Undo\n"
        "Ctrl+Y: Redo\n"
        "Ctrl+T: Add Audio Track\n"
        "Ctrl+Shift+T: Add Synth Track\n"
        "Ctrl+N: New Project\n"
        "Ctrl+O: Open Project\n"
        "Ctrl+S: Save Project\n"
        "Ctrl+B: Export Master Bounce");
}

void Application::showAboutDialog()
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon,
        "About DAWg",
        "DAWg \xe2\x80\x94 Digital Audio Workstation\n"
        "Version 0.7.0 (Milestone 0.7.0)\n\n"
        "Developed by The No-hands Company\n"
        "Engineered to the Zajfan standard: Lock-Free Audio Engine,\n"
        "Automatic PDC, High-End Studio DSP Suite & CLAP Sandboxing.");
}

project::Project Application::createStudioStarterProject()
{
    auto proj = project::Project::create("Studio Starter", 1);
    const auto audioTrk = proj.addAudioTrack("Audio 1");
    const auto synthTrk = proj.addInstrumentTrack("Synth Lead");
    const auto drumBusTrk = proj.addSubmixGroupTrack("Drum Bus");
    const auto reverbTrk = proj.addAuxReturnTrack("Plate Reverb");

    (void)drumBusTrk;

    if (audioTrk != 0 && reverbTrk != 0)
    {
        static_cast<void>(proj.addTrackSend(audioTrk, project::TrackSend {
            .targetTrackId = reverbTrk,
            .levelLinear = 0.5f,
            .preFader = false
        }));
    }

    if (synthTrk != 0 && reverbTrk != 0)
    {
        static_cast<void>(proj.addTrackSend(synthTrk, project::TrackSend {
            .targetTrackId = reverbTrk,
            .levelLinear = 0.35f,
            .preFader = false
        }));
    }

    if (synthTrk != 0)
    {
        project::MidiClip starterClip {
            .id = 0,
            .trackId = synthTrk,
            .startSample = 0,
            .lengthSamples = 384000,
            .notes = {
                { .id = 1, .pitch = 60, .velocity = 100, .startSample = 0,      .lengthSamples = 22000 },
                { .id = 2, .pitch = 64, .velocity = 95,  .startSample = 24000,  .lengthSamples = 22000 },
                { .id = 3, .pitch = 67, .velocity = 105, .startSample = 48000,  .lengthSamples = 22000 },
                { .id = 4, .pitch = 71, .velocity = 100, .startSample = 72000,  .lengthSamples = 22000 },
                { .id = 5, .pitch = 72, .velocity = 110, .startSample = 96000,  .lengthSamples = 44000 },
                { .id = 6, .pitch = 67, .velocity = 90,  .startSample = 144000, .lengthSamples = 44000 },
                { .id = 7, .pitch = 65, .velocity = 95,  .startSample = 192000, .lengthSamples = 44000 },
                { .id = 8, .pitch = 60, .velocity = 100, .startSample = 288000, .lengthSamples = 90000 }
            }
        };
        static_cast<void>(proj.addMidiClip(std::move(starterClip)));
    }

    static_cast<void>(proj.setProjectLoop(true, 0, 384000));

    return proj;
}

} // namespace dawg::app
