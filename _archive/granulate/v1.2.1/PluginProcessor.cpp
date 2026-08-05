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
    float       basePosition)
{
    const float spray = parameters.getRawParameterValue("spray")->load();
    const float windowSize = parameters.getRawParameterValue("windowSize")->load();
    const float pitchDisp = parameters.getRawParameterValue("pitchDispersion")->load();
    const float ampMod = parameters.getRawParameterValue("amplitudeMod")->load();
    const float amDisp = parameters.getRawParameterValue("amDispersion")->load();
    const float stereoSpread = parameters.getRawParameterValue("stereoSpread")->load();
    const float pitchKnob = parameters.getRawParameterValue("pitch")->load();

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
    grain.samplePosition = (double)targetPos * (double)numSamples;
    grain.grainEnvelopePhase = 0.0f;

    // Pitch: MIDI note relative to C4 (60), or pitch knob for mouse mode
    float totalPitch = (midiNote >= 0) ? (float)(midiNote - 60) : pitchKnob;

    // Add pitch dispersion
    totalPitch += (dist01(rng) - 0.5f) * 2.0f * pitchDisp * 12.0f;
    grain.pitch = std::pow(2.0f, totalPitch / 12.0f);

    // Amplitude: ampMod sets the base reduction (0 = full, 1 = up to silent),
    // amDisp adds per-grain random variation on top of that.
    const float baseAmp = 1.0f - ampMod * 0.5f;   // ranges 1.0 → 0.5
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
// processGrains — called once per processBlock on the audio thread
//==============================================================================
void GranulateAudioProcessor::processGrains(juce::AudioBuffer<float>& buffer,
    int startSample,
    int numSamples)
{
    if (!hasSample()) return;

    const juce::ScopedTryLock sl(bufferLock);
    if (!sl.isLocked()) return;

    // --- Load shared parameters once per block ---
    const int   maxGrains = (int)parameters.getRawParameterValue("numGrains")->load();
    const float grainSize = std::max(0.001f,
        parameters.getRawParameterValue("grainSize")->load());
    const float basePosKnob = parameters.getRawParameterValue("grainPosition")->load();
    const float volume = parameters.getRawParameterValue("volume")->load();
    const float noteAttack = parameters.getRawParameterValue("noteAttack")->load();
    const float noteDecay = parameters.getRawParameterValue("noteDecay")->load();
    const float noteRelease = parameters.getRawParameterValue("noteRelease")->load();

    const float sampleRate = (float)getSampleRate();
    const float grainInterval = grainSize / (float)std::max(1, maxGrains);
    const int   numChannels = sampleBuffer.getNumChannels();
    const int   maxSampleIndex = sampleBuffer.getNumSamples() - 1;

    // Snapshot mouse-mode atomic state once per block
    const bool  mousePressedNow = mousePressed.load();
    const bool  mouseReleasedNow = mouseNoteReleased.load();
    const bool  processMouseVoice = mousePressedNow || mouseReleasedNow;
    const float mouseTargetPos = mousePosition.load();

    // =========================================================================
    // Per-sample loop
    // =========================================================================
    for (int i = 0; i < numSamples; ++i)
    {
        const int bufferIndex = startSample + i;
        float totalLeft = 0.0f;
        float totalRight = 0.0f;

        // =====================================================================
        // MOUSE VOICE
        // =====================================================================
        if (processMouseVoice)
        {
            // Advance position smoother one sample at a time.
            // Must be inside the loop — the smoothing factor is computed
            // at sample-rate cadence.
            const float basePos = smoothedMousePosition.process(mouseTargetPos);

            // --- Note envelope ---
            float mouseNoteEnv = 1.0f;
            if (mouseReleasedNow)
            {
                float timer = mouseNoteEnvelopeTimer.load() + 1.0f / sampleRate;
                mouseNoteEnvelopeTimer.store(timer);
                const float phase = timer / noteRelease;
                mouseNoteEnv = getNoteEnvelope(phase, true);

                if (timer >= noteRelease)
                {
                    for (auto& g : mouseGrains) g.isActive = false;
                    mouseNoteReleased.store(false);
                    mouseNoteEnvelopeTimer.store(0.0f);
                    mouseGrainTimer = 0.0f;
                    // Don't break the outer loop — MIDI voices may still run
                }
            }
            else
            {
                float timer = mouseNoteEnvelopeTimer.load() + 1.0f / sampleRate;
                mouseNoteEnvelopeTimer.store(timer);
                const float attackDecay = noteAttack + noteDecay;
                const float phase = attackDecay > 0.001f ? timer / attackDecay : 1.0f;
                mouseNoteEnv = getNoteEnvelope(phase, false);
            }

            // --- Grain scheduler ---
            if (mousePressedNow && !mouseReleasedNow)
            {
                mouseGrainTimer += 1.0f / sampleRate;
                if (mouseGrainTimer >= grainInterval)
                {
                    mouseGrainTimer -= grainInterval;
                    for (int v = 0; v < maxGrains; ++v)
                    {
                        if (!mouseGrains[v].isActive)
                        {
                            triggerGrain(mouseGrains[v], -1, basePos);
                            break;
                        }
                    }
                }
            }

            // --- Accumulate mouse grains ---
            for (int v = 0; v < maxGrains; ++v)
            {
                auto& grain = mouseGrains[v];
                if (!grain.isActive) continue;

                const int idx = (int)grain.samplePosition;
                if (idx >= 0 && idx < maxSampleIndex)
                {
                    const double frac = grain.samplePosition - (double)idx;
                    float sample = 0.0f;
                    if (numChannels == 1)
                    {
                        const float* d = sampleBuffer.getReadPointer(0);
                        sample = d[idx] + (float)frac * (d[idx + 1] - d[idx]);
                    }
                    else
                    {
                        const float* l = sampleBuffer.getReadPointer(0);
                        const float* r = sampleBuffer.getReadPointer(1);
                        sample = 0.5f * (l[idx] + (float)frac * (l[idx + 1] - l[idx])
                            + r[idx] + (float)frac * (r[idx + 1] - r[idx]));
                    }
                    const float env = getGrainEnvelope(grain.grainEnvelopePhase, grain)
                        * mouseNoteEnv * grain.amplitude * volume;
                    totalLeft += sample * env * grain.panLeft;
                    totalRight += sample * env * grain.panRight;
                }

                grain.samplePosition += grain.pitch;
                grain.grainEnvelopePhase += 1.0f / (grainSize * sampleRate);
                if (grain.grainEnvelopePhase >= 1.0f || grain.samplePosition >= maxSampleIndex)
                    grain.isActive = false;
            }
        }

        // =====================================================================
        // POLYPHONIC MIDI VOICES
        // =====================================================================
        for (auto& nv : noteVoices)
        {
            if (!nv.isActive && !nv.isReleased) continue;

            // --- Note envelope ---
            float noteEnv = 1.0f;
            if (nv.isReleased)
            {
                nv.noteEnvelopeTimer += 1.0f / sampleRate;
                const float phase = nv.noteEnvelopeTimer / noteRelease;
                noteEnv = getNoteEnvelope(phase, true);

                if (nv.noteEnvelopeTimer >= noteRelease)
                {
                    // Voice finished releasing — fully deactivate
                    nv.isActive = false;
                    nv.isReleased = false;
                    for (auto& g : nv.grains) g.isActive = false;
                    continue;
                }
            }
            else
            {
                nv.noteEnvelopeTimer += 1.0f / sampleRate;
                const float attackDecay = noteAttack + noteDecay;
                const float phase = attackDecay > 0.001f
                    ? nv.noteEnvelopeTimer / attackDecay : 1.0f;
                noteEnv = getNoteEnvelope(phase, false);
            }

            // --- Grain scheduler ---
            if (!nv.isReleased)
            {
                nv.grainTimer += 1.0f / sampleRate;
                if (nv.grainTimer >= grainInterval)
                {
                    nv.grainTimer -= grainInterval;
                    for (int v = 0; v < maxGrains; ++v)
                    {
                        if (!nv.grains[v].isActive)
                        {
                            triggerGrain(nv.grains[v], nv.midiNote, basePosKnob);
                            break;
                        }
                    }
                }
            }

            // --- Accumulate grains ---
            for (int v = 0; v < maxGrains; ++v)
            {
                auto& grain = nv.grains[v];
                if (!grain.isActive) continue;

                const int idx = (int)grain.samplePosition;
                if (idx >= 0 && idx < maxSampleIndex)
                {
                    const double frac = grain.samplePosition - (double)idx;
                    float sample = 0.0f;
                    if (numChannels == 1)
                    {
                        const float* d = sampleBuffer.getReadPointer(0);
                        sample = d[idx] + (float)frac * (d[idx + 1] - d[idx]);
                    }
                    else
                    {
                        const float* l = sampleBuffer.getReadPointer(0);
                        const float* r = sampleBuffer.getReadPointer(1);
                        sample = 0.5f * (l[idx] + (float)frac * (l[idx + 1] - l[idx])
                            + r[idx] + (float)frac * (r[idx + 1] - r[idx]));
                    }
                    const float env = getGrainEnvelope(grain.grainEnvelopePhase, grain)
                        * noteEnv * grain.amplitude * volume;
                    totalLeft += sample * env * grain.panLeft;
                    totalRight += sample * env * grain.panRight;
                }

                grain.samplePosition += grain.pitch;
                grain.grainEnvelopePhase += 1.0f / (grainSize * sampleRate);
                if (grain.grainEnvelopePhase >= 1.0f || grain.samplePosition >= maxSampleIndex)
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
    // ScopedTryLock: if the GUI is currently reading, we skip this update
    // rather than blocking the audio thread. The visualiser catches up next block.
    // =========================================================================
    {
        const juce::ScopedTryLock dl(displayLock);
        if (dl.isLocked())
        {
            displaySnapshot.clear();
            for (const auto& nv : noteVoices)
                for (const auto& g : nv.grains)
                    if (g.isActive) displaySnapshot.push_back(g);
            for (const auto& g : mouseGrains)
                if (g.isActive) displaySnapshot.push_back(g);
        }
    }
}

//==============================================================================
void GranulateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

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

    // Store the last loaded sample path as a property on the state tree so
    // it survives DAW session save/reload.
    if (lastLoadedSamplePath.isNotEmpty())
        state.setProperty("samplePath", lastLoadedSamplePath, nullptr);

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

        // Restore the last loaded sample. If the file no longer exists
        // (moved, deleted, on a different machine) we silently skip it —
        // the user will just see an empty waveform display as if starting fresh.
        const juce::String path = newState.getProperty("samplePath", "").toString();
        if (path.isNotEmpty())
        {
            const juce::File f(path);
            if (f.existsAsFile())
                loadSample(f);
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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GranulateAudioProcessor();
}