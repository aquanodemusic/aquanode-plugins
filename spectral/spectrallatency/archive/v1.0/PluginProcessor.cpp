#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpectralLatencyAudioProcessor::SpectralLatencyAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    delayCurve_write.fill(0.0f);
    delayCurve_read.fill(0.0f);

    fftProcessor = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();
}

SpectralLatencyAudioProcessor::~SpectralLatencyAudioProcessor() {}

//==============================================================================
const juce::String SpectralLatencyAudioProcessor::getName()          const { return JucePlugin_Name; }
bool  SpectralLatencyAudioProcessor::acceptsMidi()                   const { return false; }
bool  SpectralLatencyAudioProcessor::producesMidi()                  const { return false; }
bool  SpectralLatencyAudioProcessor::isMidiEffect()                  const { return false; }
double SpectralLatencyAudioProcessor::getTailLengthSeconds()         const { return 0.0; }
int   SpectralLatencyAudioProcessor::getNumPrograms() { return 1; }
int   SpectralLatencyAudioProcessor::getCurrentProgram() { return 0; }
void  SpectralLatencyAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralLatencyAudioProcessor::getProgramName(int) { return {}; }
void  SpectralLatencyAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void SpectralLatencyAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    fifoWritePos = 0;

    inputFifoL.assign(fftSize * 2, 0.0f);
    inputFifoR.assign(fftSize * 2, 0.0f);
    outputAccumL.assign(fftSize * 2, 0.0f);
    outputAccumR.assign(fftSize * 2, 0.0f);
    accumReadPosL = 0;
    accumReadPosR = 0;

    // Clear delay buffers
    for (auto& bd : binDelays)
    {
        bd.bufferL.clear();
        bd.bufferR.clear();
        bd.currentDelaySamples = 0;
    }

    {
        juce::ScopedLock lock(displayLock);
        smoothedSpectrum.assign(maxBins, 0.0f);
    }

    updateLatencyCompensation();
}

void SpectralLatencyAudioProcessor::releaseResources() {}

bool SpectralLatencyAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Must be stereo in + stereo out
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    return true;
}

//==============================================================================
void SpectralLatencyAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }

    // Normalization for Hann^2 with 75% overlap (4x)
    float overlaps = static_cast<float>(fftSize) / static_cast<float>(hopSize);
    windowNormFactor = 1.0f / (0.375f * overlaps);
}

void SpectralLatencyAudioProcessor::allocateBuffers()
{
    inputFifoL.assign(fftSize * 2, 0.0f);
    inputFifoR.assign(fftSize * 2, 0.0f);
    fftBufL.assign(fftSize * 2, 0.0f);
    fftBufR.assign(fftSize * 2, 0.0f);
    outputAccumL.assign(fftSize * 2, 0.0f);
    outputAccumR.assign(fftSize * 2, 0.0f);

    window.resize(fftSize);
    accumReadPosL = 0;
    accumReadPosR = 0;
    fifoWritePos = 0;

    smoothedSpectrum.assign(maxBins, 0.0f);

    // Allocate delay buffers for each bin
    // Maximum possible delay = 10 seconds at typical sample rate
    // In frames = (maxLatency * sampleRate) / hopSize
    // Let's allocate generously: 10s * 48000Hz / (fftSize/4) = conservative upper bound
    maxDelayFrames = static_cast<int>((10.0 * 48000.0) / hopSize) + 10;

    binDelays.clear();
    binDelays.resize(maxBins);
    for (auto& bd : binDelays)
    {
        bd.bufferL.clear();
        bd.bufferR.clear();
        bd.currentDelaySamples = 0;
    }

    createWindow();
}

//==============================================================================
// Delay curve — message-thread API
//==============================================================================

void SpectralLatencyAudioProcessor::setDelayCurveRange(int startBin, int endBin,
    float startVal, float endVal)
{
    if (startBin > endBin) { std::swap(startBin, endBin); std::swap(startVal, endVal); }
    startBin = juce::jlimit(0, numBins - 1, startBin);
    endBin = juce::jlimit(0, numBins - 1, endBin);

    {
        juce::ScopedLock lock(curveLock);
        int span = endBin - startBin;

        if (span == 0)
        {
            delayCurve_write[startBin] = juce::jlimit(-1.0f, 1.0f, startVal);
        }
        else
        {
            for (int bin = startBin; bin <= endBin; ++bin)
            {
                float t = static_cast<float> (bin - startBin) / static_cast<float> (span);
                float v = startVal + t * (endVal - startVal);
                delayCurve_write[bin] = juce::jlimit(-1.0f, 1.0f, v);
            }
        }
    }

    curveIsDirty.store(true, std::memory_order_release);
    updateLatencyCompensation();
}

void SpectralLatencyAudioProcessor::resetDelayCurve()
{
    {
        juce::ScopedLock lock(curveLock);
        delayCurve_write.fill(0.0f);
    }
    curveIsDirty.store(true, std::memory_order_release);
    updateLatencyCompensation();
}

void SpectralLatencyAudioProcessor::randomize()
{
    auto& r = juce::Random::getSystemRandom();

    {
        juce::ScopedLock lock(curveLock);
        // Randomize the curve for all active bins
        for (int i = 0; i < numBins; ++i)
            delayCurve_write[i] = r.nextFloat() * 2.0f - 1.0f; // Range: -1.0 to 1.0
        // Randomize the global max latency (between 10ms and 10s)
        maxLatencySeconds = 0.01f + r.nextFloat() * 9.99f;
    }
    // Notify the audio thread and the DAW about latency changes
    curveIsDirty.store(true, std::memory_order_release);
    updateLatencyCompensation();
}

void SpectralLatencyAudioProcessor::getDelayCurve(std::array<float, maxBins>& dest)
{
    juce::ScopedLock lock(curveLock);
    dest = delayCurve_write;
}

//==============================================================================
// Spectrum display — message-thread API
//==============================================================================

void SpectralLatencyAudioProcessor::getSpectrumData(float* out, int numBinsOut)
{
    juce::ScopedLock lock(displayLock);
    int n = juce::jmin(numBinsOut, (int)smoothedSpectrum.size());
    for (int i = 0; i < n; ++i)
        out[i] = smoothedSpectrum[i];
}

//==============================================================================
// FFT size and latency control
//==============================================================================

void SpectralLatencyAudioProcessor::setFFTSize(int newSize)
{
    if (newSize != 32 && newSize != 128 && newSize != 256 && newSize != 512 &&
        newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;

    int newOrder = 11;
    if (newSize == 32)   newOrder = 5;
    else if (newSize == 128)  newOrder = 7;
    else if (newSize == 256)  newOrder = 8;
    else if (newSize == 512)  newOrder = 9;
    else if (newSize == 1024) newOrder = 10;
    else if (newSize == 2048) newOrder = 11;
    else if (newSize == 4096) newOrder = 12;
    else if (newSize == 8192) newOrder = 13;

    suspendProcessing(true);

    // -----------------------------------------------------------------------
    // Save the current size's state unless we're mid-restore (setStateInformation
    // has already populated allSizeStates with the correct saved data).
    // -----------------------------------------------------------------------
    if (!isRestoringState)
    {
        juce::ScopedLock lock(curveLock);
        auto& curr = allSizeStates[fftSize];
        curr.curve = delayCurve_write;
        curr.maxLatency = maxLatencySeconds;
    }

    fftOrder = newOrder;
    fftSize = newSize;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;

    fftProcessor = std::make_unique<juce::dsp::FFT>(fftOrder);
    allocateBuffers();

    // -----------------------------------------------------------------------
    // Load saved state for the new size (or zeroed defaults if first visit).
    // -----------------------------------------------------------------------
    {
        juce::ScopedLock lock(curveLock);
        auto it = allSizeStates.find(newSize);
        if (it != allSizeStates.end())
        {
            delayCurve_write = it->second.curve;
            maxLatencySeconds = it->second.maxLatency;
        }
        else
        {
            delayCurve_write.fill(0.0f);
            // maxLatencySeconds keeps its previous value on first visit
        }
    }
    curveIsDirty.store(true, std::memory_order_release);

    updateLatencyCompensation();
    suspendProcessing(false);
}

void SpectralLatencyAudioProcessor::setMaxLatency(float seconds)
{
    maxLatencySeconds = juce::jlimit(0.01f, 10.0f, seconds);
    updateLatencyCompensation();
}

//==============================================================================

int SpectralLatencyAudioProcessor::calculateRequiredLatencyFrames() const
{
    // Find minimum curve value (most negative = earliest)
    float minCurveValue = 0.0f;
    {
        juce::ScopedLock lock(curveLock);
        for (int i = 0; i < numBins; ++i)
            minCurveValue = juce::jmin(minCurveValue, delayCurve_write[i]);
    }

    // If we have negative delays, we need global compensation
    if (minCurveValue < 0.0f)
    {
        // Convert to frames (each frame = hopSize samples)
        float latencySeconds = -minCurveValue * maxLatencySeconds;
        int latencyFrames = static_cast<int>(latencySeconds * currentSampleRate / hopSize);
        return latencyFrames;
    }

    return 0;  // No additional latency needed
}

void SpectralLatencyAudioProcessor::updateLatencyCompensation()
{
    int requiredFrames = calculateRequiredLatencyFrames();
    int latencySamples = requiredFrames * hopSize + (fftSize - hopSize);  // Include inherent FFT latency

    reportedLatencySamples.store(latencySamples, std::memory_order_release);
    setLatencySamples(latencySamples);
}

//==============================================================================
// Core DSP — called every hopSize samples from processBlock
//==============================================================================

void SpectralLatencyAudioProcessor::processFFTFrame()
{
    // Swap in the latest delay curve if the message thread updated it
    if (curveIsDirty.load(std::memory_order_acquire))
    {
        {
            juce::ScopedLock lock(curveLock);
            delayCurve_read = delayCurve_write;
        }
        curveIsDirty.store(false, std::memory_order_release);
    }

    // Calculate global delay offset in frames
    int globalDelayFrames = calculateRequiredLatencyFrames();

    // ------- Forward FFT for both channels -------
    for (auto* buf : { &fftBufL, &fftBufR })
    {
        std::fill(buf->begin(), buf->end(), 0.0f);
    }

    for (int i = 0; i < fftSize; ++i)
    {
        fftBufL[i] = inputFifoL[i] * window[i];
        fftBufR[i] = inputFifoR[i] * window[i];
    }

    fftProcessor->performRealOnlyForwardTransform(fftBufL.data(), true);
    fftProcessor->performRealOnlyForwardTransform(fftBufR.data(), true);

    // ------- Per-bin delay processing + display magnitude collection -------
    std::array<float, maxBins> localMag{};

    for (int bin = 0; bin < numBins; ++bin)
    {
        // Current complex values
        std::complex<float> complexL(fftBufL[bin * 2], fftBufL[bin * 2 + 1]);
        std::complex<float> complexR(fftBufR[bin * 2], fftBufR[bin * 2 + 1]);

        // Calculate delay for this bin in frames
        float delayNorm = delayCurve_read[bin];  // -1.0 to +1.0
        float delaySeconds = delayNorm * maxLatencySeconds;
        int delayFrames = static_cast<int>(delaySeconds * currentSampleRate / hopSize);

        // Apply global offset so negative delays can be realized
        int totalDelayFrames = delayFrames + globalDelayFrames;
        totalDelayFrames = juce::jmax(0, totalDelayFrames);  // Clamp to non-negative

        auto& bd = binDelays[bin];

        // Push current values to delay buffers
        bd.bufferL.push_back(complexL);
        bd.bufferR.push_back(complexR);

        // Maintain buffer size = required delay + 1
        int requiredSize = totalDelayFrames + 1;
        while ((int)bd.bufferL.size() > requiredSize && !bd.bufferL.empty())
        {
            bd.bufferL.pop_front();
            bd.bufferR.pop_front();
        }

        // Get delayed values (or current if not enough samples yet)
        std::complex<float> delayedL = complexL;
        std::complex<float> delayedR = complexR;

        if ((int)bd.bufferL.size() > totalDelayFrames)
        {
            delayedL = bd.bufferL[bd.bufferL.size() - totalDelayFrames - 1];
            delayedR = bd.bufferR[bd.bufferR.size() - totalDelayFrames - 1];
        }

        // Write back delayed values
        fftBufL[bin * 2] = delayedL.real();
        fftBufL[bin * 2 + 1] = delayedL.imag();
        fftBufR[bin * 2] = delayedR.real();
        fftBufR[bin * 2 + 1] = delayedR.imag();

        // Store magnitude for display
        localMag[bin] = std::abs(complexL);
    }

    // Update display buffers
    {
        juce::ScopedLock lock(displayLock);
        const float smooth = 0.7f;
        for (int bin = 0; bin < numBins; ++bin)
            smoothedSpectrum[bin] = smoothedSpectrum[bin] * smooth + localMag[bin] * (1.0f - smooth);
    }

    // ------- Inverse FFT + Overlap-Add -------
    fftProcessor->performRealOnlyInverseTransform(fftBufL.data());
    fftProcessor->performRealOnlyInverseTransform(fftBufR.data());

    const int accumSize = (int)outputAccumL.size();
    for (int i = 0; i < fftSize; ++i)
    {
        int posL = (accumReadPosL + i) % accumSize;
        int posR = (accumReadPosR + i) % accumSize;
        outputAccumL[posL] += fftBufL[i] * window[i] * windowNormFactor;
        outputAccumR[posR] += fftBufR[i] * window[i] * windowNormFactor;
    }
}

//==============================================================================
void SpectralLatencyAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalIn = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    const float* inL = buffer.getReadPointer(0);
    const float* inR = (totalIn > 1) ? buffer.getReadPointer(1) : inL;

    float* outL = buffer.getWritePointer(0);
    float* outR = (totalOut > 1) ? buffer.getWritePointer(1) : outL;

    for (int s = 0; s < numSamples; ++s)
    {
        inputFifoL[fifoWritePos] = inL[s];
        inputFifoR[fifoWritePos] = inR[s];
        fifoWritePos++;

        if (fifoWritePos >= fftSize)
        {
            processFFTFrame();

            // Shift FIFO by hopSize
            std::memmove(inputFifoL.data(),
                inputFifoL.data() + hopSize,
                (fftSize - hopSize) * sizeof(float));
            std::memmove(inputFifoR.data(),
                inputFifoR.data() + hopSize,
                (fftSize - hopSize) * sizeof(float));

            fifoWritePos -= hopSize;
        }

        // Read from accumulator
        float outSampleL = outputAccumL[accumReadPosL];
        float outSampleR = outputAccumR[accumReadPosR];

        outputAccumL[accumReadPosL] = 0.0f;
        outputAccumR[accumReadPosR] = 0.0f;

        accumReadPosL = (accumReadPosL + 1) % (int)outputAccumL.size();
        accumReadPosR = (accumReadPosR + 1) % (int)outputAccumR.size();

        outL[s] = outSampleL;
        outR[s] = outSampleR;
    }

    // Clear any extra output channels
    for (int ch = 2; ch < totalOut; ++ch)
        buffer.clear(ch, 0, numSamples);
}

//==============================================================================
// Editor
//==============================================================================

bool SpectralLatencyAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralLatencyAudioProcessor::createEditor()
{
    return new SpectralLatencyAudioProcessorEditor(*this);
}

//==============================================================================
// State persistence
//==============================================================================
//
// Binary format (v2):
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
// Legacy v1 format (no magic, just [int fftSize][float maxLatency][curve]):
//   detected because the second int32 (which v2 uses as activeFFTSize)
//   is a valid FFT size (32/128/256/512/1024/2048/4096/8192) and the
//   third int32 (numEntries) is outside 1–8, which can't happen in v2.
//==============================================================================

static constexpr juce::int32 kStateMagic = 0x53504C32;  // 'SPL2'

static bool isValidFFTSize(int s)
{
    return s == 32 || s == 128 || s == 256 || s == 512 ||
        s == 1024 || s == 2048 || s == 4096 || s == 8192;
}

void SpectralLatencyAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Flush the current size into allSizeStates so it is included in the save.
    {
        juce::ScopedLock lock(curveLock);
        auto& curr = allSizeStates[fftSize];
        curr.curve = delayCurve_write;
        curr.maxLatency = maxLatencySeconds;
    }

    // Calculate total byte size needed.
    //   header : magic(4) + activeFFTSize(4) + numEntries(4)  = 12 bytes
    //   per entry : fftSize(4) + maxLatency(4) + numBins(4) + numBins*4
    int numEntries = (int)allSizeStates.size();
    size_t totalSize = 12;
    for (auto& kv : allSizeStates)
    {
        int bins = kv.first / 2 + 1;
        totalSize += 4 + 4 + 4 + (size_t)bins * 4;
    }

    destData.ensureSize(totalSize);
    char* ptr = static_cast<char*> (destData.getData());

    // Magic
    juce::int32 magic = kStateMagic;
    std::memcpy(ptr, &magic, 4);          ptr += 4;

    // Active FFT size
    std::memcpy(ptr, &fftSize, 4);        ptr += 4;

    // Entry count
    std::memcpy(ptr, &numEntries, 4);     ptr += 4;

    // Entries
    for (auto& kv : allSizeStates)
    {
        int   sz = kv.first;
        float lat = kv.second.maxLatency;
        int   bins = sz / 2 + 1;

        std::memcpy(ptr, &sz, 4);                                     ptr += 4;
        std::memcpy(ptr, &lat, 4);                                     ptr += 4;
        std::memcpy(ptr, &bins, 4);                                    ptr += 4;
        std::memcpy(ptr, kv.second.curve.data(), (size_t)bins * 4);  ptr += bins * 4;
    }
}

void SpectralLatencyAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (sizeInBytes < 12)
        return;

    const char* ptr = static_cast<const char*>(data);
    const char* end = ptr + sizeInBytes;

    // -----------------------------------------------------------------------
    // Detect format: v2 starts with magic 0x53504C32; v1 starts with fftSize.
    // -----------------------------------------------------------------------
    juce::int32 firstWord;
    std::memcpy(&firstWord, ptr, 4);

    if (firstWord == kStateMagic)
    {
        // ===== v2 format =====
        ptr += 4;   // skip magic

        int savedActiveSize;
        std::memcpy(&savedActiveSize, ptr, 4);  ptr += 4;

        int numEntries;
        std::memcpy(&numEntries, ptr, 4);       ptr += 4;

        if (numEntries < 1 || numEntries > 8)
            return;

        allSizeStates.clear();

        for (int i = 0; i < numEntries; ++i)
        {
            if (ptr + 12 > end) break;

            int   sz;   std::memcpy(&sz, ptr, 4);  ptr += 4;
            float lat;  std::memcpy(&lat, ptr, 4);  ptr += 4;
            int   bins; std::memcpy(&bins, ptr, 4);  ptr += 4;

            if (bins < 1 || bins > maxBins)     break;
            if (ptr + bins * 4 > end)           break;
            if (!isValidFFTSize(sz))           break;

            PerSizeState state;
            state.maxLatency = juce::jlimit(0.01f, 10.0f, lat);
            state.curve.fill(0.0f);
            std::memcpy(state.curve.data(), ptr, (size_t)bins * 4);
            ptr += bins * 4;

            allSizeStates[sz] = std::move(state);
        }

        // Restore to the saved active FFT size.
        // isRestoringState suppresses the "save current state" step inside
        // setFFTSize so the freshly loaded allSizeStates entries are not
        // overwritten by stale defaults.
        isRestoringState = true;
        setFFTSize(isValidFFTSize(savedActiveSize) ? savedActiveSize : 2048);
        isRestoringState = false;
    }
    else
    {
        // ===== v1 legacy format =====
        // Layout: [int32 fftSize][float maxLatency][numBins × float curve]
        int   savedSize;
        float savedLatency;
        std::memcpy(&savedSize, ptr, 4);
        std::memcpy(&savedLatency, ptr + 4, 4);

        if (!isValidFFTSize(savedSize)) return;

        int savedBins = savedSize / 2 + 1;
        int expectedSizeBytes = 4 + 4 + savedBins * 4;
        if (sizeInBytes < expectedSizeBytes) return;

        PerSizeState state;
        state.maxLatency = juce::jlimit(0.01f, 10.0f, savedLatency);
        state.curve.fill(0.0f);
        std::memcpy(state.curve.data(), ptr + 8, (size_t)savedBins * 4);

        allSizeStates.clear();
        allSizeStates[savedSize] = std::move(state);

        isRestoringState = true;
        setFFTSize(savedSize);
        isRestoringState = false;
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralLatencyAudioProcessor();
}