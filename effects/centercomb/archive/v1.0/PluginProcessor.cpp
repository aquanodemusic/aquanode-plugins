#include "PluginProcessor.h"
#include "PluginEditor.h"

CenterCombAudioProcessor::CenterCombAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
}

CenterCombAudioProcessor::~CenterCombAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
CenterCombAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain", -24.0f, 24.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "freq", "Center Freq",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f),
        1000.0f));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "amount", "Filter Count", 0, 64, 2));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "spreadHz", "Spread (Hz)",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 0.01f, 0.3f),
        100.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "spreadRatio", "Spread (Ratio)",
        juce::NormalisableRange<float>(1.01f, 10.0f, 0.0001f, 0.5f),
        1.05f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "q", "Q",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 0.1f, 0.3f),
        10.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "damp", "Dampening", 0.0f, 1.0f, 0.5f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "linear", "Linear View", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "spreadMode", "Spread Mode", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "wetOnly", "Wet Only", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "hardLimiter", "Hard Limiter", false));

    return layout;
}


void CenterCombAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 1 };

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < maxFilters; ++i)
            filters[ch][i].prepare(spec);
}

void CenterCombAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    float mainGainDB = apvts.getRawParameterValue("gain")->load();
    float centerFreq = apvts.getRawParameterValue("freq")->load();
    int pairs = static_cast<int>(apvts.getRawParameterValue("amount")->load());
    float q = apvts.getRawParameterValue("q")->load();
    float damp = apvts.getRawParameterValue("damp")->load();

    int totalActiveFilters = 1 + (pairs * 2);
    const float lowLimit = 20.0f;
    const float highLimit = static_cast<float>(currentSampleRate) * 0.49f;

    bool isMultiplicative = apvts.getRawParameterValue("spreadMode")->load() > 0.5f;
    bool wetOnly = apvts.getRawParameterValue("wetOnly")->load() > 0.5f;
    bool hardLimiter = apvts.getRawParameterValue("hardLimiter")->load() > 0.5f;

    float spread = isMultiplicative
        ? apvts.getRawParameterValue("spreadRatio")->load()
        : apvts.getRawParameterValue("spreadHz")->load();
    spread = juce::jmax(spread, 0.0001f);

    // --- 1) Make a copy of dry signal for wet processing ---
    juce::AudioBuffer<float> wetBuffer;
    wetBuffer.makeCopyOf(buffer);

    // --- 2) Update filter coefficients ---
    for (int i = 0; i < maxFilters; ++i)
    {
        float filterFreq = centerFreq;
        float filterGain = 0.0f;

        if (i < totalActiveFilters)
        {
            int offsetIdx = i - pairs;

            if (isMultiplicative)
                filterFreq = centerFreq * std::pow(spread, static_cast<float>(offsetIdx));
            else
                filterFreq = centerFreq + (static_cast<float>(offsetIdx) * spread);

            if (filterFreq >= lowLimit && filterFreq <= highLimit)
            {
                float weight = 1.0f - (damp * (std::abs(offsetIdx) / static_cast<float>(pairs + 1)));
                filterGain = mainGainDB * weight;
            }
            else
            {
                filterGain = 0.0f;
                filterFreq = juce::jlimit(lowLimit, highLimit, filterFreq);
            }
        }

        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate, filterFreq, q, juce::Decibels::decibelsToGain(filterGain));

        for (int ch = 0; ch < 2; ++ch)
            filters[ch][i].coefficients = coeffs;
    }

    // --- 3) Process wet buffer ---
    for (int ch = 0; ch < wetBuffer.getNumChannels(); ++ch)
    {
        auto* wetData = wetBuffer.getWritePointer(ch);
        for (int sample = 0; sample < wetBuffer.getNumSamples(); ++sample)
        {
            float tempSample = wetData[sample];

            for (int i = 0; i < totalActiveFilters; ++i)
                tempSample = filters[ch][i].processSample(tempSample);

            wetData[sample] = tempSample;
        }
    }

    // --- 4) WetOnly mode: subtract dry from wet ---
    if (wetOnly)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* dryData = buffer.getReadPointer(ch);
            auto* wetData = wetBuffer.getWritePointer(ch);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                wetData[sample] -= dryData[sample];
        }
    }

    if (hardLimiter)
    {
        // --- 6) Hard limiter for wet buffer ---
        const float maxLevel = 0.90f; // peak limit
        for (int ch = 0; ch < wetBuffer.getNumChannels(); ++ch)
        {
            auto* wetData = wetBuffer.getWritePointer(ch);
            for (int sample = 0; sample < wetBuffer.getNumSamples(); ++sample)
                wetData[sample] = juce::jlimit(-maxLevel, maxLevel, wetData[sample]);
        }
    }

    // --- 5) Copy wet back to main buffer ---
    buffer.makeCopyOf(wetBuffer);
}


// --- Standard Boilerplate ---
void CenterCombAudioProcessor::releaseResources() {}
#ifndef JucePlugin_PreferredChannelConfigurations
bool CenterCombAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() && !layouts.getMainInputChannelSet().isDisabled();
}
#endif
bool CenterCombAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* CenterCombAudioProcessor::createEditor() { return new CenterCombAudioProcessorEditor(*this); }
const juce::String CenterCombAudioProcessor::getName() const { return "CenterComb"; }
bool CenterCombAudioProcessor::acceptsMidi() const { return false; }
bool CenterCombAudioProcessor::producesMidi() const { return false; }
bool CenterCombAudioProcessor::isMidiEffect() const { return false; }
double CenterCombAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int CenterCombAudioProcessor::getNumPrograms() { return 1; }
int CenterCombAudioProcessor::getCurrentProgram() { return 0; }
void CenterCombAudioProcessor::setCurrentProgram(int index) {}
const juce::String CenterCombAudioProcessor::getProgramName(int index) { return {}; }
void CenterCombAudioProcessor::changeProgramName(int index, const juce::String& newName) {}
void CenterCombAudioProcessor::getStateInformation(juce::MemoryBlock& d) { copyXmlToBinary(*apvts.copyState().createXml(), d); }
void CenterCombAudioProcessor::setStateInformation(const void* d, int s) {
    auto xml = getXmlFromBinary(d, s);
    if (xml != nullptr && xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CenterCombAudioProcessor(); }