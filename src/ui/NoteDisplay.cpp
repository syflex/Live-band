#include "ui/NoteDisplay.h"

#include "common/Pitch.h"

#include <cmath>

void NoteDisplay::addFrame(const lb::AnalysisFrame& frame)
{
    if (frame.isVoiced() && frame.midiPitch > 0.0f)
    {
        midi_ = frame.midiPitch;
        frequencyHz_ = frame.f0Hz;
        periodicity_ = frame.periodicity;
        framesSinceVoiced_ = 0;
    }
    else if (framesSinceVoiced_ < holdFrames)
    {
        ++framesSinceVoiced_;
    }
}

void NoteDisplay::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().reduced(8);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    const bool showing = framesSinceVoiced_ < holdFrames;
    const int nearest = static_cast<int>(std::lround(midi_));
    const float cents = (midi_ - static_cast<float>(nearest)) * 100.0f;

    // Note name.
    g.setColour(showing ? juce::Colours::white : juce::Colours::grey);
    g.setFont(juce::Font(juce::FontOptions(52.0f, juce::Font::bold)));
    g.drawText(showing ? juce::String(lb::noteName(nearest)) : juce::String("-"), area.removeFromTop(64), juce::Justification::centred);

    // Cents meter: -50 to +50, green near the centre.
    auto meter = area.removeFromTop(26).reduced(10, 6);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRect(meter);
    g.setColour(juce::Colours::grey);
    g.drawVerticalLine(meter.getCentreX(), static_cast<float>(meter.getY()), static_cast<float>(meter.getBottom()));
    if (showing)
    {
        const float x = static_cast<float>(meter.getCentreX()) + cents / 50.0f * static_cast<float>(meter.getWidth()) / 2.0f;
        g.setColour(std::abs(cents) < 10.0f ? juce::Colours::limegreen : std::abs(cents) < 25.0f ? juce::Colours::yellow : juce::Colours::orange);
        g.fillRect(juce::Rectangle<float>(x - 2.0f, static_cast<float>(meter.getY()) - 3.0f, 4.0f, static_cast<float>(meter.getHeight()) + 6.0f));
    }

    // Details.
    g.setColour(juce::Colours::white.withAlpha(showing ? 0.9f : 0.5f));
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    const int wholeCents = juce::roundToInt(cents);
    const auto details = showing ? juce::String(wholeCents >= 0 ? "+" : "") + juce::String(wholeCents) + " cents   " + juce::String(frequencyHz_, 1)
                                       + " Hz   clarity " + juce::String(juce::roundToInt(periodicity_ * 100.0f)) + "%"
                                 : juce::String("listening...");
    g.drawText(details, area.removeFromTop(22), juce::Justification::centred);
}
