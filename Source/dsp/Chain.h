#pragma once

#include "Crossover.h"
#include "Dynamics.h"

#if BDR_BENCH
 #include <chrono>
namespace bdr { inline double benchT[8] {}; }
 #define BDR_T0(k) auto _t##k = std::chrono::steady_clock::now();
 #define BDR_T1(k, slot) bdr::benchT[slot] += std::chrono::duration<double> (std::chrono::steady_clock::now() - _t##k).count();
#else
 #define BDR_T0(k)
 #define BDR_T1(k, slot)
#endif

namespace bdr
{
// Settings for one block, read from the parameters once per host block.
struct ChainSettings
{
    float xover1 = 270.0f, xover2 = 1247.0f;
    std::array<float, 3> threshold {}, ratio { 1, 1, 1 }, attack { 10, 8, 6 }, release { 150, 75, 100 }, makeup { 1, 1, 1 };
    float makeupAll = 1.0f, ceiling = -0.01f, limRelease = 150.0f;
    bool clipOn = false, clipZero = false, limitOn = false;
    int soloBand = -1; // -1: all bands
};

// The original's processCore at one sample rate: crossover -> band compressors -> sum x makeup ->
// soft clip -> limiter, on blocks of `block` samples. Classic runs one at the session rate with
// 512-sample blocks; Precision runs one at 32x with 16384-sample blocks, between the
// converters, exactly as the original does.
class Chain
{
public:
    void prepare (double sampleRate, int numChannels, int blockSize)
    {
        rate = sampleRate;
        channels = juce::jlimit (1, kMaxCh, numChannels);
        block = blockSize;
        crossover.prepare (rate, channels, block);
        const int maxLook = (int) (rate * 0.01) + 2;
        for (int b = 0; b < 3; ++b)
        {
            comps[(size_t) b].prepare (rate);
            bandDelay[(size_t) b].prepare (maxLook + 2);
        }
        limiter.prepare (rate, std::max (3, (int) (rate * 0.01)) + 1);
        smoothCoef = 1.0f - (float) std::exp (-1.0 / (0.02 * rate));
        for (auto& v : sum) v.assign ((size_t) block, 0.0f);
        for (auto& v : gains) v.assign ((size_t) block, 1.0f);
        for (auto& v : peaks) v.assign ((size_t) block, 0.0f);
        mks.assign ((size_t) block, 1.0f);
        delayed.assign ((size_t) block, 0.0f);
        haveMakeup = false;
        reset();
    }

    void reset()
    {
        crossover.reset();
        for (int b = 0; b < 3; ++b)
        {
            comps[(size_t) b].reset();
            bandDelay[(size_t) b].reset();
        }
        limiter.reset();
    }

    // Returns the lookahead in samples (at this chain's rate).
    int setLookahead (float seconds)
    {
        int la = 1;
        for (int b = 0; b < 3; ++b)
        {
            la = comps[(size_t) b].setLookahead (seconds);
            bandDelay[(size_t) b].setDelay (la - 1);
        }
        limiter.setLookahead (la);
        return la;
    }

    // Precision Lite: the crossover's recursions with fused multiply-adds (not bit-exact; ~1e-16).
    void setFast (bool f) { crossover.setFast (f); limiter.fast = f; fast = f; }

    // The chain's own delay, in samples at its rate: crossover block + compressor and limiter lookahead.
    int delay() const { return crossover.latency() + bandDelay[0].getDelay() + (limiter.getLookahead() - 1); }

    void apply (const ChainSettings& s)
    {
        settings = s;
        crossover.setFrequencies (s.xover1, s.xover2);
        for (int b = 0; b < 3; ++b)
        {
            comps[(size_t) b].setTimes (s.attack[(size_t) b], s.release[(size_t) b]);
            comps[(size_t) b].setCurve (s.threshold[(size_t) b], s.ratio[(size_t) b]);
            comps[(size_t) b].setInterval (30); // every 30 samples at any rate, as the original
        }
        limiter.setCeiling (s.ceiling);
        limiter.setRelease (s.limRelease);
        if (! haveMakeup)
        {
            bandMakeup = s.makeup;
            makeupGain = s.makeupAll;
            haveMakeup = true;
        }
    }

    // One block in, one block out (out may not alias in). Band by band (detection, compressor
    // gains, delayed audio x gain x makeup into the sum), then makeup, clip and limiter per sample.
    void process (const float* const* in, float* const* out)
    {
        BDR_T0 (a)
        crossover.process (in);
        BDR_T1 (a, 0)
        BDR_T0 (b)
        const auto& s = settings;
        for (int ch = 0; ch < channels; ++ch)
            std::fill (sum[(size_t) ch].begin(), sum[(size_t) ch].begin() + block, 0.0f);
        // Detection inputs for all three bands (vectorisable), then the three compressors in one
        // loop: they are independent, so their per-sample dependency chains overlap.
        const float* bin[3][kMaxCh];
        for (int b = 0; b < 3; ++b)
        {
            bin[b][0] = crossover.band (b, 0);
            bin[b][1] = crossover.band (b, channels > 1 ? 1 : 0);
            float* pk = peaks[(size_t) b].data();
            for (int i = 0; i < block; ++i)
                pk[i] = std::max (std::abs (bin[b][0][i]), std::abs (bin[b][1][i]));
        }
        BDR_T1 (b, 1)
        BDR_T0 (c)
        CompressorTrio::run (comps[0], comps[1], comps[2], peaks[0].data(), peaks[1].data(), peaks[2].data(),
                             gains[0].data(), gains[1].data(), gains[2].data(), block);
        BDR_T1 (c, 2)
        BDR_T0 (d)
        for (int b = 0; b < 3; ++b)
        {
            const float* g = gains[(size_t) b].data();
            float gr = grMin[(size_t) b];
            for (int i = 0; i < block; ++i) gr = std::min (gr, g[i]);
            grMin[(size_t) b] = gr;
            // Band makeup (smoothed towards its target) and Solo.
            auto& mk = bandMakeup[(size_t) b];
            const float target = s.makeup[(size_t) b];
            const float audible = s.soloBand < 0 || s.soloBand == b ? 1.0f : 0.0f;
            if (! (mk < target || mk > target)) // already there: no smoothing to do
                std::fill (mks.begin(), mks.begin() + block, mk);
            else
                for (int i = 0; i < block; ++i)
                {
                    mk += (target - mk) * smoothCoef;
                    mks[(size_t) i] = mk;
                }
            for (int ch = 0; ch < channels; ++ch)
                bandDelay[(size_t) b].accumulateBlock (ch, bin[b][ch], g, mks.data(), audible, sum[(size_t) ch].data(), block);
            bandDelay[(size_t) b].advanceBlock();
        }

        BDR_T1 (d, 3)
        BDR_T0 (e)
        if (s.limitOn)
        {
            // One pass: makeup (smoothed), clip, limiter, per sample, as the original.
            for (int i = 0; i < block; ++i)
            {
                makeupGain += (s.makeupAll - makeupGain) * smoothCoef;
                float v[kMaxCh];
                for (int ch = 0; ch < channels; ++ch)
                {
                    float x = sum[(size_t) ch][(size_t) i] * makeupGain;
                    if (s.clipOn)
                        x = s.clipZero ? SoftClip::zeroDbfs (x) : fast ? SoftClip::stockFast (x) : SoftClip::stock (x);
                    v[ch] = x;
                }
                limMin = std::min (limMin, limiter.process (v, channels, true));
                for (int ch = 0; ch < channels; ++ch)
                    out[ch][i] = v[ch];
            }
        }
        else
        {
            // Limiter off: the same makeup and clip, then the limiter's plain delay (kept so the
            // latency doesn't change), block-wise.
            if (! (makeupGain < s.makeupAll || makeupGain > s.makeupAll))
                std::fill (mks.begin(), mks.begin() + block, makeupGain);
            else
                for (int i = 0; i < block; ++i)
                {
                    makeupGain += (s.makeupAll - makeupGain) * smoothCoef;
                    mks[(size_t) i] = makeupGain;
                }
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* acc = sum[(size_t) ch].data();
                float* o = out[ch];
                for (int i = 0; i < block; ++i) o[i] = acc[i] * mks[(size_t) i];
                if (s.clipOn)
                {
                    if (s.clipZero)
                        for (int i = 0; i < block; ++i) o[i] = SoftClip::zeroDbfs (o[i]);
                    else if (fast)
                        for (int i = 0; i < block; ++i) o[i] = SoftClip::stockFast (o[i]);
                    else
                        for (int i = 0; i < block; ++i) o[i] = SoftClip::stock (o[i]);
                }
            }
            limiter.processBlock (out, channels, block, false);
        }
        BDR_T1 (e, 4)
    }

    // Most gain reduction since the last call (for the meters).
    std::array<float, 3> grMin { 1, 1, 1 };
    float limMin = 1.0f;

private:
    double rate = 48000.0;
    int channels = 2, block = 512;
    Crossover3 crossover;
    std::array<BandCompressor, 3> comps;
    std::array<Delay, 3> bandDelay;
    Limiter limiter;
    ChainSettings settings;
    std::array<float, 3> bandMakeup { 1, 1, 1 };
    float makeupGain = 1.0f, smoothCoef = 0.0f;
    bool haveMakeup = false, fast = false;
    std::array<std::vector<float>, kMaxCh> sum;
    std::array<std::vector<float>, 3> gains, peaks;
    std::vector<float> mks, delayed;
};
} // namespace bdr
