#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralStereoizeAudioProcessor::SpectralStereoizeAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true))
{
    widthCurve_write.fill (1.0f);
    widthCurve_read.fill  (1.0f);

    fftProcessor = std::make_unique<juce::dsp::FFT> (fftOrder);
    allocateBuffers();
}

SpectralStereoizeAudioProcessor::~SpectralStereoizeAudioProcessor() {}

//==============================================================================
const juce::String SpectralStereoizeAudioProcessor::getName()          const { return JucePlugin_Name; }
bool  SpectralStereoizeAudioProcessor::acceptsMidi()                   const { return false; }
bool  SpectralStereoizeAudioProcessor::producesMidi()                  const { return false; }
bool  SpectralStereoizeAudioProcessor::isMidiEffect()                  const { return false; }
double SpectralStereoizeAudioProcessor::getTailLengthSeconds()         const { return 0.0; }
int   SpectralStereoizeAudioProcessor::getNumPrograms()                      { return 1; }
int   SpectralStereoizeAudioProcessor::getCurrentProgram()                   { return 0; }
void  SpectralStereoizeAudioProcessor::setCurrentProgram (int)               {}
const juce::String SpectralStereoizeAudioProcessor::getProgramName (int)     { return {}; }
void  SpectralStereoizeAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void SpectralStereoizeAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    fifoWritePos = 0;
    hopCounter   = 0;

    for (int ch = 0; ch < kNumCh; ++ch)
        std::fill (inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        accumReadPos[ch] = 0;
    }

    {
        juce::ScopedLock lock (displayLock);
        std::fill (smoothedMain.begin(), smoothedMain.end(), 0.0f);
        std::fill (smoothedSC.begin(),   smoothedSC.end(),   0.0f);
    }
}

void SpectralStereoizeAudioProcessor::releaseResources() {}

bool SpectralStereoizeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Main bus must be stereo in + stereo out
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo()) return false;

    // Sidechain bus, if enabled, must be mono or stereo
    auto sc = layouts.getChannelSet (true, 1);
    if (!sc.isDisabled()
        && sc != juce::AudioChannelSet::mono()
        && sc != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
void SpectralStereoizeAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }

    // Normalization for Hann^2 with 75% overlap (4x)
    // Formula: 1.0 / ( (3/8) * (fftSize / hopSize) )
    float overlaps = static_cast<float>(fftSize) / static_cast<float>(hopSize);
    windowNormFactor = 1.0f / (0.375f * overlaps);
}

void SpectralStereoizeAudioProcessor::allocateBuffers()
{
    inputFifo.assign  (kNumCh, std::vector<float> (fftSize * 2, 0.0f));
    fftBuf.assign     (kNumCh, std::vector<float> (fftSize * 2, 0.0f));
    outputAccum.assign (2,     std::vector<float> (fftSize * 2, 0.0f));

    window.resize (fftSize);
    accumReadPos.assign (2, 0);

    fifoWritePos = 0;
    hopCounter   = 0;

    smoothedMain.assign (maxBins, 0.0f);
    smoothedSC.assign   (maxBins, 0.0f);

    createWindow();
}

//==============================================================================
// Width curve — message-thread API
//==============================================================================

void SpectralStereoizeAudioProcessor::setWidthCurveRange (int startBin, int endBin,
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
            widthCurve_write[startBin] = juce::jlimit (0.0f, 2.0f, startVal);
        }
        else
        {
            for (int bin = startBin; bin <= endBin; ++bin)
            {
                float t = static_cast<float> (bin - startBin) / static_cast<float> (span);
                float v = startVal + t * (endVal - startVal);
                widthCurve_write[bin] = juce::jlimit (0.0f, 2.0f, v);
            }
        }
    }

    curveIsDirty.store (true, std::memory_order_release);
}

void SpectralStereoizeAudioProcessor::resetWidthCurve()
{
    {
        juce::ScopedLock lock (curveLock);
        widthCurve_write.fill (1.0f);
    }
    curveIsDirty.store (true, std::memory_order_release);
}

void SpectralStereoizeAudioProcessor::getWidthCurve (std::array<float, maxBins>& dest)
{
    juce::ScopedLock lock (curveLock);
    dest = widthCurve_write;
}

//==============================================================================
// Spectrum display — message-thread API
//==============================================================================

void SpectralStereoizeAudioProcessor::getMainSpectrumData (float* out, int numBinsOut)
{
    juce::ScopedLock lock (displayLock);
    int n = juce::jmin (numBinsOut, (int) smoothedMain.size());
    for (int i = 0; i < n; ++i)
        out[i] = smoothedMain[i];
}

void SpectralStereoizeAudioProcessor::getSidechainSpectrumData (float* out, int numBinsOut)
{
    juce::ScopedLock lock (displayLock);
    int n = juce::jmin (numBinsOut, (int) smoothedSC.size());
    for (int i = 0; i < n; ++i)
        out[i] = smoothedSC[i];
}

//==============================================================================
// FFT size change
//==============================================================================

void SpectralStereoizeAudioProcessor::setFFTSize(int newSize)
{
    // 1. Validate all supported sizes
    if (newSize != 32 && newSize != 128 && newSize != 256 && newSize != 512 &&
        newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;

    // 2. Map sizes to their FFT orders (2^order = size)
    int newOrder = 10;
    if (newSize == 32)   newOrder = 5;
    else if (newSize == 128)  newOrder = 7;
    else if (newSize == 256)  newOrder = 8;
    else if (newSize == 512)  newOrder = 9;
    else if (newSize == 1024) newOrder = 10;
    else if (newSize == 2048) newOrder = 11;
    else if (newSize == 4096) newOrder = 12;
    else if (newSize == 8192) newOrder = 13;

    suspendProcessing(true);

    fftOrder = newOrder;
    fftSize = newSize;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;

    fftProcessor = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();

    {
        juce::ScopedLock lock(curveLock);
        widthCurve_write.fill(1.0f);
    }
    curveIsDirty.store(true, std::memory_order_release);

    suspendProcessing(false);
}

//==============================================================================
// Core DSP — called every hopSize samples from processBlock
//==============================================================================

void SpectralStereoizeAudioProcessor::processFFTFrame()
{
    // Swap in the latest width curve if the message thread updated it
    if (curveIsDirty.load (std::memory_order_acquire))
    {
        {
            juce::ScopedLock lock (curveLock);
            widthCurve_read = widthCurve_write;
        }
        curveIsDirty.store (false, std::memory_order_release);
    }

    // ------- Forward FFT for all three internal channels -------
    for (int ch = 0; ch < kNumCh; ++ch)
    {
        // Zero buffer, then copy windowed real input into first fftSize slots
        std::fill (fftBuf[ch].begin(), fftBuf[ch].end(), 0.0f);
        for (int i = 0; i < fftSize; ++i)
            fftBuf[ch][i] = inputFifo[ch][i] * window[i];

        fftProcessor->performRealOnlyForwardTransform (fftBuf[ch].data(), true);
    }

    // ------- Per-bin processing + display magnitude collection -------
    std::array<float, maxBins> localMainMag {};
    std::array<float, maxBins> localSCMag   {};

    for (int bin = 0; bin < numBins; ++bin)
    {
        // Apply per-bin width multiplier to the Side channel (ch 1)
        float w = widthCurve_read[bin];
        fftBuf[1][bin * 2] *= w;
        fftBuf[1][bin * 2 + 1] *= w;

        // NEU: Grüne Kurve = Processed Side Magnitude (Stereo-Level am Ausgang)
        float rS = fftBuf[1][bin * 2], iS = fftBuf[1][bin * 2 + 1];
        localMainMag[bin] = std::sqrt(rS * rS + iS * iS);

        // Pinke Kurve = Sidechain Mid Magnitude (Unveränderte Referenz)
        float rSC = fftBuf[2][bin * 2], iSC = fftBuf[2][bin * 2 + 1];
        localSCMag[bin] = std::sqrt(rSC * rSC + iSC * iSC);
    }

    // Single bulk copy into display buffers under one lock acquisition
    {
        juce::ScopedLock lock (displayLock);
        const float smooth = 0.7f;
        for (int bin = 0; bin < numBins; ++bin)
        {
            smoothedMain[bin] = smoothedMain[bin] * smooth + localMainMag[bin] * (1.0f - smooth);
            smoothedSC[bin]   = smoothedSC[bin]   * smooth + localSCMag[bin]   * (1.0f - smooth);
        }
    }

    // ------- Inverse FFT + Overlap-Add for Mid (ch 0) and Side (ch 1) -------
    for (int ch = 0; ch < 2; ++ch)
    {
        fftProcessor->performRealOnlyInverseTransform (fftBuf[ch].data());

        const int accumSize = (int) outputAccum[ch].size();
        for (int i = 0; i < fftSize; ++i)
        {
            int pos = (accumReadPos[ch] + i) % accumSize;
            outputAccum[ch][pos] += fftBuf[ch][i] * window[i] * windowNormFactor;
        }
    }
}

//==============================================================================
// Stelle sicher, dass die Funktion davor (processFFTFrame) korrekt geschlossen wurde!

void SpectralStereoizeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalIn = getTotalNumInputChannels();

    const float* inL = buffer.getReadPointer(0);
    const float* inR = (totalIn > 1) ? buffer.getReadPointer(1) : inL;

    const float* scL = (totalIn > 2) ? buffer.getReadPointer(2) : nullptr;
    const float* scR = (totalIn > 3) ? buffer.getReadPointer(3) : scL;

    float* outL = buffer.getWritePointer(0);
    float* outR = (getTotalNumOutputChannels() > 1) ? buffer.getWritePointer(1) : outL;

    for (int s = 0; s < numSamples; ++s)
    {
        float mid = (inL[s] + inR[s]) * 0.5f;
        float side = (inL[s] - inR[s]) * 0.5f;

        float scMid = 0.0f;
        if (scL != nullptr)
            scMid = (scR != nullptr) ? (scL[s] + scR[s]) * 0.5f : scL[s];

        inputFifo[0][fifoWritePos] = mid;
        inputFifo[1][fifoWritePos] = side;
        inputFifo[2][fifoWritePos] = scMid;
        fifoWritePos++;

        if (fifoWritePos >= fftSize)
        {
            processFFTFrame();

            for (int ch = 0; ch < kNumCh; ++ch)
            {
                std::memmove(inputFifo[ch].data(),
                    inputFifo[ch].data() + hopSize,
                    (fftSize - hopSize) * sizeof(float));
            }
            fifoWritePos -= hopSize;
        }

        float midOut = outputAccum[0][accumReadPos[0]];
        float sideOut = outputAccum[1][accumReadPos[1]];

        outputAccum[0][accumReadPos[0]] = 0.0f;
        outputAccum[1][accumReadPos[1]] = 0.0f;

        accumReadPos[0] = (accumReadPos[0] + 1) % (int)outputAccum[0].size();
        accumReadPos[1] = (accumReadPos[1] + 1) % (int)outputAccum[1].size();

        outL[s] = midOut + sideOut;
        outR[s] = midOut - sideOut;
    }
}

//==============================================================================
// Editor
//==============================================================================

bool SpectralStereoizeAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralStereoizeAudioProcessor::createEditor()
{
    return new SpectralStereoizeAudioProcessorEditor (*this);
}

//==============================================================================
// State persistence
//==============================================================================

void SpectralStereoizeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ScopedLock lock (curveLock);

    // Format: [fftSize : int] [numBins × float width curve]
    size_t totalSize = sizeof (int) + (size_t) numBins * sizeof (float);
    destData.ensureSize (totalSize);

    char* ptr = static_cast<char*> (destData.getData());
    std::memcpy (ptr, &fftSize, sizeof (int));
    ptr += sizeof (int);
    std::memcpy (ptr, widthCurve_write.data(), (size_t) numBins * sizeof (float));
}

void SpectralStereoizeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (sizeInBytes < (int) sizeof (int))
        return;

    const char* ptr = static_cast<const char*> (data);

    int savedFFTSize;
    std::memcpy (&savedFFTSize, ptr, sizeof (int));
    ptr += sizeof (int);

    int savedNumBins      = savedFFTSize / 2 + 1;
    int expectedSizeBytes = (int) sizeof (int) + savedNumBins * (int) sizeof (float);

    if (sizeInBytes < expectedSizeBytes)
        return;

    setFFTSize (savedFFTSize);   // resets curve to 1.0 and reallocates

    {
        juce::ScopedLock lock (curveLock);
        std::memcpy (widthCurve_write.data(), ptr, (size_t) savedNumBins * sizeof (float));
    }
    curveIsDirty.store (true, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralStereoizeAudioProcessor();
}