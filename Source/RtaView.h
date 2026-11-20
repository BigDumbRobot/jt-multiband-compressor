#pragma once

#include "SpectrumAnalyzer.h"
#include "Theme.h"

// Real-time analyzer drawn behind the EQ curves: input spectrum (dim) and
// output spectrum (bright) with smooth fall-back ballistics.
class RtaView
{
public:
    enum Mode { Off = 0, In, Out, Both };
    static constexpr const char* modeNames[] = { "RTA OFF", "RTA IN", "RTA OUT", "RTA IN + OUT" };

    // Display slope in dB per octave around 1 kHz, so a typical mix reads roughly flat.
    static constexpr float kTiltDbPerOct = 4.5f;
    static constexpr float kTopDb = 6.0f, kBottomDb = -84.0f;

    RtaView()
    {
        pre.fill (-150.0f);
        post.fill (-150.0f);
    }

    // Call once per editor frame.
    void update (const dynamo::SpectrumAnalyzer& in, const dynamo::SpectrumAnalyzer& out, float frameSeconds)
    {
        const float fall = 24.0f * frameSeconds; // dB per frame
        for (int i = 0; i < dynamo::SpectrumAnalyzer::kDisplayPoints; ++i)
        {
            const float tilt = kTiltDbPerOct * std::log2 (dynamo::SpectrumAnalyzer::displayFrequency (i) / 1000.0f);
            step (pre[(size_t) i], in.getDisplayDb (i) + tilt, fall);
            step (post[(size_t) i], out.getDisplayDb (i) + tilt, fall);
        }
    }

    void paint (juce::Graphics& g, juce::Rectangle<float> plot, const std::function<float (float)>& freqToX, int mode) const
    {
        using namespace theme;
        if (mode == Off) return;
        if (mode == In || mode == Both)
        {
            const auto p = makePath (pre, plot, freqToX, true);
            g.setGradientFill (juce::ColourGradient (lavender.withAlpha (mode == Both ? 0.22f : 0.35f), 0, plot.getY(),
                                                     lavender.withAlpha (0.03f), 0, plot.getBottom(), false));
            g.fillPath (p);
            g.setColour (hotPink.withAlpha (mode == Both ? 0.35f : 0.6f));
            g.strokePath (makePath (pre, plot, freqToX, false), juce::PathStrokeType (1.0f));
        }
        if (mode == Out || mode == Both)
        {
            g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.32f), 0, plot.getY(), cyan.withAlpha (0.02f), 0, plot.getBottom(), false));
            g.fillPath (makePath (post, plot, freqToX, true));
            const auto line = makePath (post, plot, freqToX, false);
            g.setColour (cyan.withAlpha (0.18f));
            g.strokePath (line, juce::PathStrokeType (4.0f));
            g.setColour (cyan.withAlpha (0.85f));
            g.strokePath (line, juce::PathStrokeType (1.3f));
        }
    }

    // Small clickable mode label; returns its bounds for hit testing.
    static juce::Rectangle<float> labelBounds (juce::Rectangle<float> plot)
    {
        return { plot.getRight() - 128.0f, plot.getY() + 6.0f, 120.0f, 20.0f };
    }

    static void paintLabel (juce::Graphics& g, juce::Rectangle<float> plot, int mode)
    {
        using namespace theme;
        const auto r = labelBounds (plot);
        g.setColour (night.withAlpha (0.7f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (mode == Off ? dimText : cyan);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setFont (font (11.0f, true));
        g.drawText (modeNames[juce::jlimit (0, 3, mode)], r, juce::Justification::centred);
    }

private:
    static void step (float& v, float target, float fall)
    {
        v = target > v ? v + 0.6f * (target - v) : std::max (target, v - fall);
    }

    static float toY (float db, juce::Rectangle<float> plot)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - kBottomDb) / (kTopDb - kBottomDb));
        return plot.getBottom() - t * plot.getHeight();
    }

    juce::Path makePath (const std::array<float, dynamo::SpectrumAnalyzer::kDisplayPoints>& d, juce::Rectangle<float> plot,
                         const std::function<float (float)>& freqToX, bool closed) const
    {
        juce::Path p;
        for (int i = 0; i < dynamo::SpectrumAnalyzer::kDisplayPoints; ++i)
        {
            const float x = freqToX (dynamo::SpectrumAnalyzer::displayFrequency (i));
            const float y = toY (d[(size_t) i], plot);
            if (i == 0)
            {
                if (closed) { p.startNewSubPath (x, plot.getBottom()); p.lineTo (x, y); }
                else p.startNewSubPath (x, y);
            }
            else p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (freqToX (dynamo::SpectrumAnalyzer::kDisplayMaxHz), plot.getBottom());
            p.closeSubPath();
        }
        return p;
    }

    std::array<float, dynamo::SpectrumAnalyzer::kDisplayPoints> pre {}, post {};
};
