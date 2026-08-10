#pragma once

#include "SpectralCore.h"

// Spectral Morph - adapted from the SpectralMorph VST, reduced to the part that
// matters: spectral envelope morphing. Main In keeps its fine structure and
// phase while its per-bin magnitude envelope is warped toward Morph In's
// envelope: scale = (morphEnv / mainEnv) ^ amount. Amount 0 = untouched,
// 1 = Main wearing Morph's spectral shape (vocoder-like), and the drawn
// curve makes the amount PER BIN - draw where in the spectrum the morph
// happens (e.g. keep the bass, morph only the highs). Global Morph scales
// the whole curve; Smooth sets the envelope extraction width (wide = broad
// formants, narrow = fine spectral detail). Gate/enhance/interp extras from
// the original are dropped.
// Inputs: 0 = Main In, 1 = Morph In. Output: 0 = Audio Out.
class SpecMorphModule : public aquanode::SpectralModuleBase
{
public:
    enum ParamIndex { pMorph = 0, pSmooth, pDryWet };

    SpecMorphModule() : SpectralModuleBase (2, 1.0f) {}

protected:
    void processSpectrum (int, std::complex<float>* bins, const std::complex<float>* sideBins) override
    {
        if (sideBins == nullptr)
            return;

        float mainMag[kNumBins], sideMag[kNumBins];
        for (int b = 0; b < kNumBins; ++b)
        {
            mainMag[b] = std::abs (bins[b]);
            sideMag[b] = std::abs (sideBins[b]);
        }

        // envelope extraction: forward+backward one-pole across bins,
        // width from the Smooth knob (in bins)
        const float width = juce::jmax (1.0f, param (pSmooth));
        const float coeff = 1.0f / width;
        extractEnvelope (mainMag, mainEnv, coeff);
        extractEnvelope (sideMag, sideEnv, coeff);

        const float amount = param (pMorph) * 0.01f;
        constexpr float maxRatio = 8.0f;

        for (int b = 0; b < kNumBins; ++b)
        {
            if (mainEnv[b] <= 1.0e-10f)
                continue;

            const float envRatio = sideEnv[b] > 1.0e-10f
                ? juce::jmin (sideEnv[b] / mainEnv[b], maxRatio)
                : 0.0f;

            const float binAmount = amount * binCurve[b];
            const float sc = binAmount <= 1.0e-4f ? 1.0f : std::pow (envRatio, binAmount);
            bins[b] *= sc;
        }
    }

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 3; }

private:
    static void extractEnvelope (const float* mag, float* env, float coeff)
    {
        float s = mag[0];
        for (int b = 0; b < kNumBins; ++b)            // forward pass
        {
            s += coeff * (mag[b] - s);
            env[b] = s;
        }
        s = env[kNumBins - 1];
        for (int b = kNumBins - 1; b >= 0; --b)       // backward pass
        {
            s += coeff * (env[b] - s);
            env[b] = s;
        }
    }

    float mainEnv[kNumBins] {};
    float sideEnv[kNumBins] {};
};
