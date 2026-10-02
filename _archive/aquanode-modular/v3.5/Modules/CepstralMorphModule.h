#pragma once

#include "ModuleCore.h"
#include <juce_dsp/juce_dsp.h>

//==============================================================================
// Cepstral Morph - the Cepstral mode of the SpectralMorph plugin, brought over
// as a patchable module and locked to 4096-point FFT / 4x overlap.
//
// The maths is a straight port of MorphEngine's cepstral path, kept bit for
// bit: windowed STFT of both inputs, a spectral envelope for each obtained by
// taking the real cepstrum of the log magnitude and liftering away everything
// above a low quefrency cutoff, and then a per-bin gain applied to the carrier
//
//     gain[bin] = exp( morph * ( logEnvModulator[bin] - logEnvCarrier[bin] ) )
//
// Carrier is the signal you hear; Modulator only donates its timbre. Because
// the gain is real and positive the carrier's phase is untouched, so this is a
// pure zero-phase magnitude filter - it never smears transients the way a
// phase-vocoder morph does.
//
// Everything the plugin offers beyond this mode (Spectral / Vocoder / Inject /
// Partials, the spectrogram, HD Visuals, the reassignment analysis and the
// display frame queue) is deliberately absent: no visuals were wanted here,
// and dropping them takes the per-instance footprint down to roughly 385 kB.
//
// Fixed at order 12 / 4x overlap, so latency is fftSize - hop = 3072 samples.
// That delay is inherent to the analysis and is NOT reported to the host, so
// on a parallel path the Morph Out will sit ~70 ms (at 44.1 kHz) behind a dry
// path patched around it. Mix compensates internally - the dry it blends back
// is read from the input FIFO at the matching delay - so the module on its own
// never combs against itself.
//==============================================================================
class CepstralMorphModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pMorph = 0, pClarity, pSmooth, pMaxBoost, pDynamics, pMix, pFlip, pFreeze };

    static constexpr int kOrder   = 12;              // 4096
    static constexpr int kFFTSize = 1 << kOrder;
    static constexpr int kOverlap = 4;
    static constexpr int kHop     = kFFTSize / kOverlap;          // 1024
    static constexpr int kLatency = kFFTSize - kHop;              // 3072
    static constexpr int kNumBins = kFFTSize / 2 + 1;             // 2049

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Global; }

    //==========================================================================
    void prepare (double newSampleRate) override
    {
        aquanode::SynthModule::prepare (newSampleRate);

        if (fft == nullptr)
            fft = std::make_unique<juce::dsp::FFT> (kOrder);

        // Real-only transforms need 2*fftSize of scratch.
        for (auto* v : { &inCarL, &inCarR, &inModL, &inModR, &outFifoL, &outFifoR })
            v->assign (kFFTSize, 0.0f);
        for (auto* v : { &accumL, &accumR })
            v->assign (kFFTSize * 2, 0.0f);
        for (auto* v : { &workCL, &workCR, &workML, &workMR, &cepBuf })
            v->assign (kFFTSize * 2, 0.0f);
        for (auto* v : { &magCar, &magMod, &envCarInst, &envModInst, &envCarSm, &envModSm, &gain })
            v->assign (kNumBins, 0.0f);

        window.assign (kFFTSize, 0.0f);
        for (int i = 0; i < kFFTSize; ++i)            // periodic Hann
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) kFFTSize));

        // sum of Hann^2 across hops - exactly COLA at 4x
        olaScale = 1.0f / (0.375f * (float) kOverlap);

        reset();
    }

    void reset() override
    {
        for (auto* v : { &inCarL, &inCarR, &inModL, &inModR, &outFifoL, &outFifoR,
                         &accumL, &accumR, &envCarSm, &envModSm })
            std::fill (v->begin(), v->end(), 0.0f);

        envPrimed = false;
        rover = kLatency;
    }

    //==========================================================================
    void processSample (const aquanode::StereoFrame* inputs,
                        aquanode::StereoFrame* outputs) override
    {
        if (fft == nullptr || inCarL.empty())
        {
            zeroOutputs (outputs);
            return;
        }

        const float mix = param (pMix) * 0.01f;
        const float wet = mix, dry = 1.0f - mix;

        const int r = rover - kLatency;               // read index, always >= 0

        // the input FIFO doubles as the latency-matched dry path
        const float dryL = inCarL[(size_t) r];
        const float dryR = inCarR[(size_t) r];

        const float wetL = outFifoL[(size_t) r];
        const float wetR = outFifoR[(size_t) r];

        inCarL[(size_t) rover] = inputs[0][0];
        inCarR[(size_t) rover] = inputs[0][1];
        inModL[(size_t) rover] = inputs[1][0];
        inModR[(size_t) rover] = inputs[1][1];

        outputs[0][0] = wetL * wet + dryL * dry;
        outputs[0][1] = wetR * wet + dryR * dry;

        if (++rover >= kFFTSize)
        {
            rover = kLatency;
            processFrame();

            std::copy (accumL.begin(), accumL.begin() + kHop, outFifoL.begin());
            std::copy (accumR.begin(), accumR.begin() + kHop, outFifoR.begin());
            std::memmove (accumL.data(), accumL.data() + kHop, sizeof (float) * (size_t) kFFTSize);
            std::memmove (accumR.data(), accumR.data() + kHop, sizeof (float) * (size_t) kFFTSize);
            std::fill (accumL.begin() + kFFTSize, accumL.begin() + kFFTSize + kHop, 0.0f);
            std::fill (accumR.begin() + kFFTSize, accumR.begin() + kFFTSize + kHop, 0.0f);

            std::memmove (inCarL.data(), inCarL.data() + kHop, sizeof (float) * (size_t) kLatency);
            std::memmove (inCarR.data(), inCarR.data() + kHop, sizeof (float) * (size_t) kLatency);
            std::memmove (inModL.data(), inModL.data() + kHop, sizeof (float) * (size_t) kLatency);
            std::memmove (inModR.data(), inModR.data() + kHop, sizeof (float) * (size_t) kLatency);
        }
    }

private:
    //==========================================================================
    void processFrame()
    {
        auto forward = [&] (const std::vector<float>& fifo, std::vector<float>& work)
        {
            for (int i = 0; i < kFFTSize; ++i)
                work[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
            std::fill (work.begin() + kFFTSize, work.begin() + kFFTSize * 2, 0.0f);
            fft->performRealOnlyForwardTransform (work.data(), false);
        };

        forward (inCarL, workCL);
        forward (inCarR, workCR);
        forward (inModL, workML);
        forward (inModR, workMR);

        const float norm = 2.0f / (float) kFFTSize;

        for (int b = 0; b < kNumBins; ++b)
        {
            const float cl = workCL[(size_t) (b * 2)], cli = workCL[(size_t) (b * 2 + 1)];
            const float cr = workCR[(size_t) (b * 2)], cri = workCR[(size_t) (b * 2 + 1)];
            const float ml = workML[(size_t) (b * 2)], mli = workML[(size_t) (b * 2 + 1)];
            const float mr = workMR[(size_t) (b * 2)], mri = workMR[(size_t) (b * 2 + 1)];

            magCar[(size_t) b] = std::sqrt (cl * cl + cli * cli + cr * cr + cri * cri) * norm;
            magMod[(size_t) b] = std::sqrt (ml * ml + mli * mli + mr * mr + mri * mri) * norm;
        }

        // ---- envelopes -------------------------------------------------------
        const int lifter = lifterCutoff (param (pClarity) * 0.01f);
        logEnvelope (magCar.data(), envCarInst.data(), lifter);
        logEnvelope (magMod.data(), envModInst.data(), lifter);

        // ---- temporal smoothing ---------------------------------------------
        const bool freeze = param (pFreeze) > 0.5f;

        if (! envPrimed)
        {
            std::copy (envCarInst.begin(), envCarInst.begin() + kNumBins, envCarSm.begin());
            std::copy (envModInst.begin(), envModInst.begin() + kNumBins, envModSm.begin());
            envPrimed = true;
        }
        else
        {
            const float a = juce::jlimit (0.0f, 0.95f, param (pSmooth) * 0.01f);
            for (int b = 0; b < kNumBins; ++b)
            {
                envCarSm[(size_t) b] = envCarSm[(size_t) b] * a + envCarInst[(size_t) b] * (1.0f - a);
                if (! freeze)
                    envModSm[(size_t) b] = envModSm[(size_t) b] * a + envModInst[(size_t) b] * (1.0f - a);
            }
        }

        // Flip swaps which input is the one you hear and which merely donates
        // its timbre - the whole point of a socket pair, so it stays a knob.
        const bool flip = param (pFlip) > 0.5f;

        const float* envC = flip ? envModSm.data() : envCarSm.data();
        const float* envM = flip ? envCarSm.data() : envModSm.data();
        const float* magC = flip ? magMod.data()   : magCar.data();

        std::vector<float>& carL = flip ? workML : workCL;
        std::vector<float>& carR = flip ? workMR : workCR;

        // ---- per-bin gain ----------------------------------------------------
        const float morph = param (pMorph) * 0.01f;
        const float limit = juce::Decibels::decibelsToGain (param (pMaxBoost));

        double eBefore = 0.0, eAfter = 0.0;

        for (int b = 0; b < kNumBins; ++b)
        {
            const float g = juce::jlimit (1.0f / limit, limit,
                                          std::exp (morph * (envM[b] - envC[b])));
            gain[(size_t) b] = g;

            const double m = (double) magC[b];
            eBefore += m * m;
            eAfter  += m * m * (double) g * (double) g;
        }

        {
            // Correction that would fully restore the carrier's own frame energy.
            const float lmFull  = (eAfter > 1.0e-12) ? (float) std::sqrt (eBefore / eAfter) : 1.0f;
            const float lmFullC = juce::jlimit (0.05f, 20.0f, lmFull);

            // Dynamics blends between that (0%) and no correction at all (100%),
            // so the modulator's loudness contour can show through proportionally.
            const float amount = 1.0f - juce::jlimit (0.0f, 1.0f, param (pDynamics) * 0.01f);
            const float lmC    = 1.0f + (lmFullC - 1.0f) * amount;

            if (lmC != 1.0f)
                for (int b = 0; b < kNumBins; ++b)
                    gain[(size_t) b] *= lmC;
        }

        // ---- render ----------------------------------------------------------
        for (int b = 0; b < kNumBins; ++b)
        {
            const float g = gain[(size_t) b];
            carL[(size_t) (b * 2)] *= g;  carL[(size_t) (b * 2 + 1)] *= g;
            carR[(size_t) (b * 2)] *= g;  carR[(size_t) (b * 2 + 1)] *= g;
        }

        mirrorSpectrum (carL);
        mirrorSpectrum (carR);

        fft->performRealOnlyInverseTransform (carL.data());
        fft->performRealOnlyInverseTransform (carR.data());

        for (int i = 0; i < kFFTSize; ++i)
        {
            const float w = window[(size_t) i] * olaScale;
            accumL[(size_t) i] += carL[(size_t) i] * w;
            accumR[(size_t) i] += carR[(size_t) i] * w;
        }
    }

    //==========================================================================
    /** Real cepstrum -> lifter -> back. Result is the *log* envelope. */
    void logEnvelope (const float* mag, float* logEnv, int lifter)
    {
        // A relative floor keeps log() away from the deep nulls between partials,
        // which would otherwise dominate the low-quefrency estimate on quiet frames.
        float peak = 0.0f;
        for (int b = 0; b < kNumBins; ++b) peak = juce::jmax (peak, mag[b]);
        const float floorMag = juce::jmax (1.0e-7f, peak * 1.0e-6f);

        for (int b = 0; b < kNumBins; ++b)
            cepBuf[(size_t) b] = std::log (juce::jmax (mag[b], floorMag));

        for (int b = 1; b < kNumBins - 1; ++b)                // even mirror
            cepBuf[(size_t) (kFFTSize - b)] = cepBuf[(size_t) b];

        std::fill (cepBuf.begin() + kFFTSize, cepBuf.begin() + kFFTSize * 2, 0.0f);
        fft->performRealOnlyForwardTransform (cepBuf.data(), false);

        for (int q = lifter + 1; q <= kFFTSize / 2; ++q)      // keep low quefrencies only
        {
            cepBuf[(size_t) (q * 2)] = 0.0f;
            cepBuf[(size_t) (q * 2 + 1)] = 0.0f;
            const int m = kFFTSize - q;
            if (m > kFFTSize / 2)
            {
                cepBuf[(size_t) (m * 2)] = 0.0f;
                cepBuf[(size_t) (m * 2 + 1)] = 0.0f;
            }
        }

        fft->performRealOnlyInverseTransform (cepBuf.data());
        std::copy (cepBuf.begin(), cepBuf.begin() + kNumBins, logEnv);
    }

    /** JUCE's inverse expects the conjugate half to be present. */
    void mirrorSpectrum (std::vector<float>& v) const
    {
        for (int b = 1; b < kFFTSize / 2; ++b)
        {
            v[(size_t) ((kFFTSize - b) * 2)]     =  v[(size_t) (b * 2)];
            v[(size_t) ((kFFTSize - b) * 2 + 1)] = -v[(size_t) (b * 2 + 1)];
        }
    }

    /** Clarity (0..2, high = more detail) -> quefrency cutoff. 0..1 is the
        broad-formant range; 1..2 keeps pushing until at 200% every quefrency
        survives, the cepstral round trip becomes an identity, and the
        "envelope" is exactly the raw log magnitude - full spectral
        substitution rather than a very detailed envelope. */
    int lifterCutoff (float clarity01) const noexcept
    {
        const float lo  = 6.0f;
        const float mid = (float) juce::jmax (10, kFFTSize / 14);
        const float hi  = (float) (kFFTSize / 2);
        const float c   = juce::jlimit (0.0f, 2.0f, clarity01);

        const float target = (c <= 1.0f)
            ? std::exp (std::log (lo)  + c          * (std::log (mid) - std::log (lo)))
            : std::exp (std::log (mid) + (c - 1.0f) * (std::log (hi)  - std::log (mid)));

        return juce::jlimit (2, kFFTSize / 2, juce::roundToInt (target));
    }

    //==========================================================================
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> inCarL, inCarR, inModL, inModR;
    std::vector<float> outFifoL, outFifoR, accumL, accumR;
    std::vector<float> workCL, workCR, workML, workMR, cepBuf;
    std::vector<float> magCar, magMod, envCarInst, envModInst, envCarSm, envModSm, gain;
    std::vector<float> window;

    float olaScale { 1.0f };
    int   rover { kLatency };
    bool  envPrimed { false };
};
