#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Helper — used in both loadCurvesFromBinary and setStateInformation
static constexpr juce::int32 kStateMagic = 0x53504C32;  // 'SPL2'

static bool isValidFFTSize (int s)
{
    return s == 32 || s == 128 || s == 256  || s == 512 ||
           s == 1024 || s == 2048 || s == 4096 || s == 8192;
}

// Canonical ordered list of supported FFT sizes (index maps to APVTS choice)
static const int kFFTSizes[8] = { 32, 128, 256, 512, 1024, 2048, 4096, 8192 };

static int fftSizeToIndex (int sz)
{
    for (int i = 0; i < 8; ++i)
        if (kFFTSizes[i] == sz) return i;
    return 5; // fallback: 2048
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SpectralLatencyAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Max latency: 0.01–10 s, skewed so 0.5 s sits exactly at mid-travel.
    // NormalisableRange skew = log((0.5 - 0.01)/(10.0 - 0.01)) / log(0.5) ≈ 4.352
    // (equivalent to Slider::setSkewFactorFromMidPoint(0.5) on [0.01, 10.0])
    juce::NormalisableRange<float> latencyRange (0.01f, 10.0f, 0.001f);
    latencyRange.setSkewForCentre (0.5f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "maxLatency", "Max Latency (s)", latencyRange, 1.0f));

    // FFT size: 8 choices, default index 5 = 2048
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "fftSizeIndex", "FFT Size",
        juce::StringArray { "32", "128", "256", "512", "1024", "2048", "4096", "8192" },
        5));

    // Delay curve bins for FFT size 32 (17 bins: 0-16)
    // Range: -1.0 (earlier) to +1.0 (later), default 0.0 (no delay)
    for (int i = 0; i < 17; ++i)
    {
        juce::String paramID   = "bin" + juce::String (i);
        juce::String paramName = "Bin " + juce::String (i) + " Delay";
        
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            paramID, paramName, -1.0f, 1.0f, 0.0f));
    }

    return layout;
}

//==============================================================================
SpectralLatencyAudioProcessor::SpectralLatencyAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    apvts.addParameterListener ("maxLatency",   this);
    apvts.addParameterListener ("fftSizeIndex", this);
    
    // Add listeners for all 17 bin parameters (FFT size 32)
    for (int i = 0; i < 17; ++i)
        apvts.addParameterListener ("bin" + juce::String (i), this);

    delayCurve_write.fill (0.0f);
    delayCurve_read .fill (0.0f);

    fftProcessor = std::make_unique<juce::dsp::FFT> (fftOrder);
    allocateBuffers();
}

SpectralLatencyAudioProcessor::~SpectralLatencyAudioProcessor()
{
    apvts.removeParameterListener ("maxLatency",   this);
    apvts.removeParameterListener ("fftSizeIndex", this);
    
    // Remove listeners for all 17 bin parameters (FFT size 32)
    for (int i = 0; i < 17; ++i)
        apvts.removeParameterListener ("bin" + juce::String (i), this);
}

//==============================================================================
// APVTS::Listener — called on the message thread whenever a parameter changes.
// isRestoringState guards against re-entrancy in JUCE versions that fire
// listeners synchronously from apvts.replaceState().
//==============================================================================

void SpectralLatencyAudioProcessor::parameterChanged (const juce::String& parameterID,
                                                       float newValue)
{
    if (isRestoringState)
        return;

    if (parameterID == "maxLatency")
    {
        float clamped = juce::jlimit (0.01f, 10.0f, newValue);
        if (clamped != maxLatencySeconds)
        {
            maxLatencySeconds = clamped;
            updateLatencyCompensation();
        }
    }
    else if (parameterID == "fftSizeIndex")
    {
        int idx     = juce::jlimit (0, 7, (int) std::round (newValue));
        int newSize = kFFTSizes[idx];
        if (newSize != fftSize)
            setFFTSize (newSize);
    }
    else if (parameterID.startsWith ("bin") && fftSize == 32)
    {
        // Handle bin parameter changes when FFT size is 32
        int binIndex = parameterID.substring (3).getIntValue();
        if (binIndex >= 0 && binIndex < 17)
        {
            float clamped = juce::jlimit (-1.0f, 1.0f, newValue);
            
            juce::ScopedLock lock (curveLock);
            delayCurve_write[binIndex] = clamped;
            curveIsDirty = true;
        }
    }
}

//==============================================================================
const juce::String SpectralLatencyAudioProcessor::getName()          const { return JucePlugin_Name; }
bool  SpectralLatencyAudioProcessor::acceptsMidi()                   const { return false; }
bool  SpectralLatencyAudioProcessor::producesMidi()                  const { return false; }
bool  SpectralLatencyAudioProcessor::isMidiEffect()                  const { return false; }
double SpectralLatencyAudioProcessor::getTailLengthSeconds()         const { return 0.0; }
int   SpectralLatencyAudioProcessor::getNumPrograms() { return 1; }
int   SpectralLatencyAudioProcessor::getCurrentProgram() { return 0; }
void  SpectralLatencyAudioProcessor::setCurrentProgram (int) {}
const juce::String SpectralLatencyAudioProcessor::getProgramName (int) { return {}; }
void  SpectralLatencyAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void SpectralLatencyAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    fifoWritePos = 0;

    inputFifoL.assign (fftSize * 2, 0.0f);
    inputFifoR.assign (fftSize * 2, 0.0f);
    outputAccumL.assign (fftSize * 2, 0.0f);
    outputAccumR.assign (fftSize * 2, 0.0f);
    accumReadPosL = 0;
    accumReadPosR = 0;

    for (auto& bd : binDelays)
    {
        bd.bufferL.clear();
        bd.bufferR.clear();
        bd.currentDelaySamples = 0;
    }

    {
        juce::ScopedLock lock (displayLock);
        smoothedSpectrum.assign (maxBins, 0.0f);
    }

    updateLatencyCompensation();
}

void SpectralLatencyAudioProcessor::releaseResources() {}

bool SpectralLatencyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()) return false;
    return true;
}

//==============================================================================
void SpectralLatencyAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float> (i) / static_cast<float> (fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi * n));
    }

    float overlaps = static_cast<float> (fftSize) / static_cast<float> (hopSize);
    windowNormFactor = 1.0f / (0.375f * overlaps);
}

void SpectralLatencyAudioProcessor::allocateBuffers()
{
    inputFifoL.assign  (fftSize * 2, 0.0f);
    inputFifoR.assign  (fftSize * 2, 0.0f);
    fftBufL.assign     (fftSize * 2, 0.0f);
    fftBufR.assign     (fftSize * 2, 0.0f);
    outputAccumL.assign (fftSize * 2, 0.0f);
    outputAccumR.assign (fftSize * 2, 0.0f);

    window.resize (fftSize);
    accumReadPosL = 0;
    accumReadPosR = 0;
    fifoWritePos  = 0;

    smoothedSpectrum.assign (maxBins, 0.0f);

    maxDelayFrames = static_cast<int> ((10.0 * 48000.0) / hopSize) + 10;

    binDelays.clear();
    binDelays.resize (maxBins);
    for (auto& bd : binDelays)
    {
        bd.bufferL.clear();
        bd.bufferR.clear();
        bd.currentDelaySamples = 0;
    }

    createWindow();
}

//==============================================================================
// Helper: Sync curve to APVTS bin parameters when FFT size is 32
//==============================================================================

void SpectralLatencyAudioProcessor::syncBinsToAPVTS()
{
    if (fftSize != 32)
        return;
    
    // Push current curve values (bins 0-16) into the APVTS bin parameters
    for (int i = 0; i < 17; ++i)
    {
        juce::String paramID = "bin" + juce::String (i);
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (paramID)))
        {
            float curveValue = delayCurve_write[i];
            p->setValueNotifyingHost (p->convertTo0to1 (curveValue));
        }
    }
}

//==============================================================================
// Delay curve — message-thread API
//==============================================================================

void SpectralLatencyAudioProcessor::setDelayCurveRange (int startBin, int endBin,
                                                         float startVal, float endVal)
{
    if (startBin > endBin) { std::swap (startBin, endBin); std::swap (startVal, endVal); }
    startBin = juce::jlimit (0, numBins - 1, startBin);
    endBin   = juce::jlimit (0, numBins - 1, endBin);

    {
        juce::ScopedLock lock (curveLock);
        int span = endBin - startBin;

        if (span == 0)
        {
            delayCurve_write[startBin] = juce::jlimit (-1.0f, 1.0f, startVal);
        }
        else
        {
            for (int bin = startBin; bin <= endBin; ++bin)
            {
                float t = static_cast<float> (bin - startBin) / static_cast<float> (span);
                float v = startVal + t * (endVal - startVal);
                delayCurve_write[bin] = juce::jlimit (-1.0f, 1.0f, v);
            }
        }
    }

    curveIsDirty.store (true, std::memory_order_release);
    updateLatencyCompensation();
    
    // Sync to APVTS bin parameters if FFT size is 32
    syncBinsToAPVTS();
}

void SpectralLatencyAudioProcessor::resetDelayCurve()
{
    {
        juce::ScopedLock lock (curveLock);
        delayCurve_write.fill (0.0f);
    }
    curveIsDirty.store (true, std::memory_order_release);
    updateLatencyCompensation();
    
    // Sync to APVTS bin parameters if FFT size is 32
    syncBinsToAPVTS();
}

void SpectralLatencyAudioProcessor::randomize()
{
    auto& r = juce::Random::getSystemRandom();
    float newMaxLatency;

    {
        juce::ScopedLock lock (curveLock);
        for (int i = 0; i < numBins; ++i)
            delayCurve_write[i] = r.nextFloat() * 2.0f - 1.0f;

        newMaxLatency     = 0.01f + r.nextFloat() * 9.99f;
        maxLatencySeconds = newMaxLatency;
    }

    curveIsDirty.store (true, std::memory_order_release);
    updateLatencyCompensation();

    // Notify the APVTS and the DAW host so the max-latency slider (and any
    // automation lane) reflects the new value.  The resulting async
    // parameterChanged("maxLatency") callback will call setMaxLatency with the
    // same value, which is idempotent.
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("maxLatency")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (newMaxLatency));
        p->endChangeGesture();
    }
    
    // Sync to APVTS bin parameters if FFT size is 32
    syncBinsToAPVTS();
}

void SpectralLatencyAudioProcessor::getDelayCurve (std::array<float, maxBins>& dest)
{
    juce::ScopedLock lock (curveLock);
    dest = delayCurve_write;
}

//==============================================================================
// Spectrum display — message-thread API
//==============================================================================

void SpectralLatencyAudioProcessor::getSpectrumData (float* out, int numBinsOut)
{
    juce::ScopedLock lock (displayLock);
    int n = juce::jmin (numBinsOut, (int) smoothedSpectrum.size());
    for (int i = 0; i < n; ++i)
        out[i] = smoothedSpectrum[i];
}

//==============================================================================
// FFT size and latency control
//==============================================================================

void SpectralLatencyAudioProcessor::setFFTSize (int newSize)
{
    if (!isValidFFTSize (newSize))
        return;

    int newOrder = 5;
    if      (newSize == 128)  newOrder = 7;
    else if (newSize == 256)  newOrder = 8;
    else if (newSize == 512)  newOrder = 9;
    else if (newSize == 1024) newOrder = 10;
    else if (newSize == 2048) newOrder = 11;
    else if (newSize == 4096) newOrder = 12;
    else if (newSize == 8192) newOrder = 13;

    suspendProcessing (true);

    // Save current size's state only when we are not mid-restore
    // (setStateInformation / loadCurvesFromBinary has already populated
    //  allSizeStates with the correct saved data).
    if (!isRestoringState)
    {
        juce::ScopedLock lock (curveLock);
        auto& curr       = allSizeStates[fftSize];
        curr.curve       = delayCurve_write;
        curr.maxLatency  = maxLatencySeconds;
    }

    fftOrder = newOrder;
    fftSize  = newSize;
    hopSize  = fftSize / 4;
    numBins  = fftSize / 2 + 1;

    fftProcessor = std::make_unique<juce::dsp::FFT> (fftOrder);
    allocateBuffers();

    // Load saved state for the new size (or zeroed defaults on first visit).
    {
        juce::ScopedLock lock (curveLock);
        auto it = allSizeStates.find (newSize);
        if (it != allSizeStates.end())
        {
            delayCurve_write  = it->second.curve;
            maxLatencySeconds = it->second.maxLatency;
        }
        else
        {
            delayCurve_write.fill (0.0f);
            // maxLatencySeconds keeps its previous value on first visit
        }
    }
    curveIsDirty.store (true, std::memory_order_release);

    updateLatencyCompensation();
    
    // Sync to APVTS bin parameters if switching to FFT size 32
    syncBinsToAPVTS();
    
    suspendProcessing (false);
}

void SpectralLatencyAudioProcessor::setMaxLatency (float seconds)
{
    maxLatencySeconds = juce::jlimit (0.01f, 10.0f, seconds);
    updateLatencyCompensation();
}

//==============================================================================

int SpectralLatencyAudioProcessor::calculateRequiredLatencyFrames() const
{
    float minCurveValue = 0.0f;
    {
        juce::ScopedLock lock (curveLock);
        for (int i = 0; i < numBins; ++i)
            minCurveValue = juce::jmin (minCurveValue, delayCurve_write[i]);
    }

    if (minCurveValue < 0.0f)
    {
        float latencySeconds = -minCurveValue * maxLatencySeconds;
        int   latencyFrames  = static_cast<int> (latencySeconds * currentSampleRate / hopSize);
        return latencyFrames;
    }

    return 0;
}

void SpectralLatencyAudioProcessor::updateLatencyCompensation()
{
    int requiredFrames  = calculateRequiredLatencyFrames();
    int latencySamples  = requiredFrames * hopSize + (fftSize - hopSize);

    reportedLatencySamples.store (latencySamples, std::memory_order_release);
    setLatencySamples (latencySamples);
}

//==============================================================================
// Core DSP — called every hopSize samples from processBlock
//==============================================================================

void SpectralLatencyAudioProcessor::processFFTFrame()
{
    if (curveIsDirty.load (std::memory_order_acquire))
    {
        {
            juce::ScopedLock lock (curveLock);
            delayCurve_read = delayCurve_write;
        }
        curveIsDirty.store (false, std::memory_order_release);
    }

    int globalDelayFrames = calculateRequiredLatencyFrames();

    for (auto* buf : { &fftBufL, &fftBufR })
        std::fill (buf->begin(), buf->end(), 0.0f);

    for (int i = 0; i < fftSize; ++i)
    {
        fftBufL[i] = inputFifoL[i] * window[i];
        fftBufR[i] = inputFifoR[i] * window[i];
    }

    fftProcessor->performRealOnlyForwardTransform (fftBufL.data(), true);
    fftProcessor->performRealOnlyForwardTransform (fftBufR.data(), true);

    std::array<float, maxBins> localMag{};

    for (int bin = 0; bin < numBins; ++bin)
    {
        std::complex<float> complexL (fftBufL[bin * 2], fftBufL[bin * 2 + 1]);
        std::complex<float> complexR (fftBufR[bin * 2], fftBufR[bin * 2 + 1]);

        float delayNorm    = delayCurve_read[bin];
        float delaySeconds = delayNorm * maxLatencySeconds;
        int   delayFrames  = static_cast<int> (delaySeconds * currentSampleRate / hopSize);

        int totalDelayFrames = delayFrames + globalDelayFrames;
        totalDelayFrames = juce::jmax (0, totalDelayFrames);

        auto& bd = binDelays[bin];

        bd.bufferL.push_back (complexL);
        bd.bufferR.push_back (complexR);

        int requiredSize = totalDelayFrames + 1;
        while ((int) bd.bufferL.size() > requiredSize && !bd.bufferL.empty())
        {
            bd.bufferL.pop_front();
            bd.bufferR.pop_front();
        }

        std::complex<float> delayedL = complexL;
        std::complex<float> delayedR = complexR;

        if ((int) bd.bufferL.size() > totalDelayFrames)
        {
            delayedL = bd.bufferL[bd.bufferL.size() - totalDelayFrames - 1];
            delayedR = bd.bufferR[bd.bufferR.size() - totalDelayFrames - 1];
        }

        fftBufL[bin * 2]     = delayedL.real();
        fftBufL[bin * 2 + 1] = delayedL.imag();
        fftBufR[bin * 2]     = delayedR.real();
        fftBufR[bin * 2 + 1] = delayedR.imag();

        localMag[bin] = std::abs (complexL);
    }

    {
        juce::ScopedLock lock (displayLock);
        const float smooth = 0.7f;
        for (int bin = 0; bin < numBins; ++bin)
            smoothedSpectrum[bin] = smoothedSpectrum[bin] * smooth + localMag[bin] * (1.0f - smooth);
    }

    fftProcessor->performRealOnlyInverseTransform (fftBufL.data());
    fftProcessor->performRealOnlyInverseTransform (fftBufR.data());

    const int accumSize = (int) outputAccumL.size();
    for (int i = 0; i < fftSize; ++i)
    {
        int posL = (accumReadPosL + i) % accumSize;
        int posR = (accumReadPosR + i) % accumSize;
        outputAccumL[posL] += fftBufL[i] * window[i] * windowNormFactor;
        outputAccumR[posR] += fftBufR[i] * window[i] * windowNormFactor;
    }
}

//==============================================================================
void SpectralLatencyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalIn    = getTotalNumInputChannels();
    const int totalOut   = getTotalNumOutputChannels();

    const float* inL = buffer.getReadPointer (0);
    const float* inR = (totalIn > 1) ? buffer.getReadPointer (1) : inL;

    float* outL = buffer.getWritePointer (0);
    float* outR = (totalOut > 1) ? buffer.getWritePointer (1) : outL;

    for (int s = 0; s < numSamples; ++s)
    {
        inputFifoL[fifoWritePos] = inL[s];
        inputFifoR[fifoWritePos] = inR[s];
        fifoWritePos++;

        if (fifoWritePos >= fftSize)
        {
            processFFTFrame();

            std::memmove (inputFifoL.data(),
                          inputFifoL.data() + hopSize,
                          (fftSize - hopSize) * sizeof (float));
            std::memmove (inputFifoR.data(),
                          inputFifoR.data() + hopSize,
                          (fftSize - hopSize) * sizeof (float));

            fifoWritePos -= hopSize;
        }

        float outSampleL = outputAccumL[accumReadPosL];
        float outSampleR = outputAccumR[accumReadPosR];

        outputAccumL[accumReadPosL] = 0.0f;
        outputAccumR[accumReadPosR] = 0.0f;

        accumReadPosL = (accumReadPosL + 1) % (int) outputAccumL.size();
        accumReadPosR = (accumReadPosR + 1) % (int) outputAccumR.size();

        outL[s] = outSampleL;
        outR[s] = outSampleR;
    }

    for (int ch = 2; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
// Editor
//==============================================================================

bool SpectralLatencyAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralLatencyAudioProcessor::createEditor()
{
    return new SpectralLatencyAudioProcessorEditor (*this);
}

//==============================================================================
// State persistence helpers
//==============================================================================
//
// Binary (v2) format used inside curvesBinary:
//
//   [int32 : magic = 0x53504C32  ('SPL2')]
//   [int32 : activeFFTSize]
//   [int32 : numEntries]
//   for each entry:
//     [int32 : fftSize]
//     [float : maxLatency]
//     [int32 : numBins  (= fftSize/2+1)]
//     [numBins × float : curve values]
//
// Legacy v1 format (no magic):
//   [int32 : fftSize] [float : maxLatency] [numBins × float : curve]
//==============================================================================

void SpectralLatencyAudioProcessor::saveCurvesToBinary (juce::MemoryBlock& destData)
{
    // Flush the current size's live state into allSizeStates.
    {
        juce::ScopedLock lock (curveLock);
        auto& curr      = allSizeStates[fftSize];
        curr.curve      = delayCurve_write;
        curr.maxLatency = maxLatencySeconds;
    }

    int    numEntries = (int) allSizeStates.size();
    size_t totalSize  = 12; // magic + activeFFTSize + numEntries
    for (auto& kv : allSizeStates)
    {
        int bins = kv.first / 2 + 1;
        totalSize += 4 + 4 + 4 + (size_t) bins * 4;
    }

    destData.setSize (totalSize, true);
    char* ptr = static_cast<char*> (destData.getData());

    juce::int32 magic = kStateMagic;
    std::memcpy (ptr, &magic,      4);  ptr += 4;
    std::memcpy (ptr, &fftSize,    4);  ptr += 4;
    std::memcpy (ptr, &numEntries, 4);  ptr += 4;

    for (auto& kv : allSizeStates)
    {
        int   sz   = kv.first;
        float lat  = kv.second.maxLatency;
        int   bins = sz / 2 + 1;

        std::memcpy (ptr, &sz,   4);                                    ptr += 4;
        std::memcpy (ptr, &lat,  4);                                    ptr += 4;
        std::memcpy (ptr, &bins, 4);                                    ptr += 4;
        std::memcpy (ptr, kv.second.curve.data(), (size_t) bins * 4); ptr += bins * 4;
    }
}

void SpectralLatencyAudioProcessor::loadCurvesFromBinary (const void* data, int sizeBytes)
{
    if (sizeBytes < 12)
        return;

    const char* ptr = static_cast<const char*> (data);
    const char* end = ptr + sizeBytes;

    juce::int32 firstWord;
    std::memcpy (&firstWord, ptr, 4);

    if (firstWord == kStateMagic)
    {
        // ===== v2 format =====
        ptr += 4;

        int savedActiveSize;
        std::memcpy (&savedActiveSize, ptr, 4);  ptr += 4;

        int numEntries;
        std::memcpy (&numEntries, ptr, 4);       ptr += 4;

        if (numEntries < 1 || numEntries > 8)
            return;

        allSizeStates.clear();

        for (int i = 0; i < numEntries; ++i)
        {
            if (ptr + 12 > end) break;

            int   sz;   std::memcpy (&sz,   ptr, 4);  ptr += 4;
            float lat;  std::memcpy (&lat,  ptr, 4);  ptr += 4;
            int   bins; std::memcpy (&bins, ptr, 4);  ptr += 4;

            if (bins < 1 || bins > maxBins) break;
            if (ptr + bins * 4 > end)       break;
            if (!isValidFFTSize (sz))        break;

            PerSizeState state;
            state.maxLatency = juce::jlimit (0.01f, 10.0f, lat);
            state.curve.fill (0.0f);
            std::memcpy (state.curve.data(), ptr, (size_t) bins * 4);
            ptr += bins * 4;

            allSizeStates[sz] = std::move (state);
        }

        isRestoringState = true;
        setFFTSize (isValidFFTSize (savedActiveSize) ? savedActiveSize : 2048);
        isRestoringState = false;
    }
    else
    {
        // ===== v1 legacy format =====
        int   savedSize;
        float savedLatency;
        std::memcpy (&savedSize,    ptr, 4);
        std::memcpy (&savedLatency, ptr + 4, 4);

        if (!isValidFFTSize (savedSize)) return;

        int savedBins        = savedSize / 2 + 1;
        int expectedSizeBytes = 4 + 4 + savedBins * 4;
        if (sizeBytes < expectedSizeBytes) return;

        PerSizeState state;
        state.maxLatency = juce::jlimit (0.01f, 10.0f, savedLatency);
        state.curve.fill (0.0f);
        std::memcpy (state.curve.data(), ptr + 8, (size_t) savedBins * 4);

        allSizeStates.clear();
        allSizeStates[savedSize] = std::move (state);

        isRestoringState = true;
        setFFTSize (savedSize);
        isRestoringState = false;
    }
}

void SpectralLatencyAudioProcessor::syncAPVTSFromState()
{
    // Push current maxLatencySeconds into the APVTS parameter.
    // This fires an async parameterChanged which calls setMaxLatency with the
    // same value — idempotent and harmless.
    if (auto* p = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter ("maxLatency")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (maxLatencySeconds));
        p->endChangeGesture();
    }

    // Push current fftSize as choice index.
    int idx = fftSizeToIndex (fftSize);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("fftSizeIndex")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (idx));
        p->endChangeGesture();
    }
}

//==============================================================================
// getStateInformation / setStateInformation
//==============================================================================
//
// New format:
//   APVTS ValueTree serialised as XML (via copyXmlToBinary).
//   The v2 binary curve blob is base64-encoded in a "curvesBinary" property
//   on the root ValueTree node.
//
// Backward compatibility:
//   If the data cannot be parsed as XML (i.e. it's a legacy binary blob from
//   an earlier version), loadCurvesFromBinary handles v1 and v2 detection,
//   then syncAPVTSFromState() writes the restored values back to APVTS so
//   the editor and host see the correct state.
//==============================================================================

void SpectralLatencyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialise all curves to binary
    juce::MemoryBlock curveBlock;
    saveCurvesToBinary (curveBlock);

    // Embed the binary blob as a base64 property on the APVTS tree
    auto tree = apvts.copyState();
    tree.setProperty ("curvesBinary", curveBlock.toBase64Encoding(), nullptr);

    // Write the whole tree as XML
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectralLatencyAudioProcessor::setStateInformation (const void* data, int sizeBytes)
{
    // -------------------------------------------------------------------------
    // Try the new XML-based format first
    // -------------------------------------------------------------------------
    if (auto xml = getXmlFromBinary (data, sizeBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        // 1. Load curve data so allSizeStates is fully populated before the
        //    APVTS replaceState triggers parameterChanged → setFFTSize.
        juce::String base64 = tree.getProperty ("curvesBinary",
                                                 juce::var ("")).toString();
        if (base64.isNotEmpty())
        {
            juce::MemoryBlock curveBlock;
            if (curveBlock.fromBase64Encoding (base64))
            {
                // isRestoringState stays true across both loadCurvesFromBinary
                // and replaceState to cover JUCE versions that fire
                // parameterChanged synchronously from replaceState.
                isRestoringState = true;
                loadCurvesFromBinary (curveBlock.getData(),
                                      (int) curveBlock.getSize());
                // Note: loadCurvesFromBinary resets isRestoringState internally,
                // but we reset it again after replaceState below for safety.
                isRestoringState = true;
            }
        }

        // 2. Remove our custom property so APVTS doesn't see unknown data
        tree.removeProperty ("curvesBinary", nullptr);

        // 3. Restore APVTS parameters (maxLatency, fftSizeIndex).
        //    In JUCE versions that fire parameterChanged synchronously here,
        //    isRestoringState prevents redundant setFFTSize / setMaxLatency calls.
        //    In async JUCE versions the guard in parameterChanged itself
        //    (sizes[idx] != fftSize) prevents spurious reallocation.
        apvts.replaceState (tree);

        isRestoringState = false;

        return;
    }

    // -------------------------------------------------------------------------
    // Legacy binary format (v1 or v2) — no APVTS tree present
    // -------------------------------------------------------------------------
    loadCurvesFromBinary (data, sizeBytes);

    // Sync APVTS parameters to match the restored curve/size state so the
    // host and editor show the correct values.
    syncAPVTSFromState();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralLatencyAudioProcessor();
}