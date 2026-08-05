#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
//  Transfer curve helpers
//
//  Straight ports of compress_downwards() / compress_upwards() and the soft
//  knee parabola coefficients from Robbert van der Helm's compressor_bank.rs,
//  which in turn come from the Giannoulis et al. "Digital Dynamic Range
//  Compressor Design" paper: the knee is `x + a * (x + b)^2`.
//
//  The only difference here is that the threshold handed to these functions is
//  the drawn curve value for that bin (plus the direction's offset) instead of
//  a value read off the global threshold polynomial.
// ============================================================================
namespace
{
    inline float dbToGain(float dB) { return std::pow(10.0f, dB * 0.05f); }

    inline float gainToDb(float g)
    {
        return 20.0f * std::log10(juce::jmax(g, 1.0e-7f));
    }

    inline float compressDownwards(float inputDb, float thresholdDb,
        float ratio, float kneeWidthDb)
    {
        if (ratio <= 1.0001f) return inputDb;

        if (kneeWidthDb <= 0.0f)
            return (inputDb <= thresholdDb) ? inputDb
            : thresholdDb + ((inputDb - thresholdDb) / ratio);

        const float kneeStart = thresholdDb - (kneeWidthDb * 0.5f);
        const float kneeEnd = thresholdDb + (kneeWidthDb * 0.5f);

        if (inputDb <= kneeStart) return inputDb;

        if (inputDb <= kneeEnd)
        {
            const float a = (1.0f / (2.0f * kneeWidthDb * ratio)) - (1.0f / (2.0f * kneeWidthDb));
            const float b = -thresholdDb + (kneeWidthDb * 0.5f);
            const float x = inputDb + b;
            return inputDb + (a * x * x);
        }

        return thresholdDb + ((inputDb - thresholdDb) / ratio);
    }

    inline float compressUpwards(float inputDb, float thresholdDb,
        float ratio, float kneeWidthDb)
    {
        if (ratio <= 1.0001f) return inputDb;

        if (kneeWidthDb <= 0.0f)
            return (inputDb >= thresholdDb) ? inputDb
            : thresholdDb + ((inputDb - thresholdDb) / ratio);

        const float kneeStart = thresholdDb - (kneeWidthDb * 0.5f);
        const float kneeEnd = thresholdDb + (kneeWidthDb * 0.5f);

        if (inputDb >= kneeEnd) return inputDb;

        if (inputDb >= kneeStart)
        {
            // The upwards version negates the scale and flips the sign on the
            // knee half-width in the intercept
            const float a = -((1.0f / (2.0f * kneeWidthDb * ratio)) - (1.0f / (2.0f * kneeWidthDb)));
            const float b = -thresholdDb - (kneeWidthDb * 0.5f);
            const float x = inputDb + b;
            return inputDb + (a * x * x);
        }

        return thresholdDb + ((inputDb - thresholdDb) / ratio);
    }

    // Log-scaled spectral envelope via an O(N) prefix sum — the same routine
    // SpectralCompare uses for its morph, so the two plugins behave alike.
    constexpr int kMinHalfWidth = 3;

    void computeLogEnvelope(const float* mags, float* env, int n,
        float logFraction, double* prefixSum)
    {
        prefixSum[0] = 0.0;
        for (int i = 0; i < n; ++i)
            prefixSum[i + 1] = prefixSum[i] + (double)mags[i];

        for (int i = 0; i < n; ++i)
        {
            const int hw = juce::jmax(kMinHalfWidth, (int)(i * logFraction));
            const int lo = juce::jmax(0, i - hw);
            const int hi = juce::jmin(n - 1, i + hw);
            const int cnt = hi - lo + 1;
            env[i] = (float)((prefixSum[hi + 1] - prefixSum[lo]) / (double)cnt);
        }
    }

    constexpr float kMorphEnvSmooth = 0.5f;   // IIR on the sidechain envelope
    constexpr float kMorphMaxScale = 50.0f;

    constexpr int   kFFTSizes[] = { 512, 1024, 2048, 4096, 8192 };
    constexpr int   kOverlaps[] = { 2, 4, 8, 16 };
    constexpr float kMinusInfDB = -100.0f;
    constexpr float kEnvelopeInit = 0.0883883f;   // -24 dB sine RMS, as in the original
    constexpr float kTimingFadeMs = 150.0f;
}

// ============================================================================
//  Parameters
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SpectralCompressAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbString = [](float v, int) { return String(v, 1) + " dB"; };
    auto msString = [](float v, int) {
        return (v < 10.0f) ? String(v, 2) + " ms" : String(v, 1) + " ms";
        };
    auto pctString = [](float v, int) { return String(roundToInt(v * 100.0f)) + " %"; };
    auto ratioString = [](float v, int) {
        return (v <= 1.005f) ? String("off") : String(v, (v < 10.0f ? 2 : 1)) + " : 1";
        };

    // ---- global -----------------------------------------------------------
    layout.add(std::make_unique<AudioParameterFloat>(
        "gain", "Output Gain",
        NormalisableRange<float>(-30.0f, 30.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(dbString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "mix", "Mix",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    layout.add(std::make_unique<AudioParameterChoice>(
        "fftSize", "FFT Size",
        StringArray{ "512", "1024", "2048", "4096", "8192" }, 2));

    layout.add(std::make_unique<AudioParameterChoice>(
        "overlap", "Window Overlap",
        StringArray{ "2x", "4x", "8x", "16x" }, 1));

    layout.add(std::make_unique<AudioParameterFloat>(
        "attack", "Attack",
        NormalisableRange<float>(0.0f, 250.0f, 0.01f, 0.35f), 5.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(msString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "release", "Release",
        NormalisableRange<float>(0.0f, 1000.0f, 0.1f, 0.35f), 60.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(msString)));

    layout.add(std::make_unique<AudioParameterChoice>(
        "mode", "Mode",
        StringArray{ "Level Curve", "Sidechain Match", "Sidechain Compress" }, 0));

    // Hear only what the compressors changed: wet minus the delay-aligned dry
    layout.add(std::make_unique<AudioParameterBool>("delta", "Delta", false));

    // ---- downwards --------------------------------------------------------
    layout.add(std::make_unique<AudioParameterFloat>(
        "downOffset", "Down Offset",
        NormalisableRange<float>(-40.0f, 40.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(dbString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "downRatio", "Down Ratio",
        NormalisableRange<float>(1.0f, 300.0f, 0.01f, 0.22f), 4.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(ratioString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "downKnee", "Down Knee",
        NormalisableRange<float>(0.0f, 36.0f, 0.1f), 6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(dbString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "downAmount", "Down Amount",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    // ---- upwards ----------------------------------------------------------
    layout.add(std::make_unique<AudioParameterFloat>(
        "upOffset", "Up Offset",
        NormalisableRange<float>(-40.0f, 40.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(dbString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "upRatio", "Up Ratio",
        NormalisableRange<float>(1.0f, 300.0f, 0.01f, 0.22f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(ratioString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "upKnee", "Up Knee",
        NormalisableRange<float>(0.0f, 36.0f, 0.1f), 6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(dbString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "upAmount", "Up Amount",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    // ---- sidechain / stereo ----------------------------------------------
    layout.add(std::make_unique<AudioParameterFloat>(
        "scMorph", "SC Morph",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    // Envelope-transfer morph: reshapes the input's spectral envelope into the
    // sidechain's.  0 = off, 1 = full transfer, 2 = exaggerated.
    layout.add(std::make_unique<AudioParameterFloat>(
        "morphAmount", "Spectral Morph",
        NormalisableRange<float>(0.0f, 2.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 2) + " x"; })));

    // Clarity / envelope width.  The slider is inverted so that turning right
    // gives the sharpest (most detailed) envelope, as in SpectralCompare.
    {
        NormalisableRange<float> clarityRange(
            0.01f, 0.5f,
            [](float mn, float mx, float t) { return mx - t * (mx - mn); },
            [](float mn, float mx, float v) { return (mx - v) / (mx - mn); });
        clarityRange.interval = 0.005f;
        layout.add(std::make_unique<AudioParameterFloat>(
            "morphClarity", "Morph Clarity",
            clarityRange, 0.25f,
            AudioParameterFloatAttributes().withStringFromValueFunction(
                [](float v, int) { return juce::String(v, 3); })));
    }

    // Rotates the drawn curve across the bins, wrapping at both ends
    layout.add(std::make_unique<AudioParameterFloat>(
        "curveShift", "Curve Shift",
        NormalisableRange<float>(-100.0f, 100.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1) + " %"; })));

    // Display-only smoothing for the analyser
    layout.add(std::make_unique<AudioParameterFloat>(
        "visSmooth", "Visual Smooth",
        NormalisableRange<float>(0.0f, 0.97f, 0.001f), 0.6f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "scLink", "SC Link",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    layout.add(std::make_unique<AudioParameterFloat>(
        "stereoLink", "Stereo Link",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(pctString)));

    return layout;
}

// ============================================================================
//  Construction
// ============================================================================
SpectralCompressAudioProcessor::SpectralCompressAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
    apvts(*this, nullptr, "STATE", createParameterLayout())
{
    curveDB_write.fill(kCurveDefaultDB);
    curveDB_read.fill(kCurveDefaultDB);

    pGain = apvts.getRawParameterValue("gain");
    pMix = apvts.getRawParameterValue("mix");
    pFFTSize = apvts.getRawParameterValue("fftSize");
    pOverlap = apvts.getRawParameterValue("overlap");
    pAttack = apvts.getRawParameterValue("attack");
    pRelease = apvts.getRawParameterValue("release");
    pMode = apvts.getRawParameterValue("mode");
    pDelta = apvts.getRawParameterValue("delta");

    pDownOffset = apvts.getRawParameterValue("downOffset");
    pDownRatio = apvts.getRawParameterValue("downRatio");
    pDownKnee = apvts.getRawParameterValue("downKnee");
    pDownAmount = apvts.getRawParameterValue("downAmount");

    pUpOffset = apvts.getRawParameterValue("upOffset");
    pUpRatio = apvts.getRawParameterValue("upRatio");
    pUpKnee = apvts.getRawParameterValue("upKnee");
    pUpAmount = apvts.getRawParameterValue("upAmount");

    pScMorph = apvts.getRawParameterValue("scMorph");
    pMorphAmount = apvts.getRawParameterValue("morphAmount");
    pMorphClarity = apvts.getRawParameterValue("morphClarity");
    pCurveShift = apvts.getRawParameterValue("curveShift");
    pVisSmooth = apvts.getRawParameterValue("visSmooth");
    pScLink = apvts.getRawParameterValue("scLink");
    pStereoLink = apvts.getRawParameterValue("stereoLink");

    apvts.addParameterListener("fftSize", this);
    apvts.addParameterListener("overlap", this);

    fftOrder = 11;
    fftSize = 1 << fftOrder;
    overlapTimes = 4;
    hopSize = fftSize / overlapTimes;
    numBins = fftSize / 2 + 1;

    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

SpectralCompressAudioProcessor::~SpectralCompressAudioProcessor()
{
    apvts.removeParameterListener("fftSize", this);
    apvts.removeParameterListener("overlap", this);
}

// ============================================================================
//  Boilerplate
// ============================================================================
const juce::String SpectralCompressAudioProcessor::getName() const { return JucePlugin_Name; }
bool   SpectralCompressAudioProcessor::acceptsMidi() const { return false; }
bool   SpectralCompressAudioProcessor::producesMidi() const { return false; }
bool   SpectralCompressAudioProcessor::isMidiEffect() const { return false; }
double SpectralCompressAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    SpectralCompressAudioProcessor::getNumPrograms() { return 1; }
int    SpectralCompressAudioProcessor::getCurrentProgram() { return 0; }
void   SpectralCompressAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralCompressAudioProcessor::getProgramName(int) { return {}; }
void   SpectralCompressAudioProcessor::changeProgramName(int, const juce::String&) {}
bool   SpectralCompressAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralCompressAudioProcessor::createEditor()
{
    return new SpectralCompressAudioProcessorEditor(*this);
}

bool SpectralCompressAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (mainIn != mainOut)
        return false;

    // The sidechain bus may be disabled, mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet(true, 1);
        if (!sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

// ============================================================================
//  Allocation / configuration
// ============================================================================
void SpectralCompressAudioProcessor::createWindow()
{
    window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
    {
        const float n = (float)i / (float)(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }

    // Analysis and synthesis both use the Hann window, so the overlap-add of
    // w^2 has to be normalised.  Rather than relying on the analytic value
    // (0.375 * overlap for Hann) we measure it, which keeps this correct if the
    // window is ever swapped out.
    float maxSum = 0.0f;
    for (int i = 0; i < hopSize; ++i)
    {
        float sum = 0.0f;
        for (int k = i; k < fftSize; k += hopSize)
            sum += window[k] * window[k];
        maxSum = juce::jmax(maxSum, sum);
    }
    olaNorm = (maxSum > 0.0f) ? (1.0f / maxSum) : 1.0f;
}

void SpectralCompressAudioProcessor::allocateBuffers()
{
    const int nCh = 2;

    inputFifo.assign(nCh, std::vector<float>(fftSize, 0.0f));
    scFifo.assign(nCh, std::vector<float>(fftSize, 0.0f));
    fftBuffer.assign(nCh, std::vector<float>(fftSize * 2, 0.0f));
    scBuffer.assign(nCh, std::vector<float>(fftSize * 2, 0.0f));
    outputAccum.assign(nCh, std::vector<float>(fftSize * 4, 0.0f));

    fifoIndex.assign(nCh, 0);
    outputWritePos.assign(nCh, 0);

    envelopes.assign(nCh, std::vector<float>(numBins, kEnvelopeInit));
    mainMags.assign(nCh, std::vector<float>(numBins, 0.0f));
    scMagnitudes.assign(nCh, std::vector<float>(numBins, 0.0f));
    grAccum.assign(numBins, 0.0f);

    morphMainEnv.assign(nCh, std::vector<float>(numBins, 0.0f));
    morphSideEnv.assign(numBins, 0.0f);
    morphEnvTmp.assign(numBins, 0.0f);
    morphMonoSide.assign(numBins, 0.0f);
    morphPrefixSum.assign((size_t)numBins + 1, 0.0);

    dryDelaySize = maxFFTSize * 2;
    dryDelay.assign(nCh, std::vector<float>(dryDelaySize, 0.0f));
    dryWritePos = 0;

    {
        juce::ScopedLock l(displayLock);
        displayInput.assign(numBins, 0.0f);
        displaySidechain.assign(numBins, 0.0f);
        displayGR.assign(numBins, 0.0f);
    }

    createWindow();
    envelopeTimingScale = 0.0f;
}

void SpectralCompressAudioProcessor::resetState()
{
    for (int ch = 0; ch < (int)inputFifo.size(); ++ch)
    {
        std::fill(inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);
        std::fill(scFifo[ch].begin(), scFifo[ch].end(), 0.0f);
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(dryDelay[ch].begin(), dryDelay[ch].end(), 0.0f);
        std::fill(envelopes[ch].begin(), envelopes[ch].end(), kEnvelopeInit);
        fifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
    }
    dryWritePos = 0;
    envelopeTimingScale = 0.0f;
}

void SpectralCompressAudioProcessor::applyFFTConfig(int newFFTSize, int newOverlap)
{
    newFFTSize = juce::jlimit(512, maxFFTSize, newFFTSize);
    newOverlap = juce::jlimit(2, 16, newOverlap);
    if (newFFTSize == fftSize && newOverlap == overlapTimes)
        return;

    suspendProcessing(true);

    const int oldNumBins = numBins;
    const int newNumBins = newFFTSize / 2 + 1;

    // Resample the drawn curve so the shape survives an FFT size change
    if (newNumBins != oldNumBins)
    {
        std::array<float, maxBins> resampled;
        resampled.fill(kCurveDefaultDB);
        {
            juce::ScopedLock l(curveLock);
            for (int i = 0; i < newNumBins; ++i)
            {
                const float pos = (float)i * (float)(oldNumBins - 1) / (float)(newNumBins - 1);
                const int   lo = juce::jlimit(0, oldNumBins - 1, (int)pos);
                const int   hi = juce::jlimit(0, oldNumBins - 1, lo + 1);
                const float t = pos - (float)lo;
                resampled[i] = curveDB_write[lo] * (1.0f - t) + curveDB_write[hi] * t;
            }
            curveDB_write = resampled;
        }
        curveDirty.store(true);
    }

    int order = 9;
    while ((1 << order) < newFFTSize) ++order;

    fftOrder = order;
    fftSize = newFFTSize;
    overlapTimes = newOverlap;
    hopSize = fftSize / overlapTimes;
    numBins = newNumBins;

    fft = std::make_unique<juce::dsp::FFT>(fftOrder);

    allocateBuffers();
    resetState();

    {
        juce::ScopedLock l(curveLock);
        std::copy(curveDB_write.begin(), curveDB_write.end(), curveDB_read.begin());
    }
    curveDirty.store(false);

    setLatencySamples(fftSize);
    suspendProcessing(false);
}

void SpectralCompressAudioProcessor::parameterChanged(const juce::String& paramID, float newValue)
{
    if (paramID == "fftSize")
    {
        const int idx = juce::jlimit(0, 4, (int)newValue);
        pendingFFTSize.store(kFFTSizes[idx]);
        triggerAsyncUpdate();
    }
    else if (paramID == "overlap")
    {
        const int idx = juce::jlimit(0, 3, (int)newValue);
        pendingOverlap.store(kOverlaps[idx]);
        triggerAsyncUpdate();
    }
}

void SpectralCompressAudioProcessor::handleAsyncUpdate()
{
    const int newSize = pendingFFTSize.exchange(0);
    const int newOverlap = pendingOverlap.exchange(0);
    applyFFTConfig(newSize > 0 ? newSize : fftSize,
        newOverlap > 0 ? newOverlap : overlapTimes);
}

// ============================================================================
//  prepareToPlay
// ============================================================================
void SpectralCompressAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    scScratch.setSize(2, juce::jmax(512, samplesPerBlock), false, true, true);

    const int wantedSize = kFFTSizes[juce::jlimit(0, 4, (int)pFFTSize->load())];
    const int wantedOverlap = kOverlaps[juce::jlimit(0, 3, (int)pOverlap->load())];

    if (wantedSize != fftSize || wantedOverlap != overlapTimes)
    {
        applyFFTConfig(wantedSize, wantedOverlap);
    }
    else
    {
        allocateBuffers();
        resetState();
    }

    gainSmoothed.reset(sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue(dbToGain(pGain->load()));
    mixSmoothed.reset(sampleRate, 0.02);
    mixSmoothed.setCurrentAndTargetValue(pMix->load());

    setLatencySamples(fftSize);
}

void SpectralCompressAudioProcessor::releaseResources() {}

// ============================================================================
//  The compressor bank — runs once per hop
// ============================================================================
void SpectralCompressAudioProcessor::processFrame(int nCh, bool scActive)
{
    const int   nb = numBins;
    const float normScale = 2.0f / (float)fftSize;
    const int   mode = juce::jlimit(0, 2, (int)pMode->load(std::memory_order_relaxed));
    const float morphAmount = pMorphAmount->load(std::memory_order_relaxed);
    const bool  morphOn = scActive && (morphAmount > 0.001f);
    const bool  needSC = scActive && ((mode != ModeLevel) || morphOn);

    // ---- refresh the read side of the drawn curve -------------------------
    if (curveDirty.load(std::memory_order_relaxed))
    {
        const juce::ScopedTryLock l(curveLock);
        if (l.isLocked())
        {
            std::copy(curveDB_write.begin(), curveDB_write.begin() + nb, curveDB_read.begin());
            curveDirty.store(false, std::memory_order_relaxed);
        }
    }

    // ---- forward transforms ----------------------------------------------
    for (int ch = 0; ch < nCh; ++ch)
    {
        for (int i = 0; i < fftSize; ++i)
            fftBuffer[ch][i] = inputFifo[ch][i] * window[i];
        std::fill(fftBuffer[ch].begin() + fftSize, fftBuffer[ch].end(), 0.0f);

        fft->performRealOnlyForwardTransform(fftBuffer[ch].data());

        for (int b = 0; b < nb; ++b)
        {
            const float re = fftBuffer[ch][b * 2];
            const float im = fftBuffer[ch][b * 2 + 1];
            mainMags[ch][b] = std::sqrt(re * re + im * im) * normScale;
        }
    }

    if (needSC)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            for (int i = 0; i < fftSize; ++i)
                scBuffer[ch][i] = scFifo[ch][i] * window[i];
            std::fill(scBuffer[ch].begin() + fftSize, scBuffer[ch].end(), 0.0f);

            fft->performRealOnlyForwardTransform(scBuffer[ch].data());

            for (int b = 0; b < nb; ++b)
            {
                const float re = scBuffer[ch][b * 2];
                const float im = scBuffer[ch][b * 2 + 1];
                scMagnitudes[ch][b] = std::sqrt(re * re + im * im) * normScale;
            }
        }
    }
    else
    {
        for (int ch = 0; ch < nCh; ++ch)
            std::fill(scMagnitudes[ch].begin(), scMagnitudes[ch].begin() + nb, 0.0f);
    }

    // ---- spectral morph (envelope transfer) --------------------------------
    // Ported from SpectralCompare: build a log-scaled spectral envelope for the
    // sidechain and for the input, then scale every bin by (sideEnv/mainEnv)^morph.
    // At 1.0 the input takes on the sidechain's spectral shape while keeping its
    // own fine structure; the Clarity knob sets how wide the envelope averaging is.
    // This runs before the compressor bank, so the envelope followers and the
    // analyser all see the morphed signal.
    if (morphOn)
    {
        const float clarity = pMorphClarity->load(std::memory_order_relaxed);
        const float invCh = 1.0f / (float)juce::jmax(1, nCh);

        for (int b = 0; b < nb; ++b)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < nCh; ++ch) sum += scMagnitudes[ch][b];
            morphMonoSide[b] = sum * invCh;
        }

        computeLogEnvelope(morphMonoSide.data(), morphEnvTmp.data(), nb,
            clarity, morphPrefixSum.data());

        for (int b = 0; b < nb; ++b)
            morphSideEnv[b] = morphSideEnv[b] * kMorphEnvSmooth
            + morphEnvTmp[b] * (1.0f - kMorphEnvSmooth);

        const float maxRatio = std::pow(kMorphMaxScale,
            1.0f / juce::jmax(morphAmount, 0.01f));

        for (int ch = 0; ch < nCh; ++ch)
        {
            computeLogEnvelope(mainMags[ch].data(), morphMainEnv[ch].data(), nb,
                clarity, morphPrefixSum.data());

            for (int b = 0; b < nb; ++b)
            {
                float sc = 1.0f;
                if (morphMainEnv[ch][b] > 1.0e-10f)
                {
                    const float ratio = (morphSideEnv[b] > 1.0e-10f)
                        ? juce::jmin(morphSideEnv[b] / morphMainEnv[ch][b], maxRatio)
                        : 0.0f;
                    sc = std::pow(ratio, morphAmount);
                }

                fftBuffer[ch][b * 2] *= sc;
                fftBuffer[ch][b * 2 + 1] *= sc;
                mainMags[ch][b] *= sc;
            }
        }
    }

    // ---- envelope follower coefficients -----------------------------------
    // The effective sample rate is one frame per hop, exactly as in the original.
    const float effSR = (float)currentSampleRate / (float)hopSize;

    const float attackMs = pAttack->load(std::memory_order_relaxed) * envelopeTimingScale;
    const float releaseMs = pRelease->load(std::memory_order_relaxed) * envelopeTimingScale;

    const float attackOld = (attackMs <= 0.0f) ? 0.0f
        : std::exp(-1.0f / (attackMs * 0.001f * effSR));
    const float attackNew = 1.0f - attackOld;
    const float releaseOld = (releaseMs <= 0.0f) ? 0.0f
        : std::exp(-1.0f / (releaseMs * 0.001f * effSR));
    const float releaseNew = 1.0f - releaseOld;

    if (envelopeTimingScale < 1.0f)
        envelopeTimingScale = juce::jmin(1.0f,
            envelopeTimingScale + 1.0f / ((kTimingFadeMs / 1000.0f) * effSR));

    // ---- channel-link weights ---------------------------------------------
    const float chCount = (float)juce::jmax(1, nCh);
    const float scLink = pScLink->load(std::memory_order_relaxed);
    const float scOther = scLink / chCount;
    const float scThis = 1.0f - (scOther * (chCount - 1.0f));

    const float stLink = pStereoLink->load(std::memory_order_relaxed);
    const float stOther = stLink / chCount;
    const float stThis = 1.0f - (stOther * (chCount - 1.0f));

    // ---- compressor settings ----------------------------------------------
    const float downOffset = pDownOffset->load(std::memory_order_relaxed);
    const float downRatio = pDownRatio->load(std::memory_order_relaxed);
    const float downKnee = pDownKnee->load(std::memory_order_relaxed);
    const float downAmount = pDownAmount->load(std::memory_order_relaxed);

    const float upOffset = pUpOffset->load(std::memory_order_relaxed);
    const float upRatio = pUpRatio->load(std::memory_order_relaxed);
    const float upKnee = pUpKnee->load(std::memory_order_relaxed);
    const float upAmount = pUpAmount->load(std::memory_order_relaxed);

    const float morph = needSC ? pScMorph->load(std::memory_order_relaxed) : 0.0f;

    // Upwards compression on the DC bin (and the two bins the Hann window
    // smears it into) only ever amplifies rumble, so it is skipped there.
    const int firstNonDCBin = 2;

    const int curveShift = getCurveShiftBins();

    std::fill(grAccum.begin(), grAccum.begin() + nb, 0.0f);

    for (int ch = 0; ch < nCh; ++ch)
    {
        for (int b = 0; b < nb; ++b)
        {
            // -- detector magnitude ----------------------------------------
            float detector;
            if (mode == ModeSidechain)
            {
                detector = 0.0f;
                for (int c = 0; c < nCh; ++c)
                    detector += scMagnitudes[c][b] * (c == ch ? scThis : scOther);
            }
            else
            {
                detector = 0.0f;
                for (int c = 0; c < nCh; ++c)
                    detector += mainMags[c][b] * (c == ch ? stThis : stOther);
            }

            // -- envelope follower -----------------------------------------
            float& env = envelopes[ch][b];
            if (env > detector) env = (releaseOld * env) + (releaseNew * detector);
            else                env = (attackOld * env) + (attackNew * detector);

            const float envDb = gainToDb(env);

            // -- target level for this bin ---------------------------------
            // Level / SC-Compress: the drawn curve is the target.
            // SC-Match: the target morphs from the drawn curve towards the
            // sidechain's own spectrum, so at 100% the input is reshaped into
            // the sidechain's spectral envelope.
            int srcBin = b - curveShift;
            if (srcBin < 0)   srcBin += nb;
            if (srcBin >= nb) srcBin -= nb;

            float target = curveDB_read[srcBin];
            if (mode == ModeMatch && morph > 0.0f)
            {
                float scMag = 0.0f;
                for (int c = 0; c < nCh; ++c)
                    scMag += scMagnitudes[c][b] * (c == ch ? scThis : scOther);
                const float scDb = juce::jmax(kMinusInfDB, gainToDb(scMag));
                target = target * (1.0f - morph) + scDb * morph;
            }

            float gainDb = 0.0f;

            if (envDb > kMinusInfDB)
            {
                const float thresholdDown = target + downOffset;
                const float thresholdUp = target + upOffset;

                const float outDown = compressDownwards(envDb, thresholdDown, downRatio, downKnee);
                const float outUp = (b >= firstNonDCBin)
                    ? compressUpwards(envDb, thresholdUp, upRatio, upKnee)
                    : envDb;

                // Each direction contributes its own delta, so the two banks
                // stack the same way they do in the original.
                gainDb = (outDown - envDb) * downAmount + (outUp - envDb) * upAmount;
                gainDb = juce::jlimit(-60.0f, 60.0f, gainDb);
            }

            grAccum[b] += gainDb;

            const float g = dbToGain(gainDb);
            fftBuffer[ch][b * 2] *= g;
            fftBuffer[ch][b * 2 + 1] *= g;
        }

        // -- rebuild the conjugate-symmetric half before the inverse --------
        for (int b = 1; b < fftSize / 2; ++b)
        {
            fftBuffer[ch][(fftSize - b) * 2] = fftBuffer[ch][b * 2];
            fftBuffer[ch][(fftSize - b) * 2 + 1] = -fftBuffer[ch][b * 2 + 1];
        }
        fftBuffer[ch][1] = 0.0f;
        fftBuffer[ch][(fftSize / 2) * 2 + 1] = 0.0f;
    }

    // ---- display handoff ---------------------------------------------------
    {
        const juce::ScopedTryLock l(displayLock);
        if (l.isLocked())
        {
            const float smooth = juce::jlimit(0.0f, 0.97f,
                pVisSmooth->load(std::memory_order_relaxed));
            const float inv = 1.0f / (float)juce::jmax(1, nCh);
            for (int b = 0; b < nb; ++b)
            {
                float m = 0.0f, s = 0.0f;
                for (int ch = 0; ch < nCh; ++ch) { m += mainMags[ch][b]; s += scMagnitudes[ch][b]; }
                m *= inv; s *= inv;

                displayInput[b] = displayInput[b] * smooth + m * (1.0f - smooth);
                displaySidechain[b] = displaySidechain[b] * smooth + s * (1.0f - smooth);
                displayGR[b] = displayGR[b] * smooth + (grAccum[b] * inv) * (1.0f - smooth);
            }
        }
    }

    // ---- inverse transform + overlap-add ----------------------------------
    for (int ch = 0; ch < nCh; ++ch)
    {
        fft->performRealOnlyInverseTransform(fftBuffer[ch].data());

        const int accumSize = (int)outputAccum[ch].size();
        for (int i = 0; i < fftSize; ++i)
        {
            const int idx = (outputWritePos[ch] + i) % accumSize;
            outputAccum[ch][idx] += fftBuffer[ch][i] * window[i] * olaNorm;
        }
    }
}

// ============================================================================
//  processBlock
// ============================================================================
void SpectralCompressAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    const int nCh = juce::jlimit(1, 2, mainBus.getNumChannels());

    // Sidechain: read it before we start writing into the main bus, since the
    // two buses can share the same underlying channel array.
    bool scActive = false;
    if (scScratch.getNumSamples() < numSamples || scScratch.getNumChannels() < nCh)
        scScratch.setSize(juce::jmax(2, nCh), juce::jmax(numSamples, 512), false, true, true);
    scScratch.clear(0, numSamples);

    if (auto* scBusPtr = getBus(true, 1))
    {
        if (scBusPtr->isEnabled())
        {
            auto scBus = getBusBuffer(buffer, true, 1);
            const int scCh = scBus.getNumChannels();
            if (scCh > 0)
            {
                scActive = true;
                for (int ch = 0; ch < nCh; ++ch)
                    scScratch.copyFrom(ch, 0, scBus, juce::jmin(ch, scCh - 1), 0, numSamples);
            }
        }
    }
    scConnected.store(scActive, std::memory_order_relaxed);

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);

    gainSmoothed.setTargetValue(dbToGain(pGain->load(std::memory_order_relaxed)));
    mixSmoothed.setTargetValue(pMix->load(std::memory_order_relaxed));

    const bool deltaMode = pDelta->load(std::memory_order_relaxed) >= 0.5f;

    for (int s = 0; s < numSamples; ++s)
    {
        // ---- feed the analysis fifos + the delayed dry path ---------------
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float in = mainBus.getReadPointer(ch)[s];
            if (fifoIndex[ch] < fftSize)
            {
                inputFifo[ch][fifoIndex[ch]] = in;
                scFifo[ch][fifoIndex[ch]] = scActive ? scScratch.getReadPointer(ch)[s] : 0.0f;
            }
            dryDelay[ch][dryWritePos] = in;
        }
        for (int ch = 0; ch < nCh; ++ch)
            ++fifoIndex[ch];

        // ---- fire a frame every hopSize samples ---------------------------
        if (fifoIndex[0] >= fftSize)
        {
            processFrame(nCh, scActive);

            for (int ch = 0; ch < nCh; ++ch)
            {
                std::copy(inputFifo[ch].begin() + hopSize, inputFifo[ch].end(),
                    inputFifo[ch].begin());
                std::fill(inputFifo[ch].end() - hopSize, inputFifo[ch].end(), 0.0f);

                std::copy(scFifo[ch].begin() + hopSize, scFifo[ch].end(),
                    scFifo[ch].begin());
                std::fill(scFifo[ch].end() - hopSize, scFifo[ch].end(), 0.0f);

                fifoIndex[ch] -= hopSize;
            }
        }

        // ---- read the wet output, mix the delayed dry back in -------------
        const float mix = mixSmoothed.getNextValue();
        const float gain = gainSmoothed.getNextValue();

        const int readPos = (dryWritePos - fftSize + dryDelaySize) % dryDelaySize;

        for (int ch = 0; ch < nCh; ++ch)
        {
            const int accumSize = (int)outputAccum[ch].size();
            const float wet = outputAccum[ch][outputWritePos[ch]];
            outputAccum[ch][outputWritePos[ch]] = 0.0f;
            outputWritePos[ch] = (outputWritePos[ch] + 1) % accumSize;

            const float dry = dryDelay[ch][readPos];

            // In delta mode the dry signal is subtracted instead of mixed in, so
            // you only hear what the compressor bank actually changed.  Mix then
            // scales how much of that difference is sent out.
            const float out = deltaMode ? (wet - dry) * mix
                : (wet * mix + dry * (1.0f - mix));

            outBus.getWritePointer(ch)[s] = out * gain;
        }

        dryWritePos = (dryWritePos + 1) % dryDelaySize;
    }
}

// ============================================================================
//  Theme
// ============================================================================
void SpectralCompressAudioProcessor::setLightMode(bool shouldBeLight)
{
    lightMode = shouldBeLight;
    applyTheme();
}

void SpectralCompressAudioProcessor::applyTheme()
{
    if (lightMode)
    {
        backgroundColor = juce::Colour(0xfffbfcfd);
        sidebarColor = juce::Colour(0xffeceff2);
        gridColor = juce::Colour(0xffb4bcc4);
        textColor = juce::Colour(0xff1e2429);
        accentColor = juce::Colour(0xff2f86d6);   // the same blue, on white
        spectrumColor = juce::Colour(0xff5f7d8c);
        outputColor = juce::Colour(0xff2f86d6);
        sidechainColor = juce::Colour(0xffb03a95);
        curveColor = juce::Colour(0xffd08a00);
        downColor = juce::Colour(0xffd2402f);
        upColor = juce::Colour(0xff1f9e46);
    }
    else
    {
        backgroundColor = juce::Colour(0xff141618);
        sidebarColor = juce::Colour(0xff1b1e21);
        gridColor = juce::Colour(0xff3a4046);
        textColor = juce::Colour(0xffc8d0d6);
        accentColor = juce::Colour(0xff55eedd);
        spectrumColor = juce::Colour(0xff2f7f74);
        outputColor = juce::Colour(0xff8fd6ff);
        sidechainColor = juce::Colour(0xffdd55bb);
        curveColor = juce::Colour(0xffffcc33);
        downColor = juce::Colour(0xffff6b5c);
        upColor = juce::Colour(0xff7dff8f);
    }
}

// ============================================================================
//  Curve API  (message thread)
// ============================================================================
void SpectralCompressAudioProcessor::setCurveRange(int startBin, int endBin,
    float startDB, float endDB)
{
    if (startBin > endBin) { std::swap(startBin, endBin); std::swap(startDB, endDB); }
    startBin = juce::jlimit(0, numBins - 1, startBin);
    endBin = juce::jlimit(0, numBins - 1, endBin);
    startDB = juce::jlimit(kCurveMinDB, kCurveMaxDB, startDB);
    endDB = juce::jlimit(kCurveMinDB, kCurveMaxDB, endDB);

    {
        juce::ScopedLock l(curveLock);
        const int span = endBin - startBin;
        if (span == 0)
        {
            curveDB_write[startBin] = startDB;
        }
        else
        {
            for (int b = startBin; b <= endBin; ++b)
            {
                const float t = (float)(b - startBin) / (float)span;
                curveDB_write[b] = startDB + t * (endDB - startDB);
            }
        }
    }
    curveDirty.store(true);
}

void SpectralCompressAudioProcessor::resetCurve(float dB)
{
    dB = juce::jlimit(kCurveMinDB, kCurveMaxDB, dB);
    { juce::ScopedLock l(curveLock); curveDB_write.fill(dB); }
    curveDirty.store(true);
}

int SpectralCompressAudioProcessor::getCurveShiftBins() const
{
    if (pCurveShift == nullptr || numBins < 2) return 0;
    const float pct = pCurveShift->load(std::memory_order_relaxed);
    int shift = juce::roundToInt(pct * 0.01f * (float)(numBins - 1));
    shift %= numBins;
    if (shift < 0) shift += numBins;
    return shift;
}

void SpectralCompressAudioProcessor::learnCurveFromInput(float offsetDB)
{
    std::vector<float> snapshot;
    {
        juce::ScopedLock l(displayLock);
        snapshot = displayInput;
    }
    if (snapshot.empty()) return;

    // Write through the shift so the learned curve lands where it is displayed
    const int shift = getCurveShiftBins();

    juce::ScopedLock l(curveLock);
    for (int b = 0; b < numBins && b < (int)snapshot.size(); ++b)
    {
        int dest = b - shift;
        if (dest < 0)        dest += numBins;
        if (dest >= numBins) dest -= numBins;

        const float dB = gainToDb(snapshot[b]) + offsetDB;
        curveDB_write[dest] = juce::jlimit(kCurveMinDB, kCurveMaxDB, dB);
    }
    curveDirty.store(true);
}

void SpectralCompressAudioProcessor::tiltCurve(float dbPerOctave)
{
    const int shift = getCurveShiftBins();

    juce::ScopedLock l(curveLock);
    const float refHz = 1000.0f;
    for (int b = 0; b < numBins; ++b)
    {
        int dest = b - shift;
        if (dest < 0)        dest += numBins;
        if (dest >= numBins) dest -= numBins;

        const float hz = juce::jmax(20.0f, binToHz(b));
        const float octaves = std::log2(hz / refHz);
        curveDB_write[dest] = juce::jlimit(kCurveMinDB, kCurveMaxDB,
            curveDB_write[dest] + octaves * dbPerOctave);
    }
    curveDirty.store(true);
}

void SpectralCompressAudioProcessor::getCurveData(float* dest, int numToCopy)
{
    juce::ScopedLock l(curveLock);
    const int n = juce::jmin(numToCopy, numBins);
    for (int i = 0; i < n; ++i) dest[i] = curveDB_write[i];
    for (int i = n; i < numToCopy; ++i) dest[i] = kCurveDefaultDB;
}

// ============================================================================
//  Display accessors
// ============================================================================
void SpectralCompressAudioProcessor::getInputSpectrum(float* dest, int numToCopy)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numToCopy, (int)displayInput.size());
    for (int i = 0; i < n; ++i) dest[i] = displayInput[i];
    for (int i = n; i < numToCopy; ++i) dest[i] = 0.0f;
}

void SpectralCompressAudioProcessor::getSidechainSpectrum(float* dest, int numToCopy)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numToCopy, (int)displaySidechain.size());
    for (int i = 0; i < n; ++i) dest[i] = displaySidechain[i];
    for (int i = n; i < numToCopy; ++i) dest[i] = 0.0f;
}

void SpectralCompressAudioProcessor::getGainReduction(float* dest, int numToCopy)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numToCopy, (int)displayGR.size());
    for (int i = 0; i < n; ++i) dest[i] = displayGR[i];
    for (int i = n; i < numToCopy; ++i) dest[i] = 0.0f;
}

// ============================================================================
//  State
// ============================================================================
void SpectralCompressAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Store the drawn curve alongside the parameters
    juce::ValueTree curveNode("CURVE");
    {
        juce::ScopedLock l(curveLock);
        curveNode.setProperty("numBins", numBins, nullptr);
        juce::MemoryBlock mb(curveDB_write.data(), sizeof(float) * (size_t)numBins);
        curveNode.setProperty("data", mb.toBase64Encoding(), nullptr);
    }
    state.appendChild(curveNode, nullptr);
    state.setProperty("lightMode", lightMode, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void SpectralCompressAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr) return;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid()) return;

    if (tree.hasProperty("lightMode"))
        setLightMode((bool)tree.getProperty("lightMode"));

    auto curveNode = tree.getChildWithName("CURVE");
    tree.removeChild(curveNode, nullptr);
    apvts.replaceState(tree);

    if (curveNode.isValid())
    {
        const int storedBins = (int)curveNode.getProperty("numBins", 0);
        juce::MemoryBlock mb;
        if (storedBins > 1 && mb.fromBase64Encoding(curveNode.getProperty("data").toString()))
        {
            const int available = (int)(mb.getSize() / sizeof(float));
            const int n = juce::jmin(storedBins, available, (int)maxBins);
            if (n > 1)
            {
                const float* src = static_cast<const float*>(mb.getData());
                juce::ScopedLock l(curveLock);
                for (int i = 0; i < numBins; ++i)
                {
                    const float pos = (float)i * (float)(n - 1) / (float)juce::jmax(1, numBins - 1);
                    const int   lo = juce::jlimit(0, n - 1, (int)pos);
                    const int   hi = juce::jlimit(0, n - 1, lo + 1);
                    const float t = pos - (float)lo;
                    curveDB_write[i] = juce::jlimit(kCurveMinDB, kCurveMaxDB,
                        src[lo] * (1.0f - t) + src[hi] * t);
                }
                curveDirty.store(true);
            }
        }
    }
}

// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralCompressAudioProcessor();
}
