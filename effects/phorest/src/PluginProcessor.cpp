#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PhorestAudioProcessor::PhorestAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

PhorestAudioProcessor::~PhorestAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PhorestAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // Logarithmic skew so the bottom half of the knob covers 0.01–1 Hz for fine slow-sweep control
    juce::NormalisableRange<float> sweepRange(0.01f, 100.0f);
    sweepRange.setSkewForCentre(10.0f); // 12 o'clock = 1 Hz
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sweepFreq", "Sweep Freq", sweepRange, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("minDepth", "Min Depth", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("maxDepth", "Max Depth", 0.0f, 1.0f, 1.0f));
    // freqRange is now the maximum sweep frequency in Hz (log-skewed).
    // Centre of the knob = 1500 Hz → classic phaser territory.
    // Low values keep the sweep in the bass; high values open it up to air/treble.
    juce::NormalisableRange<float> rangeRange(50.0f, 20000.0f);
    rangeRange.setSkewForCentre(1500.0f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>("freqRange", "Freq Range", rangeRange, 1500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo", "Stereo", 0.0f, 1.0f, 0.25f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("stages", "Stages", 4, 48, 20));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("feedback", "Feedback", 0.0f, 0.99f, 0.90f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("dryWet", "Dry-Wet", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("outGain", "Out Gain", -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>("lfoShape", "LFO Shape", 0, 6, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("manualPos", "Manual Position", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("detune", "Detune", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("dryCancel", "Dry Cancel", 0.0f, 1.0f, 0.15f));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String PhorestAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PhorestAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PhorestAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PhorestAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PhorestAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PhorestAudioProcessor::getNumPrograms()
{
    return 1;
}

int PhorestAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PhorestAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String PhorestAudioProcessor::getProgramName(int index)
{
    return {};
}

void PhorestAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void PhorestAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    leftChannel = PhaserState();
    rightChannel = PhaserState();
}

void PhorestAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PhorestAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

float PhorestAudioProcessor::calculateAllpassCoeff(float frequencyHz)
{
    // First-order allpass filter coefficient calculation
    // Uses bilinear transform: a = (tan(π*f/fs) - 1) / (tan(π*f/fs) + 1)
    frequencyHz = juce::jlimit(20.0f, static_cast<float>(currentSampleRate * 0.48f), frequencyHz);
    float omega = juce::MathConstants<float>::pi * frequencyHz / static_cast<float>(currentSampleRate);
    float tanOmega = std::tan(omega);
    return (tanOmega - 1.0f) / (tanOmega + 1.0f);
}

float PhorestAudioProcessor::generateLFO(float phase, int shape)
{
    float value = 0.0f;

    switch (shape)
    {
    case 0: // Manual - returns 0, actual position is set by manualPos parameter
        value = 0.0f;
        break;

    case 1: // Sine
        value = std::sin(phase);
        break;

    case 2: // Triangle
        value = (2.0f / juce::MathConstants<float>::pi) * std::asin(std::sin(phase));
        break;

    case 3: // Ramp Up (sawtooth rising)
        value = (2.0f / juce::MathConstants<float>::pi) * (phase - juce::MathConstants<float>::pi) - 1.0f;
        if (value < -1.0f) value += 2.0f;
        break;

    case 4: // Ramp Down (sawtooth falling)
        value = -(2.0f / juce::MathConstants<float>::pi) * (phase - juce::MathConstants<float>::pi) + 1.0f;
        if (value > 1.0f) value -= 2.0f;
        break;

    case 5: // Square
        value = (std::sin(phase) >= 0.0f) ? 1.0f : -1.0f;
        break;

    case 6: // tanh(2 * sin(x))
        value = std::tanh(2.0f * std::sin(phase));
        break;

    default:
        value = std::sin(phase);
    }

    return value * 0.5f + 0.5f; // Convert from [-1, 1] to [0, 1]
}

// --- processSample: log-interpolated frequency sweep ---
float PhorestAudioProcessor::processSample(float input, PhaserState& state, float lfoValue,
    int numStages, float feedback, float detune, float maxFreqHz)
{
    // Logarithmic interpolation from 20 Hz to maxFreqHz.
    // This is crucial: the ear hears frequency logarithmically, so log interp gives
    // uniform-sounding resonance density across the entire sweep range, including bass.
    const float minFreq = 20.0f;
    maxFreqHz = juce::jlimit(minFreq + 1.0f, static_cast<float>(currentSampleRate * 0.45f), maxFreqHz);
    float lvo = juce::jlimit(0.0f, 1.0f, lfoValue); // safety clamp
    float centerFreq = minFreq * std::pow(maxFreqHz / minFreq, lvo);

    float phaserInput = input + state.feedbackSample * feedback;
    float output = phaserInput;

    for (int stage = 0; stage < numStages; ++stage)
    {
        // Multiplicative (logarithmic) stage spacing — keeps resonances musically even
        // at all settings, especially at low frequencies where linear spacing bunched them up.
        float stageFreq = centerFreq * std::pow(1.35f, static_cast<float>(stage));

        if (detune > 0.0f) {
            float detuneAmount = 1.0f + (detune * 0.12f * (stage % 2 == 0 ? 1.0f : -1.0f));
            stageFreq *= detuneAmount;
        }

        stageFreq = juce::jlimit(10.0f, static_cast<float>(currentSampleRate * 0.45f), stageFreq);
        float a = calculateAllpassCoeff(stageFreq);

        float outputSample = a * output + state.z1[stage];
        state.z1[stage] = output - a * outputSample;
        output = outputSample;
    }

    state.feedbackSample = output;
    return output;
}

std::complex<float> PhorestAudioProcessor::calculateComplexResponse(float testFreq, float lfoValue,
    int stages, float feedback)
{
    // Get current parameters
    float detune = *apvts.getRawParameterValue("detune");
    float freqRange = *apvts.getRawParameterValue("freqRange"); // now in Hz

    // Log interpolation — must match processSample exactly
    const float minFreq = 20.0f;
    float maxFreq = juce::jlimit(minFreq + 1.0f, static_cast<float>(currentSampleRate * 0.45f), freqRange);
    float lvo = juce::jlimit(0.0f, 1.0f, lfoValue);
    float centerFreq = minFreq * std::pow(maxFreq / minFreq, lvo);

    // Digital frequency (normalized)
    float omega = juce::MathConstants<float>::twoPi * testFreq / static_cast<float>(currentSampleRate);
    std::complex<float> z = std::exp(std::complex<float>(0.0f, omega)); // e^(jω)

    // Calculate cascade of allpass filters with detune
    std::complex<float> H(1.0f, 0.0f);

    for (int stage = 0; stage < stages; ++stage)
    {
        float stageFreq = centerFreq * std::pow(1.3f, static_cast<float>(stage));

        // Apply detune by spreading frequencies
        if (detune > 0.0f)
        {
            float detuneAmount = 1.0f + (detune * 0.12f * (stage % 2 == 0 ? 1.0f : -1.0f));
            stageFreq *= detuneAmount;
        }

        stageFreq = juce::jlimit(20.0f, static_cast<float>(currentSampleRate * 0.48f), stageFreq);
        float a = calculateAllpassCoeff(stageFreq);

        // Allpass transfer function: (a + z^-1) / (1 + a*z^-1)
        std::complex<float> numerator = a + (1.0f / z);
        std::complex<float> denominator = 1.0f + a / z;
        H *= numerator / denominator;
    }

    // Include feedback in the response
    // With feedback: H_feedback = H / (1 - feedback*H)
    std::complex<float> H_with_feedback = H / (std::complex<float>(1.0f, 0.0f) - feedback * H);

    // The phaser output is: input + H_with_feedback
    // So the total response relative to input is: 1 + H_with_feedback
    std::complex<float> totalResponse = std::complex<float>(1.0f, 0.0f) + H_with_feedback;

    return totalResponse;
}

// --- Visualizer Optimization: Prevents CPU spikes ---
void PhorestAudioProcessor::updateFrequencyResponse(float lfoValue, int stages, float feedback)
{
    // Skip if values haven't changed enough to matter (saves CPU)
    static float lastLfo = -1.0f;
    if (std::abs(lfoValue - lastLfo) < 0.001f) return;
    lastLfo = lfoValue;

    const juce::ScopedLock lock(responseLock);
    currentResponse.currentLFOPhase = lfoValue;

    for (int i = 0; i < FrequencyResponse::numPoints; ++i)
    {
        float freq = currentResponse.frequencies[i];
        // Use a simplified magnitude approx for visualizer to prevent spikes
        std::complex<float> response = calculateComplexResponse(freq, lfoValue, stages, feedback);
        float magnitudeDb = juce::Decibels::gainToDecibels(std::abs(response), -100.0f);
        currentResponse.magnitudes[i] = (magnitudeDb + 18.0f) / 24.0f;
    }
}
PhorestAudioProcessor::FrequencyResponse PhorestAudioProcessor::getFrequencyResponse()
{
    const juce::ScopedLock lock(responseLock);
    return currentResponse;
}

void PhorestAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // 1. Fetch parameters
    float sweepFreq = *apvts.getRawParameterValue("sweepFreq");
    float minDepth = *apvts.getRawParameterValue("minDepth");
    float maxDepth = *apvts.getRawParameterValue("maxDepth");

    // Decoupled: freqRange is now Width/Excursion, not speed multiplier
    float freqRange = *apvts.getRawParameterValue("freqRange");

    float stereoPhase = *apvts.getRawParameterValue("stereo");
    int stages = static_cast<int>(*apvts.getRawParameterValue("stages"));
    float feedback = *apvts.getRawParameterValue("feedback");
    float dryWet = *apvts.getRawParameterValue("dryWet");
    float outGainDb = *apvts.getRawParameterValue("outGain");
    int lfoShape = static_cast<int>(*apvts.getRawParameterValue("lfoShape"));
    float manualPos = *apvts.getRawParameterValue("manualPos");
    float detune = *apvts.getRawParameterValue("detune");
    float dryCancel = *apvts.getRawParameterValue("dryCancel");

    float outGain = juce::Decibels::decibelsToGain(outGainDb);

    // 2. Visualizer Update (using decoupled logic)
    static int visualUpdateCounter = 0;
    if (++visualUpdateCounter >= 16)
    {
        visualUpdateCounter = 0;
        float lfoVal;
        if (lfoShape == 0) {
            lfoVal = manualPos;
        }
        else {
            float lfoL = generateLFO(leftChannel.lfoPhase, lfoShape);
            float lfoR = generateLFO(leftChannel.lfoPhase + (stereoPhase * juce::MathConstants<float>::twoPi), lfoShape);
            lfoVal = (lfoL + lfoR) * 0.5f;
        }

        // Map LFO [0,1] into [minDepth, maxDepth] — no multiplier, no clamping needed
        float displayLfo = juce::jlimit(0.0f, 1.0f, minDepth + lfoVal * (maxDepth - minDepth));
        updateFrequencyResponse(displayLfo, stages, feedback);
    }

    // 3. Per-Sample Processing
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float leftLfo, rightLfo;

        if (lfoShape == 0)
        {
            leftLfo = manualPos;
            rightLfo = manualPos;
        }
        else
        {
            float lfoIncrement = (sweepFreq / static_cast<float>(currentSampleRate)) * juce::MathConstants<float>::twoPi;

            leftChannel.lfoPhase += lfoIncrement;
            if (leftChannel.lfoPhase >= juce::MathConstants<float>::twoPi)
                leftChannel.lfoPhase -= juce::MathConstants<float>::twoPi;

            float rightPhaseOffset = stereoPhase * juce::MathConstants<float>::twoPi;
            float currentRightPhase = leftChannel.lfoPhase + rightPhaseOffset;
            if (currentRightPhase >= juce::MathConstants<float>::twoPi)
                currentRightPhase -= juce::MathConstants<float>::twoPi;

            // Raw 0-1 LFO
            float rawL = generateLFO(leftChannel.lfoPhase, lfoShape);
            float rawR = generateLFO(currentRightPhase, lfoShape);

            // Map raw LFO [0,1] into [minDepth, maxDepth] and clamp — clean and symmetric
            leftLfo = juce::jlimit(0.0f, 1.0f, minDepth + rawL * (maxDepth - minDepth));
            rightLfo = juce::jlimit(0.0f, 1.0f, minDepth + rawR * (maxDepth - minDepth));
        }

        // Process Channels
        if (totalNumInputChannels > 0)
        {
            float* channelData = buffer.getWritePointer(0);
            float wet = processSample(channelData[sample], leftChannel, leftLfo, stages, feedback, detune, freqRange);
            float mixed = channelData[sample] * (1.0f - dryWet) + wet * dryWet;
            channelData[sample] = (mixed - (channelData[sample] * dryCancel)) * outGain;
        }

        if (totalNumInputChannels > 1)
        {
            float* channelData = buffer.getWritePointer(1);
            float wet = processSample(channelData[sample], rightChannel, rightLfo, stages, feedback, detune, freqRange);
            float mixed = channelData[sample] * (1.0f - dryWet) + wet * dryWet;
            channelData[sample] = (mixed - (channelData[sample] * dryCancel)) * outGain;
        }
    }
}

//==============================================================================
bool PhorestAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PhorestAudioProcessor::createEditor()
{
    return new PhorestAudioProcessorEditor(*this);
}

//==============================================================================
void PhorestAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PhorestAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhorestAudioProcessor();
}