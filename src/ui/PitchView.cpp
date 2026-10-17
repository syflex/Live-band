#include "ui/PitchView.h"

#include "common/Pitch.h"

#include <algorithm>

PitchView::PitchView()
    : frames_(capacity)
{
    setOpaque(true);
}

void PitchView::addFrame(const lb::AnalysisFrame& frame)
{
    newest_ = (newest_ + 1) % capacity;
    frames_[static_cast<std::size_t>(newest_)] = frame;
    count_ = std::min(count_ + 1, capacity);

    if (frame.hasOnset())
        onsets_.push_back({ frame.onsetSampleTime, (frame.flags & lb::AnalysisFrame::pitchOnset) != 0 });
    const auto oldest = frame.sampleTime - static_cast<std::int64_t>(windowSeconds * sampleRate_);
    onsets_.erase(onsets_.begin(), std::find_if(onsets_.begin(), onsets_.end(), [oldest](const Onset& o) { return o.sampleTime >= oldest; }));

    // Let the ODF scale follow its recent peak, slowly.
    odfScale_ = std::max(frame.odf, odfScale_ * 0.999f);
}

void PitchView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1b1f24));
    auto area = getLocalBounds();
    const auto odfArea = area.removeFromBottom(36).toFloat();
    area.removeFromTop(8); // room for the top label
    const auto labels = area.removeFromLeft(34);
    const auto plot = area.toFloat();

    const auto yForMidi = [&](float midi) {
        return plot.getBottom() - (midi - lowestMidi) / (highestMidi - lowestMidi) * plot.getHeight();
    };

    // Note grid: a line per semitone, brighter on each C, labelled.
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    for (int midi = static_cast<int>(lowestMidi); midi <= static_cast<int>(highestMidi); ++midi)
    {
        const float y = yForMidi(static_cast<float>(midi));
        const bool isC = midi % 12 == 0;
        g.setColour(juce::Colours::white.withAlpha(isC ? 0.25f : 0.06f));
        g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        if (isC)
        {
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawText(juce::String(lb::noteName(midi)), labels.withY(juce::roundToInt(y) - 7).withHeight(14), juce::Justification::centredRight);
        }
    }

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawText("onset strength", odfArea.reduced(4, 0), juce::Justification::topLeft);
    g.setColour(juce::Colours::orange);
    g.drawText("| onset (spectrum)", odfArea.reduced(4, 0).translated(100.0f, 0.0f), juce::Justification::topLeft);
    g.setColour(juce::Colour(0xffce93d8));
    g.drawText("| onset (pitch)", odfArea.reduced(4, 0).translated(220.0f, 0.0f), juce::Justification::topLeft);

    if (count_ == 0)
        return;

    const auto& newest = frames_[static_cast<std::size_t>(newest_)];
    const double windowSamples = windowSeconds * sampleRate_;
    const auto xForTime = [&](std::int64_t sampleTime) {
        return plot.getRight() - static_cast<float>(static_cast<double>(newest.sampleTime - sampleTime) / windowSamples) * plot.getWidth();
    };

    // Onsets, coloured by which detector found them.
    for (const auto& onset : onsets_)
    {
        const float x = xForTime(onset.sampleTime);
        if (x < plot.getX())
            continue;
        g.setColour((onset.fromPitch ? juce::Colour(0xffce93d8) : juce::Colours::orange).withAlpha(0.7f));
        g.drawVerticalLine(juce::roundToInt(x), plot.getY(), odfArea.getBottom());
    }

    // ODF and pitch, oldest to newest.
    juce::Path odfPath;
    bool odfStarted = false;
    for (int i = count_ - 1; i >= 0; --i)
    {
        const auto& f = frames_[static_cast<std::size_t>((newest_ - i + capacity) % capacity)];
        const float x = xForTime(f.sampleTime);
        if (x < plot.getX())
            continue;

        const float odfY = odfArea.getBottom() - std::min(1.0f, f.odf / odfScale_) * (odfArea.getHeight() - 14.0f);
        if (!odfStarted)
        {
            odfPath.startNewSubPath(x, odfArea.getBottom());
            odfStarted = true;
        }
        odfPath.lineTo(x, odfY);

        if (f.isVoiced() && f.midiPitch >= lowestMidi && f.midiPitch <= highestMidi)
        {
            g.setColour(juce::Colour(0xff4fc3f7).withAlpha(0.35f + 0.65f * f.periodicity));
            g.fillEllipse(x - 2.0f, yForMidi(f.midiPitch) - 2.0f, 4.0f, 4.0f);
        }
    }
    if (odfStarted)
    {
        odfPath.lineTo(plot.getRight(), odfArea.getBottom());
        odfPath.closeSubPath();
        g.setColour(juce::Colours::orange.withAlpha(0.35f));
        g.fillPath(odfPath);
    }
}
