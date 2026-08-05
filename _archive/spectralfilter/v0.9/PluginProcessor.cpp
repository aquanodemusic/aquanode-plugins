#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralFilterAudioProcessor::SpectralFilterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Initialise both curve buffers to 0 dB (unity gain, flat response)
    filterCurveDB_write.fill(0.0f);
    filterCurveDB_read.fill(0.0f);
    linearGainCache.fill(1.0f);   // 0 dB = linear 1.0

    fftAnalysis  = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    allocateBuffers();
}

SpectralFilterAudioProcessor::~SpectralFilterAudioProcessor() {}

//==============================================================================
const juce::String SpectralFilterAudioProcessor::getName() const { return JucePlugin_Name; }
bool SpectralFilterAudioProcessor::acceptsMidi()  const { return false; }
bool SpectralFilterAudioProcessor::producesMidi() const { return false; }
bool SpectralFilterAudioProcessor::isMidiEffect() const { return false; }
double SpectralFilterAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  SpectralFilterAudioProcessor::getNumPrograms()  { return 1; }
int  SpectralFilterAudioProcessor::getCurrentProgram() { return 0; }
void SpectralFilterAudioProcessor::setCurrentProgram(int) {}
const juce::String SpectralFilterAudioProcessor::getProgramName(int) { return {}; }
void SpectralFilterAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void SpectralFilterAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    for (int ch = 0; ch < (int)inputFifoIndex.size(); ++ch)
    {
        inputFifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
        std::fill(inputFifo[ch].begin(),   inputFifo[ch].end(),   0.0f);
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(fftBuffer[ch].begin(),   fftBuffer[ch].end(),   0.0f);
    }

    // Rebuild gain cache in case sample rate changed bin spacing
    rebuildLinearGainCache();
}

void SpectralFilterAudioProcessor::releaseResources() {}

bool SpectralFilterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

//==============================================================================
void SpectralFilterAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / static_cast<float>(fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

//==============================================================================
void SpectralFilterAudioProcessor::allocateBuffers()
{
    const int numChannels = 2;

    inputFifo.resize(numChannels);
    analysisFrame.resize(numChannels);
    fftBuffer.resize(numChannels);
    outputAccum.resize(numChannels);

    inputFifoIndex.resize(numChannels, 0);
    outputWritePos.resize(numChannels, 0);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        inputFifo[ch].resize(fftSize * 2, 0.0f);
        analysisFrame[ch].resize(fftSize, 0.0f);
        fftBuffer[ch].resize(fftSize * 2, 0.0f);
        outputAccum[ch].resize(fftSize * 4, 0.0f);
    }

    window.resize(fftSize);
    smoothedSpectrumMagnitudes.resize(numBins, 0.0f);
    
    createWindow();
}

//==============================================================================
// FIX: pre-compute linear gains once so the per-bin loop only does a multiply.
// Called on the audio thread after a curve update, or in prepareToPlay.
// No lock required — linearGainCache and filterCurveDB_read are audio-thread-only.
void SpectralFilterAudioProcessor::rebuildLinearGainCache()
{
    for (int bin = 0; bin < numBins; ++bin)
    {
        float dB = filterCurveDB_read[bin];
        if (dB <= -144.0f)
            linearGainCache[bin] = 0.0f;
        else
            linearGainCache[bin] = std::pow(10.0f, dB / 20.0f);
    }
}

//==============================================================================
// Message-thread API
//==============================================================================

void SpectralFilterAudioProcessor::setFilterCurveRange(int startBin, int endBin,
                                                      float startDB, float endDB)
{
    // Normalise direction
    if (startBin > endBin) { std::swap(startBin, endBin); std::swap(startDB, endDB); }
    startBin = juce::jlimit(0, numBins - 1, startBin);
    endBin   = juce::jlimit(0, numBins - 1, endBin);

    {
        juce::ScopedLock lock(filterCurveLock);

        int span = endBin - startBin;
        if (span == 0)
        {
            filterCurveDB_write[startBin] = startDB;
        }
        else
        {
            for (int bin = startBin; bin <= endBin; ++bin)
            {
                float t = static_cast<float>(bin - startBin) / static_cast<float>(span);
                filterCurveDB_write[bin] = startDB + t * (endDB - startDB);
            }
        }
    }

    curveIsDirty.store(true, std::memory_order_release);
}

void SpectralFilterAudioProcessor::resetFilterCurve()
{
    {
        juce::ScopedLock lock(filterCurveLock);
        filterCurveDB_write.fill(0.0f);
    }
    curveIsDirty.store(true, std::memory_order_release);
}

void SpectralFilterAudioProcessor::setFFTSize(int newSize)
{
    // Validate size
    if (newSize != 1024 && newSize != 2048 && newSize != 4096 && newSize != 8192)
        return;
    
    // Calculate new order
    int newOrder = 0;
    if (newSize == 1024) newOrder = 10;
    else if (newSize == 2048) newOrder = 11;
    else if (newSize == 4096) newOrder = 12;
    else if (newSize == 8192) newOrder = 13;
    
    // CRITICAL: Suspend audio processing while we reallocate buffers
    // This prevents the audio thread from accessing buffers mid-reallocation
    suspendProcessing(true);
    
    // Update parameters
    fftOrder = newOrder;
    fftSize = newSize;
    hopSize = fftSize / 4;
    numBins = fftSize / 2 + 1;
    
    // Recreate FFT objects
    fftAnalysis  = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);
    
    // Reallocate all buffers
    allocateBuffers();
    
    // Reset filter curve
    {
        juce::ScopedLock lock(filterCurveLock);
        filterCurveDB_write.fill(0.0f);
    }
    curveIsDirty.store(true, std::memory_order_release);
    
    // Clear processing state
    for (int ch = 0; ch < (int)inputFifoIndex.size(); ++ch)
    {
        inputFifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
        std::fill(inputFifo[ch].begin(),   inputFifo[ch].end(),   0.0f);
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(fftBuffer[ch].begin(),   fftBuffer[ch].end(),   0.0f);
    }
    
    rebuildLinearGainCache();
    
    // Resume audio processing
    suspendProcessing(false);
}

void SpectralFilterAudioProcessor::setBackgroundColor(juce::Colour color)
{
    backgroundColor = color;
}

void SpectralFilterAudioProcessor::setCurveColor(juce::Colour color)
{
    curveColor = color;
}

void SpectralFilterAudioProcessor::setGridColor(juce::Colour color)
{
    gridColor = color;
}

void SpectralFilterAudioProcessor::setSpectrumColor(juce::Colour color)
{
    spectrumColor = color;
}

void SpectralFilterAudioProcessor::randomizeFilterCurve()
{
    juce::Random random;
    
    {
        juce::ScopedLock lock(filterCurveLock);
        
        // Generate random curve with some smoothness
        for (int bin = 0; bin < numBins; bin += 8)  // Step by 8 for smoother curves
        {
            float randomDB = random.nextFloat() * 48.0f - 24.0f;  // Range: -24 to +24 dB
            
            // Interpolate between control points
            int nextBin = juce::jmin(bin + 8, numBins - 1);
            float nextRandomDB = random.nextFloat() * 48.0f - 24.0f;
            
            for (int i = bin; i < nextBin && i < numBins; ++i)
            {
                float t = static_cast<float>(i - bin) / 8.0f;
                filterCurveDB_write[i] = randomDB + t * (nextRandomDB - randomDB);
            }
        }
    }
    
    curveIsDirty.store(true, std::memory_order_release);
}

void SpectralFilterAudioProcessor::getFilterCurve(std::array<float, maxBins>& dest)
{
    juce::ScopedLock lock(filterCurveLock);
    // Copy only the active bins (numBins may be less than maxBins)
    for (int i = 0; i < numBins; ++i)
        dest[i] = filterCurveDB_write[i];
}

//==============================================================================
void SpectralFilterAudioProcessor::performSpectralFilter(int channel)
{
    // ---- Check if the curve has been updated since last frame ----
    // FIX: we acquire the lock only here (once per FFT frame, not per bin).
    if (curveIsDirty.load(std::memory_order_acquire))
    {
        {
            juce::ScopedLock lock(filterCurveLock);
            filterCurveDB_read = filterCurveDB_write;
        }
        curveIsDirty.store(false, std::memory_order_release);
        rebuildLinearGainCache();  // recompute the linear gain array (no lock needed)
    }

    // ---- Forward FFT ----
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer[channel].data(), true);

    // ---- Per-bin gain application ----
    // FIX: smoothed magnitudes computed in a local array first, then copied
    // under the display lock in one shot — no per-bin lock acquisition.
    std::array<float, maxBins> localMagnitudes;

    for (int bin = 0; bin < numBins; ++bin)
    {
        float real = fftBuffer[channel][bin * 2];
        float imag = fftBuffer[channel][bin * 2 + 1];

        if (channel == 0)
            localMagnitudes[bin] = std::sqrt(real * real + imag * imag);

        // FIX: just multiply — no std::pow() here, no branching per bin
        float gain = linearGainCache[bin];
        fftBuffer[channel][bin * 2]     = real * gain;
        fftBuffer[channel][bin * 2 + 1] = imag * gain;
    }

    // Update display magnitudes for channel 0 — single lock, single bulk copy
    if (channel == 0)
    {
        juce::ScopedLock lock(fftDisplayLock);
        const float smooth = 0.7f;
        for (int bin = 0; bin < numBins; ++bin)
        {
            smoothedSpectrumMagnitudes[bin] =
                smoothedSpectrumMagnitudes[bin] * smooth
                + localMagnitudes[bin] * (1.0f - smooth);
        }
    }

    // ---- Inverse FFT ----
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer[channel].data());

    // ---- Overlap-add ----
    const float overlapFactor  = static_cast<float>(fftSize) / static_cast<float>(hopSize);
    const float normalization  = 2.0f / overlapFactor;
    const int   accumSize      = static_cast<int>(outputAccum[channel].size());

    for (int i = 0; i < fftSize; ++i)
    {
        int outIndex = (outputWritePos[channel] + i) % accumSize;
        outputAccum[channel][outIndex] += fftBuffer[channel][i] * window[i] * normalization;
    }
}

//==============================================================================
void SpectralFilterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    for (int channel = 0; channel < totalIn; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        const int accumSize = static_cast<int>(outputAccum[channel].size());
        // FIX: fifo capacity is fftSize*2, so the guard is that size
        const int fifoCapacity = static_cast<int>(inputFifo[channel].size());

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            // FIX: bounds-guard — should never trigger with a 2x FIFO but
            // prevents a crash if somehow the host sends an oversized block
            if (inputFifoIndex[channel] < fifoCapacity)
                inputFifo[channel][inputFifoIndex[channel]] = channelData[sample];

            inputFifoIndex[channel]++;

            // When we have accumulated fftSize new samples, run an FFT frame
            if (inputFifoIndex[channel] >= fftSize)
            {
                // Apply analysis window
                for (int i = 0; i < fftSize; ++i)
                    analysisFrame[channel][i] = inputFifo[channel][i] * window[i];

                std::copy(analysisFrame[channel].begin(), analysisFrame[channel].end(),
                          fftBuffer[channel].begin());

                performSpectralFilter(channel);

                // Shift FIFO left by hopSize (keep the overlap)
                std::copy(inputFifo[channel].begin() + hopSize,
                          inputFifo[channel].begin() + fftSize,
                          inputFifo[channel].begin());
                std::fill(inputFifo[channel].begin() + (fftSize - hopSize),
                          inputFifo[channel].begin() + fftSize,
                          0.0f);

                inputFifoIndex[channel] -= hopSize;
            }

            // Read one output sample from the overlap-add accumulator
            float outputSample = outputAccum[channel][outputWritePos[channel]];
            outputAccum[channel][outputWritePos[channel]] = 0.0f;

            channelData[sample] = outputSample;
            outputWritePos[channel] = (outputWritePos[channel] + 1) % accumSize;
        }
    }
}

//==============================================================================
void SpectralFilterAudioProcessor::getFFTData(float* fftDataOut, int numBinsOut)
{
    juce::ScopedLock lock(fftDisplayLock);
    int binsToUse = juce::jmin(numBinsOut, (int)smoothedSpectrumMagnitudes.size());
    for (int i = 0; i < binsToUse; ++i)
        fftDataOut[i] = smoothedSpectrumMagnitudes[i];
}

//==============================================================================
bool SpectralFilterAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralFilterAudioProcessor::createEditor()
{
    return new SpectralFilterAudioProcessorEditor(*this);
}

//==============================================================================
void SpectralFilterAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ScopedLock lock(filterCurveLock);
    
    // Save: [fftSize(int)] [bgColor(uint32)] [curveColor(uint32)] [gridColor(uint32)] [spectrumColor(uint32)] [numBins worth of curve data]
    size_t totalSize = sizeof(int) + 4 * sizeof(juce::uint32) + numBins * sizeof(float);
    destData.ensureSize(totalSize);
    
    char* ptr = static_cast<char*>(destData.getData());
    
    // Write FFT size
    std::memcpy(ptr, &fftSize, sizeof(int));
    ptr += sizeof(int);
    
    // Write colors as ARGB uint32
    juce::uint32 bgColorARGB = backgroundColor.getARGB();
    juce::uint32 curveColorARGB = curveColor.getARGB();
    juce::uint32 gridColorARGB = gridColor.getARGB();
    juce::uint32 spectrumColorARGB = spectrumColor.getARGB();
    std::memcpy(ptr, &bgColorARGB, sizeof(juce::uint32));
    ptr += sizeof(juce::uint32);
    std::memcpy(ptr, &curveColorARGB, sizeof(juce::uint32));
    ptr += sizeof(juce::uint32);
    std::memcpy(ptr, &gridColorARGB, sizeof(juce::uint32));
    ptr += sizeof(juce::uint32);
    std::memcpy(ptr, &spectrumColorARGB, sizeof(juce::uint32));
    ptr += sizeof(juce::uint32);
    
    // Write curve data (only numBins, not the full maxBins array)
    std::memcpy(ptr, filterCurveDB_write.data(), numBins * sizeof(float));
}

void SpectralFilterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Minimum valid state: sizeof(int) for fftSize
    if (sizeInBytes < static_cast<int>(sizeof(int)))
        return;
    
    const char* ptr = static_cast<const char*>(data);
    
    // Read FFT size
    int savedFFTSize;
    std::memcpy(&savedFFTSize, ptr, sizeof(int));
    ptr += sizeof(int);
    
    // Check which format we have
    int remainingBytes = sizeInBytes - sizeof(int);
    int savedNumBins = savedFFTSize / 2 + 1;
    int expectedCurveBytesOld = savedNumBins * sizeof(float);
    int expectedCurveBytesWith2Colors = 2 * sizeof(juce::uint32) + savedNumBins * sizeof(float);
    int expectedCurveBytesWith3Colors = 3 * sizeof(juce::uint32) + savedNumBins * sizeof(float);
    int expectedCurveBytesWith4Colors = 4 * sizeof(juce::uint32) + savedNumBins * sizeof(float);
    
    bool has4Colors = (remainingBytes == expectedCurveBytesWith4Colors);
    bool has3Colors = (remainingBytes == expectedCurveBytesWith3Colors);
    bool has2Colors = (remainingBytes == expectedCurveBytesWith2Colors);
    
    // Load colors if present
    if (has4Colors)
    {
        juce::uint32 bgColorARGB, curveColorARGB, gridColorARGB, spectrumColorARGB;
        std::memcpy(&bgColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        std::memcpy(&curveColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        std::memcpy(&gridColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        std::memcpy(&spectrumColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        
        backgroundColor = juce::Colour(bgColorARGB);
        curveColor = juce::Colour(curveColorARGB);
        gridColor = juce::Colour(gridColorARGB);
        spectrumColor = juce::Colour(spectrumColorARGB);
    }
    else if (has3Colors)
    {
        juce::uint32 bgColorARGB, curveColorARGB, gridColorARGB;
        std::memcpy(&bgColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        std::memcpy(&curveColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        std::memcpy(&gridColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        
        backgroundColor = juce::Colour(bgColorARGB);
        curveColor = juce::Colour(curveColorARGB);
        gridColor = juce::Colour(gridColorARGB);
        // Keep default spectrum color
    }
    else if (has2Colors)
    {
        juce::uint32 bgColorARGB, curveColorARGB;
        std::memcpy(&bgColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        std::memcpy(&curveColorARGB, ptr, sizeof(juce::uint32));
        ptr += sizeof(juce::uint32);
        
        backgroundColor = juce::Colour(bgColorARGB);
        curveColor = juce::Colour(curveColorARGB);
        // Keep default grid and spectrum colors
    }
    else if (remainingBytes != expectedCurveBytesOld)
    {
        // Invalid data size
        return;
    }
    
    // Set the FFT size (this will reset the curve and reallocate)
    setFFTSize(savedFFTSize);
    
    // Now load the curve data
    {
        juce::ScopedLock lock(filterCurveLock);
        std::memcpy(filterCurveDB_write.data(), ptr, savedNumBins * sizeof(float));
    }
    curveIsDirty.store(true, std::memory_order_release);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralFilterAudioProcessor();
}
