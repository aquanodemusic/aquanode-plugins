#pragma once

#include "SpectralCore.h"

// Spectral Freeze - adapted from the Spectral Smoothe VST: every hop, the
// live magnitude spectrum is cepstrally liftered (Character) into a smooth,
// crisp envelope with the source's buzzy fine detail replaced by something
// closer to a formant curve than ordinary spectral smoothing gets you.
// Freeze locks that behaviour into one of seven modes: the magnitude
// behaviour is Static / Evolving / Continuous, crossed with whether the
// auto Gain Match keeps Following the live dry signal while frozen or
// Holds the level it had at the instant freeze engaged - plus a granular
// Stretch mode that crossfades between periodically re-snapshotted grains.
// The original's FFT Size choice is dropped: this framework's spectral
// modules all share a fixed 1024-point / 256-hop STFT engine, the same
// trade every other Spec* module here makes. Gain Match here compares
// per-hop spectral energy (Parseval) rather than a windowed time-domain
// RMS, and runs per channel rather than on the combined stereo signal;
// both are hop-rate equivalents of the original block-rate metering.
// Input: 0 = Audio In. Output: 0 = Audio Out.
class SpecFreezeModule : public aquanode::SpectralModuleBase
{
public:
    enum ParamIndex
    {
        pCharacter = 0,
        pFreeze,
        pFreezeMode,
        pEvolveRate,
        pDiffusion,
        pStretchTime,
        pDryWet,
        pGainMatch
    };

    // Mirrors pid::FreezeMode from the original plugin. Low 2 bits (mod 3)
    // pick the magnitude behaviour; values >= 3 are the "Follow" variants
    // of the same three. Stretch (6) sits outside that encoding entirely.
    enum class FreezeMode
    {
        StaticHold = 0,
        EvolvingHold = 1,
        ContinuousHold = 2,
        StaticFollow = 3,
        EvolvingFollow = 4,
        ContinuousFollow = 5,
        Stretch = 6
    };

    SpecFreezeModule();

    void prepare (double sr) override;

protected:
    void processSpectrum (int channel, std::complex<float>* bins, const std::complex<float>*) override;
    void spectralReset() override;

    float dryWetAmount() const override { return param (pDryWet) * 0.01f; }
    int curveParamOffset() const override { return 8; }

    // Spectral Freeze has no per-bin curve to draw - its controls are all knobs.
    std::unique_ptr<juce::Component> createExtraContentComponent() override { return nullptr; }
    int extraContentHeight() const override { return 0; }

private:
    static int freezeModeBase (FreezeMode m) { return (int) m % 3; }
    static bool freezeModeFollows (FreezeMode m)
    {
        return m == FreezeMode::StaticFollow || m == FreezeMode::EvolvingFollow
            || m == FreezeMode::ContinuousFollow;
    }

    void cepstralSmoothMagnitude (const float* logMag, float* outSmoothedLogMag, float characterNorm);

    struct ChannelState
    {
        std::array<float, kNumBins> previousPhase {};
        std::array<float, kNumBins> synthesisPhase {};
        std::array<float, kNumBins> heldMag {};
        std::array<float, kNumBins> holdIncrement {};

        std::array<float, kNumBins> grainLogMagA {}, grainLogMagB {};
        std::array<float, kNumBins> grainIncrementA {}, grainIncrementB {};
        int grainHopCounter = 0;

        bool wasFrozen = false;
        float matchGain = 1.0f;

        juce::Random rng;
    };

    std::array<ChannelState, 2> channels;
    aquanode::Fft1024 cepstrumFft;   // separate FFT instance for the cepstral lifter step
};
