#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
// Hilfsfunktionen
//==============================================================================

// Anti-Aliasing Helper: Frequency Folding
static inline float foldFrequency(float fIn, float nyquist)
{
    if (nyquist >= 22050.0f) return fIn;
    if (nyquist <= 1.0f) return 0.0f;

    const float virtualSR = nyquist * 2.0f;
    return std::fabs(fIn - virtualSR * std::round(fIn / virtualSR));
}

// Triviale Sound-Klasse für den Synthesiser
struct FM12Sound : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

//==============================================================================
// FM12Voice Implementierung
//==============================================================================

FM12Voice::FM12Voice(FM12SynthAudioProcessor& p)
    : processor(p), parameters(p.apvts)
{
    for (auto& env : opEnvelopes)
        env.setSampleRate(sampleRate);
}

// Statische ID-Generatoren
String FM12Voice::opParamID(int op, const char* name)
{
    return "op" + String(op) + "_" + String(name);
}

String FM12Voice::opPhaseID(int op)
{
    return "op" + String(op) + "_phase";
}

String FM12Voice::routeID(int from, int to)
{
    return "route_" + String(from) + "_" + String(to);
}

bool FM12Voice::canPlaySound(juce::SynthesiserSound* s)
{
    return dynamic_cast<juce::SynthesiserSound*>(s) != nullptr;
}

void FM12Voice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    noteMidi = midiNoteNumber;
    level = velocity;

    for (int i = 0; i < numOperators; ++i)
    {
        opPhase[i] = *parameters.getRawParameterValue(opPhaseID(i));
        opLastOutput[i] = 0.0f;
        opOutput[i] = 0.0f;

        juce::ADSR::Parameters adsr;
        adsr.attack = *parameters.getRawParameterValue(opParamID(i, "attack"));
        adsr.decay = *parameters.getRawParameterValue(opParamID(i, "decay"));
        adsr.sustain = *parameters.getRawParameterValue(opParamID(i, "sustain"));
        adsr.release = *parameters.getRawParameterValue(opParamID(i, "release"));

        opEnvelopes[i].setParameters(adsr);
        opEnvelopes[i].noteOn();
    }

    isVoiceActiveFlag = true;
}

void FM12Voice::stopNote(float, bool allowTailOff)
{
    for (auto& env : opEnvelopes)
        env.noteOff();

    if (!allowTailOff)
        clearCurrentNote();
}

void FM12Voice::setCurrentPlaybackSampleRate(double newRate)
{
    if (newRate > 0.0)
    {
        sampleRate = newRate;
        for (auto& env : opEnvelopes)
            env.setSampleRate(newRate);
    }
}

void FM12Voice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                int startSample,
                                int numSamples)
{
    if (!isVoiceActive())
        return;

    // --- 1. PRE-CALCULATION PHASE ---
    const float masterVol = *parameters.getRawParameterValue("masterVol");
    const float nyquist = *parameters.getRawParameterValue("nyquistLimit");
    const double noteFreq = juce::MidiMessage::getMidiNoteInHertz(noteMidi);
    const bool modetoggle = *parameters.getRawParameterValue("adsrAffectsFM") > 0.5f;

    const float invSampleRate = 1.0f / (float)sampleRate;
    const float twoPi = juce::MathConstants<float>::twoPi;

    alignas(16) float opRatio[numOperators];
    alignas(16) float opLevel[numOperators];
    alignas(16) float opFmAmt[numOperators];

    activeConnections.clear();
    activeOperatorIndices.clear();
    bool opIsActive[numOperators] = { false };

    // --- A) Read operator parameters & mark all as carriers initially ---
    for (int i = 0; i < numOperators; ++i)
    {
        opRatio[i] = *parameters.getRawParameterValue(opParamID(i, "ratio"));
        opLevel[i] = *parameters.getRawParameterValue(opParamID(i, "level"));
        opFmAmt[i] = *parameters.getRawParameterValue(opParamID(i, "fm"));
        isCarrier[i] = true;
    }

    // --- B) Analyze routing matrix to identify modulators ---
    for (int src = 0; src < numOperators; ++src)
    {
        for (int dst = 0; dst < numOperators; ++dst)
        {
            if (*parameters.getRawParameterValue(routeID(src, dst)) > 0.5f)
            {
                activeConnections.push_back({ src, dst });
                isCarrier[src] = false;

                opIsActive[src] = true;
                opIsActive[dst] = true;
            }
        }
    }

    // --- C) Final active operator list ---
    for (int i = 0; i < numOperators; ++i)
    {
        if (isCarrier[i]) opIsActive[i] = true;
        if (opIsActive[i]) activeOperatorIndices.push_back(i);
    }

    if (activeOperatorIndices.empty())
        return;

    const int numChannels = outputBuffer.getNumChannels();

    // --- 2. AUDIO LOOP ---
    for (int s = 0; s < numSamples; ++s)
    {
        // A) Envelope values for active operators
        alignas(16) float envValues[numOperators];
        for (int op : activeOperatorIndices)
            envValues[op] = opEnvelopes[op].getNextSample();

        // B) FM accumulation
        alignas(16) float fmIn[numOperators] = { 0.0f };

        for (const auto& conn : activeConnections)
        {
            const int src = conn.source;
            const int dst = conn.dest;

            // Base modulator signal (oscillator)
            float modSignal = currentRawSamples[src]; // output from previous sample or current modulator

            // FM amount
            float fmIndex = opFmAmt[src];

            // Apply envelope only if toggle is ON
            if (modetoggle)
                fmIndex *= envValues[src];

            // Accumulate FM input
            fmIn[dst] += modSignal * fmIndex;
        }

        float mixOut = 0.0f;

        // C) Process active operators
        for (int op : activeOperatorIndices)
        {
            const float baseFreq = noteFreq * opRatio[op];
            const float modFreq = baseFreq + (fmIn[op] * baseFreq);

            // Phase update
            float phaseInc = std::fabs(modFreq) * invSampleRate;
            opPhase[op] += phaseInc;
            if (opPhase[op] >= 1.0f)
                opPhase[op] -= 1.0f;

            // Oscillator
            const float osc = std::sin(opPhase[op] * twoPi);

            if (isCarrier[op])
            {
                // Carrier: envelope always controls amplitude
                currentRawSamples[op] = osc * envValues[op];
                mixOut += currentRawSamples[op] * opLevel[op];
            }
            else
            {
                // Modulator: constant amplitude, ADSR only affects FM index if toggle is ON
                currentRawSamples[op] = osc;
            }
        }

        // D) Update last outputs for FM feedback
        for (int op : activeOperatorIndices)
            opLastOutput[op] = currentRawSamples[op];

        // E) Write output
        const float out = mixOut * masterVol;

        for (int ch = 0; ch < numChannels; ++ch)
            outputBuffer.addSample(ch, startSample + s, out);

        // F) Voice activity check
        bool anyActive = false;
        for (int op : activeOperatorIndices)
        {
            if (opEnvelopes[op].isActive())
            {
                anyActive = true;
                break;
            }
        }

        if (!anyActive)
        {
            clearCurrentNote();
            isVoiceActiveFlag = false;
            break;
        }
    }
}


//==============================================================================
// FM12SynthAudioProcessor Implementierung
//==============================================================================

FM12SynthAudioProcessor::FM12SynthAudioProcessor()
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (int i = 0; i < maxVoices; ++i)
        synth.addVoice(new FM12Voice(*this));

    synth.addSound(new FM12Sound());
}

FM12SynthAudioProcessor::~FM12SynthAudioProcessor() = default;

void FM12SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<FM12Voice*>(synth.getVoice(i)))
            v->setCurrentPlaybackSampleRate(sampleRate);
    }
}

void FM12SynthAudioProcessor::releaseResources()
{
}

void FM12SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* FM12SynthAudioProcessor::createEditor()
{
    return new FM12SynthAudioProcessorEditor(*this);
}

void FM12SynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FM12SynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

// Parameter Layout
juce::AudioProcessorValueTreeState::ParameterLayout FM12SynthAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Global Parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>("masterVol", "Master Volume", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("nyquistLimit", "Nyquist Limit", 1000.0f, 22050.0f, 22050.0f));
    
    // ADSR Routing Toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>("adsrAffectsFM", "ADSR Affects FM", false));

    // Operator Parameters
    for (int op = 0; op < 12; ++op) {
        auto opPrefix = "op" + juce::String(op) + "_";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "attack", "Attack", 0.0f, 5.0f, 0.1f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "decay", "Decay", 0.0f, 5.0f, 0.1f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "sustain", "Sustain", 0.0f, 1.0f, 0.8f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "release", "Release", 0.0f, 5.0f, 0.5f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "level", "Level", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "fm", "FM Amt", 0.0f, 100.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "ratio", "Ratio", 0.0f, 20.0f, 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(opPrefix + "phase", "Phase", 0.0f, 1.0f, 0.0f));
    }

    // Routing Matrix
    for (int from = 0; from < 12; ++from) {
        for (int to = 0; to < 12; ++to) {
            params.push_back(std::make_unique<juce::AudioParameterBool>("route_" + juce::String(from) + "_" + juce::String(to), "Route", false));
        }
    }

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FM12SynthAudioProcessor();
}
