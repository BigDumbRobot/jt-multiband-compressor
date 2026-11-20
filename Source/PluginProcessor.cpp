#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float v) { return s * std::pow (e / s, v); },
             [] (float s, float e, float x) { return std::log (x / s) / std::log (e / s); },
             [] (float s, float e, float x) { return juce::jlimit (s, e, std::round (x * 10.0f) / 10.0f); } };
}

juce::NormalisableRange<float> range (float lo, float hi, float step, float centre = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > lo && centre < hi)
        r.setSkewForCentre (centre);
    return r;
}

juce::String fmt (float v, int decimals, const char* unit) { return juce::String (v, decimals) + unit; }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MultibandProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    // Values show at full precision (as the original's do): hosts set parameters from this text.
    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; });
    auto db = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        if (std::abs (v) < 0.005f) return String ("0.00 dB");
        return (v > 0 ? "+" : "") + fmt (v, 2, " dB");
    });
    auto ms = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return fmt (v, v < 10.0f ? 2 : 1, " ms"); });
    auto ratio = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return fmt (v, 2, ":1"); });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::xover1, 1 }, "Crossover 1", logRange (35.0f, 16000.0f), 270.0f, hz));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::xover2, 1 }, "Crossover 2", logRange (35.0f, 16000.0f), 1247.0f, hz));

    const float attack[3] = { 10.0f, 8.0f, 6.0f }, release[3] = { 150.0f, 75.0f, 100.0f };
    const char* names[3] = { "Low", "Mid", "High" };
    for (int b = 0; b < 3; ++b)
    {
        const String n (names[b]);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bandParam (b, "thresh"), 1 }, n + " Threshold", range (-60.0f, 0.0f, 0.01f), 0.0f, db));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bandParam (b, "ratio"), 1 }, n + " Ratio", range (1.0f, 8.0f, 0.01f, 2.5f), 1.0f, ratio));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bandParam (b, "attack"), 1 }, n + " Attack", range (0.1f, 10.0f, 0.01f, 2.0f), attack[b], ms));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bandParam (b, "release"), 1 }, n + " Release", range (10.0f, 500.0f, 0.01f, 100.0f), release[b], ms));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::bandParam (b, "makeup"), 1 }, n + " MakeUp", range (-12.0f, 24.0f, 0.01f), 0.0f, db));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::makeup, 1 }, "MakeUp", range (-12.0f, 12.0f, 0.01f), 0.0f, db));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::softClip, 1 }, "Soft Clip", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::clip0dB, 1 }, "Soft Clip 0 dBFS", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::limiter, 1 }, "Limiter", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::limThresh, 1 }, "Limiter Ceiling", range (-60.0f, 1.0f, 0.01f, -6.0f), -0.01f, db));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::limRelease, 1 }, "Limiter Release", range (10.0f, 500.0f, 0.01f, 100.0f), 150.0f, ms));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::gainMatch, 1 }, "Gain Match", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::precision, 1 }, "Precision", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::longLook, 1 }, "Lookahead 10 ms", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::lite, 1 }, "Precision Lite", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::band, 1 }, "Band", StringArray { "Low", "Mid", "High" }, 2));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::solo, 1 }, "Solo", false));
    return layout;
}

MultibandProcessor::MultibandProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool MultibandProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

// Test hook for null tests: JT_TEST_PARAMS="id=value,id=value" sets parameters to exact
// values (hosts set them through text and ranges, which can't hit e.g. -9.9499969 exactly).
// Unset in normal use, so it does nothing.
static void applyTestParams (juce::AudioProcessorValueTreeState& apvts)
{
    const auto spec = juce::SystemStats::getEnvironmentVariable ("JT_TEST_PARAMS", {});
    for (const auto& item : juce::StringArray::fromTokens (spec, ",", {}))
    {
        const auto id = item.upToFirstOccurrenceOf ("=", false, false).trim();
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        {
            const float v = item.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            if (auto* raw = apvts.getRawParameterValue (id)) raw->store (v); // exact, not via the 0..1 round trip
        }
    }
}

void MultibandProcessor::prepareToPlay (double sampleRate, int)
{
    applyTestParams (apvts);
    testNoPredict = std::getenv ("JT_TEST_NO_PREDICT") != nullptr; // test hook: Gain Match starts at 0 dB like the original
    sr = sampleRate;
    numCh = juce::jlimit (1, 2, getTotalNumInputChannels());

    base.prepare (sr, numCh, kBlock);
    os32.prepare (sr * bdr::Converter32::kFactor, numCh, kBlock * bdr::Converter32::kFactor);
    converter.prepare (numCh, kBlock);
    {
        liteConv.prepare (numCh, kBlock);
        lite.prepare (sr * 32, numCh, kBlock * 32);
        lite.setFast (true);
    }
    for (auto* v : { &up32, &out32 })
        for (auto& c : *v) c.assign ((size_t) (kBlock * bdr::Converter32::kFactor), 0.0f);
    inLoudness.prepare (sr);
    outLoudness.prepare (sr);
    analyzerIn.prepare (sr);
    analyzerOut.prepare (sr);
    dryDelay.prepare (8192);
    for (auto* f : { &inFifo, &outFifo })
        for (auto& c : *f) c.assign ((size_t) kBlock, 0.0f);

    matchCoef = 1.0 - std::exp (-1.0 / (sr * 0.08));
    bypassStep = (float) (1.0 / (0.035 * sr));
    bypassMix = param (ids::bypass) > 0.5f ? 1.0f : 0.0f;

    longLookahead = param (ids::longLook) > 0.5f;
    precise = param (ids::precision) > 0.5f;
    liteOn = param (ids::lite) > 0.5f;
    readSettings();
    updateLatency();
    clearState();
}

// A fresh start (prepare, host reset, or the transport starting): nothing from before the
// stop is left in the delay lines, filters or detectors, and Gain Match starts measuring
// again. Unlike the original, Gain Match keeps the gain it had, so playback starts at the
// matched level instead of jumping from 0 dB.
void MultibandProcessor::clearState()
{
    base.reset();
    os32.reset();
    converter.reset();
    lite.reset();
    liteConv.reset();
    dryDelay.reset();
    inLoudness.reset();
    outLoudness.reset();
    for (auto* f : { &inFifo, &outFifo })
        for (auto& c : *f) std::fill (c.begin(), c.end(), 0.0f);
    fifoPos = 0;
    matchGain = matchTarget = std::pow (10.0, (double) matchMemory.load() * 0.05);
}

void MultibandProcessor::reset() { clearState(); }

// Latency: the FIFO's block plus what the original reports. Classic: the crossover's block and the
// compressor and limiter lookahead. Precision: the 32x converters' 1040 samples plus the
// 32x chain's delay, rounded to whole samples as the original does.
void MultibandProcessor::updateLatency()
{
    const float seconds = longLookahead ? 0.010f : 0.002f;
    base.setLookahead (seconds);
    os32.setLookahead (seconds);
    lite.setLookahead (seconds);
    // Precision and Lite: the original's 32x latency (Lite keeps its timing exactly).
    const int chainLatency = engine() == 0 ? base.delay()
                                           : 1040 + (int) std::lround (os32.delay() / (double) bdr::Converter32::kFactor);
    latency = kBlock + chainLatency;
    dryDelay.setDelay (latency);
    setLatencySamples (latency);
}

void MultibandProcessor::readSettings()
{
    auto& s = settings;
    s.xover1 = param (ids::xover1);
    s.xover2 = param (ids::xover2);
    for (int b = 0; b < 3; ++b)
    {
        s.threshold[(size_t) b] = param (ids::bandParam (b, "thresh"));
        s.ratio[(size_t) b] = param (ids::bandParam (b, "ratio"));
        s.attack[(size_t) b] = param (ids::bandParam (b, "attack"));
        s.release[(size_t) b] = param (ids::bandParam (b, "release"));
        s.makeup[(size_t) b] = bdr::dbToGain (param (ids::bandParam (b, "makeup")));
    }
    s.makeupAll = bdr::dbToGain (param (ids::makeup));
    s.clipOn = param (ids::softClip) > 0.5f;
    s.clipZero = param (ids::clip0dB) > 0.5f;
    s.limitOn = param (ids::limiter) > 0.5f;
    s.ceiling = param (ids::limThresh);
    s.limRelease = param (ids::limRelease);
    s.soloBand = param (ids::solo) > 0.5f ? juce::roundToInt (param (ids::band)) : -1;
    base.apply (s);
    os32.apply (s);
    lite.apply (s);
}

void MultibandProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = juce::jmin (numCh, buffer.getNumChannels());
    if (chans <= 0 || n <= 0)
        return;

    // Parameters, once per host block.
    const bool wantPrecise = param (ids::precision) > 0.5f;
    const bool wantLite = param (ids::lite) > 0.5f;
    const bool lng = param (ids::longLook) > 0.5f;
    if (wantPrecise != precise || wantLite != liteOn || lng != longLookahead)
    {
        // Switching engines starts the new one fresh, as the original re-initialises at the new rate.
        const int before = engine();
        precise = wantPrecise;
        liteOn = wantLite;
        if (engine() != before)
        {
            if (engine() == 0) base.reset();
            if (engine() == 1) { os32.reset(); converter.reset(); }
            if (engine() == 2) { lite.reset(); liteConv.reset(); }
        }
        longLookahead = lng;
        updateLatency();
    }
    readSettings();
    matchOn = param (ids::gainMatch) > 0.5f;
    bypassed = param (ids::bypass) > 0.5f;

    // Pressing play: start clean (see clearState).
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying)
                clearState();
            wasPlaying = playing;
        }

    // Gain Match before it has ever measured anything: start from what the settings predict
    // (undoing the makeup) rather than 0 dB, so the very first play is close to matched too.
    if (matchOn && ! matchKnown && ! testNoPredict)
    {
        float bandAvg = 0.0f;
        for (int b = 0; b < 3; ++b)
            bandAvg += param (ids::bandParam (b, "makeup")) / 3.0f;
        matchGain = matchTarget = std::pow (10.0, (double) juce::jlimit (-24.0f, 24.0f, -(param (ids::makeup) + bandAvg)) * 0.05);
    }

    for (auto* c : { &base, &os32, &lite })
    {
        c->grMin = { 1.0f, 1.0f, 1.0f };
        c->limMin = 1.0f;
    }
    std::array<float, 2> peak {};
    bool clipped = false;
    std::array<float*, 2> data { buffer.getWritePointer (0), buffer.getWritePointer (chans > 1 ? 1 : 0) };
    const bool analyse = analyzerWanted.load();
    if (analyse && (int) rtaIn.size() < n)
    {
        rtaIn.resize ((size_t) n);
        rtaOut.resize ((size_t) n);
    }

    // Through the FIFO: the chain runs on whole 512-sample blocks, as the original does at a
    // 512-sample host buffer, so every block-based decision lands on the same samples.
    for (int i = 0; i < n; ++i)
    {
        float out[2] {};
        for (int ch = 0; ch < chans; ++ch)
        {
            inFifo[(size_t) ch][(size_t) fifoPos] = data[(size_t) ch][i];
            out[ch] = outFifo[(size_t) ch][(size_t) fifoPos];
        }
        if (++fifoPos == kBlock)
        {
            fifoPos = 0;
            runChain (chans);
        }

        // Bypass: the latency-matched dry signal, 35 ms crossfade.
        bypassMix = juce::jlimit (0.0f, 1.0f, bypassMix + (bypassed ? bypassStep : -bypassStep));
        float drySum = 0.0f, outSum = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float dry = dryDelay.process (ch, data[(size_t) ch][i]);
            const float o = bypassMix <= 0.0f ? out[ch] : out[ch] + bypassMix * (dry - out[ch]);
            data[(size_t) ch][i] = o;
            drySum += dry;
            outSum += o;
            peak[(size_t) ch] = std::max (peak[(size_t) ch], std::abs (o));
            clipped = clipped || std::abs (o) >= 1.0f;
        }
        dryDelay.advance();
        if (analyse)
        {
            rtaIn[(size_t) i] = drySum / (float) chans;
            rtaOut[(size_t) i] = outSum / (float) chans;
        }
    }
    if (analyse)
    {
        analyzerIn.push (rtaIn.data(), n);
        analyzerOut.push (rtaOut.data(), n);
    }

    for (int ch = chans; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    // Meters: keep the most reduction / highest peak until the editor reads them.
    const auto& active = engine() == 2 ? lite : engine() == 1 ? os32 : base;
    const auto& grMin = active.grMin;
    const float limMin = active.limMin;
    for (int b = 0; b < 3; ++b)
    {
        const float db = bdr::gainToDb (grMin[(size_t) b]);
        if (db < meters.bandGr[(size_t) b].load()) meters.bandGr[(size_t) b].store (db);
    }
    if (const float db = bdr::gainToDb (limMin); db < meters.limitGr.load()) meters.limitGr.store (db);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float p = peak[(size_t) (chans > 1 ? ch : 0)];
        if (p > meters.peak[(size_t) ch].load()) meters.peak[(size_t) ch].store (p);
    }
    meters.matchDb.store (bdr::gainToDb ((float) matchGain));
    if (clipped) meters.clipped.store (true);
}

// One 512-sample block, as the original processes a 512-sample host buffer: input meter ->
// Classic: the chain at the session rate; Precision: up 32x -> the chain at 32x ->
// down -> output meter -> Gain Match target (once per block) -> Gain Match gain.
void MultibandProcessor::runChain (int chans)
{
    for (int i = 0; i < kBlock; ++i)
    {
        const float x[2] = { inFifo[0][(size_t) i], chans > 1 ? inFifo[1][(size_t) i] : 0.0f };
        inLoudness.push (x, chans);
    }
    const float* in[2] = { inFifo[0].data(), inFifo[1].data() };
    float* out[2] = { outFifo[0].data(), outFifo[1].data() };
    if (engine() == 2)
    {
        for (int c = 0; c < chans; ++c)
            liteConv.up (c, in[c], kBlock, up32[(size_t) c].data());
        const float* inF[2] = { up32[0].data(), up32[1].data() };
        float* oF[2] = { out32[0].data(), out32[1].data() };
        lite.process (inF, oF);
        for (int c = 0; c < chans; ++c)
            liteConv.down (c, oF[c], kBlock, out[c]);
    }
    else if (precise)
    {
        for (int c = 0; c < chans; ++c)
            converter.up (c, in[c], kBlock, up32[(size_t) c].data());
        const float* in32[2] = { up32[0].data(), up32[1].data() };
        float* o32[2] = { out32[0].data(), out32[1].data() };
        os32.process (in32, o32);
        for (int c = 0; c < chans; ++c)
            converter.down (c, o32[c], kBlock, out[c]);
    }
    else
        base.process (in, out);

    for (int i = 0; i < kBlock; ++i)
    {
        const float y[2] = { outFifo[0][(size_t) i], chans > 1 ? outFifo[1][(size_t) i] : 0.0f };
        outLoudness.push (y, chans);
    }
    samplesSinceStart += kBlock;

    // Gain Match target: once per block, from both meters.
    if (matchOn)
    {
        if (! bypassed && bypassMix <= 0.0f)
        {
            const double inL = inLoudness.getLufs(), outL = outLoudness.getLufs();
            if (inL > -70.0 && outL > -70.0 && inLoudness.samplesMeasured() / std::max (sr, 1.0) >= 0.4)
            {
                const double diff = inL - outL;
                matchTarget = diff < -24.0 ? std::pow (10.0, -1.2) : std::pow (10.0, std::min (diff, 24.0) * 0.05);
                matchKnown = true;
            }
        }
    }
    else
        matchTarget = 1.0;

    // Apply it (when off, until the gain has glided back to unity).
    if (matchOn || matchGain != 1.0)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            matchGain = matchGain + (matchTarget - matchGain) * matchCoef;
            const float g = (float) matchGain;
            for (int ch = 0; ch < chans; ++ch)
                outFifo[(size_t) ch][(size_t) i] *= g;
        }
        if (! matchOn && std::abs (matchGain - 1.0) < 1.0e-6)
            matchGain = 1.0;
    }
    if (matchOn && matchKnown)
        matchMemory.store (bdr::gainToDb ((float) matchTarget)); // remembered across stops, resets and sessions
}

//==============================================================================
namespace
{
struct Preset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values; // anything not listed is at its default
};

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = {
        { "Default", {} },
        // Tad: gentle 1.2:1 on all three bands from -20 dB, +2.19 dB makeup into the soft
        // clipper, Gain Match and the 32x-precision detector on.
        { "Tad", { { ids::bandParam (0, "thresh"), -20.0f }, { ids::bandParam (0, "ratio"), 1.2f },
                   { ids::bandParam (1, "thresh"), -20.0f }, { ids::bandParam (1, "ratio"), 1.2f },
                   { ids::bandParam (2, "thresh"), -20.0f }, { ids::bandParam (2, "ratio"), 1.2f },
                   { ids::makeup, 2.19f },
                   { ids::softClip, 1.0f },
                   { ids::gainMatch, 1.0f },
                   { ids::precision, 1.0f } } },
    };
    return list;
}
} // namespace

int MultibandProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String MultibandProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size()) ? presets()[(size_t) index].name : "";
}

void MultibandProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    currentProgram = index;
    const auto& preset = presets()[(size_t) index];
    for (auto* p : getParameters())
    {
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (prm == nullptr || prm->getParameterID() == ids::bypass)
            continue; // a preset never bypasses the plugin
        float v = prm->getDefaultValue();
        for (const auto& [id, value] : preset.values)
            if (id == prm->getParameterID())
                v = prm->convertTo0to1 (value);
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (v);
        prm->endChangeGesture();
    }
}

void MultibandProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (matchKnown)
        state.setProperty ("matchDb", matchMemory.load(), nullptr); // Gain Match picks up where it was
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void MultibandProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            if (state.hasProperty ("matchDb"))
            {
                matchMemory.store ((float) state.getProperty ("matchDb"));
                matchKnown = true;
                state.removeProperty ("matchDb", nullptr);
            }
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* MultibandProcessor::createEditor() { return new MultibandEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultibandProcessor(); }
