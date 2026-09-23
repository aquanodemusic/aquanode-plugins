#include "VirusOscModule.h"

namespace aquavibrio
{

using namespace aquanode;

void VirusOscModule::resolveIndices()
{
    pVolume = paramIndex ("volume");
    pRatio = paramIndex ("fmRatio");
    pMode = paramIndex ("mode");
    pWave = paramIndex ("wave");
    pShape = paramIndex ("shape");
    pPulseWidth = paramIndex ("pulseWidth");
    pUnison = paramIndex ("unison");
    pDetune = paramIndex ("detune");
    pSpread = paramIndex ("spread");
    pDrift = paramIndex ("drift");
    pSync = paramIndex ("sync");
    pSyncRatio = paramIndex ("syncRatio");
    pFormant = paramIndex ("formant");
    pSyncOut = paramIndex ("syncOut");
    pInitPhase = paramIndex ("initPhase");
    pSyncEnv = paramIndex ("syncEnvAmount");
    indicesResolved = true;
}

void VirusOscModule::voiceNoteOn (int voice, int note, bool retrigger)
{
    if (! indicesResolved)
        resolveIndices();

    pool.noteOn (voice, voiceLimit());
    glide.noteOn (voice, (float) note, isMonoVoice());
    gateOn[voice] = true;

    if (retrigger)
        return;

    // Init Phase at zero leaves the oscillator free-running, which is the
    // analogue behaviour and why two notes never sound quite identical. Above
    // zero every note starts at that phase, which is what gives a patch a
    // consistent attack transient.
    const float initPhase = param (pInitPhase);

    // Free-running: every copy in the stack starts at an unrelated phase.
    // (Evenly fanned phases look tidy but cancel: nine sines a ninth of a
    // cycle apart sum to silence. Random phases keep the stack's energy at
    // the 1/sqrt(n) the output gain expects.)
    for (int u = 0; u < kMaxUnison; ++u)
    {
        phaseSeed = phaseSeed * 1664525u + 1013904223u;
        phase[voice][u] = initPhase > 0.0f ? (double) initPhase
                                           : (double) (phaseSeed >> 8) / 16777216.0;
    }

    syncPhase[voice] = 0.0;
}

void VirusOscModule::voiceNoteOff (int voice)
{
    pool.noteOff (voice, voiceLimit());
    gateOn[voice] = false;
}

void VirusOscModule::voiceReset (int voice)
{
    pool.resetVoice (voice);
    gateOn[voice] = false;
    gateLvl[voice] = 0.0f;
    syncPhase[voice] = 0.0;
    lastSyncHigh[voice] = false;

    for (int u = 0; u < kMaxUnison; ++u)
    {
        phase[voice][u] = 0.0;
        drift[voice][u] = 0.0f;
        driftCount[voice][u] = 0.0;
    }
}

//==============================================================================
// One partial. Mode decides what the waveform actually is:
//
//   Classic   the wave table, morphed by Shape towards a saw and then a
//             pulse whose width is Pulse Width
//   HyperSaw  a plain saw - the character comes from the unison stack
//   Wavetable the same table read with Formant Shift, which scans the cycle
//             faster than it repeats so the spectrum moves up without the
//             pitch following it
//
float VirusOscModule::renderPartial (float wave, double p, float shape, float pulseWidth,
                                     float formant, double frequency)
{
    const auto& table = VirusWaveTable::instance();

    // Formant shift: read the cycle at a multiple of its own rate and wrap.
    // At 1.0 nothing changes; above it the whole spectrum slides upward while
    // the repeat rate, and so the pitch, stays put.
    const double formantPhase = formant == 1.0f
                                  ? p
                                  : std::fmod (p * (double) formant, 1.0);

    // Wave is continuous: between two table entries the two neighbours are
    // crossfaded, which is what lets a wavetable index sweep smoothly.
    const int w0 = juce::jlimit (0, VirusWaveTable::kNumWaves - 1, (int) wave);
    const float wf = juce::jlimit (0.0f, 1.0f, wave - (float) w0);
    float wavePart = table.read (w0, formantPhase, frequency, sampleRate);

    if (wf > 0.001f && w0 < VirusWaveTable::kNumWaves - 1)
        wavePart += (table.read (w0 + 1, formantPhase, frequency, sampleRate) - wavePart) * wf;

    if (shape <= 0.0f)
        return wavePart;

    // saw, band-limited enough for this purpose by the PolyBLEP correction
    const auto blep = [] (double t, double dt)
    {
        if (t < dt)        { t /= dt;  return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt)  { t = (t - 1.0) / dt;  return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    };

    // A FALLING saw: its fundamental is +sin, in phase with the table's sine
    // based waves. (A rising saw is exactly out of phase with them, so the
    // wave->saw crossfade cancelled - about 9 dB quieter mid-morph.)
    const double dt = frequency / sampleRate;
    const float saw = blep (p, dt) - (float) (2.0 * p - 1.0);

    if (shape <= 0.5f)
        return wavePart + (saw - wavePart) * (shape * 2.0f);

    // pulse: a saw minus the same saw shifted by the pulse width
    const double shifted = p + (double) pulseWidth >= 1.0
                             ? p + (double) pulseWidth - 1.0
                             : p + (double) pulseWidth;

    const float saw2 = blep (shifted, dt) - (float) (2.0 * shifted - 1.0);
    // A pulse wave's DC offset changes with its width, and at narrow settings
    // it dominates the output - measured against the hardware, ours had up to
    // 17 times as much DC as signal while the Virus has essentially none. The
    // offset term is dropped here rather than filtered later, so pulse-width
    // modulation does not pump the whole voice up and down.
    const float pulse = (saw - saw2) * 0.5f;

    return saw + (pulse - saw) * ((shape - 0.5f) * 2.0f);
}

//==============================================================================
void VirusOscModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (! indicesResolved)
        resolveIndices();

    zeroOutputs (outputs);

    const float voiceGain = pool.nextGain (v, sampleRate);

    if (pool.isSilent (v) && ! gateOn[v] && gateLvl[v] <= 0.0f)
        return;

    freqHz[v] = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                   ! pool.isMuted (v), sampleRate));

    const float fmIn = isInputConnected (0) ? inputs[0][0] : 0.0f;
    const float syncIn = isInputConnected (1) ? inputs[1][0] : 0.0f;
    const float envIn = isInputConnected (2) ? inputs[2][0] : 1.0f;

    const int mode = (int) param (pMode);
    const float wave = juce::jlimit (0.0f, (float) (VirusWaveTable::kNumWaves - 1), param (pWave));
    const float shape = juce::jlimit (0.0f, 1.0f, param (pShape));
    const float pulseWidth = juce::jlimit (0.02f, 0.98f, param (pPulseWidth));
    const float formant = juce::jmax (0.25f, param (pFormant));
    const bool syncEnabled = param (pSync) > 0.5f;

    const int unison = juce::jlimit (1, kMaxUnison, (int) param (pUnison));
    const float detuneCents = param (pDetune);
    const float spread = param (pSpread);
    const float driftCents = param (pDrift);

    const double baseFreq = juce::jlimit (0.05, sampleRate * 0.49,
                                          freqHz[v] * (double) param (pRatio) * (1.0 + (double) fmIn));

    // The master's own cycle, used for hard sync. Its frequency is the
    // oscillator's times Sync Ratio, which is how the Virus can sync to a
    // pitch other than the one it plays.
    bool masterWrapped = false;

    if (syncEnabled || param (pSyncOut) > 0.5f)
    {
        // The sync frequency takes the envelope too - sweeping it is the
        // whole point of a sync sound, and on the Virus it is the filter
        // envelope that does the sweeping.
        const double syncRatio = juce::jmax (0.05,
            (double) param (pSyncRatio) * (1.0 + (double) param (pSyncEnv) * (double) (isInputConnected (2) ? envIn : 0.0f)));

        syncPhase[v] += baseFreq * syncRatio / sampleRate;

        if (syncPhase[v] >= 1.0)
        {
            syncPhase[v] -= std::floor (syncPhase[v]);
            masterWrapped = true;
        }
    }

    // A slave takes its reset from the Sync In socket instead.
    const bool syncedFromInput = syncIn > 0.5f && ! lastSyncHigh[v];
    lastSyncHigh[v] = syncIn > 0.5f;

    float sumL = 0.0f, sumR = 0.0f;

    for (int u = 0; u < unison; ++u)
    {
        // slow random walk in pitch, one per partial
        if ((driftCount[v][u] -= 1.0) <= 0.0)
        {
            drift[v][u] = rng.nextFloat() * 2.0f - 1.0f;
            driftCount[v][u] = sampleRate * (0.1 + rng.nextDouble() * 0.9);
        }

        const float unisonPos = unison > 1 ? 2.0f * (float) u / (float) (unison - 1) - 1.0f : 0.0f;
        const double cents = (double) (drift[v][u] * driftCents + unisonPos * detuneCents);
        const double freq = baseFreq * std::pow (2.0, cents / 1200.0);

        phase[v][u] += freq / sampleRate;

        if (phase[v][u] >= 1.0)
            phase[v][u] -= std::floor (phase[v][u]);

        // hard sync: the slave restarts its cycle when the master wraps,
        // which is what puts the harmonics somewhere other than the pitch
        if (syncEnabled && (masterWrapped || syncedFromInput))
            phase[v][u] = 0.0;

        const float sampleValue = renderPartial (wave, phase[v][u],
                                       mode == 1 ? 0.5f : shape,   // HyperSaw is saws
                                       pulseWidth,
                                       mode == 2 ? formant : 1.0f, // formant only in Wavetable
                                       freq);

        const float pan = juce::jlimit (-1.0f, 1.0f, unisonPos * spread);
        sumL += sampleValue * std::sqrt (0.5f * (1.0f - pan));
        sumR += sampleValue * std::sqrt (0.5f * (1.0f + pan));
    }

    // gate ramp, so a note off does not cut the waveform mid-cycle
    const float target = gateOn[v] ? 1.0f : 0.0f;
    const float step = (float) (1.0 / (sampleRate * 0.003));
    gateLvl[v] += juce::jlimit (-step, step, target - gateLvl[v]);

    const float norm = 1.0f / std::sqrt ((float) unison);
    const float amp = voiceGain * gateLvl[v] * vel[v] * param (pVolume) * envIn * norm;

    outputs[0][0] = sumL * amp;
    outputs[0][1] = sumR * amp;

    // Sync Out rides on the right channel of the output when it is switched
    // on, which is what a slave's Sync In reads.
    if (param (pSyncOut) > 0.5f)
        outputs[0][1] = masterWrapped ? 1.0f : 0.0f;
}

//==============================================================================
static ModuleDescriptor virusOscDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.virus";
    d.displayName = "Virus Oscillator";
    d.description =
        "The Access Virus oscillator: a 64-waveform table with Shape morphing towards a saw and "
        "then a pulse, hard sync with its own sync frequency, a unison stack for HyperSaw, and a "
        "formant shift that moves the spectrum without moving the pitch. Mode picks which of the "
        "three oscillator models is running.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 0;
    d.sockets = {
        modIn ("fmIn", "FM In"),
        modIn ("syncIn", "Sync In"),
        modIn ("envIn", "Env In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 0),
        makeRotary ("fmRatio", "FM Ratio", 0.001f, 100.0f, 1.0f, 0, {}, true),
        makeCombo  ("mode", "Mode", { "Classic", "HyperSaw", "Wavetable" }, 0, 0, 2),
        makeRotary ("wave", "Wave", 0.0f, 63.0f, 2.0f, 1, {}, false, 1.0f),
        makeRotary ("shape", "Shape", 0.0f, 1.0f, 0.5f, 1),
        makeRotary ("pulseWidth", "Pulse Width", 0.02f, 0.98f, 0.5f, 1),
        makeRotary ("formant", "Formant", 0.25f, 4.0f, 1.0f, 1, {}, true),
        makeRotary ("unison", "Unison", 1.0f, (float) VirusOscModule::kMaxUnison, 1.0f, 2, {}, false, 1.0f),
        makeRotary ("detune", "Detune", 0.0f, 100.0f, 0.0f, 2, "ct"),
        makeRotary ("spread", "Spread", 0.0f, 1.0f, 0.5f, 2),
        makeRotary ("drift", "Drift", 0.0f, 50.0f, 3.0f, 2, "ct"),
        makeCombo  ("sync", "Sync", { "Off", "On" }, 0, 3, 2),
        makeRotary ("syncRatio", "Sync Freq", 0.02f, 32.0f, 1.0f, 3, {}, true),
        makeRotary ("syncEnvAmount", "Sync Env", -1.0f, 1.0f, 0.0f, 3),
        makeCombo  ("syncOut", "Sync Out", { "Off", "On" }, 0, 3, 2),
        makeRotary ("initPhase", "Init Phase", 0.0f, 1.0f, 0.0f, 3),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 4, {}, false, 1.0f).noMod(),
        makeRotary ("glide", "Glide", 0.0f, 1000.0f, 0.0f, 4, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

} // namespace aquavibrio

// The registration macro expands to an `extern "C"` variable, which has to
// sit at global scope - inside a namespace MSVC rejects it, and the error it
// reports points at <xlocale> rather than here. The vendored modules all do
// the same thing; they just have no namespace to be inside.
using VirusOscModule = aquavibrio::VirusOscModule;
AQUANODE_REGISTER_MODULE (VirusOscModule, aquavibrio::virusOscDescriptor)
