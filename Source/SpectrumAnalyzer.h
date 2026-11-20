#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

namespace dynamo
{
// FFT analysis that serves two jobs: it finds resonances for Hunter bands,
// and it publishes a spectrum for the editor to draw.
class SpectrumAnalyzer
{
public:
    static constexpr int kDisplayPoints = 256;
    static constexpr float kDisplayMinHz = 20.0f, kDisplayMaxHz = 20000.0f;

    SpectrumAnalyzer()
    {
        for (auto& d : display) d.store (-120.0f);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int order = sampleRate <= 50000.0 ? 12 : (sampleRate <= 100000.0 ? 13 : 14);
        size = 1 << order;
        hop = size / 4;
        numBins = size / 2 + 1;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) size);
        double wsum = 0.0;
        for (int i = 0; i < size; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) size);
            wsum += window[(size_t) i];
        }
        scale = (float) (2.0 / wsum);
        ring.assign ((size_t) size, 0.0f);
        fftData.assign ((size_t) size * 2, 0.0f);
        fast.assign ((size_t) numBins, 0.0f);
        slow.assign ((size_t) numBins, 0.0f);
        prefix.assign ((size_t) numBins + 1, 0.0);
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        std::fill (fast.begin(), fast.end(), 0.0f);
        std::fill (slow.begin(), slow.end(), 0.0f);
        std::fill (prefix.begin(), prefix.end(), 0.0);
        writePos = 0; hopCount = 0; frames = 0;
    }

    void push (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) writePos] = x[i];
            writePos = (writePos + 1) % size;
            if (++hopCount >= hop)
            {
                hopCount = 0;
                runFrame();
            }
        }
    }

    // Number of frames analysed so far; changes when there is new data.
    uint64_t frameCount() const { return frames; }

    // Finds the most prominent spectral peak between loHz and hiHz, favouring
    // peaks close to currentHz so a band doesn't jump between similar peaks.
    bool findPeak (float loHz, float hiHz, float currentHz, float& outHz, float& outProminenceDb) const
    {
        if (frames < 2) return false;
        const float binHz = (float) sr / (float) size;
        const int kLo = std::max (3, (int) std::floor (loHz / binHz));
        const int kHi = std::min (numBins - 3, (int) std::ceil (hiHz / binHz));
        float bestScore = -1.0e9f, bestProm = 0.0f;
        int bestK = -1;

        for (int k = kLo; k <= kHi; ++k)
        {
            const float p = slow[(size_t) k];
            if (p < 1.0e-9f || p < slow[(size_t) k - 1] || p < slow[(size_t) k + 1]) continue;
            const int a = std::max (1, (int) (k / 1.41421356f));
            const int b = std::min (numBins - 1, (int) (k * 1.41421356f) + 1);
            const double mean = (prefix[(size_t) b + 1] - prefix[(size_t) a]) / (double) (b - a + 1);
            const float prom = 10.0f * std::log10 ((float) (p / std::max (mean, 1.0e-20)));
            const float fk = (float) k * binHz;
            const float score = prom - 4.0f * std::abs (std::log2 (fk / std::max (currentHz, 1.0f)));
            if (score > bestScore)
            {
                bestScore = score;
                bestProm = prom;
                bestK = k;
            }
        }

        if (bestK < 0 || bestProm < 3.0f) return false;

        const float a = toDb (slow[(size_t) bestK - 1]), b = toDb (slow[(size_t) bestK]), c = toDb (slow[(size_t) bestK + 1]);
        const float denom = a - 2.0f * b + c;
        const float delta = std::abs (denom) > 1.0e-6f ? std::clamp (0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
        outHz = ((float) bestK + delta) * binHz;
        outProminenceDb = bestProm;
        return true;
    }

    float getDisplayDb (int i) const { return display[(size_t) i].load (std::memory_order_relaxed); }

    static float displayFrequency (int i)
    {
        return kDisplayMinHz * std::pow (kDisplayMaxHz / kDisplayMinHz, (float) i / (float) (kDisplayPoints - 1));
    }

private:
    static float toDb (float p) { return 10.0f * std::log10 (p + 1.0e-20f); }

    void runFrame()
    {
        for (int i = 0; i < size; ++i)
            fftData[(size_t) i] = ring[(size_t) ((writePos + i) % size)] * window[(size_t) i];
        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

        const bool first = frames == 0;
        for (int k = 0; k < numBins; ++k)
        {
            const float m = fftData[(size_t) k] * scale;
            const float p = m * m;
            fast[(size_t) k] = first ? p : 0.5f * fast[(size_t) k] + 0.5f * p;
            slow[(size_t) k] = first ? p : 0.85f * slow[(size_t) k] + 0.15f * p;
        }

        prefix[0] = 0.0;
        for (int k = 0; k < numBins; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + slow[(size_t) k];

        // RTA points: power averaged over 1/6 octave, interpolated between bins
        // at the low end where bins are wider than that.
        const float binHz = (float) sr / (float) size;
        const float halfBand = std::pow (2.0f, 1.0f / 12.0f);
        for (int i = 0; i < kDisplayPoints; ++i)
        {
            const float f = displayFrequency (i);
            const float k0f = f / halfBand / binHz, k1f = f * halfBand / binHz;
            float p;
            if (k1f - k0f < 2.0f)
            {
                const float kf = std::clamp (f / binHz, 1.0f, (float) numBins - 2.0f);
                const int k = (int) kf;
                const float t = kf - (float) k;
                p = fast[(size_t) k] * (1.0f - t) + fast[(size_t) k + 1] * t;
            }
            else
            {
                const int k0 = std::clamp ((int) std::lround (k0f), 1, numBins - 1);
                const int k1 = std::clamp ((int) std::lround (k1f), k0, numBins - 1);
                double sum = 0.0;
                for (int k = k0; k <= k1; ++k) sum += fast[(size_t) k];
                p = (float) (sum / (k1 - k0 + 1));
            }
            display[(size_t) i].store (toDb (p), std::memory_order_relaxed);
        }
        ++frames;
    }

    double sr = 48000.0;
    int size = 4096, hop = 1024, numBins = 2049;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, ring, fftData, fast, slow;
    std::vector<double> prefix;
    float scale = 1.0f;
    int writePos = 0, hopCount = 0;
    uint64_t frames = 0;
    std::array<std::atomic<float>, kDisplayPoints> display;
};
} // namespace dynamo
