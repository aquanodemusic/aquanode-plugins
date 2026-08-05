#pragma once

#include "SpectralCore.h"

// Spectral Filter - adapted from the SpectralFilter VST: the drawn curve is a
// per-bin gain across the spectrum. Curve at ~0.83 = unity (0 dB); the top
// is +12 dB and the bottom is a full cut (-60 dB -> silence). Draw notches,
// brick walls, spectral holes, or comb shapes freehand.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpecFilterModule : public aquanode::SpectralModuleBase
{
public:
    static constexpr float kUnityCurve = 60.0f / 72.0f;   // curve value giving 0 dB
    enum ParamIndex { pDryWet = 0 };

    SpecFilterModule() : SpectralModuleBase (1, kUnityCurve) {}

protected:
    void processSpectrum (int, std::complex<float>* bins, const std::complex<float>*) override
    {
        for (int b = 0; b < kNumBins; ++b)
            bins[b] *= gainCache[b];
    }

    void curveChanged() override
    {
        for (int b = 0; b < kNumBins; ++b)
        {
            const float db = binCurve[b] * 72.0f - 60.0f;   // -60 .. +12 dB
            gainCache[b] = db <= -59.5f ? 0.0f : std::pow (10.0f, db / 20.0f);
        }
    }

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 1; }

private:
    float gainCache[kNumBins] {};
};
