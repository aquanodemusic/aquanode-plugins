#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Constructor for the audio processor
// Sets up all parameters, frequency table, and caches pointers for fast access.
//==============================================================================
EQResonatorAudioProcessor::EQResonatorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)   // stereo input
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)), // stereo output
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout()) // initialize parameter state
{
    // --- 1. Compute frequency table for all notes and octaves ---
    // Stores the frequency in Hz for each note across all octaves
    // MIDI note 69 = A4 = 440Hz
    for (int n = 0; n < numNotes; ++n)
    {
        for (int oct = 0; oct < numOctaves; ++oct)
        {
            int index = (n * numOctaves) + oct;       // Index into noteFrequencies array
            int midiNote = n + (oct * 12) + 12;      // MIDI note number (starting from C1)
            noteFrequencies[index] = 440.0f * std::pow(2.0f, (float(midiNote) - 69.0f) / 12.0f);
            // Formula converts MIDI note to frequency in Hz
        }
    }

    // --- 2. Cache parameter pointers ---
    // These pointers allow fast reading in the audio thread without repeatedly
    // calling getRawParameterValue, which is slightly more expensive.
    // IMPORTANT: The IDs must exactly match those used in createParameterLayout()

    for (int i = 0; i < 12; ++i) {
        noteParams[i] = apvts.getRawParameterValue(juce::String(i)); // Notes 0-11
    }

    for (int i = 0; i < 10; ++i) {
        octaveActiveParams[i] = apvts.getRawParameterValue("octave" + juce::String(i)); // Octave on/off
        octaveQParams[i] = apvts.getRawParameterValue("qOctave" + juce::String(i));     // Q per octave
    }

    // Cache global parameters
    attenuateParam = apvts.getRawParameterValue("attenuate"); // Notch mode toggle
    wetGainParam = apvts.getRawParameterValue("wetGain");     // Wet gain slider
    wetOnlyParam = apvts.getRawParameterValue("wetOnly");
}

//==============================================================================
// Destructor: nothing special needed because all memory is managed automatically
//==============================================================================
EQResonatorAudioProcessor::~EQResonatorAudioProcessor() {}

//==============================================================================
// Create the parameter layout for the plugin.
// This defines all automatable parameters for notes, octaves, Q values, and global controls.
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EQResonatorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- 1. 12 Notes (C to B) ---
    // The parameter IDs must exactly match the strings used in the constructor
    for (int i = 0; i < 12; ++i)
    {
        // Using simple string IDs "0", "1", ... "11" for notes
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ juce::String(i), 1 },
            "Note " + juce::String(i), // Human-readable name
            true));                     // Default state: active
    }

    // --- 2. 10 Octaves and their Q factors ---
    for (int i = 0; i < 10; ++i)
    {
        // Octave on/off toggle
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{ "octave" + juce::String(i), 1 },
            "Octave Active " + juce::String(i + 1),
            true)); // default: active

        // Q (resonance) parameter for this octave
        // Skew factor 0.2 allows finer control in the low range
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ "qOctave" + juce::String(i), 1 },
            "Q Octave " + juce::String(i + 1),
            juce::NormalisableRange<float>(1.0f, 1000.0f, 0.1f, 0.2f),
            50.0f)); // default Q value
    }

    // --- 3. Global Controls ---

    // Toggle between additive (resonator) and subtractive (notch) modes
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "attenuate", 1 },
        "Attenuate Mode (Notch)",
        false)); // default: off

    // Wet gain control for resonator mode, in dB
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wetGain", 1 },
        "Wet Gain (dB)",
        juce::NormalisableRange<float>(-60.0f, 24.0f, 0.1f),
        0.0f)); // default: 0 dB

    // --- Wet Only switch ---
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "wetOnly", 1 },
        "Wet Only",
        false)); // default off

    return layout;
}

//==============================================================================
// Called before playback starts. Initialize filters and sample rate.
//==============================================================================
void EQResonatorAudioProcessor::prepareToPlay(double sampleRate, int)
{
    lastSampleRate = sampleRate;

    // Reset all filters for left and right channels
    for (int i = 0; i < totalFilters; ++i) {
        filtersL[i].reset();
        filtersR[i].reset();
    }
}

//==============================================================================
// Main audio processing function
// This is called for each block of audio samples.
// Applies the resonator or notch filters based on active notes and octaves.
//==============================================================================
void EQResonatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals; // Avoid denormal numbers for performance
    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumOutputChannels();

    // Create a copy of the original buffer (dry signal)
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // Read global parameters
    const bool isAttenuate = attenuateParam->load() > 0.5f;       // Notch vs resonator mode
    const float wetGainDb = wetGainParam->load();                 // Wet gain in dB
    const float wetGainAmt = juce::Decibels::decibelsToGain(wetGainDb); // Convert dB to linear gain

    // --- Detect if any note selection changed ---
    bool noteSelectionChanged = false;
    for (int n = 0; n < 12; ++n) {
        float currentState = noteParams[n]->load();
        if (std::abs(currentState - lastNoteStates[n]) > 0.1f) {
            noteSelectionChanged = true;  // Trigger coefficient update
            lastNoteStates[n] = currentState;
        }
    }

    // If in additive mode (resonator), clear the buffer before summing filters
    if (!isAttenuate) buffer.clear();

    // --- Loop through octaves ---
    for (int oct = 0; oct < numOctaves; ++oct)
    {
        float rawQ = octaveQParams[oct]->load();               // Raw Q value from parameter
        bool octActive = octaveActiveParams[oct]->load() > 0.5f; // Is this octave active?
        float effectiveQ = isAttenuate ? juce::jmin(rawQ, 40.0f) : rawQ; // Limit Q in notch mode

        // Detect if Q has changed significantly
        bool qChanged = std::abs(effectiveQ - lastOctaveQs[oct]) > 0.01f;
        lastOctaveQs[oct] = effectiveQ;

        if (octActive)
        {
            // --- Loop through notes ---
            for (int n = 0; n < numNotes; ++n)
            {
                if (noteParams[n]->load() > 0.5f) // Only process active notes
                {
                    int idx = (n * numOctaves) + oct; // Index into filter array

                    // Skip frequencies above Nyquist
                    if (noteFrequencies[idx] >= lastSampleRate * 0.45f) continue;

                    // --- Refresh filter coefficients if needed ---
                    // Update if Q changed, mode changed, or note selection changed
                    if (qChanged || (isAttenuate != lastIsAttenuate) || noteSelectionChanged)
                    {
                        auto coeffs = isAttenuate
                            ? juce::IIRCoefficients::makeNotchFilter(lastSampleRate, noteFrequencies[idx], effectiveQ)
                            : juce::IIRCoefficients::makeBandPass(lastSampleRate, noteFrequencies[idx], effectiveQ);

                        filtersL[idx].setCoefficients(coeffs);
                        filtersR[idx].setCoefficients(coeffs);

                        // Reset filter memory in notch mode to prevent "sticking"
                        if (isAttenuate) {
                            filtersL[idx].reset();
                            filtersR[idx].reset();
                        }
                    }

                    // --- Apply filter to each channel ---
                    for (int ch = 0; ch < numChannels; ++ch) {
                        auto* channelData = buffer.getWritePointer(ch);
                        const auto* dryData = dryBuffer.getReadPointer(ch);
                        auto& filter = (ch == 0) ? filtersL[idx] : filtersR[idx];

                        if (isAttenuate) {
                            // Notch mode: overwrite the signal
                            for (int s = 0; s < numSamples; ++s) {
                                channelData[s] = filter.processSingleSampleRaw(channelData[s]);
                            }
                        }
                        else {
                            // Resonator mode: add filtered signal to dry with wet gain
                            for (int s = 0; s < numSamples; ++s) {
                                channelData[s] += filter.processSingleSampleRaw(dryData[s]) * wetGainAmt;
                            }
                        }
                    }
                }
            }
        }
    }

    lastIsAttenuate = isAttenuate;

    // Add dry signal back in resonator mode (if not attenuate)
    const bool wetOnly = wetOnlyParam->load() > 0.5f;

    if (!isAttenuate && !wetOnly) {
        // Only add dry signal if not wet-only
        for (int ch = 0; ch < numChannels; ++ch) {
            buffer.addFrom(ch, 0, dryBuffer, ch, 0, numSamples, 1.0f);
        }
    }
}

//==============================================================================
// --- JUCE standard boilerplate ---
// These provide plugin metadata, program info, and state handling
//==============================================================================

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

//==============================================================================
// Save plugin state to memory
//==============================================================================
void EQResonatorAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();                  // Copy ValueTree state
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);                 // Convert to binary
}

//==============================================================================
// Restore plugin state from memory
//==============================================================================
void EQResonatorAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
// Release any extra resources when playback stops
//==============================================================================
void EQResonatorAudioProcessor::releaseResources() {}

//==============================================================================
// Factory function called by JUCE to create the plugin instance
//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new EQResonatorAudioProcessor();
}