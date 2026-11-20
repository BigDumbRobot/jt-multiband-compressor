#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#if defined (__aarch64__)
 #include <arm_neon.h>
#endif
#include <cstring>
#include <vector>

// The three-band crossover.
//
// Lowpass: elliptic, 0.3 dB ripple, 30 dB stopband spec, passband edge = the crossover
// frequency. The order is the smallest meeting the spec with the stopband edge at fc + 300 Hz,
// widening that edge x1.1 while the order is above 4; the filter is then designed for that
// order and edge (Orfanidis' Landen-transform method), bilinear with the edge prewarped.
//
// It runs block-wise and (nearly) zero-phase: each block is filtered backwards (reversed, with
// a tail of B/2 zeros whose spill is overlap-added into the previous block) and then forwards,
// so it delays by half a block. Bands are a cascade of two such lowpasses:
//     low = LP1(x),  mid = LP2(x - LP1(x)),  high = (x - LP1(x)) - mid
// summing back to the input delayed by one block.
//
// The block is fixed at 512 samples, fed through an input FIFO, so the sound and latency don't
// depend on the DAW's buffer size: one block of FIFO plus one block of crossover.
namespace bdr
{
namespace ellip
{
// The design, operation for operation in a fixed order (constants as exact bit patterns, complex
// arithmetic spelled out), so the coefficients are reproducible to the last bit.
inline double bits (std::uint64_t u) { double d; std::memcpy (&d, &u, sizeof d); return d; }

constexpr double kPi = 3.14159265358979323846;
inline const double kInvEp = bits (0x400dea0d4d64d252);    // 1 / eps_p (0.3 dB ripple)
inline const double kTwoInvEp = bits (0x401dea0d4d64d252); // 2 / eps_p
inline const double kK1 = bits (0x3f81541308f25efb);       // spec k1 = eps_p / eps_s (30 dB)
inline const double kK1p = bits (0x3fefffb4ee1f0635);      // sqrt(1 - k1^2)
inline const double kGainEven = bits (0x3feee9e3887110cf); // 1 / sqrt(1 + eps_p^2)
inline const double kLandenK1[3] = { bits (0x3ef2c48e4207b870), bits (0x3dd603aeb6e3164e), bits (0x3b9e4a214fe8e474) };

// Complex arithmetic exactly as the libc++ the original was built with (finite values only).
struct C { double re, im; };
inline C mul (C z, C w) { return { z.re * w.re - z.im * w.im, z.im * w.re + z.re * w.im }; }
inline C div (C z, C w)
{
    double c = w.re, d = w.im;
    int ilogbw = 0;
    const double lg = std::logb (std::fmax (std::fabs (c), std::fabs (d)));
    if (std::isfinite (lg))
    {
        ilogbw = (int) lg;
        c = std::scalbn (c, -ilogbw);
        d = std::scalbn (d, -ilogbw);
    }
    const double denom = c * c + d * d;
    return { std::scalbn ((c * z.re + d * z.im) / denom, -ilogbw), std::scalbn ((c * z.im - d * z.re) / denom, -ilogbw) };
}
inline C csqrt (C z)
{
    const double rho = std::sqrt (std::hypot (z.re, z.im));
    const double th = std::atan2 (z.im, z.re) * 0.5;
    return { rho * std::cos (th), rho * std::sin (th) };
}
inline C scale (C z, double s) { return { z.re * s, z.im * s }; }

// Landen sequence: k_{n+1} = (k_n / (sqrt(1 - k_n^2) + 1))^2, stored until one falls below 1e-15.
inline int landen (double k, double (&v)[8])
{
    int n = 0;
    if (! (k >= 1.0e-15)) return 0;
    for (;;)
    {
        const int i = n;
        k = k / (std::sqrt (1.0 - k * k) + 1.0);
        k = k * k;
        v[n++] = k;
        if (i > 6 || ! (k >= 1.0e-15)) return n;
    }
}

// Ascending Landen: w = ((v + 1) w) / ((v w) w + 1), from the last v to the first.
inline C ascend (C w, const double* v, int n)
{
    for (int i = n - 1; i >= 0; --i)
    {
        const C num = scale (w, v[i] + 1.0);
        C den = mul (scale (w, v[i]), w);
        den.re = den.re + 1.0;
        w = div (num, den);
    }
    return w;
}

// cd(u K, k): cos(u pi/2) (libc++ ccos = ccosh(-im, re)), then ascending Landen.
inline C cde (C u, double k)
{
    double v[8];
    const int n = landen (k, v);
    const double b = u.re * (kPi / 2.0), a = -(u.im * (kPi / 2.0));
    C w { std::cos (b) * std::cosh (a), std::sin (b) * std::sinh (a) };
    if (a == 0.0 && b == 0.0) w = { 1.0, 0.0 };
    return ascend (w, v, n);
}

// Arithmetic-geometric mean as the original runs it (59 steps at most, stops below 1e-16).
inline double agmK (double b)
{
    double a = 1.0, an = 0.0;
    for (int cnt = 59;; --cnt)
    {
        an = (a + b) * 0.5;
        if (cnt == 0) break;
        b = std::sqrt (a * b);
        const double d = std::fabs (an - b);
        a = an;
        if (! (d >= 1.0e-16)) break;
    }
    return kPi / (an + an);
}

// Order and stopband edge, as the original chooses them.
inline int order (double fc, double fs, double& fsb)
{
    const double K1p = agmK (kK1), K1 = agmK (kK1p);
    double edge = fc + 300.0;
    int n = 0;
    for (int i = 0; i < 64; ++i)
    {
        const double k = fc / edge;
        const double kp = std::sqrt (1.0 - k * k);
        const double Kk = agmK (kp), Kkp = agmK (std::sqrt (1.0 - kp * kp));
        n = (int) std::ceil ((Kk * K1p) / (Kkp * K1));
        fsb = edge;
        if (n <= 4) break;
        const double next = edge * 1.1;
        if (next >= fs * 0.5 + -1.0) break;
        edge = next;
        fsb = edge;
    }
    return n;
}

struct Lowpass
{
    double gain = 1.0;
    std::vector<std::array<double, 4>> sections; // b1, b2, a1, a2 (b0 = 1)
};

inline Lowpass design (double fc, double fs)
{
    double fsb = 0.0;
    const int N = order (fc, fs, fsb);
    const double wp = std::tan (fc * kPi / fs);
    const double k = wp / std::tan (fsb * kPi / fs);
    const double Nd = (double) N;

    // v0 = -j asne(j / eps_p, k1) / N, asne by descending Landen then (2/pi) asin.
    C w { 0.0, kInvEp }, two { 0.0, kTwoInvEp };
    for (double v : kLandenK1)
    {
        const double v1 = v + 1.0;
        const C t = mul (scale (w, v * 4.0), w);
        const C s = csqrt ({ v1 * v1 - t.re, -t.im });
        w = div (two, { v1 + s.re, s.im });
        two = { w.re + w.re, w.im + w.im };
    }
    const C jw = mul ({ 0.0, 1.0 }, w);
    const C w2 = mul (w, w);
    const C r = csqrt ({ 1.0 - w2.re, 0.0 - w2.im });
    const C z { jw.re + r.re, jw.im + r.im };
    const C lg { std::log (std::hypot (z.re, z.im)), std::atan2 (z.im, z.re) };
    const C mj { -0.0, -1.0 };
    const C as = scale (mul (mj, lg), 2.0 / kPi);
    const double v0 = mul (mj, as).re / Nd;

    auto toZ = [wp] (C s) { return div ({ wp * s.re + 1.0, wp * s.im }, { 1.0 - wp * s.re, -(wp * s.im) }); };
    auto norm = [] (C q) { return q.re * q.re + q.im * q.im; };

    Lowpass lp;
    if (N & 1)
    {
        double v[8];
        const int n = landen (k, v);
        const double x = v0 * -(kPi / 2.0);
        C s { std::cosh (x) * 0.0, -std::sinh (x) };
        s = ascend (s, v, n);
        const C p = toZ (mul ({ 0.0, 1.0 }, s));
        lp.sections.push_back ({ 1.0, 0.0, -p.re, 0.0 }); // zero at z = -1
    }
    for (int i = N / 2; i >= 1; --i)
    {
        const double ui = ((double) i + (double) i + -1.0) / Nd;
        const double d = k * cde ({ ui, 0.0 }, k).re;
        const C zero = toZ ({ 0.0 / d, 1.0 / d });
        const C pole = toZ (mul ({ 0.0, 1.0 }, cde ({ ui, -v0 }, k)));
        lp.sections.push_back ({ zero.re * -2.0, norm (zero), pole.re * -2.0, norm (pole) });
    }
    double dc = 1.0;
    for (auto& s : lp.sections)
        dc = dc * (((s[0] + 1.0) + s[1]) / ((s[2] + 1.0) + s[3]));
    lp.gain = ((N & 1) ? 1.0 : kGainEven) / dc;
    return lp;
}
} // namespace ellip

//==============================================================================
// One block-wise lowpass for up to two channels.
class BlockLowpass
{
public:
    void prepare (int blockSize, int numChannels)
    {
        B = blockSize;
        T = blockSize / 2;
        channels = numChannels;
        for (auto& w : work) w.assign ((size_t) (B + T), 0.0);
        for (auto& c : ch)
            c.saved.assign ((size_t) T, 0.0);
        reset();
    }

    void setFilter (const ellip::Lowpass& f)
    {
        filter = f;
        for (auto& c : ch)
        {
            c.back.assign (filter.sections.size(), {});
            c.fwd.assign (filter.sections.size(), {});
        }
    }

    void reset()
    {
        for (auto& c : ch)
        {
            for (auto& s : c.back) s.fill (0.0);
            for (auto& s : c.fwd) s.fill (0.0);
            std::fill (c.saved.begin(), c.saved.end(), 0.0);
        }
    }

    // B samples in, B samples out (delayed by T), all channels at once: the channels' recursions run
    // interleaved in one loop (independent, so the CPU overlaps them). Same arithmetic per channel.
    void process (const float* const* in, float* const* out)
    {
        // Load reversed, with the gain applied (the original multiplies by it before each pass).
        const double g = filter.gain;
        for (int c = 0; c < channels; ++c)
        {
            auto& w = work[(size_t) c];
            for (int i = 0; i < B; ++i)
                w[(size_t) i] = (double) in[c][B - 1 - i] * g;
            std::fill (w.begin() + B, w.end(), 0.0);
        }
        if (channels == 2 && filter.sections.size() == 2)
            runTwoSectionsStereo (true, B + T, nullptr);
        else
            runAll (true, B + T, nullptr);
        for (int c = 0; c < channels; ++c)
        {
            auto& w = work[(size_t) c];
            auto& st = ch[(size_t) c];
            for (int i = 0; i < T; ++i)
                w[(size_t) (B + i)] += st.saved[(size_t) i];
            std::copy (w.begin(), w.begin() + T, st.saved.begin());
            for (int i = T; i < B + T; ++i)
                w[(size_t) i] *= g;
        }
        // Forward pass: over the backward result read in reverse (no reversal copy), straight to float.
        if (channels == 2 && filter.sections.size() == 2)
            runTwoSectionsStereo (false, B, out);
        else
            runAll (false, B, out);
    }

private:
    using State = std::array<double, 4>; // x1, x2, y1, y2
    static constexpr double kFlush = 1.6666666666666667e-05; // the original zeroes small feedback state per block

    // Each section in turn, the channels' recursions interleaved (independent, so the CPU overlaps
    // them), feed-forward terms summed off the feedback path. Rounding differs from the original's
    // operation order only at ~1e-16; the per-block state flush is kept.
    // backward: in place over work[0..n). forward (out != nullptr): reads work from the end
    // (work[B+T-1-i]), writes out[c][i] as float. Gain already applied to work.
    void runAll (bool backward, int n, float* const* out)
    {
        const int S = (int) filter.sections.size();
        for (int c = 0; c < channels; ++c)
        {
            double* w = work[(size_t) c].data();
            auto& st = backward ? ch[(size_t) c].back : ch[(size_t) c].fwd;
            for (int s2 = 0; s2 < S; ++s2)
            {
                const double k0 = filter.sections[(size_t) s2][0], k1 = filter.sections[(size_t) s2][1];
                const double k2 = filter.sections[(size_t) s2][2], k3 = filter.sections[(size_t) s2][3];
                auto& z = st[(size_t) s2];
                double x1 = z[0], x2 = z[1], y1 = z[2], y2 = z[3];
                const bool last = s2 == S - 1;
                for (int i = 0; i < n; ++i)
                {
                    const int j = backward ? i : B + T - 1 - i;
                    const double x0 = w[j];
                    const double y = x0 + k0 * x1 + k1 * x2 - k2 * y1 - k3 * y2;
                    x2 = x1; x1 = x0; y2 = y1; y1 = y;
                    if (! backward && last) out[c][i] = (float) y;
                    else w[j] = y;
                }
                z = flushed (x1, x2, y1, y2);
            }
        }
    }

    // Both sections, both channels in one loop, all state in locals: four independent recursions
    // for the CPU to overlap. Exactly the original's arithmetic, in its order (the per-block state flush
    // makes even 1-ulp differences matter, so no reassociation or fused multiply-adds here).
    void runTwoSectionsStereo (bool backward, int n, float* const* out)
    {
       #if defined (__aarch64__)
        if (fast) { runTwoSectionsStereoFast (backward, n, out); return; }
       #endif
        const auto& A = filter.sections[0];
        const auto& Bq = filter.sections[1];
        const double a0 = A[0], a1 = A[1], a2 = A[2], a3 = A[3];
        const double b0 = Bq[0], b1 = Bq[1], b2 = Bq[2], b3 = Bq[3];
        auto& sL = backward ? ch[0].back : ch[0].fwd;
        auto& sR = backward ? ch[1].back : ch[1].fwd;
        double l0x1 = sL[0][0], l0x2 = sL[0][1], l0y1 = sL[0][2], l0y2 = sL[0][3];
        double l1x1 = sL[1][0], l1x2 = sL[1][1], l1y1 = sL[1][2], l1y2 = sL[1][3];
        double r0x1 = sR[0][0], r0x2 = sR[0][1], r0y1 = sR[0][2], r0y2 = sR[0][3];
        double r1x1 = sR[1][0], r1x2 = sR[1][1], r1y1 = sR[1][2], r1y2 = sR[1][3];
        double* x = work[0].data();
        double* v = work[1].data();
        auto step = [&] (double xl, double xr, double& zl, double& zr)
        {
            const double yl = xl + a0 * l0x1 + a1 * l0x2 - a2 * l0y1 - a3 * l0y2;
            const double yr = xr + a0 * r0x1 + a1 * r0x2 - a2 * r0y1 - a3 * r0y2;
            l0x2 = l0x1; l0x1 = xl; l0y2 = l0y1; l0y1 = yl;
            r0x2 = r0x1; r0x1 = xr; r0y2 = r0y1; r0y1 = yr;
            zl = yl + b0 * l1x1 + b1 * l1x2 - b2 * l1y1 - b3 * l1y2;
            zr = yr + b0 * r1x1 + b1 * r1x2 - b2 * r1y1 - b3 * r1y2;
            l1x2 = l1x1; l1x1 = yl; l1y2 = l1y1; l1y1 = zl;
            r1x2 = r1x1; r1x1 = yr; r1y2 = r1y1; r1y1 = zr;
        };
        if (backward)
            for (int i = 0; i < n; ++i)
            {
                double zl, zr;
                step (x[i], v[i], zl, zr);
                x[i] = zl;
                v[i] = zr;
            }
        else
        {
            float* oL = out[0];
            float* oR = out[1];
            const int top = B + T - 1;
            for (int i = 0; i < n; ++i)
            {
                double zl, zr;
                step (x[top - i], v[top - i], zl, zr);
                oL[i] = (float) zl;
                oR[i] = (float) zr;
            }
        }
        // The original flushes each section's feedback state at the end of the block.
        sL[0] = flushed (l0x1, l0x2, l0y1, l0y2);
        sL[1] = flushed (l1x1, l1x2, l1y1, l1y2);
        sR[0] = flushed (r0x1, r0x2, r0y1, r0y2);
        sR[1] = flushed (r1x1, r1x2, r1y1, r1y2);
    }

   #if defined (__aarch64__)
    // Precision Lite: the same recursions, both channels in one register, each sample's feedback term
    // fused last (one multiply-add on the recursion's critical path instead of three operations).
    // Rounding differs from the original's order by ~1e-16 per step.
    void runTwoSectionsStereoFast (bool backward, int n, float* const* out)
    {
        const auto& A = filter.sections[0];
        const auto& Bq = filter.sections[1];
        const float64x2_t a0 = vdupq_n_f64 (A[0]), a1 = vdupq_n_f64 (A[1]), na2 = vdupq_n_f64 (-A[2]), na3 = vdupq_n_f64 (-A[3]);
        const float64x2_t b0 = vdupq_n_f64 (Bq[0]), b1 = vdupq_n_f64 (Bq[1]), nb2 = vdupq_n_f64 (-Bq[2]), nb3 = vdupq_n_f64 (-Bq[3]);
        auto& sL = backward ? ch[0].back : ch[0].fwd;
        auto& sR = backward ? ch[1].back : ch[1].fwd;
        auto pair = [] (double l, double r) { return vcombine_f64 (vdup_n_f64 (l), vdup_n_f64 (r)); };
        float64x2_t x1 = pair (sL[0][0], sR[0][0]), x2 = pair (sL[0][1], sR[0][1]), y1 = pair (sL[0][2], sR[0][2]), y2 = pair (sL[0][3], sR[0][3]);
        float64x2_t u1 = pair (sL[1][0], sR[1][0]), u2 = pair (sL[1][1], sR[1][1]), z1 = pair (sL[1][2], sR[1][2]), z2 = pair (sL[1][3], sR[1][3]);
        double* x = work[0].data();
        double* v = work[1].data();
        const int top = B + T - 1;
        for (int i = 0; i < n; ++i)
        {
            const int j = backward ? i : top - i;
            const float64x2_t in = pair (x[j], v[j]);
            const float64x2_t y = vfmaq_f64 (vfmaq_f64 (vfmaq_f64 (vfmaq_f64 (in, a0, x1), a1, x2), na3, y2), na2, y1);
            x2 = x1; x1 = in; y2 = y1; y1 = y;
            const float64x2_t z = vfmaq_f64 (vfmaq_f64 (vfmaq_f64 (vfmaq_f64 (y, b0, u1), b1, u2), nb3, z2), nb2, z1);
            u2 = u1; u1 = y; z2 = z1; z1 = z;
            if (backward) { x[j] = vgetq_lane_f64 (z, 0); v[j] = vgetq_lane_f64 (z, 1); }
            else { out[0][i] = (float) vgetq_lane_f64 (z, 0); out[1][i] = (float) vgetq_lane_f64 (z, 1); }
        }
        sL[0] = flushed (vgetq_lane_f64 (x1, 0), vgetq_lane_f64 (x2, 0), vgetq_lane_f64 (y1, 0), vgetq_lane_f64 (y2, 0));
        sR[0] = flushed (vgetq_lane_f64 (x1, 1), vgetq_lane_f64 (x2, 1), vgetq_lane_f64 (y1, 1), vgetq_lane_f64 (y2, 1));
        sL[1] = flushed (vgetq_lane_f64 (u1, 0), vgetq_lane_f64 (u2, 0), vgetq_lane_f64 (z1, 0), vgetq_lane_f64 (z2, 0));
        sR[1] = flushed (vgetq_lane_f64 (u1, 1), vgetq_lane_f64 (u2, 1), vgetq_lane_f64 (z1, 1), vgetq_lane_f64 (z2, 1));
    }
   #endif

    State flushed (double x1, double x2, double y1, double y2) const
    {
        const double t = flushLevel;
        return { x1, x2, std::abs (y1) < t ? 0.0 : y1, std::abs (y2) < t ? 0.0 : y2 };
    }
public:
    bool fast = false;          // Precision Lite: runTwoSectionsStereoFast
    double flushLevel = kFlush; // the original's (adjustable for experiments: XoverCheck ratecmp)
private:

    struct Channel
    {
        std::vector<State> back, fwd;
        std::vector<double> saved;
    };
    ellip::Lowpass filter;
    std::array<Channel, 2> ch;
    std::array<std::vector<double>, 2> work;
    int B = 512, T = 256, channels = 2;
};

//==============================================================================
// Two lowpasses in cascade -> three bands.
class BlockCrossover
{
public:
    void prepare (int blockSize, int numChannels)
    {
        B = blockSize;
        T = blockSize / 2;
        lp1.prepare (B, numChannels);
        lp2.prepare (B, numChannels);
        for (auto* v : { &l1, &hp1 })
            for (auto& c : *v) c.assign ((size_t) B, 0.0f);
        l1p = { l1[0].data(), l1[1].data() };
        for (auto& c : delays)
            for (auto& d : c)
                d.assign ((size_t) T, 0.0f);
    }

    void design (double f1, double f2, double fs)
    {
        lp1.setFilter (ellip::design (f1, fs));
        lp2.setFilter (ellip::design (f2, fs));
    }

    void reset()
    {
        lp1.reset();
        lp2.reset();
        for (auto& c : delays)
            for (auto& d : c)
                std::fill (d.begin(), d.end(), 0.0f);
    }

    // One block per channel in; low/mid/high per channel out.
    void process (const float* const* in, float* const* low, float* const* midOut, float* const* high, int channels)
    {
        lp1.process (in, l1p.data());
        for (int c = 0; c < channels; ++c)
        {
            auto& d = delays[(size_t) c];
            delay (d[0], in[c], hp1[(size_t) c].data());                 // x delayed by T ...
            for (int i = 0; i < B; ++i) hp1[(size_t) c][(size_t) i] -= l1[(size_t) c][(size_t) i]; // ... minus LP1
            delay (d[1], l1[(size_t) c].data(), low[c]);
        }
        const float* h[2] = { hp1[0].data(), hp1[1].data() };
        lp2.process (h, midOut);
        for (int c = 0; c < channels; ++c)
        {
            delay (delays[(size_t) c][2], hp1[(size_t) c].data(), high[c]);
            for (int i = 0; i < B; ++i) high[c][i] -= midOut[c][i];
        }
    }

    void setFlushLevel (double level) { lp1.flushLevel = level; lp2.flushLevel = level; }
    void setFast (bool f) { lp1.fast = f; lp2.fast = f; }


private:
    void delay (std::vector<float>& buf, const float* in, float* out) const
    {
        for (int i = 0; i < T; ++i) out[i] = buf[(size_t) i];
        for (int i = T; i < B; ++i) out[i] = in[i - T];
        std::copy (in + B - T, in + B, buf.begin());
    }

    BlockLowpass lp1, lp2;
    std::array<std::vector<float>, 2> l1, hp1;
    std::array<float*, 2> l1p {};
    std::array<std::array<std::vector<float>, 3>, 2> delays;
    int B = 512, T = 256;
};

//==============================================================================
// The crossover the processor uses, a block at a time (kBlock samples, the original at a
// 512-sample host buffer; the processor's FIFO feeds it). Bands come out one block later.
// Moving a crossover starts a second crossover with the new filters, warms it up for 40 ms,
// then crossfades to it over 30 ms (as the original), so moves never click.
class Crossover3
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kBands = 3;
    static constexpr int kBlock = 512;

    void setFlushLevel (double level) { for (auto& x : xo) x.setFlushLevel (level); }
    void setFast (bool f) { for (auto& x : xo) x.setFast (f); }


    void prepare (double sampleRate, int numChannels, int blockSize = kBlock)
    {
        sr = sampleRate;
        block = blockSize;
        channels = juce::jlimit (1, kMaxChannels, numChannels);
        for (auto& x : xo) x.prepare (block, channels);
        for (auto& e : bands)
            for (auto& b : e)
                for (auto& c : b) c.assign ((size_t) block, 0.0f);
        warmLength = (int) std::lround (0.04 * sr);
        fadeLength = juce::jmax (1, (int) std::lround (0.03 * sr));
        haveDesign = false;
        reset();
    }

    void reset()
    {
        for (auto& x : xo) x.reset();
        transition = -1;
    }

    int latency() const { return block; } // 2T, T = block / 2

    // The original truncates the crossover frequencies to whole Hz; crossover 2 stays at least
    // half an octave above crossover 1.
    void setFrequencies (float f1, float f2)
    {
        const double nyq = sr * 0.49;
        double a = std::floor (juce::jlimit (20.0, nyq / 1.5, (double) f1));
        double b = std::floor (juce::jlimit (a * 1.41421356, nyq, (double) f2));
        wanted = { a, b };
        if (! haveDesign)
        {
            xo[(size_t) current].design (a, b, sr);
            designed = wanted;
            haveDesign = true;
        }
        else if (transition < 0 && wanted != designed)
            startTransition();
    }

    // One block (blockSize samples per channel) in; band(b, c) holds the block of bands out.
    void process (const float* const* in)
    {
        run (current, in);
        if (transition >= 0) run (1 - current, in);
        if (transition < 0)
            return;
        for (int i = 0; i < block; ++i, ++transition)
            if (transition >= warmLength)
            {
                const float t = juce::jlimit (0.0f, 1.0f, (float) (transition - warmLength) / (float) fadeLength);
                for (int b = 0; b < kBands; ++b)
                    for (int c = 0; c < channels; ++c)
                    {
                        auto& v = bands[(size_t) current][(size_t) b][(size_t) c][(size_t) i];
                        v += t * (bands[(size_t) (1 - current)][(size_t) b][(size_t) c][(size_t) i] - v);
                    }
            }
        if (transition >= warmLength + fadeLength)
        {
            // From the next block the new crossover is the current one (its own output, unblended).
            current = 1 - current;
            transition = -1;
            if (wanted != designed) startTransition(); // a move that arrived meanwhile
        }
    }

    const float* band (int b, int c) const { return bands[(size_t) shown][(size_t) b][(size_t) c].data(); }

private:
    void run (int which, const float* const* in)
    {
        auto& o = bands[(size_t) which];
        float* lo[2] = { o[0][0].data(), o[0][1].data() };
        float* mi[2] = { o[1][0].data(), o[1][1].data() };
        float* hi[2] = { o[2][0].data(), o[2][1].data() };
        xo[(size_t) which].process (in, lo, mi, hi, channels);
        shown = current;
    }

    void startTransition()
    {
        auto& next = xo[(size_t) (1 - current)];
        next.reset();
        next.design (wanted[0], wanted[1], sr);
        designed = wanted;
        transition = 0;
    }

    double sr = 48000.0;
    int channels = 2, block = kBlock, current = 0, shown = 0, transition = -1, warmLength = 1920, fadeLength = 1440;
    bool haveDesign = false;
    std::array<double, 2> wanted {}, designed {};
    std::array<BlockCrossover, 2> xo;
    std::array<std::array<std::array<std::vector<float>, kMaxChannels>, kBands>, 2> bands;
};
} // namespace bdr
