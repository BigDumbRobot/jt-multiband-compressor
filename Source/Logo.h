#pragma once

#include "Theme.h"

namespace theme
{
// The Big Dumb Robot logo in transformer armour: the same curious, tilted head as
// the BDR logo (Theme.h drawRobotIcon), refitted as a transforming robot: a chamfered
// faceplate with a centre crest, angled antenna horns, a glowing visor and a vented mouth
// grille. Drawn at any size; also rendered to assets/icons/plugin-icon.png.
//   withTile: the rounded-square app-icon tile and BIG DUMB ROBOT under the head.
inline void drawTransformerBot (juce::Graphics& g, juce::Rectangle<float> area, bool withTile = true)
{
    juce::Graphics::ScopedSaveState save (g);
    const float size = std::min (area.getWidth(), area.getHeight());
    g.addTransform (juce::AffineTransform::translation (area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f));
    const float u = size / 1024.0f;
    auto P = [u] (float x, float y) { return juce::Point<float> (x * u, y * u); };
    auto R = [u] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * u, y * u, w * u, h * u); };
    const juce::Colour line (0xffdcc8ff), glow (0xffb967ff), dark (0xff17121f);
    const float stroke = 18.0f * u;

    if (withTile)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2440), 0, 100 * u, dark, 0, 924 * u, false));
        g.fillRoundedRectangle (R (100, 100, 824, 824), 180 * u);
        g.setGradientFill (juce::ColourGradient (glow.withAlpha (0.28f), 512 * u, 400 * u, glow.withAlpha (0.0f), 512 * u, 900 * u, true));
        g.fillRoundedRectangle (R (100, 100, 824, 824), 180 * u);
    }

    auto poly = [&P] (juce::Path& p, std::initializer_list<std::pair<float, float>> pts)
    {
        bool first = true;
        for (auto [x, y] : pts)
        {
            first ? p.startNewSubPath (P (x, y)) : p.lineTo (P (x, y));
            first = false;
        }
        p.closeSubPath();
    };

    juce::Path armour, plates, fill;
    // Faceplate: chamfered, narrowing to a jaw.
    poly (armour, { { 392, 214 }, { 632, 214 }, { 660, 250 }, { 660, 352 }, { 600, 412 }, { 424, 412 }, { 364, 352 }, { 364, 250 } });
    // Centre crest on the brow.
    poly (armour, { { 480, 214 }, { 512, 132 }, { 544, 214 } });
    // Antenna horns, swept up and out from the temples.
    poly (armour, { { 372, 240 }, { 318, 150 }, { 404, 214 } });
    poly (armour, { { 652, 240 }, { 706, 150 }, { 620, 214 } });
    // Ear blocks (audio-transformer cans) on the sides.
    armour.addRoundedRectangle (R (322, 262, 42, 92), 8 * u);
    armour.addRoundedRectangle (R (660, 262, 42, 92), 8 * u);
    // Cheek plates.
    plates.startNewSubPath (P (380, 340)); plates.lineTo (P (440, 380));
    plates.startNewSubPath (P (644, 340)); plates.lineTo (P (584, 380));
    // Transformer laminations on the ear cans.
    for (float y : { 282.0f, 300.0f, 318.0f, 336.0f })
    {
        plates.startNewSubPath (P (330, y)); plates.lineTo (P (356, y));
        plates.startNewSubPath (P (668, y)); plates.lineTo (P (694, y));
    }
    // Visor and the mouth grille.
    juce::Path visor;
    poly (visor, { { 410, 270 }, { 614, 270 }, { 596, 318 }, { 428, 318 } });
    for (float x : { 476.0f, 500.0f, 524.0f, 548.0f })
        fill.addRoundedRectangle (R (x - 6, 352, 12, 34), 5 * u);
    // Glowing eyes in the visor, glancing up and over (as on the BDR logo).
    juce::Path eyes;
    eyes.addEllipse (R (0, 0, 46, 30).withCentre (P (462, 290)));
    eyes.addEllipse (R (0, 0, 46, 30).withCentre (P (562, 290)));

    // Tilted (curious), enlarged, centred over the name, like the BDR logo.
    const auto t = juce::AffineTransform::rotation (-0.21f, 512 * u, 300 * u)
                       .scaled (withTile ? 1.62f : 2.05f, withTile ? 1.62f : 2.05f, 512 * u, 300 * u)
                       .translated (0, (withTile ? 150.0f : 222.0f) * u);
    for (auto* p : { &armour, &plates, &visor, &fill, &eyes })
        p->applyTransform (t);

    juce::Graphics::ScopedSaveState clip (g);
    if (withTile)
    {
        juce::Path sq;
        sq.addRoundedRectangle (R (100, 100, 824, 824), 180 * u);
        g.reduceClipRegion (sq);
    }

    // Armour: a dark chrome wash under the line work.
    g.setGradientFill (juce::ColourGradient (glow.withAlpha (0.30f), P (512, 140), dark.withAlpha (0.6f), P (512, 700), false));
    g.fillPath (armour);
    g.setColour (glow.withAlpha (0.18f));
    g.strokePath (armour, juce::PathStrokeType (stroke * 2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    g.setColour (line);
    g.strokePath (armour, juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    g.strokePath (plates, juce::PathStrokeType (stroke * 0.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillPath (fill);

    // Visor: dark glass with a cyan glow, the eyes lit inside it.
    g.setColour (juce::Colour (0xff0b0716));
    g.fillPath (visor);
    g.setColour (cyan.withAlpha (0.25f));
    g.strokePath (visor, juce::PathStrokeType (stroke * 1.8f, juce::PathStrokeType::mitered));
    g.setColour (line);
    g.strokePath (visor, juce::PathStrokeType (stroke * 0.8f, juce::PathStrokeType::mitered));
    g.setColour (cyan.withAlpha (0.35f));
    g.strokePath (eyes, juce::PathStrokeType (stroke * 1.6f));
    g.setColour (cyan.interpolatedWith (juce::Colours::white, 0.45f));
    g.fillPath (eyes);

    if (withTile)
    {
        juce::GlyphArrangement ga;
        ga.addFittedText (font (104 * u, true), "BIG DUMB ROBOT", 180 * u, 724 * u, 664 * u, 120 * u, juce::Justification::centred, 1);
        juce::Path txt;
        ga.createPath (txt);
        g.setColour (line);
        g.fillPath (txt);
        g.strokePath (txt, juce::PathStrokeType (8 * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
} // namespace theme
