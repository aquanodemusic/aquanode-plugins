#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TranswaveAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>("position", "WT Position", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("evolution", "Evolution Speed", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("evoLFORate", "Evo LFO Rate", juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("evoLFODepth", "Evo LFO Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("posLFORate", "Pos LFO Rate", juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("posLFODepth", "Pos LFO Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("detune", "Detune (cents)", juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("pitchLFO", "Pitch LFO Depth (st)", juce::NormalisableRange<float>(0.0f, 12.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("pitchLFORate", "Pitch LFO Rate", juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.4f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("attack", "Attack", juce::NormalisableRange<float>(0.001f, 8.0f, 0.001f, 0.35f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("decay", "Decay", juce::NormalisableRange<float>(0.001f, 8.0f, 0.001f, 0.35f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sustain", "Sustain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("release", "Release", juce::NormalisableRange<float>(0.001f, 8.0f, 0.001f, 0.35f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("bitCrush", "Bit Depth", juce::NormalisableRange<float>(4.0f, 16.0f, 0.01f), 16.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("grit", "Grit (Phase Jitter)", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("gain", "Output Gain", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.7f));

    // FIXED: Made Scan Style a Text Choice Parameter!
    params.push_back(std::make_unique<juce::AudioParameterChoice>("scanStyle", "Scan Style", juce::StringArray{ "Forward", "Back and Forth", "Backward" }, 0));

    // Jump probability scales from 0 to 1 (we map this to 0-50% in the synthesis loop)
    params.push_back(std::make_unique<juce::AudioParameterFloat>("jumpProb", "Jump Probability", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("filterFreq", "Filter Frequency", juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.3f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("filterQ", "Filter Resonance", juce::NormalisableRange<float>(0.1f, 12.0f, 0.01f, 0.5f), 0.707f));

    return { params.begin(), params.end() };
}

//==============================================================================
TranswaveAudioProcessor::TranswaveAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    pPosition = apvts.getRawParameterValue("position");
    pEvolution = apvts.getRawParameterValue("evolution");
    pEvoLFORate = apvts.getRawParameterValue("evoLFORate");
    pEvoLFODepth = apvts.getRawParameterValue("evoLFODepth");
    pPosLFORate = apvts.getRawParameterValue("posLFORate");
    pPosLFODepth = apvts.getRawParameterValue("posLFODepth");
    pAttack = apvts.getRawParameterValue("attack");
    pDecay = apvts.getRawParameterValue("decay");
    pSustain = apvts.getRawParameterValue("sustain");
    pRelease = apvts.getRawParameterValue("release");
    pGain = apvts.getRawParameterValue("gain");
    pBitCrush = apvts.getRawParameterValue("bitCrush");
    pGrit = apvts.getRawParameterValue("grit");
    pDetune = apvts.getRawParameterValue("detune");
    pPitchLFO = apvts.getRawParameterValue("pitchLFO");
    pPitchLFORate = apvts.getRawParameterValue("pitchLFORate");
    pScanStyle = apvts.getRawParameterValue("scanStyle");
    pJumpProb = apvts.getRawParameterValue("jumpProb");
    pFilterFreq = apvts.getRawParameterValue("filterFreq");
    pFilterQ = apvts.getRawParameterValue("filterQ");
}

TranswaveAudioProcessor::~TranswaveAudioProcessor() {}

void TranswaveAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    gainSmooth.reset(sampleRate, 0.05);
    gainSmooth.setCurrentAndTargetValue(pGain->load());

    evoLFOPhase = 0.0; posLFOPhase = 0.0; pitchLFOPhase = 0.0;
    x1 = 0.0f; x2 = 0.0f; y1 = 0.0f; y2 = 0.0f; // Reset Filter
    scanDirection = 1;
    globalEvoPos = 0.0;

    for (auto& v : voices)
    {
        v.active = false;
        v.envStage = TranswaveVoice::Env::Idle;
        v.envLevel = 0.0f;
        v.phase = 0.0;
        v.frameOffset = 0.0f;
    }
}

void TranswaveAudioProcessor::releaseResources() {}

void TranswaveAudioProcessor::loadWavetable(const juce::File& file, int singleCycleSamples)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return;

    juce::AudioBuffer<float> buf(1, (int)reader->lengthInSamples);
    reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, true);

    int totalSamples = buf.getNumSamples();
    int cs = juce::jlimit(16, totalSamples, singleCycleSamples);
    int nf = totalSamples / cs;
    if (nf < 1) return;

    auto quantise16 = [](float s) -> float {
        float q = std::round(s * 32767.0f) / 32767.0f;
        return juce::jlimit(-1.0f, 1.0f, q);
        };

    std::vector<std::vector<float>> newFrames;
    newFrames.resize((size_t)nf, std::vector<float>((size_t)cs));

    const float* src = buf.getReadPointer(0);
    for (int f = 0; f < nf; ++f)
        for (int s = 0; s < cs; ++s)
            newFrames[(size_t)f][(size_t)s] = quantise16(src[f * cs + s]);

    {
        juce::ScopedLock sl(wavetableLock);
        frames = std::move(newFrames);
        numFrames = nf;
        cycleSamples = cs;
        wavetableLoaded = true;
        wavetableName = file.getFileNameWithoutExtension();
    }

    for (auto& v : voices) { v.active = false; v.envStage = TranswaveVoice::Env::Idle; }
}

bool TranswaveAudioProcessor::getFrameSamples(int frameIndex, std::vector<float>& outBuf) const
{
    juce::ScopedLock sl(wavetableLock);
    if (frames.empty() || frameIndex < 0 || frameIndex >= numFrames) return false;
    outBuf = frames[(size_t)frameIndex];
    return true;
}

bool TranswaveAudioProcessor::getWavetableOverview(int displayWidth, int, std::vector<float>& outBuf) const
{
    juce::ScopedLock sl(wavetableLock);
    if (frames.empty() || displayWidth <= 0) return false;

    outBuf.resize((size_t)displayWidth);
    for (int px = 0; px < displayWidth; ++px)
    {
        float t = (float)px / (float)(displayWidth - 1);
        float fi = t * (float)(numFrames - 1);
        int   f0 = juce::jlimit(0, numFrames - 1, (int)fi);

        float peak = 0.0f;
        const auto& frm = frames[(size_t)f0];
        for (float s : frm) peak = juce::jmax(peak, std::abs(s));

        outBuf[(size_t)px] = peak;
    }
    return true;
}

float TranswaveAudioProcessor::sampleFrameNearest(float frameIndex, double phase)
{
    juce::ScopedLock sl(wavetableLock);
    if (frames.empty()) return 0.0f;

    int   fi0 = (int)frameIndex;
    int   fi1 = fi0 + 1;
    float blend = frameIndex - (float)fi0;

    fi0 = juce::jlimit(0, numFrames - 1, fi0);
    fi1 = juce::jlimit(0, numFrames - 1, fi1);

    int sampleIdx = (int)(phase * cycleSamples) % cycleSamples;
    sampleIdx = juce::jlimit(0, cycleSamples - 1, sampleIdx);

    float s0 = frames[(size_t)fi0][(size_t)sampleIdx];
    float s1 = frames[(size_t)fi1][(size_t)sampleIdx];
    return s0 + blend * (s1 - s0);
}

float TranswaveAudioProcessor::applyBitCrush(float s, float bits)
{
    if (bits >= 15.9f) return s;
    float levels = std::pow(2.0f, bits) - 1.0f;
    return std::round(s * levels) / levels;
}

float TranswaveAudioProcessor::synthesiseVoice(TranswaveVoice& v, float framePosNorm, float posLFOMod, double pitchMult)
{
    if (!v.active) return 0.0f;

    float invSR = (float)(1.0 / currentSampleRate);
    float attack = juce::jmax(0.001f, pAttack->load());
    float decay = juce::jmax(0.001f, pDecay->load());
    float sustain = pSustain->load();
    float release = juce::jmax(0.001f, pRelease->load());

    switch (v.envStage)
    {
    case TranswaveVoice::Env::Attack:
        v.envLevel += invSR / attack;
        if (v.envLevel >= 1.0f) { v.envLevel = 1.0f; v.envStage = TranswaveVoice::Env::Decay; v.envTime = 0.0f; }
        break;
    case TranswaveVoice::Env::Decay:
        v.envLevel -= invSR * (1.0f - sustain) / decay;
        if (v.envLevel <= sustain) { v.envLevel = sustain; v.envStage = TranswaveVoice::Env::Sustain; }
        break;
    case TranswaveVoice::Env::Sustain: v.envLevel = sustain; break;
    case TranswaveVoice::Env::Release:
        v.envLevel -= invSR * v.releaseStartLevel / release;
        if (v.envLevel <= 0.0f) { v.envLevel = 0.0f; v.envStage = TranswaveVoice::Env::Idle; v.active = false; return 0.0f; }
        break;
    case TranswaveVoice::Env::Idle: v.active = false; return 0.0f;
    }

    // Step the phase forward
    double midiFreq = 440.0 * std::pow(2.0, (v.midiNote - 69.0) / 12.0);
    double detuneMult = std::pow(2.0, pDetune->load() / 1200.0);
    double freq = midiFreq * detuneMult * pitchMult;
    v.phase += freq / currentSampleRate;

    // CYCLE EVENT TRIGGER (Evaluated exactly when the frame wraps)
    //if (v.phase >= 1.0)
    //{
    //    v.phase -= 1.0;
    //
    //    float jumpProb = pJumpProb->load();
    //    // 0.5f multiplier gives a max probability of 50% when the knob is at full
    //    if (jumpProb > 0.0f && ((float)std::rand() / (float)RAND_MAX) < (jumpProb * 0.5f)) {
    //        v.jumpActive = true;
    //        v.jumpFramePosNorm = (float)std::rand() / (float)RAND_MAX; // Store a random target cycle
    //    }
    //    else {
    //        v.jumpActive = false; // Resume normal scanning trajectory
    //    }
    //}

    // CYCLE EVENT TRIGGER (Evaluated exactly when the frame wraps)
    if (v.phase >= 1.0)
    {
        v.phase -= 1.0;

        float jumpProb = pJumpProb->load();
        if (jumpProb > 0.0f && ((float)std::rand() / (float)RAND_MAX) < (jumpProb * 0.5f))
        {
            float randomTarget = (float)std::rand() / (float)RAND_MAX;

            // Calculate the offset required to shift the playhead to the random target frame
            v.frameOffset = randomTarget - framePosNorm;
        }
    }

    // Determine current frame by applying the voice's persistent offset to the global sweep trajectory
    // Calculate raw voice position factoring in the random jump offset and Position LFO
    float rawVoicePos = framePosNorm + v.frameOffset + posLFOMod * 0.5f;

    // Wrap the voice playhead as well to keep everything uniform and cyclic
    rawVoicePos = std::fmod(rawVoicePos, 1.0f);
    if (rawVoicePos < 0.0f) rawVoicePos += 1.0f;

    float fp = rawVoicePos;
    float frameIndex = fp * (float)(numFrames - 1);

    // --- GRIT: Probabilistic Integer Sample Snapping ---
    double lookupPhase = v.phase;
    float grit = pGrit->load();

    if (grit > 0.0f)
    {
        // Scale probability
        // This keeps the effect subtle, musical, and vintage at lower values
        float gritProbability = grit * 0.75f;

        if (((float)std::rand() / (float)RAND_MAX) < gritProbability)
        {
            // Find the exact floating point position in terms of samples
            double exactSamplePosition = lookupPhase * cycleSamples;

            // Snap cleanly to the nearest integer sample boundary 
            // This mimics an accumulator dropped-bit step or alignment fault
            lookupPhase = std::round(exactSamplePosition) / (double)cycleSamples;
        }
    }

    // Keep phase safely wrapped between 0.0 and 1.0
    while (lookupPhase < 0.0) lookupPhase += 1.0;
    while (lookupPhase >= 1.0) lookupPhase -= 1.0;

    float s = sampleFrameNearest(frameIndex, lookupPhase);
    s = applyBitCrush(s, pBitCrush->load());

    return s * v.envLevel * v.velocity;
}

float TranswaveAudioProcessor::getCurrentFramePos() const { return pPosition->load(); }

void TranswaveAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            int slot = -1;
            for (int i = 0; i < MAX_VOICES; ++i) if (!voices[i].active) { slot = i; break; }
            if (slot == -1) slot = 0;
            voices[slot].noteOn(msg.getNoteNumber(), (float)msg.getVelocity() / 127.0f);
        }
        else if (msg.isNoteOff())
        {
            for (auto& v : voices)
                if (v.active && v.midiNote == msg.getNoteNumber() && v.envStage != TranswaveVoice::Env::Release)
                    v.noteOff();
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& v : voices) { v.active = false; v.envStage = TranswaveVoice::Env::Idle; }
        }
    }

    if (!wavetableLoaded) return;

    int numSamples = buffer.getNumSamples();
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);

    gainSmooth.setTargetValue(pGain->load());

    float evolution = pEvolution->load();
    float position = pPosition->load();
    float evoLFORate = pEvoLFORate->load();
    float evoLFODepth = pEvoLFODepth->load();
    float posLFORate = pPosLFORate->load();
    float posLFODepth = pPosLFODepth->load();
    float pitchLFORate = pPitchLFORate->load();
    float pitchLFODepth = pPitchLFO->load();
    int scanStyle = (int)pScanStyle->load();

    float filterFreq = pFilterFreq->load();
    float filterQ = pFilterQ->load();
    float omega = 2.0f * juce::MathConstants<float>::pi * filterFreq / (float)currentSampleRate;
    omega = juce::jlimit(0.0f, juce::MathConstants<float>::pi * 0.99f, omega);
    float cosW = std::cos(omega);
    float alpha = std::sin(omega) / (2.0f * filterQ);
    float b0 = (1.0f - cosW) * 0.5f;
    float b1 = 1.0f - cosW;
    float b2 = b0;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosW;
    float a2 = 1.0f - alpha;
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    for (int s = 0; s < numSamples; ++s)
    {
        evoLFOPhase += evoLFORate / currentSampleRate;
        if (evoLFOPhase > 1.0) evoLFOPhase -= 1.0;

        posLFOPhase += posLFORate / currentSampleRate;
        if (posLFOPhase > 1.0) posLFOPhase -= 1.0;

        pitchLFOPhase += pitchLFORate / currentSampleRate;
        if (pitchLFOPhase > 1.0) pitchLFOPhase -= 1.0;

        float evoLFO = (float)std::sin(juce::MathConstants<double>::twoPi * evoLFOPhase);
        float posLFO = (float)std::sin(juce::MathConstants<double>::twoPi * posLFOPhase);
        float pitchLFO = (float)std::sin(juce::MathConstants<double>::twoPi * pitchLFOPhase);

        double evoStep = evolution * 0.5 / currentSampleRate;

        if (evolution == 0.0f)
        {
            globalEvoPos = 0.0;
        }
        else
        {
            if (scanStyle == 0) { globalEvoPos += evoStep; if (globalEvoPos > 1.0) globalEvoPos -= 1.0; }
            else if (scanStyle == 1) { globalEvoPos += evoStep * scanDirection; if (globalEvoPos >= 1.0) { globalEvoPos = 1.0; scanDirection = -1; } else if (globalEvoPos <= 0.0) { globalEvoPos = 0.0; scanDirection = 1; } }
            else { globalEvoPos -= evoStep; if (globalEvoPos < 0.0) globalEvoPos += 1.0; }
        }
        
        // Calculate the raw target position adding up manual control, scanning, and LFOs
        float rawEvoPos = (float)(position + globalEvoPos + evoLFO * evoLFODepth * 0.5f);

        // Wrap cleanly between 0.0 and 1.0 so it loops instead of hitting a wall
        rawEvoPos = std::fmod(rawEvoPos, 1.0f);
        if (rawEvoPos < 0.0f) rawEvoPos += 1.0f;

        float framePosNorm = rawEvoPos;


        double pitchMult = std::pow(2.0, (double)(pitchLFO * pitchLFODepth) / 12.0);

        float mix = 0.0f;
        for (auto& v : voices) mix += synthesiseVoice(v, framePosNorm, posLFO * posLFODepth, pitchMult);

        mix = mix * 0.3f;
        mix = mix / (1.0f + std::abs(mix));

        // FIXED FILTER: Using class member variables, no more static bugs!
        float filtered = b0 * mix + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = mix;
        y2 = y1; y1 = filtered;
        mix = filtered;

        float gain = gainSmooth.getNextValue();
        outL[s] = mix * gain;
        outR[s] = mix * gain;
    }
}

void TranswaveAudioProcessor::parameterChanged(const juce::String&, float) {}

void TranswaveAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TranswaveAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* TranswaveAudioProcessor::createEditor() { return new TranswaveAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TranswaveAudioProcessor(); }