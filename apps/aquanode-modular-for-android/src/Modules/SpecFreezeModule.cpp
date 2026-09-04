#include "SpecFreezeModule.h"

using namespace aquanode;

SpecFreezeModule::SpecFreezeModule() : SpectralModuleBase (1, 0.5f) {}

void SpecFreezeModule::prepare (double sr)
{
    SpectralModuleBase::prepare (sr);
}

void SpecFreezeModule::spectralReset()
{
    for (auto& cs : channels)
    {
        cs.previousPhase.fill (0.0f);
        cs.synthesisPhase.fill (0.0f);
        cs.heldMag.fill (0.0f);
        cs.holdIncrement.fill (0.0f);
        cs.grainLogMagA.fill (0.0f);
        cs.grainLogMagB.fill (0.0f);
        cs.grainIncrementA.fill (0.0f);
        cs.grainIncrementB.fill (0.0f);
        cs.grainHopCounter = 0;
        cs.wasFrozen = false;
        cs.matchGain = 1.0f;
    }
}

//==============================================================================
// Cepstral lifter: smooths a log-magnitude spectrum by keeping only its low
// quefrency cepstral coefficients (real cepstrum -> lifter -> back to log
// magnitude). Same construction as the original plugin's per-hop step,
// built here on the shared Fft1024's real<->half-spectrum transforms.
//==============================================================================
void SpecFreezeModule::cepstralSmoothMagnitude (const float* logMag, float* outSmoothedLogMag, float characterNorm)
{
    constexpr int half = kFftSize / 2;

    std::complex<float> spec[kNumBins];
    for (int i = 0; i < kNumBins; ++i)
        spec[i] = { logMag[i], 0.0f };

    float cep[kFftSize];
    cepstrumFft.inverseReal (spec, cep);

    float lift[kFftSize];
    std::fill (std::begin (lift), std::end (lift), 0.0f);

    const int minK = 2, maxK = half;
    const int k = juce::jlimit (minK, maxK,
        (int) juce::jmap (characterNorm, 0.0f, 1.0f, (float) minK, (float) maxK));

    lift[0] = cep[0];
    for (int i = 1; i < k; ++i)
        lift[i] = cep[i] * 2.0f;
    if (k >= half)
        lift[half] = cep[half];

    std::complex<float> outBins[kNumBins];
    cepstrumFft.forwardReal (lift, outBins);

    for (int i = 0; i < kNumBins; ++i)
        outSmoothedLogMag[i] = outBins[i].real();
}

//==============================================================================
void SpecFreezeModule::processSpectrum (int channel, std::complex<float>* bins, const std::complex<float>*)
{
    constexpr int half = kFftSize / 2;
    auto& cs = channels[(size_t) channel];

    float mag[kNumBins], phase[kNumBins], logMag[kNumBins];
    for (int i = 0; i < kNumBins; ++i)
    {
        mag[i] = std::abs (bins[i]);
        phase[i] = std::arg (bins[i]);
        logMag[i] = std::log (juce::jmax (1.0e-8f, mag[i]));
    }

    float smoothedLogMag[kNumBins];
    cepstralSmoothMagnitude (logMag, smoothedLogMag, param (pCharacter) * 0.01f);

    const bool freeze = param (pFreeze) > 0.5f;
    const auto mode = (FreezeMode) (int) param (pFreezeMode);
    const int modeBase = freezeModeBase (mode);
    const float evolveRate = param (pEvolveRate) * 0.01f;
    const float diffusion = param (pDiffusion) * 0.01f;
    const bool follow = freezeModeFollows (mode);
    const bool gainMatchOn = param (pGainMatch) > 0.5f;

    const bool justFroze = freeze && ! cs.wasFrozen;
    const bool isStretch = (mode == FreezeMode::Stretch);
    const float binAngularStep = juce::MathConstants<float>::twoPi / (float) kFftSize;

    // Instantaneous per-bin frequency estimate for this hop.
    float instIncrement[kNumBins];
    for (int i = 0; i < kNumBins; ++i)
    {
        float delta = phase[i] - cs.previousPhase[(size_t) i] - binAngularStep * (float) i * (float) kHop;
        delta = std::remainder (delta, juce::MathConstants<float>::twoPi);
        instIncrement[i] = binAngularStep * (float) i * (float) kHop + delta;
    }

    // --- Stretch grain lifecycle (once per hop, not per bin) ---------------
    bool grainCrossfading = false;
    float grainCrossfadeFrac = 0.0f;

    if (freeze && isStretch)
    {
        const float stretchTimeSec = param (pStretchTime);
        const int periodHops = juce::jmax (2, (int) std::round (stretchTimeSec * (float) sampleRate / (float) kHop));

        if (justFroze)
        {
            for (int i = 0; i < kNumBins; ++i)
            {
                cs.grainLogMagA[(size_t) i] = cs.grainLogMagB[(size_t) i] = smoothedLogMag[i];
                cs.grainIncrementA[(size_t) i] = cs.grainIncrementB[(size_t) i] = instIncrement[i];
            }
            cs.grainHopCounter = 0;
        }
        else
        {
            ++cs.grainHopCounter;
            if (cs.grainHopCounter >= periodHops)
            {
                cs.grainLogMagA = cs.grainLogMagB;
                cs.grainIncrementA = cs.grainIncrementB;
                for (int i = 0; i < kNumBins; ++i)
                {
                    cs.grainLogMagB[(size_t) i] = smoothedLogMag[i];
                    cs.grainIncrementB[(size_t) i] = instIncrement[i];
                }
                cs.grainHopCounter = 0;
            }
        }

        grainCrossfading = true;
        const float rawFrac = juce::jlimit (0.0f, 1.0f, (float) cs.grainHopCounter / (float) periodHops);
        grainCrossfadeFrac = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * rawFrac);
    }

    float outMag[kNumBins], outPhase[kNumBins];

    for (int i = 0; i < kNumBins; ++i)
    {
        const float liveMag = std::exp (smoothedLogMag[i]);

        if (justFroze)
        {
            cs.holdIncrement[(size_t) i] = instIncrement[i];
            cs.heldMag[(size_t) i] = liveMag;
            cs.synthesisPhase[(size_t) i] = phase[i];
        }

        if (! freeze)
        {
            outMag[i] = liveMag;
            outPhase[i] = phase[i];
            cs.synthesisPhase[(size_t) i] = phase[i];
        }
        else if (isStretch)
        {
            const float lerpLogMag = grainCrossfading
                ? cs.grainLogMagA[(size_t) i] + (cs.grainLogMagB[(size_t) i] - cs.grainLogMagA[(size_t) i]) * grainCrossfadeFrac
                : cs.grainLogMagA[(size_t) i];
            const float lerpIncrement = grainCrossfading
                ? cs.grainIncrementA[(size_t) i] + (cs.grainIncrementB[(size_t) i] - cs.grainIncrementA[(size_t) i]) * grainCrossfadeFrac
                : cs.grainIncrementA[(size_t) i];

            outMag[i] = std::exp (lerpLogMag);
            cs.synthesisPhase[(size_t) i] += lerpIncrement;
            outPhase[i] = cs.synthesisPhase[(size_t) i];
        }
        else
        {
            switch (modeBase)
            {
                case 0: // Static (Hold or Follow)
                    outMag[i] = cs.heldMag[(size_t) i];
                    cs.synthesisPhase[(size_t) i] += cs.holdIncrement[(size_t) i];
                    outPhase[i] = cs.synthesisPhase[(size_t) i];
                    break;

                case 1: // Evolving (Hold or Follow)
                    cs.heldMag[(size_t) i] += (liveMag - cs.heldMag[(size_t) i]) * evolveRate;
                    outMag[i] = cs.heldMag[(size_t) i];
                    cs.synthesisPhase[(size_t) i] += cs.holdIncrement[(size_t) i];
                    outPhase[i] = cs.synthesisPhase[(size_t) i];
                    break;

                case 2: // Continuous (Hold or Follow)
                default:
                    outMag[i] = liveMag;
                    {
                        const float jitter = (cs.rng.nextFloat() - 0.5f) * juce::MathConstants<float>::twoPi * diffusion;
                        cs.synthesisPhase[(size_t) i] += cs.holdIncrement[(size_t) i] + jitter;
                    }
                    outPhase[i] = cs.synthesisPhase[(size_t) i];
                    break;
            }
        }
    }

    for (int i = 0; i < kNumBins; ++i)
        cs.previousPhase[(size_t) i] = phase[i];
    cs.wasFrozen = freeze;

    // Gain Match: per-hop spectral energy (Parseval) ratio between the live
    // input frame and the processed frame, smoothed across hops. Hold modes
    // freeze the target the instant freeze engages, so the held drone keeps
    // the level it had at that moment; Follow modes keep the target tracking
    // the live input the whole time, so the frozen drone rises and falls
    // with whatever else is playing.
    const bool shouldTrackGain = gainMatchOn && (! freeze || follow);
    if (shouldTrackGain)
    {
        auto spectralEnergy = [half] (const float* m)
        {
            double e = (double) m[0] * m[0] + (double) m[half] * m[half];
            for (int i = 1; i < half; ++i)
                e += 2.0 * (double) m[i] * m[i];
            return e;
        };

        const double liveEnergy = spectralEnergy (mag);
        double wetEnergy = 0.0;
        {
            double e = (double) outMag[0] * outMag[0] + (double) outMag[half] * outMag[half];
            for (int i = 1; i < half; ++i)
                e += 2.0 * (double) outMag[i] * outMag[i];
            wetEnergy = e;
        }

        const float target = wetEnergy > 1.0e-12 ? juce::jlimit (0.1f, 8.0f, (float) std::sqrt (liveEnergy / wetEnergy)) : 1.0f;

        // ~50 ms one-pole smoothing at the hop rate, matching the original's smoother.
        const float coeff = 1.0f - std::exp ((float) (-(double) kHop / (0.05 * sampleRate)));
        cs.matchGain += coeff * (target - cs.matchGain);
    }

    const float gm = gainMatchOn ? cs.matchGain : 1.0f;
    for (int i = 0; i < kNumBins; ++i)
        bins[i] = std::polar (outMag[i] * gm, outPhase[i]);
}

//==============================================================================
static ModuleDescriptor specFreezeDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.specfreeze";
    d.displayName = "Spectral Freeze";
    d.description =
        "Cepstrally smooths the live magnitude spectrum every hop into a smooth, crisp envelope, "
        "then can freeze or granularly stretch that spectrum on command. Character sets how much "
        "detail survives the smoothing; Freeze Mode picks Static/Evolving/Continuous magnitude "
        "behaviour crossed with whether Gain Match keeps Following the live input while frozen or "
        "Holds the level it had at the instant freeze engaged, plus a granular Stretch mode.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 23;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("character",   "Character",   0.0f, 100.0f, 25.0f, 0, "%"),
        makeCombo  ("freeze",      "Freeze",      { "Off", "On" }, 0, 0, 1),
        makeCombo  ("freezeMode",  "Freeze Mode", { "Static Hold", "Evolving Hold", "Continuous Hold",
                                                     "Static Follow", "Evolving Follow", "Continuous Follow",
                                                     "Stretch" }, 0, 0, 2),
        makeRotary ("evolveRate",  "Evolve Rate", 0.0f, 100.0f, 5.0f, 1, "%"),
        makeRotary ("diffusion",   "Diffusion",   0.0f, 100.0f, 30.0f, 1, "%"),
        makeRotary ("stretchTime", "Stretch Time", 0.25f, 4.0f, 1.0f, 1, "s", true),
        makeRotary ("dryWet",      "Mix",         0.0f, 100.0f, 50.0f, 2, "%"),
        makeCombo  ("gainMatch",   "Gain Match",  { "Off", "On" }, 1, 2, 1)
    };
    SpectralModuleBase::addCurveParams (d, 0.5f);
    return d;
}

AQUANODE_REGISTER_MODULE (SpecFreezeModule, specFreezeDescriptor)
