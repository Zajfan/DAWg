#include "RoutingMatrixView.h"

#include <algorithm>

namespace dawg::ui {

RoutingMatrixView::RoutingMatrixView(CommandHandler handler, ReferenceToggleCallback refCb)
    : m_commandHandler(std::move(handler))
    , m_refCallback(std::move(refCb))
{
    m_destinations = { "Master Out", "Drum Bus", "Music Bus", "Aux Reverb", "Aux Delay", "Sidechain SC" };

    m_titleLabel.setText("Signal Routing Matrix & Sidechain Patchbay", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 225, 235));
    addAndMakeVisible(m_titleLabel);

    m_abRefButton.onClick = [this] { toggleReferenceMix(); };
    m_abRefButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 50, 60));
    m_abRefButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 220, 255));
    addAndMakeVisible(m_abRefButton);

    m_quickSidechainButton.onClick = [this] {
        // Quick patch Kick -> Bass Compressor Sidechain
        if (! m_model.project.tracks.empty())
        {
            MatrixRoute scRoute;
            scRoute.sourceTrackId = m_model.project.tracks.front().id;
            scRoute.destination = "Sidechain SC";
            scRoute.isSidechain = true;
            scRoute.active = true;
            m_routes.push_back(scRoute);
            repaint();
        }
    };
    m_quickSidechainButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(75, 45, 110));
    addAndMakeVisible(m_quickSidechainButton);
}

void RoutingMatrixView::setViewModel(const EditorViewModel& model)
{
    m_model = model;

    // Ensure default master routes exist for tracks
    for (const auto& track : m_model.project.tracks)
    {
        bool found = false;
        for (const auto& r : m_routes)
        {
            if (r.sourceTrackId == track.id && r.destination == "Master Out")
            {
                found = true;
                break;
            }
        }
        if (! found)
        {
            m_routes.push_back(MatrixRoute {
                .sourceTrackId = track.id,
                .destination = "Master Out",
                .sendGain = 1.0f,
                .isSidechain = false,
                .active = true
            });
        }
    }
    repaint();
}

void RoutingMatrixView::toggleReferenceMix()
{
    m_referenceActive = ! m_referenceActive;
    if (m_referenceActive)
    {
        m_abRefButton.setButtonText("[ REF MIX A/B: REFERENCE ]");
        m_abRefButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(200, 140, 30));
        m_abRefButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(255, 255, 255));
    }
    else
    {
        m_abRefButton.setButtonText("[ REF MIX A/B: NORMAL ]");
        m_abRefButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 50, 60));
        m_abRefButton.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 220, 255));
    }

    if (m_refCallback)
        m_refCallback(m_referenceActive);

    repaint();
}

void RoutingMatrixView::mouseDown(const juce::MouseEvent& e)
{
    const int startY = m_headerHeight + 24; // table start
    if (e.getPosition().getY() < startY || e.getPosition().getX() < m_labelColWidth)
        return;

    const int trackIdx = (e.getPosition().getY() - startY) / m_rowHeight;
    const int destIdx = (e.getPosition().getX() - m_labelColWidth) / m_colWidth;

    if (trackIdx < 0 || static_cast<std::size_t>(trackIdx) >= m_model.project.tracks.size() ||
        destIdx < 0 || static_cast<std::size_t>(destIdx) >= m_destinations.size())
        return;

    const auto trackId = m_model.project.tracks[static_cast<std::size_t>(trackIdx)].id;
    const auto& dest = m_destinations[static_cast<std::size_t>(destIdx)];

    // Toggle route
    auto it = std::find_if(m_routes.begin(), m_routes.end(), [trackId, &dest](const MatrixRoute& r) {
        return r.sourceTrackId == trackId && r.destination == dest;
    });

    if (it != m_routes.end())
    {
        it->active = ! it->active;
    }
    else
    {
        m_routes.push_back(MatrixRoute {
            .sourceTrackId = trackId,
            .destination = dest,
            .sendGain = 1.0f,
            .isSidechain = (dest == "Sidechain SC"),
            .active = true
        });
    }
    repaint();
}

void RoutingMatrixView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(30, 33, 38));

    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromTop(m_headerHeight);
    drawHeader(g, headerBounds);

    drawGrid(g, bounds);
}

void RoutingMatrixView::drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(24, 26, 30));
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromRGB(48, 52, 60));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));
}

void RoutingMatrixView::drawGrid(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Draw Destination Column Headers
    const int colHeaderH = 24;
    auto colHeaderBounds = bounds.removeFromTop(colHeaderH);
    g.setColour(juce::Colour::fromRGB(28, 30, 35));
    g.fillRect(colHeaderBounds);

    g.setColour(juce::Colour::fromRGB(150, 155, 168));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("SOURCE TRACKS", juce::Rectangle<int>(8, colHeaderBounds.getY(), m_labelColWidth - 8, colHeaderH), juce::Justification::centredLeft);

    for (std::size_t c = 0; c < m_destinations.size(); ++c)
    {
        const int colX = m_labelColWidth + static_cast<int>(c) * m_colWidth;
        const auto headerRect = juce::Rectangle<int>(colX, colHeaderBounds.getY(), m_colWidth, colHeaderH);
        g.setColour(c == 0 ? juce::Colour::fromRGB(100, 180, 240) : (c == 5 ? juce::Colour::fromRGB(220, 130, 255) : juce::Colour::fromRGB(180, 185, 195)));
        g.drawText(m_destinations[c], headerRect, juce::Justification::centred);
        g.setColour(juce::Colour::fromRGB(45, 48, 55));
        g.drawVerticalLine(colX, static_cast<float>(colHeaderBounds.getY()), static_cast<float>(getHeight()));
    }

    // Draw Tracks (Rows)
    for (std::size_t t = 0; t < m_model.project.tracks.size(); ++t)
    {
        const auto& track = m_model.project.tracks[t];
        const int rowY = bounds.getY() + static_cast<int>(t) * m_rowHeight;
        if (rowY >= getHeight())
            break;

        const auto rowBounds = juce::Rectangle<int>(0, rowY, getWidth(), m_rowHeight);
        g.setColour((t % 2 == 0) ? juce::Colour::fromRGB(34, 37, 43) : juce::Colour::fromRGB(30, 33, 38));
        g.fillRect(rowBounds);

        // Track Name label
        g.setColour(juce::Colour::fromRGB(225, 230, 240));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(track.name.empty() ? ("Track " + std::to_string(track.id)) : track.name,
                   juce::Rectangle<int>(12, rowY, m_labelColWidth - 16, m_rowHeight), juce::Justification::centredLeft);

        // Destination Nodes / Patch Points
        for (std::size_t c = 0; c < m_destinations.size(); ++c)
        {
            const auto& dest = m_destinations[c];
            const int colX = m_labelColWidth + static_cast<int>(c) * m_colWidth;
            const auto nodeRect = juce::Rectangle<float>(static_cast<float>(colX + m_colWidth / 2 - 8),
                                                         static_cast<float>(rowY + m_rowHeight / 2 - 8), 16.0f, 16.0f);

            bool isPatched = false;
            for (const auto& r : m_routes)
            {
                if (r.sourceTrackId == track.id && r.destination == dest && r.active)
                {
                    isPatched = true;
                    break;
                }
            }

            if (isPatched)
            {
                if (c == 5) // Sidechain
                {
                    g.setColour(juce::Colour::fromRGB(190, 70, 255));
                    g.fillEllipse(nodeRect);
                    g.setColour(juce::Colour::fromRGB(255, 220, 255));
                    g.fillEllipse(nodeRect.reduced(3.5f));
                }
                else if (c == 0) // Master
                {
                    g.setColour(juce::Colour::fromRGB(60, 160, 240));
                    g.fillEllipse(nodeRect);
                    g.setColour(juce::Colour::fromRGB(210, 240, 255));
                    g.fillEllipse(nodeRect.reduced(3.5f));
                }
                else // Bus / Aux
                {
                    g.setColour(juce::Colour::fromRGB(65, 210, 120));
                    g.fillEllipse(nodeRect);
                    g.setColour(juce::Colour::fromRGB(210, 255, 220));
                    g.fillEllipse(nodeRect.reduced(3.5f));
                }
            }
            else
            {
                g.setColour(juce::Colour::fromRGB(45, 49, 56));
                g.fillEllipse(nodeRect);
                g.setColour(juce::Colour::fromRGB(25, 28, 33));
                g.drawEllipse(nodeRect, 1.0f);
            }
        }
    }
}

void RoutingMatrixView::resized()
{
    m_titleLabel.setBounds(12, 5, 290, 26);
    m_abRefButton.setBounds(getWidth() - 210, 5, 198, 26);
    m_quickSidechainButton.setBounds(getWidth() - 390, 5, 170, 26);
}

} // namespace dawg::ui
