#include "Voice.h"

namespace aquanova {

namespace
{
    /** LFO1Sync / LFO2Sync: index 0 is "Off" (free-running, uses the Speed
        knob); 1..34 pick a note division, same table and order as the
        arpeggiator's sync list (32nd triplet .. 8 bar dotted), turned into Hz
        at the host tempo instead of seconds. */
    float syncedLfoHz (int syncIndex, double hostBpm) noexcept
    {
        static const double beats[34] =
        {
            1.0/6, 0.125, 1.0/3, 0.25, 2.0/3, 0.375, 0.5, 4.0/3, 0.75, 1.0,
            8.0/3, 1.5, 2.0, 16.0/3, 3.0, 4.0, 32.0/3, 6.0, 8.0, 64.0/3,
            12.0, 80.0/3, 16.0, 18.0, 112.0/3, 20.0, 128.0/3, 24.0, 28.0, 30.0,
            32.0, 36.0, 42.0, 48.0
        };

        const double beatSeconds = 60.0 / juce::jmax (20.0, hostBpm);
        const double noteSeconds = beats[juce::jlimit (0, 33, syncIndex - 1)] * beatSeconds;

        return (float) (1.0 / juce::jmax (0.001, noteSeconds));
    }
}

//==============================================================================
void Voice::prepare (double sr, const ParamAccess* paramsIn)
{
    sampleRate = sr;
    params = paramsIn;

    for (auto& o : oscs) o.osc.prepare (sr);
    noise.prepare (sr);
    filter.prepare (sr);
    env1.prepare (sr);
    env2.prepare (sr);
    env3.prepare (sr);
    lfo1.prepare (sr);
    lfo2.prepare (sr);

    reset();
}

void Voice::reset()
{
    for (auto& o : oscs) o.osc.reset();
    filter.reset();
    env1.reset(); env2.reset(); env3.reset();
    lfo1.reset(); lfo2.reset();
    ring1State = ring2State = 0.0f;
    held = false;
}

//==============================================================================
namespace
{
    Envelope::Settings envSettings (const ParamAccess& p, int base, bool hasDelay)
    {
        Envelope::Settings s;
        s.attack       = p.raw (base + 0);
        s.decay        = p.raw (base + 1);
        s.sustain      = p.raw (base + 2);
        s.release      = p.raw (base + 3);
        s.velocityAmount = p.uni (base + 4);

        if (hasDelay)
        {
            s.delay      = p.raw (base + 5);
            s.keyTrack   = p.uni (base + 6);
            s.adRepeat   = p.raw (base + 7);
            s.sustainTime= p.raw (base + 8);
            s.sustainRate= p.bip (base + 10);
        }
        else
        {
            s.delay      = 0;
            s.keyTrack   = p.uni (P::Env1KeyTracking);
            s.adRepeat   = p.raw (P::Env1ADRepeat);
            s.sustainTime= p.raw (P::Env1SustainTime);
            s.sustainRate= p.bip (P::Env1SustainRate);
        }

        return s;
    }
}

void Voice::noteOn (int midiNote, float vel, float detuneCents, float panSpread)
{
    // Captured before anything below mutates state: true only when this voice
    // is already sounding a note (mono retrigger / legato), as opposed to a
    // fresh trigger from silence (first note of a phrase, or a free/stolen
    // voice in poly mode). Portamento/glissando should only glide in the
    // former case - otherwise a voice pulled from the pool would slide in
    // from whatever pitch it last happened to be at, which is the "random
    // pitch drift" the RandomPreset comment above works around.
    const bool wasSounding = held && env1.isActive();

    note = midiNote;
    velocity = vel;
    detune = detuneCents;
    pan = panSpread;
    held = true;

    const auto& p = *params;

    // Envelope triggering: the EnvsTriggering parameter packs three single/multi
    // flags into one value, bit 2 = env1, bit 1 = env2, bit 0 = env3.
    const int trig = p.raw (P::EnvsTriggering);
    const bool single1 = (trig & 0x4) != 0;
    const bool single2 = (trig & 0x2) != 0;
    const bool single3 = (trig & 0x1) != 0;

    // Single-trigger + already sounding = legato: the envelope reshapes from
    // its current level instead of snapping back to 0, so a mono line played
    // legato glides in pitch without re-plucking the amplitude envelope.
    env1.noteOn (envSettings (p, P::Env1Attack, false), vel, midiNote, ! single1 || ! wasSounding);
    env2.noteOn (envSettings (p, P::Env2Attack, true),  vel, midiNote, ! single2 || ! wasSounding);
    env3.noteOn (envSettings (p, P::Env3Attack, true),  vel, midiNote, ! single3 || ! wasSounding);

    lfo1.noteOn (p.raw (P::LFO1Trigger) == 1, p.raw (P::LFO1DelayTrigger) == 0);
    lfo2.noteOn (p.raw (P::LFO1Trigger + P::LfoStride) == 1,
                 p.raw (P::LFO1DelayTrigger + P::LfoStride) == 0);

    glideTarget = (float) midiNote;

    const int portTime  = p.raw (P::PortamentoTime);
    const int glideType = p.raw (P::GlideType);

    // GlideType: 0 Normal, 1 Auto, 2/3 = 2 semi down/up, 4/5 = 5 semi,
    // 6/7 = 7 semi, 8/9 = 12 semi. The fixed-interval modes are a "flick"
    // ornament - every note glides in from a set number of semitones away
    // from its own target, regardless of what was played before or whether
    // this is a legato retrigger.
    static const int fixedSemis[10] = { 0, 0, -2, 2, -5, 5, -7, 7, -12, 12 };

    if (portTime == 0)
    {
        glideCurrent = glideTarget;
    }
    else if (glideType >= 2 && glideType <= 9)
    {
        glideCurrent = glideTarget + (float) fixedSemis[glideType];
    }
    else if (glideType == 1 && ! wasSounding)
    {
        // Auto (fingered): only glide between overlapping/legato notes; a
        // fresh or staccato trigger snaps straight to pitch.
        glideCurrent = glideTarget;
    }
    // else: Normal (glideType == 0) - always glide from whatever pitch this
    // voice was last at, even across a fresh trigger. That's the traditional
    // hardware behaviour for "Normal" portamento; it reads as smooth on a
    // single mono line and as a characterful (if occasionally messy) slide
    // in poly - exactly what Auto mode exists to avoid.

    // Oscillator start phase: 0 means free running, anything else resets the
    // phase for a consistent attack transient.
    const int startPhase = p.raw (P::OscsStartPhase);
    if (startPhase > 0)
    {
        const float ph = (float) startPhase / 128.0f;
        for (auto& o : oscs) o.osc.setStartPhase (ph);
    }

    // Key Sync (per oscillator): even with the global start phase left free-
    // running, an individual oscillator with its own Key Sync on still resets
    // to phase 0 on every note - lets one oscillator stay punchy/consistent
    // while others keep drifting freely.
    for (int o = 0; o < 3; ++o)
    {
        if (startPhase > 0)
            continue; // already handled above, same result either way

        if (p.raw (P::Osc1KeySync + o * P::OscStride) > 0)
            oscs[o].osc.setStartPhase (0.0f);
    }

    driftPhase = rng.nextFloat();
}

void Voice::noteOff()
{
    held = false;

    // Constant Gate: the physical key-up is ignored for envelope purposes -
    // the gate stays open and the envelopes run their own course (Sustain
    // Time auto-release, AD Repeat looping) until they finish on their own.
    // Used for rhythmic/looping patches that shouldn't get cut short by how
    // long the key is actually held.
    if (params->raw (P::ConstantGate) != 0)
        return;

    env1.noteOff();
    env2.noteOff();
    env3.noteOff();
}

//==============================================================================
float Voice::glideTowardsTarget()
{
    const auto& p = *params;
    const int t = p.raw (P::PortamentoTime);

    if (t == 0)
    {
        glideCurrent = glideTarget;
        return glideCurrent;
    }

    const float seconds = 0.002f * std::pow (5000.0f, (float) t / 127.0f);
    const bool exponential = p.raw (P::PortamentoGlide) == 1;
    const float diff = glideTarget - glideCurrent;

    if (exponential)
    {
        const float a = 1.0f - std::exp (-1.0f / juce::jmax (1.0f, seconds * (float) sampleRate));
        glideCurrent += diff * a;
    }
    else
    {
        const float step = 1.0f / juce::jmax (1.0f, seconds * (float) sampleRate);
        const float delta = juce::jlimit (-std::abs (diff), std::abs (diff),
                                          (diff >= 0.0f ? 1.0f : -1.0f) * step * 24.0f);
        glideCurrent += delta;
    }

    // Glissando quantises the glide to semitones.
    if (p.raw (P::PortamentoType) == 1)
        return std::round (glideCurrent);

    return glideCurrent;
}

//==============================================================================
void Voice::renderNextSample (float& outL, float& outR, float inL, float inR, float& modTap)
{
    const auto& p = *params;

    // ---------------------------------------------------------------- modulators
    const float e1 = env1.process();
    const float e2 = env2.process();
    const float e3 = env3.process();

    const int l1Sync = p.raw (P::LFO1Sync);
    const float l1Base = l1Sync > 0 ? syncedLfoHz (l1Sync, hostBpm)
                                    : Lfo::speedToHz (p.raw (P::LFO1Speed), p.raw (P::LFO1Range));
    const float l1Speed = l1Base
                        * std::pow (2.0f, p.bip (P::LFO1SpeedEnv3) * e3 * 4.0f
                                        + p.bip (P::LFO1SpeedWheel) * modWheel * 4.0f
                                        + p.bip (P::LFO1SpeedAftertouch) * aftertouch * 4.0f);

    const int l2 = P::LfoStride;
    const int l2Sync = p.raw (P::LFO1Sync + l2);
    const float l2Base = l2Sync > 0 ? syncedLfoHz (l2Sync, hostBpm)
                                    : Lfo::speedToHz (p.raw (P::LFO1Speed + l2), p.raw (P::LFO1Range + l2));
    const float l2Speed = l2Base
                        * std::pow (2.0f, p.bip (P::LFO1SpeedEnv3 + l2) * e3 * 4.0f
                                        + p.bip (P::LFO1SpeedWheel + l2) * modWheel * 4.0f
                                        + p.bip (P::LFO1SpeedAftertouch + l2) * aftertouch * 4.0f);

    const float lfoA = lfo1.process (l1Speed, p.raw (P::LFO1Type),
                                     p.raw (P::LFO1Delay), p.raw (P::LFO1FadeMode) != 0,
                                     p.bip (P::LFO1Offset), p.uni (P::LFO1Soften));

    const float lfoB = lfo2.process (l2Speed, p.raw (P::LFO1Type + l2),
                                     p.raw (P::LFO1Delay + l2), p.raw (P::LFO1FadeMode + l2) != 0,
                                     p.bip (P::LFO1Offset + l2), p.uni (P::LFO1Soften + l2));

    // A slow random walk standing in for the analogue drift of the original.
    driftPhase += 0.6f / (float) sampleRate;
    if (driftPhase >= 1.0f) { driftPhase -= 1.0f; drift = rng.nextFloat() * 2.0f - 1.0f; }
    const float driftAmt = p.uni (P::VCODrift) * drift * 0.04f;

    const float baseNote = glideTowardsTarget() + pitchBend + detune * 0.01f;

    // ---------------------------------------------------------------- oscillators
    float oscOut[3] { 0.0f, 0.0f, 0.0f };
    float mixLevel[3] { 0.0f, 0.0f, 0.0f };

    for (int i = 0; i < 3; ++i)
    {
        const int b = P::Osc1Type + i * P::OscStride;
        const int o = i * P::OscStride;

        // --- pitch, in semitones
        float semis = (float) (p.raw (P::Osc1Octave + o) - 2) * 12.0f
                    + (float) (p.raw (P::Osc1Semitone + o) - 12)
                    + (float) (p.raw (P::Osc1FineTune + o) - 64) / 64.0f
                    + p.bip (P::Osc1PitchManual + o) * 12.0f;

        semis += p.bip (P::Osc1PitchEnv2 + o)  * e2      * 24.0f
               + p.bip (P::Osc1PitchEnv3 + o)  * e3      * 24.0f
               + p.bip (P::Osc1PitchLFO1 + o)  * lfoA    * 12.0f
               + p.bip (P::Osc1PitchLFO2 + o)  * lfoB    * 12.0f
               + p.bip (P::Osc1PitchWheel + o) * modWheel * 12.0f
               + driftAmt;

        const float hz = 440.0f * std::pow (2.0f, (baseNote + semis - 69.0f) / 12.0f);

        // --- pulse width
        float pw = p.uni (P::Osc1PulseWidth + o)
                 + p.bip (P::Osc1WidthEnv2 + o)  * e2
                 + p.bip (P::Osc1WidthEnv3 + o)  * e3
                 + p.bip (P::Osc1WidthLFO1 + o)  * lfoA
                 + p.bip (P::Osc1WidthLFO2 + o)  * lfoB
                 + p.bip (P::Osc1WidthWheel + o) * modWheel;
        pw = juce::jlimit (0.02f, 0.98f, pw);

        // --- sync
        float sync = p.uni (P::Osc1Sync + o)
                   + p.bip (P::Osc1SyncEnv2 + o)  * e2
                   + p.bip (P::Osc1SyncEnv3 + o)  * e3
                   + p.bip (P::Osc1SyncLFO1 + o)  * lfoA
                   + p.bip (P::Osc1SyncLFO2 + o)  * lfoB
                   + p.bip (P::Osc1SyncWheel + o) * modWheel;
        sync = juce::jlimit (0.0f, 1.0f, sync);

        // --- hardness / soften
        float soften = p.uni (P::Osc1Soften + o)
                     + p.bip (P::Osc1SoftenEnv2 + o)    * e2
                     + p.bip (P::Osc1HardnessEnv3 + o)  * e3
                     + p.bip (P::Osc1HardnessLFO1 + o)  * lfoA
                     + p.bip (P::Osc1HardnessLFO2 + o)  * lfoB
                     + p.bip (P::Osc1HardnessWheel + o) * modWheel;
        soften = juce::jlimit (0.0f, 1.0f, soften);

        const int type = p.raw (b);

        // An oscillator contributing nothing costs nothing: skip the whole
        // waveform and soften stage. Oscillator 3 still runs when a ring
        // modulator or FM needs it as a source.
        const bool feedsRingOrFm = (i == 2)
                                 && (p.raw (P::Ring1x3Level) > 0 || p.raw (P::Ring2x3Level) > 0
                                     || p.flag (P::FM1x3) || p.flag (P::FM2x3));

        if (p.raw (P::Osc1MixLevel + o) == 0 && ! feedsRingOrFm
             && p.raw (P::Osc1MixEnv2 + o) == 64 && p.raw (P::Osc1MixEnv3 + o) == 64
             && p.raw (P::Osc1MixLFO1 + o) == 64 && p.raw (P::Osc1MixLFO2 + o) == 64
             && p.raw (P::Osc1MixWheel + o) == 64)
        {
            oscOut[i] = 0.0f;
            mixLevel[i] = 0.0f;
            continue;
        }

        if (type == Oscillator::AudioIn1)
            oscOut[i] = inL;
        else if (type == Oscillator::AudioIn2)
            oscOut[i] = inR;
        else
            oscOut[i] = oscs[i].osc.process (hz, type, pw, sync,
                                             p.uni (P::Osc1SyncSkew + o),
                                             p.uni (P::Osc1FormantWidth + o),
                                             soften);

        oscs[i].currentHz = hz;

        // --- mix level
        float mix = p.uni (P::Osc1MixLevel + o)
                  + p.bip (P::Osc1MixEnv2 + o)  * e2
                  + p.bip (P::Osc1MixEnv3 + o)  * e3
                  + p.bip (P::Osc1MixLFO1 + o)  * lfoA
                  + p.bip (P::Osc1MixLFO2 + o)  * lfoB
                  + p.bip (P::Osc1MixWheel + o) * modWheel;

        mixLevel[i] = juce::jlimit (0.0f, 1.0f, mix);
    }

    // ---------------------------------------------------------------- noise
    const bool noiseUsed = p.raw (P::NoiseMixLevel) > 0
                        || p.raw (P::NoiseMixEnv2) != 64 || p.raw (P::NoiseMixEnv3) != 64
                        || p.raw (P::NoiseMixLFO1) != 64 || p.raw (P::NoiseMixLFO2) != 64
                        || p.raw (P::NoiseMixWheel) != 64
                        || p.flag (P::FMNoise);

    const float noiseOut = noiseUsed ? noise.process (p.uni (P::NoiseSoften)) : 0.0f;

    float noiseMix = p.uni (P::NoiseMixLevel)
                   + p.bip (P::NoiseMixEnv2)  * e2
                   + p.bip (P::NoiseMixEnv3)  * e3
                   + p.bip (P::NoiseMixLFO1)  * lfoA
                   + p.bip (P::NoiseMixLFO2)  * lfoB
                   + p.bip (P::NoiseMixWheel) * modWheel;
    noiseMix = juce::jlimit (0.0f, 1.0f, noiseMix);

    // --- FM: oscillators 1 and 2 (and, with FM Noise on, the noise source)
    // can phase-modulate oscillator 3
    if (p.flag (P::FM1x3) || p.flag (P::FM2x3) || p.flag (P::FMNoise))
    {
        const float fm = (p.flag (P::FM1x3)   ? oscOut[0] : 0.0f)
                       + (p.flag (P::FM2x3)   ? oscOut[1] : 0.0f)
                       + (p.flag (P::FMNoise) ? noiseOut  : 0.0f);
        oscOut[2] = std::tanh (oscOut[2] + fm * 0.7f);
    }


    // ---------------------------------------------------------------- ring mods
    float ring1 = p.uni (P::Ring1x3Level)
                + p.bip (P::Ring1x3Env2)  * e2
                + p.bip (P::Ring1x3Env3)  * e3
                + p.bip (P::Ring1x3LFO1)  * lfoA
                + p.bip (P::Ring1x3LFO2)  * lfoB
                + p.bip (P::Ring1x3Wheel) * modWheel;

    float ring2 = p.uni (P::Ring2x3Level)
                + p.bip (P::Ring2x3Env2)  * e2
                + p.bip (P::Ring2x3Env3)  * e3
                + p.bip (P::Ring2x3LFO1)  * lfoA
                + p.bip (P::Ring2x3LFO2)  * lfoB
                + p.bip (P::Ring2x3Wheel) * modWheel;

    ring1 = juce::jlimit (0.0f, 1.0f, ring1);
    ring2 = juce::jlimit (0.0f, 1.0f, ring2);

    // ------------------------------------------------- vocoder modulator tap
    // Taken here, before the filter and the amplifier, so the vocoder is fed
    // the raw oscillator rather than an already-shaped voice.
    switch (p.raw (P::VocoderModSource))
    {
        case 1:  modTap += oscOut[0]; break;
        case 2:  modTap += oscOut[1]; break;
        case 3:  modTap += oscOut[2]; break;
        case 4:  modTap += 0.7f * (oscOut[0] + oscOut[1]); break;
        case 5:  modTap += 0.7f * (oscOut[0] + oscOut[2]); break;
        case 6:  modTap += 0.7f * (oscOut[1] + oscOut[2]); break;
        case 7:  modTap += noiseOut; break;
        default: break;                      // 0 = Audio In, handled by the engine
    }

    // ---------------------------------------------------------------- mixer
    float mixed = oscOut[0] * mixLevel[0]
                + oscOut[1] * mixLevel[1]
                + oscOut[2] * mixLevel[2]
                + noiseOut  * noiseMix
                + oscOut[0] * oscOut[2] * ring1
                + oscOut[1] * oscOut[2] * ring2;

    const float sourceSum = mixLevel[0] + mixLevel[1] + mixLevel[2] + noiseMix
                          + ring1 + ring2;
    mixed *= 0.9f / juce::jmax (1.0f, sourceSum);

    // ---------------------------------------------------------------- filter
    float cutoffSemis = (float) p.raw (P::FilterCutoff) * (132.0f / 127.0f);

    cutoffSemis += p.bip (P::FilterFreqEnv2)  * e2   * 96.0f
                 + p.bip (P::FilterFreqEnv3)  * e3   * 96.0f
                 + p.bip (P::FilterFreqLFO1)  * lfoA * 48.0f
                 + p.bip (P::FilterFreqLFO2)  * lfoB * 48.0f
                 + p.bip (P::FilterFreqWheel) * modWheel * 48.0f
                 + p.bip (P::FilterFreqAftertouch) * aftertouch * 48.0f
                 + p.bip (P::FilterTracking) * (float) (note - 60);

    const float cutoffHz = juce::jlimit (15.0f, (float) sampleRate * 0.48f,
                                         8.1758f * std::pow (2.0f, cutoffSemis / 12.0f));

    float resonance = p.uni (P::FilterResonance)
                    + p.bip (P::FilterResEnv2)  * e2
                    + p.bip (P::FilterResEnv3)  * e3
                    + p.bip (P::FilterResLFO1)  * lfoA
                    + p.bip (P::FilterResLFO2)  * lfoB
                    + p.bip (P::FilterResWheel) * modWheel
                    + p.bip (P::FilterResAftertouch) * aftertouch;
    resonance = juce::jlimit (0.0f, 1.0f, resonance);

    float filtered = filter.process (mixed, cutoffHz, resonance,
                                     p.raw (P::FilterType), p.raw (P::FilterSlope),
                                     p.uni (P::FilterQNormalise), p.uni (P::FilterOverdrive),
                                     p.uni (P::FilterWidth), p.uni (P::FilterOverdriveCurve));

    // The oscillators can bypass the filter entirely, which the SN2 uses for
    // bright layered sounds where only the noise gets filtered.
    const float bypass = p.uni (P::FilterOscsBypass);
    if (bypass > 0.0f)
        filtered = filtered * (1.0f - bypass) + mixed * bypass;

    // ---------------------------------------------------------------- amp
    // The envelope already applies velocity, so it must not be applied again.
    const float sample = filtered * e1 * gainScale;

    const float a = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    outL = sample * std::cos (a);
    outR = sample * std::sin (a);
}

} // namespace aquanova
