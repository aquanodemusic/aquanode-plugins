#pragma once

#include "SpectralCore.h"

// Spectral Enhance - adapted from the SpectralEnhance VST: the drawn curve is a
// per-bin magnitude target relative to the frame's loudest bin. Boost mode
// lifts every bin BELOW the curve up to it (exciter/denser spectrum -
// quiet partials bloom); Attenuate caps every bin ABOVE the curve down to it
// (spectral ceiling/soft limiter per bin). The original's separate range +
// slope controls became the curve itself - draw the slope you want.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpecEnhanceModule : public aquanode::SpectralModuleBase
{
public:
    enum ParamIndex { pMode = 0, pDryWet };

    SpecEnhanceModule() : SpectralModuleBase (1, 0.25f) {}

protected:
    void processSpectrum (int, std::complex<float>* bins, const std::complex<float>*) override
    {
        float maxMag = 1.0e-4f;
        for (int b = 0; b < kNumBins; ++b)
            maxMag = juce::jmax (maxMag, std::abs (bins[b]));

        const bool attenuate = param (pMode) > 0.5f;

        for (int b = 0; b < kNumBins; ++b)
        {
            const float targetDB = binCurve[b] * 63.0f - 60.0f;   // -60 .. +3 dB rel. max
            const float target = maxMag * std::pow (10.0f, targetDB / 20.0f);
            const float mag = std::abs (bins[b]);

            if (! attenuate)
            {
                // boost: raise quiet bins to the curve (keep phase; silent bins stay silent)
                if (mag > 1.0e-9f && mag < target)
                    bins[b] *= target / mag;
            }
            else
            {
                // attenuate: cap loud bins at the curve
                if (mag > target && mag > 1.0e-12f)
                    bins[b] *= target / mag;
            }
        }
    }

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 2; }
};
