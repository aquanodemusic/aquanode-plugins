/*
  ==============================================================================
    PitchControl – IIR Bell Filter Plugin
    Processor Implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

//==============================================================================
// Peaking EQ biquad design (Audio EQ Cookbook, R. Bristow-Johnson)
//   H(s) = (s^2 + s*(A/Q) + 1) / (s^2 + s/(A*Q) + 1)
//   gainDB < 0  →  cut (notch-like bell pointing downward)
//==============================================================================
Biquad makePeakingEQ (double fc, double sampleRate, double gainDB, double Q) noexcept
{
    // Clamp frequency so w0 stays well below Nyquist
    double nyquist = sampleRate * 0.5;
    fc = juce::jlimit (1.0, nyquist - 1.0, fc);

    double A  = std::pow (10.0, gainDB / 40.0);   // sqrt(10^(dB/20))
    double w0 = 2.0 * M_PI * fc / sampleRate;
    double cosw0 = std::cos (w0);
    double sinw0 = std::sin (w0);
    double alpha  = sinw0 / (2.0 * Q);

    double b0 =  1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 =  1.0 - alpha * A;
    double a0 =  1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 =  1.0 - alpha / A;

    Biquad bq;
    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    return bq;
}

// MIDI note → frequency in Hz  (A4 = 440 Hz = MIDI 69)
static inline double midiNoteToHz (int note) noexcept
{
    return 440.0 * std::pow (2.0, (note - 69) / 12.0);
}

//==============================================================================
// Parameter ID helpers
//==============================================================================
juce::String PitchControlAudioProcessor::noteActiveParamID (int i)
{
    return "note_active_" + juce::String (i);
}
juce::String PitchControlAudioProcessor::depthParamID()    { return "depth_db";   }
juce::String PitchControlAudioProcessor::qParamID()        { return "filter_q";   }
juce::String PitchControlAudioProcessor::rangeFromParamID(){ return "range_from"; }
juce::String PitchControlAudioProcessor::rangeToParamID()  { return "range_to";   }

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
PitchControlAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 12 per-note active (= protected) toggles
    for (int i = 0; i < kNumNotes; ++i)
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            noteActiveParamID (i),
            kNoteNames[i] + " protected",
            false));                        // default: not protected → filter applied

    // Global cut depth  (negative = cut)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        depthParamID(), "Depth (dB)",
        juce::NormalisableRange<float> (-64.0f, 0.0f, 0.1f),
        -12.0f));

    // Q  (narrow = high Q)
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        qParamID(), "Q",
        juce::NormalisableRange<float> (10.0f, 500.0f, 0.1f),
        30.0f));

    // Note range (MIDI indices 0–119)
    params.push_back (std::make_unique<juce::AudioParameterInt> (
        rangeFromParamID(), "Range From", 0, kTotalNotes - 1, 0));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        rangeToParamID(), "Range To", 0, kTotalNotes - 1, kTotalNotes - 1));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        wetOnlyParamID(), "Wet Only", false));

    return { params.begin(), params.end() };
}

//==============================================================================
// Constructor / Destructor
//==============================================================================
PitchControlAudioProcessor::PitchControlAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                    #endif
                      ),
#else
    :
#endif
      apvts (*this, nullptr, "PitchControl", createParameterLayout()),
      m_paramListener (*this)
{
    // Register parameter listeners
    for (int i = 0; i < kNumNotes; ++i)
        apvts.addParameterListener (noteActiveParamID (i), &m_paramListener);
    apvts.addParameterListener (depthParamID(),     &m_paramListener);
    apvts.addParameterListener (qParamID(),          &m_paramListener);
    apvts.addParameterListener (rangeFromParamID(),  &m_paramListener);
    apvts.addParameterListener (rangeToParamID(),    &m_paramListener);
    apvts.addParameterListener(wetOnlyParamID(), &m_paramListener);

    // Initialise filter states to passthrough
    for (auto& ch : m_states)
        for (auto& s : ch)
            s = Biquad::State{};
}

PitchControlAudioProcessor::~PitchControlAudioProcessor()
{
    for (int i = 0; i < kNumNotes; ++i)
        apvts.removeParameterListener (noteActiveParamID (i), &m_paramListener);
    apvts.removeParameterListener (depthParamID(),     &m_paramListener);
    apvts.removeParameterListener (qParamID(),          &m_paramListener);
    apvts.removeParameterListener (rangeFromParamID(),  &m_paramListener);
    apvts.removeParameterListener (rangeToParamID(),    &m_paramListener);
}

//==============================================================================
// Standard AudioProcessor boilerplate
//==============================================================================
const juce::String PitchControlAudioProcessor::getName() const { return JucePlugin_Name; }
bool PitchControlAudioProcessor::acceptsMidi()   const { return false; }
bool PitchControlAudioProcessor::producesMidi()  const { return false; }
bool PitchControlAudioProcessor::isMidiEffect()  const { return false; }
double PitchControlAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int PitchControlAudioProcessor::getNumPrograms() { return 1; }
int PitchControlAudioProcessor::getCurrentProgram() { return 0; }
void PitchControlAudioProcessor::setCurrentProgram (int) {}
const juce::String PitchControlAudioProcessor::getProgramName (int) { return {}; }
void PitchControlAudioProcessor::changeProgramName (int, const juce::String&) {}
bool PitchControlAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* PitchControlAudioProcessor::createEditor()
{
    return new PitchControlAudioProcessorEditor (*this);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PitchControlAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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
// prepareToPlay
//==============================================================================
void PitchControlAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    m_sampleRate  = sampleRate;
    m_numChannels = std::min (getTotalNumInputChannels(), kMaxChannels);

    // Reset all biquad states
    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int n = 0; n < kTotalNotes; ++n)
            m_states[ch][n] = Biquad::State{};

    m_dirty.store (true);
    rebuildFilters();
}

void PitchControlAudioProcessor::releaseResources() {}

//==============================================================================
// rebuildFilters – called from audio thread when m_dirty is set
//==============================================================================
void PitchControlAudioProcessor::rebuildFilters()
{
    m_dirty.store(false, std::memory_order_relaxed);

    // 1. Read Wet Only first so we can use it for the logic inversion
    m_wetOnly = apvts.getRawParameterValue(wetOnlyParamID())->load() > 0.5f;

    // Read other parameters
    const float depthDB = apvts.getRawParameterValue(depthParamID())->load();
    const float Q = apvts.getRawParameterValue(qParamID())->load();
    int         rangeFrom = (int)apvts.getRawParameterValue(rangeFromParamID())->load();
    int         rangeTo = (int)apvts.getRawParameterValue(rangeToParamID())->load();
    if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

    // Which note classes are "protected" (active in the UI)
    bool noteProtected[kNumNotes]{};
    int  numProtected = 0;
    for (int i = 0; i < kNumNotes; ++i)
    {
        noteProtected[i] = apvts.getRawParameterValue(noteActiveParamID(i))->load() > 0.5f;
        if (noteProtected[i]) ++numProtected;
    }

    // Key rule: if NO note is protected, bypass everything
    const bool anyProtected = (numProtected > 0);
    m_anyActive = false;

    for (int n = 0; n < kTotalNotes; ++n)
    {
        int noteClass = n % kNumNotes;
        bool isNoteProtected = noteProtected[noteClass];

        // --- INVERSION LOGIC ---
        // Normal Mode (Wet Only OFF): Filter the note if it is NOT protected.
        // Wet Only Mode (Wet Only ON): Filter the note if it IS protected.
        bool shouldFilterThisNote = m_wetOnly ? isNoteProtected : !isNoteProtected;

        // A filter is active when:
        //  1. At least one note is protected (safety bypass)
        //  2. This MIDI note is within [rangeFrom, rangeTo]
        //  3. The note matches our (potentially inverted) selection criteria
        bool active = anyProtected
            && (n >= rangeFrom && n <= rangeTo)
            && shouldFilterThisNote;

        m_filterActive[n] = active;

        if (active)
        {
            m_anyActive = true;
            double fc = midiNoteToHz(n);
            m_filters[n] = makePeakingEQ(fc, m_sampleRate, (double)depthDB, (double)Q);
        }
        else
        {
            m_filters[n] = Biquad{}; // Identity
        }
    }

    // Reset states to avoid clicks
    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int n = 0; n < kTotalNotes; ++n)
            if (m_filterActive[n])
                m_states[ch][n] = Biquad::State{};
}

//==============================================================================
// processBlock
//==============================================================================
void PitchControlAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Clear any extra output channels
    for (int i = numInputChannels; i < numOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Rebuild filters if parameters changed (m_wetOnly is updated here)
    if (m_dirty.load(std::memory_order_relaxed))
        rebuildFilters();

    // If no filters are active, "Wet Only" would be silence (Dry - Dry = 0)
    if (!m_anyActive)
    {
        if (m_wetOnly)
            buffer.clear();
        return;
    }

    const int numCh = std::min(numInputChannels, kMaxChannels);

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int s = 0; s < numSamples; ++s)
        {
            const float drySample = data[s];
            double processedSample = (double)drySample;

            // Apply active filters in series
            for (int n = 0; n < kTotalNotes; ++n)
            {
                if (m_filterActive[n])
                {
                    processedSample = m_filters[n].process(processedSample, m_states[ch][n]);
                }
            }

            if (m_wetOnly)
            {
                // Isolate the difference (the "Color" being applied)
                data[s] = drySample - (float)processedSample;
            }
            else
            {
                data[s] = (float)processedSample;
            }
        }
    }
}

//==============================================================================
// State save / restore
//==============================================================================
void PitchControlAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void PitchControlAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchControlAudioProcessor();
}
