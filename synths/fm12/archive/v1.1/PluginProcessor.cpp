#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
// Optimized Helper Functions
//==============================================================================

// ===== SINE TABLE STORAGE =====
std::array<float, FM12SynthAudioProcessor::sineTableSize>
FM12SynthAudioProcessor::sineTable;

inline float FM12SynthAudioProcessor::fastSin(float phase) noexcept
{
    const float pos = phase * (float)sineTableSize;
    const int i0 = (int)pos & sineTableMask;
    const int i1 = (i0 + 1) & sineTableMask;
    const float frac = pos - (int)pos;

    return sineTable[i0] + frac * (sineTable[i1] - sineTable[i0]);
}

// Anti-Aliasing Helper: Frequency Folding (optimized)
static inline float foldFrequency(float fIn, float nyquist) noexcept
{
    if (nyquist >= 22050.0f) return fIn;
    if (nyquist <= 1.0f) return 0.0f;

    const float virtualSR = nyquist * 2.0f;
    return std::fabs(fIn - virtualSR * std::round(fIn / virtualSR));
}

// Triviale Sound-Klasse
struct FM12Sound : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

//==============================================================================
// FM12Voice Implementation (Optimized)
//==============================================================================

FM12Voice::FM12Voice(FM12SynthAudioProcessor& p)
    : processor(p), parameters(p.apvts)
{
    for (auto& env : opEnvelopes)
        env.setSampleRate(sampleRate);
    
    activeConnections.reserve(numOperators * numOperators);
    activeOperatorIndices.reserve(numOperators);
}

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

String FM12Voice::feedbackID(int op)
{
    return "feedback_" + String(op);
}

bool FM12Voice::canPlaySound(juce::SynthesiserSound* s)
{
    return dynamic_cast<FM12Sound*>(s) != nullptr;
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

    // --- 1. PRE-CALCULATION ---
    const float masterVol = *parameters.getRawParameterValue("masterVol");
    const float nyquist = *parameters.getRawParameterValue("nyquistLimit");
    const double noteFreq = juce::MidiMessage::getMidiNoteInHertz(noteMidi);
    const bool fmMode = *parameters.getRawParameterValue("adsrAffectsFM") > 0.5f;

    const float invSampleRate = 1.0f / (float)sampleRate;
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float invTwoPi = 1.0f / twoPi;

    alignas(16) float opRatio[numOperators];
    alignas(16) float opLevel[numOperators];
    alignas(16) float opFeedbackAmount[numOperators];

    activeConnections.clear();
    activeOperatorIndices.clear();

    bool opIsActive[numOperators] = { false };

    for (int i = 0; i < numOperators; ++i)
    {
        opRatio[i] = *parameters.getRawParameterValue(opParamID(i, "ratio"));
        opLevel[i] = *parameters.getRawParameterValue(opParamID(i, "level"));
        opFeedbackAmount[i] = *parameters.getRawParameterValue(feedbackID(i));
        isCarrier[i] = true;
    }

    for (int src = 0; src < numOperators; ++src)
    {
        for (int dst = 0; dst < numOperators; ++dst)
        {
            if (src == dst) continue;

            if (*parameters.getRawParameterValue(routeID(src, dst)) > 0.5f)
            {
                activeConnections.push_back({ src, dst });
                isCarrier[src] = false;
                opIsActive[src] = true;
                opIsActive[dst] = true;
            }
        }
    }

    for (int i = 0; i < numOperators; ++i)
    {
        if (opFeedbackAmount[i] > 0.001f)
            opIsActive[i] = true;
        if (isCarrier[i])
            opIsActive[i] = true;

        if (opIsActive[i])
            activeOperatorIndices.push_back(i);
    }

    if (activeOperatorIndices.empty())
        return;

    const int numChannels = outputBuffer.getNumChannels();

    // --- 2. AUDIO LOOP ---
    for (int s = 0; s < numSamples; ++s)
    {
        alignas(16) float env[numOperators];
        for (int op : activeOperatorIndices)
            env[op] = opEnvelopes[op].getNextSample();

        alignas(16) float fmIn[numOperators] = { 0.0f };

        // Modulator Accumulation
        if (fmMode)
        {
            for (const auto& c : activeConnections)
                fmIn[c.dest] += currentRawSamples[c.source] * env[c.source] * opLevel[c.source] * 10.0f;
        }
        else
        {
            for (const auto& c : activeConnections)
                fmIn[c.dest] += currentRawSamples[c.source] * env[c.source] * opLevel[c.source] * 5.0f;
        }

        float mixOut = 0.0f;

        for (int op : activeOperatorIndices)
        {
            const float baseFreq = (float)noteFreq * opRatio[op];

            // FIXED: Self-Feedback Logic
            if (opFeedbackAmount[op] > 0.001f)
            {
                if (fmMode)
                {
                    const float fbAmount = opFeedbackAmount[op] * opLevel[op];
                    fmIn[op] += opLastOutput[op] * fbAmount * 10.0f;
                }
                else
                {
                    // For PM Sawtooth feedback, we need a depth of approx 1.0 to 2.0.
                    // We use the actual knob parameter here and scale it to 8.0 
                    // to reach "noise" territory at max settings.
                    const float fbDepth = 8.0f;
                    fmIn[op] += opLastOutput[op] * opFeedbackAmount[op] * fbDepth;
                }
            }

            float finalFreq = baseFreq;

            if (fmMode)
            {
                const float fmFactor = std::exp2(fmIn[op]);
                finalFreq = baseFreq * fmFactor;
            }
            else
            {
                // Applying accumulated modulation (external + self-feedback) to phase
                opPhase[op] += fmIn[op] * invTwoPi;
            }

            finalFreq = foldFrequency(finalFreq, nyquist);

            opPhase[op] += finalFreq * invSampleRate;
            opPhase[op] -= std::floor(opPhase[op]);

            const float rawSample = FM12SynthAudioProcessor::fastSin(opPhase[op]);
            currentRawSamples[op] = rawSample;

            opOutput[op] = rawSample * env[op] * opLevel[op];
            opLastOutput[op] = rawSample;

            if (isCarrier[op])
                mixOut += opOutput[op];
        }

        const float finalSample = mixOut * masterVol * level * 0.5f;
        const int globalSample = startSample + s;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (auto* channelData = outputBuffer.getWritePointer(ch))
                channelData[globalSample] += finalSample;
        }

        if ((s & 63) == 0)
        {
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
}

//==============================================================================
// FM12SynthAudioProcessor Implementation (with Chorus)
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
    // Initialize sine table (once)
    static bool sineTableInitialized = false;

    if (!sineTableInitialized)
    {
        const float invSize = 1.0f / (float)sineTableSize;
        for (int i = 0; i < sineTableSize; ++i)
        {
            sineTable[i] = std::sin(juce::MathConstants<float>::twoPi * (float)i * invSize);
        }
        sineTableInitialized = true;
    }

    // Pre-allocate voices
    for (int i = 0; i < maxVoices; ++i)
        synth.addVoice(new FM12Voice(*this));

    synth.addSound(new FM12Sound());
}

FM12SynthAudioProcessor::~FM12SynthAudioProcessor() = default;

void FM12SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate(sampleRate);

    // Clear chorus buffers
    std::fill(std::begin(chorusDelayBufferL), std::end(chorusDelayBufferL), 0.0f);
    std::fill(std::begin(chorusDelayBufferR), std::end(chorusDelayBufferR), 0.0f);
    chorusWritePos = 0;
    chorusLFOPhase = 0.0f;
}

void FM12SynthAudioProcessor::releaseResources()
{
}

void FM12SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    
    // Render synth
    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    // Apply chorus effect
    const float chorusAmount = *apvts.getRawParameterValue("chorusAmount");
    
    if (chorusAmount > 0.001f)
    {
        const float chorusWidth = *apvts.getRawParameterValue("chorusWidth");
        const int numSamples = buffer.getNumSamples();
        const int numChannels = jmin(buffer.getNumChannels(), 2);

        const float lfoSpeed = 0.5f; // Hz
        const float lfoIncrement = lfoSpeed / (float)currentSampleRate;
        
        float* channelData[2] = {
            numChannels > 0 ? buffer.getWritePointer(0) : nullptr,
            numChannels > 1 ? buffer.getWritePointer(1) : nullptr
        };

        for (int i = 0; i < numSamples; ++i)
        {
            // LFO for chorus modulation
            chorusLFOPhase += lfoIncrement;
            if (chorusLFOPhase >= 1.0f)
                chorusLFOPhase -= 1.0f;

            const float lfo = FM12SynthAudioProcessor::fastSin(chorusLFOPhase);
            
            // Different delay times for stereo width
            const float baseDelay = 0.003f; // 3ms base delay
            const float modDepth = 0.002f * chorusWidth; // width control
            
            const float delayTimeL = baseDelay + modDepth * lfo;
            const float delayTimeR = baseDelay + modDepth * (-lfo); // inverted for width
            
            const float delaySamplesL = delayTimeL * (float)currentSampleRate;
            const float delaySamplesR = delayTimeR * (float)currentSampleRate;

            // Process left channel
            if (channelData[0])
            {
                const float dry = channelData[0][i];
                
                chorusDelayBufferL[chorusWritePos] = dry;
                
                const float readPosL = chorusWritePos - delaySamplesL;
                const int readIdxL = ((int)readPosL + maxChorusDelay) & (maxChorusDelay - 1);
                const int readIdxL1 = (readIdxL + 1) & (maxChorusDelay - 1);
                const float fracL = readPosL - (int)readPosL;
                
                const float wet = chorusDelayBufferL[readIdxL] + fracL * (chorusDelayBufferL[readIdxL1] - chorusDelayBufferL[readIdxL]);
                
                channelData[0][i] = dry * (1.0f - chorusAmount * 0.5f) + wet * chorusAmount * 0.5f;
            }

            // Process right channel
            if (channelData[1])
            {
                const float dry = channelData[1][i];
                
                chorusDelayBufferR[chorusWritePos] = dry;
                
                const float readPosR = chorusWritePos - delaySamplesR;
                const int readIdxR = ((int)readPosR + maxChorusDelay) & (maxChorusDelay - 1);
                const int readIdxR1 = (readIdxR + 1) & (maxChorusDelay - 1);
                const float fracR = readPosR - (int)readPosR;
                
                const float wet = chorusDelayBufferR[readIdxR] + fracR * (chorusDelayBufferR[readIdxR1] - chorusDelayBufferR[readIdxR]);
                
                channelData[1][i] = dry * (1.0f - chorusAmount * 0.5f) + wet * chorusAmount * 0.5f;
            }

            chorusWritePos = (chorusWritePos + 1) & (maxChorusDelay - 1);
        }
    }
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

juce::AudioProcessorValueTreeState::ParameterLayout
FM12SynthAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Helper lambda for 4 decimal places display
    auto stringFromVal = [](float value, int) { return juce::String(value, 4); };
    auto valFromString = [](const juce::String& text) { return text.getFloatValue(); };

    // --- Global ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "masterVol", "Master Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f,
        juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "nyquistLimit", "Nyquist Limit",
        juce::NormalisableRange<float>(1000.0f, 22050.0f, 1.0f), 22050.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1); }, valFromString)); // Hz braucht nur 1 Dezimalstelle

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "adsrAffectsFM", "FM Mode", false));

    // --- Chorus ---
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusAmount", "Chorus Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusWidth", "Chorus Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f,
        juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));

    // --- Operators ---
    for (int op = 0; op < 12; ++op)
    {
        auto p = "op" + juce::String(op) + "_";

        // ADSR (Standard linear range, but with 4 decimals)
        params.push_back(std::make_unique<juce::AudioParameterFloat>(p + "attack", "Attack", juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f), 0.1f, juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(p + "decay", "Decay", juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f), 0.1f, juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(p + "sustain", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f, juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(p + "release", "Release", juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f), 0.5f, juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));

        // LEVEL: Hier ist der SKEW Factor (0.3f) entscheidend für PM Mode!
        // 0.3 bedeutet: Der Knob ist in der Mitte bei ca. 0.12 (statt 0.5).
        // Das gibt viel mehr Auflösung im unteren Bereich.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "level", "Level",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f, 0.3f), // Skew 0.3
            0.0f,
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            stringFromVal,
            valFromString));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "ratio", "Ratio",
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.0001f), 1.0f,
            juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "phase", "Phase",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
            juce::String(), juce::AudioProcessorParameter::genericParameter, stringFromVal, valFromString));
    }

    // --- Feedback ---
    for (int op = 0; op < 12; ++op)
    {
        // FEEDBACK: Auch hier Skew 0.3 für feinere Kontrolle bei wenig Feedback
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "feedback_" + juce::String(op),
            "Feedback " + juce::String(op),
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f, 0.3f), // Skew 0.3
            0.0f,
            juce::String(),
            juce::AudioProcessorParameter::genericParameter,
            stringFromVal,
            valFromString));
    }

    // --- Routing Matrix ---
    for (int from = 0; from < 12; ++from)
    {
        for (int to = 0; to < 12; ++to)
        {
            if (from != to)
            {
                params.push_back(std::make_unique<juce::AudioParameterBool>(
                    "route_" + juce::String(from) + "_" + juce::String(to),
                    "Route", false));
            }
        }
    }

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FM12SynthAudioProcessor();
}
