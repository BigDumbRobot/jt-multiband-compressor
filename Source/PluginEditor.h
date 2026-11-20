#pragma once

#include "PluginProcessor.h"
#include "Theme.h"
#include "RtaView.h"

// The crossover display: three bands across a log frequency axis, with draggable
// crossover lines and each band's gain reduction hanging from the top.
class BandDisplay : public juce::Component
{
public:
    explicit BandDisplay (MultibandProcessor& p) : proc (p) {}
    void setReduction (int band, float db) { gr[(size_t) band] = db; }
    void updateRta() { rta.update (proc.analyzerIn, proc.analyzerOut, 1.0f / 30.0f); }
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (12.0f, 10.0f).withTrimmedBottom (14.0f).withTrimmedTop (18.0f); }
    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float crossover (int i) const;
    int hitCrossover (float x) const;

    MultibandProcessor& proc;
    std::array<float, 3> gr {};
    int dragging = -1, hover = -1;
    RtaView rta;
};

// Output level, limiter reduction and Gain Match amount as horizontal bars.
class OutputMeter : public juce::Component
{
public:
    void set (float l, float r, float limitDb, float matchDb, bool matchOn)
    {
        levels = { l, r };
        limit = limitDb;
        match = matchDb;
        showMatch = matchOn;
        repaint();
    }
    void paint (juce::Graphics&) override;

private:
    std::array<float, 2> levels {};
    float limit = 0.0f, match = 0.0f;
    bool showMatch = false;
};

class MultibandEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MultibandEditor (MultibandProcessor&);
    ~MultibandEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider&, const juce::String& paramId, juce::Colour accent);
    void setupButton (juce::TextButton&, const juce::String& label, const juce::String& paramId, juce::Colour accent, const juce::String& tip);

    MultibandProcessor& proc;
    theme::LookAndFeel lnf;
    theme::ConsoleButtonLookAndFeel consoleLnf;
    juce::TooltipWindow tooltips { this, 600 };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    BandDisplay display;
    OutputMeter meter;
    juce::ComboBox presetBox;

    struct BandControls
    {
        juce::Slider thresh, ratio, makeup, attack, release;
        juce::TextButton solo;
    };
    std::array<BandControls, 3> bandUi;
    juce::Slider makeupKnob, limThreshKnob, limReleaseKnob;
    juce::TextButton precisionButton, liteButton, lookButton, clipButton, clip0Button, limitButton, matchButton, bypassButton;

    std::vector<std::unique_ptr<SliderAtt>> sliderAtts;
    std::vector<std::unique_ptr<ButtonAtt>> buttonAtts;

    juce::Rectangle<int> header, displayArea, outputPanel, meterArea;
    std::array<juce::Rectangle<int>, 3> bandPanels;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;

    std::array<float, 3> grShown {};
    float levelL = 0.0f, levelR = 0.0f, limitShown = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandEditor)
};
