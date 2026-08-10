#include "PluginProcessor.h"
#include "PluginEditor.h"

AnyFMAudioProcessor::AnyFMAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withInput("Sidechain", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter(modulationIndex = new juce::AudioParameterFloat(
        "modIndex", "Modulation Depth",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 0.1f, 0.5f),
        50.0f));

    addParameter(modulatorGain = new juce::AudioParameterFloat(
        "modGain", "Modulator Gain",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f),
        1.0f));

    addParameter(carrierGain = new juce::AudioParameterFloat(
        "carGain", "Carrier Gain",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.01f),
        1.0f));

    addParameter(dryWet = new juce::AudioParameterFloat(
        "dryWet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.5f));

    addParameter(fmType = new juce::AudioParameterChoice(
        "fmType", "Modulation Type",
        juce::StringArray{ "Phase Mod (Delay-FM)", "Ring Mod (AM)" },
        0));
}

AnyFMAudioProcessor::~AnyFMAudioProcessor() {}

const juce::String AnyFMAudioProcessor::getName() const { return JucePlugin_Name; }
bool AnyFMAudioProcessor::acceptsMidi() const { return false; }
bool AnyFMAudioProcessor::producesMidi() const { return false; }
bool AnyFMAudioProcessor::isMidiEffect() const { return false; }
double AnyFMAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int AnyFMAudioProcessor::getNumPrograms() { return 1; }
int AnyFMAudioProcessor::getCurrentProgram() { return 0; }
void AnyFMAudioProcessor::setCurrentProgram(int index) {}
const juce::String AnyFMAudioProcessor::getProgramName(int index) { return {}; }
void AnyFMAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void AnyFMAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Buffer Gr��e fest auf 4096 (Zweierpotenz ist performanter f�r Modulo)
    int bufferSize = 4096;
    delayBuffer.setSize(2, bufferSize);
    delayBuffer.clear();
    writePosition = 0;

    setLatencySamples(1024);

    modulationIndexSmoothed.resize(2);
    for (auto& smoother : modulationIndexSmoothed)
    {
        smoother.reset(sampleRate, 0.02);
    }
}

void AnyFMAudioProcessor::releaseResources() {}

bool AnyFMAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo() &&
        layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AnyFMAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Sidechain (Modulator) Bus holen
    auto modulatorBuffer = getBusBuffer(buffer, true, 1);
    const bool hasSidechain = (modulatorBuffer.getNumChannels() > 0);

    // Parameter Caching
    const float carGain = carrierGain->get();
    const float modGain = modulatorGain->get();
    const float wetAmount = dryWet->get();
    const float dryAmount = 1.0f - wetAmount;
    const float currentModIndex = modulationIndex->get();
    const int mode = fmType->getIndex();
    const int delayBufferLength = delayBuffer.getNumSamples();

    // Wir loopen �ber die OUTPUT Kan�le (meistens 2), damit Mono-zu-Stereo klappt
    for (int channel = 0; channel < totalNumOutputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        // Falls der Input Mono ist, nutzen wir f�r den rechten Output auch Input-Kanal 0
        const int inputChannel = juce::jmin(channel, totalNumInputChannels - 1);
        const float* inputReadPtr = (inputChannel >= 0) ? buffer.getReadPointer(inputChannel) : nullptr;

        // Modulator Kanal (Sidechain)
        const float* modData = nullptr;
        if (hasSidechain)
        {
            int modChannel = juce::jmin(channel, modulatorBuffer.getNumChannels() - 1);
            modData = modulatorBuffer.getReadPointer(modChannel);
        }

        float* delayData = delayBuffer.getWritePointer(juce::jmin(channel, delayBuffer.getNumChannels() - 1));
        auto& smoother = modulationIndexSmoothed[juce::jmin(channel, (int)modulationIndexSmoothed.size() - 1)];
        smoother.setTargetValue(currentModIndex);

        int localWritePos = writePosition;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            // Sicherstellen, dass wir Daten zum Lesen haben
            float inputSample = (inputReadPtr != nullptr) ? inputReadPtr[sample] : 0.0f;
            float carrierSignal = inputSample * carGain;

            // In Delay-Buffer schreiben
            delayData[localWritePos] = carrierSignal;

            float modSample = (modData != nullptr) ? modData[sample] * modGain : 0.0f;
            float wetSignal = 0.0f;

            if (mode == 0) // Phase Modulation (Delay-FM)
            {
                float smoothDepth = smoother.getNextValue();
                float baseOffset = 1024.0f; // Fester Delay-Offset

                // Leseposition berechnen
                float readPos = (float)localWritePos - baseOffset + (modSample * smoothDepth);

                // Wrap-Around (schnell)
                while (readPos < 0.0f) readPos += (float)delayBufferLength;
                while (readPos >= (float)delayBufferLength) readPos -= (float)delayBufferLength;

                // Interpolation
                int indexA = (int)readPos;
                int indexB = (indexA + 1) % delayBufferLength;
                float frac = readPos - (float)indexA;

                wetSignal = (delayData[indexA] * (1.0f - frac)) + (delayData[indexB] * frac);

                // Anti-Ghosting / Gate: Wenn kein Input, dann kein Output
                // Hier mit einem sehr kleinen Fade, um Knacksen zu vermeiden
                if (std::abs(carrierSignal) < 0.000001f) wetSignal = 0.0f;
            }
            else // Ring Modulation
            {
                wetSignal = carrierSignal * (modSample * 2.0f);
                smoother.skip(1); // Smoother im Sync halten
            }

            // Mix zur�ck in den Buffer schreiben
            channelData[sample] = (carrierSignal * dryAmount) + (wetSignal * wetAmount);

            localWritePos = (localWritePos + 1) % delayBufferLength;
        }
    }

    // Schreibposition erst NACHDEM alle Kan�le bearbeitet wurden global verschieben
    writePosition = (writePosition + numSamples) % delayBufferLength;

    // Metering
    carrierLevel.store(buffer.getMagnitude(0, 0, numSamples));
    modulatorLevel.store(hasSidechain ? modulatorBuffer.getMagnitude(0, 0, numSamples) : 0.0f);
    outputLevel.store(buffer.getMagnitude(0, 0, numSamples));

    // Falls die DAW mehr Output-Kan�le will als wir bearbeitet haben: Clearen
    for (int i = totalNumOutputChannels; i < getTotalNumOutputChannels(); ++i)
        buffer.clear(i, 0, numSamples);
}

// Dummy Implementierungen der alten Funktionen
void AnyFMAudioProcessor::performPhaseModulation(juce::AudioBuffer<float>&, const juce::AudioBuffer<float>&, int) {}
void AnyFMAudioProcessor::performFrequencyModulation(juce::AudioBuffer<float>&, const juce::AudioBuffer<float>&, int) {}
void AnyFMAudioProcessor::updateMeters(const juce::AudioBuffer<float>&, const juce::AudioBuffer<float>&, const juce::AudioBuffer<float>&) {}

bool AnyFMAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* AnyFMAudioProcessor::createEditor() { return new AnyFMAudioProcessorEditor(*this); }

void AnyFMAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = juce::ValueTree("AnyFMState");
    state.setProperty("modIndex", modulationIndex->get(), nullptr);
    state.setProperty("modGain", modulatorGain->get(), nullptr);
    state.setProperty("carGain", carrierGain->get(), nullptr);
    state.setProperty("dryWet", dryWet->get(), nullptr);
    state.setProperty("fmType", fmType->getIndex(), nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AnyFMAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName("AnyFMState"))
    {
        auto state = juce::ValueTree::fromXml(*xmlState);
        *modulationIndex = state.getProperty("modIndex", 50.0f);
        *modulatorGain = state.getProperty("modGain", 1.0f);
        *carrierGain = state.getProperty("carGain", 1.0f);
        *dryWet = state.getProperty("dryWet", 0.5f);
        *fmType = state.getProperty("fmType", 0);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AnyFMAudioProcessor(); }