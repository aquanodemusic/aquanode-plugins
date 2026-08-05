#include "PluginProcessor.h"
#include "PluginEditor.h"

EQResonatorAudioProcessor::EQResonatorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int n = 0; n < numNotes; ++n)
    {
        for (int oct = 0; oct < numOctaves; ++oct)
        {
            int index = (n * numOctaves) + oct;
            int midiNote = n + (oct * 12) + 12;
            noteFrequencies[index] = 440.0f * std::pow(2.0f, (float(midiNote) - 69.0f) / 12.0f);
        }
    }

    for (int i = 0; i < 12; ++i)
        noteParams[i] = apvts.getRawParameterValue(juce::String(i));

    for (int i = 0; i < 10; ++i)
    {
        octaveActiveParams[i] = apvts.getRawParameterValue("octave" + juce::String(i));
        octaveQParams[i] = apvts.getRawParameterValue("qOctave" + juce::String(i));
        octaveGainParams[i] = apvts.getRawParameterValue("gainOctave" + juce::String(i));
    }

    wetOnlyParam = apvts.getRawParameterValue("wetOnly");
    subtractModeParam = apvts.getRawParameterValue("subtractMode"); // NEW
}

EQResonatorAudioProcessor::~EQResonatorAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
EQResonatorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Notes (C–B)
    for (int i = 0; i < 12; ++i)
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ juce::String(i), 1 },
            "Note " + juce::String(i),
            true));

    // Per-octave controls
    for (int i = 0; i < 10; ++i)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ "octave" + juce::String(i), 1 },
            "Octave " + juce::String(i + 1),
            true));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "qOctave" + juce::String(i), 1 },
            "Q Octave " + juce::String(i + 1),
            juce::NormalisableRange<float>(0.1f, 500.0f, 0.1f, 0.75f),
            150.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "gainOctave" + juce::String(i), 1 },
            "Gain Octave " + juce::String(i + 1),
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
            -12.0f));
    }

    // Global
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "wetOnly", 1 },
        "Wet Only",
        false));

    // --- NEW: Subtract mode toggle ---
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "subtractMode", 1 },
        "Subtract Mode",
        false));

    return layout;
}

void EQResonatorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;

    for (int i = 0; i < totalFilters; ++i)
    {
        filtersL[i].reset();
        filtersR[i].reset();
    }

    std::fill(std::begin(lastOctaveQs), std::end(lastOctaveQs), -1.0f);
    std::fill(std::begin(lastOctaveGains), std::end(lastOctaveGains), -1000.0f);

    dryCopyBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
}

void EQResonatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Make a dry copy of the input buffer
    for (int ch = 0; ch < numChannels; ++ch)
        dryCopyBuffer.copyFrom(ch, 0, buffer.getReadPointer(ch), numSamples);

    const bool wetOnly = wetOnlyParam->load() > 0.5f;
    const bool subtractMode = subtractModeParam->load() > 0.5f;

    // Track note changes if needed (optional)
    bool noteSelectionChanged = false;

    // Clear buffer if additive mode (resonator) — sum will go here
    if (!subtractMode)
        buffer.clear();

    // --- Loop through octaves ---
    for (int oct = 0; oct < numOctaves; ++oct)
    {
        if (octaveActiveParams[oct]->load() < 0.5f)
            continue;

        const float Q = octaveQParams[oct]->load();
        const float gainDb = octaveGainParams[oct]->load();
        const float gainLin = juce::Decibels::decibelsToGain(gainDb);

        // Update filters if Q or notes changed
        bool needsUpdate = (std::abs(Q - lastOctaveQs[oct]) > 0.01f) || noteSelectionChanged;

        for (int n = 0; n < numNotes; ++n)
        {
            if (noteParams[n]->load() < 0.5f)
                continue;

            const int idx = (n * numOctaves) + oct;
            const float freq = noteFrequencies[idx];

            if (freq >= lastSampleRate * 0.45f)
                continue;

            if (needsUpdate)
            {
                // Choose filter type based on subtractMode
                auto coeffs = subtractMode
                    ? juce::IIRCoefficients::makeNotchFilter(lastSampleRate, freq, Q)
                    : juce::IIRCoefficients::makeBandPass(lastSampleRate, freq, Q);

                filtersL[idx].setCoefficients(coeffs);
                filtersR[idx].setCoefficients(coeffs);

                // Reset filter memory for notch mode to avoid "sticking"
                if (subtractMode) {
                    filtersL[idx].reset();
                    filtersR[idx].reset();
                }
            }

            // Apply filters to each channel
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* outData = buffer.getWritePointer(ch);
                const auto* dryData = dryCopyBuffer.getReadPointer(ch);
                auto& filter = (ch == 0) ? filtersL[idx] : filtersR[idx];

                if (subtractMode)
                {
                    // Notch mode: overwrite buffer to remove the frequency
                    for (int s = 0; s < numSamples; ++s)
                        outData[s] = filter.processSingleSampleRaw(outData[s]);
                }
                else
                {
                    // Additive resonator: sum filtered dry signal
                    for (int s = 0; s < numSamples; ++s)
                        outData[s] += filter.processSingleSampleRaw(dryData[s]) * gainLin;
                }
            }
        }

        lastOctaveQs[oct] = Q;
        lastOctaveGains[oct] = gainDb;
    }

    // In additive mode, add dry signal back if not wet-only
    if (!subtractMode && !wetOnly)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.addFrom(ch, 0, dryCopyBuffer.getReadPointer(ch), numSamples);
    }
}


const juce::String EQResonatorAudioProcessor::getName() const { return JucePlugin_Name; }
bool EQResonatorAudioProcessor::acceptsMidi() const { return false; }
bool EQResonatorAudioProcessor::producesMidi() const { return false; }
bool EQResonatorAudioProcessor::isMidiEffect() const { return false; }
double EQResonatorAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int EQResonatorAudioProcessor::getNumPrograms() { return 1; }
int EQResonatorAudioProcessor::getCurrentProgram() { return 0; }
void EQResonatorAudioProcessor::setCurrentProgram(int) {}
const juce::String EQResonatorAudioProcessor::getProgramName(int) { return {}; }
void EQResonatorAudioProcessor::changeProgramName(int, const juce::String&) {}
bool EQResonatorAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* EQResonatorAudioProcessor::createEditor() { return new EQResonatorAudioProcessorEditor(*this); }

#ifndef JucePlugin_PreferredChannelConfigurations
bool EQResonatorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
}
#endif

void EQResonatorAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();                  // Copy ValueTree state
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);                 // Convert to binary
}

void EQResonatorAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

void EQResonatorAudioProcessor::releaseResources() {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new EQResonatorAudioProcessor();
}