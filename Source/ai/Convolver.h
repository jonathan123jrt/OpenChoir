#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace openchoir
{

/** Uniformly partitioned FFT convolver (overlap-save), one channel.

    Deterministic and allocation-free once set up, so the same object gives
    identical results in real time and in offline export. Latency is exactly
    `blockSize` samples (it collects a block, then convolves it). */
class UniformConvolver
{
public:
    void setup (const std::vector<float>& ir, int blockSizeToUse = 512)
    {
        B = blockSizeToUse;
        const int order = (int) std::round (std::log2 ((double) (2 * B)));
        fft = std::make_unique<juce::dsp::FFT> (order);
        N = 2 * B;
        bins = N / 2 + 1;
        P = juce::jmax (1, (int) ((ir.size() + (size_t) B - 1) / (size_t) B));

        work.assign ((size_t) (2 * N), 0.0f);

        // calibrate this FFT backend's round-trip scaling for a product of two spectra
        std::vector<float> a ((size_t) (2 * N), 0.0f), b ((size_t) (2 * N), 0.0f);
        a[0] = 1.0f; b[0] = 1.0f;
        fft->performRealOnlyForwardTransform (a.data(), true);
        fft->performRealOnlyForwardTransform (b.data(), true);
        complexMulAcc (a.data(), b.data(), work.data(), true);
        fft->performRealOnlyInverseTransform (work.data());
        const float scale = std::abs (work[0]) > 1.0e-12f ? 1.0f / work[0] : 1.0f;

        H.assign ((size_t) P, std::vector<float> ((size_t) (2 * N), 0.0f));
        for (int p = 0; p < P; ++p)
        {
            auto& h = H[(size_t) p];
            for (int i = 0; i < B; ++i)
            {
                const size_t k = (size_t) (p * B + i);
                h[(size_t) i] = k < ir.size() ? ir[k] * scale : 0.0f;
            }
            fft->performRealOnlyForwardTransform (h.data(), true);
        }

        X.assign ((size_t) P, std::vector<float> ((size_t) (2 * N), 0.0f));
        inBuf.assign ((size_t) N, 0.0f);
        outBuf.assign ((size_t) B, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& x : X) std::fill (x.begin(), x.end(), 0.0f);
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (outBuf.begin(), outBuf.end(), 0.0f);
        fill = 0;
        head = 0;
    }

    bool isReady() const noexcept { return fft != nullptr; }
    int  latency()  const noexcept { return B; }

    /** In-place: replaces `data` with the convolved signal (delayed by latency()). */
    void process (float* data, int numSamples) noexcept
    {
        if (fft == nullptr) return;
        for (int i = 0; i < numSamples; ++i)
        {
            inBuf[(size_t) (B + fill)] = data[i];
            data[i] = outBuf[(size_t) fill];
            if (++fill == B)
            {
                processFrame();
                fill = 0;
            }
        }
    }

private:
    void processFrame() noexcept
    {
        // newest input spectrum into the frequency-domain delay line
        head = (head + P - 1) % P;
        auto& x = X[(size_t) head];
        std::copy (inBuf.begin(), inBuf.end(), x.begin());
        std::fill (x.begin() + N, x.end(), 0.0f);
        fft->performRealOnlyForwardTransform (x.data(), true);

        std::fill (work.begin(), work.end(), 0.0f);
        for (int p = 0; p < P; ++p)
            complexMulAcc (X[(size_t) ((head + p) % P)].data(), H[(size_t) p].data(), work.data(), false);

        fft->performRealOnlyInverseTransform (work.data());
        for (int i = 0; i < B; ++i)
            outBuf[(size_t) i] = work[(size_t) (B + i)];   // overlap-save: keep the last B

        // slide the input window
        std::copy (inBuf.begin() + B, inBuf.end(), inBuf.begin());
    }

    void complexMulAcc (const float* a, const float* b, float* acc, bool overwrite) const noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            const float ar = a[2 * k], ai = a[2 * k + 1], br = b[2 * k], bi = b[2 * k + 1];
            const float re = ar * br - ai * bi, im = ar * bi + ai * br;
            if (overwrite) { acc[2 * k] = re; acc[2 * k + 1] = im; }
            else           { acc[2 * k] += re; acc[2 * k + 1] += im; }
        }
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    int B = 512, N = 1024, bins = 513, P = 1, fill = 0, head = 0;
    std::vector<std::vector<float>> H, X;
    std::vector<float> work, inBuf, outBuf;
};

} // namespace openchoir
