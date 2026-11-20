#pragma once

#if defined (__aarch64__)
 #include <arm_neon.h>
#endif

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace bdr
{
constexpr int kMaxCh = 2;

// The original calls Apple's exp10f; other implementations can differ by one bit, which is enough
// to flip the compressor's attack/release decision when gain and target are nearly equal.
inline float exp10f_ (float x)
{
#if defined (__APPLE__)
    return __exp10f (x);
#else
    return std::pow (10.0f, x);
#endif
}

inline float dbToGain (float db) { return exp10f_ (db * 0.05f); } // as the original computes makeup gains


inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

//==============================================================================
// Simple per-channel delay line, power-of-two sized.
class Delay
{
public:
    void prepare (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 1) size <<= 1;
        mask = size - 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        pos = 0;
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        pos = 0;
    }
    void setDelay (int d)
    {
        delay = juce::jlimit (0, mask, d);
        for (auto& h : hist) h.assign ((size_t) delay, 0.0f);
    }
    int getDelay() const { return delay; }
    // Writes x and returns the sample from `delay` samples ago. Call once per channel, then advance().
    float process (int ch, float x)
    {
        auto& b = buf[(size_t) ch];
        b[(size_t) pos] = x;
        return b[(size_t) ((pos - delay) & mask)];
    }
    void advance() { pos = (pos + 1) & mask; }

    // A block for one channel (call for each channel, then advanceBlock): out[i] = in[i - delay].
    // Block form: the last `delay` samples are kept in order, so it's two copies, not a ring.
    void processBlock (int ch, const float* in, float* out, int n)
    {
        auto& h = hist[(size_t) ch];
        if ((int) h.size() != delay) h.assign ((size_t) delay, 0.0f); // (only when the delay changes)
        const int d = delay;
        if (n >= d)
        {
            std::copy (h.begin(), h.end(), out);
            std::copy (in, in + (n - d), out + d);
            std::copy (in + (n - d), in + n, h.begin());
        }
        else
        {
            std::copy (h.begin(), h.begin() + n, out);
            std::copy (h.begin() + n, h.end(), h.begin());
            std::copy (in, in + n, h.end() - n);
        }
    }
    void advanceBlock() {}

    // processBlock fused with the chain's band sum: acc[i] += ((in[i - delay] * g[i]) * m[i]) * a
    // (the same products, without writing the delayed block out first).
    void accumulateBlock (int ch, const float* in, const float* g, const float* m, float a, float* acc, int n)
    {
        auto& h = hist[(size_t) ch];
        if ((int) h.size() != delay) h.assign ((size_t) delay, 0.0f);
        const int d = delay;
        if (n < d)
        {
            std::vector<float> tmp ((size_t) n);
            processBlock (ch, in, tmp.data(), n);
            for (int i = 0; i < n; ++i) acc[i] += ((tmp[(size_t) i] * g[i]) * m[i]) * a;
            return;
        }
        const float* hp = h.data();
        for (int i = 0; i < d; ++i) acc[i] += ((hp[i] * g[i]) * m[i]) * a;
        for (int i = d; i < n; ++i) acc[i] += ((in[i - d] * g[i]) * m[i]) * a;
        std::copy (in + (n - d), in + n, h.begin());
    }

private:
    std::array<std::vector<float>, kMaxCh> buf;
    std::array<std::vector<float>, kMaxCh> hist; // block form
    int mask = 0, pos = 0, delay = 0;
};

//==============================================================================
// The detector's view of the signal. Both modes see it kDelay samples late (so latency
// is the same in both):
//   sample(): the plain sample at that time, linked across channels (max |x|).
//   peak():   the highest level between the previous sample and this one, from 7
//             interpolated points (8x), i.e. what a detector running at 32x would catch.
class SubSampleDetector
{
public:
    static constexpr int kTaps = 16, kPhases = 8, kDelay = 7;

    SubSampleDetector()
    {
        // Point m-1+k/8 from samples m-8 .. m+7, Kaiser-windowed sinc, rows normalised.
        const double beta = 6.0;
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        for (int k = 1; k < kPhases; ++k)
        {
            double sum = 0;
            for (int j = 0; j < kTaps; ++j)
            {
                const double u = (double) j - (7.0 + (double) k / kPhases); // distance from the point
                const double s = std::abs (u) < 1e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * u) / (juce::MathConstants<double>::pi * u);
                const double r = u / 8.5;
                const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
                coef[(size_t) k - 1][(size_t) j] = s * w;
                sum += s * w;
            }
            for (auto& c : coef[(size_t) k - 1]) c /= sum;
        }
        reset();
    }

    void reset()
    {
        for (auto& h : hist) h.fill (0.0f);
        pos = 0;
        lastSample = lastPeak = 0.0f;
    }

    // Push one sample per channel. precise = compute the in-between points.
    void push (const float* x, int channels, bool precise)
    {
        float s = 0.0f, p = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& h = hist[(size_t) ch];
            h[(size_t) pos] = h[(size_t) (pos + kTaps)] = x[ch]; // doubled buffer: a straight run of 16
            const float* w = h.data() + pos + 1;                  // oldest .. newest (x[n-15] .. x[n])
            const float m = std::abs (w[8]);                     // x[m], m = n - 7
            s = std::max (s, m);
            if (precise)
            {
                float hi = m;
                for (int k = 0; k < kPhases - 1; ++k)
                {
                    double v = 0.0;
                    for (int j = 0; j < kTaps; ++j)
                        v += coef[(size_t) k][(size_t) j] * w[j];
                    hi = std::max (hi, (float) std::abs (v));
                }
                p = std::max (p, hi);
            }
        }
        pos = (pos + 1) % kTaps;
        lastSample = s;
        lastPeak = precise ? p : s;
    }

    float sample() const { return lastSample; }
    float peak() const { return lastPeak; }

private:
    std::array<std::array<double, kTaps>, kPhases - 1> coef {};
    std::array<std::array<float, 2 * kTaps>, kMaxCh> hist {};
    int pos = 0;
    float lastSample = 0.0f, lastPeak = 0.0f;
};

//==============================================================================
// Band compressor: feed-forward, hard knee, after the original design:
// level = 0.8 x (power smoothed over 0.8 ms) + 0.2 x (highest peak since the last update),
// gain target recalculated every `interval` samples, attack / hold / release smoothing.
//   Classic:   interval 30 samples at the session rate (the original at 1x).
//   Precision: the 32x engine's timing (30 samples at 32x is under one session sample):
//              a new target every sample, from the in-between peak.
class BandCompressor
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        msCoef = std::exp (-1000.0f / ((float) sr * 0.8f)); // single precision throughout, as the original
        reset();
        setTimes (10.0f, 150.0f);
        setLookahead (0.002f);
    }

    void reset()
    {
        ms = 0.0f;
        maxp = 1.0e-6f;
        target = gain = 1.0f;
        counter = counterStart;
        hold = holdSamples; // the original starts with the hold already run out
    }

    // The gain target is recalculated every 30 samples, counted from when the compressor starts.
    // If this compressor hears the band `samplesLate` later than the original's does, start the
    // count that much further on, so the updates land on the same samples as the original's.
    void alignUpdates (int samplesLate)
    {
        counterStart = (30 - samplesLate % 30) % 30;
        counter = counterStart;
    }

    void setTimes (float attackMs, float releaseMs)
    {
        att = std::exp (-1000.0f / ((float) sr * std::max (attackMs, 0.01f)));
        rel = std::exp (-1000.0f / ((float) sr * std::max (releaseMs, 0.01f)));
    }

    void setCurve (float thresholdDb, float ratio)
    {
        threshold = thresholdDb;
        slope = 1.0f - 1.0f / std::max (ratio, 1.0f);
    }

    // Returns the lookahead in samples; the audio is delayed by lookahead - 1.
    int setLookahead (float seconds)
    {
        lookahead = std::max (1, (int) (sr * juce::jlimit (0.0005f, 0.01f, seconds)));
        holdSamples = (int) (1.5f * (float) lookahead); // 3 ms hold at 2 ms lookahead, 15 ms at 10 ms
        return lookahead;
    }
    int getLookahead() const { return lookahead; }

    void setPrecision (bool p) { interval = p ? 1 : 30; }
    void setInterval (int samples) { interval = std::max (1, samples); }

    // One detector step; returns the gain for the audio `lookahead - 1` samples back.
    float process (float sample, float peak)
    {
        const float p = std::max (sample, 1.0e-6f);
        const float p2 = p * p;
        ms = p2 * (1.0f - msCoef) + msCoef * ms;
        const float pk = std::max (peak, 1.0e-6f);
        maxp = std::max (maxp, pk * pk);

        if (counter >= interval)
        {
            const float level = ms * 0.8f + maxp * 0.199999988f; // 0x3e4ccccc exactly: the original's 0.2, one bit short
            counter = 0;
            maxp = 1.0e-6f;
            const float over = 10.0f * std::log10 (std::max (level, 1.0e-30f)) - threshold;
            target = over > 0.0f ? exp10f_ ((over * slope) * -0.05f) : 1.0f;
        }
        ++counter;

        if (gain > target)
        {
            gain = gain * att + target * (1.0f - att);
            hold = 0;
        }
        else if (hold < holdSamples)
            ++hold;
        else
            gain = gain * rel + target * (1.0f - rel);
        return gain;
    }

private:
    friend struct CompressorTrio;
    double sr = 48000.0;
    float msCoef = 0.0f, att = 0.0f, rel = 0.0f, threshold = 0.0f, slope = 0.0f;
    float ms = 0.0f, maxp = 1.0e-6f, target = 1.0f, gain = 1.0f;
    int counter = 0, counterStart = 0, interval = 30, hold = 144, holdSamples = 144, lookahead = 96;
};

//==============================================================================
// The three band compressors in one 4-lane loop (NEON): the same operations in the same order as
// BandCompressor::process (separate multiplies and adds, no fusing), so the gains are bit-identical,
// at about a third of the cost. The 30-sample target update stays scalar (log10f / exp10f per band).
// Requires the three to share their update interval and count (they always do in the chain).
struct CompressorTrio
{
    static void run (BandCompressor& c0, BandCompressor& c1, BandCompressor& c2,
                     const float* p0, const float* p1, const float* p2, float* g0, float* g1, float* g2, int n)
    {
#if defined (__aarch64__)
        BandCompressor* c[3] = { &c0, &c1, &c2 };
        auto lanes = [&] (float BandCompressor::*m) { float v[4] = { c0.*m, c1.*m, c2.*m, c0.*m }; return vld1q_f32 (v); };
        const float32x4_t msc = lanes (&BandCompressor::msCoef), att = lanes (&BandCompressor::att), rel = lanes (&BandCompressor::rel);
        const float32x4_t one = vdupq_n_f32 (1.0f), floorV = vdupq_n_f32 (1.0e-6f);
        const float32x4_t oneMinusMsc = vsubq_f32 (one, msc), oneMinusAtt = vsubq_f32 (one, att), oneMinusRel = vsubq_f32 (one, rel);
        float32x4_t ms = lanes (&BandCompressor::ms), maxp = lanes (&BandCompressor::maxp);
        float32x4_t target = lanes (&BandCompressor::target), gain = lanes (&BandCompressor::gain);
        int32_t hv[4] = { c0.hold, c1.hold, c2.hold, c0.hold }, hn[4] = { c0.holdSamples, c1.holdSamples, c2.holdSamples, c0.holdSamples };
        int32x4_t hold = vld1q_s32 (hv);
        const int32x4_t holdN = vld1q_s32 (hn), oneI = vdupq_n_s32 (1), zeroI = vdupq_n_s32 (0);
        int counter = c0.counter;
        const int interval = c0.interval;
        for (int i = 0; i < n; ++i)
        {
            float pv[4] = { p0[i], p1[i], p2[i], p0[i] };
            float32x4_t p = vmaxq_f32 (vld1q_f32 (pv), floorV);
            const float32x4_t pp = vmulq_f32 (p, p);
            ms = vaddq_f32 (vmulq_f32 (pp, oneMinusMsc), vmulq_f32 (msc, ms));
            maxp = vmaxq_f32 (maxp, pp);
            if (counter >= interval)
            {
                const float32x4_t level = vaddq_f32 (vmulq_f32 (ms, vdupq_n_f32 (0.8f)), vmulq_f32 (maxp, vdupq_n_f32 (0.199999988f)));
                float lv[4], tv[4];
                vst1q_f32 (lv, level);
                vst1q_f32 (tv, target);
                for (int b = 0; b < 3; ++b)
                {
                    const float over = 10.0f * std::log10 (std::max (lv[b], 1.0e-30f)) - c[b]->threshold;
                    tv[b] = over > 0.0f ? exp10f_ ((over * c[b]->slope) * -0.05f) : 1.0f;
                }
                tv[3] = tv[0];
                target = vld1q_f32 (tv);
                maxp = vdupq_n_f32 (1.0e-6f);
                counter = 0;
            }
            ++counter;
            const uint32x4_t attack = vcgtq_f32 (gain, target);
            const uint32x4_t holding = vcltq_s32 (hold, holdN);
            const float32x4_t gA = vaddq_f32 (vmulq_f32 (gain, att), vmulq_f32 (target, oneMinusAtt));
            const float32x4_t gR = vaddq_f32 (vmulq_f32 (gain, rel), vmulq_f32 (target, oneMinusRel));
            gain = vbslq_f32 (attack, gA, vbslq_f32 (holding, gain, gR));
            hold = vbslq_s32 (attack, zeroI, vbslq_s32 (holding, vaddq_s32 (hold, oneI), hold));
            g0[i] = vgetq_lane_f32 (gain, 0);
            g1[i] = vgetq_lane_f32 (gain, 1);
            g2[i] = vgetq_lane_f32 (gain, 2);
        }
        float v[4];
        int32_t h[4];
        vst1q_s32 (h, hold);
        for (int b = 0; b < 3; ++b)
        {
            vst1q_f32 (v, ms); c[b]->ms = v[b];
            vst1q_f32 (v, maxp); c[b]->maxp = v[b];
            vst1q_f32 (v, target); c[b]->target = v[b];
            vst1q_f32 (v, gain); c[b]->gain = v[b];
            c[b]->hold = h[b];
            c[b]->counter = counter;
        }
#else
        for (int i = 0; i < n; ++i)
        {
            g0[i] = c0.process (p0[i], p0[i]);
            g1[i] = c1.process (p1[i], p1[i]);
            g2[i] = c2.process (p2[i], p2[i]);
        }
#endif
    }
};

//==============================================================================
// Soft clipper on the sum.
//   Stock: the original's law (see stock()).
//   0 dBFS: unity gain, knee from -2 dBFS, ceiling exactly 0 dBFS.
struct SoftClip
{
    // The stock law:
    // -1.4 dB small-signal gain, cubic knee, ceiling 0.998726 from +3.5 dBFS.
    static float stock (float x)
    {
        constexpr float inMax = 1.49619f, pre = 0.66836435f, knee = 0.4f, post = 1.2738854f;
        float u = std::min (std::abs (x), inMax) * pre;
        if (u > knee)
            u = u - std::pow (u - knee, 3.0f); // powf, as the original
        const float y = (x < 0.0f ? -post : post) * u;
        return std::clamp (y, -1.0f, 1.0f);
    }

    // Precision Lite: the stock law branchless (vectorises), the cube as t*t*t (same to -138 dB).
    static float stockFast (float x)
    {
        constexpr float inMax = 1.49619f, pre = 0.66836435f, knee = 0.4f, post = 1.2738854f;
        float u = std::min (std::abs (x), inMax) * pre;
        const float t = std::max (u - knee, 0.0f);
        u = u - t * t * t;
        return std::clamp (std::copysign (post * u, x), -1.0f, 1.0f);
    }

    static float zeroDbfs (float x)
    {
        constexpr float knee = 0.794328f, room = 1.0f - knee, p = 4.3f; // 0 dBFS sine peaks out at -0.27 dBFS
        const float a = std::abs (x);
        if (a <= knee) return x;
        const float u = (a - knee) / room;
        const float y = knee + room * u / std::pow (1.0f + std::pow (u, p), 1.0f / p);
        return x < 0.0f ? -y : y;
    }
};

//==============================================================================
// The original's brickwall limiter (inline in its processCore), per channel:
//   detector |x|, linked: max(own, link x loudest other channel) (link 1.0 by default);
//   peak hold: a new peak sets the target 10^((thr - peak dB)/20) (1 below threshold) and
//   restarts the hold; with no new peak for N samples (and no hold running) the peak resets;
//   attack: a straight ramp from where the gain started towards the target over N - 2 samples;
//   release: after a hold of N samples, one-pole towards the target;
//   output: the audio delayed N - 1 samples x gain (double), clamped at the threshold.
// thr = ceiling - 0.025 dB. N = lookahead in samples. Gains in double, as the original.
class Limiter
{
public:
    void prepare (double sampleRate, int maxLookahead)
    {
        sr = (float) sampleRate;
        bufMax = maxLookahead;
        for (auto& b : buf) b.assign ((size_t) std::max (bufMax, 3), 0.0f);
        setLookahead (96);
        setRelease (150.0f);
        setCeiling (-0.01f);
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        pos = 0;
        for (auto& c : ch) c = Channel {};
    }

    void setLookahead (int samples)
    {
        N = juce::jlimit (3, bufMax, samples);
        step = 1.0 / (double) (N - 2);
        reset();
    }
    int getLookahead() const { return N; }
    void setRelease (float ms) { rel = std::exp (-1000.0f / (std::max (ms, 0.001f) * sr)); }
    void setCeiling (float db)
    {
        thrDb = (db + 0.0f) + -0.025f;
        thrLinD = std::exp ((double) thrDb * 0.05 * 2.302585092994046);
        thrLin = (float) thrLinD;
        thrPow = std::pow (10.0, (double) thrDb / 10.0);
    }
    void setLink (float amount) { link = amount; }
    bool fast = false; // Precision Lite (see stepGain)

    // In place, one sample per channel; returns the lowest gain applied. Off: delay only.
    float process (float* x, int channels, bool on)
    {
        const int next = pos + 1 == N ? 0 : pos + 1;
        if (! on)
        {
            for (int c = 0; c < channels; ++c)
            {
                buf[(size_t) c][(size_t) pos] = x[c];
                x[c] = buf[(size_t) c][(size_t) next];
            }
            pos = next;
            return 1.0f;
        }
        // Detector link 100% (the default): both channels see max(|L|, |R|), so their states are
        // identical from reset on; run one and copy it (same arithmetic, half the work).
        // (link is fixed at 100% in this plugin, so ch[1] is never needed once linked)
        const bool linked = channels == 2 && link == 1.0f;
        if (linked)
            stepGain (x, 1, std::max (std::abs (x[0]), std::abs (x[1])));
        else
            stepGain (x, channels);
        float lowest = 1.0f;
        for (int c = 0; c < channels; ++c)
        {
            auto& s = ch[(size_t) (linked ? 0 : c)];
            auto& b = buf[(size_t) c];
            b[(size_t) pos] = x[c];
            float y = (float) (s.gain * (double) b[(size_t) next]);
            if (y > thrLin) y = thrLin;
            else if (y < -thrLin) y = -thrLin;
            x[c] = y;
            lowest = std::min (lowest, (float) s.gain);
        }
        pos = next;
        return lowest;
    }

    // The detector and gain for one sample (no audio).
    // `linkedOne`: the linked detector value already worked out (one channel, link 100%).
    void stepGain (const float* x, int channels, float linkedOne = -1.0f)
    {
        float det[kMaxCh] {}, linked[kMaxCh] {};
        if (linkedOne >= 0.0f)
            linked[0] = linkedOne;
        else
        {
            for (int c = 0; c < channels; ++c) det[c] = std::abs (x[c]);
            for (int c = 0; c < channels; ++c)
            {
                float others = 0.0f;
                for (int o = 0; o < channels; ++o)
                    if (o != c && others < det[o]) others = det[o];
                others *= link;
                linked[c] = det[c] < others ? others : det[c];
            }
        }

        for (int c = 0; c < channels; ++c)
        {
            auto& s = ch[(size_t) c];
            const float p = std::max (linked[c], 1.0e-6f);
            const float p2 = p * p;
            float count;
            if (p2 > s.peak2)
            {
                s.peak2 = p2;
                // As the original: dB of the peak in double, target 10^((thr - dB) / 20) above the
                // threshold, else 1 (exactly its ops).
                if (fast) // Precision Lite: the same target as ceiling / peak (~1e-7), no log and exp
                    s.target = (double) p > thrLinD ? thrLinD / (double) p : 1.0;
                else
                {
                    const double peakDb = (double) std::log10 (p2) * 10.0;
                    float over = 0.0f;
                    if (peakDb > (double) thrDb)
                        over = (float) ((double) thrDb - peakDb);
                    s.target = std::exp ((double) over * 0.05 * 2.302585092994046);
                }
                count = 0.0f;
            }
            else
            {
                count = s.counter;
                if (count >= (float) N && ! (s.hold != 0 && s.hold < N))
                {
                    s.peak2 = 1.0e-6f;
                    count = 0.0f;
                }
            }
            s.counter = count + 1.0f;

            double g = s.gain;
            const double t = s.target;
            if (std::abs (g - t) > 1.0e-11)
            {
                if (g > t)
                {
                    g -= (s.start - t) * step;
                    g = t > g ? t : g;
                    s.gain = g;
                    s.hold = 0;
                }
                else if (s.hold < N)
                    ++s.hold;
                else
                {
                    g = g * (double) rel + t * (double) (1.0f - rel);
                    s.gain = g;
                    s.start = g;
                }
            }

        }
    }

    // A block in place (x[c][0..n)): the same per-sample steps as process(), without a call per
    // sample; off, a plain delay of N - 1. Returns the lowest gain applied.
    float processBlock (float* const* x, int channels, int n, bool on)
    {
        if (! on)
        {
            int p = pos;
            for (int c = 0; c < channels; ++c)
            {
                float* b = buf[(size_t) c].data();
                float* xc = x[c];
                p = pos;
                for (int i = 0; i < n; ++i)
                {
                    const int nx = p + 1 == N ? 0 : p + 1;
                    b[p] = xc[i];
                    xc[i] = b[nx];
                    p = nx;
                }
            }
            pos = p;
            return 1.0f;
        }
        float lowest = 1.0f;
        float v[kMaxCh];
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < channels; ++c) v[c] = x[c][i];
            lowest = std::min (lowest, process (v, channels, true));
            for (int c = 0; c < channels; ++c) x[c][i] = v[c];
        }
        return lowest;
    }

private:
    struct Channel
    {
        float counter = 0.0f, peak2 = 1.0e-6f;
        int hold = 0;
        double gain = 1.0, target = 1.0, start = 1.0;
    };
    std::array<std::vector<float>, kMaxCh> buf;
    std::array<Channel, kMaxCh> ch {};
    float sr = 48000.0f, rel = 0.0f, thrDb = 0.0f, thrLin = 1.0f, link = 1.0f;
    double thrLinD = 1.0, thrPow = 1.0;
    double step = 1.0 / 94.0;
    int N = 96, bufMax = 481, pos = 0;
};

//==============================================================================
// Loudness meter: BS.1770 K-weighting (two transposed
// direct-form II biquads, double), the squared output summed over the channels, collected into
// 10 ms bins in a ring of 300 (3 s) with a running sum (re-added exactly each time the ring
// wraps). Loudness covers all samples measured, including the bin being filled.
class Loudness
{
public:
    void prepare (double sampleRate)
    {
        const double sr = sampleRate;
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            c = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
            const double a0 = 1.0 + K / Q + K * K;
            c2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        hop = std::max (1, (int) (sr * 0.01));
        reset();
    }

    void reset()
    {
        for (auto& z : state) z.fill (0.0);
        ring.fill (0.0);
        running = acc = 0.0;
        count = idx = filled = 0;
    }

    void push (const float* x, int channels)
    {
        double sq = 0.0;
        for (int ch = 0; ch < channels; ++ch)
        {
            auto& z = state[(size_t) ch];
            const double in = (double) x[ch];
            const double y1 = c[0] * in + z[0];
            z[0] = z[1] + (c[1] * in - y1 * c[3]);
            z[1] = c[2] * in - y1 * c[4];
            const double y2 = y1 * c2[0] + z[2];
            z[2] = z[3] + (y1 * c2[1] - y2 * c2[3]);
            z[3] = y1 * c2[2] - y2 * c2[4];
            sq += y2 * y2;
        }
        acc += sq;
        if (++count < hop)
            return;
        if (filled == kBins)
            running -= ring[(size_t) idx];
        else
            ++filled;
        ring[(size_t) idx] = acc;
        running = acc + running;
        acc = 0.0;
        if (++idx == kBins)
        {
            idx = 0;
            running = 0.0;
            for (int b = 0; b < filled; ++b) running += ring[(size_t) b];
        }
        count = 0;
    }

    double samplesMeasured() const { return (double) filled * (double) hop + (double) count; }

    double getLufs() const
    {
        const double n = samplesMeasured();
        if (n > 0.0)
        {
            const double meanSquare = (running + acc) / n;
            if (meanSquare > 1.0e-20)
                return std::log10 (meanSquare) * 10.0 + -0.691;
        }
        return -200.0;
    }

private:
    static constexpr int kBins = 300;
    std::array<double, 5> c {}, c2 {};
    std::array<std::array<double, 4>, kMaxCh> state {};
    std::array<double, kBins> ring {};
    double running = 0.0, acc = 0.0;
    int hop = 480, count = 0, idx = 0, filled = 0;
};
} // namespace bdr
