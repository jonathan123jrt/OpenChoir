#pragma once

#include <juce_dsp/juce_dsp.h>
#include <random>
#include "Convolver.h"

namespace openchoir
{

/** Microphone colours. These are tonal/room characters, not captures of real microphones. */
struct MicSpec
{
    const char* name;
    const char* description;
    float lowHz, lowDb;            // low shelf
    float midHz, midDb, midQ;      // presence/peak
    float highHz, highDb;          // high shelf
    float roomDecay, roomMix;      // small room: RT60 seconds, wet amount
};

inline const std::array<MicSpec, 4>& micSpecs()
{
    static const std::array<MicSpec, 4> m {{
        { "Studio LDC",      "Balanced, tightest room. The best place to start.", 120.0f, -1.0f, 4000.0f, +1.5f, 0.9f, 10000.0f, +0.5f, 0.25f, 0.10f },
        { "Bright LDC",      "Brighter and roomier than Studio.",                 150.0f, -1.5f, 5000.0f, +2.0f, 0.8f,  9000.0f, +3.0f, 0.40f, 0.18f },
        { "Small Diaphragm", "Darkest and roomiest. Sits the choir further back.", 200.0f, -2.0f, 2500.0f, -1.0f, 0.7f,  6000.0f, -3.5f, 0.65f, 0.28f },
        { "Vintage LDC",     "Darker and warm, with a roomy sound.",              250.0f, +1.5f, 3500.0f, -1.0f, 0.8f,  8000.0f, -2.5f, 0.50f, 0.22f },
    }};
    return m;
}

/** Decaying-noise room impulse response, energy-normalised, decorrelated per channel. */
inline std::vector<float> makeRoomIR (double sr, float rt60, float preDelayMs, unsigned seed, float lowpassHz)
{
    const int n = (int) (sr * (rt60 * 1.1 + preDelayMs * 0.001)) + 1;
    std::vector<float> ir ((size_t) n, 0.0f);
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    const int pre = (int) (preDelayMs * 0.001 * sr);
    const float k = 6.907755f / (rt60 * (float) sr);   // -60 dB at rt60
    const float lp = std::exp (-2.0f * juce::MathConstants<float>::pi * lowpassHz / (float) sr);
    float s1 = 0, s2 = 0;
    for (int i = pre; i < n; ++i)
    {
        float v = nd (rng) * std::exp (-k * (float) (i - pre));
        s1 = (1 - lp) * v + lp * s1;            // two one-pole low-passes: darker tail
        s2 = (1 - lp) * s1 + lp * s2;
        ir[(size_t) i] = s2;
    }
    // a few early reflections
    const float er[][2] = { { 7.0f, 0.5f }, { 13.0f, 0.4f }, { 19.0f, 0.33f }, { 29.0f, 0.25f } };
    std::uniform_real_distribution<float> u (0.6f, 1.0f);
    for (auto& e : er)
    {
        const int idx = pre + (int) (e[0] * 0.001 * sr);
        if (idx < n) ir[(size_t) idx] += e[1] * u (rng) * 0.05f;
    }
    double energy = 0;
    for (auto v : ir) energy += (double) v * v;
    const float g = energy > 0 ? (float) (1.0 / std::sqrt (energy)) : 1.0f;
    for (auto& v : ir) v *= g;
    return ir;
}

struct FXParams
{
    int   mic = 0;            // index into micSpecs()
    float loCut = 0.0f;       // 0..1  -> 50 Hz .. 800 Hz
    float brightness = 0.0f;  // 0..1  -> 0 .. +12 dB high shelf at 3 kHz, Q 1
    float reverb = 0.25f;     // 0..1  dry .. reverb only (1.5 s decay)
    float outputDb = 0.0f;
};

/** Mic colour -> Lo-Cut -> Brightness -> Reverb -> output. Stereo, deterministic,
    used both for live preview and for exporting the files you drag into the DAW. */
class FXChain
{
public:
    static constexpr float reverbSeconds = 1.5f;
    static constexpr int   convBlock = 512;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        scratch.setSize (2, juce::jmax (maxBlock, 16));
        room.setSize (2, juce::jmax (maxBlock, 16));
        for (int ch = 0; ch < 2; ++ch)
        {
            for (size_t m = 0; m < micSpecs().size(); ++m)
            {
                const auto& s = micSpecs()[m];
                roomConv[m][(size_t) ch].setup (makeRoomIR (sr, s.roomDecay, 4.0f, 1000u + (unsigned) m * 10 + (unsigned) ch, 7000.0f), convBlock);
            }
            verbConv[(size_t) ch].setup (makeRoomIR (sr, reverbSeconds, 18.0f, 77u + (unsigned) ch, 6000.0f), convBlock);
        }
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (maxBlock, 16), 1 };
        for (auto* f : allFilters()) { f->prepare (spec); f->reset(); }
        last = {}; last.mic = -1; last.loCut = -1; last.brightness = -1;
        currentMic = -1;
        updateFilters (FXParams{});
    }

    void reset()
    {
        for (auto* f : allFilters()) f->reset();
        for (auto& mc : roomConv) for (auto& c : mc) c.reset();
        for (auto& c : verbConv) c.reset();
    }

    /** buffer: 2 channels, processed in place. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples, const FXParams& p)
    {
        updateFilters (p);
        jassert (buffer.getNumChannels() >= 2);
        if (scratch.getNumSamples() < numSamples) { scratch.setSize (2, numSamples, false, false, true); room.setSize (2, numSamples, false, false, true); }

        const auto& spec = micSpecs()[(size_t) currentMic];
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            // --- mic colour: EQ + small room
            for (int i = 0; i < numSamples; ++i)
            {
                float v = d[i];
                v = lowShelf[(size_t) ch].processSample (v);
                v = peak[(size_t) ch].processSample (v);
                v = highShelf[(size_t) ch].processSample (v);
                d[i] = v;
            }
            float* r = room.getWritePointer (ch);
            std::copy (d, d + numSamples, r);
            roomConv[(size_t) currentMic][(size_t) ch].process (r, numSamples);
            juce::FloatVectorOperations::addWithMultiply (d, r, spec.roomMix, numSamples);

            // --- lo-cut (24 dB/oct) and brightness
            for (int i = 0; i < numSamples; ++i)
            {
                float v = hp1[(size_t) ch].processSample (d[i]);
                v = hp2[(size_t) ch].processSample (v);
                d[i] = bright[(size_t) ch].processSample (v);
            }

            // --- reverb: dry .. reverb only
            float* w = scratch.getWritePointer (ch);
            std::copy (d, d + numSamples, w);
            verbConv[(size_t) ch].process (w, numSamples);
            const float wet = juce::jlimit (0.0f, 1.0f, p.reverb);
            const float dryG = std::cos (wet * juce::MathConstants<float>::halfPi);
            const float wetG = std::sin (wet * juce::MathConstants<float>::halfPi) * 0.8f;
            juce::FloatVectorOperations::multiply (d, dryG, numSamples);
            juce::FloatVectorOperations::addWithMultiply (d, w, wetG, numSamples);

            juce::FloatVectorOperations::multiply (d, juce::Decibels::decibelsToGain (p.outputDb), numSamples);
        }
    }

    static float loCutHz (float knob) { return 50.0f * std::pow (16.0f, juce::jlimit (0.0f, 1.0f, knob)); }   // 50..800 Hz
    static float brightnessDb (float knob) { return 12.0f * juce::jlimit (0.0f, 1.0f, knob); }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coefs  = juce::dsp::IIR::Coefficients<float>;

    std::vector<Filter*> allFilters()
    {
        std::vector<Filter*> v;
        for (int ch = 0; ch < 2; ++ch)
            for (auto* f : { &lowShelf[(size_t) ch], &peak[(size_t) ch], &highShelf[(size_t) ch], &hp1[(size_t) ch], &hp2[(size_t) ch], &bright[(size_t) ch] })
                v.push_back (f);
        return v;
    }

    void updateFilters (const FXParams& p)
    {
        const int mic = juce::jlimit (0, (int) micSpecs().size() - 1, p.mic);
        if (mic != currentMic)
        {
            const auto& s = micSpecs()[(size_t) mic];
            auto ls = Coefs::makeLowShelf (sr, s.lowHz, 0.707f, juce::Decibels::decibelsToGain (s.lowDb));
            auto pk = Coefs::makePeakFilter (sr, s.midHz, s.midQ, juce::Decibels::decibelsToGain (s.midDb));
            auto hs = Coefs::makeHighShelf (sr, s.highHz, 0.707f, juce::Decibels::decibelsToGain (s.highDb));
            for (int ch = 0; ch < 2; ++ch)
            {
                *lowShelf[(size_t) ch].coefficients = *ls;
                *peak[(size_t) ch].coefficients = *pk;
                *highShelf[(size_t) ch].coefficients = *hs;
                if (currentMic >= 0) roomConv[(size_t) mic][(size_t) ch].reset();
            }
            currentMic = mic;
        }
        if (! juce::approximatelyEqual (p.loCut, last.loCut))
        {
            const float fc = loCutHz (p.loCut);
            // 4th-order Butterworth = two biquads with Q 0.541 and 1.307
            auto a = Coefs::makeHighPass (sr, fc, 0.5412f);
            auto b = Coefs::makeHighPass (sr, fc, 1.3066f);
            for (int ch = 0; ch < 2; ++ch) { *hp1[(size_t) ch].coefficients = *a; *hp2[(size_t) ch].coefficients = *b; }
            last.loCut = p.loCut;
        }
        if (! juce::approximatelyEqual (p.brightness, last.brightness))
        {
            auto c = Coefs::makeHighShelf (sr, 3000.0f, 1.0f, juce::Decibels::decibelsToGain (brightnessDb (p.brightness)));
            for (int ch = 0; ch < 2; ++ch) *bright[(size_t) ch].coefficients = *c;
            last.brightness = p.brightness;
        }
    }

    double sr = 44100.0;
    int currentMic = -1;
    FXParams last;
    std::array<Filter, 2> lowShelf, peak, highShelf, hp1, hp2, bright;
    std::array<std::array<UniformConvolver, 2>, 4> roomConv;
    std::array<UniformConvolver, 2> verbConv;
    juce::AudioBuffer<float> scratch, room;
};

} // namespace openchoir
