#pragma once

#include "SpectralCore.h"

// Spectral Delay - adapted from the SpectralLatency VST: every bin gets its own
// delay line (in whole FFT frames), set by the drawn curve times Max Time.
// Draw a rising line and the highs arrive later than the lows - chords smear
// into arpeggios of frequency. Feedback (new here) regenerates each bin into
// its own delay for spectral shimmer trails. The original's negative-delay /
// host-latency-compensation half is dropped; delays are positive only.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpecDelayModule : public aquanode::SpectralModuleBase
{
public:
    static constexpr int kMaxFrames = 1024;   // ~5.5 s at 48 kHz, 256-sample hop
    enum ParamIndex { pMaxTime = 0, pFeedback, pDryWet };

    SpecDelayModule() : SpectralModuleBase (1, 0.0f)
    {
        for (int c = 0; c < 2; ++c)
            history[c].assign ((size_t) kMaxFrames * kNumBins, { 0.0f, 0.0f });
    }

protected:
    void processSpectrum (int channel, std::complex<float>* bins, const std::complex<float>*) override
    {
        const double framesPerSecond = sampleRate / (double) kHop;
        const float maxSeconds = param (pMaxTime);
        const float fb = param (pFeedback) * 0.01f * 0.9f;

        auto* hist = history[channel].data();
        const int w = writeFrame[channel];

        for (int b = 0; b < kNumBins; ++b)
        {
            const int delayFrames = juce::jlimit (0, kMaxFrames - 1,
                (int) (binCurve[b] * maxSeconds * framesPerSecond));

            const int r = (w - delayFrames + kMaxFrames) % kMaxFrames;
            const std::complex<float> delayed = hist[(size_t) r * kNumBins + b];

            if (delayFrames == 0)
            {
                // zero delay: pass through (reading slot w here would be stale
                // pre-write data; feedback at zero delay would self-amplify)
                hist[(size_t) w * kNumBins + b] = bins[b];
            }
            else
            {
                hist[(size_t) w * kNumBins + b] = bins[b] + delayed * fb;
                bins[b] = delayed;
            }
        }

        writeFrame[channel] = (w + 1) % kMaxFrames;
    }

    void spectralReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (history[c].begin(), history[c].end(), std::complex<float> (0.0f, 0.0f));
            writeFrame[c] = 0;
        }
    }

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 3; }

private:
    std::vector<std::complex<float>> history[2];
    int writeFrame[2] {};
};
