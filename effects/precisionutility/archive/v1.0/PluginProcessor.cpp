/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PrecisionUtilityAudioProcessor::PrecisionUtilityAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#else
    :
#endif
apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

PrecisionUtilityAudioProcessor::~PrecisionUtilityAudioProcessor()
{
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PrecisionUtilityAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Delay parameter (0-10000 ms)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "delay",
        "Delay",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f),
        0.0f,
        "ms"));

    // Phase invert parameter (0-100%)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "phase",
        "Phase Invert",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f,
        "%"));

    // Left pan parameter (-100 to +100, where -100 = right channel, 0 = center, +100 = left channel)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "panLeft",
        "Pan Left",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
        -100.0f,  // Default: full left (no processing)
        ""));

    // Right pan parameter (-100 to +100, where -100 = left channel, 0 = center, +100 = right channel)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "panRight",
        "Pan Right",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
        100.0f,   // Default: full right (no processing)
        ""));

    return layout;
}

//==============================================================================
const juce::String PrecisionUtilityAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PrecisionUtilityAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool PrecisionUtilityAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool PrecisionUtilityAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double PrecisionUtilityAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PrecisionUtilityAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int PrecisionUtilityAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PrecisionUtilityAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String PrecisionUtilityAudioProcessor::getProgramName(int index)
{
    return {};
}

void PrecisionUtilityAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void PrecisionUtilityAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Calculate max delay buffer size (10 seconds at current sample rate)
    int maxDelaySamples = static_cast<int>(sampleRate * 10.0);

    // Initialize delay buffer
    delayBuffer.setSize(2, maxDelaySamples);
    delayBuffer.clear();
    delayBufferWritePosition[0] = 0;
    delayBufferWritePosition[1] = 0;
}

void PrecisionUtilityAudioProcessor::releaseResources()
{
    delayBuffer.clear();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PrecisionUtilityAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void PrecisionUtilityAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Get parameter values
    float delayMs = apvts.getRawParameterValue("delay")->load();
    float phasePercent = apvts.getRawParameterValue("phase")->load();
    float panLeft = apvts.getRawParameterValue("panLeft")->load();
    float panRight = apvts.getRawParameterValue("panRight")->load();

    // Calculate delay in samples
    int delaySamples = static_cast<int>((delayMs / 1000.0) * currentSampleRate);

    // Check if we're at default settings (complete bypass)
    bool isDefaultState = (delayMs == 0.0f && phasePercent == 0.0f &&
        panLeft == -100.0f && panRight == 100.0f);

    if (isDefaultState)
    {
        // Complete bypass - don't touch the audio at all
        return;
    }

    // Phase inversion factor (0.0 to 1.0)
    float phaseFactor = phasePercent / 100.0f;

    const int bufferSize = buffer.getNumSamples();
    const int delayBufferSize = delayBuffer.getNumSamples();

    // Process delay and phase invert if needed
    bool needsPhaseOrDelay = (delaySamples > 0 || phaseFactor > 0.0f);

    if (needsPhaseOrDelay)
    {
        if (delaySamples > 0)
        {
            // Use delay buffer when delay is active
            for (int channel = 0; channel < juce::jmin(totalNumInputChannels, 2); ++channel)
            {
                auto* channelData = buffer.getWritePointer(channel);
                auto* delayData = delayBuffer.getWritePointer(channel);

                for (int sample = 0; sample < bufferSize; ++sample)
                {
                    float inputSample = channelData[sample];

                    // Apply phase inversion
                    float processedSample = inputSample * (1.0f - phaseFactor) + (-inputSample) * phaseFactor;

                    // Write to delay buffer
                    delayData[delayBufferWritePosition[channel]] = processedSample;

                    // Read from delay buffer
                    int readPosition = delayBufferWritePosition[channel] - delaySamples;
                    if (readPosition < 0)
                        readPosition += delayBufferSize;

                    channelData[sample] = delayData[readPosition];

                    // Increment write position for this channel
                    if (++delayBufferWritePosition[channel] >= delayBufferSize)
                        delayBufferWritePosition[channel] = 0;
                }
            }
        }
        else
        {
            // No delay - process phase inversion in place (zero latency)
            for (int channel = 0; channel < juce::jmin(totalNumInputChannels, 2); ++channel)
            {
                auto* channelData = buffer.getWritePointer(channel);

                for (int sample = 0; sample < bufferSize; ++sample)
                {
                    float inputSample = channelData[sample];

                    // Apply phase inversion only
                    channelData[sample] = inputSample * (1.0f - phaseFactor) + (-inputSample) * phaseFactor;
                }
            }
        }
    }

    // Apply independent L/R panning only if not at default
    bool needsPanning = (panLeft != -100.0f || panRight != 100.0f);

    if (needsPanning && totalNumInputChannels >= 2)
    {
        auto* leftData = buffer.getWritePointer(0);
        auto* rightData = buffer.getWritePointer(1);

        // Store original samples before processing
        juce::AudioBuffer<float> tempBuffer(2, bufferSize);
        tempBuffer.copyFrom(0, 0, leftData, bufferSize);
        tempBuffer.copyFrom(1, 0, rightData, bufferSize);

        auto* origLeft = tempBuffer.getReadPointer(0);
        auto* origRight = tempBuffer.getReadPointer(1);

        for (int sample = 0; sample < bufferSize; ++sample)
        {
            float leftInput = origLeft[sample];
            float rightInput = origRight[sample];

            // Pan Left: Routes LEFT INPUT channel
            // -100 (default): Left input → Left output only
            // 0: Left input → Both outputs equally (center)
            // +100: Left input → Right output only
            float leftPan = (panLeft + 100.0f) / 200.0f; // Maps -100..100 to 0..1
            float leftToLeft = 1.0f - leftPan;   // 1 at -100, 0.5 at 0, 0 at +100
            float leftToRight = leftPan;         // 0 at -100, 0.5 at 0, 1 at +100

            // Pan Right: Routes RIGHT INPUT channel  
            // +100 (default): Right input → Right output only
            // 0: Right input → Both outputs equally (center)
            // -100: Right input → Left output only
            float rightPan = (panRight + 100.0f) / 200.0f; // Maps -100..100 to 0..1
            float rightToRight = rightPan;       // 0 at -100, 0.5 at 0, 1 at +100
            float rightToLeft = 1.0f - rightPan; // 1 at -100, 0.5 at 0, 0 at +100

            // Mix both inputs to outputs
            leftData[sample] = leftInput * leftToLeft + rightInput * rightToLeft;
            rightData[sample] = leftInput * leftToRight + rightInput * rightToRight;
        }
    }
}

//==============================================================================
bool PrecisionUtilityAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* PrecisionUtilityAudioProcessor::createEditor()
{
    return new PrecisionUtilityAudioProcessorEditor(*this);
}

//==============================================================================
void PrecisionUtilityAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PrecisionUtilityAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrecisionUtilityAudioProcessor();
}