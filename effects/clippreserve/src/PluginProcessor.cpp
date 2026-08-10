/*
  ==============================================================================
    ClipPreserve - Detail Preserving Clipper

    Signal flow (per sample, per channel):

      driven    = input * driveGain
      clipped   = hardClip(driven, threshold)
      delta     = driven - clipped              // overflow above threshold

      -- filter delta (each stage optional via toggle) --
      deltaHP   = highpass(delta, hpFreq)       // remove lows (if hpOn)
      deltaFilt = lowpass(deltaHP, lpFreq)      // remove highs (if lpOn)

      -- sidechain-informed preserve --
      scLevel   = abs(sidechain sample)         // 0..n, typically 0..1
      dynAmount = preserveAmount * (1 + scLevel * scGain)
      dynAmount = clamp(dynAmount, 0, 1)

      output    = (clipped + deltaFilt * dynAmount) * outputGain

    Sidechain intent:
      The sidechain carries a signal whose transient/detail energy you want
      to protect (e.g. drums). When the sidechain is loud, more of the clipped
      delta is folded back, preserving that detail in the output mix.

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

    // Preserve: base foldback amount (0–100 %)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "preserve", "Preserve",
        juce::NormalisableRange<float> (0.f, 100.f, 0.1f),
        50.f, "%"));

    // HP Frequency for the delta chain (500 Hz – 20 kHz)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "hpfreq", "HP Freq",
        juce::NormalisableRange<float> (500.f, 20000.f, 1.f, 0.3f),
        8000.f, "Hz"));

    // LP Frequency for the delta chain (200 Hz – 20 kHz)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "lpfreq", "LP Freq",
        juce::NormalisableRange<float> (200.f, 20000.f, 1.f, 0.3f),
        8000.f, "Hz"));

    // Output gain, -12–+12 dB
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "output", "Output",
        juce::NormalisableRange<float> (-12.f, 12.f, 0.01f),
        0.f, "dB"));

    // Sidechain preserve gain: how much the SC signal boosts foldback (0–400 %)
    // At 0% sidechain has no effect; at 400% a full-scale SC signal quadruples preserve
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "scgain", "SC Gain",
        juce::NormalisableRange<float> (0.f, 400.f, 0.1f),
        100.f, "%"));

    // Toggle: HP filter active
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "hpon", "HP On", true));

    // Toggle: LP filter active
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "lpon", "LP On", false));

    // Toggle: 2x oversampling
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        "oversample", "2x Oversample", false));

    return { params.begin(), params.end() };
}

//==============================================================================
ClipPreserveAudioProcessor::ClipPreserveAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                      .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                      .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                      .withOutput ("Output",    juce::AudioChannelSet::stereo(), true))
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
void ClipPreserveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    hp1L.reset(); hp2L.reset();
    hp1R.reset(); hp2R.reset();
    lp1L.reset(); lp2L.reset();
    lp1R.reset(); lp2R.reset();

    // Prepare oversampler for main stereo bus (2 channels)
    oversampler.initProcessing ((size_t) samplesPerBlock);
    oversampler.reset();
}

void ClipPreserveAudioProcessor::releaseResources()
{
    oversampler.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ClipPreserveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Main output must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Main input must be stereo
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Sidechain (bus index 1) must be stereo or disabled
    const auto& sc = layouts.getChannelSet (true, 1);
    if (!sc.isDisabled() && sc != juce::AudioChannelSet::stereo())
        return false;

    return true;
}
#endif

//==============================================================================
void ClipPreserveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    // --- Read parameters ---
    const float driveDb      = apvts.getRawParameterValue ("drive")->load();
    const float threshDb     = apvts.getRawParameterValue ("threshold")->load();
    const float preservePct  = apvts.getRawParameterValue ("preserve")->load();
    const float hpFreqHz     = apvts.getRawParameterValue ("hpfreq")->load();
    const float lpFreqHz     = apvts.getRawParameterValue ("lpfreq")->load();
    const float outputDb     = apvts.getRawParameterValue ("output")->load();
    const float scGainPct    = apvts.getRawParameterValue ("scgain")->load();
    const bool  hpOn         = apvts.getRawParameterValue ("hpon")->load() > 0.5f;
    const bool  lpOn         = apvts.getRawParameterValue ("lpon")->load() > 0.5f;
    const bool  oversampleOn = apvts.getRawParameterValue ("oversample")->load() > 0.5f;

    const float driveGain      = juce::Decibels::decibelsToGain (driveDb);
    const float threshold      = juce::Decibels::decibelsToGain (threshDb);
    const float preserveAmount = preservePct / 100.f;
    const float outputGain     = juce::Decibels::decibelsToGain (outputDb);
    const float scGain         = scGainPct / 100.f;   // e.g. 1.0 = 100%

    // HP coefficient at base sample rate (oversampled rate handled below)
    // a = exp(-2π·fc/fs)
    auto makeCoeff = [](float fc, double fs) {
        return std::exp (-juce::MathConstants<float>::twoPi * fc / (float) fs);
    };

    // --- Extract sidechain buffer (bus 1) ---
    // getBusBuffer returns a reference, so we check channel count to detect connection.
    const int numMainSamples = buffer.getNumSamples();
    auto scBus = getBusBuffer (buffer, true, 1);   // AudioBuffer<float> by value (lightweight view)

    // Build per-sample sidechain envelope at the main sample rate
    juce::AudioBuffer<float> scEnvelope (1, numMainSamples);
    scEnvelope.clear();
    {
        float* env = scEnvelope.getWritePointer (0);
        const bool scConnected = (scBus.getNumChannels() > 0) && (scBus.getNumSamples() > 0);
        if (scConnected)
        {
            for (int i = 0; i < numMainSamples; ++i)
            {
                float peak = 0.f;
                for (int ch = 0; ch < scBus.getNumChannels(); ++ch)
                    peak = juce::jmax (peak, std::abs (scBus.getReadPointer (ch)[i]));
                env[i] = peak;
            }
        }
        // If not connected, env stays 0 → sc has no effect → normal preserve behaviour
    }

    // --- Grab main stereo bus (channels 0 & 1) ---
    // We only process 2 channels via the oversampler regardless of total channels
    const int numMainCh = juce::jmin (buffer.getNumChannels(), 2);

    // Build a 2-ch sub-buffer pointing at the main channels
    // (oversampler works on a dsp::AudioBlock)
    juce::dsp::AudioBlock<float> mainBlock (buffer.getArrayOfWritePointers(),
                                            (size_t) numMainCh,
                                            (size_t) numMainSamples);

    // ---- Helper lambda: process one block of samples (at whatever sample rate) ----
    // Called either directly (no oversampling) or on the upsampled block.
    // scFactor scales the sidechain index back to main-rate when oversampled.
    auto processKernel = [&] (juce::dsp::AudioBlock<float>& block,
                               double fs,
                               float  scIndexScale)   // 1.f normal, 0.5f at 2x OS
    {
        const float aHP = makeCoeff (hpFreqHz, fs);
        const float aLP = makeCoeff (lpFreqHz, fs);
        const int nSamples = (int) block.getNumSamples();

        for (int ch = 0; ch < (int) block.getNumChannels(); ++ch)
        {
            float* data = block.getChannelPointer ((size_t) ch);

            SimplePole& s1hp = (ch == 0) ? hp1L : hp1R;
            SimplePole& s2hp = (ch == 0) ? hp2L : hp2R;
            SimplePole& s1lp = (ch == 0) ? lp1L : lp1R;
            SimplePole& s2lp = (ch == 0) ? lp2L : lp2R;

            for (int i = 0; i < nSamples; ++i)
            {
                // Map oversampled index back to main-rate SC envelope index
                const int scIdx = juce::jlimit (0, numMainSamples - 1,
                                                (int) ((float) i * scIndexScale));
                const float scLevel = scEnvelope.getReadPointer (0)[scIdx];

                // Sidechain-informed preserve: when SC is loud (e.g. drums transient),
                // boost how much delta is folded back so that detail is better
                // preserved in the clipped output at exactly those moments.
                const float dynAmount = juce::jlimit (
                    0.f, 1.f,
                    preserveAmount * (1.f + scLevel * scGain));

                const float input   = data[i];
                const float driven  = input * driveGain;
                const float clipped = hardClip (driven, threshold);
                const float delta   = driven - clipped;

                const float deltaFilt = processDelta (delta,
                                                       s1hp, s2hp,
                                                       s1lp, s2lp,
                                                       aHP, aLP,
                                                       hpOn, lpOn);

                data[i] = (clipped + deltaFilt * dynAmount) * outputGain;
            }
        }
    };

    if (oversampleOn)
    {
        // Upsample main block to 2x
        auto oversampledBlock = oversampler.processSamplesUp (mainBlock);

        // Process at 2x rate; SC index scale = 0.5 (2 oversampled samples per main sample)
        processKernel (oversampledBlock, currentSampleRate * 2.0, 0.5f);

        // Downsample back
        oversampler.processSamplesDown (mainBlock);
    }
    else
    {
        processKernel (mainBlock, currentSampleRate, 1.0f);
    }

    // Clear any extra channels beyond stereo (sidechain channels etc.)
    for (int ch = numMainCh; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numMainSamples);
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
