#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace openchoir
{

/** Quick key estimate for the UI, shown as soon as a vocal is loaded (the AI
    engine re-checks it with its own pitch tracker). YIN pitch per 46 ms frame ->
    pitch-class histogram -> Krumhansl-Kessler key profiles. */
struct KeyEstimate
{
    int tonic = 0;          // 0 = C
    bool minor = false;
    float confidence = 0;   // 0..1
    float lowHz = 0, highHz = 0;   // sung range (10th / 90th percentile)
    bool valid = false;

    juce::String name() const
    {
        static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (n[tonic % 12]) + (minor ? " minor" : " major");
    }
};

inline KeyEstimate estimateKey (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    KeyEstimate k;
    const int decim = juce::jmax (1, (int) std::round (sampleRate / 11025.0));
    const double sr = sampleRate / decim;
    const int n = audio.getNumSamples() / decim;
    if (n < (int) sr) return k;

    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0;
        for (int d = 0; d < decim; ++d)
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                s += audio.getSample (ch, i * decim + d);
        x[(size_t) i] = s / (float) (decim * audio.getNumChannels());
    }

    const int W = 512, hop = 256;
    const int tauMin = (int) (sr / 1000.0), tauMax = (int) (sr / 70.0);
    std::vector<float> d ((size_t) tauMax + 1), f0s;
    std::vector<float> energies;
    for (int s = 0; s + W + tauMax < n; s += hop)
    {
        double e = 0;
        for (int i = 0; i < W; ++i) e += x[(size_t) (s + i)] * x[(size_t) (s + i)];
        energies.push_back ((float) e);
    }
    const float maxE = energies.empty() ? 0.0f : *std::max_element (energies.begin(), energies.end());
    std::array<double, 12> hist {};
    int fi = 0;
    for (int s = 0; s + W + tauMax < n; s += hop, ++fi)
    {
        if (energies[(size_t) fi] < maxE * 0.003f) continue;   // ~ -25 dB gate
        d[0] = 0;
        double run = 0;
        int best = -1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double acc = 0;
            for (int i = 0; i < W; ++i)
            {
                const float df = x[(size_t) (s + i)] - x[(size_t) (s + i + tau)];
                acc += df * df;
            }
            run += acc;
            d[(size_t) tau] = run > 0 ? (float) (acc * tau / run) : 1.0f;
            if (tau >= tauMin && best < 0 && d[(size_t) tau] < 0.15f)
                best = tau;
            else if (best > 0 && tau == best + 1 && d[(size_t) tau] < d[(size_t) best])
                best = tau;
        }
        if (best <= 0) continue;
        while (best + 1 <= tauMax && d[(size_t) best + 1] < d[(size_t) best]) ++best;
        const double f0 = sr / best;
        f0s.push_back ((float) f0);
        const int pc = ((int) std::lround (69.0 + 12.0 * std::log2 (f0 / 440.0)) % 12 + 12) % 12;
        hist[(size_t) pc] += 1.0;
    }
    if (f0s.size() < 20) return k;

    static const double maj[] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    auto corr = [&] (const double* prof, int tonic)
    {
        double mh = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mh += hist[(size_t) i]; mp += prof[i]; }
        mh /= 12; mp /= 12;
        double num = 0, dh = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[(size_t) i] - mh, b = prof[(i - tonic + 12) % 12] - mp;
            num += a * b; dh += a * a; dp += b * b;
        }
        return num / std::sqrt (dh * dp + 1e-12);
    };
    double best = -2, second = -2;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const double r = corr (m ? mnr : maj, t);
            if (r > best) { second = best; best = r; k.tonic = t; k.minor = m == 1; }
            else if (r > second) second = r;
        }
    k.confidence = (float) juce::jlimit (0.0, 1.0, (best - second) * 5.0 + 0.3);
    std::sort (f0s.begin(), f0s.end());
    k.lowHz  = f0s[(size_t) (f0s.size() / 10)];
    k.highHz = f0s[(size_t) (f0s.size() * 9 / 10)];
    k.valid = true;
    return k;
}

} // namespace openchoir
