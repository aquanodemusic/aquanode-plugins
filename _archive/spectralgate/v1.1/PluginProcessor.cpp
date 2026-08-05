#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralGateAudioProcessor::SpectralGateAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters",
      {
          std::make_unique<juce::AudioParameterFloat>("lowerFreq", "Lower Frequency", 0.0f, 1.0f, 0.0f),
          std::make_unique<juce::AudioParameterFloat>("upperFreq", "Upper Frequency", 0.0f, 1.0f, 1.0f),
          std::make_unique<juce::AudioParameterFloat>("magnitude", "Magnitude Threshold", 0.0f, 1.0f, 0.0f),
          std::make_unique<juce::AudioParameterFloat>("slope", "Threshold Slope", -1.0f, 1.0f, 0.0f),
          std::make_unique<juce::AudioParameterFloat>("invert", "Invert", 0.0f, 1.0f, 0.0f)
      })
{
    // Get parameter pointers for fast access
    lowerFreqParameter = parameters.getRawParameterValue("lowerFreq");
    upperFreqParameter = parameters.getRawParameterValue("upperFreq");
    magnitudeParameter = parameters.getRawParameterValue("magnitude");
    slopeParameter = parameters.getRawParameterValue("slope");
    invertParameter = parameters.getRawParameterValue("invert");

    // Create FFT objects
    fftAnalysis = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(fftOrder);

    // Allocate for 2 channels (stereo)
    const int numChannels = 2;
    
    inputFifo.resize(numChannels);
    analysisFrame.resize(numChannels);
    fftBuffer.resize(numChannels);
    outputAccum.resize(numChannels);
    
    inputFifoIndex.resize(numChannels, 0);
    outputWritePos.resize(numChannels, 0);
    grainCounter.resize(numChannels, 0);
    
    for (int ch = 0; ch < numChannels; ++ch)
    {
        inputFifo[ch].resize(fftSize, 0.0f);
        analysisFrame[ch].resize(fftSize, 0.0f);
        fftBuffer[ch].resize(fftSize * 2, 0.0f);
        outputAccum[ch].resize(fftSize * 4, 0.0f);
    }

    // Window and spectrum buffers (shared)
    window.resize(fftSize);
    spectrumMagnitudes.resize(fftSize / 2 + 1, 0.0f);
    smoothedSpectrumMagnitudes.resize(fftSize / 2 + 1, 0.0f);

    // Create window function
    createWindow();
}

SpectralGateAudioProcessor::~SpectralGateAudioProcessor()
{
}

const juce::String SpectralGateAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpectralGateAudioProcessor::acceptsMidi() const { return false; }
bool SpectralGateAudioProcessor::producesMidi() const { return false; }
bool SpectralGateAudioProcessor::isMidiEffect() const { return false; }
double SpectralGateAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SpectralGateAudioProcessor::getNumPrograms() { return 1; }
int SpectralGateAudioProcessor::getCurrentProgram() { return 0; }
void SpectralGateAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String SpectralGateAudioProcessor::getProgramName(int index) { juce::ignoreUnused(index); return {}; }
void SpectralGateAudioProcessor::changeProgramName(int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

void SpectralGateAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);

    // Reset all channel state
    for (int ch = 0; ch < inputFifoIndex.size(); ++ch)
    {
        inputFifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
        grainCounter[ch] = 0;
        
        std::fill(inputFifo[ch].begin(), inputFifo[ch].end(), 0.0f);
        std::fill(outputAccum[ch].begin(), outputAccum[ch].end(), 0.0f);
        std::fill(fftBuffer[ch].begin(), fftBuffer[ch].end(), 0.0f);
    }
}

void SpectralGateAudioProcessor::releaseResources()
{
}

bool SpectralGateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void SpectralGateAudioProcessor::createWindow()
{
    // Standard Hann window
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / (fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void SpectralGateAudioProcessor::performSpectralGate(int channel)
{
    // Forward FFT (time domain to frequency domain)
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer[channel].data(), true);

    // Get parameters (these are now in logarithmic scale, 0-1)
    float lowerBinLog = *lowerFreqParameter;
    float upperBinLog = *upperFreqParameter;
    float magThreshold = *magnitudeParameter;
    float slopeNormalized = *slopeParameter;  // 0-1 from APVTS
    float slope = slopeNormalized * 2.0f - 1.0f;  // Convert to -1 to +1
    bool invert = *invertParameter >= 0.5f;

    // Convert logarithmic bin positions (0-1) to actual frequencies
    const float nyquistFreq = 22050.0f; // Assuming 44.1 kHz
    const float minFreq = 20.0f;
    const float logMin = std::log10(minFreq);
    const float logMax = std::log10(nyquistFreq);
    
    auto binToFreq = [&](float binLog) -> float {
        float logFreq = logMin + binLog * (logMax - logMin);
        return std::pow(10.0f, logFreq);
    };
    
    float lowerFreq = binToFreq(lowerBinLog);
    float upperFreq = binToFreq(upperBinLog);
    
    // Convert frequencies to actual FFT bin indices
    const float freqPerBin = nyquistFreq / (fftSize / 2);
    int lowerBinIndex = static_cast<int>(lowerFreq / freqPerBin);
    int upperBinIndex = static_cast<int>(upperFreq / freqPerBin);

    int numBins = fftSize / 2 + 1;

    // First pass: find max magnitude for relative threshold
    float maxMagnitude = 0.001f;
    for (int bin = 0; bin < numBins; ++bin)
    {
        float real = fftBuffer[channel][bin * 2];
        float imag = fftBuffer[channel][bin * 2 + 1];
        float magnitude = std::sqrt(real * real + imag * imag);
        maxMagnitude = std::max(maxMagnitude, magnitude);
    }

    // Base threshold in dB: -60 dB to +3 dB (63 dB range)
    float baseThresholdDB = (magThreshold * 63.0f - 60.0f);
    
    // Slope range: ±30 dB across the spectrum
    const float slopeRangeDB = 30.0f;

    // Process each frequency bin
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Extract real and imaginary components
        float real = fftBuffer[channel][bin * 2];
        float imag = fftBuffer[channel][bin * 2 + 1];

        // Calculate magnitude
        float magnitude = std::sqrt(real * real + imag * imag);

        // Store magnitude for visualization (use channel 0 for display)
        if (channel == 0)
        {
            juce::ScopedLock lock(fftLock);
            spectrumMagnitudes[bin] = magnitude;
            
            // Apply exponential smoothing for display (smoothing factor ~0.7 for nice visual decay)
            const float smoothingFactor = 0.7f;
            smoothedSpectrumMagnitudes[bin] = smoothedSpectrumMagnitudes[bin] * smoothingFactor + 
                                               magnitude * (1.0f - smoothingFactor);
        }

        // Calculate per-bin threshold based on frequency position and slope
        // Normalized position: 0.0 at DC, 1.0 at Nyquist
        float binPosition = static_cast<float>(bin) / (numBins - 1);
        
        // Apply slope: negative slope = easier for low freq, harder for high freq
        //             positive slope = harder for low freq, easier for high freq
        float slopeAdjustmentDB = slope * slopeRangeDB * (binPosition - 0.5f);
        float binThresholdDB = baseThresholdDB + slopeAdjustmentDB;
        
        // Convert to magnitude
        float thresholdMagnitude = maxMagnitude * std::pow(10.0f, binThresholdDB / 20.0f);

        // Apply spectral gate
        bool insideFreqRange = (bin >= lowerBinIndex && bin <= upperBinIndex);
        bool aboveThreshold = (magnitude >= thresholdMagnitude);

        bool shouldKeep;
        if (invert)
        {
            // Inverted mode: keep bins OUTSIDE the region that are ABOVE threshold
            shouldKeep = !insideFreqRange && aboveThreshold;
        }
        else
        {
            // Normal mode: keep bins INSIDE the region that are ABOVE threshold
            shouldKeep = insideFreqRange && aboveThreshold;
        }

        if (!shouldKeep)
        {
            // Zero out this bin
            fftBuffer[channel][bin * 2] = 0.0f;
            fftBuffer[channel][bin * 2 + 1] = 0.0f;
        }
    }

    // Inverse FFT (frequency domain to time domain)
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer[channel].data());

    // Calculate normalization factor based on overlap
    float overlapFactor = static_cast<float>(fftSize) / hopSize;
    float normalization = 2.0f / overlapFactor;

    // Overlap-add to output accumulator with windowing and normalization
    for (int i = 0; i < fftSize; ++i)
    {
        int outIndex = (outputWritePos[channel] + i) % outputAccum[channel].size();
        outputAccum[channel][outIndex] += fftBuffer[channel][i] * window[i] * normalization;
    }
}

void SpectralGateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Process each channel independently
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            // Add input sample to FIFO
            inputFifo[channel][inputFifoIndex[channel]] = channelData[sample];
            inputFifoIndex[channel]++;

            // Process FFT when we have accumulated a full window
            if (inputFifoIndex[channel] >= fftSize)
            {
                // Copy input FIFO to analysis frame and apply window
                for (int i = 0; i < fftSize; ++i)
                {
                    analysisFrame[channel][i] = inputFifo[channel][i] * window[i];
                }

                // Copy to FFT buffer
                std::copy(analysisFrame[channel].begin(), analysisFrame[channel].end(), 
                         fftBuffer[channel].begin());

                // Perform spectral gating
                performSpectralGate(channel);

                // Shift input FIFO by hop size to maintain overlap
                std::copy(inputFifo[channel].begin() + hopSize,
                         inputFifo[channel].end(),
                         inputFifo[channel].begin());
                std::fill(inputFifo[channel].end() - hopSize, 
                         inputFifo[channel].end(), 0.0f);

                inputFifoIndex[channel] -= hopSize;
                grainCounter[channel] = hopSize;
            }

            // Read output sample from accumulator
            float outputSample = 0.0f;
            if (outputWritePos[channel] < outputAccum[channel].size())
            {
                outputSample = outputAccum[channel][outputWritePos[channel]];
                outputAccum[channel][outputWritePos[channel]] = 0.0f;
            }

            // Write to output
            channelData[sample] = outputSample;

            // Advance output position
            outputWritePos[channel] = (outputWritePos[channel] + 1) % outputAccum[channel].size();
        }
    }
}

void SpectralGateAudioProcessor::getFFTData(float* fftDataOut, int numBins)
{
    juce::ScopedLock lock(fftLock);

    int binsToUse = juce::jmin(numBins, static_cast<int>(smoothedSpectrumMagnitudes.size()));

    for (int i = 0; i < binsToUse; ++i)
    {
        fftDataOut[i] = smoothedSpectrumMagnitudes[i];
    }
}

bool SpectralGateAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SpectralGateAudioProcessor::createEditor()
{
    return new SpectralGateAudioProcessorEditor(*this);
}

void SpectralGateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpectralGateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralGateAudioProcessor();
}
