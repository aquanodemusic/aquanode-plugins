#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "numGrains", "Number of Grains", 1, 32, 8));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainSize", "Grain Size",
        juce::NormalisableRange<float>(0.001f, 10.0f, 0.001f, 0.4f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainPosition", "Grain Position", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "spray", "Position Spray",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.4f), 0.05f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "windowSize", "Window Size", 0.01f, 1.0f, 1.0f));

    // Live-input time window (FX mode): how many seconds of recent audio
    // the circular buffer holds for grains to be drawn from.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "timeWindow", "Time Window",
        juce::NormalisableRange<float>(0.1f, 60.0f, 0.001f, 0.3f), 5.0f));

    // Grain ADSR (affects individual grains)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainAttack", "Grain Attack",
        juce::NormalisableRange<float>(0.001f, 0.5f, 0.001f, 0.3f), 0.01f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainDecay", "Grain Decay",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f, 0.3f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainSustain", "Grain Sustain", 0.0f, 1.0f, 0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "grainRelease", "Grain Release",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f, 0.3f), 0.3f));

    // Note ADSR (affects entire note press / mouse drag)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteAttack", "Note Attack",
        juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f, 0.3f), 0.01f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteDecay", "Note Decay",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteSustain", "Note Sustain", 0.0f, 1.0f, 0.7f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "noteRelease", "Note Release",
        juce::NormalisableRange<float>(0.001f, 3.0f, 0.001f, 0.3f), 0.3f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "amplitudeMod", "Amplitude Modulation", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "amDispersion", "AM Dispersion", 0.0f, 1.0f, 0.5f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pitchDispersion", "Pitch Dispersion", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pitch", "Pitch", -24.0f, 24.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "stereoSpread", "Stereo Spread", 0.0f, 1.0f, 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "volume", "Volume",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 2.0f), 0.7f));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "reversedGrains", "Reversed Grains", 0, MAX_GRAINS_PER_NOTE, 0));

    return layout;
}

//==============================================================================
GranulateAudioProcessor::GranulateAudioProcessor()
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
    parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    rng.seed(std::random_device{}());
}

GranulateAudioProcessor::~GranulateAudioProcessor() {}

//==============================================================================
const juce::String GranulateAudioProcessor::getName() const { return JucePlugin_Name; }

bool GranulateAudioProcessor::acceptsMidi() const { return true; }

bool GranulateAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool GranulateAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double GranulateAudioProcessor::getTailLengthSeconds() const
{
    // Return the maximum possible note-release time so the DAW keeps the
    // plugin alive long enough for release tails to complete.
    auto* releaseParam = parameters.getRawParameterValue("noteRelease");
    return releaseParam ? (double)releaseParam->load() : 3.0;
}
int    GranulateAudioProcessor::getNumPrograms() { return 1; }
int    GranulateAudioProcessor::getCurrentProgram() { return 0; }
void   GranulateAudioProcessor::setCurrentProgram(int) {}
const juce::String GranulateAudioProcessor::getProgramName(int) { return {}; }
void   GranulateAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void GranulateAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    // Reset all MIDI note voices
    for (auto& nv : noteVoices)
    {
        nv.isActive = false;
        nv.isReleased = false;
        nv.midiNote = -1;
        nv.noteEnvelopeTimer = 0.0f;
        nv.grainTimer = 0.0f;
        nv.ageCounter = 0;
        for (auto& g : nv.grains) g.isActive = false;
    }
    voiceAgeCounter = 0;

    // Reset mouse voice
    for (auto& g : mouseGrains) g.isActive = false;
    mouseGrainTimer = 0.0f;
    mouseNoteEnvelopeTimer.store(0.0f);
    mouseNoteReleased.store(false);
    mousePressed.store(false);

    // 15 ms position smoothing — snappy but click-free
    smoothedMousePosition.setSmoothingTime(0.015f, (float)sampleRate);

    // ---- Live circular buffer (FX mode) ------------------------------------
    // Size for the maximum possible time window (60s) plus a little headroom,
    // so changing the Time Window knob never requires a reallocation.
    circCapacitySamples = (int)std::ceil(61.0 * sampleRate);
    circularBuffer.setSize(2, circCapacitySamples, false, true, true);
    circularBuffer.clear();
    circWritePos = 0;

    for (auto& g : liveGrains) g.isActive = false;
    liveGrainTimer = 0.0f;
}

void GranulateAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool GranulateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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
float GranulateAudioProcessor::getGrainEnvelope(float phase, const GrainVoice& grain)
{
    const float attack = grain.grainAttack;
    const float decay = grain.grainDecay;
    const float sustain = grain.grainSustain;
    const float sustainTime = 0.5f;

    const float totalTime = attack + decay + sustainTime + grain.grainRelease;
    const float attackEnd = attack / totalTime;
    const float decayEnd = (attack + decay) / totalTime;
    const float sustainEnd = (attack + decay + sustainTime) / totalTime;

    if (phase < attackEnd)
        return phase / attackEnd;
    else if (phase < decayEnd)
    {
        const float t = (phase - attackEnd) / (decayEnd - attackEnd);
        return 1.0f - (1.0f - sustain) * t;
    }
    else if (phase < sustainEnd)
        return sustain;
    else
    {
        const float t = (phase - sustainEnd) / (1.0f - sustainEnd);
        return sustain * (1.0f - t);
    }
}

float GranulateAudioProcessor::getNoteEnvelope(float phase, bool released)
{
    const float attack = parameters.getRawParameterValue("noteAttack")->load();
    const float decay = parameters.getRawParameterValue("noteDecay")->load();
    const float sustain = parameters.getRawParameterValue("noteSustain")->load();

    if (released)
    {
        // phase runs 0→1 over the release duration, passed in from the caller
        return sustain * (1.0f - phase);
    }

    // Attack → Decay → Sustain (phase runs over attack+decay time)
    const float totalTime = attack + decay;
    if (totalTime < 0.001f) return sustain;

    const float attackEnd = attack / totalTime;
    if (phase < attackEnd)
        return phase / attackEnd;
    else if (phase < 1.0f)
    {
        const float t = (phase - attackEnd) / (1.0f - attackEnd);
        return 1.0f - (1.0f - sustain) * t;
    }
    return sustain;
}

//==============================================================================
void GranulateAudioProcessor::processMidiMessages(juce::MidiBuffer& midiMessages)
{
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();

            // Silence mouse mode when MIDI arrives
            mousePressed.store(false);
            mouseNoteReleased.store(false);
            mouseNoteEnvelopeTimer.store(0.0f);
            for (auto& g : mouseGrains) g.isActive = false;
            mouseGrainTimer = 0.0f;

            // Check for re-trigger: if this note is already playing, restart it
            NoteVoice* target = nullptr;
            for (auto& nv : noteVoices)
            {
                if ((nv.isActive || nv.isReleased) && nv.midiNote == note)
                {
                    target = &nv;
                    break;
                }
            }

            // Otherwise find a free slot
            if (target == nullptr)
            {
                for (auto& nv : noteVoices)
                {
                    if (!nv.isActive && !nv.isReleased)
                    {
                        target = &nv;
                        break;
                    }
                }
            }

            // Voice steal: prefer oldest released, then oldest active
            if (target == nullptr)
            {
                int oldestAge = INT_MAX;
                for (auto& nv : noteVoices)
                {
                    if (nv.isReleased && nv.ageCounter < oldestAge)
                    {
                        oldestAge = nv.ageCounter;
                        target = &nv;
                    }
                }
            }
            if (target == nullptr)
            {
                int oldestAge = INT_MAX;
                for (auto& nv : noteVoices)
                {
                    if (nv.isActive && nv.ageCounter < oldestAge)
                    {
                        oldestAge = nv.ageCounter;
                        target = &nv;
                    }
                }
            }

            if (target != nullptr)
            {
                // Silence any grains that were playing in this slot
                for (auto& g : target->grains) g.isActive = false;

                target->isActive = true;
                target->isReleased = false;
                target->midiNote = note;
                target->noteEnvelopeTimer = 0.0f;
                target->grainTimer = 0.0f;
                target->ageCounter = ++voiceAgeCounter;
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            for (auto& nv : noteVoices)
            {
                if (nv.isActive && !nv.isReleased && nv.midiNote == note)
                {
                    nv.isReleased = true;
                    nv.noteEnvelopeTimer = 0.0f;  // start release timer
                    break;
                }
            }
        }
    }
}

//==============================================================================
void GranulateAudioProcessor::triggerGrain(GrainVoice& grain,
    int         midiNote,
    float       basePosition,
    bool        reverse)
{
    const float spray = parameters.getRawParameterValue("spray")->load();
    const float windowSize = parameters.getRawParameterValue("windowSize")->load();
    const float pitchDisp = parameters.getRawParameterValue("pitchDispersion")->load();
    const float ampMod = parameters.getRawParameterValue("amplitudeMod")->load();
    const float amDisp = parameters.getRawParameterValue("amDispersion")->load();
    const float stereoSpread = parameters.getRawParameterValue("stereoSpread")->load();
    const float pitchKnob = parameters.getRawParameterValue("pitch")->load();
    const float grainSize = std::max(0.001f, parameters.getRawParameterValue("grainSize")->load());

    const float grainAttackBase = parameters.getRawParameterValue("grainAttack")->load();
    const float grainDecayBase = parameters.getRawParameterValue("grainDecay")->load();
    const float grainSustainBase = parameters.getRawParameterValue("grainSustain")->load();
    const float grainReleaseBase = parameters.getRawParameterValue("grainRelease")->load();

    // Randomise grain ADSR ±15%
    constexpr float R = 0.3f;
    grain.grainAttack = grainAttackBase * (1.0f + (dist01(rng) - 0.5f) * R);
    grain.grainDecay = grainDecayBase * (1.0f + (dist01(rng) - 0.5f) * R);
    grain.grainSustain = juce::jlimit(0.0f, 1.0f, grainSustainBase + (dist01(rng) - 0.5f) * R);
    grain.grainRelease = grainReleaseBase * (1.0f + (dist01(rng) - 0.5f) * R);

    // Position with spray
    const float randomOffset = (dist01(rng) - 0.5f) * 2.0f * spray * windowSize;
    const float targetPos = juce::jlimit(0.0f, 1.0f, basePosition + randomOffset);
    const int   numSamples = sampleBuffer.getNumSamples();

    // Pitch: MIDI note relative to C4 (60), or pitch knob for mouse mode
    float totalPitch = (midiNote >= 0) ? (float)(midiNote - 60) : pitchKnob;
    totalPitch += (dist01(rng) - 0.5f) * 2.0f * pitchDisp * 12.0f;
    const float pitchRatio = std::pow(2.0f, totalPitch / 12.0f);

    grain.isReverse = reverse;
    grain.grainEnvelopePhase = 0.0f;

    if (reverse)
    {
        // Start at end of grain window and move backwards
        const double grainLenSamples = (double)(grainSize * (float)getSampleRate() * pitchRatio);
        grain.samplePosition = juce::jlimit(0.0, (double)(numSamples - 1),
            (double)targetPos * (double)numSamples + grainLenSamples);
        grain.pitch = -pitchRatio;
    }
    else
    {
        grain.samplePosition = (double)targetPos * (double)numSamples;
        grain.pitch = pitchRatio;
    }

    // Amplitude
    const float baseAmp = 1.0f - ampMod * 0.5f;
    const float randomAmp = (dist01(rng) - 0.5f) * 2.0f * amDisp;
    grain.amplitude = juce::jlimit(0.0f, 1.0f, baseAmp + randomAmp);

    // Stereo panning
    const float pan = (dist01(rng) - 0.5f) * 2.0f * stereoSpread;
    const float panAngle = (pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    grain.panLeft = std::cos(panAngle);
    grain.panRight = std::sin(panAngle);

    grain.isActive = true;
}

//==============================================================================
void GranulateAudioProcessor::stopAllGrains()
{
    for (auto& nv : noteVoices)
    {
        nv.isActive = false;
        nv.isReleased = false;
        for (auto& g : nv.grains) g.isActive = false;
    }
    for (auto& g : mouseGrains) g.isActive = false;
}

//==============================================================================
// Mouse-mode control (called from the GUI thread)
//==============================================================================
void GranulateAudioProcessor::setMousePosition(float position)
{
    mousePosition.store(position);

    // Only reset the note envelope on first press, not on every drag event.
    // Resetting every drag restarted the attack phase dozens of times per
    // second, which caused audible amplitude stuttering / crackling.
    if (!mousePressed.load())
    {
        mouseNoteReleased.store(false);
        mouseNoteEnvelopeTimer.store(0.0f);
    }

    mousePressed.store(true);
}

void GranulateAudioProcessor::releaseMousePosition()
{
    mousePressed.store(false);
    mouseNoteReleased.store(true);
    mouseNoteEnvelopeTimer.store(0.0f);
}

bool GranulateAudioProcessor::isInMouseMode() const
{
    return mousePressed.load();
}

//==============================================================================
// getActiveGrains — UI thread snapshot for the waveform display
//==============================================================================
std::vector<GrainVoice> GranulateAudioProcessor::getActiveGrains() const
{
    // Block briefly until the audio thread has finished its current snapshot
    // update, then return a copy. The audio thread uses ScopedTryLock so it
    // never blocks on the audio callback — it simply skips one snapshot update
    // if the GUI happens to be reading at that exact moment.
    const juce::ScopedLock dl(displayLock);
    return displaySnapshot;
}

//==============================================================================
// pushLiveAudio — records the incoming (dry) audio into the rolling circular
// buffer. Called once per block, before the output buffer is overwritten with
// granulated sound. Not sample-accurate under concurrent GUI reads, but that's
// fine here — a torn read just costs a slightly stale waveform pixel.
//==============================================================================
void GranulateAudioProcessor::pushLiveAudio(const juce::AudioBuffer<float>& input, int numSamples)
{
    if (circCapacitySamples <= 0) return;

    const int numCh = input.getNumChannels();
    auto* circL = circularBuffer.getWritePointer(0);
    auto* circR = circularBuffer.getWritePointer(1);
    const float* inL = numCh > 0 ? input.getReadPointer(0) : nullptr;
    const float* inR = numCh > 1 ? input.getReadPointer(1) : inL;

    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = (int)(circWritePos % (juce::int64)circCapacitySamples);
        circL[idx] = inL ? inL[i] : 0.0f;
        circR[idx] = inR ? inR[i] : 0.0f;
        ++circWritePos;
    }
}

float GranulateAudioProcessor::getCircSample(int channel, juce::int64 absIndex) const
{
    if (circCapacitySamples <= 0) return 0.0f;
    juce::int64 m = absIndex % (juce::int64)circCapacitySamples;
    if (m < 0) m += circCapacitySamples;
    return circularBuffer.getSample(channel, (int)m);
}

//==============================================================================
// triggerLiveGrain — same shaping logic as the original triggerGrain(), but
// positions are expressed as absolute sample indices into the live circular
// buffer's current time window, rather than 0..1 over a static loaded sample.
//==============================================================================
void GranulateAudioProcessor::triggerLiveGrain(GrainVoice& grain,
    float       basePosition01,
    juce::int64 windowStartAbs,
    juce::int64 availableLen,
    bool        reverse)
{
    const float spray = parameters.getRawParameterValue("spray")->load();
    const float windowSize = parameters.getRawParameterValue("windowSize")->load();
    const float pitchDisp = parameters.getRawParameterValue("pitchDispersion")->load();
    const float ampMod = parameters.getRawParameterValue("amplitudeMod")->load();
    const float amDisp = parameters.getRawParameterValue("amDispersion")->load();
    const float stereoSpread = parameters.getRawParameterValue("stereoSpread")->load();
    const float pitchKnob = parameters.getRawParameterValue("pitch")->load();
    const float grainSize = std::max(0.001f, parameters.getRawParameterValue("grainSize")->load());

    const float grainAttackBase = parameters.getRawParameterValue("grainAttack")->load();
    const float grainDecayBase = parameters.getRawParameterValue("grainDecay")->load();
    const float grainSustainBase = parameters.getRawParameterValue("grainSustain")->load();
    const float grainReleaseBase = parameters.getRawParameterValue("grainRelease")->load();

    // Randomise grain ADSR ±15%
    constexpr float R = 0.3f;
    grain.grainAttack = grainAttackBase * (1.0f + (dist01(rng) - 0.5f) * R);
    grain.grainDecay = grainDecayBase * (1.0f + (dist01(rng) - 0.5f) * R);
    grain.grainSustain = juce::jlimit(0.0f, 1.0f, grainSustainBase + (dist01(rng) - 0.5f) * R);
    grain.grainRelease = grainReleaseBase * (1.0f + (dist01(rng) - 0.5f) * R);

    // Position with spray — relative to the live time window, same maths as
    // the original sample-based version.
    const float randomOffset = (dist01(rng) - 0.5f) * 2.0f * spray * windowSize;
    const float targetNorm = juce::jlimit(0.0f, 1.0f, basePosition01 + randomOffset);
    const juce::int64 targetAbs = windowStartAbs + (juce::int64)((double)targetNorm * (double)(availableLen - 1));

    // Pitch: no MIDI note in FX mode — always driven by the pitch knob.
    float totalPitch = pitchKnob;
    totalPitch += (dist01(rng) - 0.5f) * 2.0f * pitchDisp * 12.0f;
    const float pitchRatio = std::pow(2.0f, totalPitch / 12.0f);

    grain.isReverse = reverse;
    grain.grainEnvelopePhase = 0.0f;

    const juce::int64 lo = windowStartAbs;
    const juce::int64 hi = windowStartAbs + availableLen - 1;

    if (reverse)
    {
        const double grainLenSamples = (double)(grainSize * (float)getSampleRate() * pitchRatio);
        grain.samplePosition = juce::jlimit((double)lo, (double)hi,
            (double)targetAbs + grainLenSamples);
        grain.pitch = -pitchRatio;
    }
    else
    {
        grain.samplePosition = juce::jlimit((double)lo, (double)hi, (double)targetAbs);
        grain.pitch = pitchRatio;
    }

    // Amplitude
    const float baseAmp = 1.0f - ampMod * 0.5f;
    const float randomAmp = (dist01(rng) - 0.5f) * 2.0f * amDisp;
    grain.amplitude = juce::jlimit(0.0f, 1.0f, baseAmp + randomAmp);

    // Stereo panning
    const float pan = (dist01(rng) - 0.5f) * 2.0f * stereoSpread;
    const float panAngle = (pan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    grain.panLeft = std::cos(panAngle);
    grain.panRight = std::sin(panAngle);

    grain.isActive = true;
}

//==============================================================================
// processGrains — called once per processBlock on the audio thread. Grains
// are continuously generated from the live circular buffer (there is no
// MIDI-note or mouse-click gating in FX mode: the granulator is always "on",
// reading from whatever audio has recently passed through the plugin).
//==============================================================================
void GranulateAudioProcessor::processGrains(juce::AudioBuffer<float>& buffer,
    int startSample,
    int numSamples)
{
    // --- Load shared parameters once per block ---
    const int   maxGrains = (int)parameters.getRawParameterValue("numGrains")->load();
    const float grainSize = std::max(0.001f,
        parameters.getRawParameterValue("grainSize")->load());
    const float basePosKnob = parameters.getRawParameterValue("grainPosition")->load();
    const float volume = parameters.getRawParameterValue("volume")->load();
    const int reversedGrains = juce::jlimit(0, maxGrains,
        (int)parameters.getRawParameterValue("reversedGrains")->load());
    const float timeWindow = juce::jlimit(0.1f, 60.0f,
        parameters.getRawParameterValue("timeWindow")->load());

    const float sampleRate = (float)getSampleRate();
    const float grainInterval = grainSize / (float)std::max(1, maxGrains);
    const juce::int64 windowSamples = juce::jmax((juce::int64)1,
        (juce::int64)((double)timeWindow * (double)sampleRate));

    // =========================================================================
    // Per-sample loop
    // =========================================================================
    for (int i = 0; i < numSamples; ++i)
    {
        const int bufferIndex = startSample + i;
        float totalLeft = 0.0f;
        float totalRight = 0.0f;

        const juce::int64 writePosNow = circWritePos;
        const juce::int64 availableLen = juce::jmin(windowSamples, writePosNow);

        if (availableLen >= 8)
        {
            const juce::int64 windowStartAbs = writePosNow - availableLen;

            // --- Grain scheduler (always running) ---
            liveGrainTimer += 1.0f / sampleRate;
            if (liveGrainTimer >= grainInterval)
            {
                liveGrainTimer -= grainInterval;
                for (int v = 0; v < maxGrains; ++v)
                {
                    if (!liveGrains[v].isActive)
                    {
                        triggerLiveGrain(liveGrains[v], basePosKnob, windowStartAbs, availableLen,
                            v < reversedGrains);
                        break;
                    }
                }
            }

            // --- Accumulate grains ---
            for (int v = 0; v < maxGrains; ++v)
            {
                auto& grain = liveGrains[v];
                if (!grain.isActive) continue;

                const juce::int64 idx0 = (juce::int64)std::floor(grain.samplePosition);
                const double frac = grain.samplePosition - (double)idx0;

                float sampleL, sampleR;
                if (grain.isReverse)
                {
                    const float l0 = getCircSample(0, idx0), l1 = getCircSample(0, idx0 - 1);
                    const float r0 = getCircSample(1, idx0), r1 = getCircSample(1, idx0 - 1);
                    sampleL = l0 + (float)(1.0 - frac) * (l1 - l0);
                    sampleR = r0 + (float)(1.0 - frac) * (r1 - r0);
                }
                else
                {
                    const float l0 = getCircSample(0, idx0), l1 = getCircSample(0, idx0 + 1);
                    const float r0 = getCircSample(1, idx0), r1 = getCircSample(1, idx0 + 1);
                    sampleL = l0 + (float)frac * (l1 - l0);
                    sampleR = r0 + (float)frac * (r1 - r0);
                }

                const float env = getGrainEnvelope(grain.grainEnvelopePhase, grain)
                    * grain.amplitude * volume;
                totalLeft += sampleL * env * grain.panLeft;
                totalRight += sampleR * env * grain.panRight;

                grain.samplePosition += grain.pitch;
                grain.grainEnvelopePhase += 1.0f / (grainSize * sampleRate);

                // Deactivate: envelope finished, or position ran outside the
                // (slowly sliding) live time window.
                if (grain.grainEnvelopePhase >= 1.0f
                    || grain.samplePosition <= (double)windowStartAbs
                    || grain.samplePosition >= (double)writePosNow)
                    grain.isActive = false;
            }
        }

        // =====================================================================
        // Write to output buffer
        // =====================================================================
        totalLeft = juce::jlimit(-0.95f, 0.95f, totalLeft);
        totalRight = juce::jlimit(-0.95f, 0.95f, totalRight);

        if (buffer.getNumChannels() >= 2)
        {
            buffer.getWritePointer(0)[bufferIndex] += totalLeft;
            buffer.getWritePointer(1)[bufferIndex] += totalRight;
        }
        else
        {
            buffer.getWritePointer(0)[bufferIndex] += 0.5f * (totalLeft + totalRight);
        }
    }
    // =========================================================================
    // Update display snapshot for the GUI thread (waveform playheads).
    // =========================================================================
    {
        const juce::ScopedTryLock dl(displayLock);
        if (dl.isLocked())
        {
            displaySnapshot.clear();
            for (const auto& g : liveGrains)
                if (g.isActive) displaySnapshot.push_back(g);
        }
    }
}

//==============================================================================
void GranulateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Capture the live (dry) input into the rolling circular buffer before
    // the buffer is overwritten with granulated output.
    pushLiveAudio(buffer, buffer.getNumSamples());

    buffer.clear();

    // MIDI/mouse-triggered polyphonic engine is retained but dormant — it
    // still updates its internal state harmlessly, in case the sample-based
    // controls are ever re-shown in the GUI.
    processMidiMessages(midiMessages);

    processGrains(buffer, 0, buffer.getNumSamples());
}

//==============================================================================
bool GranulateAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* GranulateAudioProcessor::createEditor()
{
    return new GranulateAudioProcessorEditor(*this);
}

//==============================================================================
void GranulateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    // Always save the path so the normal (audio-format) load path still works.
    if (lastLoadedSamplePath.isNotEmpty())
        state.setProperty("samplePath", lastLoadedSamplePath, nullptr);

    // If the last load was a raw import, also persist the three format
    // parameters so the exact loadSampleRaw() call can be reproduced on
    // DAW session restore.
    if (lastLoadedIsRaw)
    {
        state.setProperty("rawBitDepth", lastRawBitDepth, nullptr);
        state.setProperty("rawNumChannels", lastRawNumChannels, nullptr);
        state.setProperty("rawSampleRate", lastRawSampleRate, nullptr);
    }

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GranulateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(parameters.state.getType()))
    {
        auto newState = juce::ValueTree::fromXml(*xmlState);
        parameters.replaceState(newState);

        // Restore the last loaded sample.  If the file no longer exists
        // (moved, deleted, on a different machine) we silently skip it.
        const juce::String path = newState.getProperty("samplePath", "").toString();
        if (path.isNotEmpty())
        {
            const juce::File f(path);
            if (f.existsAsFile())
            {
                // If all three raw-format properties are present the last load
                // was a raw import; reproduce those exact settings.  Otherwise
                // fall back to the normal AudioFormatManager path.
                const bool hasRaw = newState.hasProperty("rawBitDepth")
                    && newState.hasProperty("rawNumChannels")
                    && newState.hasProperty("rawSampleRate");
                if (hasRaw)
                {
                    const int    bd = (int)newState.getProperty("rawBitDepth", 16);
                    const int    ch = (int)newState.getProperty("rawNumChannels", 1);
                    const double sr = (double)newState.getProperty("rawSampleRate", 44100.0);
                    loadSampleRaw(f, bd, ch, sr);
                }
                else
                {
                    loadSample(f);
                }
            }
            // else: file not found — discard silently, no alert
        }
    }
}

//==============================================================================
void GranulateAudioProcessor::loadSample(const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return;

    const double fileDurationSeconds = reader->lengthInSamples / reader->sampleRate;
    if (fileDurationSeconds > 60 * 60)
    {
        const int minutes = (int)(fileDurationSeconds / 60);
        const int seconds = (int)fileDurationSeconds % 60;
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Long File Warning",
            "This file is " + juce::String(minutes) + " minutes and " +
            juce::String(seconds) + " seconds long.\n\n"
            "Loading files longer than 1 hour require significant memory. Up to 3 hours are supported.",
            "OK");
    }

    const juce::int64 maxSamples = (juce::int64)3 * 60 * 60 * 48000;
    const juce::int64 samplesToRead = juce::jmin(reader->lengthInSamples, maxSamples);

    if (reader->lengthInSamples > maxSamples)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "File Truncated",
            "This file exceeds the 3-hour limit. Only the first 3 hours will be loaded.", "OK");

    try
    {
        juce::AudioBuffer<float> tempBuffer((int)reader->numChannels, (int)samplesToRead);
        const int chunkSize = 65536;
        juce::int64 samplesRead = 0;
        while (samplesRead < samplesToRead)
        {
            juce::int64 chunk = juce::jmin((juce::int64)chunkSize, samplesToRead - samplesRead);
            reader->read(&tempBuffer, (int)samplesRead, (int)chunk, samplesRead, true, true);
            samplesRead += chunk;
        }
        { const juce::ScopedLock sl(bufferLock); sampleBuffer = std::move(tempBuffer); }
        lastLoadedSamplePath = file.getFullPathName();
        lastLoadedIsRaw = false;   // this was a normal audio-format load
        stopAllGrains();
        mousePressed.store(false);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error Loading File", "Failed to load audio file:\n" + juce::String(e.what()), "OK");
    }
}

void GranulateAudioProcessor::loadSample(const void* data, size_t dataSize)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    auto inputStream = std::make_unique<juce::MemoryInputStream>(data, dataSize, false);
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(std::move(inputStream)));
    if (reader == nullptr) return;

    const double fileDurationSeconds = reader->lengthInSamples / reader->sampleRate;
    if (fileDurationSeconds > 60 * 60)
    {
        const int minutes = (int)(fileDurationSeconds / 60);
        const int seconds = (int)fileDurationSeconds % 60;
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Long File Warning",
            "This file is " + juce::String(minutes) + " minutes and " +
            juce::String(seconds) + " seconds long.\n\n"
            "Loading files longer than 1 hour require significant memory. Up to 3 hours are supported.",
            "OK");
    }

    const juce::int64 maxSamples = (juce::int64)3 * 60 * 60 * 48000;
    const juce::int64 samplesToRead = juce::jmin(reader->lengthInSamples, maxSamples);

    if (reader->lengthInSamples > maxSamples)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "File Truncated",
            "This file exceeds the 3-hour limit. Only the first 3 hours will be loaded.", "OK");

    try
    {
        juce::AudioBuffer<float> tempBuffer((int)reader->numChannels, (int)samplesToRead);
        const int chunkSize = 65536;
        juce::int64 samplesRead = 0;
        while (samplesRead < samplesToRead)
        {
            juce::int64 chunk = juce::jmin((juce::int64)chunkSize, samplesToRead - samplesRead);
            reader->read(&tempBuffer, (int)samplesRead, (int)chunk, samplesRead, true, true);
            samplesRead += chunk;
        }
        { const juce::ScopedLock sl(bufferLock); sampleBuffer = std::move(tempBuffer); }
        // Note: no lastLoadedSamplePath update here — this overload is used for
        // in-memory data (e.g. drag-and-drop binary blobs) where there is no path.
        stopAllGrains();
        mousePressed.store(false);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error Loading File", "Failed to load audio file:\n" + juce::String(e.what()), "OK");
    }
}

//==============================================================================
// Raw PCM import — interprets any file as headerless little-endian signed PCM
//==============================================================================
void GranulateAudioProcessor::loadSampleRaw(const juce::File& file,
    int bitDepth,
    int numChannels,
    double sampleRate)
{
    jassert(bitDepth == 16 || bitDepth == 24);
    jassert(numChannels >= 1 && numChannels <= 2);

    juce::MemoryBlock fileData;
    if (!file.loadFileAsData(fileData))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error Reading File",
            "Could not read: " + file.getFullPathName(), "OK");
        return;
    }

    const auto* bytes = static_cast<const uint8_t*>(fileData.getData());
    const size_t   totalBytes = fileData.getSize();
    const int      bps = bitDepth / 8;          // bytes per sample
    const size_t   frameBytes = (size_t)bps * (size_t)numChannels;

    if (totalBytes < frameBytes)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "File Too Small",
            "The file does not contain enough data to form a single audio frame.", "OK");
        return;
    }

    const juce::int64 totalFrames = (juce::int64)(totalBytes / frameBytes);

    // ---- Duration warnings (consistent with the standard load path) -------
    const double durationSeconds = (double)totalFrames / sampleRate;

    if (durationSeconds > 60.0 * 60.0)
    {
        const int mins = (int)(durationSeconds / 60.0);
        const int secs = (int)durationSeconds % 60;
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Long File Warning",
            "Interpreting this data as raw audio yields a duration of " +
            juce::String(mins) + " min " + juce::String(secs) + " sec.\n\n"
            "Loading files longer than 1 hour requires significant memory. "
            "Up to 3 hours are supported.", "OK");
    }

    const juce::int64 maxFrames = (juce::int64)(3.0 * 60.0 * 60.0 * sampleRate);
    const juce::int64 framesToRead = juce::jmin(totalFrames, maxFrames);

    if (totalFrames > maxFrames)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "File Truncated",
            "This file exceeds the 3-hour limit. Only the first 3 hours will be loaded.", "OK");

    // ---- Convert raw bytes → normalised float samples ---------------------
    try
    {
        juce::AudioBuffer<float> tempBuffer(numChannels, (int)framesToRead);

        if (bitDepth == 16)
        {
            for (juce::int64 frame = 0; frame < framesToRead; ++frame)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const size_t  off = (size_t)(frame * numChannels + ch) * 2u;
                    const int16_t raw = (int16_t)((uint16_t)bytes[off] |
                        ((uint16_t)bytes[off + 1] << 8));
                    tempBuffer.getWritePointer(ch)[(int)frame] =
                        (float)raw / 32768.0f;
                }
            }
        }
        else  // 24-bit
        {
            for (juce::int64 frame = 0; frame < framesToRead; ++frame)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const size_t   off = (size_t)(frame * numChannels + ch) * 3u;
                    const uint32_t raw24 = (uint32_t)bytes[off]
                        | ((uint32_t)bytes[off + 1] << 8)
                        | ((uint32_t)bytes[off + 2] << 16);
                    // Sign-extend 24 → 32 bits
                    const int32_t  s24 = (raw24 & 0x800000u)
                        ? (int32_t)(raw24 | 0xFF000000u)
                        : (int32_t)raw24;
                    tempBuffer.getWritePointer(ch)[(int)frame] =
                        (float)s24 / 8388608.0f;
                }
            }
        }

        { const juce::ScopedLock sl(bufferLock); sampleBuffer = std::move(tempBuffer); }

        // Store path so the DAW session can try to reload on next open.
        lastLoadedSamplePath = file.getFullPathName();
        lastLoadedIsRaw = true;
        lastRawBitDepth = bitDepth;
        lastRawNumChannels = numChannels;
        lastRawSampleRate = sampleRate;

        stopAllGrains();
        mousePressed.store(false);
    }
    catch (const std::exception& e)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Error Loading Raw Data", juce::String(e.what()), "OK");
    }
}

//==============================================================================

//==============================================================================
// Colour theming helpers — values stored as ValueTree properties so they are
// automatically persisted in getStateInformation / setStateInformation along
// with all audio parameters.
//==============================================================================
juce::Colour GranulateAudioProcessor::getColourProperty(
    const juce::String& name, juce::Colour defaultColour) const
{
    const juce::String str = parameters.state.getProperty(name, juce::String()).toString();
    if (str.isNotEmpty())
        return juce::Colour::fromString(str);
    return defaultColour;
}

void GranulateAudioProcessor::setColourProperty(const juce::String& name,
    juce::Colour colour)
{
    parameters.state.setProperty(name, colour.toString(), nullptr);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GranulateAudioProcessor();
}