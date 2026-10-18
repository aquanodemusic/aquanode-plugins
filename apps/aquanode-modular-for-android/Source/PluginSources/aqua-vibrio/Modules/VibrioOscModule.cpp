#include "VibrioOscModule.h"

namespace aquavibrio
{

using namespace aquanode;

void VibrioOscModule::resolveIndices()
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
    pPanMode = paramIndex ("panMode");
    pDrift = paramIndex ("drift");
    pSync = paramIndex ("sync");
    pSyncRatio = paramIndex ("syncRatio");
    pFormant = paramIndex ("formant");
    pSyncOut = paramIndex ("syncOut");
    pInitPhase = paramIndex ("initPhase");
    pSyncEnv = paramIndex ("syncEnvAmount");
    indicesResolved = true;
}

void VibrioOscModule::voiceNoteOn (int voice, int note, bool retrigger)
{
    if (! indicesResolved)
        resolveIndices();

    pool.noteOn (voice, voiceLimit());
    glide.noteOn (voice, (float) note, isMonoVoice());
    gateOn[voice] = true;

    if (retrigger)
        return;

    if (instantGate)
        gateLvl[voice] = 1.0f;

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
        held[voice][u] = 0.0f;
    }

    syncPhase[voice] = 0.0;
}

void VibrioOscModule::voiceNoteOff (int voice)
{
    pool.noteOff (voice, voiceLimit());
    gateOn[voice] = false;
}

void VibrioOscModule::voiceReset (int voice)
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
        held[voice][u] = 0.0f;
    }
}

// The same waveform as renderPartial without any band-limiting of its steps
// (the wave table part is band-limited by its mip already). The band-limited
// sync path places the steps itself.
float VibrioOscModule::naiveValue (float wave, double p, float shape, float pulseWidth,
                                   float formant, int mip, float waveGain, float pulseGain) const
{
    const double formantPhase = formant == 1.0f ? p : std::fmod (p * (double) formant, 1.0);
    const int w0 = juce::jlimit (0, VibrioWaveTable::kNumWaves - 1, (int) wave);
    const float wf = juce::jlimit (0.0f, 1.0f, wave - (float) w0);
    const auto* tbl = table.load (std::memory_order_relaxed);
    float wavePart = tbl->readMip (w0, formantPhase, mip);

    if (wf > 0.001f && w0 < VibrioWaveTable::kNumWaves - 1)
        wavePart += (tbl->readMip (w0 + 1, formantPhase, mip) - wavePart) * wf;

    wavePart *= waveGain;

    if (shape <= 0.0f)
        return wavePart;

    const float saw = (float) (1.0 - 2.0 * p);   // falling, as in renderPartial

    if (shape <= 0.5f)
        return wavePart + (saw - wavePart) * (shape * 2.0f);

    // the pulse without its DC: +width or -(1 - width), times its gain
    const float pulse = (p < 1.0 - (double) pulseWidth ? pulseWidth : pulseWidth - 1.0f) * pulseGain;
    return saw + (pulse - saw) * ((shape - 0.5f) * 2.0f);
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
float VibrioOscModule::renderPartial (float wave, double p, float shape, float pulseWidth,
                                     float formant, double frequency, int mip, float waveGain, float pulseGain)
{

    // Formant shift: read the cycle at a multiple of its own rate and wrap.
    // At 1.0 nothing changes; above it the whole spectrum slides upward while
    // the repeat rate, and so the pitch, stays put.
    const double formantPhase = formant == 1.0f
                                  ? p
                                  : std::fmod (p * (double) formant, 1.0);

    // Wave is continuous: between two table entries the two neighbours are
    // crossfaded, which is what lets a wavetable index sweep smoothly.
    const int w0 = juce::jlimit (0, VibrioWaveTable::kNumWaves - 1, (int) wave);
    const float wf = juce::jlimit (0.0f, 1.0f, wave - (float) w0);
    const auto* tbl = table.load (std::memory_order_relaxed);
    float wavePart = tbl->readMip (w0, formantPhase, mip);

    if (wf > 0.001f && w0 < VibrioWaveTable::kNumWaves - 1)
        wavePart += (tbl->readMip (w0 + 1, formantPhase, mip) - wavePart) * wf;

    wavePart *= waveGain;

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
    // 17 times as much DC as signal while the hardware has essentially none. The
    // offset term is dropped here rather than filtered later, so pulse-width
    // modulation does not pump the whole voice up and down.
    const float pulse = (saw - saw2) * 0.5f * pulseGain;

    return saw + (pulse - saw) * ((shape - 0.5f) * 2.0f);
}

//==============================================================================
// One partial on the band-limited path: advance the phase, collect the steps
// the waveform takes on the way (each at its exact time within the sample),
// and return the previous sample with all its corrections in.
float VibrioOscModule::renderBandLimited (int v, int u, double freq, bool reset, double sinceWrap,
                                          float wave, float shape, float pulseWidth, float formant,
                                          float waveGainNow, float pulseGainNow)
{
    const double inc = freq / sampleRate;   // through-zero: may be negative
    const int mip = mips[v][u].get (std::abs (freq), [this] (double f) { return VibrioWaveTable::mipFor (f, sampleRate); });
    const auto value = [&] (double p) { return naiveValue (wave, p, shape, pulseWidth, formant, mip, waveGainNow, pulseGainNow); };

    // The steps, forwards: the cycle's wrap (the saw's jump, the pulse's rise
    // and, with Formant Shift, the wave's own jump - read off the waveform),
    // and the pulse's falling edge.
    // (a pulse of zero or full width is silence: no edges at all)
    const bool hasEdge = pulseWidth > 1.0e-6f && pulseWidth < 1.0f - 1.0e-6f;
    const float pulseW = shape <= 0.5f || ! hasEdge ? 0.0f : shape * 2.0f - 1.0f;
    const auto wrapStep = [&] { return value (0.0) - value (1.0 - 1.0e-9); };
    const float edgeStep = -pulseGainNow * pulseW;
    const double edge = 1.0 - (double) pulseWidth;

    float before = 0.0f, after = 0.0f;
    const auto step = [&] (float size, double tau)   // tau: samples from the step to now
    {
        tau = juce::jlimit (0.0, 1.0, tau);
        before += size * (float) (0.5 * tau * tau);
        after  -= size * (float) (0.5 * (1.0 - tau) * (1.0 - tau));
    };

    // the steps passed while running from p0 for `span` samples (ending `endTau`
    // samples before now); returns the phase reached, wrapped
    const auto run = [&] (double p0, double span, double endTau)
    {
        double p1 = p0 + inc * span;
        if (inc > 0.0)
        {
            if (pulseW > 0.0f)
                for (double e : { edge, edge + 1.0 })
                    if (p0 < e && p1 >= e)
                        step (edgeStep, endTau + (p1 - e) / inc);
            if (p1 >= 1.0)
            {
                step (wrapStep(), endTau + (p1 - 1.0) / inc);
                p1 -= std::floor (p1);
            }
        }
        else if (inc < 0.0)
        {
            if (pulseW > 0.0f)
                for (double e : { edge, edge - 1.0 })
                    if (p0 > e && p1 <= e)
                        step (-edgeStep, endTau + (p1 - e) / inc);
            if (p1 < 0.0)
            {
                step (-wrapStep(), endTau + p1 / inc);
                p1 -= std::floor (p1);
            }
        }
        return p1;
    };

    double p;
    if (reset && inc != 0.0)
    {
        // the old cycle runs up to the reset, the new one from zero for the rest
        const double atReset = run (phase[v][u], 1.0 - sinceWrap, sinceWrap);
        const double start = inc > 0.0 ? 0.0 : 1.0 - 1.0e-9;
        step (value (start) - value (atReset), sinceWrap);
        p = run (start, sinceWrap, 0.0);
    }
    else
    {
        p = run (phase[v][u], 1.0, 0.0);
    }
    phase[v][u] = p;

    const float out = held[v][u] + before;
    held[v][u] = value (p) + after;
    return out;
}

//==============================================================================
void VibrioOscModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    if (! indicesResolved)
        resolveIndices();

    zeroOutputs (outputs);

    const float voiceGain = pool.nextGain (v, sampleRate);

    if (pool.isSilent (v) && ! gateOn[v] && gateLvl[v] <= 0.0f)
        return;

    freqHz[v] = noteHz[v].get (glide.next (v, glideMillis(), isMonoVoice(),
                                                   ! pool.isMuted (v), sampleRate),
                               [] (float note) { return midiNoteToHz ((double) note); });

    const float fmIn = isInputConnected (0) ? inputs[0][0] : 0.0f;
    const float syncIn = isInputConnected (1) ? inputs[1][0] : 0.0f;
    const float envIn = isInputConnected (2) ? inputs[2][0] : 1.0f;

    const int mode = (int) param (pMode);
    const float wave = juce::jlimit (0.0f, (float) (VibrioWaveTable::kNumWaves - 1), param (pWave));
    const float shape = juce::jlimit (0.0f, 1.0f, param (pShape));
    const float pulseWidth = juce::jlimit (narrowestPulse, 1.0f - narrowestPulse, param (pPulseWidth));
    const float formant = juce::jmax (0.25f, param (pFormant));
    const bool syncEnabled = param (pSync) > 0.5f;

    const int unison = juce::jlimit (1, kMaxUnison, (int) param (pUnison));
    const float detuneCents = param (pDetune);
    const float spread = param (pSpread);
    const bool alternatePan = pPanMode >= 0 && param (pPanMode) > 0.5f;
    const float driftCents = param (pDrift);

    // FM In scales the frequency: f * (1 + in). Normally held above zero;
    // through-zero (opt-in) lets it go negative and run the cycle backwards.
    const double fmFreq = freqHz[v] * (double) param (pRatio) * (1.0 + (double) fmIn);
    const double baseFreq = throughZero ? juce::jlimit (-sampleRate * 0.49, sampleRate * 0.49, fmFreq)
                                        : juce::jlimit (0.05, sampleRate * 0.49, fmFreq);

    // The master's own cycle, used for hard sync. Its frequency is the
    // oscillator's times Sync Ratio, which is how the hardware can sync to a
    // pitch other than the one it plays.
    bool masterWrapped = false;
    double sinceWrap = 0.0;   // band-limited path: samples since the master wrapped

    if (syncEnabled || param (pSyncOut) > 0.5f)
    {
        // The sync frequency takes the envelope too - sweeping it is the
        // whole point of a sync sound, and on the hardware it is the filter
        // envelope that does the sweeping.
        const double syncRatio = juce::jmax (0.05,
            (double) param (pSyncRatio) * (1.0 + (double) param (pSyncEnv) * (double) (isInputConnected (2) ? envIn : 0.0f)));

        const double masterInc = (bandLimitedSync ? freqHz[v] * (double) param (pRatio) : std::abs (baseFreq))
                                   * syncRatio / sampleRate;
        syncPhase[v] += masterInc;

        if (syncPhase[v] >= 1.0)
        {
            syncPhase[v] -= std::floor (syncPhase[v]);
            masterWrapped = true;
            sinceWrap = juce::jlimit (0.0, 1.0, syncPhase[v] / juce::jmax (1.0e-9, masterInc));
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
        const double cents = (double) (drift[v][u] * driftCents + unisonPos * detuneCents + partialCents[v][u]);
        const double freq = baseFreq * centsRatio[v][u].get (cents, [] (double c) { return std::pow (2.0, c / 1200.0); });

        if (bandLimitedSync && ! syncedFromInput)
        {
            const float value = renderBandLimited (v, u, freq, syncEnabled && masterWrapped, sinceWrap,
                                                   wave, mode == 1 ? 0.5f : shape, pulseWidth,
                                                   mode == 2 ? formant : 1.0f,
                                                   mode == 0 ? waveGain : 1.0f, mode == 0 ? pulseGain : 1.0f);
            const float pan = juce::jlimit (-1.0f, 1.0f, (alternatePan && unison > 1 ? ((u & 1) ? 1.0f : -1.0f) : unisonPos) * spread);
            const auto gains = panGains[v][u].get (pan, [] (float p)
            {
                return std::make_pair (std::sqrt (0.5f * (1.0f - p)), std::sqrt (0.5f * (1.0f + p)));
            });
            sumL += value * gains.first;
            sumR += value * gains.second;
            continue;
        }

        phase[v][u] += freq / sampleRate;

        if (phase[v][u] >= 1.0 || phase[v][u] < 0.0)
            phase[v][u] -= std::floor (phase[v][u]);

        // hard sync: the slave restarts its cycle when the master wraps,
        // which is what puts the harmonics somewhere other than the pitch
        if (syncEnabled && (masterWrapped || syncedFromInput))
            phase[v][u] = 0.0;

        const float sampleValue = renderPartial (wave, phase[v][u],
                                       mode == 1 ? 0.5f : shape,   // HyperSaw is saws
                                       pulseWidth,
                                       mode == 2 ? formant : 1.0f, // formant only in Wavetable
                                       std::abs (freq),
                                       mips[v][u].get (std::abs (freq), [this] (double f) { return VibrioWaveTable::mipFor (f, sampleRate); }),
                                       mode == 0 ? waveGain : 1.0f,
                                       mode == 0 ? pulseGain : 1.0f);

        // Even: voices fanned out across the field. Alternate (opt-in, used by
        // the Original engine): every voice hard left or right in turn.
        const float pan = juce::jlimit (-1.0f, 1.0f, (alternatePan && unison > 1 ? ((u & 1) ? 1.0f : -1.0f) : unisonPos) * spread);
        const auto gains = panGains[v][u].get (pan, [] (float p)
        {
            return std::make_pair (std::sqrt (0.5f * (1.0f - p)), std::sqrt (0.5f * (1.0f + p)));
        });
        sumL += sampleValue * gains.first;
        sumR += sampleValue * gains.second;
    }

    // gate ramp, so a note off does not cut the waveform mid-cycle
    const float target = gateOn[v] ? 1.0f : 0.0f;
    const float step = (float) (1.0 / (sampleRate * 0.003));
    gateLvl[v] += juce::jlimit (-step, step, target - gateLvl[v]);

    static const auto norms = []
    {
        std::array<float, kMaxUnison + 1> n {};
        for (int k = 1; k <= kMaxUnison; ++k)
            n[(size_t) k] = 1.0f / std::sqrt ((float) k);
        return n;
    }();
    const float norm = norms[(size_t) unison];
    const float amp = voiceGain * gateLvl[v] * vel[v] * param (pVolume) * envIn * norm;

    outputs[0][0] = sumL * amp;
    outputs[0][1] = sumR * amp;

    // Sync Out rides on the right channel of the output when it is switched
    // on, which is what a slave's Sync In reads.
    if (param (pSyncOut) > 0.5f)
        outputs[0][1] = masterWrapped ? 1.0f : 0.0f;
}

//==============================================================================
static ModuleDescriptor vibrioOscDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.vibrio";
    d.displayName = "Vibrio Oscillator";
    d.description =
        "The hardware-style oscillator: a 64-waveform table with Shape morphing towards a saw and "
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
        makeRotary ("pulseWidth", "Pulse Width", 0.0f, 1.0f, 0.5f, 1),   // held to 2..98 % unless opted out
        makeRotary ("formant", "Formant", 0.25f, 4.0f, 1.0f, 1, {}, true),
        makeRotary ("unison", "Unison", 1.0f, (float) VibrioOscModule::kMaxUnison, 1.0f, 2, {}, false, 1.0f),
        makeRotary ("detune", "Detune", 0.0f, 100.0f, 0.0f, 2, "ct"),
        makeRotary ("spread", "Spread", 0.0f, 1.0f, 0.5f, 2),
        makeCombo  ("panMode", "Unison Pan", { "Even", "Alternate" }, 0, 2, 2),
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
using VibrioOscModule = aquavibrio::VibrioOscModule;
AQUANODE_REGISTER_MODULE (VibrioOscModule, aquavibrio::vibrioOscDescriptor)
