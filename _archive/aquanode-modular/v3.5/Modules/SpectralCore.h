#pragma once

#include "ModuleCore.h"
#include <complex>

// SpectralCore - shared machinery for the Spectral effect modules, adapted from
// the SpectralSuite VSTs and locked to 1024-point FFT / 256 hop (75% overlap,
// Hann analysis + synthesis windows). Contains a self-contained radix-2 FFT
// (no juce_dsp dependency), an STFT overlap-add base class, and the shared
// drawable spectral-curve editor (64 points on a log-frequency axis, live
// input spectrum drawn behind it; drag to paint, double-click to reset).
// STFT latency is kFftSize samples; the dry path is delay-compensated so
// Dry/Wet never combs.

namespace aquanode
{

//==============================================================================
class Fft1024
{
public:
    static constexpr int kSize = 1024;
    static constexpr int kNumBins = kSize / 2 + 1;   // 513

    Fft1024()
    {
        for (int i = 0; i < kSize; ++i)
        {
            int r = 0;
            for (int b = 0; b < 10; ++b)
                r |= ((i >> b) & 1) << (9 - b);
            bitrev[i] = r;
        }
        for (int i = 0; i < kSize / 2; ++i)
            twiddle[i] = std::polar (1.0f, -juce::MathConstants<float>::twoPi * (float) i / (float) kSize);
    }

    void forwardReal (const float* in, std::complex<float>* outBins)
    {
        for (int i = 0; i < kSize; ++i)
            work[bitrev[i]] = { in[i], 0.0f };
        transform (false);
        for (int b = 0; b < kNumBins; ++b)
            outBins[b] = work[b];
    }

    void inverseReal (const std::complex<float>* inBins, float* out)
    {
        std::complex<float> full[kSize];
        for (int b = 0; b < kNumBins; ++b)
            full[b] = inBins[b];
        for (int b = kNumBins; b < kSize; ++b)
            full[b] = std::conj (inBins[kSize - b]);
        for (int i = 0; i < kSize; ++i)
            work[bitrev[i]] = full[i];
        transform (true);
        const float scale = 1.0f / (float) kSize;
        for (int i = 0; i < kSize; ++i)
            out[i] = work[i].real() * scale;
    }

private:
    void transform (bool inverse)
    {
        for (int len = 2; len <= kSize; len <<= 1)
        {
            const int half = len >> 1;
            const int step = kSize / len;
            for (int start = 0; start < kSize; start += len)
                for (int k = 0; k < half; ++k)
                {
                    auto w = twiddle[k * step];
                    if (inverse)
                        w = std::conj (w);
                    const auto a = work[start + k];
                    const auto b = work[start + k + half] * w;
                    work[start + k] = a + b;
                    work[start + k + half] = a - b;
                }
        }
    }

    int bitrev[kSize];
    std::complex<float> twiddle[kSize / 2];
    std::complex<float> work[kSize];
};

//==============================================================================
// STFT overlap-add base. Subclasses implement processSpectrum(); modules with
// a second spectral input (Morph) also receive that input's bins.
//==============================================================================
class SpectralModuleBase : public SynthModule
{
public:
    static constexpr int kFftSize = Fft1024::kSize;
    static constexpr int kHop = kFftSize / 4;
    static constexpr int kNumBins = Fft1024::kNumBins;
    static constexpr int kCurvePoints = 64;
    static constexpr int kDisplayPoints = 128;
    static constexpr int kLatency = kFftSize;   // measured: first frame lands one FFT late

    explicit SpectralModuleBase (int numSpectralInputs = 1, float curveDefault = 0.5f)
        : spectralInputs (juce::jlimit (1, 2, numSpectralInputs)),
          defaultCurveValue (curveDefault)
    {
        for (int i = 0; i < kFftSize; ++i)
            window[i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kFftSize);
        for (auto& m : displayMags)
            m.store (0.0f, std::memory_order_relaxed);
    }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        curveDirty.store (true, std::memory_order_relaxed);
        reset();
    }

    void reset() override
    {
        for (int s = 0; s < 2; ++s)
            for (int c = 0; c < 2; ++c)
                std::fill (inRing[s][c].begin(), inRing[s][c].end(), 0.0f);
        for (int c = 0; c < 2; ++c)
            std::fill (outRing[c].begin(), outRing[c].end(), 0.0f);
        ringPos = 0;
        hopCount = 0;
        spectralReset();
    }

    void setParameter (const juce::String& id, float value) override
    {
        SynthModule::setParameter (id, value);
        curveDirty.store (true, std::memory_order_relaxed);
    }

    void blockStart() override
    {
        if (curveDirty.exchange (false, std::memory_order_relaxed))
        {
            rebuildBinCurve();
            curveChanged();
        }
    }

    void processSample (const StereoFrame* inputs, StereoFrame* outputs) override
    {
        for (int s = 0; s < spectralInputs; ++s)
            for (int c = 0; c < 2; ++c)
                inRing[s][c][(size_t) ringPos] = inputs[s][(size_t) c];

        const float wet01 = dryWetAmount();
        const float wetG = std::sin (wet01 * juce::MathConstants<float>::halfPi);
        const float dryG = std::cos (wet01 * juce::MathConstants<float>::halfPi);
        const int dryIdx = (ringPos - kLatency) & (kRingSize - 1);

        for (int c = 0; c < 2; ++c)
        {
            const int outIdx = ringPos & (kOutMask);
            const float wet = outRing[c][(size_t) outIdx];
            outRing[c][(size_t) outIdx] = 0.0f;
            outputs[0][(size_t) c] = wet * wetG + inRing[0][c][(size_t) dryIdx] * dryG;
        }

        ringPos = (ringPos + 1) & (kRingSize - 1);

        if (++hopCount >= kHop)
        {
            hopCount = 0;
            doFrame();
        }
    }

    // for the curve editor
    float getDisplayMagnitude (int i) const
    {
        return displayMags[(size_t) juce::jlimit (0, kDisplayPoints - 1, i)].load (std::memory_order_relaxed);
    }
    float getCurveDefault() const { return defaultCurveValue; }

    // append the hidden curve params to a descriptor
    static void addCurveParams (ModuleDescriptor& d, float defaultValue)
    {
        for (int i = 0; i < kCurvePoints; ++i)
            d.params.push_back (makeRotary ("c" + juce::String (i), "C" + juce::String (i),
                                            0.0f, 1.0f, defaultValue, 9).hide());
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 64; }

protected:
    // subclass interface -----------------------------------------------------
    virtual void processSpectrum (int channel,
                                  std::complex<float>* bins,
                                  const std::complex<float>* sideBins) = 0;
    virtual void spectralReset() {}
    virtual void curveChanged() {}
    virtual float dryWetAmount() const { return 1.0f; }
    virtual int curveParamOffset() const = 0;   // index of "c0" in the param list

    // curve value per bin, log-frequency interpolated (rebuilt in blockStart)
    float binCurve[kNumBins] {};

    static float binToLogPos (int bin, double sampleRate)
    {
        const double freq = juce::jmax (20.0, (double) bin * sampleRate / (double) kFftSize);
        const double nyq = sampleRate * 0.5;
        return (float) juce::jlimit (0.0, 1.0, std::log (freq / 20.0) / std::log (nyq / 20.0));
    }

private:
    void rebuildBinCurve()
    {
        const int off = curveParamOffset();
        for (int b = 0; b < kNumBins; ++b)
        {
            const float x = binToLogPos (b, sampleRate) * (float) (kCurvePoints - 1);
            const int i0 = juce::jlimit (0, kCurvePoints - 1, (int) x);
            const int i1 = juce::jmin (kCurvePoints - 1, i0 + 1);
            const float frac = x - (float) i0;
            binCurve[b] = param (off + i0) * (1.0f - frac) + param (off + i1) * frac;
        }
    }

    void doFrame()
    {
        std::complex<float> sideBins[kNumBins];

        for (int c = 0; c < 2; ++c)
        {
            if (spectralInputs > 1)
            {
                assembleFrame (1, c);
                fft.forwardReal (frame, sideBins);
            }

            assembleFrame (0, c);
            fft.forwardReal (frame, bins);

            if (c == 0)
                updateDisplay();

            processSpectrum (c, bins, spectralInputs > 1 ? sideBins : nullptr);

            fft.inverseReal (bins, frame);

            // overlap-add with synthesis window; Hann^2 at 75% overlap sums to 1.5
            const float norm = 1.0f / 1.5f;
            for (int i = 0; i < kFftSize; ++i)
            {
                const int idx = (ringPos + i) & kOutMask;
                outRing[c][(size_t) idx] += frame[i] * window[i] * norm;
            }
        }
    }

    void assembleFrame (int source, int channel)
    {
        for (int i = 0; i < kFftSize; ++i)
            frame[i] = inRing[source][channel][(size_t) ((ringPos - kFftSize + i) & (kRingSize - 1))] * window[i];
    }

    void updateDisplay()
    {
        for (int p = 0; p < kDisplayPoints; ++p)
        {
            // invert the log mapping: display point -> frequency -> bin
            const double t = (double) p / (double) (kDisplayPoints - 1);
            const double freq = 20.0 * std::pow (sampleRate * 0.5 / 20.0, t);
            const int b = juce::jlimit (0, kNumBins - 1, (int) (freq * kFftSize / sampleRate));
            const float mag = std::abs (bins[b]);
            const float old = displayMags[(size_t) p].load (std::memory_order_relaxed);
            displayMags[(size_t) p].store (old * 0.7f + mag * 0.3f, std::memory_order_relaxed);
        }
    }

    static constexpr int kRingSize = 2048;   // power of two >= kFftSize + kLatency headroom
    static constexpr int kOutMask = kRingSize - 1;

    const int spectralInputs;
    const float defaultCurveValue;

    Fft1024 fft;
    float window[kFftSize];
    float frame[kFftSize];
    std::complex<float> bins[kNumBins];

    std::array<float, kRingSize> inRing[2][2];
    std::array<float, kRingSize> outRing[2];
    int ringPos { 0 };
    int hopCount { 0 };

    std::atomic<bool> curveDirty { true };
    std::array<std::atomic<float>, kDisplayPoints> displayMags;
};

} // namespace aquanode
