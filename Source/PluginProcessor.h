#pragma once

#include "dsp/Chain.h"
#include "dsp/Converter.h"
#include "SpectrumAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
inline constexpr const char* xover1 = "xover1";
inline constexpr const char* xover2 = "xover2";
inline constexpr const char* makeup = "makeup";
inline constexpr const char* softClip = "softClip";
inline constexpr const char* clip0dB = "clip0dB";
inline constexpr const char* limiter = "limiter";
inline constexpr const char* limThresh = "limThresh";
inline constexpr const char* limRelease = "limRelease";
inline constexpr const char* gainMatch = "gainMatch";
inline constexpr const char* precision = "precision";
inline constexpr const char* longLook = "longLook";
inline constexpr const char* lite = "lite";
inline constexpr const char* bypass = "bypass";
inline constexpr const char* band = "band";    // which band Solo plays
inline constexpr const char* solo = "solo";

inline const char* const bandName[3] = { "low", "mid", "high" };
inline juce::String bandParam (int b, const char* what) { return juce::String (bandName[b]) + "_" + what; }
} // namespace ids

class MultibandProcessor : public juce::AudioProcessor
{
public:
    MultibandProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets: "Default" (everything normalled) and "Tad".
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ids::bypass); }

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor.
    struct Meters
    {
        std::array<std::atomic<float>, 3> bandGr {};   // most reduction since last read, dB (<= 0)
        std::atomic<float> limitGr { 0.0f };
        std::array<std::atomic<float>, 2> peak {};     // output peak since last read
        std::atomic<float> matchDb { 0.0f };
        std::atomic<bool> clipped { false };
    } meters;

    // RTA (the same analyzer as lilEQ and Pink Spider): the input (latency-matched) and the output.
    dynamo::SpectrumAnalyzer analyzerIn, analyzerOut;
    std::atomic<bool> analyzerWanted { false };
    int getRtaMode() const { return (int) apvts.state.getProperty ("rtaMode", 3); }
    void setRtaMode (int m) { apvts.state.setProperty ("rtaMode", m, nullptr); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void updateLatency();
    void clearState();
    void runChain (int chans);
    void readSettings();
    static constexpr int kBlock = bdr::Crossover3::kBlock;

    double sr = 48000.0;
    int numCh = 2;
    int currentProgram = 0;

    // Classic: the chain at the session rate. Precision: the original's 32x engine, the same
    // chain at 32x between the 32x converters.
    // Precision Lite: the same chain at 32x, with cheap converters (FFT first stage, float).
    bdr::Chain base, os32, lite;   // lite: Precision Lite's chain (the 32x chain)
    bdr::Converter32 converter;
    bdr::ConverterLite liteConv;
    std::array<std::vector<float>, 2> up32, out32;   // one block at 32x (also Lite's, at its rate)
    bdr::Loudness inLoudness, outLoudness;
    bdr::Delay dryDelay;                     // bypass: the dry signal, latency-matched
    bdr::ChainSettings settings;
    std::vector<float> rtaIn, rtaOut;        // mono sums for the analyzers, one host block

    double matchGain = 1.0, matchTarget = 1.0, matchCoef = 0.0;   // Gain Match, linear, double as the original
    std::array<std::vector<float>, 2> inFifo, outFifo;           // 512-sample blocks in and out of the chain
    int fifoPos = 0;
    bool matchOn = false, bypassed = false, precise = false, liteOn = false;
    int engine() const { return precise ? (liteOn ? 2 : 1) : 0; } // 0 Classic, 1 Precision, 2 Lite
    bool testNoPredict = false;
    std::atomic<float> matchMemory { 0.0f };   // last matched gain, kept across stops and saved with the session
    bool matchKnown = false;                   // has Gain Match measured (or been restored) yet?
    bool wasPlaying = false;
    int64_t samplesSinceStart = 0;
    float bypassMix = 0.0f, bypassStep = 0.0f;   // 0 = processed, 1 = dry
    bool longLookahead = false;
    int latency = 0;
};
