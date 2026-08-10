#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================
// Parameter layout
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SpectralCompareAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Morph amount  [0 … 2]
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "morph", "Morph",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes()
        .withLabel("x")
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 2); })));

    // Clarity / envelope width  [0.05 … 0.5]
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "clarity", "Clarity",
        juce::NormalisableRange<float>(0.05f, 0.5f, 0.005f), 0.25f,
        juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float v, int) { return juce::String(v, 3); })));

    // FFT Size
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fftSize", "FFT Size",
        juce::StringArray{ "1024", "2048", "4096", "8192" }, 1));

    // Visual-speed sliders  [0 … 0.99]
    auto makeSpeed = [](const juce::String& id, const juce::String& name, float def)
        {
            return std::make_unique<juce::AudioParameterFloat>(
                id, name, juce::NormalisableRange<float>(0.0f, 0.99f, 0.01f), def);
        };
    params.push_back(makeSpeed("smoothMain",     "Main Speed",   0.7f));
    params.push_back(makeSpeed("smoothSidechain","Side Speed",   0.7f));
    params.push_back(makeSpeed("smoothMorph",    "Morph Speed",  0.7f));
    params.push_back(makeSpeed("smoothDelta",    "Delta Speed",  0.7f));
    params.push_back(makeSpeed("smoothOutput",   "Output Speed", 0.7f));

    // Audio-smooth toggles
    params.push_back(std::make_unique<juce::AudioParameterBool>("smoothMainAudio", "Smooth Main Audio", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("smoothSideAudio", "Smooth Side Audio", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("smoothMorphAudio", "Smooth Morph Audio", true));  // default ON = preserves old behaviour
    params.push_back(std::make_unique<juce::AudioParameterBool>("smoothDeltaAudio", "Smooth Delta Audio", false));

    // Hear-delta-only
    params.push_back(std::make_unique<juce::AudioParameterBool>("hearDelta", "Hear Delta Only", false));

    // Monitor sidechain through output (latency-compensated passthrough)
    params.push_back(std::make_unique<juce::AudioParameterBool>("monitorSide", "Monitor Sidechain", false));

    // Frequency view range knobs
    {
        juce::NormalisableRange<float> r(20.0f, 19800.0f, 1.0f);
        r.setSkewForCentre(1000.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "freqFrom", "Freq From", r, 20.0f,
            juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }
    {
        juce::NormalisableRange<float> r(220.0f, 22000.0f, 1.0f);
        r.setSkewForCentre(4000.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "freqTo", "Freq To", r, 20000.0f,
            juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }

    // Gate parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("gateEnable", "Gate Enable", false));
    {
        juce::NormalisableRange<float> r(20.0f, 22000.0f, 1.0f);
        r.setSkewForCentre(1000.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "gateBinStart", "Gate Bin Start", r, 20.0f,
            juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return juce::String(juce::roundToInt(v)) + " Hz"; })));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "gateBinEnd", "Gate Bin End", r, 20000.0f,
            juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold",
        juce::NormalisableRange<float>(-60.0f, 3.0f, 0.1f), -60.0f,
        juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction([](float v, int) {
            return juce::String(v, 1) + " dB"; })));

    // Enhance parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("enhanceEnable", "Enhance Enable", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("enhanceAttenuate", "Enhance Attenuate", false));
    {
        juce::NormalisableRange<float> r(20.0f, 22000.0f, 1.0f);
        r.setSkewForCentre(1000.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "enhanceBinStart", "Enhance Bin Start", r, 20.0f,
            juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return juce::String(juce::roundToInt(v)) + " Hz"; })));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "enhanceBinEnd", "Enhance Bin End", r, 20000.0f,
            juce::AudioParameterFloatAttributes()
            .withLabel("Hz")
            .withStringFromValueFunction([](float v, int) {
                return juce::String(juce::roundToInt(v)) + " Hz"; })));
    }
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "enhanceLevel", "Enhance Level",
        juce::NormalisableRange<float>(-60.0f, 3.0f, 0.1f), -30.0f,
        juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction([](float v, int) {
            return juce::String(v, 1) + " dB"; })));

    // ---- Display state (visual only, but persisted + exposed to DAW) ----
    params.push_back(std::make_unique<juce::AudioParameterBool>("interpolate",    "Interpolate",    true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("showMain",       "Show Main",      true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("showSidechain",  "Show Sidechain", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("showDelta",      "Show Delta",     false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("showMorph",      "Show Morph",     true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("showOutput",     "Show Output",    true));

    return { params.begin(), params.end() };
}

//==============================================================================
SpectralCompareAudioProcessor::SpectralCompareAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Main", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "SpectralCompare", createParameterLayout())
{
    morphParam = apvts.getRawParameterValue("morph");
    envelopeWidthParam = apvts.getRawParameterValue("clarity");
    smoothMainParam      = apvts.getRawParameterValue("smoothMain");
    smoothSidechainParam = apvts.getRawParameterValue("smoothSidechain");
    smoothMorphParam     = apvts.getRawParameterValue("smoothMorph");
    smoothDeltaParam     = apvts.getRawParameterValue("smoothDelta");
    smoothOutputParam    = apvts.getRawParameterValue("smoothOutput");

    smoothMainAudioParam = apvts.getRawParameterValue("smoothMainAudio");
    smoothSideAudioParam = apvts.getRawParameterValue("smoothSideAudio");
    smoothMorphAudioParam = apvts.getRawParameterValue("smoothMorphAudio");
    smoothDeltaAudioParam = apvts.getRawParameterValue("smoothDeltaAudio");

    hearDeltaParam = apvts.getRawParameterValue("hearDelta");
    monitorSideParam = apvts.getRawParameterValue("monitorSide");

    gateEnableParam = apvts.getRawParameterValue("gateEnable");
    gateBinStartParam = apvts.getRawParameterValue("gateBinStart");
    gateBinEndParam = apvts.getRawParameterValue("gateBinEnd");
    gateThresholdParam = apvts.getRawParameterValue("gateThreshold");

    enhanceEnableParam = apvts.getRawParameterValue("enhanceEnable");
    enhanceAttenuateParam = apvts.getRawParameterValue("enhanceAttenuate");
    enhanceBinStartParam = apvts.getRawParameterValue("enhanceBinStart");
    enhanceBinEndParam = apvts.getRawParameterValue("enhanceBinEnd");
    enhanceLevelParam = apvts.getRawParameterValue("enhanceLevel");

    apvts.addParameterListener("fftSize", this);

    mainFilterGain.fill(1.0f);
    sidechainFilterGain.fill(1.0f);

    fftMain = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSidechain = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

SpectralCompareAudioProcessor::~SpectralCompareAudioProcessor()
{
    apvts.removeParameterListener("fftSize", this);
    cancelPendingUpdate();
}

void SpectralCompareAudioProcessor::parameterChanged(const juce::String& paramID, float newValue)
{
    if (paramID == "fftSize")
    {
        const int idx = juce::roundToInt(newValue);
        const int sizes[] = { 1024, 2048, 4096, 8192 };
        if (idx >= 0 && idx < 4)
        {
            pendingFFTSize.store(sizes[idx], std::memory_order_relaxed);
            triggerAsyncUpdate();
        }
    }
}

void SpectralCompareAudioProcessor::handleAsyncUpdate()
{
    const int sz = pendingFFTSize.exchange(0, std::memory_order_relaxed);
    if (sz > 0) setFFTSize(sz);
}

//==============================================================================
const juce::String SpectralCompareAudioProcessor::getName() const { return JucePlugin_Name; }
bool SpectralCompareAudioProcessor::acceptsMidi()  const { return false; }
bool SpectralCompareAudioProcessor::producesMidi() const { return false; }
bool SpectralCompareAudioProcessor::isMidiEffect() const { return false; }
double SpectralCompareAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  SpectralCompareAudioProcessor::getNumPrograms() { return 1; }
int  SpectralCompareAudioProcessor::getCurrentProgram() { return 0; }
void SpectralCompareAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralCompareAudioProcessor::getProgramName(int) { return {}; }
void SpectralCompareAudioProcessor::changeProgramName(int, const juce::String&) {}

bool SpectralCompareAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

//==============================================================================
void SpectralCompareAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void SpectralCompareAudioProcessor::allocateBuffers()
{
    window.resize(fftSize);
    createWindow();

    mainFifo.assign(fftSize, 0.0f);
    sidechainFifo.assign(fftSize, 0.0f);
    analysisFrame.assign(fftSize * 2, 0.0f);
    preFftFrame.assign(fftSize, 0.0f);
    morphFrame.assign(fftSize * 2, 0.0f);

    smoothedMain.assign(numBins, 0.0f);
    smoothedSidechain.assign(numBins, 0.0f);
    smoothedMorphed.assign(numBins, 0.0f);
    smoothedOutput.assign(numBins, 0.0f);
    instantaneousSidechain.assign(numBins, 0.0f);
    morphEnvelopeSidechain.assign(numBins, 0.0f);

    // Audio-smoothing helpers
    smoothedMainEnv.assign(numBins, 0.0f);
    smoothedSideForAudio.assign(numBins, 0.0f);
    smoothedDeltaSpec.assign(numBins, 0.0f);

    // Output-spectrum capture
    outputFifo.assign(fftSize, 0.0f);
    outputAnalysisFrame.assign(fftSize * 2, 0.0f);
    outputFifoIndex = 0;

    // Stereo OLA accumulators
    outputOLAL.assign(fftSize, 0.0f);
    outputOLAR.assign(fftSize, 0.0f);
    outputReadPos = 0;

    // Per-channel synthesis FIFOs + scratch frames
    synthFifoL.assign(fftSize, 0.0f);
    synthFifoR.assign(fftSize, 0.0f);
    synthFrameL.assign(fftSize * 2, 0.0f);
    synthFrameR.assign(fftSize * 2, 0.0f);
    scaleBuf.assign(numBins, 1.0f);

    // Sidechain latency-compensation delay
    sideDelayL.assign(fftSize, 0.0f);
    sideDelayR.assign(fftSize, 0.0f);
    sideDelayPos = 0;

    const int numFrames = fftSize / hopSize;   // = 4
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

    // Pre-allocate scratch buffers used in processBlock to avoid large stack VLAs
    scratchSideMags .assign(numBins, 0.0f);
    scratchMainMags .assign(numBins, 0.0f);
    scratchMainEnv  .assign(numBins, 0.0f);
    scratchEnvInput .assign(numBins, 0.0f);
    scratchEnvTmp   .assign(numBins, 0.0f);
    scratchPrefixSum.assign(numBins + 1, 0.0);
}

//==============================================================================
void SpectralCompareAudioProcessor::setFFTSize(int newSize)
{
    if (newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;

    const int newOrder = (newSize == 1024) ? 10
        : (newSize == 2048) ? 11
        : (newSize == 4096) ? 12 : 13;

    suspendProcessing(true);
    fftOrder = newOrder;
    fftSize = newSize;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;
    fftMain = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSidechain = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
    setLatencySamples(fftSize);
    suspendProcessing(false);
}

void SpectralCompareAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    allocateBuffers();
    setLatencySamples(fftSize);
}

void SpectralCompareAudioProcessor::releaseResources() {}

// Log-scaled spectral envelope (O(N) prefix-sum)
static constexpr int kMinHalfW = 3;

static void computeLogEnvelope(const float* mags, float* env, int n, float logFraction,
                               double* prefixSum)   // caller-supplied buffer of size n+1
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
// Spectral Gate (in-place on packed re/im FFT buffer)
// ============================================================================
void SpectralCompareAudioProcessor::applySpectralGateToFrame(float* fftBuf, int nb)
{
    const float threshDB = gateThresholdParam->load(std::memory_order_relaxed);
    const float loHz = gateBinStartParam->load(std::memory_order_relaxed);
    const float hiHz = gateBinEndParam->load(std::memory_order_relaxed);

    const float nyquist = (float)(currentSampleRate * 0.5);
    const float freqPerBin = nyquist / (float)(nb - 1);
    const int   loBin = juce::jlimit(0, nb - 1, (int)(loHz / freqPerBin));
    const int   hiBin = juce::jlimit(0, nb - 1, (int)(hiHz / freqPerBin));

    float maxMag = 1e-10f;
    for (int bin = 0; bin < nb; ++bin)
    {
        const float re = fftBuf[bin * 2], im = fftBuf[bin * 2 + 1];
        maxMag = std::max(maxMag, std::sqrt(re * re + im * im));
    }

    const float threshMag = maxMag * std::pow(10.0f, threshDB / 20.0f);

    for (int bin = 0; bin < nb; ++bin)
    {
        const float re = fftBuf[bin * 2], im = fftBuf[bin * 2 + 1];
        const float mag = std::sqrt(re * re + im * im);

        // Keep only bins that are within [loBin,hiBin] AND above threshold
        const bool keep = (bin >= loBin && bin <= hiBin) && (mag >= threshMag);
        if (!keep)
        {
            fftBuf[bin * 2] = 0.0f;
            fftBuf[bin * 2 + 1] = 0.0f;
        }
    }
}

// ============================================================================
// Spectral Enhance (in-place on packed re/im FFT buffer)
// ============================================================================
void SpectralCompareAudioProcessor::applySpectralEnhanceToFrame(float* fftBuf, int nb)
{
    const float levelDB = enhanceLevelParam->load(std::memory_order_relaxed);
    const float loHz = enhanceBinStartParam->load(std::memory_order_relaxed);
    const float hiHz = enhanceBinEndParam->load(std::memory_order_relaxed);
    const bool  attenuate = enhanceAttenuateParam->load(std::memory_order_relaxed) >= 0.5f;

    const float nyquist = (float)(currentSampleRate * 0.5);
    const float freqPerBin = nyquist / (float)(nb - 1);
    const int   loBin = juce::jlimit(0, nb - 1, (int)(loHz / freqPerBin));
    const int   hiBin = juce::jlimit(0, nb - 1, (int)(hiHz / freqPerBin));

    float maxMag = 1e-10f;
    for (int bin = 0; bin < nb; ++bin)
    {
        const float re = fftBuf[bin * 2], im = fftBuf[bin * 2 + 1];
        maxMag = std::max(maxMag, std::sqrt(re * re + im * im));
    }

    const float threshMag = maxMag * std::pow(10.0f, levelDB / 20.0f);

    for (int bin = loBin; bin <= hiBin; ++bin)
    {
        const float re = fftBuf[bin * 2], im = fftBuf[bin * 2 + 1];
        const float mag = std::sqrt(re * re + im * im);

        // If the bin was already zeroed (e.g. by a spectral gate), leave it silent.
        // Never inject energy into a zeroed bin.
        if (mag < 1e-12f)
            continue;

        if (!attenuate && mag < threshMag)
        {
            const float s = threshMag / mag;
            fftBuf[bin * 2] *= s; fftBuf[bin * 2 + 1] *= s;
        }
        else if (attenuate && mag > threshMag)
        {
            const float s = threshMag / mag;
            fftBuf[bin * 2] *= s; fftBuf[bin * 2 + 1] *= s;
        }
    }
}

// ============================================================================
// Spectral filter curves — API (message thread)
// ============================================================================

void SpectralCompareAudioProcessor::setMainFilterCurveRange(
    int startBin, int endBin, float startDB, float endDB)
{
    if (startBin > endBin) { std::swap(startBin, endBin); std::swap(startDB, endDB); }
    startBin = juce::jlimit(0, numBins - 1, startBin);
    endBin   = juce::jlimit(0, numBins - 1, endBin);
    {
        juce::ScopedLock lock(filterCurveLock);
        const int span = endBin - startBin;
        if (span == 0)
        {
            mainFilterCurve_write[startBin] = startDB;
        }
        else
        {
            for (int b = startBin; b <= endBin; ++b)
            {
                float t = (float)(b - startBin) / (float)span;
                mainFilterCurve_write[b] = startDB + t * (endDB - startDB);
            }
        }
    }
    mainFilterDirty.store(true, std::memory_order_release);
}

void SpectralCompareAudioProcessor::setSidechainFilterCurveRange(
    int startBin, int endBin, float startDB, float endDB)
{
    if (startBin > endBin) { std::swap(startBin, endBin); std::swap(startDB, endDB); }
    startBin = juce::jlimit(0, numBins - 1, startBin);
    endBin   = juce::jlimit(0, numBins - 1, endBin);
    {
        juce::ScopedLock lock(filterCurveLock);
        const int span = endBin - startBin;
        if (span == 0)
        {
            sidechainFilterCurve_write[startBin] = startDB;
        }
        else
        {
            for (int b = startBin; b <= endBin; ++b)
            {
                float t = (float)(b - startBin) / (float)span;
                sidechainFilterCurve_write[b] = startDB + t * (endDB - startDB);
            }
        }
    }
    sidechainFilterDirty.store(true, std::memory_order_release);
}

void SpectralCompareAudioProcessor::resetMainFilterCurve()
{
    { juce::ScopedLock lock(filterCurveLock); mainFilterCurve_write.fill(0.0f); }
    mainFilterDirty.store(true, std::memory_order_release);
}

void SpectralCompareAudioProcessor::resetSidechainFilterCurve()
{
    { juce::ScopedLock lock(filterCurveLock); sidechainFilterCurve_write.fill(0.0f); }
    sidechainFilterDirty.store(true, std::memory_order_release);
}

void SpectralCompareAudioProcessor::getMainFilterCurveData(float* out, int numBinsOut)
{
    juce::ScopedLock lock(filterCurveLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(mainFilterCurve_write.begin(), mainFilterCurve_write.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getSidechainFilterCurveData(float* out, int numBinsOut)
{
    juce::ScopedLock lock(filterCurveLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(sidechainFilterCurve_write.begin(), sidechainFilterCurve_write.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

// Audio-thread helpers
void SpectralCompareAudioProcessor::rebuildFilterGain(
    std::array<float, maxBins>& gainCache,
    const std::array<float, maxBins>& curve, int nb)
{
    for (int i = 0; i < nb; ++i)
        gainCache[i] = std::pow(10.0f, curve[i] / 20.0f);
}

void SpectralCompareAudioProcessor::applyFilterGains(
    float* fftBuf, int nb, const std::array<float, maxBins>& gain)
{
    for (int i = 0; i < nb; ++i)
    {
        fftBuf[i * 2]     *= gain[i];
        fftBuf[i * 2 + 1] *= gain[i];
    }
}

// ============================================================================
// processBlock
// ============================================================================
void SpectralCompareAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int mainNumCh  = getMainBusNumInputChannels();
    const int numSamples = buffer.getNumSamples();
    const int olaBufSize = (int)outputOLAL.size();   // == fftSize

    auto sidechainBuffer = getBusBuffer(buffer, true, 1);
    const int sideNumCh  = sidechainBuffer.getNumChannels();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // ------------------------------------------------------------------
        // A. Capture per-channel input
        // ------------------------------------------------------------------
        const float mainL = (mainNumCh > 0) ? buffer.getSample(0, sample) : 0.0f;
        const float mainR = (mainNumCh > 1) ? buffer.getSample(1, sample) : mainL;
        const float mainMono = (mainL + mainR) * 0.5f;   // analysis only

        const float sideL = (sideNumCh > 0) ? sidechainBuffer.getSample(0, sample) : 0.0f;
        const float sideR = (sideNumCh > 1) ? sidechainBuffer.getSample(1, sample) : sideL;
        const float sideMono = (sideL + sideR) * 0.5f;

        // Sidechain monitor: latency-compensated delay (fftSize samples)
        const bool monitorSide_ = monitorSideParam->load(std::memory_order_relaxed) >= 0.5f;
        const float delayedSideL = sideDelayL[sideDelayPos];
        const float delayedSideR = sideDelayR[sideDelayPos];
        sideDelayL[sideDelayPos] = sideL;
        sideDelayR[sideDelayPos] = sideR;
        sideDelayPos = (sideDelayPos + 1) % fftSize;

        // ------------------------------------------------------------------
        // B. Read next synthesised sample from stereo OLA
        // ------------------------------------------------------------------
        const int   readIdx = outputReadPos % olaBufSize;
        const int   normIdx = outputReadPos % hopSize;
        const float norm    = olaNorm[normIdx];

        float outL = outputOLAL[readIdx] / norm;
        float outR = (mainNumCh > 1) ? (outputOLAR[readIdx] / norm) : outL;
        outputOLAL[readIdx] = 0.0f;
        outputOLAR[readIdx] = 0.0f;
        ++outputReadPos;

        // Mix in latency-compensated sidechain if monitor is ON
        if (monitorSide_) { outL += delayedSideL;  outR += delayedSideR; }

        // ------------------------------------------------------------------
        // B2. Capture output into display FIFO (mono mix)
        // ------------------------------------------------------------------
        outputFifo[outputFifoIndex] = (outL + outR) * 0.5f;
        ++outputFifoIndex;
        if (outputFifoIndex >= fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                outputAnalysisFrame[i] = outputFifo[i] * window[i];
            std::fill(outputAnalysisFrame.begin() + fftSize, outputAnalysisFrame.end(), 0.0f);
            fftMain->performRealOnlyForwardTransform(outputAnalysisFrame.data(), true);
            {
                juce::ScopedLock l(displayLock);
                const float nF         = 2.0f / (float)fftSize;
                const float smoothOutV = smoothOutputParam->load(std::memory_order_relaxed);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re     = outputAnalysisFrame[bin * 2];
                    const float im     = outputAnalysisFrame[bin * 2 + 1];
                    const float outMag = std::sqrt(re * re + im * im) * nF;
                    smoothedOutput[bin] = smoothedOutput[bin] * smoothOutV
                                       + outMag * (1.0f - smoothOutV);
                }
            }
            std::copy(outputFifo.begin() + hopSize, outputFifo.end(), outputFifo.begin());
            outputFifoIndex -= hopSize;
        }

        // Write output to all channels
        buffer.setSample(0, sample, outL);
        if (mainNumCh > 1) buffer.setSample(1, sample, outR);

        // ------------------------------------------------------------------
        // C. Push samples into analysis FIFOs
        // ------------------------------------------------------------------
        if (mainFifoIndex < fftSize)
        {
            mainFifo[mainFifoIndex]    = mainMono;  // mono analysis
            synthFifoL[mainFifoIndex]  = mainL;     // per-channel synthesis
            synthFifoR[mainFifoIndex]  = mainR;
        }
        ++mainFifoIndex;

        if (sidechainFifoIndex < fftSize)
            sidechainFifo[sidechainFifoIndex] = sideMono;
        ++sidechainFifoIndex;

        // ------------------------------------------------------------------
        // D. Main FFT hop
        // ------------------------------------------------------------------
        if (mainFifoIndex >= fftSize)
        {
            // D1. Window mono analysis frame
            for (int i = 0; i < fftSize; ++i)
            {
                preFftFrame[i]   = mainFifo[i] * window[i];
                analysisFrame[i] = preFftFrame[i];
            }
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);

            // D2. Forward FFT (mono analysis)
            fftMain->performRealOnlyForwardTransform(analysisFrame.data(), true);

            // D2a. Apply main spectral filter curve (if any bins non-zero)
            if (mainFilterDirty.load(std::memory_order_acquire))
            {
                { juce::ScopedLock lock(filterCurveLock); mainFilterCurve_read = mainFilterCurve_write; }
                mainFilterDirty.store(false, std::memory_order_release);
                rebuildFilterGain(mainFilterGain, mainFilterCurve_read, numBins);
            }
            applyFilterGains(analysisFrame.data(), numBins, mainFilterGain);

            // D2b. Main audio smooth: IIR-smooth bin magnitudes in-place so the effect
            //      is audible in ALL downstream paths (shortcut, morph, FX, delta).
            //      smoothedMainEnv is reused as the per-bin IIR state buffer.
            {
                const bool  smoothMA = smoothMainAudioParam->load(std::memory_order_relaxed) >= 0.5f;
                const float smoothMC = smoothMA ? smoothMainParam->load(std::memory_order_relaxed) : 0.0f;
                if (smoothMA)
                {
                    for (int bin = 0; bin < numBins; ++bin)
                    {
                        const float re  = analysisFrame[bin * 2];
                        const float im  = analysisFrame[bin * 2 + 1];
                        const float mag = std::sqrt(re * re + im * im);
                        smoothedMainEnv[bin] = smoothedMainEnv[bin] * smoothMC
                                             + mag * (1.0f - smoothMC);
                        if (mag > 1e-12f)
                        {
                            const float scale = smoothedMainEnv[bin] / mag;
                            analysisFrame[bin * 2]     *= scale;
                            analysisFrame[bin * 2 + 1] *= scale;
                        }
                        else
                        {
                            smoothedMainEnv[bin] *= smoothMC;
                        }
                    }
                }
            }

            // D3. Update main display + read morph/delta flags
            const float morph      = morphParam->load(std::memory_order_relaxed);
            const float normF      = 2.0f / (float)fftSize;
            const float smoothVis  = smoothMainParam->load(std::memory_order_relaxed);
            const bool  hearDelta_ = hearDeltaParam->load(std::memory_order_relaxed) >= 0.5f;
            const bool  gateOn     = gateEnableParam->load(std::memory_order_relaxed) >= 0.5f;
            const bool  enhanceOn  = enhanceEnableParam->load(std::memory_order_relaxed) >= 0.5f;
            const bool  needFx     = gateOn || enhanceOn;

            float* sideMags = scratchSideMags.data();

            {
                juce::ScopedLock l(displayLock);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re  = analysisFrame[bin * 2];
                    const float im  = analysisFrame[bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) * normF;
                    smoothedMain[bin] = smoothedMain[bin] * smoothVis + mag * (1.0f - smoothVis);
                }
                if (morph > 0.0f || hearDelta_)
                    std::copy(morphEnvelopeSidechain.begin(),
                        morphEnvelopeSidechain.begin() + numBins, sideMags);
            }

            // D4. Determine OLA write position and whether we can shortcut
            const int  writeStart = outputReadPos;
            const bool shortcut   = (!hearDelta_ && morph == 0.0f && !needFx);

            if (shortcut)
            {
                // --- Shortcut: scale = 1.0 — add windowed frames directly to stereo OLA ---
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
                // --- Full path: compute per-bin scale from mono morphFrame, apply to stereo ---

                // Build morphFrame from mono analysisFrame
                if (hearDelta_)
                {
                    std::copy(analysisFrame.begin(), analysisFrame.end(), morphFrame.begin());

                    const bool  smoothDA   = smoothDeltaAudioParam->load(std::memory_order_relaxed) >= 0.5f;
                    const float smoothDC   = smoothDA ? smoothDeltaParam->load(std::memory_order_relaxed) : 0.0f;
                    const float smoothMVis = smoothMorphParam->load(std::memory_order_relaxed);

                    {
                        juce::ScopedLock l(displayLock);
                        for (int bin = 0; bin < numBins; ++bin)
                        {
                            const float re      = morphFrame[bin * 2];
                            const float im      = morphFrame[bin * 2 + 1];
                            const float mainMag = std::sqrt(re * re + im * im) * normF;
                            const float sideMag = instantaneousSidechain[bin];

                            const float target  = std::max(0.0f, mainMag - sideMag);
                            smoothedDeltaSpec[bin] = smoothedDeltaSpec[bin] * smoothDC
                                                   + target * (1.0f - smoothDC);
                            const float deltaMag = smoothedDeltaSpec[bin];
                            const float sc       = (mainMag > 1e-12f) ? (deltaMag / mainMag) : 0.0f;

                            morphFrame[bin * 2]     = re * sc;
                            morphFrame[bin * 2 + 1] = im * sc;

                            smoothedMorphed[bin] = smoothedMorphed[bin] * smoothMVis
                                                 + deltaMag * (1.0f - smoothMVis);
                        }
                    }
                }
                else if (morph == 0.0f)
                {
                    // No morph, FX active — copy analysis directly
                    std::copy(analysisFrame.begin(), analysisFrame.end(), morphFrame.begin());
                    {
                        juce::ScopedLock l(displayLock);
                        std::fill(smoothedMorphed.begin(), smoothedMorphed.begin() + numBins, 0.0f);
                    }
                }
                else
                {
                    // ---- Envelope-transfer morph ----
                    float* mainMags = scratchMainMags.data();
                    for (int bin = 0; bin < numBins; ++bin)
                    {
                        const float re  = analysisFrame[bin * 2];
                        const float im  = analysisFrame[bin * 2 + 1];
                        mainMags[bin]   = std::sqrt(re * re + im * im) * normF;
                    }

                    float* mainEnv = scratchMainEnv.data();
                    const float envWidth = envelopeWidthParam->load(std::memory_order_relaxed);
                    computeLogEnvelope(mainMags, mainEnv, numBins, envWidth, scratchPrefixSum.data());
                    // Note: main audio smoothing already applied to analysisFrame bins in D2b.

                    std::copy(analysisFrame.begin(), analysisFrame.end(), morphFrame.begin());

                    const float smoothMVis = smoothMorphParam->load(std::memory_order_relaxed);

                    {
                        juce::ScopedLock l(displayLock);
                        for (int bin = 0; bin < numBins; ++bin)
                        {
                            const float re  = morphFrame[bin * 2];
                            const float im  = morphFrame[bin * 2 + 1];
                            const float mainMag = std::sqrt(re * re + im * im) * normF;

                            float sc = 1.0f;
                            if (mainEnv[bin] > 1e-10f)
                            {
                                static constexpr float kMaxScale = 50.0f;
                                const float maxRatio = std::pow(kMaxScale,
                                    1.0f / juce::jmax(morph, 0.01f));
                                const float envRatio = (sideMags[bin] > 1e-10f)
                                    ? juce::jmin(sideMags[bin] / mainEnv[bin], maxRatio)
                                    : 0.0f;
                                sc = std::pow(envRatio, morph);
                            }

                            morphFrame[bin * 2]     = re * sc;
                            morphFrame[bin * 2 + 1] = im * sc;

                            const float morphedMag = mainMag * sc;
                            smoothedMorphed[bin] = smoothedMorphed[bin] * smoothMVis
                                                 + morphedMag * (1.0f - smoothMVis);
                        }
                    }
                }

                // Apply gate then enhance to morphFrame
                if (gateOn)    applySpectralGateToFrame(morphFrame.data(), numBins);
                if (enhanceOn) applySpectralEnhanceToFrame(morphFrame.data(), numBins);

                // Extract per-bin magnitude scale ratio: morphed / analysis
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float reA  = analysisFrame[bin * 2], imA = analysisFrame[bin * 2 + 1];
                    const float magA = std::sqrt(reA * reA + imA * imA);
                    const float reM  = morphFrame[bin * 2],   imM = morphFrame[bin * 2 + 1];
                    const float magM = std::sqrt(reM * reM + imM * imM);
                    scaleBuf[bin] = (magA > 1e-12f) ? magM / magA : 0.0f;
                }

                // Apply scale to L channel: FFT → magnitude scale (phase preserved) → IFFT → OLA
                {
                    for (int i = 0; i < fftSize; ++i)
                        synthFrameL[i] = synthFifoL[i] * window[i];
                    std::fill(synthFrameL.begin() + fftSize, synthFrameL.end(), 0.0f);
                    fftMain->performRealOnlyForwardTransform(synthFrameL.data(), true);
                    for (int bin = 0; bin < numBins; ++bin)
                    {
                        synthFrameL[bin * 2]     *= scaleBuf[bin];
                        synthFrameL[bin * 2 + 1] *= scaleBuf[bin];
                    }
                    fftMain->performRealOnlyInverseTransform(synthFrameL.data());
                    for (int i = 0; i < fftSize; ++i)
                        outputOLAL[(writeStart + i) % olaBufSize] += synthFrameL[i] * window[i];
                }

                // Apply scale to R channel
                {
                    for (int i = 0; i < fftSize; ++i)
                        synthFrameR[i] = synthFifoR[i] * window[i];
                    std::fill(synthFrameR.begin() + fftSize, synthFrameR.end(), 0.0f);
                    fftMain->performRealOnlyForwardTransform(synthFrameR.data(), true);
                    for (int bin = 0; bin < numBins; ++bin)
                    {
                        synthFrameR[bin * 2]     *= scaleBuf[bin];
                        synthFrameR[bin * 2 + 1] *= scaleBuf[bin];
                    }
                    fftMain->performRealOnlyInverseTransform(synthFrameR.data());
                    for (int i = 0; i < fftSize; ++i)
                        outputOLAR[(writeStart + i) % olaBufSize] += synthFrameR[i] * window[i];
                }
            }

            // Slide all main FIFOs
            std::copy(mainFifo.begin() + hopSize, mainFifo.begin() + fftSize, mainFifo.begin());
            std::fill(mainFifo.begin() + (fftSize - hopSize), mainFifo.begin() + fftSize, 0.0f);
            std::copy(synthFifoL.begin() + hopSize, synthFifoL.begin() + fftSize, synthFifoL.begin());
            std::fill(synthFifoL.begin() + (fftSize - hopSize), synthFifoL.begin() + fftSize, 0.0f);
            std::copy(synthFifoR.begin() + hopSize, synthFifoR.begin() + fftSize, synthFifoR.begin());
            std::fill(synthFifoR.begin() + (fftSize - hopSize), synthFifoR.begin() + fftSize, 0.0f);
            mainFifoIndex -= hopSize;
        }

        // ------------------------------------------------------------------
        // E. Sidechain FFT hop — analysis only
        // ------------------------------------------------------------------
        if (sidechainFifoIndex >= fftSize)
        {
            for (int i = 0; i < fftSize; ++i)
                analysisFrame[i] = sidechainFifo[i] * window[i];
            std::fill(analysisFrame.begin() + fftSize, analysisFrame.end(), 0.0f);
            fftSidechain->performRealOnlyForwardTransform(analysisFrame.data(), true);

            // E1. Apply sidechain spectral filter curve
            if (sidechainFilterDirty.load(std::memory_order_acquire))
            {
                { juce::ScopedLock lock(filterCurveLock); sidechainFilterCurve_read = sidechainFilterCurve_write; }
                sidechainFilterDirty.store(false, std::memory_order_release);
                rebuildFilterGain(sidechainFilterGain, sidechainFilterCurve_read, numBins);
            }
            applyFilterGains(analysisFrame.data(), numBins, sidechainFilterGain);

            const float normF = 2.0f / (float)fftSize;
            const float smoothVisS = smoothSidechainParam->load(std::memory_order_relaxed);
            const bool  smoothSideA = smoothSideAudioParam->load(std::memory_order_relaxed) >= 0.5f;
            const float smoothSideC = smoothSideA ? smoothVisS : 0.0f;
            const bool  smoothMorphA = smoothMorphAudioParam->load(std::memory_order_relaxed) >= 0.5f;
            const float smoothMorphC = smoothMorphA ? smoothMorphParam->load(std::memory_order_relaxed) : 0.0f;

            {
                juce::ScopedLock l(displayLock);
                for (int bin = 0; bin < numBins; ++bin)
                {
                    const float re = analysisFrame[bin * 2];
                    const float im = analysisFrame[bin * 2 + 1];
                    const float mag = std::sqrt(re * re + im * im) * normF;
                    smoothedSidechain[bin] = smoothedSidechain[bin] * smoothVisS
                        + mag * (1.0f - smoothVisS);
                    instantaneousSidechain[bin] = mag;
                }

                // Sidechain audio smooth: optionally IIR-smooth the instantaneous
                // magnitudes before feeding them into the log-envelope computation
                float* envInput = scratchEnvInput.data();
                if (smoothSideA)
                {
                    for (int bin = 0; bin < numBins; ++bin)
                        smoothedSideForAudio[bin] = smoothedSideForAudio[bin] * smoothSideC
                        + instantaneousSidechain[bin] * (1.0f - smoothSideC);
                    std::copy(smoothedSideForAudio.begin(),
                        smoothedSideForAudio.begin() + numBins, envInput);
                }
                else
                {
                    std::copy(instantaneousSidechain.begin(),
                        instantaneousSidechain.begin() + numBins, envInput);
                }

                // Build frequency-smoothed envelope for morphing
                float* envTmp = scratchEnvTmp.data();
                computeLogEnvelope(envInput, envTmp, numBins,
                    envelopeWidthParam->load(std::memory_order_relaxed),
                    scratchPrefixSum.data());

                // Morph smooth: optionally IIR-smooth the envelope itself
                for (int bin = 0; bin < numBins; ++bin)
                    morphEnvelopeSidechain[bin] = morphEnvelopeSidechain[bin] * smoothMorphC
                    + envTmp[bin] * (1.0f - smoothMorphC);
            }

            std::copy(sidechainFifo.begin() + hopSize,
                sidechainFifo.begin() + fftSize,
                sidechainFifo.begin());
            std::fill(sidechainFifo.begin() + (fftSize - hopSize),
                sidechainFifo.begin() + fftSize, 0.0f);
            sidechainFifoIndex -= hopSize;
        }
    }
}

//==============================================================================
// Display data accessors
//==============================================================================
void SpectralCompareAudioProcessor::getMainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMain.begin(), smoothedMain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getSidechainFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedSidechain.begin(), smoothedSidechain.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getMorphedFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedMorphed.begin(), smoothedMorphed.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

void SpectralCompareAudioProcessor::getOutputFFTData(float* out, int numBinsOut)
{
    juce::ScopedLock l(displayLock);
    const int n = juce::jmin(numBinsOut, numBins);
    std::copy(smoothedOutput.begin(), smoothedOutput.begin() + n, out);
    std::fill(out + n, out + numBinsOut, 0.0f);
}

//==============================================================================
// Colors
//==============================================================================
void SpectralCompareAudioProcessor::setBackgroundColor(juce::Colour c) { backgroundColor = c; }
void SpectralCompareAudioProcessor::setGridColor(juce::Colour c) { gridColor = c; }
void SpectralCompareAudioProcessor::setMainSpectrumColor(juce::Colour c) { mainSpectrumColor = c; }
void SpectralCompareAudioProcessor::setSidechainSpectrumColor(juce::Colour c) { sidechainSpectrumColor = c; }
void SpectralCompareAudioProcessor::setDeltaColor(juce::Colour c) { deltaColor = c; }
void SpectralCompareAudioProcessor::setMorphColor(juce::Colour c) { morphColor = c; }
void SpectralCompareAudioProcessor::setSidebarColor(juce::Colour c) { sidebarColor = c; }
void SpectralCompareAudioProcessor::setOutputSpectrumColor(juce::Colour c) { outputSpectrumColor = c; }
void SpectralCompareAudioProcessor::setTextColor(juce::Colour c)           { textColor = c; }

void SpectralCompareAudioProcessor::resetColors()
{
    backgroundColor      = juce::Colour(0xff1a1a1a);
    gridColor            = juce::Colour(0xff444444);
    mainSpectrumColor    = juce::Colour(0xff00ff00);
    sidechainSpectrumColor = juce::Colour(0xffff00ff);
    deltaColor           = juce::Colour(0xffffff00);
    morphColor           = juce::Colour(0xff00e5ff);
    sidebarColor         = juce::Colour(0xff1a1a1a);
    outputSpectrumColor  = juce::Colour(0xff4477ff);
    textColor            = juce::Colour(0xffcccccc);
}

//==============================================================================
// State persistence
//==============================================================================
void SpectralCompareAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("bgColor",       (int)backgroundColor.getARGB(),      nullptr);
    state.setProperty("gridColor",     (int)gridColor.getARGB(),             nullptr);
    state.setProperty("mainColor",     (int)mainSpectrumColor.getARGB(),     nullptr);
    state.setProperty("sidechainColor",(int)sidechainSpectrumColor.getARGB(),nullptr);
    state.setProperty("deltaColor",    (int)deltaColor.getARGB(),            nullptr);
    state.setProperty("morphColor",    (int)morphColor.getARGB(),            nullptr);
    state.setProperty("sidebarColor",  (int)sidebarColor.getARGB(),          nullptr);
    state.setProperty("outputColor",   (int)outputSpectrumColor.getARGB(),   nullptr);
    state.setProperty("textColor",     (int)textColor.getARGB(),             nullptr);

    // Save filter curves (numBins floats each, base64-encoded)
    {
        juce::ScopedLock lock(filterCurveLock);
        juce::MemoryBlock mainMB, sideMB;
        mainMB.append(mainFilterCurve_write.data(), (size_t)numBins * sizeof(float));
        sideMB.append(sidechainFilterCurve_write.data(), (size_t)numBins * sizeof(float));
        state.setProperty("mainFilterCurve", mainMB.toBase64Encoding(), nullptr);
        state.setProperty("sideFilterCurve", sideMB.toBase64Encoding(), nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpectralCompareAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr) return;

    auto state = juce::ValueTree::fromXml(*xml);
    if (state.isValid()) apvts.replaceState(state);

    auto getCol = [&](const juce::Identifier& id, juce::uint32 def) -> juce::Colour {
        if (state.hasProperty(id))
            return juce::Colour((juce::uint32)(int)state.getProperty(id));
        return juce::Colour(def);
        };
    backgroundColor        = getCol("bgColor",       0xff1a1a1a);
    gridColor              = getCol("gridColor",     0xff444444);
    mainSpectrumColor      = getCol("mainColor",     0xff00ff00);
    sidechainSpectrumColor = getCol("sidechainColor",0xffff00ff);
    deltaColor             = getCol("deltaColor",    0xffffff00);
    morphColor             = getCol("morphColor",    0xff00e5ff);
    sidebarColor           = getCol("sidebarColor",  0xff1a1a1a);
    outputSpectrumColor    = getCol("outputColor",   0xff4477ff);
    textColor              = getCol("textColor",     0xffcccccc);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("fftSize")))
    {
        const int sizes[] = { 1024, 2048, 4096, 8192 };
        const int idx = p->getIndex();
        if (idx >= 0 && idx < 4) setFFTSize(sizes[idx]);
    }

    // Restore filter curves
    auto loadCurve = [&](const juce::Identifier& id, std::array<float, maxBins>& curve,
                         std::atomic<bool>& dirty)
    {
        if (!state.hasProperty(id)) return;
        juce::MemoryBlock mb;
        if (!mb.fromBase64Encoding(state.getProperty(id).toString())) return;
        const int bytesToLoad = juce::jmin((int)mb.getSize(), numBins * (int)sizeof(float));
        juce::ScopedLock lock(filterCurveLock);
        std::memcpy(curve.data(), mb.getData(), (size_t)bytesToLoad);
        dirty.store(true, std::memory_order_release);
    };
    loadCurve("mainFilterCurve", mainFilterCurve_write, mainFilterDirty);
    loadCurve("sideFilterCurve", sidechainFilterCurve_write, sidechainFilterDirty);
}

bool SpectralCompareAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SpectralCompareAudioProcessor::createEditor()
{
    return new SpectralCompareAudioProcessorEditor(*this);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralCompareAudioProcessor();
}