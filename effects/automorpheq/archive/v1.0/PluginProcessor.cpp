#include "PluginProcessor.h"
#include "PluginEditor.h"

AutoMorphEQAudioProcessor::AutoMorphEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    for (auto& phase : oscPhases)
        phase = 0.0;
}

AutoMorphEQAudioProcessor::~AutoMorphEQAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoMorphEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>("wet_only", "Wet Only", false));

    for (int i = 0; i < 7; ++i)
    {
        juce::String suffix = "_" + juce::String(i);
        juce::String labelNum = juce::String(i + 1);

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "start_freq" + suffix, "Start Freq " + labelNum,
            juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 1000.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "end_freq" + suffix, "End Freq " + labelNum,
            juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 2000.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "start_vol" + suffix, "Start Vol " + labelNum,
            juce::NormalisableRange<float>(0.01f, 10.0f, 0.01f), 1.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "end_vol" + suffix, "End Vol " + labelNum,
            juce::NormalisableRange<float>(0.01f, 10.0f, 0.01f), 1.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "q" + suffix, "Q " + labelNum,
            juce::NormalisableRange<float>(0.5f, 100.0f, 0.1f, 0.3f), 1.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "speed" + suffix, "Speed " + labelNum,
            juce::NormalisableRange<float>(0.00001f, 10.0f, 0.00001f, 0.25f), 0.0f));

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "pos" + suffix, "Position " + labelNum,
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    }

    return layout;
}

void AutoMorphEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
    spec.numChannels = (juce::uint32)getTotalNumOutputChannels();

    for (auto& band : filters)
    {
        band.prepare(spec);
        band.reset();
    }
}

void AutoMorphEQAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AutoMorphEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

void AutoMorphEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // STORE DRY SIGNAL FOR DELTA (WET ONLY) MODE
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(buffer);

    // Loop through each of the 7 bands to update coefficients
    for (int i = 0; i < 7; ++i)
    {
        juce::String suffix = "_" + juce::String(i);

        float startFreq = apvts.getRawParameterValue("start_freq" + suffix)->load();
        float endFreq = apvts.getRawParameterValue("end_freq" + suffix)->load();
        float startVol = apvts.getRawParameterValue("start_vol" + suffix)->load();
        float endVol = apvts.getRawParameterValue("end_vol" + suffix)->load();
        float speed = apvts.getRawParameterValue("speed" + suffix)->load();
        float manualPos = apvts.getRawParameterValue("pos" + suffix)->load();
        float bandQ = apvts.getRawParameterValue("q" + suffix)->load();

        float morphFactor = 0.0f;

        if (speed <= 0.001f)
        {
            morphFactor = manualPos / 100.0f;
        }
        else
        {
            double secondsInBlock = (double)buffer.getNumSamples() / currentSampleRate;
            oscPhases[i] += secondsInBlock / speed;
            if (oscPhases[i] > 1.0) oscPhases[i] -= 1.0;

            // Sine Morph Transformation
            morphFactor = (float)(0.5 * (1.0 - std::cos(juce::MathConstants<double>::twoPi * oscPhases[i])));
        }

        float currentFreq = juce::jlimit(20.0f, 20000.0f, startFreq + (endFreq - startFreq) * morphFactor);
        float currentVol = juce::jmax(0.0001f, startVol + (endVol - startVol) * morphFactor);

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate,
            currentFreq,
            bandQ,
            currentVol);

        *filters[i].filterLeft.coefficients = *coeffs;
        *filters[i].filterRight.coefficients = *coeffs;
    }

    // Process the audio through the filters
    juce::dsp::AudioBlock<float> block(buffer);
    for (int i = 0; i < 7; ++i)
    {
        filters[i].filterLeft.process(juce::dsp::ProcessContextReplacing<float>(block.getSingleChannelBlock(0)));
        if (totalNumInputChannels > 1)
            filters[i].filterRight.process(juce::dsp::ProcessContextReplacing<float>(block.getSingleChannelBlock(1)));
    }

    bool wetOnly = apvts.getRawParameterValue("wet_only")->load() > 0.5f;
    if (wetOnly)
    {
        for (int ch = 0; ch < totalNumInputChannels; ++ch)
        {
            // Subtract dry from wet: Output = Processed - Original
            buffer.addFrom(ch, 0, dryBuffer, ch, 0, buffer.getNumSamples(), -1.0f);
        }
    }

    // Hard Limiter / Safety
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer(ch);
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            data[s] = juce::jlimit(-1.0f, 1.0f, data[s]);
    }
}

bool AutoMorphEQAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AutoMorphEQAudioProcessor::createEditor()
{
    return new AutoMorphEQAudioProcessorEditor(*this);
}

void AutoMorphEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AutoMorphEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

const juce::String AutoMorphEQAudioProcessor::getName() const { return "AutoMorphEQ"; }
bool AutoMorphEQAudioProcessor::acceptsMidi() const { return false; }
bool AutoMorphEQAudioProcessor::producesMidi() const { return false; }
bool AutoMorphEQAudioProcessor::isMidiEffect() const { return false; }
double AutoMorphEQAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int AutoMorphEQAudioProcessor::getNumPrograms() { return 1; }
int AutoMorphEQAudioProcessor::getCurrentProgram() { return 0; }
void AutoMorphEQAudioProcessor::setCurrentProgram(int index) {}
const juce::String AutoMorphEQAudioProcessor::getProgramName(int index) { return {}; }
void AutoMorphEQAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AutoMorphEQAudioProcessor();
}