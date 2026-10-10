#pragma once

#include <cmath>
#include <vector>
#include <random>
#include <algorithm>

// OpenChoir DSP engine.
//
// One mono input feeds a shared circular buffer. Each "singer" reads it back
// through its own:
//   * dual-tap rotating-delay pitch shifter (detune, octave layers)
//   * timing offset + slow random drift (humanised entries / sloppiness)
//   * per-singer vibrato (delay modulation at its own rate)
//   * per-singer tone (one-pole low-pass at a slightly different cutoff)
//   * per-singer level wobble and stereo position
// The summed ensemble then goes through optional grit (soft clipping).
// Reverb and dry/wet mixing live in the processor.
//
// Pitch shifter notes (v0.2):
//   A classic two-tap shifter cross-fades continuously with sin^2 windows, so
//   both taps are audible most of the time and the voice is comb-filtered by the
//   tap spacing. For the small detunes a choir needs, the cycle is seconds long,
//   so OpenChoir instead plays ONE tap and only cross-fades (40 ms, equal-gain)
//   around each tap reset. Octave layers, whose cycle is short, fall back to
//   the full cross-fade automatically. The unison delay window is 20 ms, so the
//   shifter itself adds only ~5 ms of lag on average.

namespace openchoir
{

struct EngineParams
{
    int   unisonVoices   = 8;      // 1..16 singers in the main layer
    float detuneCents    = 12.0f;  // spread of per-singer tuning
    float timingMs       = 25.0f;  // spread of per-singer entry delay
    float vibrato        = 0.35f;  // 0..1
    float width          = 0.8f;   // 0..1 stereo spread
    float octDownLevel   = 0.0f;   // 0..1 level of the octave-down layer
    float octUpLevel     = 0.0f;   // 0..1 level of the octave-up layer
    float toneHz         = 9000.0f;
    float grit           = 0.0f;   // 0..1
};

// sin(2*pi*x) for x in [0, 1); max error ~0.001, plenty for modulators/windows
inline float fastSin01 (float x) noexcept
{
    // fold to [-0.5, 0.5) then Bhaskara-style parabola with refinement
    float t = x - 0.5f;                     // [-0.5, 0.5)
    float y = -16.0f * t * (0.5f - std::abs (t));  // parabola ~ sin(2*pi*x)
    return 0.225f * (y * std::abs (y) - y) + y;
}

class ChoirEngine
{
public:
    static constexpr int maxUnison   = 16;
    static constexpr int layerVoices = 4;  // voices in each octave layer
    static constexpr int totalVoices = maxUnison + 2 * layerVoices;

    void prepare (double sampleRate, unsigned seed = 0x5eed1234u)
    {
        sr = (float) sampleRate;

        // ~0.68 s at 192 kHz, plenty for timing + shifter windows
        bufferSize = 1;
        const int needed = (int) std::ceil (sr * 0.6f);
        while (bufferSize < needed) bufferSize <<= 1;
        bufferMask = bufferSize - 1;
        buffer.assign ((size_t) bufferSize, 0.0f);
        writePos = 0;

        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);
        auto bip = [&] { return uni (rng) * 2.0f - 1.0f; };

        voices.resize (totalVoices);

        for (int i = 0; i < totalVoices; ++i)
        {
            auto& v = voices[(size_t) i];
            v = Voice{};

            if (i < maxUnison)                    v.layer = 0;
            else if (i < maxUnison + layerVoices) v.layer = 1;
            else                                  v.layer = 2;

            const int idx = (v.layer == 0) ? i : (i - maxUnison) % layerVoices;

            // Spread detune evenly across the range, then jitter it, so even
            // a few singers never land on top of each other.
            const int count = (v.layer == 0) ? maxUnison : layerVoices;
            float even = (count > 1) ? ((float) idx / (float) (count - 1)) * 2.0f - 1.0f : 0.0f;
            // interleave so voice 0,1,2... alternate sharp / flat
            even = (idx % 2 == 0) ? std::abs (even) : -std::abs (even);
            v.detuneSeed = std::clamp (0.75f * even + 0.25f * bip(), -1.0f, 1.0f);

            v.timingSeed = uni (rng);
            v.panJitter  = 0.12f * bip();
            // octave layers sit at fixed inner/outer positions, alternating sides
            v.layerPan   = (idx % 2 == 0 ? 1.0f : -1.0f) * (idx < 2 ? 0.45f : 0.9f);
            v.toneSeed   = 0.65f + 0.7f * uni (rng);
            v.gainSeed   = 0.8f + 0.4f * uni (rng);

            v.vibRate    = 4.6f + 1.8f * uni (rng);
            v.vibPhase   = uni (rng);
            v.driftRateA = 0.07f + 0.18f * uni (rng);
            v.driftRateB = 0.23f + 0.31f * uni (rng);
            v.driftPhaseA = uni (rng);
            v.driftPhaseB = uni (rng);
            v.ampRate    = 0.3f + 0.6f * uni (rng);
            v.ampPhase   = uni (rng);

            // start each shifter at a different point in its cycle
            v.shPhase    = uni (rng);
            v.gain       = 0.0f;
        }

        // one-pole smoothing (~20 ms) for voice fades and pan moves
        fadeCoeff = 1.0f - std::exp (-1.0f / (0.02f * sr));
        firstBlock = true;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        for (auto& v : voices) { v.lp = 0.0f; v.gain = 0.0f; }
        firstBlock = true;
    }

    // in: mono input; outL/outR: wet ensemble (overwritten)
    void process (const float* in, float* outL, float* outR, int numSamples, const EngineParams& p)
    {
        const float invSr  = 1.0f / sr;
        const float twoPi  = 6.28318530718f;
        const float pi     = 3.14159265359f;

        // shifter delay windows: short for unison (low lag), long for octaves (smooth)
        const float winUnison = 0.020f * sr;
        const float winOctave = 0.060f * sr;
        const float xfadeSamps = 0.040f * sr;   // length of each tap hand-over

        const float timingSamps = p.timingMs * 0.001f * sr;
        const float vibSamps    = p.vibrato * 0.00045f * sr;   // up to ~±25 cents
        // slow timing wander; capped so it never adds more than a few cents of pitch drift
        const float driftSamps  = std::min (0.1f * timingSamps, 0.001f * sr) + 0.0002f * sr;

        const float lpBase = std::clamp (p.toneHz, 200.0f, 0.45f * sr);

        // gain targets / normalisation
        const int nUnison = std::clamp (p.unisonVoices, 1, maxUnison);
        const float unisonNorm = 1.0f / std::sqrt ((float) nUnison);
        const float layerNorm  = 1.0f / std::sqrt ((float) layerVoices);

        float targets[totalVoices], ratioBase[totalVoices], lpCoeff[totalVoices];
        float panTargetL[totalVoices], panTargetR[totalVoices], xf[totalVoices], win[totalVoices];

        for (int i = 0; i < totalVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (v.layer == 0)      targets[i] = (i < nUnison) ? unisonNorm : 0.0f;
            else if (v.layer == 1) targets[i] = p.octDownLevel * layerNorm;
            else                   targets[i] = p.octUpLevel * layerNorm * 0.8f;

            const float layerRatio = v.layer == 1 ? 0.5f : (v.layer == 2 ? 2.0f : 1.0f);
            const float cents = p.detuneCents * v.detuneSeed * (v.layer == 0 ? 1.0f : 0.6f);
            ratioBase[i] = layerRatio * std::pow (2.0f, cents / 1200.0f);

            // hand-over length as a fraction of the shifter cycle (max 0.25 = full cross-fade)
            win[i] = (v.layer == 0) ? winUnison : winOctave;
            const float cycle = win[i] / std::max (std::abs (1.0f - ratioBase[i]), 1.0e-6f);
            xf[i] = std::clamp (xfadeSamps / cycle, 1.0e-4f, 0.25f);

            const float fc = std::clamp (lpBase * v.toneSeed * (v.layer == 1 ? 0.6f : 1.0f), 150.0f, 0.45f * sr);
            lpCoeff[i] = 1.0f - std::exp (-twoPi * fc * invSr);

            // stereo: spread the ACTIVE unison singers evenly, widest first,
            // alternating sides, so even 2-3 singers fill the width
            float pan;
            if (v.layer == 0)
            {
                if (nUnison == 1 || i >= nUnison)
                    pan = v.panJitter * 0.5f;
                else
                {
                    const int slot = (i % 2 == 0) ? (nUnison - 1 - i / 2) : (i / 2);
                    pan = -1.0f + 2.0f * (float) slot / (float) (nUnison - 1) + v.panJitter;
                }
            }
            else
                pan = v.layerPan + 0.5f * v.panJitter;

            pan = std::clamp (pan * p.width, -1.0f, 1.0f);
            const float ang = (pan + 1.0f) * 0.25f * pi;  // constant power
            panTargetL[i] = std::cos (ang);
            panTargetR[i] = std::sin (ang);
        }

        if (firstBlock)
        {
            for (int i = 0; i < totalVoices; ++i)
            {
                voices[(size_t) i].panL = panTargetL[i];
                voices[(size_t) i].panR = panTargetR[i];
            }
            firstBlock = false;
        }

        // Grit: soft clipper normalised to unity gain for quiet signals, so turning
        // it up adds saturation on loud notes instead of just making things louder.
        const float k = 0.5f + 3.5f * p.grit;            // drive
        const float invK = 1.0f / k;

        for (int n = 0; n < numSamples; ++n)
        {
            buffer[(size_t) writePos] = in[n];

            float accL = 0.0f, accR = 0.0f;

            for (int i = 0; i < totalVoices; ++i)
            {
                auto& v = voices[(size_t) i];
                v.gain += fadeCoeff * (targets[i] - v.gain);
                v.panL += fadeCoeff * (panTargetL[i] - v.panL);
                v.panR += fadeCoeff * (panTargetR[i] - v.panR);
                if (v.gain < 1.0e-5f && targets[i] <= 0.0f)
                    continue;

                // --- modulators --------------------------------------------
                v.vibPhase    += v.vibRate * invSr;     if (v.vibPhase    >= 1.0f) v.vibPhase    -= 1.0f;
                v.driftPhaseA += v.driftRateA * invSr;  if (v.driftPhaseA >= 1.0f) v.driftPhaseA -= 1.0f;
                v.driftPhaseB += v.driftRateB * invSr;  if (v.driftPhaseB >= 1.0f) v.driftPhaseB -= 1.0f;
                v.ampPhase    += v.ampRate * invSr;     if (v.ampPhase    >= 1.0f) v.ampPhase    -= 1.0f;

                const float drift = 0.6f * fastSin01 (v.driftPhaseA) + 0.4f * fastSin01 (v.driftPhaseB);

                // --- pitch shifter -----------------------------------------
                v.shPhase += (1.0f - ratioBase[i]) / win[i];
                v.shPhase -= std::floor (v.shPhase);

                const float phA = v.shPhase;
                float phB = phA + 0.5f; if (phB >= 1.0f) phB -= 1.0f;
                const float gA = tapGain (phA, xf[i], pi);
                const float gB = 1.0f - gA;

                // --- base delay: timing spread + drift + vibrato ------------
                const float baseDelay = 2.0f
                                      + v.timingSeed * timingSamps
                                      + drift * driftSamps
                                      + vibSamps * (1.0f + fastSin01 (v.vibPhase));

                float y = 0.0f;
                if (gA > 1.0e-4f) y += gA * readFrac (baseDelay + phA * win[i]);
                if (gB > 1.0e-4f) y += gB * readFrac (baseDelay + phB * win[i]);

                // --- per-singer tone + level wobble -------------------------
                v.lp += lpCoeff[i] * (y - v.lp);
                y = v.lp;

                const float wobble = 1.0f + 0.12f * fastSin01 (v.ampPhase);
                y *= v.gain * v.gainSeed * wobble;

                accL += y * v.panL;
                accR += y * v.panR;
            }

            if (p.grit > 0.001f)
            {
                const float dl = std::tanh (accL * k) * invK;
                const float dr = std::tanh (accR * k) * invK;
                accL += p.grit * (dl - accL);
                accR += p.grit * (dr - accR);
            }

            // -3 dB trim: keeps the choir roughly level-matched to the input
            outL[n] = accL * 0.7f;
            outR[n] = accR * 0.7f;

            writePos = (writePos + 1) & bufferMask;
        }
    }

private:
    struct Voice
    {
        int   layer = 0;
        float detuneSeed = 0, timingSeed = 0, panJitter = 0, layerPan = 0, toneSeed = 1, gainSeed = 1;
        float vibRate = 5, vibPhase = 0;
        float driftRateA = 0.1f, driftRateB = 0.3f, driftPhaseA = 0, driftPhaseB = 0;
        float ampRate = 0.5f, ampPhase = 0;
        float shPhase = 0;
        float lp = 0;
        float gain = 0;
        float panL = 0.707f, panR = 0.707f;
    };

    // Gain of a shifter tap at phase p. The tap is fully on for most of the cycle,
    // hands over to the other tap (offset by half a cycle) with sin^2 ramps of width
    // 2*xf, and is silent around p = 0/1 where its delay jumps. g(p) + g(p+0.5) = 1.
    static float tapGain (float p, float xf, float pi) noexcept
    {
        const float r = 2.0f * xf;
        if (p < r)          { const float s = std::sin (0.5f * pi * p / r);            return s * s; }
        if (p < 0.5f)       return 1.0f;
        if (p < 0.5f + r)   { const float c = std::cos (0.5f * pi * (p - 0.5f) / r);   return c * c; }
        return 0.0f;
    }

    // 4-point Hermite read, `delay` samples behind the write head
    float readFrac (float delay) const
    {
        delay = std::clamp (delay, 2.0f, (float) (bufferSize - 4));
        const float pos = (float) writePos - delay;
        const float fl  = std::floor (pos);
        const float t   = pos - fl;
        const int   i1  = ((int) fl) & bufferMask;
        const int   i0  = (i1 - 1) & bufferMask;
        const int   i2  = (i1 + 1) & bufferMask;
        const int   i3  = (i1 + 2) & bufferMask;

        const float x0 = buffer[(size_t) i0], x1 = buffer[(size_t) i1];
        const float x2 = buffer[(size_t) i2], x3 = buffer[(size_t) i3];

        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + x1;
    }

    float sr = 44100.0f;
    std::vector<float> buffer;
    int bufferSize = 0, bufferMask = 0, writePos = 0;
    std::vector<Voice> voices;
    float fadeCoeff = 0.001f;
    bool firstBlock = true;
};

} // namespace openchoir
