#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralGateAudioProcessor::SpectralGateAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
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

    // Get parameters
    float lowerBin = lowerFreqBin.load();
    float upperBin = upperFreqBin.load();
    float magThreshold = magnitudeThreshold.load();

    // Convert normalized magnitude threshold (0-1) to actual magnitude
    // Using dB scale: -60 dB to 0 dB
    float thresholdMagnitude = std::pow(10.0f, (magThreshold * 60.0f - 60.0f) / 20.0f);

    int numBins = fftSize / 2 + 1;
    int lowerBinIndex = static_cast<int>(lowerBin * (numBins - 1));
    int upperBinIndex = static_cast<int>(upperBin * (numBins - 1));

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

        // Apply spectral gate
        bool outsideFreqRange = (bin < lowerBinIndex || bin > upperBinIndex);
        bool belowThreshold = (magnitude < thresholdMagnitude);

        if (outsideFreqRange || belowThreshold)
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
    juce::MemoryOutputStream stream(destData, true);
    stream.writeFloat(lowerFreqBin.load());
    stream.writeFloat(upperFreqBin.load());
    stream.writeFloat(magnitudeThreshold.load());
}

void SpectralGateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream(data, static_cast<size_t>(sizeInBytes), false);
    lowerFreqBin = stream.readFloat();
    upperFreqBin = stream.readFloat();
    magnitudeThreshold = stream.readFloat();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralGateAudioProcessor();
}
