#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
// Band-count lookup table  (shared with editor for combo population)
// ============================================================================
const int VocodeAudioProcessor::kBandCounts[VocodeAudioProcessor::kNumBandChoices] =
{ 1, 2, 4, 8, 12, 16, 20, 24, 32, 40, 48, 56, 64, 80, 96, 128 };

// ============================================================================
// Parameter layout
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocodeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Morph  [0 … 2]
    // 0 = dry carrier, 1 = full envelope transfer, 2 = over-drive
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morph", "Morph",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 1.0f,
        juce::AudioParameterFloatAttributes()
        .withLabel("x")
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));

    // Clarity / envelope width  [0.01 … 0.5]
    // Inverted display: right = sharpest (0.01), left = widest (0.5).
    // (In analog mode this knob has no effect — Q is set by band spacing.)
    {
        juce::NormalisableRange<float> r(
            0.01f, 0.5f,
            [](float mn, float mx, float t) { return mx - t * (mx - mn); },
            [](float mn, float mx, float v) { return (mx - v) / (mx - mn); });
        r.interval = 0.005f;
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "clarity", "Clarity", r, 0.25f,
            juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 3); })));
    }

    // Smoothing  [0 … 0.99]
    // FFT mode  : display IIR + sidechain spectral envelope glide.
    // Analog mode: envelope-follower release time (5 ms … 500 ms).
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "smoothing", "Smoothing",
        juce::NormalisableRange<float>(0.0f, 0.99f, 0.01f), 0.70f,
        juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));

    // Gate  [-80 … 0 dBFS]
    // FFT mode  : attenuates morphFrame when sidechain energy is below threshold.
    // Analog mode: suppresses bands whose modulator envelope is below threshold.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gate", "Gate",
        juce::NormalisableRange<float>(-80.0f, 0.0f, 0.5f), -50.0f,
        juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));

    // Formant Shift  [-24 … +24 semitones]
    // Shifts the sidechain spectral envelope relative to the carrier bands before
    // imprinting.  Positive = brighter / robotic; negative = darker / bass-heavy.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "formant", "Formant",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.5f), 0.0f,
        juce::AudioParameterFloatAttributes()
        .withLabel("st")
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 1); })));

    // Analog mode toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "analogMode", "Analog Mode", false));

    // FFT Size — 9 choices: 32 … 8192
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fftSize", "FFT Size",
        juce::StringArray{ "32","64","128","256","512","1024","2048","4096","8192" }, 6));

    // Number of analog bands — 16 choices
    {
        juce::StringArray bandStrs;
        for (int i = 0; i < kNumBandChoices; ++i)
            bandStrs.add(juce::String(kBandCounts[i]));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            "numBands", "Num Bands", bandStrs, 8));   // default index 8 → 32 bands
    }

    return { params.begin(), params.end() };
}

// ============================================================================
// Constructor / Destructor
// ============================================================================
VocodeAudioProcessor::VocodeAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Main", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Vocode", createParameterLayout())
{
    morphParam = apvts.getRawParameterValue("morph");
    clarityParam = apvts.getRawParameterValue("clarity");
    smoothingParam = apvts.getRawParameterValue("smoothing");
    gateParam = apvts.getRawParameterValue("gate");
    formantParam = apvts.getRawParameterValue("formant");
    analogModeParam = apvts.getRawParameterValue("analogMode");
    numBandsParam = apvts.getRawParameterValue("numBands");

    apvts.addParameterListener("fftSize", this);
    apvts.addParameterListener("numBands", this);

    fftObj = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

VocodeAudioProcessor::~VocodeAudioProcessor()
{
    apvts.removeParameterListener("fftSize", this);
    apvts.removeParameterListener("numBands", this);
    cancelPendingUpdate();
}

// -----------------------------------------------------------------------
// Parameter listener + async rebuild
// -----------------------------------------------------------------------
void VocodeAudioProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id == "fftSize")
    {
        const int sizes[] = { 32,64,128,256,512,1024,2048,4096,8192 };
        const int idx = juce::roundToInt(v);
        if (idx >= 0 && idx < 9)
        {
            pendingFFTSize.store(sizes[idx], std::memory_order_relaxed);
            triggerAsyncUpdate();
        }
    }
    else if (id == "numBands")
    {
        pendingNumBands.store(1, std::memory_order_relaxed);   // dirty flag
        triggerAsyncUpdate();
    }
}

void VocodeAudioProcessor::handleAsyncUpdate()
{
    const int fftSz = pendingFFTSize.exchange(0, std::memory_order_relaxed);
    if (fftSz > 0) setFFTSize(fftSz);

    const int nbFlag = pendingNumBands.exchange(0, std::memory_order_relaxed);
    if (nbFlag != 0)
    {
        suspendProcessing(true);
        rebuildAnalogBands();
        suspendProcessing(false);
    }
}

// ============================================================================
// Standard boilerplate
// ============================================================================
const juce::String VocodeAudioProcessor::getName() const { return JucePlugin_Name; }
bool VocodeAudioProcessor::acceptsMidi()  const { return false; }
bool VocodeAudioProcessor::producesMidi() const { return false; }
bool VocodeAudioProcessor::isMidiEffect() const { return false; }
double VocodeAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  VocodeAudioProcessor::getNumPrograms() { return 1; }
int  VocodeAudioProcessor::getCurrentProgram() { return 0; }
void VocodeAudioProcessor::setCurrentProgram(int) {}
const juce::String VocodeAudioProcessor::getProgramName(int) { return {}; }
void VocodeAudioProcessor::changeProgramName(int, const juce::String&) {}

bool VocodeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() &&
        mainOut != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != mainOut)
        return false;
    auto sideIn = layouts.getChannelSet(true, 1);
    if (!sideIn.isDisabled() &&
        sideIn != juce::AudioChannelSet::mono() &&
        sideIn != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

// ============================================================================
// Buffer allocation
// ============================================================================
void VocodeAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        const float n = (float)i / (float)(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::twoPi * n));
    }
}

void VocodeAudioProcessor::allocateBuffers()
{
    window.resize(fftSize);
    createWindow();

    mainFifo.assign(fftSize, 0.0f);
    sidechainFifo.assign(fftSize, 0.0f);
    analysisFrame.assign(fftSize * 2, 0.0f);
    morphFrame.assign(fftSize * 2, 0.0f);

    smoothedMain.assign(numBins, 0.0f);
    smoothedSidechain.assign(numBins, 0.0f);
    smoothedMorphed.assign(numBins, 0.0f);

    instantaneousSidechain.assign(numBins, 0.0f);
    morphEnvelopeSidechain.assign(numBins, 0.0f);

    outputOLAL.assign(fftSize, 0.0f);
    outputOLAR.assign(fftSize, 0.0f);
    outputReadPos = 0;

    synthFifoL.assign(fftSize, 0.0f);
    synthFifoR.assign(fftSize, 0.0f);
    synthFrameL.assign(fftSize * 2, 0.0f);
    synthFrameR.assign(fftSize * 2, 0.0f);
    scaleBuf.assign(numBins, 1.0f);

    const int numFrames = fftSize / hopSize;
    olaNorm.assign(hopSize, 0.0f);
    for (int n = 0; n < hopSize; ++n)
    {
        float s = 0.0f;
        for (int k = 0; k < numFrames; ++k)
        {
            const int idx = n + k * hopSize;
            if (idx < fftSize) s += window[idx] * window[idx];
        }
        olaNorm[n] = (s > 1e-8f) ? s : 1.0f;
    }

    mainFifoIndex = 0;
    sidechainFifoIndex = 0;
    outputReadPos = 0;

    scratchSideMags.assign(numBins, 0.0f);
    scratchMainMags.assign(numBins, 0.0f);
    scratchMainEnv.assign(numBins, 0.0f);
    scratchEnvTmp.assign(numBins, 0.0f);
    scratchPrefixSum.assign(numBins + 1, 0.0);

    // Analog mode scratch
    analogCLBuf.assign(kMaxAnalogBands, 0.0f);
    analogCRBuf.assign(kMaxAnalogBands, 0.0f);
    analogEnvBuf.assign(kMaxAnalogBands, 0.0f);
    analogOutFifo.assign(fftSize, 0.0f);
    analogOutFrame.assign(fftSize * 2, 0.0f);
    analogOutFifoIdx = 0;
}

void VocodeAudioProcessor::setFFTSize(int newSize)
{
    int order = 0, sz = newSize;
    while (sz > 1) { sz >>= 1; ++order; }
    if ((1 << order) != newSize || order < 5 || order > 13) return;

    suspendProcessing(true);
    fftOrder = order;
    fftSize = newSize;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;
    fftObj = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
    setLatencySamples(fftSize);
    suspendProcessing(false);
}

void VocodeAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    allocateBuffers();
    setLatencySamples(fftSize);
    rebuildAnalogBands();
}

void VocodeAudioProcessor::releaseResources() {}

// ============================================================================
// Log-scaled spectral envelope  (O(N) prefix-sum, unchanged)
// ============================================================================
static constexpr int kMinHalfW = 3;

static void computeLogEnvelope(const float* mags, float* env, int n,
    float logFraction, double* prefixSum)
{
    prefixSum[0] = 0.0;
    for (int i = 0; i < n; ++i)
        prefixSum[i + 1] = prefixSum[i] + (double)mags[i];

    for (int i = 0; i < n; ++i)
    {
        const int hw = juce::jmax(kMinHalfW, (int)(i * logFraction));
        const int lo = juce::jmax(0, i - hw);
        const int hi = juce::jmin(n - 1, i + hw);
        const int cnt = hi - lo + 1;
        env[i] = (float)((prefixSum[hi + 1] - prefixSum[lo]) / (double)cnt);
    }
}

// ============================================================================
// Analog filterbank: build / rebuild IIR coefficients
// ============================================================================
void VocodeAudioProcessor::rebuildAnalogBands()
{
    // Resolve band count from choice parameter
    const int idx = numBandsParam
        ? juce::roundToInt(numBandsParam->load(std::memory_order_relaxed)) : 8;
    const int nb = kBandCounts[juce::jlimit(0, kNumBandChoices - 1, idx)];
    currentNumBands = nb;

    analogFHigh = juce::jmin(18000.0f, (float)(currentSampleRate * 0.47));

    // 1 ms attack coefficient
    analogAttCoeff = std::exp(-1.0f / (0.001f * (float)currentSampleRate));

    const float fLow = analogFLow;
    const float fHigh = analogFHigh;
    const float fs = (float)currentSampleRate;

    // For a single band, place it at the geometric mean
    if (nb <= 1)
    {
        const float fc = std::sqrt(fLow * fHigh);
        const double w0 = juce::MathConstants<double>::twoPi * (double)fc / (double)fs;
        const double alpha = std::sin(w0) / (2.0 * 1.0);   // Q = 1
        const double a0 = 1.0 + alpha;
        auto& b = analogBands[0];
        b.b0 = (float)(alpha / a0);
        b.b2 = (float)(-alpha / a0);
        b.a1 = (float)(-2.0 * std::cos(w0) / a0);
        b.a2 = (float)((1.0 - alpha) / a0);
        b.fc = fc;
        b.cLs1 = b.cLs2 = b.cRs1 = b.cRs2 = b.ms1 = b.ms2 = 0;
        b.envelope = 0;
        return;
    }

    // Logarithmically-spaced bands with Q tuned for critical spacing:
    //   Q ≈ 1 / (r - 1),  r = (fHigh/fLow)^(1/(N-1))
    const float logRatio = std::log(fHigh / fLow);
    const float r = std::exp(logRatio / (float)(nb - 1));
    const float Q = juce::jmax(1.0f, 1.0f / (r - 1.0f));

    for (int k = 0; k < nb && k < kMaxAnalogBands; ++k)
    {
        const float fc = fLow * std::exp(logRatio * (float)k / (float)(nb - 1));

        // Audio-EQ-cookbook constant-0dB-peak bandpass:
        //   b0 = alpha, b1 = 0, b2 = -alpha
        //   a1 = -2*cos(w0), a2 = 1-alpha   (all divided by a0 = 1+alpha)
        const double w0 = juce::MathConstants<double>::twoPi * (double)fc / (double)fs;
        const double alpha = std::sin(w0) / (2.0 * (double)Q);
        const double a0 = 1.0 + alpha;

        auto& b = analogBands[k];
        b.b0 = (float)(alpha / a0);
        b.b2 = (float)(-alpha / a0);
        b.a1 = (float)(-2.0 * std::cos(w0) / a0);
        b.a2 = (float)((1.0 - alpha) / a0);
        b.fc = fc;
        b.cLs1 = b.cLs2 = b.cRs1 = b.cRs2 = b.ms1 = b.ms2 = 0;
        b.envelope = 0;
    }
}

// ============================================================================
// Analog filterbank processing  (per-sample, full stereo)
// ============================================================================
void VocodeAudioProcessor::processAnalogBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int mainNumCh = getMainBusNumInputChannels();
    auto sideBuf = getBusBuffer(buffer, true, 1);
    const int sideNumCh = sideBuf.getNumChannels();

    // Snapshot all params (safe: atomic loads)
    const float morph = morphParam->load(std::memory_order_relaxed);
    const float smooth = smoothingParam->load(std::memory_order_relaxed);
    const float gateDB = gateParam->load(std::memory_order_relaxed);
    const float gateLinear = std::pow(10.0f, gateDB / 20.0f);
    const float formantST = formantParam->load(std::memory_order_relaxed);
    const float formantFac = std::pow(2.0f, formantST / 12.0f);
    const bool  doFormant = (std::abs(formantST) > 0.05f);

    // Envelope follower release time from smoothing: 5 ms … 500 ms
    const float relMs = 5.0f + smooth * smooth * 495.0f;
    const float relCoeff = std::exp(-1.0f / (relMs * 1e-3f * (float)currentSampleRate));

    const int   nb = currentNumBands;
    const float outScale = (nb > 0) ? 1.0f / std::sqrt((float)nb) : 1.0f;
    const float logRatio = (nb > 1) ? std::log(analogFHigh / analogFLow) : 1.0f;

    float* cLBuf = analogCLBuf.data();
    float* cRBuf = analogCRBuf.data();
    float* envBuf = analogEnvBuf.data();

    const float normF = 2.0f / (float)fftSize;
    const float dispSm = smooth;   // display IIR coefficient

    for (int s = 0; s < numSamples; ++s)
    {
        const float inL = (mainNumCh > 0) ? buffer.getSample(0, s) : 0.0f;
        const float inR = (mainNumCh > 1) ? buffer.getSample(1, s) : inL;
        const float sL = (sideNumCh > 0) ? sideBuf.getSample(0, s) : 0.0f;
        const float sR = (sideNumCh > 1) ? sideBuf.getSample(1, s) : sL;
        const float mod = (sL + sR) * 0.5f;

        // ------------------------------------------------------------------
        // Step 1: filter all bands, compute per-band carrier outputs +
        //         update modulator envelope followers
        // ------------------------------------------------------------------
        for (int k = 0; k < nb; ++k)
        {
            auto& bd = analogBands[k];

            // Carrier L  (DF2T biquad; b1 = 0 so that term is omitted)
            const float yL = bd.b0 * inL + bd.cLs1;
            bd.cLs1 = -bd.a1 * yL + bd.cLs2;
            bd.cLs2 = bd.b2 * inL - bd.a2 * yL;
            cLBuf[k] = yL;

            // Carrier R
            const float yR = bd.b0 * inR + bd.cRs1;
            bd.cRs1 = -bd.a1 * yR + bd.cRs2;
            bd.cRs2 = bd.b2 * inR - bd.a2 * yR;
            cRBuf[k] = yR;

            // Modulator
            const float ym = bd.b0 * mod + bd.ms1;
            bd.ms1 = -bd.a1 * ym + bd.ms2;
            bd.ms2 = bd.b2 * mod - bd.a2 * ym;

            // Peak-tracking envelope follower
            const float absM = std::abs(ym);
            const float c = (absM > bd.envelope) ? analogAttCoeff : relCoeff;
            bd.envelope = bd.envelope * c + absM * (1.0f - c);
            envBuf[k] = bd.envelope;
        }

        // ------------------------------------------------------------------
        // Step 2: sum bands with optional formant-shifted envelope + gate +
        //         morph exponent
        // ------------------------------------------------------------------
        float outL = 0.0f, outR = 0.0f;

        for (int k = 0; k < nb; ++k)
        {
            // Formant shift: map carrier band k to a (possibly fractional)
            // modulator band index using the log-frequency index formula.
            float env;
            if (doFormant && nb > 1)
            {
                // kShift = k + (N-1) * log2(formantFac) / log2(fHigh/fLow)
                const float kShift = (float)k
                    + (float)(nb - 1) * std::log2(formantFac)
                    * std::log(2.0f) / logRatio;

                const int   k0 = juce::jlimit(0, nb - 1, (int)kShift);
                const int   k1 = juce::jmin(nb - 1, k0 + 1);
                const float t = kShift - (float)k0;
                env = envBuf[k0] * (1.0f - t) + envBuf[k1] * t;
            }
            else
            {
                env = envBuf[k];
            }

            // Per-band gate: silence the band if its envelope is below threshold
            if (env < gateLinear)
                env = 0.0f;

            // Morph exponent (matches semantics of FFT mode):
            //   0 → env^0 = 1  (unmodulated carrier, shows filterbank colouring)
            //   1 → env^1      (classic vocoder)
            //   2 → env^2      (exaggerated modulation depth)
            const float sc = (morph < 0.001f) ? 1.0f
                : std::pow(env + 1e-8f, morph);

            outL += cLBuf[k] * sc;
            outR += cRBuf[k] * sc;
        }

        outL *= outScale;
        outR *= outScale;

        buffer.setSample(0, s, outL);
        if (mainNumCh > 1) buffer.setSample(1, s, outR);

        // ------------------------------------------------------------------
        // Display FIFO updates  (all three: main, sidechain, analog output)
        // ------------------------------------------------------------------
        const float mainMono = (inL + inR) * 0.5f;
        const float outMono = (outL + outR) * 0.5f;

        // Helper lambda: push sample into a slide-style FIFO; returns true when
        // a full frame is ready (caller should process + slide).
        auto pushFifo = [&](std::vector<float>& fifo, int& idx, float sample) -> bool
            {
                if (idx < fftSize) fifo[idx] = sample;
                ++idx;
                return (idx >= fftSize);
            };

        auto slideFifo = [&](std::vector<float>& fifo, int& idx)
            {
                std::copy(fifo.begin() + hopSize, fifo.begin() + fftSize, fifo.begin());
                std::fill(fifo.begin() + (fftSize - hopSize), fifo.begin() + fftSize, 0.0f);
                idx -= hopSize;
            };

        // Main input display
        if (pushFifo(mainFifo, mainFifoIndex, mainMono))
        {
            for (int i = 0; i < fftSize; ++i) analysisFrame[i] = mainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftObj->performRealOnlyForwardTransform(analysisFrame.data(), true);
            {
                juce::ScopedLock l(displayLock);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2], im = analysisFrame[bin * 2 + 1];
                    const float mg = std::sqrt(re * re + im * im) * normF;
                    smoothedMain[bin] = smoothedMain[bin] * dispSm + mg * (1.0f - dispSm);
                }
            }
            slideFifo(mainFifo, mainFifoIndex);
        }

        // Sidechain display
        if (pushFifo(sidechainFifo, sidechainFifoIndex, mod))
        {
            for (int i = 0; i < fftSize; ++i) analysisFrame[i] = sidechainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftObj->performRealOnlyForwardTransform(analysisFrame.data(), true);
            {
                juce::ScopedLock l(displayLock);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2], im = analysisFrame[bin * 2 + 1];
                    const float mg = std::sqrt(re * re + im * im) * normF;
                    smoothedSidechain[bin] = smoothedSidechain[bin] * dispSm + mg * (1.0f - dispSm);
                }
            }
            slideFifo(sidechainFifo, sidechainFifoIndex);
        }

        // Analog output display  (feeds smoothedMorphed)
        if (pushFifo(analogOutFifo, analogOutFifoIdx, outMono))
        {
            for (int i = 0; i < fftSize; ++i) analogOutFrame[i] = analogOutFifo[i] * window[i];
            std::fill(analogOutFrame.begin() + fftSize, analogOutFrame.end(), 0.0f);
            fftObj->performRealOnlyForwardTransform(analogOutFrame.data(), true);
            {
                juce::ScopedLock l(displayLock);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analogOutFrame[bin * 2], im = analogOutFrame[bin * 2 + 1];
                    const float mg = std::sqrt(re * re + im * im) * normF;
                    smoothedMorphed[bin] = smoothedMorphed[bin] * dispSm + mg * (1.0f - dispSm);
                }
            }
            slideFifo(analogOutFifo, analogOutFifoIdx);
        }
    }
}

// ============================================================================
// processBlock
// ============================================================================
void VocodeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // ------------------------------------------------------------------
    // Mode-switch detection  (audio thread only — no locks needed)
    // ------------------------------------------------------------------
    const bool isAnalog = (analogModeParam->load(std::memory_order_relaxed) > 0.5f);

    if (isAnalog != prevAnalogMode)
    {
        if (isAnalog)
        {
            // Entering analog mode: reset display FIFOs + filter states
            mainFifoIndex = 0;
            sidechainFifoIndex = 0;
            analogOutFifoIdx = 0;
            std::fill(mainFifo.begin(), mainFifo.end(), 0.0f);
            std::fill(sidechainFifo.begin(), sidechainFifo.end(), 0.0f);
            std::fill(analogOutFifo.begin(), analogOutFifo.end(), 0.0f);
            for (int k = 0; k < kMaxAnalogBands; ++k)
            {
                auto& b = analogBands[k];
                b.cLs1 = b.cLs2 = b.cRs1 = b.cRs2 = b.ms1 = b.ms2 = 0;
                b.envelope = 0;
            }
        }
        else
        {
            // Returning to FFT mode: flush OLA + display FIFOs
            std::fill(outputOLAL.begin(), outputOLAL.end(), 0.0f);
            std::fill(outputOLAR.begin(), outputOLAR.end(), 0.0f);
            std::fill(mainFifo.begin(), mainFifo.end(), 0.0f);
            std::fill(sidechainFifo.begin(), sidechainFifo.end(), 0.0f);
            std::fill(morphEnvelopeSidechain.begin(), morphEnvelopeSidechain.end(), 0.0f);
            outputReadPos = 0;
            mainFifoIndex = 0;
            sidechainFifoIndex = 0;
        }
        prevAnalogMode = isAnalog;
    }

    // Analog path is fully self-contained
    if (isAnalog)
    {
        processAnalogBlock(buffer);
        return;
    }

    // ==================================================================
    // FFT path  (original OLA vocoder, enhanced with gate + formant)
    // ==================================================================
    const int mainNumCh = getMainBusNumInputChannels();
    const int numSamples = buffer.getNumSamples();
    const int olaBufSize = (int)outputOLAL.size();

    auto sidechainBuffer = getBusBuffer(buffer, true, 1);
    const int sideNumCh = sidechainBuffer.getNumChannels();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // A. Capture input samples
        const float mainL = (mainNumCh > 0) ? buffer.getSample(0, sample) : 0.0f;
        const float mainR = (mainNumCh > 1) ? buffer.getSample(1, sample) : mainL;
        const float mainMono = (mainL + mainR) * 0.5f;

        const float sideL = (sideNumCh > 0) ? sidechainBuffer.getSample(0, sample) : 0.0f;
        const float sideR = (sideNumCh > 1) ? sidechainBuffer.getSample(1, sample) : sideL;
        const float sideMono = (sideL + sideR) * 0.5f;

        // B. Read next synthesised sample from OLA
        const int   readIdx = (int)(outputReadPos % (int64_t)olaBufSize);
        const int   normIdx = (int)(outputReadPos % (int64_t)hopSize);
        const float norm = olaNorm[normIdx];

        const float outL = outputOLAL[readIdx] / norm;
        const float outR = (mainNumCh > 1) ? (outputOLAR[readIdx] / norm) : outL;
        outputOLAL[readIdx] = 0.0f;
        outputOLAR[readIdx] = 0.0f;
        ++outputReadPos;

        buffer.setSample(0, sample, outL);
        if (mainNumCh > 1) buffer.setSample(1, sample, outR);

        // C. Push to analysis FIFOs
        if (mainFifoIndex < fftSize)
        {
            mainFifo[mainFifoIndex] = mainMono;
            synthFifoL[mainFifoIndex] = mainL;
            synthFifoR[mainFifoIndex] = mainR;
        }
        ++mainFifoIndex;

        if (sidechainFifoIndex < fftSize)
            sidechainFifo[sidechainFifoIndex] = sideMono;
        ++sidechainFifoIndex;

        // ==============================================================
        // D. Main FFT hop
        // ==============================================================
        if (mainFifoIndex >= fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                analysisFrame[i] = mainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftObj->performRealOnlyForwardTransform(analysisFrame.data(), true);

            const float morph = morphParam->load(std::memory_order_relaxed);
            const float normF = 2.0f / (float)fftSize;
            const float smooth = smoothingParam->load(std::memory_order_relaxed);

            // D2. Main display
            {
                juce::ScopedLock l(displayLock);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2];
                    const float im = analysisFrame[bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) * normF;
                    smoothedMain[bin] = smoothedMain[bin] * smooth + mag * (1.0f - smooth);
                }
            }

            const int  writeStart = (int)(outputReadPos % (int64_t)olaBufSize);
            const bool shortcut = (morph == 0.0f);

            if (shortcut)
            {
                for (int i = 0; i < fftSize; ++i)
                {
                    const float ww = window[i] * window[i];
                    outputOLAL[(writeStart + i) % olaBufSize] += synthFifoL[i] * ww;
                    outputOLAR[(writeStart + i) % olaBufSize] += synthFifoR[i] * ww;
                }
                {
                    juce::ScopedLock l(displayLock);
                    std::fill(smoothedMorphed.begin(), smoothedMorphed.begin() + numBins, 0.0f);
                }
            }
            else
            {
                // Snapshot sidechain envelope
                float* sideMags = scratchSideMags.data();
                {
                    juce::ScopedLock l(displayLock);
                    std::copy(morphEnvelopeSidechain.begin(),
                        morphEnvelopeSidechain.begin() + numBins, sideMags);
                }

                // Main spectral envelope
                float* mainMags = scratchMainMags.data();
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2];
                    const float im = analysisFrame[bin * 2 + 1];
                    mainMags[bin] = std::sqrt(re * re + im * im) * normF;
                }

                float* mainEnv = scratchMainEnv.data();
                computeLogEnvelope(mainMags, mainEnv, numBins,
                    clarityParam->load(std::memory_order_relaxed),
                    scratchPrefixSum.data());

                // ----------------------------------------------------------
                // Gate: measure sidechain mean energy, compute soft gain
                // ----------------------------------------------------------
                float sideEnergy = 0.0f;
                for (int bin = 1; bin < numBins; ++bin) sideEnergy += sideMags[bin];
                sideEnergy /= (float)juce::jmax(numBins - 1, 1);

                const float gateLinear = std::pow(10.0f,
                    gateParam->load(std::memory_order_relaxed) / 20.0f);
                // Soft knee: ramps 0→1 over one "gate width" above threshold
                const float gateGain = juce::jlimit(0.0f, 1.0f,
                    sideEnergy / (gateLinear + 1e-12f));

                // ----------------------------------------------------------
                // Formant shift: remap sidechain envelope bins
                // ----------------------------------------------------------
                const float formantST = formantParam->load(std::memory_order_relaxed);
                const float formantFac = std::pow(2.0f, formantST / 12.0f);
                const bool  doFormant = (std::abs(formantST) > 0.05f);

                // Build morph frame
                std::copy(analysisFrame.begin(), analysisFrame.end(), morphFrame.begin());

                static constexpr float kMaxScale = 50.0f;
                const float maxRatio = std::pow(kMaxScale, 1.0f / juce::jmax(morph, 0.01f));

                float* morphedMags = scratchMainMags.data();   // reuse (done with mainMags)

                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = morphFrame[bin * 2];
                    const float im = morphFrame[bin * 2 + 1];
                    const float mainMag = std::sqrt(re * re + im * im) * normF;

                    // Formant-shifted sidechain bin lookup (linear interpolation)
                    float shiftedSide;
                    if (doFormant)
                    {
                        const float fBin = (float)bin * formantFac;
                        const int   b0 = juce::jlimit(0, numBins - 1, (int)fBin);
                        const int   b1 = juce::jmin(numBins - 1, b0 + 1);
                        const float t = fBin - (float)b0;
                        shiftedSide = sideMags[b0] * (1.0f - t) + sideMags[b1] * t;
                    }
                    else
                    {
                        shiftedSide = sideMags[bin];
                    }

                    // Apply gate gain to sidechain
                    shiftedSide *= gateGain;

                    float sc = 1.0f;
                    if (mainEnv[bin] > 1e-10f)
                    {
                        const float envRatio = (shiftedSide > 1e-10f)
                            ? juce::jmin(shiftedSide / mainEnv[bin], maxRatio)
                            : 0.0f;
                        sc = std::pow(envRatio, morph);
                    }
                    morphFrame[bin * 2] = re * sc;
                    morphFrame[bin * 2 + 1] = im * sc;
                    morphedMags[bin] = mainMag * sc;
                }

                // Display update
                {
                    juce::ScopedLock l(displayLock);
                    for (int bin = 0; bin < numBins; ++bin)
                        smoothedMorphed[bin] = smoothedMorphed[bin] * smooth
                        + morphedMags[bin] * (1.0f - smooth);
                }

                // Per-bin scale for synthesis
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float reA = analysisFrame[bin * 2], imA = analysisFrame[bin * 2 + 1];
                    const float magA = std::sqrt(reA * reA + imA * imA);
                    const float reM = morphFrame[bin * 2], imM = morphFrame[bin * 2 + 1];
                    const float magM = std::sqrt(reM * reM + imM * imM);
                    scaleBuf[bin] = (magA > 1e-12f) ? magM / magA : 0.0f;
                }

                // Apply scale to L
                for (int i = 0; i < fftSize; ++i)
                    synthFrameL[i] = synthFifoL[i] * window[i];
                std::fill(synthFrameL.begin() + fftSize, synthFrameL.end(), 0.0f);
                fftObj->performRealOnlyForwardTransform(synthFrameL.data(), true);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    synthFrameL[bin * 2] *= scaleBuf[bin];
                    synthFrameL[bin * 2 + 1] *= scaleBuf[bin];
                }
                fftObj->performRealOnlyInverseTransform(synthFrameL.data());
                for (int i = 0; i < fftSize; ++i)
                    outputOLAL[(writeStart + i) % olaBufSize] += synthFrameL[i] * window[i];

                // Apply scale to R
                for (int i = 0; i < fftSize; ++i)
                    synthFrameR[i] = synthFifoR[i] * window[i];
                std::fill(synthFrameR.begin() + fftSize, synthFrameR.end(), 0.0f);
                fftObj->performRealOnlyForwardTransform(synthFrameR.data(), true);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    synthFrameR[bin * 2] *= scaleBuf[bin];
                    synthFrameR[bin * 2 + 1] *= scaleBuf[bin];
                }
                fftObj->performRealOnlyInverseTransform(synthFrameR.data());
                for (int i = 0; i < fftSize; ++i)
                    outputOLAR[(writeStart + i) % olaBufSize] += synthFrameR[i] * window[i];
            }

            // Slide main FIFOs
            std::copy(mainFifo.begin() + hopSize, mainFifo.begin() + fftSize, mainFifo.begin());
            std::fill(mainFifo.begin() + (fftSize - hopSize), mainFifo.begin() + fftSize, 0.0f);
            std::copy(synthFifoL.begin() + hopSize, synthFifoL.begin() + fftSize, synthFifoL.begin());
            std::fill(synthFifoL.begin() + (fftSize - hopSize), synthFifoL.begin() + fftSize, 0.0f);
            std::copy(synthFifoR.begin() + hopSize, synthFifoR.begin() + fftSize, synthFifoR.begin());
            std::fill(synthFifoR.begin() + (fftSize - hopSize), synthFifoR.begin() + fftSize, 0.0f);
            mainFifoIndex -= hopSize;
        }

        // ==============================================================
        // E. Sidechain FFT hop  (analysis + display only)
        // ==============================================================
        if (sidechainFifoIndex >= fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                analysisFrame[i] = sidechainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftObj->performRealOnlyForwardTransform(analysisFrame.data(), true);

            const float normF = 2.0f / (float)fftSize;
            const float smooth = smoothingParam->load(std::memory_order_relaxed);

            {
                juce::ScopedLock l(displayLock);

                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2];
                    const float im = analysisFrame[bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) * normF;
                    smoothedSidechain[bin] = smoothedSidechain[bin] * smooth
                        + mag * (1.0f - smooth);
                    instantaneousSidechain[bin] = mag;
                }

                float* envTmp = scratchEnvTmp.data();
                computeLogEnvelope(instantaneousSidechain.data(), envTmp, numBins,
                    clarityParam->load(std::memory_order_relaxed),
                    scratchPrefixSum.data());

                for (int bin = 0; bin < numBins; ++bin)
                    morphEnvelopeSidechain[bin] = morphEnvelopeSidechain[bin] * smooth
                    + envTmp[bin] * (1.0f - smooth);
            }

            std::copy(sidechainFifo.begin() + hopSize,
                sidechainFifo.begin() + fftSize, sidechainFifo.begin());
            std::fill(sidechainFifo.begin() + (fftSize - hopSize),
                sidechainFifo.begin() + fftSize, 0.0f);
            sidechainFifoIndex -= hopSize;
        }
    }
}

// ============================================================================
// Display accessors  (message thread)
// ============================================================================
void VocodeAudioProcessor::getMainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMain.begin(), smoothedMain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void VocodeAudioProcessor::getSidechainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedSidechain.begin(), smoothedSidechain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void VocodeAudioProcessor::getMorphedFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMorphed.begin(), smoothedMorphed.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

// ============================================================================
// State persistence
// ============================================================================
void VocodeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void VocodeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml) return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (state.isValid()) apvts.replaceState(state);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("fftSize")))
    {
        const int sizes[] = { 32,64,128,256,512,1024,2048,4096,8192 };
        const int idx = p->getIndex();
        if (idx >= 0 && idx < 9) setFFTSize(sizes[idx]);
    }
}

bool VocodeAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* VocodeAudioProcessor::createEditor()
{
    return new VocodeAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocodeAudioProcessor();
}