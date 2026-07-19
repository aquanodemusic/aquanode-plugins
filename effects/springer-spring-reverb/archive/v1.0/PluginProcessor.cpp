#include "PluginProcessor.h"
#include "PluginEditor.h"
//==============================================================================
// NESTED STRUCT IMPLEMENTATIONS
// These must be scoped to SpringerAudioProcessor::SpringLine
//==============================================================================

void SpringerAudioProcessor::SpringLine::prepare(double sampleRate, int maxDelay)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 1024;
    spec.numChannels = 1;

    delay.setMaximumDelayInSamples(maxDelay);
    delay.prepare(spec);

    densityFilter.delayLine.setMaximumDelayInSamples(2000);
    densityFilter.delayLine.prepare(spec);

    delay.reset();
    densityFilter.delayLine.reset();

    lfoPhase = 0.0f;
    dampingMem = 0.0f;
    hpMem = 0.0f;
    lastOut = 0.0f;

    for (int i = 0; i < 256; ++i)
        allpasses[i].mem = 0.0f;
}

float SpringerAudioProcessor::SpringLine::process(float in, float baseDelay, float feedback, float damping,
    float lfoDepth, float lfoRate, double sr)
{
    float lfoStep = lfoRate / (float)sr;
    lfoPhase += lfoStep;
    if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
    float lfo = std::sin(lfoPhase * juce::MathConstants<float>::twoPi);

    float maxSafe = (float)delay.getMaximumDelayInSamples() - 2.0f;
    float modDelay = juce::jlimit(0.0f, maxSafe, baseDelay + (lfo * lfoDepth));

    float feedbackIn = lastOut * feedback;
    hpMem = hpMem + 0.06f * (feedbackIn - hpMem);
    feedbackIn -= hpMem;

    float inner = in + feedbackIn;
    delay.pushSample(0, inner);
    float out = delay.popSample(0, modDelay);

    out = densityFilter.process(out);

    for (int i = 0; i < numDispersionStages; ++i)
        out = allpasses[i].process(out);

    dampingMem = dampingMem + (1.0f - damping) * (out - dampingMem);
    out = dampingMem;

    out = std::tanh(out * 1.1f);

    lastOut = out;
    return out;
}

// We redefine setDelay to be safe, though it's mostly used for the Density Filter now
void SpringerAudioProcessor::SpringLine::setDelay(float samples)
{
    float maxSafe = (float)delay.getMaximumDelayInSamples() - 2.0f;
    delay.setDelay(juce::jlimit(0.0f, maxSafe, samples));
}

//==============================================================================
// AUDIO PROCESSOR IMPLEMENTATION
//==============================================================================

SpringerAudioProcessor::SpringerAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize vector sizes BEFORE calling randomize to avoid out-of-bounds access, use max numbers available
    springACoeffs.assign(256, 0.7f);
    springBCoeffs.assign(256, 0.7f);
}

SpringerAudioProcessor::~SpringerAudioProcessor()
{
}

//==============================================================================
void SpringerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int maxDelaySamples = 150000;

    springA.prepare(sampleRate, maxDelaySamples);
    springB.prepare(sampleRate, maxDelaySamples);

    // Safety: ensure vectors are sized for the full 256 range
    if (springACoeffs.size() < 256) springACoeffs.resize(256);
    if (springBCoeffs.size() < 256) springBCoeffs.resize(256);

    randomizeSprings();
}

void SpringerAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SpringerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

//==============================================================================
void SpringerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // --- 1. Load Parameters Atomically ---
    const float baseWidth = apvts.getRawParameterValue("width")->load();
    const float resonance = apvts.getRawParameterValue("resonance")->load();
    const float coupling = apvts.getRawParameterValue("coupling")->load();
    const float damping = apvts.getRawParameterValue("damping")->load();
    const float lfoRate = apvts.getRawParameterValue("lfoRate")->load();
    const float lfoDepth = apvts.getRawParameterValue("lfoDepth")->load();
    const float wet = apvts.getRawParameterValue("wet")->load();
    const bool muteDry = apvts.getRawParameterValue("muteDry")->load() > 0.5f;

    // --- 1b. Update density filters from knobs ---
    springA.densityFilter.setDelay(apvts.getRawParameterValue("densityA")->load());
    springB.densityFilter.setDelay(apvts.getRawParameterValue("densityB")->load());
    springA.densityFilter.g = 0.35f;
    springB.densityFilter.g = 0.35f;

    // --- 2. Setup Feedback/Coupling Logic ---
    float totalGainLimit = 0.95f;
    float feedback = juce::jmap(resonance, 0.0f, 1.0f, 0.0f, totalGainLimit - (coupling * 0.3f));
    float xCouple = juce::jmap(coupling, 0.0f, 1.0f, 0.0f, 0.4f);

    const int numSamples = buffer.getNumSamples();
    auto* channelDataL = buffer.getWritePointer(0);
    auto* channelDataR = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    int stages = apvts.getRawParameterValue("numStages")->load();
    stages = juce::jlimit(1, 256, stages);

    springA.numDispersionStages = stages;
    springB.numDispersionStages = stages;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float inL = channelDataL[sample];
        float inR = (channelDataR != nullptr) ? channelDataR[sample] : inL;
        float midIn = (inL + inR) * 0.5f;
        float driveIn = std::tanh(midIn * 1.5f);

        float outA = springA.process(driveIn + (springB.lastOut * xCouple),
            baseWidth,
            feedback, damping, lfoDepth, lfoRate, sr);

        float outB = springB.process(driveIn + (springA.lastOut * xCouple),
            baseWidth * 1.015f,
            feedback, damping, lfoDepth, lfoRate, sr);

        float wetL = outA * wet;
        float wetR = outB * wet;

        if (muteDry)
        {
            channelDataL[sample] = wetL;
            if (channelDataR) channelDataR[sample] = wetR;
        }
        else
        {
            channelDataL[sample] = inL + wetL;
            if (channelDataR) channelDataR[sample] = inR + wetR;
        }
    }
}


//==============================================================================
void SpringerAudioProcessor::randomizeSprings()
{
    for (int i = 0; i < 256; ++i)
    {
        // Normalize against 255.0 for a smooth curve across the whole range
        float norm = (float)i / 255.0f;
        float curve = 0.65f + (0.3f * (norm * norm));

        float varA = (random.nextFloat() * 0.04f) - 0.02f;
        float varB = (random.nextFloat() * 0.04f) - 0.02f;

        springACoeffs[i] = juce::jlimit(0.1f, 0.95f, curve + varA);
        springBCoeffs[i] = juce::jlimit(0.1f, 0.95f, curve + varB);

        // This is safe ONLY if your header defines allpasses[256]
        springA.allpasses[i].a = springACoeffs[i];
        springB.allpasses[i].a = springBCoeffs[i];
    }
}


//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SpringerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto floatParam = [](juce::String id, juce::String name, float min, float max, float def) {
        return std::make_unique<juce::AudioParameterFloat>(id, name, juce::NormalisableRange<float>(min, max), def);
        };

    // Main spring parameters
    params.push_back(floatParam("width", "Size", 0.001f, 50000.0f, 500.0f));
    params.push_back(floatParam("resonance", "Feedback", 0.0f, 1.2001f, 0.65f));
    params.push_back(floatParam("coupling", "Coupling", 0.0f, 3.001f, 0.2f));
    params.push_back(floatParam("damping", "Damping", 0.0f, 1.0f, 0.4f));
    params.push_back(floatParam("lfoRate", "Mod Rate", 0.0f, 100.0001f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "lfoDepth", "Mod Depth",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.01f, 0.3f),
        0.2f));
    params.push_back(floatParam("wet", "Wet Gain", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("muteDry", "Mute Dry", false));

    // --- NEW Density Delay Parameters ---
    params.push_back(floatParam("densityA", "Density A", 50.0f, 1000.0f, 223.0f)); // default matches old hard-coded
    params.push_back(floatParam("densityB", "Density B", 50.0f, 1000.0f, 307.0f)); // default matches old hard-coded

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "numStages", "Dispersion Stages", 1, 256, 32));

    return { params.begin(), params.end() };
}


//==============================================================================
bool SpringerAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SpringerAudioProcessor::createEditor()
{
    return new SpringerAudioProcessorEditor(*this);
}

//==============================================================================
void SpringerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    juce::String aStr, bStr;
    for (int i = 0; i < 256; ++i) { // Loop to 256
        aStr += juce::String(springACoeffs[i]) + " ";
        bStr += juce::String(springBCoeffs[i]) + " ";
    }
    xml->setAttribute("springACoeffs", aStr.trimEnd());
    xml->setAttribute("springBCoeffs", bStr.trimEnd());

    copyXmlToBinary(*xml, destData);
}

void SpringerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

        if (xmlState->hasAttribute("springACoeffs"))
        {
            juce::StringArray aVals;
            aVals.addTokens(xmlState->getStringAttribute("springACoeffs"), " ", "");
            juce::StringArray bVals;
            bVals.addTokens(xmlState->getStringAttribute("springBCoeffs"), " ", "");

            // Process up to 256 tokens
            for (int i = 0; i < 256 && i < aVals.size() && i < bVals.size(); ++i)
            {
                springACoeffs[i] = aVals[i].getFloatValue();
                springBCoeffs[i] = bVals[i].getFloatValue();

                springA.allpasses[i].a = springACoeffs[i];
                springB.allpasses[i].a = springBCoeffs[i];
            }
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpringerAudioProcessor();
}