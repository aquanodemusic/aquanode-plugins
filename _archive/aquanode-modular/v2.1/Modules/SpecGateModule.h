#pragma once

#include "SpectralCore.h"

// Spectral Gate - adapted from the SpectralGate VST: each bin passes only while
// its magnitude is above the drawn per-bin threshold (relative to the frame's
// loudest bin: curve 0 = -60 dB lets nearly everything through, 1 = +3 dB
// gates everything). Invert flips it into a residual/noise extractor. The
// Smooth control ramps each bin's gate gain between frames, taming the
// metallic "musical noise" of hard spectral gating.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpecGateModule : public aquanode::SpectralModuleBase
{
public:
    enum ParamIndex { pInvert = 0, pSmooth, pDryWet };

    SpecGateModule() : SpectralModuleBase (1, 0.3f) {}

protected:
    void processSpectrum (int channel, std::complex<float>* bins, const std::complex<float>*) override
    {
        float maxMag = 1.0e-4f;
        for (int b = 0; b < kNumBins; ++b)
            maxMag = juce::jmax (maxMag, std::abs (bins[b]));

        const bool invert = param (pInvert) > 0.5f;

        // per-frame smoothing coefficient from the Smooth time
        const float smoothMs = param (pSmooth);
        const float coeff = smoothMs < 0.5f
            ? 1.0f
            : 1.0f - std::exp ((float) (-(double) kHop / (smoothMs * 0.001 * sampleRate)));

        for (int b = 0; b < kNumBins; ++b)
        {
            const float threshDB = binCurve[b] * 63.0f - 60.0f;
            const float thresh = maxMag * std::pow (10.0f, threshDB / 20.0f);

            bool open = std::abs (bins[b]) >= thresh;
            if (invert)
                open = ! open;

            float& g = gateGain[channel][b];
            g += coeff * ((open ? 1.0f : 0.0f) - g);
            bins[b] *= g;
        }
    }

    void spectralReset() override
    {
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < kNumBins; ++b)
                gateGain[c][b] = 0.0f;
    }

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 3; }

private:
    float gateGain[2][kNumBins] {};
};
