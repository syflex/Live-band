#include "ui/BeatLamps.h"

#include <algorithm>
#include <cmath>

void BeatLamps::setBeatsPerBar(int beats)
{
    if (beats > 0 && beats != beatsPerBar_)
    {
        beatsPerBar_ = beats;
        repaint();
    }
}

void BeatLamps::setBeat(const lb::BeatTick* tick, double secondsSinceBeat)
{
    const bool active = tick != nullptr;
    const float flash = active ? static_cast<float>(std::exp(-secondsSinceBeat / 0.12)) : 0.0f;
    if (!active && !active_)
        return;
    active_ = active;
    if (active)
        tick_ = *tick;
    flash_ = flash;
    repaint();
}

void BeatLamps::setStatus(const juce::String& text)
{
    if (text != status_)
    {
        status_ = text;
        repaint();
    }
}

void BeatLamps::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    const int size = std::min(area.getHeight() - 6, 22);
    auto lamps = area.removeFromLeft(beatsPerBar_ * (size + 10));

    juce::Colour colour = juce::Colour(0xff66bb6a);
    if (active_ && (tick_.flags & lb::BeatTick::countIn) != 0)
        colour = juce::Colours::lightgrey;
    else if (active_ && (tick_.flags & lb::BeatTick::fill) != 0)
        colour = juce::Colour(0xffffa726);
    else if (active_ && (tick_.flags & lb::BeatTick::ending) != 0)
        colour = juce::Colour(0xffef5350);

    for (int i = 0; i < beatsPerBar_; ++i)
    {
        const auto box = lamps.removeFromLeft(size + 10).withSizeKeepingCentre(size, size).toFloat();
        const bool lit = active_ && tick_.beat == i;
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillEllipse(box);
        if (lit)
        {
            g.setColour(colour.withAlpha(0.45f + 0.55f * flash_));
            g.fillEllipse(box);
        }
        g.setColour(juce::Colours::white.withAlpha(lit ? 0.9f : 0.3f));
        g.drawEllipse(box, i == 0 ? 2.0f : 1.0f);
    }

    area.removeFromLeft(8);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(15.0f));
    g.drawText(status_, area, juce::Justification::centredLeft, true);
}
