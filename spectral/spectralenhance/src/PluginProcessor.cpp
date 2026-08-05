#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralEnhanceAudioProcessor::SpectralEnhanceAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters",
      {
          std::make_unique<juce::AudioParameterFloat>("lowerFreq",  "Lower Frequency",      0.0f, 1.0f, 0.0f),
          std::make_unique<juce::AudioParameterFloat>("upperFreq",  "Upper Frequency",       0.0f, 1.0f, 1.0f),
          std::make_unique<juce::AudioParameterFloat>("magnitude",  "Magnitude Threshold",   0.0f, 1.0f, 0.3f),
          std::make_unique<juce::AudioParameterFloat>("slope",      "Threshold Slope",       0.0f, 1.0f, 0.5f),
          std::make_unique<juce::AudioParameterFloat>("attenuate",  "Attenuate Mode",        0.0f, 1.0f, 0.0f)
      })
{
    lowerFreqParameter  = parameters.getRawParameterValue("lowerFreq");
    upperFreqParameter  = parameters.getRawParameterValue("upperFreq");
    magnitudeParameter  = parameters.getRawParameterValue("magnitude");
    slopeParameter      = parameters.getRawParameterValue("slope");
    attenuateParameter  = parameters.getRawParameterValue("attenuate");

    fftAnalysis   = std::make_unique<juce::dsp::FFT>(fftOrder);
    fftSynthesis  = std::make_unique<juce::dsp::FFT>(fftOrder);

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

    window.resize(fftSize);
    spectrumMagnitudes.resize(fftSize / 2 + 1, 0.0f);
    smoothedSpectrumMagnitudes.resize(fftSize / 2 + 1, 0.0f);

    createWindow();
}

SpectralEnhanceAudioProcessor::~SpectralEnhanceAudioProcessor() {}

const juce::String SpectralEnhanceAudioProcessor::getName() const { return JucePlugin_Name; }

bool SpectralEnhanceAudioProcessor::acceptsMidi() const { return false; }
bool SpectralEnhanceAudioProcessor::producesMidi() const { return false; }
bool SpectralEnhanceAudioProcessor::isMidiEffect() const { return false; }
double SpectralEnhanceAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SpectralEnhanceAudioProcessor::getNumPrograms() { return 1; }
int SpectralEnhanceAudioProcessor::getCurrentProgram() { return 0; }
void SpectralEnhanceAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }
const juce::String SpectralEnhanceAudioProcessor::getProgramName(int index) { juce::ignoreUnused(index); return {}; }
void SpectralEnhanceAudioProcessor::changeProgramName(int index, const juce::String& newName) { juce::ignoreUnused(index, newName); }

void SpectralEnhanceAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);

    for (int ch = 0; ch < (int)inputFifoIndex.size(); ++ch)
    {
        inputFifoIndex[ch] = 0;
        outputWritePos[ch] = 0;
        grainCounter[ch]   = 0;

        std::fill(inputFifo[ch].begin(),    inputFifo[ch].end(),    0.0f);
        std::fill(outputAccum[ch].begin(),  outputAccum[ch].end(),  0.0f);
        std::fill(fftBuffer[ch].begin(),    fftBuffer[ch].end(),    0.0f);
    }
}

void SpectralEnhanceAudioProcessor::releaseResources() {}

bool SpectralEnhanceAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void SpectralEnhanceAudioProcessor::createWindow()
{
    for (int i = 0; i < fftSize; ++i)
    {
        float n = static_cast<float>(i) / (fftSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

void SpectralEnhanceAudioProcessor::performSpectralEnhance(int channel)
{
    // Forward FFT
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer[channel].data(), true);

    // Read parameters
    float lowerBinLog      = *lowerFreqParameter;
    float upperBinLog      = *upperFreqParameter;
    float magThreshold     = *magnitudeParameter;
    float slopeNormalized  = *slopeParameter;
    float slope            = slopeNormalized * 2.0f - 1.0f;  // -1 to +1
    bool  attenuate        = *attenuateParameter >= 0.5f;

    // Convert 0-1 log-scale positions to frequency then to bin indices
    const float nyquistFreq  = 22050.0f;
    const float minFreq      = 20.0f;
    const float logMin       = std::log10(minFreq);
    const float logMax       = std::log10(nyquistFreq);

    auto binLogToFreq = [&](float t) -> float {
        return std::pow(10.0f, logMin + t * (logMax - logMin));
    };

    const float freqPerBin = nyquistFreq / (fftSize / 2);
    int lowerBinIndex = static_cast<int>(binLogToFreq(lowerBinLog) / freqPerBin);
    int upperBinIndex = static_cast<int>(binLogToFreq(upperBinLog) / freqPerBin);
    int numBins = fftSize / 2 + 1;

    // First pass: find max magnitude for relative threshold calculation
    float maxMagnitude = 0.001f;
    for (int bin = 0; bin < numBins; ++bin)
    {
        float real = fftBuffer[channel][bin * 2];
        float imag = fftBuffer[channel][bin * 2 + 1];
        float mag  = std::sqrt(real * real + imag * imag);
        maxMagnitude = std::max(maxMagnitude, mag);
    }

    // Threshold range: -60 dB to +3 dB
    float baseThresholdDB  = magThreshold * 63.0f - 60.0f;
    const float slopeRangeDB = 30.0f;

    // Second pass: spectral enhancement
    for (int bin = 0; bin < numBins; ++bin)
    {
        float real = fftBuffer[channel][bin * 2];
        float imag = fftBuffer[channel][bin * 2 + 1];
        float magnitude = std::sqrt(real * real + imag * imag);

        // Store smoothed magnitude for visualisation (channel 0 only)
        if (channel == 0)
        {
            juce::ScopedLock lock(fftLock);
            spectrumMagnitudes[bin] = magnitude;
            const float smoothingFactor = 0.7f;
            smoothedSpectrumMagnitudes[bin] = smoothedSpectrumMagnitudes[bin] * smoothingFactor
                                            + magnitude * (1.0f - smoothingFactor);
        }

        // Only process bins inside the selected frequency window
        bool insideFreqRange = (bin >= lowerBinIndex && bin <= upperBinIndex);
        if (!insideFreqRange)
            continue;

        // Per-bin threshold with slope
        float binPosition     = static_cast<float>(bin) / (numBins - 1);
        float slopeAdjustDB   = slope * slopeRangeDB * (binPosition - 0.5f);
        float binThresholdDB  = baseThresholdDB + slopeAdjustDB;
        float thresholdMag    = maxMagnitude * std::pow(10.0f, binThresholdDB / 20.0f);

        // Guard against zero magnitude to avoid division by zero
        if (magnitude < 1e-12f)
        {
            // If threshold is positive, set to threshold magnitude with zero phase
            if (!attenuate)
            {
                fftBuffer[channel][bin * 2]     = thresholdMag;
                fftBuffer[channel][bin * 2 + 1] = 0.0f;
            }
            continue;
        }

        if (!attenuate)
        {
            // ---- BOOST MODE ----
            // Bins below the threshold are brought up to the threshold magnitude.
            // Bins already at or above the threshold are left untouched.
            if (magnitude < thresholdMag)
            {
                float scale = thresholdMag / magnitude;
                fftBuffer[channel][bin * 2]     *= scale;
                fftBuffer[channel][bin * 2 + 1] *= scale;
            }
        }
        else
        {
            // ---- ATTENUATE MODE ----
            // Bins above the threshold are brought down to the threshold magnitude.
            // Bins already at or below the threshold are left untouched.
            if (magnitude > thresholdMag)
            {
                float scale = thresholdMag / magnitude;
                fftBuffer[channel][bin * 2]     *= scale;
                fftBuffer[channel][bin * 2 + 1] *= scale;
            }
        }
    }

    // Inverse FFT
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer[channel].data());

    // Normalisation for overlap-add
    float overlapFactor = static_cast<float>(fftSize) / hopSize;
    float normalization = 2.0f / overlapFactor;

    // Overlap-add to output accumulator
    for (int i = 0; i < fftSize; ++i)
    {
        int outIndex = (outputWritePos[channel] + i) % (int)outputAccum[channel].size();
        outputAccum[channel][outIndex] += fftBuffer[channel][i] * window[i] * normalization;
    }
}

void SpectralEnhanceAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            inputFifo[channel][inputFifoIndex[channel]] = channelData[sample];
            inputFifoIndex[channel]++;

            if (inputFifoIndex[channel] >= fftSize)
            {
                for (int i = 0; i < fftSize; ++i)
                    analysisFrame[channel][i] = inputFifo[channel][i] * window[i];

                std::copy(analysisFrame[channel].begin(), analysisFrame[channel].end(),
                          fftBuffer[channel].begin());

                performSpectralEnhance(channel);

                std::copy(inputFifo[channel].begin() + hopSize,
                          inputFifo[channel].end(),
                          inputFifo[channel].begin());
                std::fill(inputFifo[channel].end() - hopSize,
                          inputFifo[channel].end(), 0.0f);

                inputFifoIndex[channel] -= hopSize;
                grainCounter[channel]    = hopSize;
            }

            float outputSample = 0.0f;
            if (outputWritePos[channel] < (int)outputAccum[channel].size())
            {
                outputSample = outputAccum[channel][outputWritePos[channel]];
                outputAccum[channel][outputWritePos[channel]] = 0.0f;
            }

            channelData[sample] = outputSample;
            outputWritePos[channel] = (outputWritePos[channel] + 1) % (int)outputAccum[channel].size();
        }
    }
}

void SpectralEnhanceAudioProcessor::getFFTData(float* fftDataOut, int numBins)
{
    juce::ScopedLock lock(fftLock);
    int binsToUse = juce::jmin(numBins, static_cast<int>(smoothedSpectrumMagnitudes.size()));
    for (int i = 0; i < binsToUse; ++i)
        fftDataOut[i] = smoothedSpectrumMagnitudes[i];
}

bool SpectralEnhanceAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralEnhanceAudioProcessor::createEditor()
{
    return new SpectralEnhanceAudioProcessorEditor(*this);
}

void SpectralEnhanceAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpectralEnhanceAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralEnhanceAudioProcessor();
}
