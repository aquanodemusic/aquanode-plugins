#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PitchColorAudioProcessor::PitchColorAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, juce::Identifier("PitchColorVST"), createParameterLayout())
{
    // Add listener for all parameters that affect filter coefficients
    for (int i = 0; i < NOTES_PER_OCTAVE; ++i)
    {
        parameters.addParameterListener(juce::String("gain") + juce::String(i), this);
        parameters.addParameterListener(juce::String("q") + juce::String(i), this);
    }
    parameters.addParameterListener("startNote", this);
    parameters.addParameterListener("endNote", this);
}

PitchColorAudioProcessor::~PitchColorAudioProcessor()
{
    // Remove parameter listeners
    for (int i = 0; i < NOTES_PER_OCTAVE; ++i)
    {
        parameters.removeParameterListener(juce::String("gain") + juce::String(i), this);
        parameters.removeParameterListener(juce::String("q") + juce::String(i), this);
    }
    parameters.removeParameterListener("startNote", this);
    parameters.removeParameterListener("endNote", this);
}

//==============================================================================
void PitchColorAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    // Mark that filter coefficients need updating
    needsFilterUpdate.store(true);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PitchColorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 12 note gain controls (-48 dB to +24 dB)
    for (int i = 0; i < NOTES_PER_OCTAVE; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(juce::String("gain") + juce::String(i), 1),
            juce::String(noteNames[i]) + " Gain",
            juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
            0.0f,
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            [](float value, int) { return juce::String(value, 1) + " dB"; }));
    }

    // 12 note Q controls (0.1 to 10000)
    for (int i = 0; i < NOTES_PER_OCTAVE; ++i)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(juce::String("q") + juce::String(i), 1),
            juce::String(noteNames[i]) + " Q",
            juce::NormalisableRange<float>(10.0f, 100.0f, 0.1f, 0.25f),
            20.0f,
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            [](float value, int) {
                if (value >= 100.0f)
                    return juce::String(value, 0);
                else if (value >= 10.0f)
                    return juce::String(value, 1);
                else
                    return juce::String(value, 2);
            }));
    }

    // Start note (C0 = MIDI 12, B9 = MIDI 131)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("startNote", 1),
        "Start Note",
        12, 131,
        12,
        juce::String(),
        [](int value, int) {
            int octave = (value / 12) - 1;
            int note = value % 12;
            return juce::String(noteNames[note]) + juce::String(octave);
        }));

    // End note (C0 = MIDI 12, B9 = MIDI 131)
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID("endNote", 1),
        "End Note",
        12, 131,
        131,
        juce::String(),
        [](int value, int) {
            int octave = (value / 12) - 1;
            int note = value % 12;
            return juce::String(noteNames[note]) + juce::String(octave);
        }));

    // Wet only toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("wetOnly", 1),
        "Wet Only",
        false));

    return { params.begin(), params.end() };
}

//==============================================================================
float PitchColorAudioProcessor::getFrequencyForNote(int midiNoteNumber)
{
    // A4 (MIDI 69) = 440 Hz
    return 440.0f * std::pow(2.0f, (midiNoteNumber - 69.0f) / 12.0f);
}

void PitchColorAudioProcessor::updateFilterCoefficients(int noteIndex)
{
    // Calculate MIDI note number (C0 = 12, C1 = 24, etc.)
    int midiNote = 12 + noteIndex;

    // Get the pitch class (0-11) for this note
    int pitchClass = noteIndex % NOTES_PER_OCTAVE;

    // Get parameters for this pitch class
    float gainDb = parameters.getRawParameterValue(juce::String("gain") + juce::String(pitchClass))->load();
    float q = parameters.getRawParameterValue(juce::String("q") + juce::String(pitchClass))->load();

    // Get start and end notes to determine if this filter should be active
    int startNote = parameters.getRawParameterValue("startNote")->load();
    int endNote = parameters.getRawParameterValue("endNote")->load();

    // If outside range, set gain to 0 dB (unity)
    if (midiNote < startNote || midiNote > endNote)
        gainDb = 0.0f;

    // Calculate frequency for this note
    float frequency = getFrequencyForNote(midiNote);

    // Convert gain from dB to linear
    float gain = juce::Decibels::decibelsToGain(gainDb);

    // Create bell filter coefficients
    auto coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        currentSampleRate,
        frequency,
        q,
        gain);

    // Update both channels
    for (int channel = 0; channel < 2; ++channel)
    {
        *bellFilters[channel][noteIndex].coefficients = *coefficients;
    }
}

//==============================================================================
const juce::String PitchColorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PitchColorAudioProcessor::acceptsMidi() const
{
    return false;
}

bool PitchColorAudioProcessor::producesMidi() const
{
    return false;
}

bool PitchColorAudioProcessor::isMidiEffect() const
{
    return false;
}

double PitchColorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PitchColorAudioProcessor::getNumPrograms()
{
    return 1;
}

int PitchColorAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PitchColorAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String PitchColorAudioProcessor::getProgramName(int index)
{
    return {};
}

void PitchColorAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void PitchColorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = static_cast<float>(sampleRate);

    // Pre-allocate dry buffer to avoid allocation in audio thread
    dryBuffer.setSize(2, samplesPerBlock);

    // Prepare all filters
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 1; // Each filter processes one channel

    for (int channel = 0; channel < 2; ++channel)
    {
        for (int note = 0; note < NUM_NOTES; ++note)
        {
            bellFilters[channel][note].prepare(spec);
            bellFilters[channel][note].reset();
        }
    }

    // Initialize all filter coefficients
    for (int note = 0; note < NUM_NOTES; ++note)
    {
        updateFilterCoefficients(note);
    }

    // Mark that initial filter update is complete
    needsFilterUpdate.store(false);
}

void PitchColorAudioProcessor::releaseResources()
{
}

bool PitchColorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet()
        && !layouts.getMainInputChannelSet().isDisabled();
}

void PitchColorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any output channels that don't have input
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Get wet only parameter
    bool wetOnly = parameters.getRawParameterValue("wetOnly")->load();

    // Save dry signal for wet-only mode (to subtract and get only the delta)
    if (wetOnly)
    {
        dryBuffer.makeCopyOf(buffer);
    }

    // Update filter coefficients only when parameters have changed
    if (needsFilterUpdate.exchange(false))
    {
        for (int note = 0; note < NUM_NOTES; ++note)
        {
            updateFilterCoefficients(note);
        }
    }

    // Process each channel through the filter bank
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        // Apply all bell filters in series
        for (int note = 0; note < NUM_NOTES; ++note)
        {
            juce::dsp::AudioBlock<float> block(&channelData, 1, buffer.getNumSamples());
            juce::dsp::ProcessContextReplacing<float> context(block);
            bellFilters[channel][note].process(context);
        }
    }

    // Wet-only mode: subtract dry to output only filter modifications (wet - dry)
    // This removes frequencies where filters are at 0dB (unity gain)
    if (wetOnly)
    {
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            buffer.addFrom(channel, 0, dryBuffer, channel, 0, buffer.getNumSamples(), -1.0f);
        }
    }
    // Normal mode: output is already the processed signal (wet)
    // Bell filters at 0dB are unity gain, so wet already contains dry when unmodified
}

//==============================================================================
bool PitchColorAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PitchColorAudioProcessor::createEditor()
{
    return new PitchColorAudioProcessorEditor(*this);
}

//==============================================================================
void PitchColorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PitchColorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchColorAudioProcessor();
}