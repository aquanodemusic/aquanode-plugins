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
    
    // Initialize random mode state
    for (int i = 0; i < 7; ++i)
    {
        randomCurrentValue[i] = 0.0f;
        randomTargetValue[i] = juce::Random::getSystemRandom().nextFloat();
        randomLastPhase[i] = 0.0;
    }
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

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            "waveform" + suffix, "Waveform " + labelNum,
            juce::StringArray{ "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Tanh", "Random"},
            0));
    }

    return layout;
}

float AutoMorphEQAudioProcessor::applyWaveformShape(float phase, MorphWaveform waveform, int bandIndex)
{
    switch (waveform)
    {
    case MorphWaveform::Sine:
        return (float)(0.5 * (1.0 - std::cos(juce::MathConstants<double>::twoPi * phase)));

    case MorphWaveform::Triangle:
        // Triangle wave: goes up linearly then down linearly
        if (phase < 0.5f)
            return phase * 2.0f;  // Rising: 0 to 1
        else
            return 2.0f - (phase * 2.0f);  // Falling: 1 to 0

    case MorphWaveform::RampUp:
        return phase;

    case MorphWaveform::RampDown:
        return 1.0f - phase;

    case MorphWaveform::Square:
        return phase < 0.5f ? 0.0f : 1.0f;

    case MorphWaveform::Tanh:
    {
        // Tanh of sine: smooth back and forth motion with lingering at extremes
        float sineValue = std::sin(juce::MathConstants<float>::twoPi * phase);
        float tanhValue = std::tanh(1.5f * sineValue);
        return (tanhValue + 1.0f) * 0.5f; // Normalize to 0-1
    }

    case MorphWaveform::Random:
    {
        // Per-band sample and hold with smoothing to prevent clicks
        if (bandIndex < 0 || bandIndex >= 7)
            return 0.0f;
        
        // Detect phase wrap (cycle completed)
        if (phase < randomLastPhase[bandIndex])
        {
            // New cycle - set new target and current becomes the previous target
            randomCurrentValue[bandIndex] = randomTargetValue[bandIndex];
            randomTargetValue[bandIndex] = juce::Random::getSystemRandom().nextFloat();
        }
        
        randomLastPhase[bandIndex] = phase;
        
        // Smooth interpolation between current and target value over the cycle
        // Use a small portion at the beginning for crossfade to prevent clicks
        const float crossfadeTime = 0.05f; // 5% of cycle for smooth transition
        
        if (phase < crossfadeTime)
        {
            // Smooth crossfade from current to target at start of cycle
            float crossfadePosition = phase / crossfadeTime;
            // Use cosine interpolation for smoother transition
            float smoothPosition = (1.0f - std::cos(crossfadePosition * juce::MathConstants<float>::pi)) * 0.5f;
            return randomCurrentValue[bandIndex] + (randomTargetValue[bandIndex] - randomCurrentValue[bandIndex]) * smoothPosition;
        }
        else
        {
            // Hold the target value for the rest of the cycle
            return randomTargetValue[bandIndex];
        }
    }

    default:
        return (float)(0.5 * (1.0 - std::cos(juce::MathConstants<double>::twoPi * phase)));
    }
}

void AutoMorphEQAudioProcessor::randomizeAllParameters()
{
    juce::Random random;

    for (int i = 0; i < 7; ++i)
    {
        juce::String suffix = "_" + juce::String(i);

        // Randomize frequencies with musical ranges
        // Start: 20-400 Hz, End: 1000-4000 Hz
        float startFreq = 20.0f + random.nextFloat() * (400.0f - 20.0f);
        float endFreq = 1000.0f + random.nextFloat() * (4000.0f - 1000.0f);
        apvts.getParameter("start_freq" + suffix)->setValueNotifyingHost(
            apvts.getParameter("start_freq" + suffix)->convertTo0to1(startFreq));
        apvts.getParameter("end_freq" + suffix)->setValueNotifyingHost(
            apvts.getParameter("end_freq" + suffix)->convertTo0to1(endFreq));

        // Randomize volumes (limited range: 0.1 to 3.0 for safety)
        float startVol = 0.1f + random.nextFloat() * 2.9f;
        float endVol = 0.1f + random.nextFloat() * 2.9f;
        apvts.getParameter("start_vol" + suffix)->setValueNotifyingHost(
            apvts.getParameter("start_vol" + suffix)->convertTo0to1(startVol));
        apvts.getParameter("end_vol" + suffix)->setValueNotifyingHost(
            apvts.getParameter("end_vol" + suffix)->convertTo0to1(endVol));

        // Randomize Q (0.5 to 20.0 for musicality)
        float q = 0.5f + random.nextFloat() * 19.5f;
        apvts.getParameter("q" + suffix)->setValueNotifyingHost(
            apvts.getParameter("q" + suffix)->convertTo0to1(q));

        // Randomize speed (0.1 to 5.0 seconds)
        float speed = 0.1f + random.nextFloat() * 4.9f;
        apvts.getParameter("speed" + suffix)->setValueNotifyingHost(
            apvts.getParameter("speed" + suffix)->convertTo0to1(speed));

        // Randomize position (0 to 100%)
        float pos = random.nextFloat() * 100.0f;
        apvts.getParameter("pos" + suffix)->setValueNotifyingHost(
            apvts.getParameter("pos" + suffix)->convertTo0to1(pos));

        // Randomize waveform (now 6 options: Sine, Triangle, RampUp, RampDown, Square, Tanh)
        int waveformIndex = random.nextInt(6);
        apvts.getParameter("waveform" + suffix)->setValueNotifyingHost(
            waveformIndex / 5.0f);
    }
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
        int waveformIndex = (int)apvts.getRawParameterValue("waveform" + suffix)->load();

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

            // Apply waveform shaping - pass band index for Random mode
            MorphWaveform waveform = static_cast<MorphWaveform>(waveformIndex);
            morphFactor = applyWaveformShape((float)oscPhases[i], waveform, i);
        }
        
        // Ensure morphFactor is valid (prevent NaN propagation)
        if (!std::isfinite(morphFactor))
            morphFactor = 0.0f;
        morphFactor = juce::jlimit(0.0f, 1.0f, morphFactor);

        float currentFreq = startFreq + (endFreq - startFreq) * morphFactor;
        float currentVol = startVol + (endVol - startVol) * morphFactor;

        // Comprehensive safety checks to prevent assertion failures
        // 1. Ensure frequency is valid, positive, and below Nyquist
        if (!std::isfinite(currentFreq) || currentFreq <= 0.0f)
            currentFreq = 1000.0f;
        
        float maxFreq = (float)(currentSampleRate * 0.49); // Stay safely below Nyquist
        currentFreq = juce::jlimit(20.0f, juce::jmin(20000.0f, maxFreq), currentFreq);
        
        // 2. Ensure volume/gain is valid and positive
        if (!std::isfinite(currentVol) || currentVol <= 0.0f)
            currentVol = 1.0f;
        currentVol = juce::jlimit(0.0001f, 10.0f, currentVol);
        
        // 3. Ensure Q is valid and positive
        if (!std::isfinite(bandQ) || bandQ <= 0.0f)
            bandQ = 1.0f;
        bandQ = juce::jlimit(0.1f, 100.0f, bandQ);

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
