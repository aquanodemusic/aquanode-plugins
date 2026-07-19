#include "PluginProcessor.h"
#include "PluginEditor.h"

BandpassModulatorAudioProcessor::BandpassModulatorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
}

BandpassModulatorAudioProcessor::~BandpassModulatorAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout BandpassModulatorAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "minFreq", "Min Frequency", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "maxFreq", "Max Frequency", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 2000.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "glideTime", "Glide", juce::NormalisableRange<float>(0.0001f, 10.0f, 0.0f, 0.4f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "stayTime", "Stay", juce::NormalisableRange<float>(0.0001f, 10.0f, 0.0f, 0.4f), 0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("panning", "Panning", -1.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("dryWet", "Dry/Wet", 0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("width", "Bandwidth", 0.1f, 10.0f, 1.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>("panningLfoActive", "Panning LFO Active", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteLockActive", "Note Lock", false));

    layout.add(std::make_unique<juce::AudioParameterBool>("noteC", "C", true));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteCsharp", "C#", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteD", "D", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteDsharp", "D#", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteE", "E", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteF", "F", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteFsharp", "F#", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteG", "G", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteGsharp", "G#", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteA", "A", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteAsharp", "A#", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("noteB", "B", false));

    layout.add(std::make_unique<juce::AudioParameterFloat>("wetGain", "Wet Gain", 0.0f, 2.0f, 1.0f));

    layout.add(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray{ "Random", "Up", "Down" }, 0));

    return layout;
}

void BandpassModulatorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();

    for (auto& f : filters)
    {
        f.prepare(spec);
        f.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
    }

    smoothedPanning.reset(sampleRate, 0.02);
    timeInCurrentState = 0.0;
    isGliding = false;
    currentNoteIndex = 0;
}

void BandpassModulatorAudioProcessor::releaseResources() {}

void BandpassModulatorAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

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

    if (maxF < minF) std::swap(minF, maxF);
    double sampleDuration = 1.0 / getSampleRate();

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        timeInCurrentState += sampleDuration;
        if (lfoActive) panTimeCounter += sampleDuration;

        float filterGlideDuration = (mode == 0) ? gTime : (noteLock ? sTime * 0.2f : sTime);
        if (filterGlideDuration < 0.0001f) filterGlideDuration = 0.0001f;

        if (!isGliding && timeInCurrentState >= sTime)
        {
            isGliding = true;
            timeInCurrentState = 0.0;
            startCutoff = currentCutoff;

            if (lfoActive)
            {
                isGlidingPan = true;
                panTimeCounter = 0.0;
                startPanValue = currentPanValue;
                targetPanValue = (random.nextFloat() * 2.0f) - 1.0f;
            }

            std::vector<float> allowedFreqs;
            if (noteLock) {
                allowedFreqs = getActiveNoteFrequencies(minF, maxF);
            }

            if (mode == 0) // RANDOM MODE
            {
                if (noteLock && !allowedFreqs.empty()) {
                    targetCutoff = allowedFreqs[random.nextInt((int)allowedFreqs.size())];
                }
                else {
                    targetCutoff = minF + (random.nextFloat() * (maxF - minF));
                }
            }
            else // UP / DOWN MODES
            {
                if (noteLock && !allowedFreqs.empty())
                {
                    if (mode == 1) // UP
                        currentNoteIndex = (currentNoteIndex + 1) % allowedFreqs.size();
                    else // DOWN
                        currentNoteIndex = (currentNoteIndex - 1 + (int)allowedFreqs.size()) % allowedFreqs.size();

                    targetCutoff = allowedFreqs[currentNoteIndex];
                }
                else
                {
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

        if (isGliding)
        {
            float progress = (float)(timeInCurrentState / filterGlideDuration);
            if (progress >= 1.0f) {
                currentCutoff = targetCutoff;
                if (timeInCurrentState >= sTime) isGliding = false;
            }
            else {
                currentCutoff = startCutoff + (targetCutoff - startCutoff) * progress;
            }
        }

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
            currentPanValue = manualPan;
            isGlidingPan = false;
        }

        for (auto& f : filters) {
            f.setCutoffFrequency(currentCutoff);
            f.setResonance(width);
        }

        smoothedPanning.setTargetValue(currentPanValue);
        float cp = smoothedPanning.getNextValue();
        float leftGain = std::cos((cp + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
        float rightGain = std::sin((cp + 1.0f) * juce::MathConstants<float>::pi * 0.25f);

        for (int channel = 0; channel < totalNumInputChannels; ++channel)
        {
            float* channelData = buffer.getWritePointer(channel);
            float processed = filters[channel].processSample(channel, channelData[sample]);

            processed *= (channel == 0) ? leftGain : rightGain;

            channelData[sample] = (processed * wetGain * dryWet) + (channelData[sample] * (1.0f - dryWet));
        }
    }
}


std::vector<float> BandpassModulatorAudioProcessor::getActiveNoteFrequencies(float minFreq, float maxFreq)
{
    std::vector<float> allowed;

    juce::String noteIDs[] = {
        "noteC", "noteCsharp", "noteD", "noteDsharp", "noteE", "noteF",
        "noteFsharp", "noteG", "noteGsharp", "noteA", "noteAsharp", "noteB"
    };

    for (int i = 0; i < 12; ++i)
    {
        if (*apvts.getRawParameterValue(noteIDs[i]) > 0.5f)
        {
            for (int octave = 0; octave <= 10; ++octave)
            {
                float freq = getFrequencyForNoteName(i, octave);

                if (freq >= minFreq && freq <= maxFreq)
                {
                    allowed.push_back(freq);
                }
            }
        }
    }

    std::sort(allowed.begin(), allowed.end());
    return allowed;
}

float BandpassModulatorAudioProcessor::getFrequencyForNoteName(int noteIndex, int octave)
{
    int semitonesFromA4 = (noteIndex - 9) + (octave - 4) * 12;
    return 440.0f * std::pow(2.0f, semitonesFromA4 / 12.0f);
}


const juce::String BandpassModulatorAudioProcessor::getName() const { return JucePlugin_Name; }
bool BandpassModulatorAudioProcessor::acceptsMidi() const { return false; }
bool BandpassModulatorAudioProcessor::producesMidi() const { return false; }
bool BandpassModulatorAudioProcessor::isMidiEffect() const { return false; }
double BandpassModulatorAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int BandpassModulatorAudioProcessor::getNumPrograms() { return 1; }
int BandpassModulatorAudioProcessor::getCurrentProgram() { return 0; }
void BandpassModulatorAudioProcessor::setCurrentProgram(int index) {}
const juce::String BandpassModulatorAudioProcessor::getProgramName(int index) { return {}; }
void BandpassModulatorAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void BandpassModulatorAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void BandpassModulatorAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorEditor* BandpassModulatorAudioProcessor::createEditor()
{
    return new BandpassModulatorAudioProcessorEditor(*this);
}

bool BandpassModulatorAudioProcessor::hasEditor() const
{
    return true;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BandpassModulatorAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}
#endif

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BandpassModulatorAudioProcessor();
}