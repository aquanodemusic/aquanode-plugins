#include "PluginProcessor.h" // Includes the "blueprint" of this class
#include "PluginEditor.h"    // Includes the UI instructions

//==============================================================================
/**
 * BandpassModulator - A rhythmic filter VST.
 * Developed by aquanode (2026).
 */

 // The '::' is the Scope Resolution Operator. 
 // It tells the compiler: "The function BandpassModulatorAudioProcessor() belongs to the class named the same."
BandpassModulatorAudioProcessor::BandpassModulatorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations // Pre-processor check: Only runs if this macro isn't defined
// The ':' starts the Initializer List. This sets up variables BEFORE the {} body runs.
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)   // Set up stereo input
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)), // Set up stereo output
    // apvts = AudioProcessorValueTreeState
    // APVTS IS THE PLACE TO DEFINE THE STATE OF THE PLUGIN; ALSO FOR THE INITIAL STATE!
    // This is JUCE's parameter system:
    // - Thread-safe
    // - Automatable
    // - Serializable
    // - Connects DSP <-> UI
    apvts(*this, nullptr, "Parameters", createParameterLayout())     // Initialize the Parameter State
#endif
{
    // Constructor body is empty because we used the Initializer List above.
}

// The '~' is the Destructor. It is called when the plugin is closed to clean up memory.
BandpassModulatorAudioProcessor::~BandpassModulatorAudioProcessor() {}

//==============================================================================
// This function defines all the sliders and buttons you see in the DAW.
juce::AudioProcessorValueTreeState::ParameterLayout BandpassModulatorAudioProcessor::createParameterLayout()
{
    // 'layout' is a local object that holds all our definitions.
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // std::make_unique creates a smart pointer
    // The parameter is owned by the APVTS and freed automatically
    // 'std::make_unique' is a "Smart Pointer". 
    // It creates the parameter in memory and automatically deletes it when the plugin closes (no memory leaks).
    // The '<...>' is a Template, telling it exactly what type of object to create.

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "minFreq", "Min Frequency", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));
    // NormalisableRange:
    // min, max, stepSize, skewFactor
    // skewFactor < 1 gives more resolution at lower values (good for frequencies)

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "maxFreq", "Max Frequency", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 2000.0f));

    // The 'f' suffix (e.g. 10.0f) ensures the number is a 32-bit 'float' rather than a 64-bit 'double'.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "glideTime", "Glide", juce::NormalisableRange<float>(0.0001f, 10.0f, 0.0f, 0.4f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "stayTime", "Stay", juce::NormalisableRange<float>(0.0001f, 10.0f, 0.0f, 0.4f), 0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("panning", "Panning", -1.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("dryWet", "Dry/Wet", 0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("width", "Bandwidth", 0.1f, 10.0f, 1.0f));

    // Boolean parameters for checkboxes/toggles
    layout.add(std::make_unique<juce::AudioParameterBool>("panningLfoActive", "Panning LFO Active", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteLockActive", "Note Lock", false));

    layout.add(std::make_unique<juce::AudioParameterBool>("noteC", "C", true));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteD", "D", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteE", "E", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteF", "F", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteG", "G", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteA", "A", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteB", "B", false));

    layout.add(std::make_unique<juce::AudioParameterFloat>("wetGain", "Wet Gain", 0.0f, 2.0f, 1.0f));

    // Dropdown choice for the play mode
    layout.add(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray{ "Random", "Up", "Down" }, 0));

    return layout; // Pass the completed layout back to JUCE
}

void BandpassModulatorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // 'ProcessSpec' is a struct that bundles the sample rate and buffer size for DSP algorithms.
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();

    // Iterate through our filter array using a Reference (&).
    // '&f' means we are modifying the actual filter in the array, not a copy of it.
    for (auto& f : filters)
    {
        f.prepare(spec); // Initialize the filter with current sample rate
        f.setType(juce::dsp::StateVariableTPTFilterType::bandpass); // Set the algorithm to Bandpass
    }

    // Reset smoothers and counters to avoid loud "pops" when audio starts
    smoothedPanning.reset(sampleRate, 0.02); // 0.02s ramp to smooth out panning jumps
    timeInCurrentState = 0.0;
    isGliding = false;
    currentNoteIndex = 0;
}

void BandpassModulatorAudioProcessor::releaseResources() {}
// Called when playback stops

void BandpassModulatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
//==============================================================================
// processblock is the heart of the plugin
// This runs on the REAL-TIME audio thread
// NO memory allocation, NO locks, NO UI access
{
    // 'ScopedNoDenormals' prevents the CPU from slowing down when dealing with extremely tiny numbers near zero.
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any output channels that aren't being used.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // --- PARAMETER CACHING ---
    // The '*' is the Dereference Operator. getRawParameterValue returns an address (Pointer). 
    // '*' goes to that address and pulls the actual value out.
    // getRawParameterValue() returns std::atomic<float>*
    // Dereferencing ONCE per block is fast and safe
    float minF = *apvts.getRawParameterValue("minFreq");
    float maxF = *apvts.getRawParameterValue("maxFreq");
    float gTime = *apvts.getRawParameterValue("glideTime");
    float sTime = *apvts.getRawParameterValue("stayTime");
    float width = *apvts.getRawParameterValue("width");
    float manualPan = *apvts.getRawParameterValue("panning");
    float dryWet = *apvts.getRawParameterValue("dryWet");
    float wetGain = *apvts.getRawParameterValue("wetGain");
    bool lfoActive = *apvts.getRawParameterValue("panningLfoActive") > 0.5f;
    bool noteLock = *apvts.getRawParameterValue("noteLockActive") > 0.5f;
    int mode = (int)*apvts.getRawParameterValue("mode");

    if (maxF < minF) std::swap(minF, maxF); // Ensure range is valid
    double sampleDuration = 1.0 / getSampleRate(); // Length of one sample in seconds

    // Musical frequency data
    float noteBaseFrequencies[] = { 261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f };
    juce::String noteIDs[] = { "noteC", "noteD", "noteE", "noteF", "noteG", "noteA", "noteB" };

    // --- SAMPLE LOOP ---
    // This loop runs for every single audio sample (e.g. 44,100 times per second).
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        timeInCurrentState += sampleDuration; // Increment timer
        if (lfoActive) panTimeCounter += sampleDuration;

        // Calculate how long the filter movement should take
        float filterGlideDuration = (mode == 0) ? gTime : (noteLock ? sTime * 0.2f : sTime);
        if (filterGlideDuration < 0.0001f) filterGlideDuration = 0.0001f; // Safety check

        // --- STEP TRIGGER ---
        // If the 'Stay' time is up, pick a new target frequency.
        if (!isGliding && timeInCurrentState >= sTime)
        {
            isGliding = true;
            timeInCurrentState = 0.0;
            startCutoff = currentCutoff; // Remember where we are starting the move from

            if (lfoActive)
            {
                isGlidingPan = true;
                panTimeCounter = 0.0;
                startPanValue = currentPanValue;
                targetPanValue = (random.nextFloat() * 2.0f) - 1.0f; // Random value between -1 and 1
            }

            // Note Selection Logic, build allowed note frequency list
            juce::Array<float> allowedFreqs;
            if (noteLock) {
                for (int i = 0; i < 7; ++i) {
                    if (*apvts.getRawParameterValue(noteIDs[i]) > 0.5f) {
                        for (int oct = 1; oct <= 10; ++oct) {
                            // freq = BaseFreq * 2 ^ (Octave Offset)
                            float f = noteBaseFrequencies[i] * std::pow(2.0f, (float)oct - 4.0f);
                            if (f >= minF && f <= maxF) allowedFreqs.add(f);
                        }
                    }
                }
                allowedFreqs.sort();
            }

            // Target Assignment
            if (mode == 0) { // RANDOM
                if (noteLock && allowedFreqs.size() > 0)
                    targetCutoff = allowedFreqs[random.nextInt(allowedFreqs.size())];
                else
                    targetCutoff = minF + (random.nextFloat() * (maxF - minF));
            }
            else { // UP / DOWN Sequence
                if (noteLock && allowedFreqs.size() > 0) {
                    // '%' is the Modulo operator. It loops the index back to 0 if it exceeds the array size.
                    if (mode == 1) currentNoteIndex = (currentNoteIndex + 1) % allowedFreqs.size();
                    else currentNoteIndex = (currentNoteIndex - 1 + allowedFreqs.size()) % allowedFreqs.size();
                    targetCutoff = allowedFreqs[currentNoteIndex];
                }
                else {
                    if (mode == 1) {
                        if (currentCutoff >= maxF - 5.0f) { currentCutoff = minF; startCutoff = minF; }
                        targetCutoff = maxF;
                    }
                    else {
                        if (currentCutoff <= minF + 5.0f) { currentCutoff = maxF; startCutoff = maxF; }
                        targetCutoff = minF;
                    }
                }
            }
        }

        // --- INTERPOLATION (The Glide) ---
        if (isGliding)
        {
            float progress = (float)(timeInCurrentState / filterGlideDuration);
            if (progress >= 1.0f) {
                currentCutoff = targetCutoff;
                if (timeInCurrentState >= sTime) isGliding = false;
            }
            else {
                // Linear interpolation math: current = start + (distance * progress)
                currentCutoff = startCutoff + (targetCutoff - startCutoff) * progress;
            }
        }

        // --- PANNING INTERPOLATION ---
        if (lfoActive)
        {
            if (isGlidingPan)
            {
                float panProgress = (float)(panTimeCounter / (gTime + 0.0001f));
                if (panProgress >= 1.0f) {
                    currentPanValue = targetPanValue;
                    isGlidingPan = false;
                }
                else {
                    currentPanValue = startPanValue + (targetPanValue - startPanValue) * panProgress;
                }
            }
        }
        else {
            currentPanValue = manualPan; // Follow the manual slider if LFO is off
            isGlidingPan = false;
        }

        // --- DSP EXECUTION ---
        for (auto& f : filters) {
            f.setCutoffFrequency(currentCutoff);
            f.setResonance(width);
        }

        // Constant Power Panning calculation
        // Uses Sine and Cosine to ensure the volume doesn't drop in the center.

        smoothedPanning.setTargetValue(currentPanValue);
        float cp = smoothedPanning.getNextValue();
        float leftGain = std::cos((cp + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
        float rightGain = std::sin((cp + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

        // --- FINAL CHANNEL PROCESSING ---
        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            // getWritePointer: Access the actual memory block where audio is stored.
            float* channelData = buffer.getWritePointer(channel);

            // Apply the filter to the current sample
            float processed = filters[channel].processSample(channel, channelData[sample]);

            // Apply panning gain
            processed *= (channel == 0) ? leftGain : rightGain;

            // Final Dry/Wet Mix: (Wet * Volume) + (Dry * InverseVolume)
            channelData[sample] = (processed * wetGain * dryWet) + (channelData[sample] * (1.0f - dryWet));
        }
    }
}

//==============================================================================
//==============================================================================
// --- STANDARD JUCE BOILERPLATE FUNCTIONS ---
// These functions are required by the JUCE AudioProcessor base class
// Most DAWs call these to ask the plugin what it can and cannot do

// Returns the plugin name as shown in the DAW
// JucePlugin_Name is a macro generated by Projucer / CMake
const juce::String BandpassModulatorAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

// Tells the host whether this plugin expects MIDI input
// Returning false means: audio-only plugin
bool BandpassModulatorAudioProcessor::acceptsMidi() const
{
    return false;
}

// Tells the host whether this plugin outputs MIDI
// Returning false means it does not generate MIDI notes or CCs
bool BandpassModulatorAudioProcessor::producesMidi() const
{
    return false;
}

// True only for plugins that are *pure* MIDI effects (no audio processing)
bool BandpassModulatorAudioProcessor::isMidiEffect() const
{
    return false;
}

// Tail length = how long the plugin keeps producing sound
// after input audio stops (e.g. reverb tails, delays)
// 0.0 means "no tail", audio stops immediately
double BandpassModulatorAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

// Number of programs (presets) exposed to the host
// Modern plugins usually return 1 and handle presets internally
int BandpassModulatorAudioProcessor::getNumPrograms()
{
    return 1;
}

// Returns the currently active program index
// Since we only have 1 program, this is always 0
int BandpassModulatorAudioProcessor::getCurrentProgram()
{
    return 0;
}

// Allows the host to switch programs
// Empty because program switching is not used
void BandpassModulatorAudioProcessor::setCurrentProgram(int index)
{
}

// Returns the name of a program for display in the DAW
// '{}' returns an empty juce::String
const juce::String BandpassModulatorAudioProcessor::getProgramName(int index)
{
    return {};
}

// Allows renaming programs from the host
// Not implemented because we don't use programs
void BandpassModulatorAudioProcessor::changeProgramName(
    int index,
    const juce::String& newName)
{
}

//==============================================================================
// --- STATE MANAGEMENT (PRESETS / PROJECT SAVE) --- most of this is preimplemented
// This is CRUCIAL: without this, the plugin would reset when reopening a project

// Called by the DAW when saving the project or preset
void BandpassModulatorAudioProcessor::getStateInformation(
    juce::MemoryBlock& destData)
{
    // Copy the entire APVTS state (all parameters + values)
    auto state = apvts.copyState();

    // Convert the ValueTree into XML
    // XML is human-readable and very robust
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    // Serialize the XML into a binary memory block
    // The DAW stores this blob inside the project file
    copyXmlToBinary(*xml, destData);
}

// Called by the DAW when loading a project or preset
void BandpassModulatorAudioProcessor::setStateInformation(
    const void* data,
    int sizeInBytes)
{
    // Convert the binary blob back into XML
    std::unique_ptr<juce::XmlElement> xmlState(
        getXmlFromBinary(data, sizeInBytes));

    // Safety checks:
    // - xmlState is valid
    // - XML tag matches our APVTS root
    if (xmlState != nullptr &&
        xmlState->hasTagName(apvts.state.getType()))
    {
        // Replace the entire APVTS state
        // This automatically updates:
        // - parameters
        // - UI attachments
        // - automation values
        apvts.replaceState(
            juce::ValueTree::fromXml(*xmlState));
    }
}

//==============================================================================
// --- EDITOR CREATION ---
// This tells JUCE which UI class belongs to this processor

// Creates a NEW editor instance
// 'new' is safe here: JUCE owns and deletes the editor
juce::AudioProcessorEditor*
BandpassModulatorAudioProcessor::createEditor()
{
    // '*this' passes a reference to the processor into the editor
    return new BandpassModulatorAudioProcessorEditor(*this);
}

// Tells the host that this plugin DOES have a GUI
bool BandpassModulatorAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
// --- BUS LAYOUT VALIDATION ---
// Only compiled if the plugin does NOT use fixed channel layouts

#ifndef JucePlugin_PreferredChannelConfigurations

// Called by the host to ask:
// "Is this input/output channel layout allowed?"
bool BandpassModulatorAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    // Get the input and output channel sets
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    // Reject anything that is not mono or stereo
    if (in != juce::AudioChannelSet::mono() &&
        in != juce::AudioChannelSet::stereo())
        return false;

    // Require input and output to have the same channel count
    return in == out;
}

#endif

//==============================================================================
// --- PLUGIN ENTRY POINT ---
// This is the factory function the DAW calls to create the plugin

// JUCE_CALLTYPE ensures the correct calling convention
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    // Create and return a new processor instance
    // The host owns this object
    return new BandpassModulatorAudioProcessor();
}