#include "PluginProcessor.h"
#include "PluginEditor.h"

// ─────────────────────────────────────────────────────────────────────────────
SpectralGateAudioProcessor::SpectralGateAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "Parameters",
        {
            std::make_unique<juce::AudioParameterFloat>(
                "lowerFreq",      "Lower Frequency", 0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(
                "upperFreq",      "Upper Frequency", 0.0f, 1.0f, 1.0f),
            std::make_unique<juce::AudioParameterFloat>(
                "invertInterval", "Invert Interval", 0.0f, 1.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(
                "invertGate",     "Invert Gate",     0.0f, 1.0f, 0.0f)
        })
{
    lowerFreqParameter = parameters.getRawParameterValue("lowerFreq");
    upperFreqParameter = parameters.getRawParameterValue("upperFreq");
    invertIntervalParameter = parameters.getRawParameterValue("invertInterval");
    invertGateParameter = parameters.getRawParameterValue("invertGate");

    fftAnalysis = std::make_unique<juce::dsp::FFT>(currentFFTOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(currentFFTOrder);

    allocateBuffers(2);
}

SpectralGateAudioProcessor::~SpectralGateAudioProcessor() {}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer allocation
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::allocateBuffers(int numChannels)
{
    const int numBins = currentFFTSize / 2 + 1;

    inputFifo.assign(numChannels, std::vector<float>(currentFFTSize, 0.0f));
    analysisFrame.assign(numChannels, std::vector<float>(currentFFTSize, 0.0f));
    fftBuffer.assign(numChannels, std::vector<float>(currentFFTSize * 2, 0.0f));
    outputAccum.assign(numChannels, std::vector<float>(currentFFTSize * 4, 0.0f));

    inputFifoIndex.assign(numChannels, 0);
    outputWritePos.assign(numChannels, 0);
    grainCounter.assign(numChannels, 0);

    window.resize(currentFFTSize);
    createWindow();

    smoothedSpectrumMagnitudes.assign(numBins, 0.0f);

    // Preserve gateCurve if already the right size (set by setFFTOrder before call)
    if ((int)gateCurve.size() != numBins)
        gateCurve.assign(numBins, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic FFT size
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::setFFTOrder(int order)
{
    order = juce::jlimit(9, 12, order);
    if (order == currentFFTOrder) return;

    suspendProcessing(true);

    // Interpolate existing gate curve to new bin count
    const int oldNumBins = currentFFTSize / 2 + 1;
    const int newFFTSize = 1 << order;
    const int newNumBins = newFFTSize / 2 + 1;

    std::vector<float> newCurve(newNumBins, 0.0f);
    {
        juce::ScopedLock lock(fftLock);
        for (int i = 0; i < newNumBins; ++i)
        {
            float pos = (float)i * (float)(oldNumBins - 1) / (float)(newNumBins - 1);
            int   lo = (int)pos;
            int   hi = juce::jmin(lo + 1, oldNumBins - 1);
            float t = pos - (float)lo;
            newCurve[i] = gateCurve[lo] * (1.0f - t) + gateCurve[hi] * t;
        }
    }

    currentFFTOrder = order;
    currentFFTSize = newFFTSize;
    currentHopSize = currentFFTSize / 4;

    fftAnalysis = std::make_unique<juce::dsp::FFT>(currentFFTOrder);
    fftSynthesis = std::make_unique<juce::dsp::FFT>(currentFFTOrder);

    // Install new curve before allocateBuffers (which checks size)
    {
        juce::ScopedLock lock(fftLock);
        gateCurve = std::move(newCurve);
    }

    const int numChannels = juce::jmax(2, (int)inputFifo.size());
    allocateBuffers(numChannels);

    setLatencySamples(currentFFTSize);
    suspendProcessing(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// Standard overrides
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);

    const int numChannels = juce::jmax(2, getTotalNumInputChannels());
    allocateBuffers(numChannels);

    // Tell the host how many samples of latency the plugin introduces.
    // The STFT pipeline delays audio by fftSize samples (time to fill first frame).
    setLatencySamples(currentFFTSize);
}

void SpectralGateAudioProcessor::releaseResources() {}

bool SpectralGateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

// ─────────────────────────────────────────────────────────────────────────────
// Window
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::createWindow()
{
    for (int i = 0; i < currentFFTSize; ++i)
    {
        float n = (float)i / (float)(currentFFTSize - 1);
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * n));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Spectral gate core
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::performSpectralGate(int channel)
{
    fftAnalysis->performRealOnlyForwardTransform(fftBuffer[channel].data(), true);

    const float lowerBinLog = *lowerFreqParameter;
    const float upperBinLog = *upperFreqParameter;
    const bool  invertInterval = (*invertIntervalParameter >= 0.5f);
    const bool  invertGate = (*invertGateParameter >= 0.5f);

    // Log-frequency helpers (hardcoded 44.1 kHz assumption same as original)
    constexpr float kNyquist = 22050.0f;
    constexpr float kMinFreq = 20.0f;
    const float logMin = std::log10(kMinFreq);
    const float logMax = std::log10(kNyquist);

    auto logBinToFreq = [&](float b) {
        return std::pow(10.0f, logMin + b * (logMax - logMin));
        };

    const float freqPerBin = kNyquist / (float)(currentFFTSize / 2);
    const int   lowerBinIdx = (int)(logBinToFreq(lowerBinLog) / freqPerBin);
    const int   upperBinIdx = (int)(logBinToFreq(upperBinLog) / freqPerBin);
    const int   numBins = currentFFTSize / 2 + 1;

    // Pass 1: find max magnitude for relative threshold reference
    float maxMag = 0.001f;
    for (int b = 0; b < numBins; ++b)
    {
        float re = fftBuffer[channel][b * 2];
        float im = fftBuffer[channel][b * 2 + 1];
        maxMag = std::max(maxMag, std::sqrt(re * re + im * im));
    }

    // Snapshot gate curve + update display smoothing (under lock, brief)
    std::vector<float> localCurve;
    {
        juce::ScopedLock lock(fftLock);
        localCurve = gateCurve;

        if (channel == 0)
        {
            constexpr float sf = 0.7f;
            for (int b = 0; b < numBins; ++b)
            {
                float re = fftBuffer[channel][b * 2];
                float im = fftBuffer[channel][b * 2 + 1];
                float mag = std::sqrt(re * re + im * im);
                smoothedSpectrumMagnitudes[b] =
                    smoothedSpectrumMagnitudes[b] * sf + mag * (1.0f - sf);
            }
        }
    }

    // Pass 2: gate each bin
    for (int b = 0; b < numBins; ++b)
    {
        float re = fftBuffer[channel][b * 2];
        float im = fftBuffer[channel][b * 2 + 1];
        float mag = std::sqrt(re * re + im * im);

        // Per-bin threshold from gate curve:
        //   curveValue 0.0 → threshold at -60 dB relative to max  (pass almost everything)
        //   curveValue 1.0 → threshold at  +3 dB relative to max  (gate everything)
        const int ci = juce::jlimit(0, (int)localCurve.size() - 1, b);
        const float threshDB = localCurve[ci] * 63.0f - 60.0f;
        const float thresh = maxMag * std::pow(10.0f, threshDB / 20.0f);

        // Interval gate: decides which frequency range is the "active" region
        const bool insideInterval = (b >= lowerBinIdx && b <= upperBinIdx);
        const bool inActiveRegion = invertInterval ? !insideInterval : insideInterval;

        // Amplitude gate: decides which magnitudes pass
        const bool aboveThresh = (mag >= thresh);
        const bool passesGate = invertGate ? !aboveThresh : aboveThresh;

        if (!(inActiveRegion && passesGate))
        {
            fftBuffer[channel][b * 2] = 0.0f;
            fftBuffer[channel][b * 2 + 1] = 0.0f;
        }
    }

    // Inverse FFT
    fftSynthesis->performRealOnlyInverseTransform(fftBuffer[channel].data());

    // Overlap-add with normalisation
    const float norm = 2.0f / ((float)currentFFTSize / (float)currentHopSize);
    for (int i = 0; i < currentFFTSize; ++i)
    {
        int outIdx = (outputWritePos[channel] + i) % (int)outputAccum[channel].size();
        outputAccum[channel][outIdx] += fftBuffer[channel][i] * window[i] * norm;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// processBlock
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int totalIn = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    for (int channel = 0; channel < totalIn; ++channel)
    {
        auto* data = buffer.getWritePointer(channel);

        for (int s = 0; s < buffer.getNumSamples(); ++s)
        {
            inputFifo[channel][inputFifoIndex[channel]] = data[s];
            ++inputFifoIndex[channel];

            if (inputFifoIndex[channel] >= currentFFTSize)
            {
                // Apply analysis window and copy to FFT buffer
                for (int i = 0; i < currentFFTSize; ++i)
                    analysisFrame[channel][i] = inputFifo[channel][i] * window[i];

                std::copy(analysisFrame[channel].begin(), analysisFrame[channel].end(),
                    fftBuffer[channel].begin());

                performSpectralGate(channel);

                // Slide FIFO by hop size
                std::copy(inputFifo[channel].begin() + currentHopSize,
                    inputFifo[channel].end(),
                    inputFifo[channel].begin());
                std::fill(inputFifo[channel].end() - currentHopSize,
                    inputFifo[channel].end(), 0.0f);

                inputFifoIndex[channel] -= currentHopSize;
                grainCounter[channel] = currentHopSize;
            }

            // Read next output sample from OLA accumulator
            float out = outputAccum[channel][outputWritePos[channel]];
            outputAccum[channel][outputWritePos[channel]] = 0.0f;
            data[s] = out;
            outputWritePos[channel] =
                (outputWritePos[channel] + 1) % (int)outputAccum[channel].size();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Gate curve + FFT data access
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::getFFTData(std::vector<float>& out)
{
    juce::ScopedLock lock(fftLock);
    out = smoothedSpectrumMagnitudes;
}

void SpectralGateAudioProcessor::getGateCurve(std::vector<float>& out)
{
    juce::ScopedLock lock(fftLock);
    out = gateCurve;
}

void SpectralGateAudioProcessor::setGateCurveSegment(int binStart, float valStart,
    int binEnd, float valEnd)
{
    juce::ScopedLock lock(fftLock);
    const int n = (int)gateCurve.size();
    if (n <= 0) return;

    if (binStart > binEnd) { std::swap(binStart, binEnd); std::swap(valStart, valEnd); }
    binStart = juce::jlimit(0, n - 1, binStart);
    binEnd = juce::jlimit(0, n - 1, binEnd);

    for (int b = binStart; b <= binEnd; ++b)
    {
        const float t = (binStart == binEnd)
            ? 0.0f
            : (float)(b - binStart) / (float)(binEnd - binStart);
        gateCurve[b] = juce::jlimit(0.0f, 1.0f, valStart + t * (valEnd - valStart));
    }
}

void SpectralGateAudioProcessor::resetGateCurve(float value)
{
    juce::ScopedLock lock(fftLock);
    std::fill(gateCurve.begin(), gateCurve.end(), juce::jlimit(0.0f, 1.0f, value));
}

// ─────────────────────────────────────────────────────────────────────────────
// Boilerplate
// ─────────────────────────────────────────────────────────────────────────────
const juce::String SpectralGateAudioProcessor::getName() const { return JucePlugin_Name; }
bool   SpectralGateAudioProcessor::acceptsMidi()       const { return false; }
bool   SpectralGateAudioProcessor::producesMidi()      const { return false; }
bool   SpectralGateAudioProcessor::isMidiEffect()      const { return false; }
double SpectralGateAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int    SpectralGateAudioProcessor::getNumPrograms() { return 1; }
int    SpectralGateAudioProcessor::getCurrentProgram() { return 0; }
void   SpectralGateAudioProcessor::setCurrentProgram(int) {}
const  juce::String SpectralGateAudioProcessor::getProgramName(int) { return {}; }
void   SpectralGateAudioProcessor::changeProgramName(int, const juce::String&) {}
bool   SpectralGateAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpectralGateAudioProcessor::createEditor()
{
    return new SpectralGateAudioProcessorEditor(*this);
}

// ─────────────────────────────────────────────────────────────────────────────
// State persistence  (APVTS + FFT order + gate curve)
// ─────────────────────────────────────────────────────────────────────────────
void SpectralGateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    xml->setAttribute("fftOrder", currentFFTOrder);

    {
        juce::ScopedLock lock(fftLock);
        juce::MemoryBlock curveBlock(gateCurve.data(),
            gateCurve.size() * sizeof(float));
        xml->setAttribute("gateCurve", curveBlock.toBase64Encoding());
    }

    copyXmlToBinary(*xml, destData);
}

void SpectralGateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(parameters.state.getType())) return;

    // Restore FFT order first so buffers are the right size for the curve
    const int savedOrder = xml->getIntAttribute("fftOrder", 11);
    if (savedOrder != currentFFTOrder)
        setFFTOrder(savedOrder);

    // Restore gate curve
    if (xml->hasAttribute("gateCurve"))
    {
        juce::MemoryBlock curveBlock;
        curveBlock.fromBase64Encoding(xml->getStringAttribute("gateCurve"));
        const int numFloats = (int)(curveBlock.getSize() / sizeof(float));
        const auto* floatData = static_cast<const float*> (curveBlock.getData());
        const int expectedBins = currentFFTSize / 2 + 1;

        juce::ScopedLock lock(fftLock);
        gateCurve.assign(expectedBins, 0.0f);
        for (int i = 0; i < juce::jmin(numFloats, expectedBins); ++i)
            gateCurve[i] = juce::jlimit(0.0f, 1.0f, floatData[i]);
    }

    parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralGateAudioProcessor();
}