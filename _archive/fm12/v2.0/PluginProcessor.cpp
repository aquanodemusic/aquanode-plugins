#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
// Sine table storage
//==============================================================================
std::array<float, FM12SynthAudioProcessor::sineTableSize>
FM12SynthAudioProcessor::sineTable;

inline float FM12SynthAudioProcessor::fastSin(float phase) noexcept
{
    const float pos = phase * (float)sineTableSize;
    const int   i0 = (int)pos & sineTableMask;
    const int   i1 = (i0 + 1) & sineTableMask;
    const float frac = pos - (int)pos;
    return sineTable[i0] + frac * (sineTable[i1] - sineTable[i0]);
}

// Frequency fold/alias helper.
// Returns fIn unchanged when nyquist >= 24000 (full bypass).
// For lower values, folds fIn (which may be positive or negative) into [0, nyquist].
static inline float foldFrequency(float fIn, float nyquist) noexcept
{
    if (nyquist >= 24000.0f) return fIn;
    if (nyquist <= 1.0f)     return 0.0f;
    const float virtualSR = nyquist * 2.0f;
    return std::fabs(fIn - virtualSR * std::round(fIn / virtualSR));
}

// Trivial sound class
struct FM12Sound : public juce::SynthesiserSound
{
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

//==============================================================================
// FM12Voice
//==============================================================================
FM12Voice::FM12Voice(FM12SynthAudioProcessor& p)
    : processor(p), parameters(p.apvts)
{
    for (auto& env : opEnvelopes)
        env.setSampleRate(sampleRate);

    activeConnections.reserve(numOperators * numOperators);
    activeOperatorIndices.reserve(numOperators);

    // Cache parameter pointers once — avoids hash lookups in the audio loop
    pMasterVol = parameters.getRawParameterValue("masterVol");
    pNyquist = parameters.getRawParameterValue("nyquistLimit");
    pFmEngineMode = parameters.getRawParameterValue("fmEngineMode");
    pExpFbMode = parameters.getRawParameterValue("feedbackModeExp");

    for (int i = 0; i < numOperators; ++i)
    {
        pOpRatio[i] = parameters.getRawParameterValue(opParamID(i, "ratio"));
        pOpLevel[i] = parameters.getRawParameterValue(opParamID(i, "level"));
        pOpFeedback[i] = parameters.getRawParameterValue(feedbackID(i));
        pOpAttack[i] = parameters.getRawParameterValue(opParamID(i, "attack"));
        pOpDecay[i] = parameters.getRawParameterValue(opParamID(i, "decay"));
        pOpSustain[i] = parameters.getRawParameterValue(opParamID(i, "sustain"));
        pOpRelease[i] = parameters.getRawParameterValue(opParamID(i, "release"));

        for (int j = 0; j < numOperators; ++j)
            pRoute[i][j] = (i != j) ? parameters.getRawParameterValue(routeID(i, j)) : nullptr;
    }
}

String FM12Voice::opParamID(int op, const char* name) { return "op" + String(op) + "_" + String(name); }
String FM12Voice::routeID(int from, int to) { return "route_" + String(from) + "_" + String(to); }
String FM12Voice::feedbackID(int op) { return "feedback_" + String(op); }

bool FM12Voice::canPlaySound(juce::SynthesiserSound* s)
{
    return dynamic_cast<FM12Sound*>(s) != nullptr;
}

void FM12Voice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    noteMidi = midiNoteNumber;
    level = velocity;

    isBeingKilled = false;
    killRampCounter = 0;

    for (int i = 0; i < numOperators; ++i)
    {
        opPhase[i] = 0.0f;
        opLastOutput[i] = 0.0f;
        opOutput[i] = 0.0f;
        expFeedback[i] = 0.0f;
        expDelayWrite[i] = 0;
        std::fill(std::begin(expDelayBuf[i]), std::end(expDelayBuf[i]), 0.0f);

        juce::ADSR::Parameters adsr;
        adsr.attack = *pOpAttack[i];
        adsr.decay = *pOpDecay[i];
        adsr.sustain = *pOpSustain[i];
        adsr.release = *pOpRelease[i];

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
    {
        isBeingKilled = true;
        killRampCounter = killRampSamples;
    }
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

//==============================================================================
void FM12Voice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
    int startSample,
    int numSamples)
{
    if (!isVoiceActive())
        return;

    // ─── Per-block constants ──────────────────────────────────────────────────
    const float masterVol = *pMasterVol;
    const float nyquist = *pNyquist;
    const double noteFreq = juce::MidiMessage::getMidiNoteInHertz(noteMidi);

    // FM engine mode:
    //   0 = Phase Modulation (DX7 / Dexed style)
    //   1 = Linear FM
    //   2 = Linear FM Through Zero
    //   3 = Exponential FM
    //   4 = Exponential FM Through Zero (sinh-based)
    const int  fmMode = (int)(*pFmEngineMode + 0.5f);
    const bool expFeedbackMode = *pExpFbMode > 0.5f;
    const bool nyquistActive = (nyquist < 24000.0f);

    const float invSampleRate = 1.0f / (float)sampleRate;

    // ─── Modulation depth scales ──────────────────────────────────────────────
    // All modes normalised so that a single modulator at level=1 gives a similar
    // spectral richness (β ≈ 2π for PM, β ≈ 5 for LFM at ratio=1, ±2 oct for Exp).
    //
    // PM    : fmIn accumulates in cycles (0–1 per cycle).  Added directly to the
    //         phase lookup → β = fmIn × 2π.  level=1 → β ≈ 2π ≈ 6.3 rad.
    //
    // LFM   : fmIn accumulates in Hz.  Scaled by the modulator's own frequency so
    //         the modulation index (β = Δf/fm) is constant regardless of pitch or
    //         ratio, matching the Yamaha convention.  level=1, ratio=1 → β = 5.
    //
    // ExpFM : fmIn accumulates in octaves.  level=1 → ±2 octaves deviation.
    //
    // Self-feedback scales are half the modulator scales to keep self-oscillation
    // manageable without needing a lower feedback knob setting.

    static constexpr float PM_MOD_SCALE = 1.0f;   // cycles
    static constexpr float LFM_MOD_SCALE = 5.0f;   // × modFreqHz → Hz
    static constexpr float EXPFM_MOD_SCALE = 2.0f;   // octaves
    static constexpr float PM_FB_SCALE = 0.5f;   // half of PM_MOD_SCALE
    static constexpr float LFM_FB_SCALE = 2.5f;   // half of LFM_MOD_SCALE
    static constexpr float EXPFM_FB_SCALE = 1.0f;   // half of EXPFM_MOD_SCALE
    static constexpr float EXPFM_CLAMP = 5.0f;   // ±5 oct / sinh-arg clamp

    // ─── Pre-loop: gather active operators and connections ─────────────────────
    alignas(16) float opRatio[numOperators];
    alignas(16) float opLevel[numOperators];
    alignas(16) float opFeedbackAmount[numOperators];

    activeConnections.clear();
    activeOperatorIndices.clear();

    bool opIsActive[numOperators] = { false };

    for (int i = 0; i < numOperators; ++i)
    {
        opRatio[i] = *pOpRatio[i];
        opLevel[i] = *pOpLevel[i];
        opFeedbackAmount[i] = *pOpFeedback[i];
        isCarrier[i] = true;
    }

    for (int src = 0; src < numOperators; ++src)
    {
        for (int dst = 0; dst < numOperators; ++dst)
        {
            if (src == dst) continue;
            if (*pRoute[src][dst] > 0.5f)
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
        if (opFeedbackAmount[i] > 0.001f) opIsActive[i] = true;
        if (isCarrier[i])                 opIsActive[i] = true;
        if (opIsActive[i])                activeOperatorIndices.push_back(i);
    }

    if (activeOperatorIndices.empty())
        return;

    const int numChannels = outputBuffer.getNumChannels();

    // ─── Sample loop ───────────────────────────────────────────────────────────
    for (int s = 0; s < numSamples; ++s)
    {
        // Advance all active envelopes
        alignas(16) float env[numOperators];
        for (int op : activeOperatorIndices)
            env[op] = opEnvelopes[op].getNextSample();

        // Reset fmIn for this sample (uses previous sample's currentRawSamples)
        alignas(16) float fmIn[numOperators] = { 0.0f };

        // ── Modulator accumulation ────────────────────────────────────────────
        // Uses currentRawSamples (one-sample-delayed outputs) — avoids ordering
        // dependencies and matches standard FM synthesis practice.
        for (const auto& c : activeConnections)
        {
            // Modulators (non-carriers) get a depth boost so that level=1.0
            // produces a meaningfully rich FM spectrum. Carriers are excluded —
            // they never appear as c.source in activeConnections, but the guard
            // makes the intent explicit and safe against future routing changes.
            // PM / LFM use 21x; ExpFM uses 4x (its exponential nature means it
            // already reaches high modulation indices much faster).
            const float modBoost = (fmMode <= 2) ? 21.0f : 4.0f;
            const float modScale = (!isCarrier[c.source]) ? modBoost : 1.0f;
            const float modSig = currentRawSamples[c.source]
                * env[c.source]
                * opLevel[c.source]
                * modScale;
            const float modFreqHz = opRatio[c.source] * (float)noteFreq;

            switch (fmMode)
            {
            case 0: // PM — phase offset in cycles
                fmIn[c.dest] += modSig * PM_MOD_SCALE;
                break;

            case 1: case 2: // LFM / LFM-TZ — frequency deviation in Hz
                // Scaling by the modulator's own frequency gives a constant
                // modulation index β = LFM_MOD_SCALE regardless of pitch or ratio.
                fmIn[c.dest] += modSig * modFreqHz * LFM_MOD_SCALE;
                break;

            case 3: case 4: // ExpFM / ExpFM-TZ — deviation in octaves
                fmIn[c.dest] += modSig * EXPFM_MOD_SCALE;
                break;
            }
        }

        // Clamp fmIn for ExpFM modes to prevent exp2/sinh overflow
        if (fmMode >= 3)
        {
            for (int op : activeOperatorIndices)
                fmIn[op] = juce::jlimit(-EXPFM_CLAMP, EXPFM_CLAMP, fmIn[op]);
        }

        float mixOut = 0.0f;

        for (int op : activeOperatorIndices)
        {
            const float baseFreq = (float)noteFreq * opRatio[op];

            // ── Self-feedback (direct mode) ── active when EXP FB toggle is OFF ──
            // Matches OLD_PROCESSOR.cpp logic exactly: !expFeedbackMode = direct feedback,
            // expFeedbackMode = anyFM delay ring buffer (below).
            if (!expFeedbackMode && opFeedbackAmount[op] > 0.001f)
            {
                const float fbSig = opLastOutput[op] * opFeedbackAmount[op];

                switch (fmMode)
                {
                case 0: // PM
                    fmIn[op] += fbSig * PM_MOD_SCALE * PM_FB_SCALE;
                    break;

                case 1: case 2: // LFM
                    fmIn[op] += fbSig * baseFreq * LFM_FB_SCALE;
                    break;

                case 3: case 4: // ExpFM — clamp after adding to prevent runaway
                    fmIn[op] = juce::jlimit(-EXPFM_CLAMP, EXPFM_CLAMP,
                        fmIn[op] + fbSig * EXPFM_FB_SCALE);
                    break;
                }
            }

            // ── Phase computation ─────────────────────────────────────────────
            float rawSample;

            if (fmMode == 0) // ── Phase Modulation ─────────────────────────────
            {
                // Carrier phase advances at the base frequency.
                // The Nyquist slider folds the fundamental (bitcrush effect) but
                // does not directly limit modulation sidebands — this is consistent
                // with how the old processor applied nyquist to the LFM frequency.
                float advFreq = nyquistActive ? foldFrequency(baseFreq, nyquist) : baseFreq;
                opPhase[op] += advFreq * invSampleRate;
                opPhase[op] -= std::floor(opPhase[op]);

                // Modulation is applied as a direct phase offset at lookup time.
                // fmIn is in 0–1 cycle units, so β = fmIn × 2π.
                rawSample = FM12SynthAudioProcessor::fastSin(opPhase[op] + fmIn[op]);
            }
            else // ── Frequency-modulated modes ──────────────────────────────────
            {
                float finalFreq;

                switch (fmMode)
                {
                case 1: // Linear FM (no TZ — fold negative to positive)
                    finalFreq = baseFreq + fmIn[op];
                    finalFreq = nyquistActive ? foldFrequency(finalFreq, nyquist)
                        : std::fabs(finalFreq);
                    break;

                case 2: // Linear FM Through Zero (allow backward-running phase)
                    finalFreq = baseFreq + fmIn[op];
                    if (nyquistActive)
                        finalFreq = juce::jlimit(-nyquist, nyquist, finalFreq);
                    break;

                case 3: // Exponential FM (exp2, always positive)
                    finalFreq = baseFreq * std::exp2(fmIn[op]);
                    // exp2 > 0 always, so no fabs needed at bypass
                    if (nyquistActive)
                        finalFreq = foldFrequency(finalFreq, nyquist);
                    break;

                case 4: // Exponential FM Through Zero (sinh-based)
                    // sinh is symmetric and grows exponentially in both directions,
                    // producing ExpFM character in the positive direction and
                    // true through-zero behaviour as fmIn goes negative.
                    // At fmIn=0: finalFreq = baseFreq (correct).
                    // At fmIn→-∞: finalFreq → -∞ (phase runs backwards).
                    finalFreq = baseFreq + baseFreq * std::sinh(fmIn[op]);
                    if (nyquistActive)
                        finalFreq = juce::jlimit(-nyquist, nyquist, finalFreq);
                    break;

                default:
                    finalFreq = baseFreq;
                }

                opPhase[op] += finalFreq * invSampleRate;
                opPhase[op] -= std::floor(opPhase[op]);
                rawSample = FM12SynthAudioProcessor::fastSin(opPhase[op]);
            }

            // opLastOutput feeds regular self-FB (always the raw sine)
            opLastOutput[op] = rawSample;

            float opCarrier = rawSample * env[op] * opLevel[op];
            float finalOpSample = opCarrier;

            // ── anyFM Experimental feedback (delay ring buffer) ──
            // Active when EXP FB toggle is ON (expFeedbackMode = true).
            // Replaces the carrier output with a delay-modulated signal and,
            // crucially, also feeds the processed result back into currentRawSamples
            // so that downstream modulators receive the self-FM'd sound.
            if (expFeedbackMode && opFeedbackAmount[op] > 0.001f)
            {
                static constexpr float kThreshold = 0.0005f;
                static constexpr float kDecay = 0.993f;
                static constexpr float kDepth = 1000.0f;

                expDelayBuf[op][expDelayWrite[op]] = opCarrier;

                if (std::abs(rawSample) < kThreshold)
                    expFeedback[op] *= kDecay;

                float readPos = (float)expDelayWrite[op]
                    - (float)expDelayFixed
                    + expFeedback[op] * opFeedbackAmount[op] * kDepth;

                const float bufLen = (float)expDelaySize;
                while (readPos < 0.0f)    readPos += bufLen;
                while (readPos >= bufLen) readPos -= bufLen;

                const int   iA = (int)readPos & expDelayMask;
                const int   iB = (iA + 1) & expDelayMask;
                const float fr = readPos - (float)(int)readPos;
                const float wet = expDelayBuf[op][iA] * (1.0f - fr)
                    + expDelayBuf[op][iB] * fr;

                expFeedback[op] = wet;
                finalOpSample = juce::jlimit(-0.75f, 0.75f, wet);

                expDelayWrite[op] = (expDelayWrite[op] + 1) & expDelayMask;

                // Normalise finalOpSample back to a raw-signal range so the
                // modulator accumulation (which will re-apply env*level) stays
                // correctly scaled.  Guard against near-zero env*level.
                const float envLevel = env[op] * opLevel[op];
                currentRawSamples[op] = (envLevel > 0.0001f)
                    ? finalOpSample / envLevel
                    : rawSample;
            }
            else
            {
                // Default: modulator signal is the unprocessed raw sine
                currentRawSamples[op] = rawSample;
            }

            opOutput[op] = finalOpSample;

            if (isCarrier[op])
                mixOut += opOutput[op];
        }

        float finalSample = mixOut * masterVol * level * 1.25f;

        // Kill ramp — fade stolen voice to zero, avoiding click
        if (isBeingKilled)
        {
            finalSample *= (float)killRampCounter / (float)killRampSamples;

            if (--killRampCounter <= 0)
            {
                const int gs = startSample + s;
                for (int ch = 0; ch < numChannels; ++ch)
                    if (auto* d = outputBuffer.getWritePointer(ch))
                        d[gs] += finalSample;

                clearCurrentNote();
                isVoiceActiveFlag = false;
                isBeingKilled = false;
                break;
            }
        }

        const int gs = startSample + s;
        for (int ch = 0; ch < numChannels; ++ch)
            if (auto* d = outputBuffer.getWritePointer(ch))
                d[gs] += finalSample;

        // Envelope expiry check (every 64 samples to save CPU)
        if (!isBeingKilled && (s & 63) == 0)
        {
            bool anyActive = false;
            for (int op : activeOperatorIndices)
                if (opEnvelopes[op].isActive()) { anyActive = true; break; }

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
// FM12SynthAudioProcessor
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
    static bool sineTableInitialized = false;
    if (!sineTableInitialized)
    {
        const float invSize = 1.0f / (float)sineTableSize;
        for (int i = 0; i < sineTableSize; ++i)
            sineTable[i] = std::sin(juce::MathConstants<float>::twoPi * (float)i * invSize);
        sineTableInitialized = true;
    }

    for (int i = 0; i < maxVoices; ++i)
        synth.addVoice(new FM12Voice(*this));

    synth.addSound(new FM12Sound());
}

FM12SynthAudioProcessor::~FM12SynthAudioProcessor() = default;

void FM12SynthAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate(sampleRate);

    std::fill(std::begin(chorusDelayBufferL), std::end(chorusDelayBufferL), 0.0f);
    std::fill(std::begin(chorusDelayBufferR), std::end(chorusDelayBufferR), 0.0f);
    chorusWritePos = 0;
    chorusLFOPhase = 0.0f;
}

void FM12SynthAudioProcessor::releaseResources() {}

void FM12SynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());

    // ── Chorus ────────────────────────────────────────────────────────────────
    const float chorusAmount = *apvts.getRawParameterValue("chorusAmount");
    if (chorusAmount > 0.001f)
    {
        const float chorusWidth = *apvts.getRawParameterValue("chorusWidth");
        const int   numSamples = buffer.getNumSamples();
        const int   numChannels = jmin(buffer.getNumChannels(), 2);

        const float lfoSpeed = 0.5f;
        const float lfoIncrement = lfoSpeed / (float)currentSampleRate;

        float* channelData[2] = {
            numChannels > 0 ? buffer.getWritePointer(0) : nullptr,
            numChannels > 1 ? buffer.getWritePointer(1) : nullptr
        };

        for (int i = 0; i < numSamples; ++i)
        {
            chorusLFOPhase += lfoIncrement;
            if (chorusLFOPhase >= 1.0f) chorusLFOPhase -= 1.0f;

            const float lfo = FM12SynthAudioProcessor::fastSin(chorusLFOPhase);
            const float baseDelay = 0.003f;
            const float modDepth = 0.002f * chorusWidth;

            const float delaySamplesL = (baseDelay + modDepth * lfo) * (float)currentSampleRate;
            const float delaySamplesR = (baseDelay + modDepth * (-lfo)) * (float)currentSampleRate;

            if (channelData[0])
            {
                const float dry = channelData[0][i];
                chorusDelayBufferL[chorusWritePos] = dry;
                const float readPosL = chorusWritePos - delaySamplesL;
                const int   rL = ((int)readPosL + maxChorusDelay) & (maxChorusDelay - 1);
                const int   rL1 = (rL + 1) & (maxChorusDelay - 1);
                const float frL = readPosL - (int)readPosL;
                const float wet = chorusDelayBufferL[rL] + frL * (chorusDelayBufferL[rL1] - chorusDelayBufferL[rL]);
                channelData[0][i] = dry * (1.0f - chorusAmount * 0.5f) + wet * chorusAmount * 0.5f;
            }

            if (channelData[1])
            {
                const float dry = channelData[1][i];
                chorusDelayBufferR[chorusWritePos] = dry;
                const float readPosR = chorusWritePos - delaySamplesR;
                const int   rR = ((int)readPosR + maxChorusDelay) & (maxChorusDelay - 1);
                const int   rR1 = (rR + 1) & (maxChorusDelay - 1);
                const float frR = readPosR - (int)readPosR;
                const float wet = chorusDelayBufferR[rR] + frR * (chorusDelayBufferR[rR1] - chorusDelayBufferR[rR]);
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

    auto stringFromVal = [](float value, int) { return juce::String(value, 4); };
    auto valFromString = [](const juce::String& text) { return text.getFloatValue(); };

    // ── Global ────────────────────────────────────────────────────────────────
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "masterVol", "Master Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        stringFromVal, valFromString));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "nyquistLimit", "Nyquist Limit",
        juce::NormalisableRange<float>(100.0f, 24000.0f, 1.0f, 0.35f), 24000.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1); }, valFromString));

    // FM engine mode — replaces the old adsrAffectsFM bool and throughZeroFM bool.
    //   0 = Phase Modulation Mode  (DX7 / Dexed style)
    //   1 = Linear FM Mode
    //   2 = Lin. FM Through Zero
    //   3 = Exponential FM Mode
    //   4 = Exp. FM Through Zero
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "fmEngineMode", "FM Engine Mode",
        juce::StringArray{
            "Phase Modulation Mode",
            "Linear FM Mode",
            "Lin. FM Through Zero",
            "Exponential FM Mode",
            "Exp. FM Through Zero"
        },
        0));  // default: Phase Modulation

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "feedbackModeExp", "EXP Feedback", false));

    // ── Chorus ────────────────────────────────────────────────────────────────
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusAmount", "Chorus Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        stringFromVal, valFromString));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "chorusWidth", "Chorus Width",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f,
        juce::String(), juce::AudioProcessorParameter::genericParameter,
        stringFromVal, valFromString));

    // ── Operators ─────────────────────────────────────────────────────────────
    for (int op = 0; op < 12; ++op)
    {
        auto p = "op" + juce::String(op) + "_";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "attack", "Attack",
            juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f), 0.05f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "decay", "Decay",
            juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f), 0.1f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "sustain", "Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "release", "Release",
            juce::NormalisableRange<float>(0.0f, 5.0f, 0.001f), 0.5f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));

        // Skew 0.3 — fine resolution at low levels
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "level", "Level",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f, 0.3f),
            (op == 0) ? 1.0f : 0.0f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            p + "ratio", "Ratio",
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.0001f), 1.0f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));
    }

    // ── Feedback knobs ────────────────────────────────────────────────────────
    for (int op = 0; op < 12; ++op)
    {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            "feedback_" + juce::String(op), "Feedback " + juce::String(op),
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f, 0.3f), 0.0f,
            juce::String(), juce::AudioProcessorParameter::genericParameter,
            stringFromVal, valFromString));
    }

    // ── Routing matrix ────────────────────────────────────────────────────────
    for (int from = 0; from < 12; ++from)
        for (int to = 0; to < 12; ++to)
            if (from != to)
                params.push_back(std::make_unique<juce::AudioParameterBool>(
                    "route_" + juce::String(from) + "_" + juce::String(to), "Route", false));

    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FM12SynthAudioProcessor();
}