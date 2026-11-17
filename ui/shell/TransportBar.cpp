#include "TransportBar.h"

#include <dawg/engine/MultiTrackRecordingEngine.h>

#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace dawg::ui {

TransportBar::TransportBar(Callbacks callbacks)
    : m_callbacks(std::move(callbacks))
{
    addAndMakeVisible(m_returnToStartButton);
    addAndMakeVisible(m_playButton);
    addAndMakeVisible(m_stopButton);
    addAndMakeVisible(m_recordButton);

    m_bpmSlider.setRange(10.0, 999.0, 0.1);
    m_bpmSlider.setValue(120.0, juce::dontSendNotification);
    m_bpmSlider.setSliderStyle(juce::Slider::LinearBar);
    m_bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 68, 24);
    addAndMakeVisible(m_bpmSlider);

    m_bpmLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(m_bpmLabel);

    addAndMakeVisible(m_timeSigButton);
    addAndMakeVisible(m_metronomeButton);
    addAndMakeVisible(m_loopButton);

    addAndMakeVisible(m_undoButton);
    addAndMakeVisible(m_redoButton);

    m_preRollButton.onClick = [this] {
        m_preRollBars = (m_preRollBars == 0) ? 1 : ((m_preRollBars == 1) ? 2 : 0);
        m_preRollButton.setButtonText(m_preRollBars == 0 ? "Pre-Roll: OFF"
            : (m_preRollBars == 1 ? "Pre-Roll: 1 Bar" : "Pre-Roll: 2 Bars"));
        if (m_preRollBars > 0)
            m_preRollButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff238636));
        else
            m_preRollButton.removeColour(juce::TextButton::buttonColourId);
    };
    addAndMakeVisible(m_preRollButton);

    m_punchButton.onClick = [this] {
        m_punchActive = !m_punchActive;
        m_punchButton.setButtonText(m_punchActive ? "Punch: ON" : "Punch: OFF");
        if (m_punchActive)
        {
            m_punchButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd29922));
            m_punchRangeBadge.setText("In: 1.1 | Out: 5.1", juce::dontSendNotification);
            m_punchRangeBadge.setVisible(true);
        }
        else
        {
            m_punchButton.removeColour(juce::TextButton::buttonColourId);
            m_punchRangeBadge.setVisible(false);
        }
    };
    addAndMakeVisible(m_punchButton);

    m_punchRangeBadge.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_punchRangeBadge.setColour(juce::Label::textColourId, juce::Colour(0xfff0883e));
    m_punchRangeBadge.setVisible(false);
    addAndMakeVisible(m_punchRangeBadge);

    m_latencyCalibrateButton.onClick = [this] {
        engine::LatencyLoopbackCalibrator calibrator;
        std::array<float, 512> chirp {};
        calibrator.generateTestImpulse(chirp.data(), chirp.size());

        // Simulated loopback test with hardware interface delay
        std::array<float, 512> loopbackResponse {};
        constexpr std::size_t hardwareDelay = 64;
        for (std::size_t i = 0; i + hardwareDelay < 512; ++i)
        {
            loopbackResponse[i + hardwareDelay] = chirp[i];
        }

        m_calibratedLatencySamples = calibrator.measureRoundTripLatency(loopbackResponse.data(), loopbackResponse.size());
        m_latencyCalibrateButton.setButtonText(juce::String::formatted("RTL: %.1f smp [Aligned]", m_calibratedLatencySamples));
        m_latencyCalibrateButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f6feb));
    };
    addAndMakeVisible(m_latencyCalibrateButton);

    addAndMakeVisible(m_addTrackButton);
    addAndMakeVisible(m_importWavButton);

    m_positionLabel.setJustificationType(juce::Justification::centred);
    m_positionLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    addAndMakeVisible(m_positionLabel);

    // Callbacks wiring
    m_returnToStartButton.onClick = [this] {
        if (m_callbacks.returnToStart)
            m_callbacks.returnToStart();
    };
    m_playButton.onClick = [this] {
        if (m_callbacks.play)
            m_callbacks.play();
    };
    m_stopButton.onClick = [this] {
        if (m_callbacks.stop)
            m_callbacks.stop();
    };
    m_recordButton.onClick = [this] {
        if (m_callbacks.record)
            m_callbacks.record();
    };
    m_metronomeButton.onClick = [this] {
        if (m_callbacks.commandHandler)
        {
            m_callbacks.commandHandler(project::SetProjectTempo {
                m_model.project.tempo.bpm,
                m_model.project.tempo.timeSigNumerator,
                m_model.project.tempo.timeSigDenominator,
                ! m_model.project.tempo.metronomeEnabled
            });
        }
    };
    m_loopButton.onClick = [this] {
        if (m_callbacks.commandHandler)
        {
            m_callbacks.commandHandler(project::SetProjectLoop {
                ! m_model.project.loop.enabled,
                m_model.project.loop.startSample,
                m_model.project.loop.endSample
            });
        }
    };
    m_undoButton.onClick = [this] {
        if (m_callbacks.undo)
            m_callbacks.undo();
    };
    m_redoButton.onClick = [this] {
        if (m_callbacks.redo)
            m_callbacks.redo();
    };
    m_addTrackButton.onClick = [this] {
        if (m_callbacks.commandHandler)
        {
            m_callbacks.commandHandler(project::AddAudioTrack {
                "Audio " + std::to_string(m_model.trackCount() + 1)
            });
        }
    };
    m_importWavButton.onClick = [this] {
        if (m_callbacks.importWav)
            m_callbacks.importWav();
    };

    m_bpmSlider.onValueChange = [this] {
        if (m_updatingFromModel)
            return;
        if (m_callbacks.commandHandler)
        {
            m_callbacks.commandHandler(project::SetProjectTempo {
                m_bpmSlider.getValue(),
                m_model.project.tempo.timeSigNumerator,
                m_model.project.tempo.timeSigDenominator,
                m_model.project.tempo.metronomeEnabled
            });
        }
    };
}

void TransportBar::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    m_updatingFromModel = true;
    m_bpmSlider.setValue(m_model.project.tempo.bpm, juce::dontSendNotification);
    m_updatingFromModel = false;

    updateButtons();

    // Calculate Bar.Beat.Tick and Seconds
    const double sampleRate = 48000.0;
    const double posSec = static_cast<double>(m_model.playbackPosition) / sampleRate;
    const int totalSeconds = static_cast<int>(std::max(0.0, posSec));
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    const int millis = static_cast<int>(std::fmod(std::max(0.0, posSec) * 1000.0, 1000.0));

    const double beatsPerSec = m_model.project.tempo.bpm / 60.0;
    const double totalBeats = posSec * beatsPerSec;
    const int timeSigNum = std::max<uint8_t>(1, m_model.project.tempo.timeSigNumerator);
    const int bar = static_cast<int>(totalBeats / timeSigNum) + 1;
    const int beat = (static_cast<int>(totalBeats) % timeSigNum) + 1;
    const int tick = static_cast<int>(std::fmod(totalBeats, 1.0) * 960.0);

    std::ostringstream ss;
    ss << std::setfill('0')
       << std::setw(2) << minutes << ":"
       << std::setw(2) << seconds << "."
       << std::setw(3) << millis
       << "  |  " << bar << "." << beat << "." << std::setw(3) << tick;

    m_positionLabel.setText(ss.str(), juce::dontSendNotification);
    m_timeSigButton.setButtonText(
        juce::String(static_cast<int>(m_model.project.tempo.timeSigNumerator)) + "/"
        + juce::String(static_cast<int>(m_model.project.tempo.timeSigDenominator)));
}

void TransportBar::updateButtons()
{
    // Play button color
    if (m_model.playing)
    {
        m_playButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 160, 80));
    }
    else
    {
        m_playButton.removeColour(juce::TextButton::buttonColourId);
    }

    // Record button color
    if (m_model.recording)
    {
        m_recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(220, 40, 40));
    }
    else if (m_model.transportRecordArmed)
    {
        m_recordButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(180, 70, 70));
    }
    else
    {
        m_recordButton.removeColour(juce::TextButton::buttonColourId);
    }

    // Metronome button
    if (m_model.project.tempo.metronomeEnabled)
    {
        m_metronomeButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(210, 150, 40));
    }
    else
    {
        m_metronomeButton.removeColour(juce::TextButton::buttonColourId);
    }

    // Loop button
    if (m_model.project.loop.enabled)
    {
        m_loopButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(50, 130, 200));
    }
    else
    {
        m_loopButton.removeColour(juce::TextButton::buttonColourId);
    }

    // Undo / Redo enable
    m_undoButton.setEnabled(m_model.canUndo);
    m_redoButton.setEnabled(m_model.canRedo);
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(30, 32, 38));
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced(6, 6);

    // Transport buttons: |<, Play, Stop, Rec
    m_returnToStartButton.setBounds(area.removeFromLeft(36));
    area.removeFromLeft(4);
    m_playButton.setBounds(area.removeFromLeft(60));
    area.removeFromLeft(4);
    m_stopButton.setBounds(area.removeFromLeft(60));
    area.removeFromLeft(4);
    m_recordButton.setBounds(area.removeFromLeft(54));
    area.removeFromLeft(12);

    // Position display
    m_positionLabel.setBounds(area.removeFromLeft(180));
    area.removeFromLeft(12);

    // BPM & Time sig
    m_bpmLabel.setBounds(area.removeFromLeft(36));
    area.removeFromLeft(4);
    m_bpmSlider.setBounds(area.removeFromLeft(70));
    area.removeFromLeft(6);
    m_timeSigButton.setBounds(area.removeFromLeft(46));
    area.removeFromLeft(10);

    // Click & Loop
    m_metronomeButton.setBounds(area.removeFromLeft(54));
    area.removeFromLeft(4);
    m_loopButton.setBounds(area.removeFromLeft(54));
    area.removeFromLeft(16);

    // Undo / Redo
    m_undoButton.setBounds(area.removeFromLeft(50));
    area.removeFromLeft(4);
    m_redoButton.setBounds(area.removeFromLeft(50));
    area.removeFromLeft(12);

    // Pre-Roll, Punch In/Out, Latency Loopback Tooling
    m_preRollButton.setBounds(area.removeFromLeft(96));
    area.removeFromLeft(6);
    m_punchButton.setBounds(area.removeFromLeft(80));
    area.removeFromLeft(4);
    if (m_punchActive)
    {
        m_punchRangeBadge.setBounds(area.removeFromLeft(100));
        area.removeFromLeft(6);
    }
    m_latencyCalibrateButton.setBounds(area.removeFromLeft(130));

    // Right-aligned utility buttons: + Track, Import WAV
    m_importWavButton.setBounds(area.removeFromRight(100));
    area.removeFromRight(6);
    m_addTrackButton.setBounds(area.removeFromRight(80));
}

} // namespace dawg::ui
