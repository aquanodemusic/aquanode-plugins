#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <algorithm>

//==============================================================================
//  Spectral morph engine - five modes.
//
//  Common skeleton (all modes): windowed STFT, magnitudes of main + sidechain,
//  a smoothed *spectral envelope* for each, and a per-bin gain derived from the
//  ratio of the two envelopes:
//
//      gain[bin] = exp( morph * ( logEnvModulator[bin] - logEnvCarrier[bin] ) )
//
//  Carrier = the signal you hear.  Modulator = the signal that donates timbre.
//  The Flip switch swaps which bus is which.
//
//  ---------------------------------------------------------------------------
//  MODES
//
//  0  Cepstral   Envelope via real cepstrum + low-quefrency liftering.
//                Pure zero-phase magnitude filter. Unchanged, bit for bit.
//
//  1  Spectral   Envelope via a proportional-width box average of the linear
//                magnitude spectrum. Unchanged, bit for bit.
//
//  2  Vocoder    Cepstral envelope, plus three things a plain envelope filter
//                cannot do:
//                  - Flatten: divides the carrier by its *own* fine structure,
//                    so the modulator's formants get painted onto a whitened
//                    carrier instead of merely re-weighting whichever carrier
//                    harmonics happen to sit nearby. This is what turns
//                    "filtered music" into "music that speaks".
//                  - Sibilance: per-band spectral-flatness detection on the
//                    modulator; where the modulator is noise-like (fricatives,
//                    breath, cymbals) the carrier's phase is randomised by the
//                    same amount, so /s/ and /sh/ survive as noise rather than
//                    as a filtered tone. Consonants are where intelligibility
//                    lives, and this is the single biggest win of the lot.
//                  - Attack/Release: asymmetric envelope smoothing with real
//                    time constants (the legacy Smooth knob was a per-frame
//                    coefficient, so its meaning drifted with FFT size and
//                    sample rate). Fast attack preserves consonant onsets,
//                    slow release keeps the timbre from chattering.
//
//  3  Inject     Vocoder, plus synthesis. Where the carrier simply has no
//                energy to shape - a pad with nothing above 5 kHz asked to
//                pronounce an /s/ - the filter can only give up, because a
//                filter multiplies. Inject measures the deficit between the
//                target envelope and what the carrier actually delivers, and
//                *adds* the missing energy: noise, or a phase-doubled octave
//                fold of the carrier's own lower spectrum (harmonically
//                related, so it stays musical). Fold blends between the two.
//
//  4  Partials   The real morph. Peak-picks both spectra, matches carrier
//                partials to modulator partials by log-frequency proximity,
//                and *moves* each carrier partial toward its match - the whole
//                region of influence around the peak travels together, with a
//                per-partial accumulated phase correction so the shift stays
//                coherent frame to frame (Laroche-Dolson style phase locking).
//                Glide is how far they travel; at 0 this collapses exactly
//                back to Cepstral, at 1 the carrier's partials land on the
//                modulator's. This is interpolation of spectral *structure*
//                rather than of spectral *weighting*, which is the categorical
//                difference between cross-synthesis and morphing.
//==============================================================================
class MorphEngine
{
public:
    static constexpr int kMinOrder      = 9;                  // 512
    static constexpr int kMaxOrder      = 15;                 // 32768
    static constexpr int kMaxFFT        = 1 << kMaxOrder;
    static constexpr int kDisplayBands  = 256;                // log-spaced bands sent to the UI
    static constexpr int kFrameQueue    = 96;
    static constexpr int kMaxPeaks      = 1024;               // per-frame partial budget

    MorphEngine() = default;

    enum Mode
    {
        modeCepstral = 0,
        modeSpectral,
        modeVocoder,
        modeInject,
        modePartials,
        numModes
    };

    //==========================================================================
    /** Display-only data for the "HD Visuals" rolling spectrogram. Frequency-
        reassigned (main, side) points - sub-bin-accurate frequency estimates
        paired with linear magnitude - computed purely for the UI. This has no
        bearing whatsoever on the audio path; it is read by the editor once per
        analysis hop and never touched by processFrame()'s morph/gain math. */
    struct ReassignFrame
    {
        static constexpr int kMaxPoints = 2048;
        std::array<float, kMaxPoints> freqHz {};
        std::array<float, kMaxPoints> magLin {};
        int count = 0;
    };

    struct Params
    {
        // --- shared -----------------------------------------------------------
        int   mode       = modeCepstral;
        float morph      = 1.0f;    // 0 .. 1.5   (1 = full envelope replacement)
        float clarity    = 1.0f;    // 0 .. 2     envelope detail
        float maxBoostDb = 72.0f;   // per-bin gain clamp
        float mix        = 1.0f;    // dry/wet
        bool  flip       = false;   // swap carrier / modulator
        bool  freezeSide = false;   // hold the sidechain envelope

        // 0 = preserve the carrier's own frame energy (old Level Match ON).
        // 1 = apply no correction at all, so the modulator's loudness contour
        // passes through untouched (old Level Match OFF). Continuous in between.
        float dynamics   = 1.0f;

        // --- legacy modes (Cepstral / Spectral) --------------------------------
        float smooth     = 0.40f;   // 0 .. 0.95  per-frame envelope smoothing

        // --- Vocoder / Inject / Partials --------------------------------------
        float attackMs   = 5.0f;    // envelope rise time
        float releaseMs  = 60.0f;   // envelope fall time
        float flatten    = 0.0f;    // 0 .. 1     carrier whitening

        // --- Vocoder / Inject ---------------------------------------------------
        float unvoiced   = 0.0f;    // 0 .. 1     noise substitution amount

        // --- Inject -------------------------------------------------------------
        float fill       = 0.0f;    // 0 .. 1     how much missing energy to add
        float fold       = 0.0f;    // 0 = noise, 1 = octave fold of the carrier

        // --- Partials -----------------------------------------------------------
        float glide      = 0.0f;    // 0 .. 1     how far partials travel
        float lock       = 1.0f;    // 0 .. 1     phase-lock amount
        float peakFloorDb = -60.0f; // peak-picking threshold below frame peak
    };

    //==========================================================================
    void prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn;

        for (int o = kMinOrder; o <= kMaxOrder; ++o)
            ffts[(size_t) (o - kMinOrder)] = std::make_unique<juce::dsp::FFT> (o);

        const size_t big  = (size_t) kMaxFFT * 2;
        const size_t half = (size_t) kMaxFFT / 2 + 1;

        for (auto* v : { &inMainL, &inMainR, &inSideL, &inSideR })  v->assign (kMaxFFT, 0.0f);
        for (auto* v : { &outFifoL, &outFifoR })                     v->assign (kMaxFFT, 0.0f);
        for (auto* v : { &accumL, &accumR })                         v->assign (kMaxFFT * 2, 0.0f);
        for (auto* v : { &workML, &workMR, &workSL, &workSR, &cepBuf }) v->assign (big, 0.0f);

        // HD Visuals (frequency-reassigned rolling spectrogram) - display only.
        raWinH.assign (kMaxFFT, 0.0f);
        raWinDH.assign (kMaxFFT, 0.0f);
        raWorkH.assign (big, 0.0f);
        raWorkDH.assign (big, 0.0f);
        raMonoMain.assign (kMaxFFT, 0.0f);
        raMonoSide.assign (kMaxFFT, 0.0f);

        for (auto* v : { &magMain, &magSide,
                         &envMainInst, &envSideInst, &envMainSm, &envSideSm,
                         &gain, &gainLog,
                         &logMagCar, &logMagMod, &flatMod,
                         &outLRe, &outLIm, &outRRe, &outRIm, &phaseAcc })
            v->assign (half, 0.0f);

        covered.assign (half, 0);

        window.assign (kMaxFFT, 0.0f);
        frameQueue.assign ((size_t) kFrameQueue * kDisplayBands * 2, -120.0f);
        bandStart.assign (kDisplayBands + 1, 0);
        prefixSum.assign ((size_t) kMaxFFT / 2 + 2, 0.0);
        prefixLog.assign ((size_t) kMaxFFT / 2 + 2, 0.0);

        // reserve so the peak lists never allocate on the audio thread
        peakBinCar.reserve (kMaxPeaks); peakPosCar.reserve (kMaxPeaks);
        peakBinMod.reserve (kMaxPeaks); peakPosMod.reserve (kMaxPeaks);
        regionLo.reserve   (kMaxPeaks); regionHi.reserve   (kMaxPeaks);
        regionShift.reserve (kMaxPeaks);

        setFFTSize (currentOrder, currentOverlap, true);
    }

    void reset()
    {
        for (auto* v : { &inMainL, &inMainR, &inSideL, &inSideR,
                         &outFifoL, &outFifoR, &accumL, &accumR,
                         &envMainSm, &envSideSm, &phaseAcc })
            std::fill (v->begin(), v->end(), 0.0f);

        envPrimed = false;
        rover = inFifoLatency;
    }

    /** Cheap: no allocation, safe to call from the audio thread. */
    void setFFTSize (int order, int overlap, bool force = false)
    {
        order   = juce::jlimit (kMinOrder, kMaxOrder, order);
        // Hann^2 overlap-add is exactly COLA at 4x/8x; 2x is offered as an
        // extra option on request but will ripple slightly (a faint amplitude
        // modulation) since the window^2 sum isn't perfectly flat at 50%
        // overlap. Coarser time resolution too. Accepted trade-off, not a bug.
        overlap = (overlap >= 8 ? 8 : (overlap >= 4 ? 4 : 2));

        if (! force && order == currentOrder && overlap == currentOverlap)
            return;

        currentOrder   = order;
        currentOverlap = overlap;
        fftSize        = 1 << order;
        numBins        = fftSize / 2 + 1;
        hopSize        = fftSize / overlap;
        inFifoLatency  = fftSize - hopSize;
        olaScale       = 1.0f / (0.375f * (float) overlap);  // sum of Hann^2 across hops

        for (int i = 0; i < fftSize; ++i)                    // periodic Hann
            window[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) fftSize));
        buildBandMap();
        buildReassignWindows();
        reset();
    }

    int getFFTSize()        const noexcept { return fftSize; }
    int getLatencySamples() const noexcept { return inFifoLatency; }

    //==========================================================================
    /** main: in/out (processed in place).  side: sidechain, may be empty. */
    void process (juce::AudioBuffer<float>& main,
                  const juce::AudioBuffer<float>& side,
                  const Params& p)
    {
        const int n     = main.getNumSamples();
        const int mChan = main.getNumChannels();
        const int sChan = side.getNumChannels();

        float* mL = main.getWritePointer (0);
        float* mR = mChan > 1 ? main.getWritePointer (1) : nullptr;

        const float* sL = sChan > 0 ? side.getReadPointer (0) : nullptr;
        const float* sR = sChan > 1 ? side.getReadPointer (1) : sL;

        const float wet = p.mix, dry = 1.0f - p.mix;

        for (int i = 0; i < n; ++i)
        {
            const int r = rover - inFifoLatency;             // read index, always >= 0

            const float dryL = inMainL[(size_t) r];          // input FIFO doubles as the
            const float dryR = inMainR[(size_t) r];          // latency-matched dry path

            const float outL = outFifoL[(size_t) r];
            const float outR = outFifoR[(size_t) r];

            inMainL[(size_t) rover] = mL[i];
            inMainR[(size_t) rover] = mR != nullptr ? mR[i] : mL[i];
            inSideL[(size_t) rover] = sL != nullptr ? sL[i] : 0.0f;
            inSideR[(size_t) rover] = sR != nullptr ? sR[i] : 0.0f;

            mL[i] = outL * wet + dryL * dry;
            if (mR != nullptr) mR[i] = outR * wet + dryR * dry;

            if (++rover >= fftSize)
            {
                rover = inFifoLatency;
                processFrame (p);

                std::copy (accumL.begin(), accumL.begin() + hopSize, outFifoL.begin());
                std::copy (accumR.begin(), accumR.begin() + hopSize, outFifoR.begin());
                std::memmove (accumL.data(), accumL.data() + hopSize, sizeof (float) * (size_t) fftSize);
                std::memmove (accumR.data(), accumR.data() + hopSize, sizeof (float) * (size_t) fftSize);
                std::fill (accumL.begin() + fftSize, accumL.begin() + fftSize + hopSize, 0.0f);
                std::fill (accumR.begin() + fftSize, accumR.begin() + fftSize + hopSize, 0.0f);

                std::memmove (inMainL.data(), inMainL.data() + hopSize, sizeof (float) * (size_t) inFifoLatency);
                std::memmove (inMainR.data(), inMainR.data() + hopSize, sizeof (float) * (size_t) inFifoLatency);
                std::memmove (inSideL.data(), inSideL.data() + hopSize, sizeof (float) * (size_t) inFifoLatency);
                std::memmove (inSideR.data(), inSideR.data() + hopSize, sizeof (float) * (size_t) inFifoLatency);
            }
        }
    }

    //==========================================================================
    /** UI side: pull one analysis frame (dB values).  Returns false when empty. */
    bool popDisplayFrame (float* mainDb, float* sideDb) noexcept
    {
        const int r = readIdx.load (std::memory_order_relaxed);
        if (r == writeIdx.load (std::memory_order_acquire))
            return false;

        const float* src = frameQueue.data() + (size_t) r * kDisplayBands * 2;
        std::copy (src, src + kDisplayBands, mainDb);
        std::copy (src + kDisplayBands, src + kDisplayBands * 2, sideDb);
        readIdx.store ((r + 1) % kFrameQueue, std::memory_order_release);
        return true;
    }

    double getSampleRate() const noexcept { return sampleRate; }

    //==========================================================================
    // HD Visuals - frequency-reassigned rolling spectrogram (display only).
    // When disabled the extra analysis in processFrame() is skipped entirely,
    // so this is also the CPU on/off switch for the feature.
    void setHDVisualsEnabled (bool shouldEnable) noexcept
    {
        hdVisualsEnabled.store (shouldEnable, std::memory_order_relaxed);
    }

    bool isHDVisualsEnabled() const noexcept
    {
        return hdVisualsEnabled.load (std::memory_order_relaxed);
    }

    /** UI side: grabs the latest reassigned frame, if a new one has arrived
        since the last call. Returns false when nothing new is available. */
    bool popReassignedMain (ReassignFrame& out) noexcept
    {
        const juce::SpinLock::ScopedLockType sl (raLock);
        if (! raMainReady)
            return false;
        out = raMainLatest;
        raMainReady = false;
        return true;
    }

    bool popReassignedSide (ReassignFrame& out) noexcept
    {
        const juce::SpinLock::ScopedLockType sl (raLock);
        if (! raSideReady)
            return false;
        out = raSideLatest;
        raSideReady = false;
        return true;
    }

private:
    //==========================================================================
    static inline float smoothstep (float e0, float e1, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - e0) / juce::jmax (1.0e-9f, e1 - e0));
        return t * t * (3.0f - 2.0f * t);
    }

    /** Soft ceiling in the log-gain domain: linear up to 60% of the limit, then
        a tanh knee. A hard clamp puts a corner in the envelope that you can
        hear as a gritty edge on extreme Morph/Flatten settings. */
    static inline float softLimitLog (float x, float limit) noexcept
    {
        if (limit <= 1.0e-6f) return 0.0f;
        const float k  = 0.6f * limit;
        const float ax = std::abs (x);
        if (ax <= k) return x;
        const float s = (x < 0.0f) ? -1.0f : 1.0f;
        return s * (k + (limit - k) * std::tanh ((ax - k) / (limit - k)));
    }

    static inline float wrapPi (float x) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        x = std::fmod (x, twoPi);
        if (x >  juce::MathConstants<float>::pi) x -= twoPi;
        if (x < -juce::MathConstants<float>::pi) x += twoPi;
        return x;
    }

    inline float nextUniform() noexcept          // xorshift32, RT safe
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float) (rngState & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }

    //==========================================================================
    void processFrame (const Params& p)
    {
        auto& fft = *ffts[(size_t) (currentOrder - kMinOrder)];

        auto forward = [&] (const std::vector<float>& fifo, std::vector<float>& work)
        {
            for (int i = 0; i < fftSize; ++i)
                work[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
            std::fill (work.begin() + fftSize, work.begin() + fftSize * 2, 0.0f);
            fft.performRealOnlyForwardTransform (work.data(), false);
        };

        forward (inMainL, workML);
        forward (inMainR, workMR);
        forward (inSideL, workSL);
        forward (inSideR, workSR);

        const float norm = 2.0f / (float) fftSize;

        for (int b = 0; b < numBins; ++b)
        {
            const float ml = workML[(size_t) (b * 2)], mli = workML[(size_t) (b * 2 + 1)];
            const float mr = workMR[(size_t) (b * 2)], mri = workMR[(size_t) (b * 2 + 1)];
            const float sl = workSL[(size_t) (b * 2)], sli = workSL[(size_t) (b * 2 + 1)];
            const float sr = workSR[(size_t) (b * 2)], sri = workSR[(size_t) (b * 2 + 1)];

            magMain[(size_t) b] = std::sqrt (ml * ml + mli * mli + mr * mr + mri * mri) * norm;
            magSide[(size_t) b] = std::sqrt (sl * sl + sli * sli + sr * sr + sri * sri) * norm;
        }

        pushDisplayFrame();

        // --- HD Visuals: sub-bin frequency-reassigned points for the rolling
        //     spectrogram. Purely additive analysis of the same raw FIFO
        //     samples above - nothing here feeds back into the audio path.
        if (hdVisualsEnabled.load (std::memory_order_relaxed))
        {
            for (int i = 0; i < fftSize; ++i)
            {
                raMonoMain[(size_t) i] = 0.5f * (inMainL[(size_t) i] + inMainR[(size_t) i]);
                raMonoSide[(size_t) i] = 0.5f * (inSideL[(size_t) i]  + inSideR[(size_t) i]);
            }

            runReassignment (raMonoMain.data(), raScratchMain);
            runReassignment (raMonoSide.data(), raScratchSide);

            const juce::SpinLock::ScopedLockType sl (raLock);
            raMainLatest = raScratchMain;  raMainReady = true;
            raSideLatest = raScratchSide;  raSideReady = true;
        }

        const int  mode     = juce::jlimit (0, (int) numModes - 1, p.mode);
        const bool advanced = mode >= modeVocoder;

        // ---- envelopes -------------------------------------------------------
        if (mode == modeSpectral)
        {
            const float width = spectralWidthFromClarity (p.clarity);
            spectralEnvelope (magMain.data(), envMainInst.data(), width);
            spectralEnvelope (magSide.data(), envSideInst.data(), width);
        }
        else
        {
            const int lifter = lifterCutoff (p.clarity);
            logEnvelope (magMain.data(), envMainInst.data(), lifter, fft);
            logEnvelope (magSide.data(), envSideInst.data(), lifter, fft);
        }

        // ---- temporal smoothing ---------------------------------------------
        if (! envPrimed)
        {
            std::copy (envMainInst.begin(), envMainInst.begin() + numBins, envMainSm.begin());
            std::copy (envSideInst.begin(), envSideInst.begin() + numBins, envSideSm.begin());
            envPrimed = true;
        }
        else if (advanced)
        {
            // Real time constants, so the knobs mean the same thing at any FFT
            // size or sample rate, and asymmetric so consonant onsets survive.
            // At high Clarity the envelope resolves individual harmonics, so
            // smoothing across frames blurs partials that move with vibrato
            // or pitch drift - exactly the detail Clarity was just turned up
            // to capture. Shrink the effective time constants as Clarity
            // climbs past 100%; the knobs themselves are unaffected below
            // that, and can still be set to 0 ms for fully instantaneous
            // tracking at any Clarity.
            const float clarityShrink = (p.clarity <= 1.0f) ? 1.0f
                                       : juce::jmap (juce::jmin (2.0f, p.clarity), 1.0f, 2.0f, 1.0f, 0.15f);

            const float dt   = (float) hopSize / (float) juce::jmax (1.0, sampleRate);
            const float aAtk = std::exp (-dt / juce::jmax (0.0001f, p.attackMs  * 0.001f * clarityShrink));
            const float aRel = std::exp (-dt / juce::jmax (0.0001f, p.releaseMs * 0.001f * clarityShrink));

            for (int b = 0; b < numBins; ++b)
            {
                const float cm = (envMainInst[(size_t) b] > envMainSm[(size_t) b]) ? aAtk : aRel;
                envMainSm[(size_t) b] = envMainSm[(size_t) b] * cm + envMainInst[(size_t) b] * (1.0f - cm);

                if (! p.freezeSide)
                {
                    const float cs = (envSideInst[(size_t) b] > envSideSm[(size_t) b]) ? aAtk : aRel;
                    envSideSm[(size_t) b] = envSideSm[(size_t) b] * cs + envSideInst[(size_t) b] * (1.0f - cs);
                }
            }
        }
        else
        {
            const float a = juce::jlimit (0.0f, 0.95f, p.smooth);
            for (int b = 0; b < numBins; ++b)
            {
                envMainSm[(size_t) b] = envMainSm[(size_t) b] * a + envMainInst[(size_t) b] * (1.0f - a);
                if (! p.freezeSide)
                    envSideSm[(size_t) b] = envSideSm[(size_t) b] * a + envSideInst[(size_t) b] * (1.0f - a);
            }
        }

        const float* envCar = p.flip ? envSideSm.data() : envMainSm.data();
        const float* envMod = p.flip ? envMainSm.data() : envSideSm.data();
        const float* magCar = p.flip ? magSide.data()   : magMain.data();
        const float* magMod = p.flip ? magMain.data()   : magSide.data();

        std::vector<float>& carL = p.flip ? workSL : workML;
        std::vector<float>& carR = p.flip ? workSR : workMR;

        // ---- per-bin gain ----------------------------------------------------
        double eBefore = 0.0, eAfter = 0.0;

        if (! advanced)
        {
            // Legacy path, untouched: hard clamp on the linear gain.
            const float limit = juce::Decibels::decibelsToGain (p.maxBoostDb);

            for (int b = 0; b < numBins; ++b)
            {
                const float g = juce::jlimit (1.0f / limit, limit,
                                              std::exp (p.morph * (envMod[b] - envCar[b])));
                gain[(size_t) b] = g;

                const double m = (double) magCar[b];
                eBefore += m * m;
                eAfter  += m * m * (double) g * (double) g;
            }
        }
        else
        {
            const float limitLog = p.maxBoostDb / 8.6858896f;    // dB -> nepers
            computeLogMag (magCar, logMagCar.data());

            for (int b = 0; b < numBins; ++b)
            {
                float gl = softLimitLog (p.morph * (envMod[b] - envCar[b]), limitLog);

                if (p.flatten > 0.0f)
                {
                    // Divide out the carrier's own fine structure. At flatten = 1
                    // and morph = 1 the output magnitude becomes exactly the
                    // modulator's envelope: a true vocoder. In between, the carrier
                    // keeps some of its own character. The whitening term gets its
                    // own soft ceiling so Max Boost still governs the morph term on
                    // its own terms rather than the two fighting over one budget.
                    gl += softLimitLog (p.flatten * (envCar[b] - logMagCar[(size_t) b]), limitLog);
                }

                gainLog[(size_t) b] = gl;
                const float g = std::exp (gl);
                gain[(size_t) b] = g;

                const double m = (double) magCar[b];
                eBefore += m * m;
                eAfter  += m * m * (double) g * (double) g;
            }
        }

        {
            // Correction factor that would fully restore the carrier's own
            // frame energy (this is what "Level Match ON" used to apply).
            const float lmFull = (eAfter > 1.0e-12) ? (float) std::sqrt (eBefore / eAfter) : 1.0f;
            const float lmFullC = juce::jlimit (0.05f, 20.0f, lmFull);

            // Dynamics blends between that (0) and no correction at all (1),
            // so the modulator's amplitude contour is free to show through
            // proportionally instead of being an all-or-nothing switch.
            const float amount = 1.0f - juce::jlimit (0.0f, 1.0f, p.dynamics);
            const float lmC    = 1.0f + (lmFullC - 1.0f) * amount;

            if (lmC != 1.0f)
            {
                const float lmL = std::log (lmC);
                for (int b = 0; b < numBins; ++b) gain[(size_t) b] *= lmC;
                if (advanced)
                    for (int b = 0; b < numBins; ++b) gainLog[(size_t) b] += lmL;
            }
        }

        // ---- render ----------------------------------------------------------
        if (mode == modePartials)
        {
            renderPartials (p, carL, carR, magCar, magMod);
        }
        else
        {
            for (int b = 0; b < numBins; ++b)
            {
                const float g = gain[(size_t) b];
                carL[(size_t) (b * 2)] *= g;  carL[(size_t) (b * 2 + 1)] *= g;
                carR[(size_t) (b * 2)] *= g;  carR[(size_t) (b * 2 + 1)] *= g;
            }

            if (advanced && (p.unvoiced > 0.0f || (mode == modeInject && p.fill > 0.0f)))
                computeLogMag (magMod, logMagMod.data());

            if (advanced && p.unvoiced > 0.0f)
                applyUnvoiced (p, carL, carR, magMod, envMod);

            if (mode == modeInject && p.fill > 0.0f)
                applyInject (p, carL, carR, envCar, envMod, norm);
        }

        if (advanced)
        {
            // Phase rotation / injection / partial migration can all leave a
            // stray imaginary part at DC and Nyquist, which must be real for
            // the inverse transform to give a real signal back.
            carL[1] = 0.0f;  carR[1] = 0.0f;
            carL[(size_t) (fftSize + 1)] = 0.0f;
            carR[(size_t) (fftSize + 1)] = 0.0f;
        }

        mirrorSpectrum (carL);
        mirrorSpectrum (carR);

        fft.performRealOnlyInverseTransform (carL.data());
        fft.performRealOnlyInverseTransform (carR.data());

        for (int i = 0; i < fftSize; ++i)
        {
            const float w = window[(size_t) i] * olaScale;
            accumL[(size_t) i] += carL[(size_t) i] * w;
            accumR[(size_t) i] += carR[(size_t) i] * w;
        }
    }

    //==========================================================================
    //  Vocoder / Inject helpers
    //==========================================================================

    /** Log magnitude with a floor relative to the frame peak, same trick the
        cepstral path uses: an absolute floor lets the deep nulls between
        partials dominate on quiet frames. */
    void computeLogMag (const float* mag, float* out) const
    {
        float peak = 0.0f;
        for (int b = 0; b < numBins; ++b) peak = juce::jmax (peak, mag[b]);
        const float fl = juce::jmax (1.0e-7f, peak * 1.0e-6f);
        for (int b = 0; b < numBins; ++b)
            out[b] = std::log (juce::jmax (mag[b], fl));
    }

    /** Per-bin spectral flatness (geometric mean / arithmetic mean) of the
        modulator over a proportional-width window. Near 1 = noise-like, near 0
        = a clean partial. White noise lands around 0.56, which is why the
        mapping below starts opening at 0.35. Both means come from prefix sums,
        so this is O(1) per bin. */
    void computeFlatness (const float* mag, const float* logMag, float* flat, float widthFrac)
    {
        prefixSum[0] = 0.0;
        prefixLog[0] = 0.0;
        for (int b = 0; b < numBins; ++b)
        {
            prefixSum[(size_t) (b + 1)] = prefixSum[(size_t) b] + (double) mag[b];
            prefixLog[(size_t) (b + 1)] = prefixLog[(size_t) b] + (double) logMag[b];
        }

        for (int b = 0; b < numBins; ++b)
        {
            const int hw  = juce::jmax (4, (int) ((float) b * widthFrac));
            const int lo  = juce::jmax (0, b - hw);
            const int hi  = juce::jmin (numBins - 1, b + hw);
            const double cnt = (double) (hi - lo + 1);

            const double meanLin = (prefixSum[(size_t) (hi + 1)] - prefixSum[(size_t) lo]) / cnt;
            const double meanLog = (prefixLog[(size_t) (hi + 1)] - prefixLog[(size_t) lo]) / cnt;

            flat[b] = (float) juce::jlimit (0.0, 1.0,
                          std::exp (meanLog) / juce::jmax (meanLin, 1.0e-12));
        }
    }

    /** Rotates each carrier bin by a random angle scaled by how noise-like the
        modulator is there. A full random rotation per frame turns a partial
        into band-limited noise on overlap-add; a partial rotation slides
        continuously between the two. Magnitude is untouched, so Level Match
        stays valid, and L/R share the angle so the stereo image survives.

        Gated by the modulator's own level: without the gate, a modulator that
        is silent above 8 kHz reads as perfectly "flat" up there and would
        dissolve the carrier's top end into hiss. */
    void applyUnvoiced (const Params& p, std::vector<float>& carL, std::vector<float>& carR,
                        const float* magMod, const float* envMod)
    {
        computeFlatness (magMod, logMagMod.data(), flatMod.data(), 0.25f);

        float envMax = -1.0e30f;
        for (int b = 0; b < numBins; ++b) envMax = juce::jmax (envMax, envMod[b]);
        const float gateFloor = envMax - 55.0f / 8.6858896f;   // 55 dB below the envelope peak

        for (int b = 0; b < numBins; ++b)
        {
            const float noisiness = smoothstep (0.35f, 0.60f, flatMod[(size_t) b]);
            const float gate      = smoothstep (gateFloor, gateFloor + 1.0f, envMod[b]);
            const float u         = p.unvoiced * noisiness * gate;
            if (u <= 1.0e-4f) continue;

            const float ang = u * (nextUniform() * 2.0f - 1.0f) * juce::MathConstants<float>::pi;
            const float c = std::cos (ang), s = std::sin (ang);

            const size_t re = (size_t) (b * 2), im = re + 1;
            float lr = carL[re], li = carL[im];
            float rr = carR[re], ri = carR[im];
            carL[re] = lr * c - li * s;  carL[im] = lr * s + li * c;
            carR[re] = rr * c - ri * s;  carR[im] = rr * s + ri * c;
        }
    }

    /** Adds the energy the carrier cannot supply.

        A filter multiplies, so it can never put an /s/ on a bass note - there
        is nothing at 7 kHz to boost. Here we compare the target envelope with
        what the filtered carrier actually delivers and add the shortfall,
        either as noise or as a phase-doubled copy of the carrier an octave
        below (a genuine frequency doubling: squaring a unit phasor doubles its
        per-hop phase advance too, so the fold stays coherent frame to frame
        and sounds like a harmonic rather than a shimmer). */
    void applyInject (const Params& p, std::vector<float>& carL, std::vector<float>& carR,
                      const float* envCar, const float* envMod, float norm)
    {
        // magMain/magSide are L+R quadrature sums, so a single channel gets ~1/sqrt(2).
        const float toRaw = (1.0f / norm) * 0.70710678f;

        for (int b = 2; b < numBins; ++b)
        {
            const float desiredLog = envCar[b] + p.morph * (envMod[b] - envCar[b]);
            const float currentLog = logMagCar[(size_t) b] + gainLog[(size_t) b];
            const float deficit    = desiredLog - currentLog;          // nepers
            if (deficit <= 0.0f) continue;

            const float w = juce::jlimit (0.0f, 1.0f, deficit / 2.303f);   // full weight at 20 dB short
            const float injMag = std::exp (desiredLog) * p.fill * w * toRaw;
            if (injMag <= 1.0e-12f) continue;

            // noise phasor, shared by both channels so the injection is centred
            const float na = (nextUniform() * 2.0f - 1.0f) * juce::MathConstants<float>::pi;
            const float nr = std::cos (na), ni = std::sin (na);

            // octave-fold phasor, per channel, from the bin an octave down
            const int   h  = b / 2;
            const size_t hs = (size_t) (h * 2);

            auto foldPhasor = [&] (const std::vector<float>& src, float& fr, float& fi)
            {
                const float xr = src[hs], xi = src[hs + 1];
                const float m  = std::sqrt (xr * xr + xi * xi);
                if (m < 1.0e-20f) { fr = nr; fi = ni; return; }
                const float ur = xr / m, ui = xi / m;
                fr = ur * ur - ui * ui;                 // squared = octave up
                fi = 2.0f * ur * ui;
            };

            float flr, fli, frr, fri;
            foldPhasor (carL, flr, fli);
            foldPhasor (carR, frr, fri);

            auto blend = [&] (float fr, float fi, float& outR, float& outI)
            {
                float xr = nr * (1.0f - p.fold) + fr * p.fold;
                float xi = ni * (1.0f - p.fold) + fi * p.fold;
                const float m = std::sqrt (xr * xr + xi * xi);
                if (m < 1.0e-9f) { outR = 1.0f; outI = 0.0f; return; }
                outR = xr / m; outI = xi / m;
            };

            float ulr, uli, urr, uri;
            blend (flr, fli, ulr, uli);
            blend (frr, fri, urr, uri);

            const size_t re = (size_t) (b * 2), im = re + 1;
            carL[re] += ulr * injMag;  carL[im] += uli * injMag;
            carR[re] += urr * injMag;  carR[im] += uri * injMag;
        }
    }

    //==========================================================================
    //  Partials mode
    //==========================================================================

    /** Local maxima above a threshold relative to the frame peak, with a
        parabolic fit on the log magnitude for sub-bin position. */
    int findPeaks (const float* mag, std::vector<int>& bins, std::vector<float>& pos,
                   float floorDb) const
    {
        bins.clear();
        pos.clear();

        float peak = 0.0f;
        for (int b = 0; b < numBins; ++b) peak = juce::jmax (peak, mag[b]);
        if (peak <= 1.0e-9f) return 0;

        const float thr = peak * juce::Decibels::decibelsToGain (floorDb);

        for (int b = 2; b < numBins - 2; ++b)
        {
            const float m = mag[b];
            if (m < thr) continue;
            if (! (m > mag[b - 1] && m >= mag[b + 1] && m > mag[b - 2] && m >= mag[b + 2]))
                continue;

            const float a1 = std::log (juce::jmax (mag[b - 1], 1.0e-12f));
            const float a2 = std::log (juce::jmax (m,          1.0e-12f));
            const float a3 = std::log (juce::jmax (mag[b + 1], 1.0e-12f));
            const float den = a1 - 2.0f * a2 + a3;
            const float d   = (std::abs (den) > 1.0e-12f)
                            ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a1 - a3) / den)
                            : 0.0f;

            bins.push_back (b);
            pos.push_back ((float) b + d);

            if ((int) bins.size() >= kMaxPeaks) break;
        }
        return (int) bins.size();
    }

    void renderPartials (const Params& p, std::vector<float>& carL, std::vector<float>& carR,
                         const float* magCar, const float* magMod)
    {
        const int nCar = findPeaks (magCar, peakBinCar, peakPosCar, p.peakFloorDb);
        const int nMod = findPeaks (magMod, peakBinMod, peakPosMod, p.peakFloorDb);

        std::fill (outLRe.begin(), outLRe.begin() + numBins, 0.0f);
        std::fill (outLIm.begin(), outLIm.begin() + numBins, 0.0f);
        std::fill (outRRe.begin(), outRRe.begin() + numBins, 0.0f);
        std::fill (outRIm.begin(), outRIm.begin() + numBins, 0.0f);
        std::fill (covered.begin(), covered.begin() + numBins, (std::uint8_t) 0);

        // ---- regions of influence: midpoints between adjacent carrier peaks --
        regionLo.clear(); regionHi.clear(); regionShift.clear();

        for (int i = 0; i < nCar; ++i)
        {
            const int hi = (i == nCar - 1) ? numBins - 1
                                           : (peakBinCar[(size_t) i] + peakBinCar[(size_t) (i + 1)]) / 2;
            const int lo = (i == 0) ? 0 : regionHi[(size_t) (i - 1)] + 1;
            regionLo.push_back (juce::jmin (lo, peakBinCar[(size_t) i]));
            regionHi.push_back (juce::jmax (hi, peakBinCar[(size_t) i]));
            regionShift.push_back (0);
        }

        // ---- match carrier partials to modulator partials --------------------
        // Both lists are sorted, so a single forward scan finds the nearest
        // neighbour. Matching in log frequency, capped at one octave: beyond
        // that the two are not plausibly "the same" partial and the carrier
        // stays put rather than leaping across the spectrum.
        if (nMod > 0 && p.glide > 0.0f)
        {
            int j = 0;
            for (int i = 0; i < nCar; ++i)
            {
                const float fc = juce::jmax (1.0f, peakPosCar[(size_t) i]);
                while (j + 1 < nMod && peakPosMod[(size_t) (j + 1)] < fc) ++j;

                float best = peakPosMod[(size_t) j];
                if (j + 1 < nMod
                    && std::abs (peakPosMod[(size_t) (j + 1)] - fc) < std::abs (best - fc))
                    best = peakPosMod[(size_t) (j + 1)];

                best = juce::jmax (1.0f, best);
                if (std::abs (std::log2 (best / fc)) > 1.0f) continue;   // > 1 octave: no match

                regionShift[(size_t) i] = juce::roundToInt (p.glide * (best - fc));
            }
        }

        // ---- move each region, phase-locked ----------------------------------
        const float advance = juce::MathConstants<float>::twoPi * (float) hopSize / (float) fftSize;

        for (int i = 0; i < nCar; ++i)
        {
            const int sh = regionShift[(size_t) i];
            const int lo = regionLo[(size_t) i];
            const int hi = regionHi[(size_t) i];

            // A partial moved by sh bins must advance sh * 2pi * hop / N more
            // phase per frame than the source bin it was copied from, so the
            // correction accumulates. Keyed on the *carrier* peak bin, which is
            // far more stable frame to frame than the destination would be, and
            // smeared onto its neighbours so one bin of jitter doesn't reset it.
            const int   key = peakBinCar[(size_t) i];
            const float acc = wrapPi (phaseAcc[(size_t) key] + advance * (float) sh);
            phaseAcc[(size_t) key] = acc;
            if (key > 0)           phaseAcc[(size_t) (key - 1)] = acc;
            if (key < numBins - 1) phaseAcc[(size_t) (key + 1)] = acc;

            // One rotation for the whole region keeps the bins of a partial in
            // step with each other - that is the phase locking. At lock = 0 you
            // get raw bin copying and the classic phase-vocoder smear, which is
            // sometimes exactly the sound you want.
            const float rot = p.lock * acc;
            const float cr = std::cos (rot), sr = std::sin (rot);

            for (int b = lo; b <= hi; ++b)
            {
                covered[(size_t) b] = 1;
                const int d = b + sh;
                if (d < 0 || d >= numBins) continue;

                const float g = gain[(size_t) b];
                const size_t s = (size_t) (b * 2);

                const float lr = carL[s] * g, li = carL[s + 1] * g;
                const float rr = carR[s] * g, ri = carR[s + 1] * g;

                outLRe[(size_t) d] += lr * cr - li * sr;
                outLIm[(size_t) d] += lr * sr + li * cr;
                outRRe[(size_t) d] += rr * cr - ri * sr;
                outRIm[(size_t) d] += rr * sr + ri * cr;
            }
        }

        // ---- everything below the peak threshold passes through unmoved ------
        // (the noise bed, breath, room tone: shifting that would sound wrong)
        for (int b = 0; b < numBins; ++b)
        {
            if (covered[(size_t) b]) continue;
            const float g = gain[(size_t) b];
            const size_t s = (size_t) (b * 2);
            outLRe[(size_t) b] += carL[s] * g;  outLIm[(size_t) b] += carL[s + 1] * g;
            outRRe[(size_t) b] += carR[s] * g;  outRIm[(size_t) b] += carR[s + 1] * g;
        }

        for (int b = 0; b < numBins; ++b)
        {
            const size_t s = (size_t) (b * 2);
            carL[s] = outLRe[(size_t) b];  carL[s + 1] = outLIm[(size_t) b];
            carR[s] = outRRe[(size_t) b];  carR[s + 1] = outRIm[(size_t) b];
        }
    }

    //==========================================================================
    //  Envelope estimators (unchanged)
    //==========================================================================

    /** Simpler "spectral matching" envelope: a variable-width moving average of
        the (linear) magnitude spectrum, log()'d at the end so it drops straight
        into the same temporal-smoothing / gain-ratio code the cepstral path
        uses. Window half-width grows with bin index, giving roughly log-frequency
        resolution (narrow near DC, wide near Nyquist) without needing an extra
        FFT round-trip the way the cepstral lifter does. Cheaper, and can ring
        a little near sharp peaks, but is otherwise a fine, simple alternative. */
    void spectralEnvelope (const float* mag, float* logEnv, float widthFraction)
    {
        prefixSum[0] = 0.0;
        for (int b = 0; b < numBins; ++b)
            prefixSum[(size_t) (b + 1)] = prefixSum[(size_t) b] + (double) mag[b];

        for (int b = 0; b < numBins; ++b)
        {
            const int hw  = juce::jmax (3, (int) ((float) b * widthFraction));
            const int lo  = juce::jmax (0, b - hw);
            const int hi  = juce::jmin (numBins - 1, b + hw);
            const int cnt = hi - lo + 1;
            const float avg = (float) ((prefixSum[(size_t) (hi + 1)] - prefixSum[(size_t) lo])
                                       / (double) cnt);
            logEnv[b] = std::log (juce::jmax (avg, 1.0e-7f));
        }
    }

    /** Maps the Clarity knob (0..2, high = more detail) onto a box-average
        width fraction for spectralEnvelope(), mirroring lifterCutoff() so the
        same knob feels directionally consistent in both morph modes. 0..1
        matches the original range; 1..2 ("clarity > 100%") narrows the window
        even further for an even crisper/less blurred result. */
    float spectralWidthFromClarity (float clarity01) const noexcept
    {
        const float lo  = 0.0015f;   // extreme sharpness at 200%
        const float mid = 0.005f;    // old 100% ceiling (narrowest/sharpest)
        const float hi  = 0.35f;     // wide/blurred at 0%
        const float c   = juce::jlimit (0.0f, 2.0f, clarity01);

        return (c <= 1.0f)
            ? hi  + c            * (mid - hi)
            : mid + (c - 1.0f)   * (lo  - mid);
    }

    /** Real cepstrum -> lifter -> back.  Result is the *log* envelope. */
    void logEnvelope (const float* mag, float* logEnv, int lifter, juce::dsp::FFT& fft)
    {
        // A relative floor keeps log() away from the deep nulls between partials,
        // which would otherwise dominate the low-quefrency estimate on quiet frames.
        float peak = 0.0f;
        for (int b = 0; b < numBins; ++b) peak = juce::jmax (peak, mag[b]);
        const float floorMag = juce::jmax (1.0e-7f, peak * 1.0e-6f);

        for (int b = 0; b < numBins; ++b)
            cepBuf[(size_t) b] = std::log (juce::jmax (mag[b], floorMag));

        for (int b = 1; b < numBins - 1; ++b)                 // even mirror
            cepBuf[(size_t) (fftSize - b)] = cepBuf[(size_t) b];

        std::fill (cepBuf.begin() + fftSize, cepBuf.begin() + fftSize * 2, 0.0f);
        fft.performRealOnlyForwardTransform (cepBuf.data(), false);

        for (int q = lifter + 1; q <= fftSize / 2; ++q)       // keep low quefrencies only
        {
            cepBuf[(size_t) (q * 2)] = 0.0f;
            cepBuf[(size_t) (q * 2 + 1)] = 0.0f;
            const int m = fftSize - q;
            if (m > fftSize / 2)
            {
                cepBuf[(size_t) (m * 2)] = 0.0f;
                cepBuf[(size_t) (m * 2 + 1)] = 0.0f;
            }
        }

        fft.performRealOnlyInverseTransform (cepBuf.data());
        std::copy (cepBuf.begin(), cepBuf.begin() + numBins, logEnv);
    }

    /** JUCE's inverse expects the conjugate half to be present. */
    void mirrorSpectrum (std::vector<float>& v) const
    {
        for (int b = 1; b < fftSize / 2; ++b)
        {
            v[(size_t) ((fftSize - b) * 2)]     =  v[(size_t) (b * 2)];
            v[(size_t) ((fftSize - b) * 2 + 1)] = -v[(size_t) (b * 2 + 1)];
        }
    }

    int lifterCutoff (float clarity01) const noexcept
    {
        // Wider low end so low "clarity" values don't collapse the envelope
        // into an almost flat line. Ceiling is split in two stages so 0..1
        // behaves exactly as before, and 1..2 ("clarity > 100%") pushes the
        // envelope resolution further still for an even crisper/less blurred
        // morph, at the cost of the envelope starting to track individual
        // harmonics rather than just the broad formant shape.
        const float lo  = 6.0f;
        const float mid = (float) juce::jmax (10, fftSize / 14);   // old 100% ceiling
        // 200% now reaches fftSize/2: every quefrency is kept, the cepstral
        // round trip becomes an identity transform, and the "envelope" is
        // exactly the raw log magnitude - true full spectral substitution,
        // not just a very detailed envelope. (Previously this was capped at
        // fftSize/6 and additionally hard-clamped below, so Clarity could
        // never actually get here regardless of how high it was turned up.)
        const float hi  = (float) (fftSize / 2);
        const float c   = juce::jlimit (0.0f, 2.0f, clarity01);

        const float target = (c <= 1.0f)
            ? std::exp (std::log (lo)  + c            * (std::log (mid) - std::log (lo)))
            : std::exp (std::log (mid) + (c - 1.0f)    * (std::log (hi)  - std::log (mid)));

        return juce::jlimit (2, fftSize / 2, juce::roundToInt (target));
    }

    void buildBandMap()
    {
        const double nyq = juce::jmax (1000.0, sampleRate * 0.5);
        const double f0  = 20.0;
        for (int i = 0; i <= kDisplayBands; ++i)
        {
            const double t = (double) i / (double) kDisplayBands;
            const double f = f0 * std::pow (nyq / f0, t);
            bandStart[(size_t) i] = juce::jlimit (0, numBins - 1,
                                                  (int) std::round (f / (sampleRate * 0.5) * (numBins - 1)));
        }
    }

    /** Blackman-Harris 4-term window (~92 dB sidelobes) and its exact analytic
        time-derivative. Used only for the HD Visuals reassignment display path;
        the audio path keeps its own Hann window untouched. */
    void buildReassignWindows()
    {
        const int N = fftSize;
        const double twoPi = juce::MathConstants<double>::twoPi;
        constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;

        for (int n = 0; n < N; ++n)
        {
            const double ph = twoPi * (double) n / (double) N;
            raWinH[(size_t) n]  = (float) (a0
                                        - a1 * std::cos (ph)
                                        + a2 * std::cos (2.0 * ph)
                                        - a3 * std::cos (3.0 * ph));
            raWinDH[(size_t) n] = (float) ((twoPi / (double) N) * (
                                          a1 * std::sin (ph)
                                   - 2.0 * a2 * std::sin (2.0 * ph)
                                   + 3.0 * a3 * std::sin (3.0 * ph)));
        }
    }

    /** Frequency-only spectral reassignment on one mono, unwindowed analysis
        frame:  correctedFreq(k) = binFreq(k) - Im(FFT(x*dH) / FFT(x*H)) * fs/2pi.
        Display-only: only ever read by the UI, never by the audio path. */
    void runReassignment (const float* mono, ReassignFrame& out) noexcept
    {
        const int N = fftSize;

        for (int n = 0; n < N; ++n)
        {
            raWorkH[(size_t) n]  = mono[n] * raWinH[(size_t) n];
            raWorkDH[(size_t) n] = mono[n] * raWinDH[(size_t) n];
        }
        std::fill (raWorkH.begin() + N,  raWorkH.begin() + N * 2,  0.0f);
        std::fill (raWorkDH.begin() + N, raWorkDH.begin() + N * 2, 0.0f);

        auto& fft = *ffts[(size_t) (currentOrder - kMinOrder)];
        fft.performRealOnlyForwardTransform (raWorkH.data(),  false);
        fft.performRealOnlyForwardTransform (raWorkDH.data(), false);

        const float fs            = (float) sampleRate;
        const float binW          = fs / (float) N;
        const int   nBins         = N / 2;
        const float maxReassignHz = 2.0f * binW;              // +/- 2 bins max shift
        const float normF         = 2.0f / (float) N;

        // Adaptive threshold: keep bins within 50 dB of the frame RMS.
        double sumSq = 0.0;
        for (int k = 1; k < nBins; ++k)
        {
            const float re = raWorkH[(size_t) (k * 2)], im = raWorkH[(size_t) (k * 2 + 1)];
            sumSq += (double) (re * re + im * im);
        }
        const float rmsDB    = 10.0f * std::log10 ((float) (sumSq / (double) juce::jmax (1, nBins)) + 1.0e-12f);
        const float threshDB = juce::jmax (-90.0f, rmsDB - 50.0f);

        out.count = 0;

        for (int k = 1; k < nBins - 1 && out.count < ReassignFrame::kMaxPoints; ++k)
        {
            const float reH  = raWorkH[(size_t) (k * 2)],  imH  = raWorkH[(size_t) (k * 2 + 1)];
            const float reDH = raWorkDH[(size_t) (k * 2)], imDH = raWorkDH[(size_t) (k * 2 + 1)];

            const float mag   = std::sqrt (reH * reH + imH * imH);
            const float magDB = 20.0f * std::log10 (mag + 1.0e-9f);
            if (magDB < threshDB)
                continue;

            // Im(cDH / cH), with the denominator nudged away from exact zero.
            const float denomRe = reH + 1.0e-9f, denomIm = imH;
            const float denomMagSq = juce::jmax (denomRe * denomRe + denomIm * denomIm, 1.0e-20f);
            const float ratioIm = (imDH * denomRe - reDH * denomIm) / denomMagSq;

            const float fShift = -(fs / juce::MathConstants<float>::twoPi) * ratioIm;
            const float fCorr  = (float) k * binW + fShift;

            if (std::abs (fShift) > maxReassignHz) continue;
            if (fCorr < 1.0f || fCorr > fs * 0.5f)  continue;

            out.freqHz[(size_t) out.count] = fCorr;
            out.magLin[(size_t) out.count] = mag * normF;
            ++out.count;
        }
    }

    void pushDisplayFrame()
    {
        const int w    = writeIdx.load (std::memory_order_relaxed);
        const int next = (w + 1) % kFrameQueue;
        if (next == readIdx.load (std::memory_order_acquire))
            return;                                           // UI is behind: drop

        float* dst = frameQueue.data() + (size_t) w * kDisplayBands * 2;

        for (int i = 0; i < kDisplayBands; ++i)
        {
            const int b0 = bandStart[(size_t) i];
            const int b1 = juce::jmax (b0 + 1, bandStart[(size_t) (i + 1)]);
            float pm = 0.0f, ps = 0.0f;
            for (int b = b0; b < b1 && b < numBins; ++b)
            {
                pm = juce::jmax (pm, magMain[(size_t) b]);
                ps = juce::jmax (ps, magSide[(size_t) b]);
            }
            dst[i]                 = juce::Decibels::gainToDecibels (pm, -120.0f);
            dst[kDisplayBands + i] = juce::Decibels::gainToDecibels (ps, -120.0f);
        }
        writeIdx.store (next, std::memory_order_release);
    }

    //==========================================================================
    double sampleRate = 44100.0;
    int currentOrder = 11, currentOverlap = 4;
    int fftSize = 2048, numBins = 1025, hopSize = 512, inFifoLatency = 1536, rover = 1536;
    float olaScale = 1.0f;
    bool envPrimed = false;
    std::uint32_t rngState = 0x9E3779B9u;

    std::array<std::unique_ptr<juce::dsp::FFT>, kMaxOrder - kMinOrder + 1> ffts;

    std::vector<float> window;
    std::vector<float> inMainL, inMainR, inSideL, inSideR;
    std::vector<float> outFifoL, outFifoR, accumL, accumR;
    std::vector<float> workML, workMR, workSL, workSR, cepBuf;
    std::vector<float> magMain, magSide, envMainInst, envSideInst, envMainSm, envSideSm;
    std::vector<float> gain, gainLog, logMagCar, logMagMod, flatMod;

    // Partials scratch
    std::vector<float> outLRe, outLIm, outRRe, outRIm, phaseAcc;
    std::vector<std::uint8_t> covered;
    std::vector<int>   peakBinCar, peakBinMod, regionLo, regionHi, regionShift;
    std::vector<float> peakPosCar, peakPosMod;

    std::vector<int>   bandStart;
    std::vector<float> frameQueue;
    std::atomic<int>   writeIdx { 0 }, readIdx { 0 };

    std::vector<double> prefixSum, prefixLog;   // O(1)-per-bin box averages

    // HD Visuals - frequency-reassigned rolling spectrogram (display only,
    // never read by the audio path above).
    std::atomic<bool> hdVisualsEnabled { true };
    std::vector<float> raWinH, raWinDH;                 // BH4 window + derivative
    std::vector<float> raWorkH, raWorkDH;                // complex scratch (audio thread only)
    std::vector<float> raMonoMain, raMonoSide;           // mono mixdown scratch (audio thread only)
    juce::SpinLock raLock;
    ReassignFrame raMainLatest, raSideLatest;            // latest frame ready for the UI
    ReassignFrame raScratchMain, raScratchSide;           // audio-thread write targets
    bool raMainReady = false, raSideReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphEngine)
};