/*
    PluginProcessor.cpp  -  83ChorusVerb

    GPLv3.
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"

ChorusVerbAudioProcessor::ChorusVerbAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "83ChorusVerb", createLayout())
{
    fx_.bindParameters(apvts);
}

ChorusVerbAudioProcessor::~ChorusVerbAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout ChorusVerbAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    cv83::FxChain::addParameters(layout);
    return layout;
}

void ChorusVerbAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    fx_.prepare(sampleRate, juce::jmax(1, samplesPerBlock));
    scratchR_.assign((size_t)juce::jmax(1, samplesPerBlock), 0.0f);
}

bool ChorusVerbAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Input must mirror output - this is a plain stereo (or mono) insert effect.
    return layouts.getMainInputChannelSet() == mainOut;
}

void ChorusVerbAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();   // no MIDI in or out - this is a pure audio effect

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    if (numCh >= 2)
    {
        fx_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    }
    else if (numCh == 1)
    {
        // The chorus/phaser/reverb all widen a source, so on a mono bus we run
        // the chain against a scratch right channel seeded from the same
        // input, then fold the stereo result back down to mono.
        if ((int)scratchR_.size() < numSamples)
            scratchR_.assign((size_t)numSamples, 0.0f);
        juce::FloatVectorOperations::copy(scratchR_.data(), buffer.getReadPointer(0), numSamples);

        float* l = buffer.getWritePointer(0);
        fx_.process(l, scratchR_.data(), numSamples);

        for (int n = 0; n < numSamples; ++n)
            l[n] = (l[n] + scratchR_[(size_t)n]) * 0.5f;
    }
}

void ChorusVerbAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (std::unique_ptr<juce::XmlElement> xml{ state.createXml() })
        copyXmlToBinary(*xml, dest);
}

void ChorusVerbAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (std::unique_ptr<juce::XmlElement> xml{ getXmlFromBinary(data, sizeInBytes) })
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* ChorusVerbAudioProcessor::createEditor()
{
    return new ChorusVerbAudioProcessorEditor(*this);
}

// This creates instances of the plugin - required by every JUCE plugin format
// wrapper (VST3, AU, standalone, ...).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChorusVerbAudioProcessor();
}
