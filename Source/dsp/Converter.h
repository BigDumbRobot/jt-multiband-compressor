#pragma once

#include <juce_core/juce_core.h>
#include "Chain.h" // (bench timers)
#if JUCE_MAC || JUCE_IOS
 #include <Accelerate/Accelerate.h>
#endif
#if defined (__aarch64__)
 #include <arm_neon.h>
#endif

#include <array>
#include <type_traits>
#include <cmath>
#include <vector>

namespace bdr
{
// Dot product with eight independent partial sums, so the multiply-adds run in parallel instead
// of each waiting for the last (the converters are long FIR filters). Summation order differs from
// a single running sum by rounding only (~1e-16).
inline float dotf (const float* a, const float* b, int n)
{
#if JUCE_MAC || JUCE_IOS
    float r = 0.0f;
    vDSP_dotpr (a, 1, b, 1, &r, (vDSP_Length) n);
    return r;
#else
    float s[8] {};
    int j = 0;
    for (; j + 8 <= n; j += 8)
        for (int k = 0; k < 8; ++k)
            s[k] += a[j + k] * b[j + k];
    float acc = ((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7]));
    for (; j < n; ++j) acc += a[j] * b[j];
    return acc;
#endif
}

// The original's dot product, in its exact summation order (its compiler vectorised the loop):
// eight partial sums S0..S7 over elements j = k mod 8 for every whole group of eight, the leftover
// elements added to S0 in turn, then ((S5 + S4) + (S7 + S6)) + ((S3 + S2) + (S1 + S0)).
// Same arithmetic as the original to the last bit, and still vectorised.
inline double dot (const double* a, const double* b, int n)
{
    if (n < 8)
    {
        double s = 0.0;
        for (int j = 0; j < n; ++j) s = s + a[j] * b[j];
        return s;
    }
    double S[8];
    int j = 0;
#if defined (__aarch64__)
    float64x2_t s01 = vdupq_n_f64 (0.0), s23 = s01, s45 = s01, s67 = s01;
    for (; j + 8 <= n; j += 8)
    {
        s01 = vaddq_f64 (s01, vmulq_f64 (vld1q_f64 (a + j), vld1q_f64 (b + j)));
        s23 = vaddq_f64 (s23, vmulq_f64 (vld1q_f64 (a + j + 2), vld1q_f64 (b + j + 2)));
        s45 = vaddq_f64 (s45, vmulq_f64 (vld1q_f64 (a + j + 4), vld1q_f64 (b + j + 4)));
        s67 = vaddq_f64 (s67, vmulq_f64 (vld1q_f64 (a + j + 6), vld1q_f64 (b + j + 6)));
    }
    vst1q_f64 (S, s01); vst1q_f64 (S + 2, s23); vst1q_f64 (S + 4, s45); vst1q_f64 (S + 6, s67);
#else
    for (int k = 0; k < 8; ++k) S[k] = 0.0;
    for (; j + 8 <= n; j += 8)
        for (int k = 0; k < 8; ++k)
            S[k] = S[k] + a[j + k] * b[j + k];
#endif
    double t = S[0];
    for (; j < n; ++j) t = t + a[j] * b[j];
    return ((S[5] + S[4]) + (S[7] + S[6])) + ((S[3] + S[2]) + (S[1] + t));
}

// Two of the dot products above at once, sharing loads: (a0 . b0) and (a1 . b1). Each keeps the
// original's exact order; doing them together just halves the memory traffic when a0 == a1
// (one prototype, two windows) or b0 == b1 (two branches, one window).
inline void dot2 (const double* a0, const double* b0, const double* a1, const double* b1, int n, double& r0, double& r1)
{
#if defined (__aarch64__)
    if (n >= 8)
    {
        float64x2_t p0 = vdupq_n_f64 (0.0), p1 = p0, p2 = p0, p3 = p0, q0 = p0, q1 = p0, q2 = p0, q3 = p0;
        int j = 0;
        for (; j + 8 <= n; j += 8)
        {
            p0 = vaddq_f64 (p0, vmulq_f64 (vld1q_f64 (a0 + j), vld1q_f64 (b0 + j)));
            q0 = vaddq_f64 (q0, vmulq_f64 (vld1q_f64 (a1 + j), vld1q_f64 (b1 + j)));
            p1 = vaddq_f64 (p1, vmulq_f64 (vld1q_f64 (a0 + j + 2), vld1q_f64 (b0 + j + 2)));
            q1 = vaddq_f64 (q1, vmulq_f64 (vld1q_f64 (a1 + j + 2), vld1q_f64 (b1 + j + 2)));
            p2 = vaddq_f64 (p2, vmulq_f64 (vld1q_f64 (a0 + j + 4), vld1q_f64 (b0 + j + 4)));
            q2 = vaddq_f64 (q2, vmulq_f64 (vld1q_f64 (a1 + j + 4), vld1q_f64 (b1 + j + 4)));
            p3 = vaddq_f64 (p3, vmulq_f64 (vld1q_f64 (a0 + j + 6), vld1q_f64 (b0 + j + 6)));
            q3 = vaddq_f64 (q3, vmulq_f64 (vld1q_f64 (a1 + j + 6), vld1q_f64 (b1 + j + 6)));
        }
        double S[8], T[8];
        vst1q_f64 (S, p0); vst1q_f64 (S + 2, p1); vst1q_f64 (S + 4, p2); vst1q_f64 (S + 6, p3);
        vst1q_f64 (T, q0); vst1q_f64 (T + 2, q1); vst1q_f64 (T + 4, q2); vst1q_f64 (T + 6, q3);
        double s0 = S[0], t0 = T[0];
        for (int k = j; k < n; ++k) { s0 = s0 + a0[k] * b0[k]; t0 = t0 + a1[k] * b1[k]; }
        r0 = ((S[5] + S[4]) + (S[7] + S[6])) + ((S[3] + S[2]) + (S[1] + s0));
        r1 = ((T[5] + T[4]) + (T[7] + T[6])) + ((T[3] + T[2]) + (T[1] + t0));
        return;
    }
#endif
    r0 = dot (a0, b0, n);
    r1 = dot (a1, b1, n);
}

// The 32x sample-rate converter.
//
// Two stages, 2x then 16x. Each is a Kaiser-windowed sinc prototype (beta 20, cutoff halfway
// between the pass and stop edges, length R x taps + 1, unity DC), split into R polyphase branches
// of taps + 1 coefficients, branch r taking prototype taps j x R - r, each branch normalised to
// unity DC. All in double.
//   Up:   each input sample enters a history of taps + 1; branch r of the output = branch r . history.
//   Down: each group of R samples enters a history of the prototype's length; one output per group,
//         prototype . history, taken when the group's first sample has entered.
// One polyphase stage of the converter: a Kaiser-windowed sinc prototype (cutoff halfway
// between the pass and stop edges, length R x taps + 1, unity DC), split into R branches of taps + 1
// coefficients, branch r taking prototype taps j x R - r, each normalised to unity DC. Designed in
// double. T = double: the original's exact arithmetic. T = float: Precision Lite's (vDSP sums).
// Block versions of the original's per-sample ring buffers (same windows, same sums): a linear
// buffer holds the history followed by the block.
template <typename T>
struct PolyStage
{
    int R = 2, P = 1025, L = 2049;
    std::vector<T> poly, proto;                       // designed in double, as the original (stored as T)
    std::vector<T> polyT;                             // float only: [tap][branch]
    struct Hist { std::vector<T> up, down; };
    std::array<Hist, 2> hist;
    std::vector<T> buf;

    static double i0 (double half)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 400; ++k)
        {
            term *= half / k;
            const double t2 = term * term;
            sum += t2;
            if (t2 < sum * 1.0e-21) break;
        }
        return sum;
    }

    void design (int ratio, int taps, double pass, double stop, double beta = 20.0)
    {
        R = ratio;
        P = taps + 1;
        L = ratio * taps + 1;
        const int M = (L - 1) / 2;
        const double fc = (pass + stop) * 0.5, half = beta * 0.5, i0b = i0 (half);
        std::vector<double> pd ((size_t) L, 0.0);
        for (int n = 0; n <= M; ++n)
        {
            const double r = (double) n / M;
            const double w = i0 (half * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            const double x = 2.0 * fc * n;
            const double sinc = std::abs (x) < 1.0e-30 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
            pd[(size_t) (M + n)] = pd[(size_t) (M - n)] = 2.0 * fc * sinc * w;
        }
        const std::vector<double> raw = pd; // the branches come from the raw prototype, as the original's
        double sum = 0.0;
        for (auto v : pd) sum += v;
        for (auto& v : pd) v /= sum;
        proto.assign (pd.begin(), pd.end());

        std::vector<double> pl ((size_t) (R * P), 0.0);
        for (int r = 0; r < R; ++r)
        {
            double bs = 0.0;
            for (int j = 0; j < P; ++j)
                if (const int k = j * R - r; k >= 0 && k < L)
                    bs += raw[(size_t) k];
            for (int j = 0; j < P; ++j)
                if (const int k = j * R - r; k >= 0 && k < L)
                    pl[(size_t) (r * P + j)] = raw[(size_t) k] / bs;
        }
        poly.assign (pl.begin(), pl.end());
        if constexpr (std::is_same_v<T, float>)
        {
            polyT.assign ((size_t) (R * P), T (0)); // [tap][branch], for upBlock's all-branches loop
            for (int r = 0; r < R; ++r)
                for (int j = 0; j < P; ++j)
                    polyT[(size_t) (j * R + r)] = poly[(size_t) (r * P + j)];
        }
    }

    void allocate (int numChannels, int maxIn)
    {
        for (int c = 0; c < numChannels; ++c)
        {
            hist[(size_t) c].up.assign ((size_t) (P - 1), T (0));
            hist[(size_t) c].down.assign ((size_t) (L - 1), T (0));
        }
        buf.assign ((size_t) (std::max (P, L) + maxIn * R + 8), T (0));
    }

    void reset()
    {
        for (auto& h : hist)
        {
            std::fill (h.up.begin(), h.up.end(), T (0));
            std::fill (h.down.begin(), h.down.end(), T (0));
        }
    }

    // n inputs -> n x R outputs. Output (i, r) = branch r . (P samples ending with input i).
    void upBlock (int c, const T* in, int n, T* out)
    {
        auto& h = hist[(size_t) c].up;
        std::copy (h.begin(), h.end(), buf.begin());
        std::copy (in, in + n, buf.begin() + (P - 1));
        for (int i = 0; i < n; ++i)
        {
            const T* w = buf.data() + i;
            T* o = out + i * R;
            if constexpr (std::is_same_v<T, double>)
            {
                int r = 0;
                for (; r + 2 <= R; r += 2)
                    dot2 (poly.data() + r * P, w, poly.data() + (r + 1) * P, w, P, o[r], o[r + 1]);
                for (; r < R; ++r) o[r] = dot (poly.data() + r * P, w, P);
            }
            else if (R == 16 && ! polyT.empty())
            {
               #if defined (__aarch64__)
                // All 16 branches at once: tap-major coefficients, 16 running sums in 4 registers.
                float32x4_t s0 = vdupq_n_f32 (0.0f), s1 = s0, s2 = s0, s3 = s0;
                const float* k = polyT.data();
                for (int j = 0; j < P; ++j, k += 16)
                {
                    const float32x4_t wj = vdupq_n_f32 (w[j]);
                    s0 = vfmaq_f32 (s0, vld1q_f32 (k), wj);
                    s1 = vfmaq_f32 (s1, vld1q_f32 (k + 4), wj);
                    s2 = vfmaq_f32 (s2, vld1q_f32 (k + 8), wj);
                    s3 = vfmaq_f32 (s3, vld1q_f32 (k + 12), wj);
                }
                vst1q_f32 (o, s0); vst1q_f32 (o + 4, s1); vst1q_f32 (o + 8, s2); vst1q_f32 (o + 12, s3);
               #else
                for (int r = 0; r < R; ++r) o[r] = dotf (poly.data() + r * P, w, P);
               #endif
            }
            else
                for (int r = 0; r < R; ++r) o[r] = dotf (poly.data() + r * P, w, P);
        }
        std::copy (buf.begin() + n, buf.begin() + n + (P - 1), h.begin());
    }

    // n x R inputs -> n outputs. Output k = prototype . (L samples ending with input k x R).
    void downBlock (int c, const T* in, int n, T* out)
    {
        auto& h = hist[(size_t) c].down;
        std::copy (h.begin(), h.end(), buf.begin());
        std::copy (in, in + n * R, buf.begin() + (L - 1));
        if constexpr (std::is_same_v<T, double>)
        {
            int k = 0;
            for (; k + 2 <= n; k += 2)
                dot2 (proto.data(), buf.data() + k * R, proto.data(), buf.data() + (k + 1) * R, L, out[k], out[k + 1]);
            for (; k < n; ++k)
                out[k] = dot (proto.data(), buf.data() + k * R, L);
        }
        else
            for (int k = 0; k < n; ++k)
                out[k] = dotf (proto.data(), buf.data() + k * R, L);
        std::copy (buf.begin() + n * R, buf.begin() + n * R + (L - 1), h.begin());
    }
};

class Converter32
{
public:
    static constexpr int kFactor = 32;

    void prepare (int numChannels, int maxBlock)
    {
        channels = juce::jlimit (1, 2, numChannels);
        mid.assign ((size_t) (2 * maxBlock), 0.0);
        wide.assign ((size_t) (32 * maxBlock), 0.0);
        in64.assign ((size_t) maxBlock, 0.0);
        stages[0].design (2, 1024, 0.24375, 0.25);
        stages[1].design (16, 32, 0.015625, 0.046875);
        stages[0].allocate (channels, maxBlock * 2);
        stages[1].allocate (channels, maxBlock * 2);
        reset();
    }

    void reset()
    {
        for (auto& s : stages) s.reset();
    }

    // n base-rate samples -> n x 32 samples at 32x (double inside, as the original).
    void up (int c, const float* in, int n, float* out)
    {
        for (int i = 0; i < n; ++i) in64[(size_t) i] = (double) in[i];
        BDR_T0 (u0)
        stages[0].upBlock (c, in64.data(), n, mid.data());
        BDR_T1 (u0, 5)
        stages[1].upBlock (c, mid.data(), n * 2, wide.data());
        for (int i = 0; i < n * 32; ++i) out[i] = (float) wide[(size_t) i];
    }

    // n x 32 samples at 32x -> n base-rate samples.
    void down (int c, const float* in, int n, float* out)
    {
        for (int i = 0; i < n * 32; ++i) wide[(size_t) i] = (double) in[i];
        stages[1].downBlock (c, wide.data(), n * 2, mid.data());
        BDR_T0 (d0)
        stages[0].downBlock (c, mid.data(), n, in64.data());
        BDR_T1 (d0, 6)
        for (int i = 0; i < n; ++i) out[i] = (float) in64[(size_t) i];
    }

    // The designed coefficients (for the checks): stage 0 = 2x, 1 = 16x.
    const std::vector<double>& prototype (int stage) const { return stages[(size_t) stage].proto; }
    const std::vector<double>& branches (int stage) const { return stages[(size_t) stage].poly; }

private:
    std::array<PolyStage<double>, 2> stages;
    std::vector<double> mid, wide, in64;
    int channels = 2;
};

#if JUCE_MAC || JUCE_IOS
// FFT correlation (overlap-save, float, vDSP): y_f[i] = sum_j h_f[j] x[i + j - (P - 1)] for several
// filters h_f of P taps, i.e. the same windows and coefficients as the direct form, at a fraction of
// the cost for long filters (rounding ~1e-7 instead of the exact sums).
class FftCorrelator
{
public:
    // `inputs` input streams (each with its own history), `filters` filters per input; prepare
    // with all coefficient sets as filters[input][filter][tap].
    void prepare (int blockSize, int taps, const std::vector<std::vector<std::vector<double>>>& h, int numChannels)
    {
        n = blockSize; P = taps;
        log2N = 1;
        while ((1 << log2N) < n + P - 1) ++log2N;
        N = 1 << log2N;
        if (setup != nullptr) vDSP_destroy_fftsetup (setup);
        setup = vDSP_create_fftsetup ((vDSP_Length) log2N, kFFTRadix2);
        inputs = (int) h.size();
        filters = (int) h[0].size();
        spec.assign ((size_t) (inputs * filters), {});
        std::vector<float> g ((size_t) N);
        for (int in = 0; in < inputs; ++in)
            for (int f = 0; f < filters; ++f)
            {
                std::fill (g.begin(), g.end(), 0.0f);
                const auto& hf = h[(size_t) in][(size_t) f];
                for (int m = 0; m < P; ++m) // correlation -> convolution with the reversed taps
                    g[(size_t) m] = m < (int) hf.size() ? (float) hf[(size_t) (P - 1 - m)] : 0.0f;
                auto& sp = spec[(size_t) (in * filters + f)];
                sp.assign ((size_t) N, 0.0f);
                forward (g.data(), sp.data());
                const float scale = 1.0f / (4.0f * (float) N); // vDSP: forward x2 each, inverse xN
                for (auto& v : sp) v *= scale;
            }
        hist.assign ((size_t) (numChannels * inputs), std::vector<float> ((size_t) (N - n), 0.0f));
        frame.assign ((size_t) N, 0.0f);
        X.assign ((size_t) (inputs), std::vector<float> ((size_t) N, 0.0f));
        Y.assign ((size_t) N, 0.0f);
        tmp.assign ((size_t) N, 0.0f);
        y.assign ((size_t) N, 0.0f);
    }
    ~FftCorrelator() { if (setup != nullptr) vDSP_destroy_fftsetup (setup); }

    void reset() { for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f); }

    // Input blocks in[input] (n samples each, channel c). sumFilters: one output, the sum over inputs
    // of filter 0 (the down stage); otherwise, per filter, out[f] from input 0 (the up stage).
    void process (int c, const float* const* in, float* const* out, bool sumFilters)
    {
        for (int k = 0; k < inputs; ++k)
        {
            auto& h = hist[(size_t) (c * inputs + k)];
            std::copy (h.begin(), h.end(), frame.begin());
            std::copy (in[k], in[k] + n, frame.begin() + (N - n));
            std::copy (frame.begin() + n, frame.end(), h.begin());
            forward (frame.data(), X[(size_t) k].data());
        }
        if (sumFilters)
        {
            std::fill (Y.begin(), Y.end(), 0.0f);
            for (int k = 0; k < inputs; ++k)
            {
                mul (X[(size_t) k].data(), spec[(size_t) (k * filters)].data(), tmp.data());
                vDSP_vadd (Y.data(), 1, tmp.data(), 1, Y.data(), 1, (vDSP_Length) N);
            }
            inverse (Y.data(), y.data());
            std::copy (y.begin() + (N - n), y.end(), out[0]);
        }
        else
            for (int f = 0; f < filters; ++f)
            {
                mul (X[0].data(), spec[(size_t) f].data(), Y.data());
                inverse (Y.data(), y.data());
                std::copy (y.begin() + (N - n), y.end(), out[f]);
            }
    }

private:
    // Packed real spectra: [0..N/2) real parts, [N/2..N) imaginary parts; element 0 carries DC (re)
    // and Nyquist (im).
    void forward (const float* x, float* z)
    {
        DSPSplitComplex sc { z, z + N / 2 };
        vDSP_ctoz (reinterpret_cast<const DSPComplex*> (x), 2, &sc, 1, (vDSP_Length) (N / 2));
        vDSP_fft_zrip (setup, &sc, 1, (vDSP_Length) log2N, kFFTDirection_Forward);
    }
    void inverse (float* z, float* x)
    {
        DSPSplitComplex sc { z, z + N / 2 };
        vDSP_fft_zrip (setup, &sc, 1, (vDSP_Length) log2N, kFFTDirection_Inverse);
        vDSP_ztoc (&sc, 1, reinterpret_cast<DSPComplex*> (x), 2, (vDSP_Length) (N / 2));
    }
    void mul (const float* a, const float* b, float* o)
    {
        const float dc = a[0] * b[0], ny = a[N / 2] * b[N / 2];
        DSPSplitComplex A { const_cast<float*> (a), const_cast<float*> (a) + N / 2 };
        DSPSplitComplex B { const_cast<float*> (b), const_cast<float*> (b) + N / 2 };
        DSPSplitComplex O { o, o + N / 2 };
        vDSP_zvmul (&A, 1, &B, 1, &O, 1, (vDSP_Length) (N / 2), 1);
        o[0] = dc;
        o[N / 2] = ny;
    }

    FFTSetup setup = nullptr;
    int n = 512, P = 1025, N = 2048, log2N = 11, inputs = 1, filters = 1;
    std::vector<std::vector<float>> spec, hist, X;
    std::vector<float> frame, Y, tmp, y;
};
#endif

// Precision Lite's converters: the original's two stages with the same coefficients, the 1024-tap
// first stage by FFT correlation in float, the second stage direct in float (all 16 branches in one
// NEON loop). Same timing as the original's (latency 1040); rounding ~1e-7 instead of the
// original's double sums. With the chain's fast mode (Chain::setFast) Lite runs at about half
// Precision's CPU and nulls Precision at -80 to -126 dB.
//
// (Tried and dropped, 2026-11-20: a shorter first stage differs by 4-76 dB at 21-24 kHz, where the
// original is a brickwall flat to 23.4 kHz; the chain at 16x is -56..-74 dB off, mostly because the
// block-wise crossover's seam transients depend on the sample spacing (XoverCheck ratecmp).)
class ConverterLite
{
public:
    void prepare (int numChannels, int maxBlock)
    {
        channels = juce::jlimit (1, 2, numChannels);
        n = maxBlock;
        PolyStage<double> s1;
        s1.design (2, 1024, 0.24375, 0.25); // the original's first stage, exactly
        const int P = s1.P;
        std::vector<std::vector<double>> branches (2, std::vector<double> ((size_t) P));
        for (int r = 0; r < 2; ++r)
            for (int j = 0; j < P; ++j) branches[(size_t) r][(size_t) j] = s1.poly[(size_t) (r * P + j)];
        std::vector<double> even ((size_t) P, 0.0), odd ((size_t) P, 0.0);
        for (int q = 0; q < P; ++q)
        {
            even[(size_t) q] = s1.proto[(size_t) (2 * q)];
            if (2 * q + 1 < s1.L) odd[(size_t) q] = s1.proto[(size_t) (2 * q + 1)];
        }
#if JUCE_MAC || JUCE_IOS
        upFft.prepare (n, P, { branches }, channels);
        downFft.prepare (n, P, { { even }, { odd } }, channels);
#endif
        stage2.design (16, 32, 0.015625, 0.046875);
        stage2.allocate (channels, maxBlock * 2);
        mid.assign ((size_t) (2 * maxBlock), 0.0f);
        a.assign ((size_t) maxBlock, 0.0f);
        b.assign ((size_t) maxBlock, 0.0f);
        reset();
    }
    void reset()
    {
#if JUCE_MAC || JUCE_IOS
        upFft.reset();
        downFft.reset();
#endif
        stage2.reset();
    }

    void up (int c, const float* in, int count, float* out)
    {
        jassert (count == n);
#if JUCE_MAC || JUCE_IOS
        const float* ins[1] = { in };
        float* outs[2] = { a.data(), b.data() };
        upFft.process (c, ins, outs, false);
        for (int i = 0; i < count; ++i) { mid[(size_t) (2 * i)] = a[(size_t) i]; mid[(size_t) (2 * i + 1)] = b[(size_t) i]; }
#endif
        stage2.upBlock (c, mid.data(), count * 2, out);
    }
    void down (int c, const float* in, int count, float* out)
    {
        stage2.downBlock (c, in, count * 2, mid.data());
#if JUCE_MAC || JUCE_IOS
        for (int i = 0; i < count; ++i) { a[(size_t) i] = mid[(size_t) (2 * i)]; b[(size_t) i] = mid[(size_t) (2 * i + 1)]; }
        const float* ins[2] = { a.data(), b.data() };
        float* outs[1] = { out };
        downFft.process (c, ins, outs, true);
#endif
    }

private:
#if JUCE_MAC || JUCE_IOS
    FftCorrelator upFft, downFft;
#endif
    PolyStage<float> stage2;
    std::vector<float> mid, a, b;
    int channels = 2, n = 512;
};
} // namespace bdr
