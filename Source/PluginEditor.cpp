#include "PluginEditor.h"
#include "Logo.h"

namespace
{
using namespace theme;
constexpr int kWidth = 1000, kHeight = 700;
constexpr float kGrRange = 24.0f; // dB shown by the gain-reduction bars
const char* const kBandTitle[3] = { "LOW", "MID", "HIGH" };

juce::String hzText (float f)
{
    return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + "k" : juce::String (juce::roundToInt (f));
}

juce::Colour bandCol (int b)
{
    static const juce::Colour c[3] = { hotPink, cyan, mint };
    return c[b];
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour titleColour)
{
    g.setColour (panel.withAlpha (0.82f));
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    g.setColour (lavender.withAlpha (0.45f));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.2f);
    if (title.isNotEmpty())
    {
        g.setColour (titleColour);
        g.setFont (font (13.0f, true));
        g.drawText (title, r.reduced (14, 10).withHeight (16), juce::Justification::centredLeft);
    }
}
} // namespace

//==============================================================================
float BandDisplay::freqToX (float hz) const
{
    const auto p = plot();
    return p.getX() + std::log (hz / 20.0f) / std::log (1000.0f) * p.getWidth();
}

float BandDisplay::xToFreq (float x) const
{
    const auto p = plot();
    return 20.0f * std::pow (1000.0f, (x - p.getX()) / p.getWidth());
}

float BandDisplay::crossover (int i) const
{
    const float f1 = proc.apvts.getRawParameterValue (ids::xover1)->load();
    const float f2 = proc.apvts.getRawParameterValue (ids::xover2)->load();
    return i == 0 ? f1 : std::max (f2, f1 * 1.41421356f); // as the DSP uses them
}

int BandDisplay::hitCrossover (float x) const
{
    int best = -1;
    float bestD = 9.0f;
    for (int i = 0; i < 2; ++i)
        if (const float d = std::abs (freqToX (crossover (i)) - x); d < bestD)
        {
            bestD = d;
            best = i;
        }
    return best;
}

void BandDisplay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (night.withAlpha (0.88f));
    g.fillRoundedRectangle (b, 6.0f);

    const auto p = plot();
    const float x1 = freqToX (crossover (0)), x2 = freqToX (crossover (1));
    const float edges[4] = { p.getX(), x1, x2, p.getRight() };

    // Bands: a wash of each band's colour, its name, and its gain reduction hanging from the top.
    for (int band = 0; band < 3; ++band)
    {
        const auto region = juce::Rectangle<float> (edges[band], p.getY(), edges[band + 1] - edges[band], p.getHeight());
        const auto c = bandCol (band);
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.16f), 0, region.getY(), c.withAlpha (0.03f), 0, region.getBottom(), false));
        g.fillRect (region);

        g.setFont (font (11.0f, true));
        g.setColour (c.withAlpha (0.9f));
        g.drawText (kBandTitle[band], region.withY (b.getY() + 6.0f).withHeight (14.0f), juce::Justification::centred);

        const float red = juce::jlimit (0.0f, kGrRange, -gr[(size_t) band]);
        const float w = juce::jlimit (6.0f, 46.0f, region.getWidth() * 0.35f);
        const auto bar = juce::Rectangle<float> (w, p.getHeight() * red / kGrRange).withPosition (region.getCentreX() - w * 0.5f, p.getY());
        if (red > 0.05f)
        {
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.85f), 0, bar.getY(), c.withAlpha (0.35f), 0, bar.getBottom(), false));
            g.fillRoundedRectangle (bar, 2.0f);
            g.setColour (c.withAlpha (0.25f));
            g.drawRoundedRectangle (bar.expanded (2.0f), 3.0f, 2.0f);
        }
        if (region.getWidth() > 40.0f)
        {
            g.setColour (red > 0.05f ? theme::text : dimText.withAlpha (0.6f));
            g.setFont (font (11.0f));
            g.drawText (red > 0.05f ? "-" + juce::String (red, 1) + " dB" : "0.0 dB",
                        juce::Rectangle<float> (80.0f, 14.0f).withCentre ({ region.getCentreX(), bar.getBottom() + 12.0f }),
                        juce::Justification::centred);
        }
    }

    // RTA behind everything else (as in lilEQ and Pink Spider): input pink, output cyan.
    rta.paint (g, p, [this] (float hz) { return freqToX (hz); }, proc.getRtaMode());

    // GR scale on the left.
    g.setFont (font (9.5f));
    for (float db : { 6.0f, 12.0f, 18.0f })
    {
        const float y = p.getY() + p.getHeight() * db / kGrRange;
        g.setColour (lavender.withAlpha (0.1f));
        g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
        g.setColour (dimText.withAlpha (0.7f));
        g.drawText ("-" + juce::String ((int) db), juce::Rectangle<float> (p.getX() + 2.0f, y - 7.0f, 24.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Frequency guides.
    for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f);
        const bool major = f == 100.0f || f == 1000.0f || f == 10000.0f;
        g.setColour (lavender.withAlpha (major ? 0.2f : 0.08f));
        g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        g.setColour (dimText);
        g.setFont (font (10.5f));
        g.drawText (hzText (f), juce::Rectangle<float> (x - 24.0f, p.getBottom() + 2.0f, 48.0f, 14.0f), juce::Justification::centred);
    }

    // Crossover lines: neon, with a handle and the frequency.
    for (int i = 0; i < 2; ++i)
    {
        const float x = freqToX (crossover (i));
        juce::Path line;
        line.startNewSubPath (x, p.getY());
        line.lineTo (x, p.getBottom());
        const bool active = dragging == i || hover == i;
        neonStroke (g, line, active ? juce::Colours::white : lavender, active ? 2.0f : 1.4f);
        const auto knob = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, p.getCentreY() });
        g.setColour (night);
        g.fillEllipse (knob);
        g.setColour (active ? juce::Colours::white : lavender);
        g.drawEllipse (knob, 1.6f);
        g.setFont (font (11.0f, true));
        g.setColour (juce::Colours::white);
        const auto label = juce::Rectangle<float> (64.0f, 16.0f).withCentre ({ x, p.getCentreY() + 18.0f });
        g.setColour (night.withAlpha (0.75f));
        g.fillRoundedRectangle (label, 3.0f);
        g.setColour (theme::text);
        const float f = crossover (i);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz", label,
                    juce::Justification::centred);
    }

    RtaView::paintLabel (g, p, proc.getRtaMode());
    g.setColour (lavender.withAlpha (0.55f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.2f);
}

void BandDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitCrossover (e.position.x);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BandDisplay::mouseDown (const juce::MouseEvent& e)
{
    // The RTA label cycles In + Out / In / Out / Off.
    if (RtaView::labelBounds (plot()).contains (e.position))
    {
        proc.setRtaMode ((proc.getRtaMode() + 1) % 4);
        dragging = -1;
        repaint();
        return;
    }
    dragging = hitCrossover (e.position.x);
    if (dragging >= 0)
        proc.apvts.getParameter (dragging == 0 ? ids::xover1 : ids::xover2)->beginChangeGesture();
}

void BandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    auto* prm = proc.apvts.getParameter (dragging == 0 ? ids::xover1 : ids::xover2);
    float f = juce::jlimit (35.0f, 16000.0f, xToFreq (e.position.x));
    if (dragging == 1)
        f = std::max (f, crossover (0) * 1.41421356f); // half an octave above crossover 1
    prm->setValueNotifyingHost (prm->convertTo0to1 (f));
    repaint();
}

void BandDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        proc.apvts.getParameter (dragging == 0 ? ids::xover1 : ids::xover2)->endChangeGesture();
    dragging = -1;
}

void BandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click a crossover line: back to its default.
    if (const int i = hitCrossover (e.position.x); i >= 0)
    {
        auto* prm = proc.apvts.getParameter (i == 0 ? ids::xover1 : ids::xover2);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->getDefaultValue());
        prm->endChangeGesture();
        repaint();
    }
}

//==============================================================================
void OutputMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float rowH = r.getHeight() / 4.0f;
    auto row = [&] (const juce::String& label, float fraction, juce::Colour c, const juce::String& value, bool fromRight)
    {
        auto line = r.removeFromTop (rowH).reduced (0.0f, 3.0f);
        g.setFont (font (10.0f, true));
        g.setColour (dimText);
        g.drawText (label, line.removeFromLeft (44.0f), juce::Justification::centredLeft);
        auto valueBox = line.removeFromRight (52.0f);
        g.setColour (night.withAlpha (0.85f));
        g.fillRoundedRectangle (line, 3.0f);
        fraction = juce::jlimit (0.0f, 1.0f, fraction);
        if (fraction > 0.0f)
        {
            const auto fill = fromRight ? line.withLeft (line.getRight() - line.getWidth() * fraction) : line.withWidth (line.getWidth() * fraction);
            g.setColour (c.withAlpha (0.25f));
            g.fillRoundedRectangle (fill.expanded (1.5f), 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (fill.reduced (0.0f, 2.0f), 2.0f);
        }
        g.setColour (theme::text);
        g.setFont (font (10.5f));
        g.drawText (value, valueBox, juce::Justification::centredRight);
    };
    auto levelRow = [&] (const char* label, float lin)
    {
        const float db = bdr::gainToDb (lin);
        const auto c = db > -0.5f ? hotPink : (db > -6.0f ? lemon : mint);
        row (label, (db + 60.0f) / 60.0f, c, db < -59.0f ? juce::String ("-inf") : juce::String (db, 1), false);
    };
    levelRow ("OUT L", levels[0]);
    levelRow ("OUT R", levels[1]);
    row ("LIMIT", -limit / 12.0f, lemon, juce::String (limit, 1), true);
    row ("MATCH", showMatch ? std::abs (match) / 12.0f : 0.0f, cyan, showMatch ? (match > 0 ? "+" : "") + juce::String (match, 1) : juce::String ("off"), false);
}

//==============================================================================
MultibandEditor::MultibandEditor (MultibandProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    setLookAndFeel (&lnf);
    proc.analyzerWanted.store (true);
    addAndMakeVisible (display);
    addAndMakeVisible (meter);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setTooltip ("Presets");
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    for (int b = 0; b < 3; ++b)
    {
        auto& ui = bandUi[(size_t) b];
        const auto c = bandCol (b);
        setupKnob (ui.thresh, ids::bandParam (b, "thresh"), c);
        setupKnob (ui.ratio, ids::bandParam (b, "ratio"), c);
        setupKnob (ui.makeup, ids::bandParam (b, "makeup"), c);
        setupKnob (ui.attack, ids::bandParam (b, "attack"), c);
        setupKnob (ui.release, ids::bandParam (b, "release"), c);

        // SOLO: hear just this band; click again for all bands.
        ui.solo.setButtonText ("SOLO");
        ui.solo.setLookAndFeel (&consoleLnf);
        ui.solo.setColour (juce::TextButton::buttonOnColourId, c);
        ui.solo.setTooltip ("Hear only this band");
        ui.solo.onClick = [this, b]
        {
            auto* soloPrm = proc.apvts.getParameter (ids::solo);
            auto* bandPrm = proc.apvts.getParameter (ids::band);
            const bool soloing = soloPrm->getValue() > 0.5f && juce::roundToInt (bandPrm->convertFrom0to1 (bandPrm->getValue())) == b;
            bandPrm->beginChangeGesture();
            bandPrm->setValueNotifyingHost (bandPrm->convertTo0to1 ((float) b));
            bandPrm->endChangeGesture();
            soloPrm->beginChangeGesture();
            soloPrm->setValueNotifyingHost (soloing ? 0.0f : 1.0f);
            soloPrm->endChangeGesture();
        };
        addAndMakeVisible (ui.solo);
    }
    setupKnob (makeupKnob, ids::makeup, lavender);
    setupKnob (limThreshKnob, ids::limThresh, lemon);
    setupKnob (limReleaseKnob, ids::limRelease, lemon);

    setupButton (precisionButton, "PRECISION", ids::precision, cyan,
                 "Precision: the whole chain at 32x oversampling");
    setupButton (liteButton, "LITE", ids::lite, cyan,
                 "Precision Lite: the same 32x engine at about half the CPU");
    setupButton (lookButton, "LOOK 10MS", ids::longLook, lavender, "Lookahead 10 ms instead of 2 ms (compressors and limiter)");
    setupButton (clipButton, "SOFT CLIP", ids::softClip, sunset, "Soft clipper on the sum");
    setupButton (clip0Button, "CLIP 0DB", ids::clip0dB, sunset, "Soft clip law: unity gain, ceiling exactly 0 dBFS");
    setupButton (limitButton, "LIMITER", ids::limiter, lemon, "Brickwall limiter on the sum");
    setupButton (matchButton, "GAIN MATCH", ids::gainMatch, mint, "Match output loudness to the input (BS.1770, 3 s)");
    setupButton (bypassButton, "BYPASS", ids::bypass, hotPink, "Latency-matched bypass");

    setSize (kWidth, kHeight);
    startTimerHz (30);
}

MultibandEditor::~MultibandEditor()
{
    stopTimer();
    proc.analyzerWanted.store (false);
    for (auto* b : { &precisionButton, &liteButton, &lookButton, &clipButton, &clip0Button, &limitButton, &matchButton, &bypassButton })
        b->setLookAndFeel (nullptr);
    for (auto& ui : bandUi)
        ui.solo.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MultibandEditor::setupKnob (juce::Slider& s, const juce::String& paramId, juce::Colour accent)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 18);
    s.setColour (juce::Slider::textBoxOutlineColourId, theme::text.withAlpha (0.35f));
    s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    sliderAtts.push_back (std::make_unique<SliderAtt> (proc.apvts, paramId, s));
    if (auto* prm = proc.apvts.getParameter (paramId))
        s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    addAndMakeVisible (s);
}

void MultibandEditor::setupButton (juce::TextButton& b, const juce::String& label, const juce::String& paramId, juce::Colour accent,
                                   const juce::String& tip)
{
    b.setButtonText (label);
    b.setClickingTogglesState (true);
    b.setLookAndFeel (&consoleLnf);
    b.setColour (juce::TextButton::buttonOnColourId, accent);
    b.setTooltip (tip);
    buttonAtts.push_back (std::make_unique<ButtonAtt> (proc.apvts, paramId, b));
    addAndMakeVisible (b);
}

void MultibandEditor::timerCallback()
{
    // Lite only means something with Precision on.
    if (auto* prm = proc.apvts.getRawParameterValue (ids::precision))
        liteButton.setEnabled (prm->load() > 0.5f);

    // Gain reduction: fast down, slow back up, so peaks stay readable.
    for (int b = 0; b < 3; ++b)
    {
        const float now = proc.meters.bandGr[(size_t) b].exchange (0.0f);
        auto& shown = grShown[(size_t) b];
        shown = now < shown ? now : shown + 0.18f * (now - shown);
        display.setReduction (b, shown);
    }
    const float lim = proc.meters.limitGr.exchange (0.0f);
    limitShown = lim < limitShown ? lim : limitShown + 0.18f * (lim - limitShown);
    levelL = std::max (proc.meters.peak[0].exchange (0.0f), levelL * 0.86f);
    levelR = std::max (proc.meters.peak[1].exchange (0.0f), levelR * 0.86f);
    meter.set (levelL, levelR, limitShown, proc.meters.matchDb.load(), proc.apvts.getRawParameterValue (ids::gainMatch)->load() > 0.5f);

    // Solo lights on the band being soloed.
    const bool soloOn = proc.apvts.getRawParameterValue (ids::solo)->load() > 0.5f;
    const int soloBand = juce::roundToInt (proc.apvts.getRawParameterValue (ids::band)->load());
    for (int b = 0; b < 3; ++b)
        bandUi[(size_t) b].solo.setToggleState (soloOn && soloBand == b, juce::dontSendNotification);

    // A preset picked from the host shows here too.
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);

    // The 0 dB law only matters with the clipper in.
    clip0Button.setEnabled (proc.apvts.getRawParameterValue (ids::softClip)->load() > 0.5f);
    const bool limitIn = proc.apvts.getRawParameterValue (ids::limiter)->load() > 0.5f;
    limThreshKnob.setEnabled (limitIn);
    limReleaseKnob.setEnabled (limitIn);
    display.updateRta();
    display.repaint();
}

void MultibandEditor::resized()
{
    captions.clear();
    auto area = getLocalBounds();
    header = area.removeFromTop (74);
    presetBox.setBounds (kWidth - 146 - 24 - 200, 26, 200, 28); // left of the flag
    area = area.reduced (16, 0).withTrimmedBottom (16);

    outputPanel = area.removeFromRight (214);
    area.removeFromRight (14);
    displayArea = area.removeFromTop (196);
    display.setBounds (displayArea);
    area.removeFromTop (12);

    // A knob is about as tall as it is wide, plus its value box, centred in its cell with the caption on top.
    auto knob = [this] (juce::Slider& s, juce::Rectangle<int> r, const juce::String& caption)
    {
        r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), juce::jmin (r.getWidth(), 96) + 40));
        captions.push_back ({ r.removeFromTop (16), caption });
        s.setBounds (r);
    };

    const int gap = 12;
    const int w = (area.getWidth() - 2 * gap) / 3;
    for (int b = 0; b < 3; ++b)
    {
        bandPanels[(size_t) b] = area.removeFromLeft (w);
        if (b < 2) area.removeFromLeft (gap);
        auto& ui = bandUi[(size_t) b];
        ui.solo.setBounds (bandPanels[(size_t) b].getRight() - 76, bandPanels[(size_t) b].getY() + 6, 66, 28);
        auto r = bandPanels[(size_t) b].reduced (10, 10).withTrimmedTop (26);
        const int kw = r.getWidth() / 3;
        auto row1 = r.removeFromTop (r.getHeight() / 2);
        knob (ui.thresh, row1.removeFromLeft (kw), "THRESHOLD");
        knob (ui.ratio, row1.removeFromLeft (kw), "RATIO");
        knob (ui.makeup, row1, "MAKEUP");
        auto row2 = r.withSizeKeepingCentre (kw * 2, r.getHeight());
        knob (ui.attack, row2.removeFromLeft (kw), "ATTACK");
        knob (ui.release, row2, "RELEASE");
    }

    // Output column.
    auto o = outputPanel.reduced (12, 10).withTrimmedTop (26);
    knob (makeupKnob, o.removeFromTop (112).withSizeKeepingCentre (100, 112), "MAKEUP");
    o.removeFromTop (8);
    auto buttonRow = [&o] (juce::Button& a, juce::Button* b)
    {
        auto row = o.removeFromTop (38);
        o.removeFromTop (6);
        if (b == nullptr)
        {
            a.setBounds (row);
            return;
        }
        a.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3));
        row.removeFromLeft (6);
        b->setBounds (row);
    };
    buttonRow (precisionButton, &liteButton);
    buttonRow (clipButton, &clip0Button);
    buttonRow (limitButton, &matchButton);
    o.removeFromTop (4);
    auto lim = o.removeFromTop (112);
    knob (limThreshKnob, lim.removeFromLeft (lim.getWidth() / 2), "CEILING");
    knob (limReleaseKnob, lim, "RELEASE");
    o.removeFromTop (6);
    buttonRow (lookButton, &bypassButton);
    meterArea = o.withTrimmedTop (4);
    meter.setBounds (meterArea);
}

void MultibandEditor::paint (juce::Graphics& g)
{
    drawNightSky (g, getWidth(), getHeight());

    // Header: the robot, the chrome title (shrunk to fit before the preset box), maker, and
    // From Canada with love in the corner.
    drawTransformerBot (g, { 10.0f, 4.0f, 66.0f, 66.0f }, false);
    {
        const juce::String title = "JT MULTIBAND COMPRESSOR";
        const auto r = juce::Rectangle<float> (84.0f, 6.0f, (float) presetBox.getX() - 84.0f - 20.0f, 50.0f);
        float size = 40.0f;
        const float w = juce::GlyphArrangement::getStringWidth (titleFont (size), title) + 6.0f;
        if (w > r.getWidth())
            size *= r.getWidth() / w;
        drawChromeTitle (g, title, r, size);
    }
    g.setFont (font (11.0f, true));
    g.setColour (dimText);
    g.drawText ("BIG DUMB ROBOT", 86, 54, 300, 14, juce::Justification::left);
    drawFromCanada (g, { (float) getWidth() - 146.0f, 14.0f, 124.0f, 40.0f });
    g.setFont (font (10.0f, true));
    g.setColour (dimText);
    g.drawText ("PRESET", presetBox.getBounds().translated (0, -16).withHeight (14), juce::Justification::centred);

    for (int b = 0; b < 3; ++b)
    {
        const auto c = bandCol (b);
        const float lo = b == 0 ? 20.0f : proc.apvts.getRawParameterValue (b == 1 ? ids::xover1 : ids::xover2)->load();
        const float hi = b == 2 ? 20000.0f : proc.apvts.getRawParameterValue (b == 0 ? ids::xover1 : ids::xover2)->load();
        drawPanel (g, bandPanels[(size_t) b], kBandTitle[b], c);
        g.setFont (font (11.0f));
        g.setColour (dimText);
        g.drawText (hzText (lo) + " - " + hzText (hi) + " Hz", bandPanels[(size_t) b].reduced (14, 10).withHeight (16).withTrimmedRight (76),
                    juce::Justification::centredRight);
    }
    drawPanel (g, outputPanel, "OUTPUT", lavender);

    g.setFont (font (12.0f));
    for (auto& [r, t] : captions)
    {
        g.setColour (dimText);
        g.drawText (t, r, juce::Justification::centred);
    }

    // Faint CRT scanlines, as on Waldo's panels.
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    for (int y = 0; y < getHeight(); y += 3)
        g.drawHorizontalLine (y, 0.0f, (float) getWidth());
}
