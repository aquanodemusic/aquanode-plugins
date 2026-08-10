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

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "freqMode",
        "Frequency Mode",
        juce::StringArray{ "Regular", "Wrap", "Mirror" },
        0));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "spreadMode", "Spread Mode", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "wetOnly", "Wet Only", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "hardLimiter", "Hard Limiter", true));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "smoothTime", "Smooth Time (ms)", 0.0f, 2000.0f, 1.0f));

    return layout;
}


void CenterCombAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 1 };

    // Initialize all smoothers
    const float rampTime = 0.02f; // 20ms
    smoothedParams.gain.reset(sampleRate, rampTime);
    smoothedParams.freq.reset(sampleRate, rampTime);
    smoothedParams.q.reset(sampleRate, rampTime);
    smoothedParams.damp.reset(sampleRate, rampTime);
    smoothedParams.spreadHz.reset(sampleRate, rampTime);
    smoothedParams.spreadRatio.reset(sampleRate, rampTime);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < maxFilters; ++i)
            filters[ch][i].prepare(spec);

    for (int i = 0; i < maxFilters; ++i)
        lastFreqs[i] = 1000.0f; // Default starting position

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < maxFilters; ++i)
            filters[ch][i].prepare(spec);
}

static float wrapFrequency(double freq, float minFreq, float maxFreq)
{
    double range = (double)maxFreq - (double)minFreq;
    double x = std::fmod(freq - (double)minFreq, range);
    if (x < 0.0)
        x += range;
    return minFreq + (float)x;
}

static float mirrorFrequency(double freq, float minFreq, float maxFreq)
{
    double range = (double)maxFreq - (double)minFreq;
    double period = 2.0 * range;

    double x = std::fmod(freq - (double)minFreq, period);
    if (x < 0.0)
        x += period;

    if (x > range)
        x = period - x;

    return minFreq + (float)x;
}

void CenterCombAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const double sampleRate = getSampleRate();

    // --- 1) Dynamic Smoothing Time Update ---
    float targetSmoothMs = apvts.getRawParameterValue("smoothTime")->load();
    if (std::abs(targetSmoothMs - currentSmoothingMs) > 0.01f)
    {
        currentSmoothingMs = targetSmoothMs;
        float timeInSeconds = currentSmoothingMs / 1000.0f;
        // ... (Reset smoothers as before) ...
        smoothedParams.gain.reset(sampleRate, timeInSeconds);
        smoothedParams.freq.reset(sampleRate, timeInSeconds);
        smoothedParams.q.reset(sampleRate, timeInSeconds);
        smoothedParams.damp.reset(sampleRate, timeInSeconds);
        smoothedParams.spreadHz.reset(sampleRate, timeInSeconds);
        smoothedParams.spreadRatio.reset(sampleRate, timeInSeconds);
    }

    // --- 2) Update Smoothers & Capture Values ---
    smoothedParams.gain.setTargetValue(apvts.getRawParameterValue("gain")->load());
    smoothedParams.freq.setTargetValue(apvts.getRawParameterValue("freq")->load());
    smoothedParams.q.setTargetValue(juce::jlimit(0.1f, 100.0f, static_cast<float>(*apvts.getRawParameterValue("q"))));
    smoothedParams.damp.setTargetValue(apvts.getRawParameterValue("damp")->load());
    smoothedParams.spreadHz.setTargetValue(apvts.getRawParameterValue("spreadHz")->load());
    smoothedParams.spreadRatio.setTargetValue(apvts.getRawParameterValue("spreadRatio")->load());

    const float currentGainDB = smoothedParams.gain.getNextValue();
    const float currentFreq = smoothedParams.freq.getNextValue();
    const float currentQ = smoothedParams.q.getNextValue();
    const float currentDamp = smoothedParams.damp.getNextValue();

    // Advance smoothers we don't need per-sample
    smoothedParams.gain.skip(numSamples - 1);
    smoothedParams.freq.skip(numSamples - 1);
    smoothedParams.q.skip(numSamples - 1);
    smoothedParams.damp.skip(numSamples - 1);

    // --- LOGIC: Parameter Capture ---
    int targetPairs = static_cast<int>(apvts.getRawParameterValue("amount")->load());
    const bool isMultiplicative = *apvts.getRawParameterValue("spreadMode") > 0.5f;
    const bool wetOnly = *apvts.getRawParameterValue("wetOnly") > 0.5f;
    const bool hardLimiter = *apvts.getRawParameterValue("hardLimiter") > 0.5f;
    const auto freqMode = static_cast<int>(apvts.getRawParameterValue("freqMode")->load()); // 0=Reg, 1=Wrap, 2=Mirror

    // --- SAFETY LOGIC: Max 12 Tines in Wrap+Multi Mode ---
    if (isMultiplicative && freqMode == 1)
    {
        if (targetPairs > 5)
            targetPairs = 5;
    }

    float currentSpread = isMultiplicative ? smoothedParams.spreadRatio.getNextValue() : smoothedParams.spreadHz.getNextValue();
    if (isMultiplicative) smoothedParams.spreadRatio.skip(numSamples - 1);
    else smoothedParams.spreadHz.skip(numSamples - 1);

    const float lowLimit = 20.0f;
    const float highLimit = static_cast<float>(sampleRate) * 0.475f;

    // --- 3) Filter Processing ---
    juce::AudioBuffer<float> dryCopy;
    if (wetOnly) dryCopy.makeCopyOf(buffer);

    for (int i = 0; i < maxFilters; ++i)
    {
        int totalTargetFilters = 1 + (targetPairs * 2);

        if (i < totalTargetFilters)
        {
            int offsetIdx = i - targetPairs;
            double rawF = currentFreq;

            // Use double for calculation to prevent overflow before wrapping
            if (isMultiplicative)
                rawF = (double)currentFreq * std::pow((double)currentSpread, (double)offsetIdx);
            else
                rawF = (double)currentFreq + ((double)offsetIdx * (double)currentSpread);

            // Tiny offset to prevent perfect math stacking
            rawF += (double)i * 0.0001;

            float f = 0.0f;

            // Perform math on DOUBLE first
            if (freqMode == 1)      f = wrapFrequency(rawF, lowLimit, highLimit);
            else if (freqMode == 2) f = mirrorFrequency(rawF, lowLimit, highLimit);
            else                    f = static_cast<float>(rawF);

            lastFreqs[i] = f;

            // VETO: Only process if frequency is within safe bounds
            if (f >= lowLimit && f <= highLimit && std::isfinite(f))
            {
                float weight = 1.0f - (currentDamp * (std::abs(offsetIdx) / static_cast<float>(targetPairs + 1)));

                // --- FIX: 40Hz HARD CUT (Mirror Mode Only) ---
                // If Multiplicative AND Mirror are active, kill the mud cluster < 55Hz
                if (isMultiplicative && freqMode == 2)
                {
                    if (f > 35.0f && f < 45.0f)
                        weight = 0.0f;
                }

                weight = juce::jmax(0.0f, weight);

                // Optimization: Skip processing if weight is effectively zero
                if (weight > 0.001f)
                {
                    float filterGain = currentGainDB * weight;

                    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        sampleRate, f, currentQ, juce::Decibels::decibelsToGain(filterGain));

                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    {
                        filters[ch][i].coefficients = coeffs;
                        juce::dsp::AudioBlock<float> block(buffer);
                        juce::dsp::ProcessContextReplacing<float> context(block.getSingleChannelBlock(ch));
                        filters[ch][i].process(context);
                    }
                }
                else
                {
                    // Reset silent filters to prevent state corruption/NaNs
                    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                        filters[ch][i].reset();
                }
            }
        }
        else
        {
            // Reset unused filters
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                filters[ch][i].reset();

            lastFreqs[i] = currentFreq;
        }
    }

    // --- 4) Post-Processing & Safety ---

        // Check for the special "Safety Mode" condition
    const bool isSpecialSafetyMode = isMultiplicative && (freqMode == 1 || freqMode == 2);
    const float limitThreshold = isSpecialSafetyMode ? 0.80f : 0.95f;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* channelData = buffer.getWritePointer(ch);
        const auto* dryData = wetOnly ? dryCopy.getReadPointer(ch) : nullptr;

        for (int s = 0; s < numSamples; ++s)
        {
            // 1. Handle Wet Only
            if (wetOnly) channelData[s] -= dryData[s];

            // 2. Safety Limiting
            // If Special Safety Mode is on, we force the limiter regardless of the 'hardLimiter' parameter
            if (hardLimiter || isSpecialSafetyMode)
            {
                channelData[s] = juce::jlimit(-limitThreshold, limitThreshold, channelData[s]);
            }

            // 3. Final NaN/Inf check
            if (std::isnan(channelData[s]) || std::isinf(channelData[s]))
                channelData[s] = 0.0f;
        }
    }
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