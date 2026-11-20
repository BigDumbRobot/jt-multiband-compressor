#pragma once

#include "BinaryData.h"

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

// Shared theme (same as Dynamo EQ / lilEQ). 80s vaporwave look: neon pink, cyan and violet on a deep purple night.
namespace theme
{
inline const juce::Colour night     { 0xff12062a };
inline const juce::Colour dusk      { 0xff2a0b4a };
inline const juce::Colour panel     { 0xff1d0a38 };
inline const juce::Colour pink      { 0xffff3fa4 };
inline const juce::Colour hotPink   { 0xffff71ce };
inline const juce::Colour cyan      { 0xff01cdfe };
inline const juce::Colour mint      { 0xff05ffa1 };
inline const juce::Colour lavender  { 0xffb967ff };
inline const juce::Colour lemon     { 0xfffffb96 };
inline const juce::Colour sunset    { 0xffff9e3d };
inline const juce::Colour text      { 0xfff3e8ff };
inline const juce::Colour dimText   { 0xffa98bd6 };

inline juce::Colour bandColour (int band)
{
    static const juce::Colour c[] = { hotPink, cyan, mint, lavender, lemon, sunset };
    return c[band % 6];
}

inline juce::Font font (float size, bool bold = false, bool italic = false)
{
    int style = juce::Font::plain;
    if (bold) style |= juce::Font::bold;
    if (italic) style |= juce::Font::italic;
    return juce::Font (juce::FontOptions (size, style));
}

// Neon tube: wide faint strokes under a thin bright core.
inline void neonStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core = 2.0f)
{
    for (auto [w, a] : { std::pair { core * 5.0f, 0.08f }, { core * 3.0f, 0.15f }, { core * 1.8f, 0.35f } })
    {
        g.setColour (c.withAlpha (a));
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.35f));
    g.strokePath (p, juce::PathStrokeType (core, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Night-time sky backdrop (same as Pink Spider): dark at the top glowing towards dusk at the
// bottom, with a fixed scatter of faint stars.
inline void drawNightSky (juce::Graphics& g, int w, int h)
{
    g.setGradientFill (juce::ColourGradient (night, 0, 0, dusk.brighter (0.15f), 0, (float) h, false));
    g.fillAll();

    juce::Random rng (42);   // fixed seed so the stars never move
    const int count = w * h / 5000;
    for (int i = 0; i < count; ++i)
    {
        const float x = rng.nextFloat() * (float) w, y = rng.nextFloat() * (float) h * 0.9f;
        g.setColour (text.withAlpha (0.15f + 0.4f * rng.nextFloat()));
        g.fillEllipse (x, y, 1.6f, 1.6f);
    }
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, pink.withAlpha (0.4f));
        setColour (juce::Label::textColourId, text);
        setColour (juce::ComboBox::backgroundColourId, night);
        setColour (juce::ComboBox::outlineColourId, lavender.withAlpha (0.6f));
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::arrowColourId, cyan);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, pink.withAlpha (0.5f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TextEditor::backgroundColourId, night);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, pink.withAlpha (0.4f));
        setColour (juce::CaretComponent::caretColourId, cyan);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Body: dark disc with a chrome-ish rim.
        g.setGradientFill (juce::ColourGradient (dusk.brighter (0.3f), c.x, c.y - r, night, c.x, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c));
        g.setGradientFill (juce::ColourGradient (lavender.withAlpha (0.8f), c.x, c.y - r, pink.withAlpha (0.4f), c.x, c.y + r, false));
        g.drawEllipse (juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c), 1.2f);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r * 0.95f, r * 0.95f, 0.0f, startAngle, endAngle, true);
        g.setColour (night.brighter (0.4f));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Bipolar parameters fill from the middle.
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
        const float from = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r * 0.95f, r * 0.95f, 0.0f, std::min (from, angle), std::max (from, angle), true);
            neonStroke (g, arc, accent, 2.2f);
        }

        juce::Path tick;
        tick.startNewSubPath (c.getPointOnCircumference (r * 0.25f, angle));
        tick.lineTo (c.getPointOnCircumference (r * 0.7f, angle));
        g.setColour (juce::Colours::white);
        g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (night);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 9.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (cyan);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (2, 1, box.getWidth() - 18, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setMinimumHorizontalScale (0.8f);
    }
    juce::Font getPopupMenuFont() override { return font (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        const bool on = b.getToggleState();
        g.setColour (on ? accent.withAlpha (down ? 0.6f : 0.4f) : night.brighter (over ? 0.25f : 0.1f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? accent : lavender.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 4.0f, on ? 1.6f : 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, true); }
};

// Console push buttons, after the SSL 4000: a square cap sitting in a beige collar, bevelled
// edges, a glassy overlay on top. Off: white cap, lettering in the button's colour. On: the cap
// lights in that colour and glows.
class ConsoleButtonLookAndFeel : public LookAndFeel
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        const bool on = b.getToggleState();
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);

        // Collar: a chunky grey bevelled surround, like an SSL key's housing. Four sloped faces,
        // lit from above: top lightest, left and right mid, bottom darkest.
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 3.0f);
        const float bevel = juce::jmin (9.0f, r.getHeight() * 0.18f);
        const auto in = r.reduced (bevel);
        auto face4 = [&] (juce::Point<float> a, juce::Point<float> b2, juce::Point<float> c, juce::Point<float> d, juce::Colour col)
        {
            juce::Path p;
            p.startNewSubPath (a); p.lineTo (b2); p.lineTo (c); p.lineTo (d); p.closeSubPath();
            g.setColour (col);
            g.fillPath (p);
        };
        face4 (r.getTopLeft(), r.getTopRight(), in.getTopRight(), in.getTopLeft(), juce::Colour (0xffc9c9c6));          // top
        face4 (r.getTopLeft(), in.getTopLeft(), in.getBottomLeft(), r.getBottomLeft(), juce::Colour (0xffa3a3a0));       // left
        face4 (r.getTopRight(), r.getBottomRight(), in.getBottomRight(), in.getTopRight(), juce::Colour (0xff7a7a77));   // right
        face4 (r.getBottomLeft(), in.getBottomLeft(), in.getBottomRight(), r.getBottomRight(), juce::Colour (0xff5b5b58)); // bottom
        g.setColour (juce::Colours::white.withAlpha (0.35f));   // crisp outer edge highlight
        g.drawLine (r.getX() + 1.0f, r.getY() + 0.5f, r.getRight() - 1.0f, r.getY() + 0.5f, 1.0f);
        g.setColour (juce::Colour (0xff1e1e1c));
        g.drawRect (r, 1.0f);
        g.setColour (juce::Colour (0xff2c2c2a));   // the well the cap sits in
        g.fillRect (in);

        // Cap: pressed caps sit lower.
        auto cap = in.reduced (1.5f).withTrimmedBottom (1.5f);
        if (down) cap = cap.translated (0.0f, 1.5f);
        if (on)   // lit: a glow spilling onto the collar
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (cap.expanded (2.0f), 4.0f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.55f));   // the cap's side, below its face
        g.fillRoundedRectangle (cap.translated (0.0f, down ? 0.5f : 2.0f), 3.0f);
        const auto top = on ? accent.brighter (0.55f) : juce::Colour (0xfffbf8ef);
        const auto bottom = on ? accent.darker (0.15f) : juce::Colour (0xffcdc8b9);
        juce::ColourGradient face (top, 0, cap.getY(), bottom, 0, cap.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (cap, 3.0f);
        if (over && ! down)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (cap, 3.0f);
        }

        // Bevel: light top and left edges, dark bottom and right.
        g.setColour (juce::Colours::white.withAlpha (on ? 0.6f : 0.85f));
        g.drawLine (cap.getX() + 2.0f, cap.getY() + 0.8f, cap.getRight() - 2.0f, cap.getY() + 0.8f, 1.2f);
        g.drawLine (cap.getX() + 0.8f, cap.getY() + 2.0f, cap.getX() + 0.8f, cap.getBottom() - 2.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawLine (cap.getX() + 2.0f, cap.getBottom() - 0.8f, cap.getRight() - 2.0f, cap.getBottom() - 0.8f, 1.2f);
        g.drawLine (cap.getRight() - 0.8f, cap.getY() + 2.0f, cap.getRight() - 0.8f, cap.getBottom() - 2.0f, 1.0f);

        // Glassy overlay: a soft sheen over the top half and a thin specular streak.
        auto sheen = cap.reduced (1.5f).removeFromTop (cap.getHeight() * 0.48f);
        juce::ColourGradient glass (juce::Colours::white.withAlpha (on ? 0.55f : 0.65f), 0, sheen.getY(),
                                    juce::Colours::white.withAlpha (0.0f), 0, sheen.getBottom(), false);
        g.setGradientFill (glass);
        g.fillRoundedRectangle (sheen, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRoundedRectangle (cap.getX() + 4.0f, cap.getY() + 2.0f, cap.getWidth() * 0.35f, 1.3f, 0.6f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawRoundedRectangle (cap, 3.0f, 0.8f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
        const bool on = b.getToggleState();
        const float bevel = juce::jmin (9.0f, (float) b.getHeight() * 0.18f);
        auto area = b.getLocalBounds().toFloat().reduced (bevel + 3.0f).withTrimmedBottom (1.5f);
        if (down) area = area.translated (0.0f, 1.5f);
        g.setFont (font (12.0f, true));
        // Off: lettering in the button's colour, darkened to read on white. On: dark on the lit cap.
        g.setColour (on ? juce::Colour (0xff1a0d22) : accent.darker (0.55f));
        g.drawFittedText (b.getButtonText(), area.toNearestInt(), juce::Justification::centred, 2, 0.9f);   // "\n" stacks a label
    }
};

// The title face: Russo One, blocky 80s (SIL Open Font License, assets/fonts/RussoOne-OFL.txt).
inline juce::Font titleFont (float size)
{
    static const juce::Typeface::Ptr face =
        juce::Typeface::createSystemTypefaceFor (BinaryData::RussoOneRegular_ttf, BinaryData::RussoOneRegular_ttfSize);
    return juce::Font (juce::FontOptions (face).withHeight (size));
}

// A hardware channel-fader cap: ridged brushed metal under a light purple wash, with a centre
// index line (hot pink while the console drives it).
inline void drawFaderCap (juce::Graphics& g, juce::Rectangle<float> cap, bool live)
{
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (cap.translated (2.0f, 3.0f), 3.0f);
    juce::ColourGradient metal (juce::Colour (0xffeeebf3), 0, cap.getY(), juce::Colour (0xffcfcad8), 0, cap.getBottom(), false);
    metal.addColour (0.45, juce::Colour (0xff8f8a99));
    metal.addColour (0.55, juce::Colour (0xff8f8a99));
    g.setGradientFill (metal);
    g.fillRoundedRectangle (cap, 3.0f);
    // Grip ridges above and below the index line.
    const float mid = cap.getCentreY();
    for (float y = cap.getY() + 4.0f; y < cap.getBottom() - 3.0f; y += 3.5f)
    {
        if (std::abs (y - mid) < 4.0f) continue;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.drawHorizontalLine ((int) y, cap.getX() + 2.0f, cap.getRight() - 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawHorizontalLine ((int) y + 1, cap.getX() + 2.0f, cap.getRight() - 2.0f);
    }
    // Light purple overlay, then the edge.
    g.setColour (lavender.withAlpha (0.38f));
    g.fillRoundedRectangle (cap, 3.0f);
    g.setColour (lavender.withAlpha (0.8f));
    g.drawRoundedRectangle (cap.reduced (0.5f), 3.0f, 1.0f);
    // Index line.
    g.setColour (live ? hotPink : juce::Colours::white);
    g.fillRect (juce::Rectangle<float> (cap.getX() + 1.0f, mid - 1.0f, cap.getWidth() - 2.0f, 2.0f));
}

// Chrome title (Pink Spider's treatment): Russo One, chrome gradient over pink and cyan offsets.
inline void drawChromeTitle (juce::Graphics& g, const juce::String& title, juce::Rectangle<float> r, float size,
                             std::optional<juce::Font> face = std::nullopt)
{
    g.setFont (face ? *face : titleFont (size));
    g.setColour (pink.withAlpha (0.7f));
    g.drawText (title, r.translated (3.0f, 3.0f), juce::Justification::centredLeft);
    g.setColour (cyan.withAlpha (0.5f));
    g.drawText (title, r.translated (-1.5f, 0.0f), juce::Justification::centredLeft);
    juce::ColourGradient chrome (juce::Colours::white, 0, r.getY() + size * 0.15f, lavender, 0, r.getBottom() - size * 0.15f, false);
    chrome.addColour (0.48, juce::Colour (0xffd8c8ff));
    chrome.addColour (0.52, juce::Colour (0xff5a2a8a));
    chrome.addColour (0.8, hotPink);
    g.setGradientFill (chrome);
    g.drawText (title, r, juce::Justification::centredLeft);
}

// The Canadian flag, tone on tone in purples (as in Pink Spider and Crodie): deeper purple
// bars, a dusk centre, and the maple leaf (the official flag's outline) a shade lighter.
inline void drawFlag (juce::Graphics& g, juce::Rectangle<float> r)
{
    static constexpr float leaf[][2] = {
        { -0.0223f, 0.5000f }, { -0.0112f, 0.2859f }, { -0.0387f, 0.2615f }, { -0.2519f, 0.2990f }, { -0.2231f, 0.2196f },
        { -0.2280f, 0.2015f }, { -0.4615f, 0.0124f }, { -0.4089f, -0.0122f }, { -0.4005f, -0.0318f }, { -0.4467f, -0.1737f },
        { -0.3122f, -0.1452f }, { -0.2940f, -0.1546f }, { -0.2680f, -0.2159f }, { -0.1630f, -0.1032f }, { -0.1355f, -0.1174f },
        { -0.1861f, -0.3784f }, { -0.1050f, -0.3315f }, { -0.0824f, -0.3382f }, { 0.0000f, -0.5000f }, { 0.0824f, -0.3382f },
        { 0.1050f, -0.3315f }, { 0.1861f, -0.3784f }, { 0.1355f, -0.1174f }, { 0.1630f, -0.1032f }, { 0.2680f, -0.2159f },
        { 0.2940f, -0.1546f }, { 0.3122f, -0.1452f }, { 0.4467f, -0.1737f }, { 0.4005f, -0.0318f }, { 0.4089f, -0.0122f },
        { 0.4615f, 0.0124f }, { 0.2280f, 0.2015f }, { 0.2231f, 0.2196f }, { 0.2519f, 0.2990f }, { 0.0387f, 0.2615f },
        { 0.0112f, 0.2859f }, { 0.0223f, 0.5000f } };
    const juce::Colour barColour (0xff35175e), centreColour (0xff221044), leafColour (0xff4b2a7d);

    juce::Graphics::ScopedSaveState save (g);
    juce::Path shape;
    shape.addRoundedRectangle (r, 1.5f);
    g.reduceClipRegion (shape);
    const float bar = r.getWidth() * 0.25f;
    g.setColour (barColour);
    g.fillRect (r.withWidth (bar));
    g.fillRect (r.withLeft (r.getRight() - bar));
    const auto centre = r.reduced (bar, 0.0f);
    g.setColour (centreColour);
    g.fillRect (centre);
    juce::Path p;
    const float size = centre.getHeight() * 0.86f;
    const auto c = centre.getCentre().translated (0.0f, -size * 0.01f);
    for (size_t i = 0; i < std::size (leaf); ++i)
    {
        const juce::Point<float> pt (c.x + leaf[i][0] * size, c.y + leaf[i][1] * size);
        i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
    }
    p.closeSubPath();
    g.setColour (leafColour);
    g.fillPath (p);
    g.setColour (lavender.withAlpha (0.18f));
    g.strokePath (p, juce::PathStrokeType (0.6f));
    g.setColour (lavender.withAlpha (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 1.5f, 0.8f);
}

// "From Canada with love": the flag and the line under it, small and tone on tone.
inline void drawFromCanada (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawFlag (g, r.removeFromTop (24.0f).withSizeKeepingCentre (44.0f, 22.0f));
    g.setFont (font (9.5f, false, true));
    g.setColour (lavender.withAlpha (0.45f));
    g.drawText ("From Canada with love", r.withHeight (14.0f), juce::Justification::centred);
}

// The Big Dumb Robot logo / app and plugin icon, drawn at any size (rendered to
// assets/icons/plugin-icon.png): a basic line robot head in light purple, tilted (curious),
// with BIG DUMB ROBOT under it.
inline void drawRobotIcon (juce::Graphics& g, float size)
{
    const float u = size / 1024.0f;
    auto R = [u] (float x, float y, float w, float h) { return juce::Rectangle<float> (x * u, y * u, w * u, h * u); };
    const juce::Colour line (0xffdcc8ff), glow (0xffb967ff), dark (0xff17121f);
    const float stroke = 20.0f * u;

    // Rounded square with a light purple wash.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2440), 0, 100 * u, dark, 0, 924 * u, false));
    g.fillRoundedRectangle (R (100, 100, 824, 824), 180 * u);
    g.setGradientFill (juce::ColourGradient (glow.withAlpha (0.28f), 512 * u, 400 * u, glow.withAlpha (0.0f), 512 * u, 900 * u, true));
    g.fillRoundedRectangle (R (100, 100, 824, 824), 180 * u);

    // The head, drawn upright: face, ears, antenna, eyes; pupils glance up and over.
    juce::Path head, fill;
    head.addRoundedRectangle (R (378, 214, 268, 190), 40 * u);
    head.addRoundedRectangle (R (338, 268, 40, 82), 16 * u);
    head.addRoundedRectangle (R (646, 268, 40, 82), 16 * u);
    head.startNewSubPath (512 * u, 214 * u);
    head.lineTo (512 * u, 168 * u);
    head.addEllipse (R (0, 0, 96, 96).withCentre ({ 450 * u, 302 * u }));
    head.addEllipse (R (0, 0, 96, 96).withCentre ({ 574 * u, 302 * u }));
    fill.addEllipse (R (0, 0, 40, 40).withCentre ({ 462 * u, 290 * u }));
    fill.addEllipse (R (0, 0, 40, 40).withCentre ({ 586 * u, 290 * u }));
    fill.addEllipse (R (0, 0, 46, 46).withCentre ({ 512 * u, 150 * u }));
    fill.addRoundedRectangle (R (476, 356, 72, 14), 7 * u);

    // Tilt it (curious), then make it big and centre it above the name.
    const auto t = juce::AffineTransform::rotation (-0.21f, 512 * u, 300 * u)
                       .scaled (2.15f, 2.15f, 512 * u, 300 * u)
                       .translated (0, 128 * u);
    head.applyTransform (t);
    fill.applyTransform (t);

    // Zoomed in: the head fills the square and is cropped by it.
    juce::Graphics::ScopedSaveState clip (g);
    {
        juce::Path sq;
        sq.addRoundedRectangle (R (100, 100, 824, 824), 180 * u);
        g.reduceClipRegion (sq);
    }
    g.setColour (glow.withAlpha (0.18f));
    g.strokePath (head, juce::PathStrokeType (stroke * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (line);
    g.strokePath (head, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillPath (fill);

    // BIG DUMB ROBOT: chunky letters (glyph outlines, filled and stroked thick).
    juce::GlyphArrangement ga;
    ga.addFittedText (font (104 * u, true), "BIG DUMB ROBOT", 180 * u, 724 * u, 664 * u, 120 * u, juce::Justification::centred, 1);
    juce::Path txt;
    ga.createPath (txt);
    g.setColour (line);
    g.fillPath (txt);
    g.strokePath (txt, juce::PathStrokeType (8 * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
} // namespace theme
