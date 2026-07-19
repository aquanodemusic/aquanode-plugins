#include "AnalogDriftModule.h"

using namespace aquanode;

//==============================================================================
void AnalogDriftModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void AnalogDriftModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void AnalogDriftModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    for (int u = 0; u < kMaxUnison; ++u)
    {
        phase[v][u] = 0.0;
        drift[v][u] = 0.0f;
        driftTarget[v][u] = 0.0f;
        driftCount[v][u] = 0.0;
        phWobble[v][u] = 0.0f;
        phTarget[v][u] = 0.0f;
        phCount[v][u] = 0.0;
    }
    freqHz[v] = 0.0;
    gateLvl[v] = 0.0f;
    gateOn[v] = false;
    vel[v] = 1.0f;
}

void AnalogDriftModule::blockStart()
{
    latchedSample = getLoadedSample();
    if (latchedSample != nullptr && latchedSample->getNumSamples() > 0)
    {
        tableData = latchedSample->getReadPointer (0);
        tableSourceLen = latchedSample->getNumSamples();
    }
    else
    {
        tableData = nullptr;
        tableSourceLen = 0;
    }

    // the random walks are lowpassed at roughly the Drift Rate so slow
    // settings wander lazily and fast settings jitter
    const double rate = juce::jmax (0.01, (double) param (pDriftRate));
    driftSmoothCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi
                                                * rate / sampleRate));
}

void AnalogDriftModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    gateOn[v] = true;
    if (! retrigger)
    {
        // analog oscillators free-run: every partial starts wherever it
        // happens to be, so each note attacks a little differently
        for (int u = 0; u < kMaxUnison; ++u)
            phase[v][u] = rng.nextDouble();
        gateLvl[v] = 0.0f;
    }
}

void AnalogDriftModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gateOn[v] = false;
}

float AnalogDriftModule::readWave (int waveform, double phase01) const
{
    switch (waveform)
    {
        case 0: return (float) std::sin (phase01 * juce::MathConstants<double>::twoPi);   // Sine
        case 1: return (float) (1.0 - 4.0 * std::abs (phase01 - 0.5));                    // Tri
        case 2: return (float) (2.0 * phase01 - 1.0);                                     // Saw
        case 3: return phase01 < 0.5 ? 1.0f : -1.0f;                                      // Square
        case 4: return readSampleTable (phase01);                                         // Sample
        default: return 0.0f;
    }
}

float AnalogDriftModule::readSampleTable (double phase01) const
{
    if (tableData == nullptr)
        return 0.0f;

    const int cycleLen = juce::jlimit (1, 2048, (int) param (pCycleLength));
    const double pos = phase01 * cycleLen;
    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % cycleLen;
    const double frac = pos - i0;

    const float s0 = i0 < tableSourceLen ? tableData[i0] : 0.0f;
    const float s1 = i1 < tableSourceLen ? tableData[i1] : 0.0f;
    return (float) (s0 + (s1 - s0) * frac);
}

// one step of a smoothed random walk: pick a fresh +-1 target at randomised
// Drift Rate intervals, glide toward it with a one-pole matched to that rate
void AnalogDriftModule::advanceDrift (float& value, float& target, double& countdown, double rateHz)
{
    countdown -= 1.0;
    if (countdown <= 0.0)
    {
        target = rng.nextFloat() * 2.0f - 1.0f;
        // 0.5..1.5x the nominal interval, so partials never move in lockstep
        countdown = (sampleRate / rateHz) * (0.5 + rng.nextDouble());
    }
    value += driftSmoothCoeff * (target - value);
}

void AnalogDriftModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void AnalogDriftModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    freqHz[v] = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                   ! pool.isMuted (v), sampleRate));

    const float fmIn = inputs[0][0];
    const bool envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    const double invSr = 1.0 / sampleRate;
    const double baseFreq = freqHz[v] * param (pFmRatio);

    const int unison = juce::jlimit (1, kMaxUnison, (int) param (pUnison));
    const double rate = juce::jmax (0.01, (double) param (pDriftRate));
    const float driftCents = param (pDrift);
    const float phDriftAmt = param (pPhaseDrift);
    const float detuneCents = param (pDetune);
    const float spread = param (pSpread);
    const int waveform = (int) param (pWaveform);

    float sumL = 0.0f, sumR = 0.0f;

    for (int u = 0; u < unison; ++u)
    {
        // independent random walks: pitch drift (cents) and phase wobble
        advanceDrift (drift[v][u], driftTarget[v][u], driftCount[v][u], rate);
        advanceDrift (phWobble[v][u], phTarget[v][u], phCount[v][u], rate);

        // symmetric unison detune (-1..+1 across the stack, 0 when alone)
        const float unisonPos = unison > 1
            ? 2.0f * (float) u / (float) (unison - 1) - 1.0f
            : 0.0f;

        const double cents = (double) (drift[v][u] * driftCents + unisonPos * detuneCents);
        const double freq = baseFreq * std::exp (cents * (1.0 / 1200.0) * 0.69314718055994531);

        phase[v][u] += freq * invSr;
        phase[v][u] -= std::floor (phase[v][u]);

        // FM In offsets the read phase DX7-style; Phase Drift adds a slow
        // random offset on top (up to +-0.2 cycles at full)
        double readPhase = phase[v][u] + (double) fmIn
                         + (double) (phWobble[v][u] * phDriftAmt * 0.2f);
        readPhase -= std::floor (readPhase);

        const float y = readWave (waveform, readPhase);

        // equal-power spread across the stereo field
        const float pan = unisonPos * spread;                       // -1..1
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        sumL += y * std::cos (ang);
        sumR += y * std::sin (ang);
    }

    // keep the perceived level steady as partials stack up
    const float norm = 1.0f / std::sqrt ((float) unison);

    float gain;
    if (envConnected)
        gain = envIn;
    else
    {
        const float gateStep = (float) (invSr / gateRampSeconds);
        const float target = gateOn[v] ? 1.0f : 0.0f;
        if (gateLvl[v] < target)      gateLvl[v] = juce::jmin (target, gateLvl[v] + gateStep);
        else if (gateLvl[v] > target) gateLvl[v] = juce::jmax (target, gateLvl[v] - gateStep);
        gain = gateLvl[v];
    }

    const float amp = gain * vel[v] * param (pVolume) * norm;
    outputs[0][0] = sumL * amp;
    outputs[0][1] = sumR * amp;
}

//==============================================================================
static ModuleDescriptor analogDriftDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.drift";
    d.displayName = "Analog Drift";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 17;
    d.sockets = {
        modIn ("fmIn",  "FM In"),
        modIn ("envIn", "Env In"),
        midiIn ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume",  "Volume",   0.0f, 1.0f, 0.8f, 0),
        makeRotary ("fmRatio", "FM Ratio", 0.001f, 100.0f, 1.0f, 0, {}, true),
        makeCombo  ("waveform", "Waveform", { "Sine", "Tri", "Saw", "Square", "Sample" }, 2, 0, 2),
        makeRotary ("cycleLength", "Cycle Length", 1.0f, 2048.0f, 2048.0f, 1, {}, false, 1.0f)
            .visibleWhen ("waveform", 4.0f),
        makeButton ("loadSample", "Load Sample", 1, 2)
            .visibleWhen ("waveform", 4.0f),
        makeRotary ("drift",      "Drift",       0.0f, 50.0f, 8.0f, 2, "ct"),
        makeRotary ("driftRate",  "Drift Rate",  0.05f, 8.0f, 0.5f, 2, "Hz", true),
        makeRotary ("phaseDrift", "Phase Drift", 0.0f, 1.0f, 0.15f, 2),
        makeRotary ("unison",     "Unison",      1.0f, (float) AnalogDriftModule::kMaxUnison,
                                                 3.0f, 4, {}, false, 1.0f).noMod(),
        makeRotary ("detune",     "Detune",      0.0f, 100.0f, 12.0f, 4, "ct"),
        makeRotary ("spread",     "Spread",      0.0f, 1.0f, 0.7f, 4),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 6, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 6, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AnalogDriftModule, analogDriftDescriptor)
