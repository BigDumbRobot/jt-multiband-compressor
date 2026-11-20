// Crossover design checks, rate comparisons and CPU benchmarks:
//   JTXoverCheck <fc> <fs>                                  print one lowpass design
//   JTXoverCheck run <f1> <f2> <fs> <in.f32> <out.f32>      mono float32 file in; low, mid, high
//                                                               (interleaved float32) out, straight
//                                                               through BlockCrossover (no FIFO)
#include "dsp/Crossover.h"
#include "dsp/Dynamics.h"
#include "dsp/Chain.h"
#include "dsp/Converter.h"
#include <chrono>
#include <random>
#include <cstdio>
#include <cstring>
#include <vector>
int main (int argc, char* argv[])
{
    // bench: time the 32x engine's parts on 10 s of stereo noise at 48 kHz (share of real time)
    if (argc > 1 && std::strcmp (argv[1], "bench") == 0)
    {
        const double fs = 48000.0;
        const int B = 512, n = (int) (fs * (argc > 2 ? std::atof (argv[2]) : 10.0)) / B * B;
        std::mt19937 rng (1);
        std::normal_distribution<float> nd (0.0f, 0.1f);
        std::vector<float> x[2];
        for (auto& c : x) { c.resize ((size_t) n); for (auto& v : c) v = nd (rng); }
        const bool liteMode = std::getenv ("LITE") != nullptr; // LITE=1: Precision Lite's converters and chain
        bdr::Converter32 conv; conv.prepare (2, B);
        bdr::ConverterLite liteConv; liteConv.prepare (2, B);
        bdr::Chain os32, base;
        os32.prepare (fs * 32, 2, B * 32); os32.setLookahead (0.002f); os32.setFast (liteMode);
        base.prepare (fs, 2, B); base.setLookahead (0.002f);
        bdr::ChainSettings s;
        s.threshold = { -24, -24, -24 }; s.ratio = { 4, 4, 4 };
        s.clipOn = std::getenv ("NOCLIP") == nullptr; s.limitOn = std::getenv ("NOLIMIT") == nullptr;
        os32.apply (s); base.apply (s);
        std::vector<float> up[2], o32[2], out[2];
        for (int c = 0; c < 2; ++c) { up[c].resize ((size_t) B * 32); o32[c].resize ((size_t) B * 32); out[c].resize ((size_t) B); }
        double tUp = 0, tChain = 0, tDown = 0, tBase = 0;
        using clk = std::chrono::steady_clock;
        for (int i = 0; i < n; i += B)
        {
            auto t0 = clk::now();
            for (int c = 0; c < 2; ++c)
                liteMode ? liteConv.up (c, x[c].data() + i, B, up[c].data()) : conv.up (c, x[c].data() + i, B, up[c].data());
            auto t1 = clk::now();
            const float* in32[2] = { up[0].data(), up[1].data() }; float* oo[2] = { o32[0].data(), o32[1].data() };
            os32.process (in32, oo);
            auto t2 = clk::now();
            for (int c = 0; c < 2; ++c)
                liteMode ? liteConv.down (c, o32[c].data(), B, out[c].data()) : conv.down (c, o32[c].data(), B, out[c].data());
            auto t3 = clk::now();
            const float* in1[2] = { x[0].data() + i, x[1].data() + i }; float* ob[2] = { out[0].data(), out[1].data() };
            base.process (in1, ob);
            auto t4 = clk::now();
            tUp += std::chrono::duration<double> (t1 - t0).count(); tChain += std::chrono::duration<double> (t2 - t1).count();
            tDown += std::chrono::duration<double> (t3 - t2).count(); tBase += std::chrono::duration<double> (t4 - t3).count();
        }
        const double secs = n / fs;
        // (chain part timers include the classic chain too; it's ~0.5%)
        // chain parts at 32x: crossover alone
        {
            bdr::Crossover3 xo; xo.prepare (fs * 32, 2, B * 32); xo.setFrequencies (270.5f, 1246.5f);
            auto t0 = clk::now();
            for (int i = 0; i < n; i += B) { const float* in32[2] = { up[0].data(), up[1].data() }; xo.process (in32); }
            std::printf ("crossover@32x alone %.1f%%\n", 100 * std::chrono::duration<double> (clk::now() - t0).count() / (n / fs));
            bdr::Limiter lim; lim.prepare (fs * 32, 15361); lim.setLookahead (3072); lim.setCeiling (-6.0f); lim.fast = liteMode;
            float fr[2];
            t0 = clk::now();
            for (int i = 0; i < n * 32; ++i) { fr[0] = up[0][(size_t) (i % (B * 32))]; fr[1] = up[1][(size_t) (i % (B * 32))]; lim.process (fr, 2, true); }
            std::printf ("limiter@32x alone %.1f%%\n", 100 * std::chrono::duration<double> (clk::now() - t0).count() / (n / fs));
            bdr::BandCompressor bc; bc.prepare (fs * 32); bc.setLookahead (0.002f); bc.setCurve (-24, 4); bc.setTimes (10, 100);
            t0 = clk::now();
            float acc = 0;
            for (int i = 0; i < n * 32; ++i) acc += bc.process (std::abs (up[0][(size_t) (i % (B * 32))]), 0.f);
            std::printf ("one band compressor@32x alone %.1f%% (%g)\n", 100 * std::chrono::duration<double> (clk::now() - t0).count() / (n / fs), acc);
        }
        std::printf ("chain parts: crossover %.1f%%  detection %.1f%%  compressors %.1f%%  apply %.1f%%  out(clip+limiter) %.1f%%\n",
                     100 * bdr::benchT[0] / secs, 100 * bdr::benchT[1] / secs, 100 * bdr::benchT[2] / secs, 100 * bdr::benchT[3] / secs, 100 * bdr::benchT[4] / secs);
        std::printf ("converter stage 1 (2x): up %.1f%%  down %.1f%%\n", 100 * bdr::benchT[5] / secs, 100 * bdr::benchT[6] / secs);
        std::printf ("share of real time (stereo 48 kHz): up %.1f%%  chain@32x %.1f%%  down %.1f%%  | classic chain %.2f%%\n",
                     100 * tUp / secs, 100 * tChain / secs, 100 * tDown / secs, 100 * tBase / secs);
        return 0;
    }
    // comp <thr> <ratio> <att> <rel> <fs> <in.f32 stereo interleaved> <out.f32 gains>: the Classic
    // detector + compressor, writing the gain for each sample (applies to audio lookahead - 1 back)
    if (argc > 1 && std::strcmp (argv[1], "comp") == 0)
    {
        const float thr = (float) std::atof (argv[2]), ratio = (float) std::atof (argv[3]);
        const float att = (float) std::atof (argv[4]), rel = (float) std::atof (argv[5]);
        const double fs = std::atof (argv[6]);
        std::FILE* in = std::fopen (argv[7], "rb");
        std::vector<float> x;
        float v;
        while (std::fread (&v, 4, 1, in) == 1) x.push_back (v);
        std::fclose (in);
        bdr::BandCompressor c;
        c.prepare (fs);
        c.setTimes (att, rel);
        c.setCurve (thr, ratio);
        c.setPrecision (false);
        std::FILE* out = std::fopen (argv[8], "wb");
        for (size_t i = 0; i + 1 < x.size(); i += 2)
        {
            const float g = c.process (std::max (std::abs (x[i]), std::abs (x[i + 1])), std::max (std::abs (x[i]), std::abs (x[i + 1])));
            std::fwrite (&g, 4, 1, out);
        }
        std::fclose (out);
        return 0;
    }
    if (argc > 1 && std::strcmp (argv[1], "ratecmp") == 0) // crossover at 32x vs at 16x on the same band-limited signal
    {
        const double flush = argc > 2 ? std::atof (argv[2]) : 1.6667e-5;
        const double f1 = argc > 3 ? std::atof (argv[3]) : 270.0, f2 = argc > 4 ? std::atof (argv[4]) : 1246.0;
        const int blocks = 200, per = argc > 5 ? std::atoi (argv[5]) : 1; // per: base blocks per crossover block
        const int phase = argc > 6 ? std::atoi (argv[6]) : 0;                // 16x samples at 32x positions 2j + phase
        std::mt19937 rng (3);
        std::normal_distribution<float> nd (0.0f, 0.2f);
        bdr::Converter32 cv; cv.prepare (1, 512);
        std::vector<float> x (512), up ((size_t) (512 * 32 * blocks));
        float lp = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            for (auto& v : x) { lp = 0.9f * lp + 0.1f * nd (rng); v = lp + 0.3f * nd (rng); }
            cv.up (0, x.data(), 512, up.data() + (size_t) b * 512 * 32);
        }
        bdr::Crossover3 a, c;
        a.prepare (48000.0 * 32, 1, 512 * 32 * per); c.prepare (48000.0 * 16, 1, 512 * 16 * per);
        a.setFrequencies ((float) f1, (float) f2); c.setFrequencies ((float) f1, (float) f2);
        c.setFlushLevel (flush);
        if (argc > 8) a.setFlushLevel (std::atof (argv[8]));
        const int H = 512 * 16 * per;
        std::vector<float> half ((size_t) H);
        double err[3] {}, sig[3] {};
        for (int b = 0; b < blocks / per; ++b)
        {
            const float* in32 = up.data() + (size_t) b * 2 * H;
            for (int j = 0; j < H; ++j) half[(size_t) j] = in32[2 * j + phase];
            const float* ia[1] = { in32 }; const float* ic[1] = { half.data() };
            a.process (ia); c.process (ic);
            for (int band = 0; band < 3; ++band)
                for (int j = (b == 0 ? H / 2 : 0); j < H; ++j)
                {
                    const double u = a.band (band, 0)[2 * j + phase], v = c.band (band, 0)[j];
                    err[band] += (u - v) * (u - v); sig[band] += u * u;
                }
        }
        for (int band = 0; band < 3; ++band)
            std::printf ("band %d: 16x vs 32x %.1f dB\n", band, 10 * std::log10 (err[band] / sig[band]));
        return 0;
    }
    if (argc > 2 && std::strcmp (argv[1], "osdump") == 0) // osdump <dir>: converter coefficients as raw f64
    {
        bdr::Converter32 cv;
        cv.prepare (2, 512);
        for (int st = 0; st < 2; ++st)
        {
            auto save = [&] (const std::vector<double>& v, const char* name)
            {
                const std::string path = std::string (argv[2]) + "/jt_stage" + std::to_string (st) + "_" + name + ".f64";
                std::FILE* f = std::fopen (path.c_str(), "wb");
                std::fwrite (v.data(), 8, v.size(), f);
                std::fclose (f);
            };
            save (cv.prototype (st), "proto");
            save (cv.branches (st), "poly");
        }
        return 0;
    }
    if (argc > 1 && std::strcmp (argv[1], "run") == 0)
    {
        const double f1 = std::atof (argv[2]), f2 = std::atof (argv[3]), fs = std::atof (argv[4]);
        std::FILE* in = std::fopen (argv[5], "rb");
        std::vector<float> x;
        float v;
        while (std::fread (&v, 4, 1, in) == 1) x.push_back (v);
        std::fclose (in);
        bdr::BlockCrossover xo;
        xo.prepare (512, 1);
        xo.design (f1, f2, fs);
        std::vector<float> lo (512), mi (512), hi (512);
        std::FILE* out = std::fopen (argv[6], "wb");
        for (size_t i = 0; i + 512 <= x.size(); i += 512)
        {
            const float* in[1] = { x.data() + i };
            float* l[1] = { lo.data() }; float* m[1] = { mi.data() }; float* h[1] = { hi.data() };
            xo.process (in, l, m, h, 1);
            for (int k = 0; k < 512; ++k)
            {
                const float t[3] = { lo[(size_t) k], mi[(size_t) k], hi[(size_t) k] };
                std::fwrite (t, 4, 3, out);
            }
        }
        std::fclose (out);
        return 0;
    }
    const double fc = argc > 1 ? std::atof (argv[1]) : 270.0, fs = argc > 2 ? std::atof (argv[2]) : 48000.0;
    double fsb = 0;
    const int n = bdr::ellip::order (fc, fs, fsb);
    const auto lp = bdr::ellip::design (fc, fs);
    std::printf ("FILTER fs=%.1f fc=%.1f order=%d block=512 tail=256 gain=%.17g\n", fs, fc, n, lp.gain);
    for (auto& s : lp.sections)
        std::printf ("  SEC %.17g %.17g %.17g %.17g\n", s[0], s[1], s[2], s[3]);
}
