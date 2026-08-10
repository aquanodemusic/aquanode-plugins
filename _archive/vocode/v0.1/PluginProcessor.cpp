/*
  ==============================================================================
    PluginProcessor.cpp  —  Vocode  (fixed)

    Bug-fix log
    -----------
    1. OLA desync  (carrierFifoIdx init)
       Root cause : carrierFifoIdx started at 0, so the first hop fired after a
                    full fftSize new samples.  By then olaReadPos == fftSize, but
                    the write had started at position 0 — every synthesised sample
                    was read (as zero) before it was ever written.
       Fix        : Pre-prime the FIFO with carrierFifoIdx = fftSize - hopSize, so
                    the first hop fires after only hopSize new samples.  Also offset
                    olaWritePos to fftSize so write always leads read by exactly
                    fftSize samples throughout the session.

    2. Noise amplification / "giant bass with no input"
       Root cause : rawScale = env / (carRMS + 1e-9).  When the carrier band is
                    near-silent (~1e-8) and the mod envelope is non-zero, the scale
                    reaches ~1e9, clamped to 64.  64 × 1e-8 = 6e-7 per bin, audible
                    after OLA accumulation across 32+ bands.
       Fix        : Hard-gate bands whose carrier RMS is below a -80 dBFS threshold
                    (1e-4) — output for those bins is simply 0, not "noise × 64".

    3. DC / Nyquist bins in the vocoder path
       Root cause : Band 0 was forced to start at bin 0 (DC); Nyquist was also
                    processed.  Both produce DC offset and alias tones after IFFT.
       Fix        : Explicitly zero both bins in synthBuf before IFFT.

    4. OLA normalisation
       Root cause : olaScale = 2 / overlapFactor = 0.5.  For a Hann-squared
                    analysis-synthesis pair with 75 % overlap the per-sample sum of
                    w² = 3/2, so the scale should be 2/3 ≈ 0.667 for unity gain.
       Fix        : olaScale = 2.0f / 3.0f.
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Parameter layout
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocodeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "numBands", "Bands", 1, 2048, 32));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f, 0.4f), 5.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // Long release = per-band spectral reverb / "vocode shimmer" tail.
    // In self-vocode mode, a long release gives a spectral-freeze / hold effect.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release",
        juce::NormalisableRange<float>(0.0f, 4000.0f, 1.0f, 0.3f), 80.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morph", "Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    // Self-vocode: carrier is used as its own modulator.
    // Effect is subtle at default release; increase Release for spectral hold/shimmer.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "selfVocode", "Self-Vocode", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "dryWet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fftSize", "FFT Size",
        juce::StringArray{ "1024", "2048", "4096", "8192" }, 1));

    return { params.begin(), params.end() };
}

//==============================================================================
// Constructor / destructor
//==============================================================================
VocodeAudioProcessor::VocodeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Carrier", juce::AudioChannelSet::stereo(), true)
        .withInput("Modulator", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "VocodeState", createParameterLayout())
{
    pNumBands = apvts.getRawParameterValue("numBands");
    pAttack = apvts.getRawParameterValue("attack");
    pRelease = apvts.getRawParameterValue("release");
    pMorph = apvts.getRawParameterValue("morph");
    pSelfVocode = apvts.getRawParameterValue("selfVocode");
    pDryWet = apvts.getRawParameterValue("dryWet");

    apvts.addParameterListener("fftSize", this);
    apvts.addParameterListener("numBands", this);
    apvts.addParameterListener("attack", this);
    apvts.addParameterListener("release", this);

    bwCurve_write.fill(0.5f);
    bwCurve_read.fill(0.5f);

    fftProc = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

VocodeAudioProcessor::~VocodeAudioProcessor()
{
    apvts.removeParameterListener("fftSize", this);
    apvts.removeParameterListener("numBands", this);
    apvts.removeParameterListener("attack", this);
    apvts.removeParameterListener("release", this);
}

//==============================================================================
// parameterChanged  (message thread)
//==============================================================================
void VocodeAudioProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id == "fftSize")
    {
        const int sizes[] = { 1024, 2048, 4096, 8192 };
        setFFTSizeInternal(sizes[juce::jlimit(0, 3, juce::roundToInt(v))]);
    }
    else if (id == "numBands")
    {
        numBands = juce::jlimit(1, maxBands, juce::roundToInt(v));
        recomputeBandLayout();
    }
    else if (id == "attack" || id == "release")
    {
        recomputeEnvCoefs();
    }
}

//==============================================================================
// prepareToPlay
//==============================================================================
void VocodeAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    {
        const int sizes[] = { 1024, 2048, 4096, 8192 };
        const int idx = juce::jlimit(0, 3,
            juce::roundToInt(apvts.getRawParameterValue("fftSize")->load()));
        fftSize = sizes[idx];
        fftOrder = (fftSize == 1024) ? 10 : (fftSize == 2048) ? 11 : (fftSize == 4096) ? 12 : 13;
        hopSize = fftSize / 4;
        numBins = fftSize / 2 + 1;
    }

    numBands = juce::jlimit(1, maxBands, juce::roundToInt(pNumBands->load()));

    fftProc = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
    recomputeBandLayout();
    recomputeEnvCoefs();
    envState.fill(0.0f);
}

//==============================================================================
// isBusesLayoutSupported
//==============================================================================
bool VocodeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != mainOut)
        return false;

    if (layouts.getBuses(true).size() > 1)
    {
        const auto& side = layouts.getChannelSet(true, 1);
        if (!side.isDisabled() &&
            side != juce::AudioChannelSet::mono() &&
            side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
// createWindow
//==============================================================================
void VocodeAudioProcessor::createWindow()
{
    window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        const float n = float(i) / float(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * n));
    }
}

//==============================================================================
// allocateBuffers
//  Bug fix #1 is here: FIFO pre-primed and OLA write offset established.
//==============================================================================
void VocodeAudioProcessor::allocateBuffers()
{
    carrierFifoL.assign(fftSize, 0.0f);
    carrierFifoR.assign(fftSize, 0.0f);
    modFifo.assign(fftSize, 0.0f);

    // FIX #1a — Pre-prime FIFOs so the first hop fires after only hopSize
    // new samples (not a full fftSize), keeping OLA write/read in sync.
    carrierFifoIdx = fftSize - hopSize;
    modFifoIdx = fftSize - hopSize;

    const int olaSize = fftSize * 4;
    olaL.assign(olaSize, 0.0f);
    olaR.assign(olaSize, 0.0f);

    // FIX #1b — Start write at position fftSize so there is always exactly
    // fftSize samples of distance between write (ahead) and read (behind).
    // This provides the standard OLA latency without read overtaking write.
    olaWritePos = fftSize;
    olaReadPos = 0;

    fftBufL.assign(fftSize * 2, 0.0f);
    fftBufR.assign(fftSize * 2, 0.0f);
    fftBufMod.assign(fftSize * 2, 0.0f);
    synthBufL.assign(fftSize * 2, 0.0f);
    synthBufR.assign(fftSize * 2, 0.0f);

    smoothCarrier.assign(numBins, 0.0f);
    smoothMod.assign(numBins, 0.0f);
    smoothVocode.assign(numBins, 0.0f);

    createWindow();
}

//==============================================================================
// setFFTSizeInternal
//==============================================================================
void VocodeAudioProcessor::setFFTSizeInternal(int newSize)
{
    if (newSize == fftSize) return;

    suspendProcessing(true);

    fftSize = newSize;
    fftOrder = (fftSize == 1024) ? 10 : (fftSize == 2048) ? 11 : (fftSize == 4096) ? 12 : 13;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;

    fftProc = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
    recomputeBandLayout();
    recomputeEnvCoefs();
    envState.fill(0.0f);

    suspendProcessing(false);
}

//==============================================================================
// recomputeBandLayout  — log-spaced bands, bin 1..numBins-1 (DC excluded)
//==============================================================================
void VocodeAudioProcessor::recomputeBandLayout()
{
    const int nb = juce::jlimit(1, maxBands, numBands);

    if (nb == 1)
    {
        // Single band: bins 1..numBins-2 (skip DC at 0, Nyquist at numBins-1)
        bandBinStart[0] = 1;
        bandBinEnd[0] = numBins - 2;
        return;
    }

    // Log-frequency spacing from bin 1 to bin numBins-2 (skip DC + Nyquist).
    const float logMin = std::log2(1.0f);
    const float logMax = std::log2(float(numBins - 2));
    const float step = (logMax - logMin) / float(nb);

    for (int b = 0; b < nb; ++b)
    {
        int lo = juce::roundToInt(std::pow(2.0f, logMin + float(b) * step));
        int hi = juce::roundToInt(std::pow(2.0f, logMin + float(b + 1) * step)) - 1;
        lo = juce::jlimit(1, numBins - 2, lo);
        hi = juce::jlimit(lo, numBins - 2, hi);
        bandBinStart[b] = lo;
        bandBinEnd[b] = hi;
    }
}

//==============================================================================
// recomputeEnvCoefs
//==============================================================================
void VocodeAudioProcessor::recomputeEnvCoefs()
{
    const double sr = (currentSampleRate > 0.0) ? currentSampleRate : 44100.0;
    const double hopsPerSec = sr / double(hopSize);

    const float attMs = pAttack ? pAttack->load() : 5.0f;
    const float relMs = pRelease ? pRelease->load() : 80.0f;

    const float attHops = float(attMs * 0.001 * hopsPerSec);
    const float relHops = float(relMs * 0.001 * hopsPerSec);

    const float aC = (attHops > 0.0f) ? std::exp(-1.0f / attHops) : 0.0f;
    const float rC = (relHops > 0.0f) ? std::exp(-1.0f / relHops) : 0.0f;

    attCoef.fill(aC);
    relCoef.fill(rC);
}

//==============================================================================
// processBlock
//==============================================================================
void VocodeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int mainCh = getMainBusNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    auto sideBuf = getBusBuffer(buffer, true, 1);
    const int sideCh = sideBuf.getNumChannels();

    // Sync numBands if automated
    {
        const int nb = juce::jlimit(1, maxBands, juce::roundToInt(pNumBands->load()));
        if (nb != numBands) { numBands = nb; recomputeBandLayout(); }
    }

    // Refresh bandwidth curve (editor thread → audio thread)
    if (bwDirty.load(std::memory_order_acquire))
    {
        juce::ScopedLock l(bwLock);
        bwCurve_read = bwCurve_write;
        bwDirty.store(false, std::memory_order_release);
    }

    const bool  selfVocode = pSelfVocode->load(std::memory_order_relaxed) >= 0.5f;
    const float morph = pMorph->load(std::memory_order_relaxed);
    const float dryWet = pDryWet->load(std::memory_order_relaxed);

    const int olaSize = int(olaL.size());

    for (int s = 0; s < numSamples; ++s)
    {
        // ------------------------------------------------------------------
        // A. Read carrier (stereo) and modulator (mono mix)
        // ------------------------------------------------------------------
        const float cL = (mainCh > 0) ? buffer.getSample(0, s) : 0.0f;
        const float cR = (mainCh > 1) ? buffer.getSample(1, s) : cL;

        float mMono;
        if (selfVocode)
        {
            // Self-vocode: carrier becomes its own modulator.
            // With short release the effect is subtle (spectral mirror).
            // With long release each band's energy lingers → spectral freeze /
            // shimmer — try Release > 500 ms for an obvious "spectral reverb" tail.
            mMono = (cL + cR) * 0.5f;
        }
        else
        {
            const float mL = (sideCh > 0) ? sideBuf.getSample(0, s) : 0.0f;
            const float mR = (sideCh > 1) ? sideBuf.getSample(1, s) : mL;
            mMono = (mL + mR) * 0.5f;
        }

        // ------------------------------------------------------------------
        // B. Fill analysis FIFOs
        // ------------------------------------------------------------------
        if (carrierFifoIdx < fftSize)
        {
            carrierFifoL[carrierFifoIdx] = cL;
            carrierFifoR[carrierFifoIdx] = cR;
        }
        ++carrierFifoIdx;

        if (modFifoIdx < fftSize)
            modFifo[modFifoIdx] = mMono;
        ++modFifoIdx;

        // ------------------------------------------------------------------
        // C. When we have a full window of new samples, fire a hop.
        //    With FIFO pre-primed (see allocateBuffers), this fires every
        //    hopSize samples from the very first processBlock call.
        // ------------------------------------------------------------------
        if (carrierFifoIdx >= fftSize)
        {
            processHop(selfVocode, morph);

            // Slide FIFOs forward by hopSize (75 % overlap)
            std::copy(carrierFifoL.begin() + hopSize,
                carrierFifoL.begin() + fftSize, carrierFifoL.begin());
            std::copy(carrierFifoR.begin() + hopSize,
                carrierFifoR.begin() + fftSize, carrierFifoR.begin());
            std::copy(modFifo.begin() + hopSize,
                modFifo.begin() + fftSize, modFifo.begin());

            std::fill(carrierFifoL.begin() + fftSize - hopSize,
                carrierFifoL.begin() + fftSize, 0.0f);
            std::fill(carrierFifoR.begin() + fftSize - hopSize,
                carrierFifoR.begin() + fftSize, 0.0f);
            std::fill(modFifo.begin() + fftSize - hopSize,
                modFifo.begin() + fftSize, 0.0f);

            carrierFifoIdx -= hopSize;
            modFifoIdx -= hopSize;
        }

        // ------------------------------------------------------------------
        // D. Read next synthesised output sample from OLA ring buffer.
        //    olaWritePos always leads olaReadPos by exactly fftSize samples,
        //    so every sample is read only after all overlapping hops have
        //    accumulated into it.
        // ------------------------------------------------------------------
        const int ri = olaReadPos % olaSize;
        const float outL = olaL[ri];
        const float outR = olaR[ri];
        olaL[ri] = 0.0f;   // clear slot for future OLA accumulation
        olaR[ri] = 0.0f;
        ++olaReadPos;

        // Dry/wet blend
        buffer.setSample(0, s, (1.0f - dryWet) * cL + dryWet * outL);
        buffer.setSample(1, s, (1.0f - dryWet) * cR + dryWet * outR);
    }
}

//==============================================================================
// processHop  — core vocoder algorithm, called every hopSize samples
//==============================================================================
void VocodeAudioProcessor::processHop(bool /*selfVocode*/, float morph)
{
    const int nb = numBands;

    // ------------------------------------------------------------------
    // 1. Window + forward FFT
    // ------------------------------------------------------------------
    for (int i = 0; i < fftSize; ++i)
    {
        fftBufL[i] = carrierFifoL[i] * window[i];
        fftBufR[i] = carrierFifoR[i] * window[i];
        fftBufMod[i] = modFifo[i] * window[i];
    }
    std::fill(fftBufL.begin() + fftSize, fftBufL.end(), 0.0f);
    std::fill(fftBufR.begin() + fftSize, fftBufR.end(), 0.0f);
    std::fill(fftBufMod.begin() + fftSize, fftBufMod.end(), 0.0f);

    fftProc->performRealOnlyForwardTransform(fftBufL.data(), true);
    fftProc->performRealOnlyForwardTransform(fftBufR.data(), true);
    fftProc->performRealOnlyForwardTransform(fftBufMod.data(), true);

    // ------------------------------------------------------------------
    // 2. Per-band RMS of the modulator
    // ------------------------------------------------------------------
    std::array<float, maxBands> modBandRMS{};
    for (int b = 0; b < nb; ++b)
    {
        float sumSq = 0.0f;
        int   count = 0;
        for (int bin = bandBinStart[b]; bin <= bandBinEnd[b]; ++bin)
        {
            const float re = fftBufMod[bin * 2];
            const float im = fftBufMod[bin * 2 + 1];
            sumSq += re * re + im * im;
            ++count;
        }
        modBandRMS[b] = (count > 0) ? std::sqrt(sumSq / float(count)) : 0.0f;
    }

    // ------------------------------------------------------------------
    // 3. Per-band envelope followers  (attack / release)
    //    Long release → each band lingers = "vocode reverb" shimmer tail.
    //    In self-vocode mode, long release creates a spectral-freeze effect.
    // ------------------------------------------------------------------
    for (int b = 0; b < nb; ++b)
    {
        const float target = modBandRMS[b];
        const float coef = (target > envState[b]) ? attCoef[b] : relCoef[b];
        envState[b] = coef * envState[b] + (1.0f - coef) * target;
    }

    // ------------------------------------------------------------------
    // 4. Per-band RMS of the carrier (mono mix, for normalisation)
    // ------------------------------------------------------------------
    std::array<float, maxBands> carBandRMS{};
    for (int b = 0; b < nb; ++b)
    {
        float sumSq = 0.0f;
        int   count = 0;
        for (int bin = bandBinStart[b]; bin <= bandBinEnd[b]; ++bin)
        {
            const float reL = fftBufL[bin * 2], imL = fftBufL[bin * 2 + 1];
            const float reR = fftBufR[bin * 2], imR = fftBufR[bin * 2 + 1];
            const float re = (reL + reR) * 0.5f;
            const float im = (imL + imR) * 0.5f;
            sumSq += re * re + im * im;
            ++count;
        }
        carBandRMS[b] = (count > 0) ? std::sqrt(sumSq / float(count)) : 0.0f;
    }

    // ------------------------------------------------------------------
    // 5. Bandwidth blur on the envelope (Gaussian smear across bands)
    // ------------------------------------------------------------------
    std::array<float, maxBands> envSmeared{};
    for (int b = 0; b < nb; ++b)
    {
        const float bw = bwCurve_read[b];
        const float sigma = bw * 8.0f + 0.5f;        // 0.5 … 8.5 bands
        const int   rad = juce::jmin(juce::roundToInt(sigma * 3.0f), nb - 1);

        if (rad == 0)
        {
            envSmeared[b] = envState[b];
            continue;
        }

        float wSum = 0.0f, vSum = 0.0f;
        const float inv2s2 = 0.5f / (sigma * sigma);
        for (int db = -rad; db <= rad; ++db)
        {
            const int nb2 = b + db;
            if (nb2 < 0 || nb2 >= nb) continue;
            const float w = std::exp(-float(db * db) * inv2s2);
            wSum += w;
            vSum += w * envState[nb2];
        }
        envSmeared[b] = (wSum > 0.0f) ? vSum / wSum : envState[b];
    }

    // ------------------------------------------------------------------
    // 6. Apply vocoder scale to carrier bins (stereo)
    //
    //    scale = envSmeared[b] / carBandRMS[b]
    //      Replaces the carrier's per-band amplitude with the mod envelope.
    //
    //    FIX #2 — Carrier noise gate
    //      If the carrier band is below -80 dBFS (threshold = 1e-4), we set
    //      the scale to 0 instead of dividing by a near-zero value.  This
    //      prevents quantisation noise from being amplified to audible levels
    //      when the modulator has energy but the carrier is silent.
    //
    //    morph=0 → unity gain on carrier (dry pass-through)
    //    morph=1 → full vocoder transfer function
    // ------------------------------------------------------------------
    std::copy(fftBufL.begin(), fftBufL.end(), synthBufL.begin());
    std::copy(fftBufR.begin(), fftBufR.end(), synthBufR.begin());

    //  Silence threshold for the carrier band (-80 dBFS).
    //  Below this level there is no meaningful carrier signal to shape;
    //  outputting the noise × large scale just produces audible rumble.
    static constexpr float kCarrierThreshold = 1e-4f;
    //  Maximum per-band gain (24 dB).  Legitimate carriers can be boosted
    //  up to this amount if their band is much quieter than the mod envelope.
    static constexpr float kMaxScale = 16.0f;

    for (int b = 0; b < nb; ++b)
    {
        float rawScale;
        if (carBandRMS[b] < kCarrierThreshold)
        {
            // Gate: no carrier energy → output this band as silence.
            rawScale = 0.0f;
        }
        else
        {
            rawScale = envSmeared[b] / carBandRMS[b];
            rawScale = juce::jmin(rawScale, kMaxScale);
        }

        // Blend between dry (morph=0, scale=1) and wet (morph=1, scale=rawScale)
        const float blended = 1.0f + morph * (rawScale - 1.0f);

        for (int bin = bandBinStart[b]; bin <= bandBinEnd[b]; ++bin)
        {
            synthBufL[bin * 2] *= blended;
            synthBufL[bin * 2 + 1] *= blended;
            synthBufR[bin * 2] *= blended;
            synthBufR[bin * 2 + 1] *= blended;
        }
    }

    // FIX #3 — Zero DC and Nyquist bins.
    //   DC (bin 0) causes a constant offset after IFFT → audible thud/rumble.
    //   Nyquist (bin numBins-1) aliases back to DC in the JUCE real FFT.
    //   Both should be silent in a vocoder context.
    synthBufL[0] = synthBufL[1] = 0.0f;
    synthBufR[0] = synthBufR[1] = 0.0f;
    synthBufL[(numBins - 1) * 2] = synthBufL[(numBins - 1) * 2 + 1] = 0.0f;
    synthBufR[(numBins - 1) * 2] = synthBufR[(numBins - 1) * 2 + 1] = 0.0f;

    // ------------------------------------------------------------------
    // 7. Update display magnitudes (taken before IFFT while still spectral)
    // ------------------------------------------------------------------
    {
        juce::ScopedLock l(displayLock);
        const float smoothCoef = 0.7f;
        const float normF = 2.0f / float(fftSize);

        for (int bin = 0; bin < numBins; ++bin)
        {
            auto binMag = [](const std::vector<float>& buf, int b, float nf)
                {
                    const float re = buf[b * 2], im = buf[b * 2 + 1];
                    return std::sqrt(re * re + im * im) * nf;
                };

            smoothCarrier[bin] = smoothCarrier[bin] * smoothCoef
                + binMag(fftBufL, bin, normF) * (1.0f - smoothCoef);
            smoothMod[bin] = smoothMod[bin] * smoothCoef
                + binMag(fftBufMod, bin, normF) * (1.0f - smoothCoef);
            smoothVocode[bin] = smoothVocode[bin] * smoothCoef
                + binMag(synthBufL, bin, normF) * (1.0f - smoothCoef);
        }
    }

    // ------------------------------------------------------------------
    // 8. Inverse FFT + windowed overlap-add
    //
    //    JUCE's performRealOnlyInverseTransform applies 1/N, so the
    //    round-trip analysis → no-op IFFT gives back x[n]*w[n].
    //
    //    Applying the analysis window a second time on synthesis (Hann²)
    //    and OLA-ing at 75% overlap gives a per-sample sum of w² = 3/2.
    //
    //    FIX #4 — olaScale = 2/3  (not 2/4 = 0.5 as before).
    //    Derivation: unity requires 1 / (sum of w² per sample)
    //                = 1 / (3/2) = 2/3.
    // ------------------------------------------------------------------
    fftProc->performRealOnlyInverseTransform(synthBufL.data());
    fftProc->performRealOnlyInverseTransform(synthBufR.data());

    static constexpr float kOlaScale = 2.0f / 3.0f;   // Hann² / 75% overlap

    const int olaSize = int(olaL.size());

    for (int i = 0; i < fftSize; ++i)
    {
        const int idx = (olaWritePos + i) % olaSize;
        olaL[idx] += synthBufL[i] * window[i] * kOlaScale;
        olaR[idx] += synthBufR[i] * window[i] * kOlaScale;
    }
    olaWritePos = (olaWritePos + hopSize) % olaSize;
}

//==============================================================================
// Bandwidth curve API
//==============================================================================
void VocodeAudioProcessor::setBandwidthCurveRange(int s, int e,
    float sv, float ev)
{
    if (s > e) { std::swap(s, e); std::swap(sv, ev); }
    s = juce::jlimit(0, maxBands - 1, s);
    e = juce::jlimit(0, maxBands - 1, e);

    {
        juce::ScopedLock l(bwLock);
        const int span = e - s;
        if (span == 0)
        {
            bwCurve_write[s] = sv;
        }
        else
        {
            for (int b = s; b <= e; ++b)
            {
                const float t = float(b - s) / float(span);
                bwCurve_write[b] = sv + t * (ev - sv);
            }
        }
    }
    bwDirty.store(true, std::memory_order_release);
}

void VocodeAudioProcessor::resetBandwidthCurve()
{
    { juce::ScopedLock l(bwLock); bwCurve_write.fill(0.5f); }
    bwDirty.store(true, std::memory_order_release);
}

void VocodeAudioProcessor::getBandwidthCurve(std::array<float, maxBands>& dest) const
{
    juce::ScopedLock l(bwLock);
    dest = bwCurve_write;
}

//==============================================================================
// Display data
//==============================================================================
void VocodeAudioProcessor::getDisplayMagnitudes(float* cOut, float* mOut,
    float* vOut, int n) const
{
    juce::ScopedLock l(displayLock);
    const int use = juce::jmin(n, int(smoothCarrier.size()));
    for (int i = 0; i < use; ++i)
    {
        if (cOut) cOut[i] = smoothCarrier[i];
        if (mOut) mOut[i] = smoothMod[i];
        if (vOut) vOut[i] = smoothVocode[i];
    }
}

//==============================================================================
// State persistence
//==============================================================================
void VocodeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::MemoryBlock blob;
    {
        juce::ScopedLock l(bwLock);
        const size_t sz = sizeof(int) + 6 * sizeof(juce::uint32)
            + maxBands * sizeof(float);
        blob.ensureSize(sz);
        char* p = static_cast<char*>(blob.getData());

        auto writeInt = [&](int          v) { std::memcpy(p, &v, sizeof(int));           p += sizeof(int);           };
        auto writeUInt = [&](juce::uint32 v) { std::memcpy(p, &v, sizeof(juce::uint32));  p += sizeof(juce::uint32);  };

        writeInt(fftSize);
        writeUInt(colBackground.getARGB());
        writeUInt(colCarrier.getARGB());
        writeUInt(colModulator.getARGB());
        writeUInt(colOutput.getARGB());
        writeUInt(colBandwidth.getARGB());
        writeUInt(colGrid.getARGB());
        std::memcpy(p, bwCurve_write.data(), maxBands * sizeof(float));
    }

    juce::XmlElement root("VocodeState");
    root.setAttribute("version", 1);
    root.setAttribute("binaryData", blob.toBase64Encoding());

    auto apvtsState = apvts.copyState();
    root.addChildElement(apvtsState.createXml().release());

    copyXmlToBinary(root, destData);
}

void VocodeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName("VocodeState") && xml->hasAttribute("binaryData"))
        {
            if (auto* child = xml->getFirstChildElement())
            {
                auto tree = juce::ValueTree::fromXml(*child);
                if (tree.isValid()) apvts.replaceState(tree);
            }

            juce::MemoryBlock blob;
            blob.fromBase64Encoding(xml->getStringAttribute("binaryData"));
            if (blob.getSize() < sizeof(int)) return;

            const char* p = static_cast<const char*>(blob.getData());

            auto readInt = [&]() -> int
                {
                    int v; std::memcpy(&v, p, sizeof(int)); p += sizeof(int); return v;
                };
            auto readColour = [&]() -> juce::Colour
                {
                    juce::uint32 v;
                    std::memcpy(&v, p, sizeof(juce::uint32));
                    p += sizeof(juce::uint32);
                    return juce::Colour(v);
                };

            const int savedFFT = readInt();
            colBackground = readColour();
            colCarrier = readColour();
            colModulator = readColour();
            colOutput = readColour();
            colBandwidth = readColour();
            colGrid = readColour();

            {
                juce::ScopedLock l(bwLock);
                std::memcpy(bwCurve_write.data(), p, maxBands * sizeof(float));
                bwDirty.store(true);
            }

            const int sizes[] = { 1024, 2048, 4096, 8192 };
            for (int sz : sizes)
                if (sz == savedFFT) { setFFTSizeInternal(savedFFT); break; }
        }
    }
}

//==============================================================================
// Editor
//==============================================================================
juce::AudioProcessorEditor* VocodeAudioProcessor::createEditor()
{
    return new VocodeAudioProcessorEditor(*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocodeAudioProcessor();
}