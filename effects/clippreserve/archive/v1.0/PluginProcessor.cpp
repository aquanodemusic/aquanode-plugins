/*
  ==============================================================================
    ClipPreserve - Detail Preserving Clipper
    
    Signal flow (per sample, per channel):
    
      clipped   = hardClip(input, threshold)
      delta     = input - clipped            // the "overflow" above threshold
      deltaHP   = highpass(delta, hpFreq)    // only keep high-freq detail
      foldback  = deltaHP * amount           // scale how much detail to fold in
      output    = clipped + foldback         // re-add detail to clipped signal
    
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
    ClipPreserveAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Drive: input gain before clipper, 0–40 dB
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "drive", "Drive",
        juce::NormalisableRange<float> (0.f, 40.f, 0.01f, 0.5f),
        0.f, "dB"));

    // Threshold: clip ceiling, -24–0 dBFS
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-24.f, 0.f, 0.01f),
        0.f, "dBFS"));

    // Preserve: amount of foldback (0–100 %)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preserve", "Preserve",
        juce::NormalisableRange<float> (0.f, 100.f, 0.1f),
        50.f, "%"));

    // HP Frequency for the delta chain (500 Hz – 20 kHz)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "hpfreq", "HP Freq",
        juce::NormalisableRange<float> (500.f, 20000.f, 1.f, 0.3f),
        8000.f, "Hz"));

    // Output gain, -12–+12 dB
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> (-12.f, 12.f, 0.01f),
        0.f, "dB"));

    return { params.begin(), params.end() };
}

//==============================================================================
ClipPreserveAudioProcessor::ClipPreserveAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

ClipPreserveAudioProcessor::~ClipPreserveAudioProcessor() {}

//==============================================================================
const juce::String ClipPreserveAudioProcessor::getName() const { return JucePlugin_Name; }
bool ClipPreserveAudioProcessor::acceptsMidi() const { return false; }
bool ClipPreserveAudioProcessor::producesMidi() const { return false; }
bool ClipPreserveAudioProcessor::isMidiEffect() const { return false; }
double ClipPreserveAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int ClipPreserveAudioProcessor::getNumPrograms() { return 1; }
int ClipPreserveAudioProcessor::getCurrentProgram() { return 0; }
void ClipPreserveAudioProcessor::setCurrentProgram (int) {}
const juce::String ClipPreserveAudioProcessor::getProgramName (int) { return {}; }
void ClipPreserveAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void ClipPreserveAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    hp1L.reset(); hp2L.reset();
    hp1R.reset(); hp2R.reset();
}

void ClipPreserveAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ClipPreserveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
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
void ClipPreserveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // --- Read parameters ---
    const float driveDb     = apvts.getRawParameterValue ("drive")->load();
    const float threshDb    = apvts.getRawParameterValue ("threshold")->load();
    const float preservePct = apvts.getRawParameterValue ("preserve")->load();
    const float hpFreqHz    = apvts.getRawParameterValue ("hpfreq")->load();
    const float outputDb    = apvts.getRawParameterValue ("output")->load();

    const float driveGain      = juce::Decibels::decibelsToGain (driveDb);
    const float threshold      = juce::Decibels::decibelsToGain (threshDb);  // linear
    const float preserveAmount = preservePct / 100.f;
    const float outputGain     = juce::Decibels::decibelsToGain (outputDb);

    // High-pass filter coefficient: a = exp(-2π·fc/fs)
    // Used for both cascaded stages
    const float a = std::exp (-juce::MathConstants<float>::twoPi
                               * hpFreqHz / (float)currentSampleRate);

    // --- Level metering accumulators ---
    float peakInL = 0.f, peakInR = 0.f;
    float peakOutL = 0.f, peakOutR = 0.f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buffer.getWritePointer (ch);

        // Choose which filter state to use per channel
        SimpleHP& stage1 = (ch == 0) ? hp1L : hp1R;
        SimpleHP& stage2 = (ch == 0) ? hp2L : hp2R;

        float& peakIn  = (ch == 0) ? peakInL  : peakInR;
        float& peakOut = (ch == 0) ? peakOutL : peakOutR;

        for (int i = 0; i < numSamples; ++i)
        {
            const float input   = data[i];
            peakIn = std::max (peakIn, std::abs (input));

            // 1. Apply drive
            const float driven  = input * driveGain;

            // 2. Hard clip
            const float clipped = hardClip (driven, threshold);

            // 3. Delta = the part that was clipped away (overflow)
            const float delta   = driven - clipped;

            // 4. High-pass the delta (two cascaded 1-pole HP stages ≈ 12 dB/oct)
            const float deltaHP = stage2.process (stage1.process (delta, a), a);

            // 5. Foldback: add the high-freq detail back to the clipped signal
            const float out = (clipped + deltaHP * preserveAmount) * outputGain;

            data[i] = out;
            peakOut = std::max (peakOut, std::abs (out));
        }
    }

    // Update atomic meters (smoothed toward peak)
    auto smooth = [](std::atomic<float>& meter, float peak)
    {
        const float prev = meter.load();
        meter.store (peak > prev ? peak : prev * 0.9995f);
    };

    smooth (inputLevelL,  peakInL);
    smooth (inputLevelR,  peakInR);
    smooth (outputLevelL, peakOutL);
    smooth (outputLevelR, peakOutR);

    // Clear any extra output channels
    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

//==============================================================================
bool ClipPreserveAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* ClipPreserveAudioProcessor::createEditor()
{
    return new ClipPreserveAudioProcessorEditor (*this);
}

//==============================================================================
void ClipPreserveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ClipPreserveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClipPreserveAudioProcessor();
}
