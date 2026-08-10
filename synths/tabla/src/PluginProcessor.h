/*
  ==============================================================================

    PluginProcessor.h
    Physically-inspired virtual tabla — hybrid modal / resonator engine.

    Signal-flow overview (why each block exists):

      MIDI/UI strike
          -> Excitation model            (finite-duration contact pulse;
                                          models the finger/pad/palm contact,
                                          not an idealised impulse)
          -> Position-dependent gains    (where on the skin you strike decides
                                          which modes get energy)
          -> Modal membrane bank         (bank of 2-pole resonators = the
                                          vibrating skin; syahi loading pulls
                                          the low modes toward a harmonic
                                          1:2:3:4:5 series, which is what makes
                                          a tabla sound *pitched* not *drummy*)
          -> Tension nonlinearity        (instantaneous energy raises modal
                                          pitch slightly -> the characteristic
                                          attack "chirp" / pitch glide)
          -> Body + air resonators       (a few low-Q resonators colour the
                                          radiated sound: shell + enclosed air)
      Two drums (bayan = bass, dayan = treble) cross-couple a little
          -> Sympathetic ring
      Sum -> 2x-oversampled soft saturation (transient distortion / attack
                                             compression) -> stereo out

    Everything lives in these two .h/.cpp pairs so it drops straight into a
    Projucer project (add modules: juce_audio_basics, juce_audio_devices,
    juce_audio_formats, juce_audio_plugin_client, juce_audio_processors,
    juce_audio_utils, juce_core, juce_data_structures, juce_dsp,
    juce_events, juce_graphics, juce_gui_basics, juce_gui_extra).

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

//==============================================================================
namespace tabla
{
    constexpr int   kMaxDayanModes = 30;
    constexpr int   kMaxBayanModes = 20;
    constexpr float kTwoPi         = 6.28318530718f;

    // Parameter IDs -----------------------------------------------------------
    namespace pid
    {
        const juce::String tuning        = "tuning";        // dayan pitch, semitones
        const juce::String tension       = "tension";       // amplitude->pitch (nonlinear)
        const juce::String syahi         = "syahi";         // harmonic loading strength
        const juce::String size          = "size";          // membrane size (global pitch)
        const juce::String damping       = "damping";       // frequency-dependent damping
        const juce::String brightness    = "brightness";    // spectral tilt of excitation/modes
        const juce::String strikePos     = "strikepos";     // default radial strike position
        const juce::String hardness      = "hardness";      // excitation hardness -> saturation/noise
        const juce::String pressure      = "pressure";      // bayan heel pressure (bend)
        const juce::String bodyRes       = "bodyres";       // shell resonance amount
        const juce::String airRes        = "airres";        // air-cavity resonance amount
        const juce::String sympathetic   = "sympathetic";   // cross-drum ringing
        const juce::String bassCoupling  = "basscoupling";  // bayan -> dayan energy transfer
        const juce::String randomness    = "randomness";    // humanisation amount
        const juce::String articulation  = "articulation";  // contact-duration scaling
        const juce::String sustain       = "sustain";       // open-ring length multiplier
        const juce::String decay         = "decay";         // global decay multiplier
        const juce::String velResponse   = "velresponse";   // velocity curve shape
        const juce::String output        = "output";        // master gain
        const juce::String width         = "width";         // stereo diffusion/widening amount
    }

    //==========================================================================
    /** One strike request. All the expressive DNA of a stroke lives here. */
    struct StrikeParams
    {
        float position   = 0.5f;   // 0 = centre (syahi), 1 = rim  -> which modes ring
        float level      = 0.9f;   // velocity-scaled energy
        float hardness   = 0.5f;   // 0 soft pad ... 1 hard fingertip -> brightness + noise
        float durationMs = 6.0f;   // contact duration (longer = softer, less HF)
        float brightness = 0.5f;   // extra HF tilt for this stroke
        float muteAmount = 0.0f;   // 0 open ring ... 1 fully choked (na/te/ti)
        float decayScale = 1.0f;   // multiplies modal T60 after this strike
        float bendTarget = 0.0f;   // extra pitch offset (bayan slides), semitones
    };

    //==========================================================================
    /** Bank of 2-pole modal resonators = the vibrating membrane.

        Each mode is y[n] = a1 y[n-1] + a2 y[n-2] + b0 x[n], a pole pair at
        radius R (sets decay) and angle theta (sets frequency). This is modal
        synthesis: cheap, unconditionally stable for R<1, and lets us place
        each partial exactly where the loaded tabla skin wants it.            */
    class ModalBank
    {
    public:
        void prepare (double sampleRate, int maxModes)
        {
            sr = sampleRate;
            f0.assign   (maxModes, 0.0f);
            t60.assign  (maxModes, 0.5f);
            amp.assign  (maxModes, 0.0f);
            inGain.assign(maxModes, 0.0f);
            a1.assign   (maxModes, 0.0f);
            a2.assign   (maxModes, 0.0f);
            b0.assign   (maxModes, 0.0f);
            y1.assign   (maxModes, 0.0f);
            y2.assign   (maxModes, 0.0f);
            n = 0;
        }

        void setModeCount (int count) { n = juce::jmin (count, (int) f0.size()); }
        int  getModeCount() const     { return n; }

        void setMode (int i, float freq, float decayT60, float amplitude)
        {
            f0[i]  = freq;
            t60[i] = juce::jmax (0.02f, decayT60);
            amp[i] = amplitude;
        }

        /** Recompute pole coefficients from current pitch/decay multipliers.
            Called at control rate (every few dozen samples) so bends stay
            smooth without paying transcendental cost per sample.            */
        void updateCoeffs (float pitchMul, float decayMul)
        {
            for (int i = 0; i < n; ++i)
            {
                const float f = juce::jlimit (10.0f, (float) sr * 0.48f, f0[i] * pitchMul);
                const float R = std::exp (-6.90776f / (t60[i] * decayMul * (float) sr));
                const float theta = tabla::kTwoPi * f / (float) sr;
                a1[i] = 2.0f * R * std::cos (theta);
                a2[i] = -R * R;
                b0[i] = (1.0f - R) * amp[i];   // rough peak normalisation
            }
        }

        /** Per-mode spatial excitation weights from a radial strike position.
            Centre strikes (r->0) drive every mode -> full, boomy "open" tone.
            Rim strikes (r->1) selectively suppress modes -> thinner, brighter,
            edge-of-skin colour. A cheap stand-in for J_m(k r) mode shapes.   */
        void setInputGainsFromPosition (float r, float brightnessTilt)
        {
            for (int i = 0; i < n; ++i)
            {
                const float shape = std::abs (std::cos ((float) i * r * 1.9f));
                const float tilt  = 1.0f + brightnessTilt * (0.04f * (float) i);
                inGain[i] = juce::jlimit (0.0f, 4.0f, (0.35f + 0.65f * shape) * tilt);
            }
        }

        /** Inject one excitation sample and advance every resonator. */
        inline float process (float exc)
        {
            float out = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float y = b0[i] * inGain[i] * exc + a1[i] * y1[i] + a2[i] * y2[i];
                y2[i] = y1[i];
                y1[i] = y;
                out  += y;
            }
            return out;
        }

        /** Hand landing on a ringing skin: instantly bleed stored energy. */
        void chokeEnergy (float keep)
        {
            for (int i = 0; i < n; ++i) { y1[i] *= keep; y2[i] *= keep; }
        }

    private:
        double sr = 48000.0;
        int    n  = 0;
        std::vector<float> f0, t60, amp, inGain, a1, a2, b0, y1, y2;
    };

    //==========================================================================
    /** Low-Q peaking resonator for shell / air-cavity coloration. */
    class BodyResonator
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; reset(); }
        void reset() { z1 = z2 = 0.0f; }

        void set (float freq, float q, float gainDb)
        {
            const float w0 = tabla::kTwoPi * juce::jlimit (20.0f, (float) sr * 0.45f, freq) / (float) sr;
            const float cw = std::cos (w0), sw = std::sin (w0);
            const float alpha = sw / (2.0f * juce::jmax (0.1f, q));
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float b0 =  1.0f + alpha * A;
            const float b1 = -2.0f * cw;
            const float b2 =  1.0f - alpha * A;
            const float a0 =  1.0f + alpha / A;
            const float a1 = -2.0f * cw;
            const float a2 =  1.0f - alpha / A;
            nb0 = b0 / a0; nb1 = b1 / a0; nb2 = b2 / a0;
            na1 = a1 / a0; na2 = a2 / a0;
        }

        inline float process (float x)
        {
            const float y = nb0 * x + z1;
            z1 = nb1 * x - na1 * y + z2;
            z2 = nb2 * x - na2 * y;
            return y;
        }

    private:
        double sr = 48000.0;
        float nb0 = 1, nb1 = 0, nb2 = 0, na1 = 0, na2 = 0, z1 = 0, z2 = 0;
    };

    //==========================================================================
    /** Cheap Schroeder allpass — passes all frequencies equally but smears
        phase over `delayMs`. Used purely as a decorrelator here, not a tone
        shaper.                                                              */
    class Allpass
    {
    public:
        void prepare (double sampleRate, float delayMs, float gain)
        {
            len = juce::jmax (1, (int) (delayMs * 0.001f * sampleRate));
            buf.assign ((size_t) len, 0.0f);
            idx = 0;
            g = gain;
        }

        inline float process (float x)
        {
            const float bufOut = buf[(size_t) idx];
            const float y = -g * x + bufOut;
            buf[(size_t) idx] = x + g * bufOut;
            idx = (idx + 1) % len;
            return y;
        }

    private:
        std::vector<float> buf;
        int idx = 0, len = 1;
        float g = 0.55f;
    };

    /** Output-bus widener: each channel runs through its own two-stage
        allpass chain with mutually-prime-ish delay times, so correlated
        content (mainly the bayan bleeding into both channels via the pan
        law) decorrelates into a sense of space rather than just volume.
        A mid/side scale on top pushes width further at high `amount`.      */
    class StereoWidener
    {
    public:
        void prepare (double sampleRate)
        {
            lA.prepare (sampleRate, 11.7f, 0.55f);
            lB.prepare (sampleRate, 19.3f, 0.55f);
            rA.prepare (sampleRate, 13.9f, 0.55f);
            rB.prepare (sampleRate, 21.1f, 0.55f);
        }

        void process (float& L, float& R, float amount)
        {
            const float dl = lB.process (lA.process (L));
            const float dr = rB.process (rA.process (R));

            // Blend in the decorrelated signal by `amount` first...
            const float wl = L + amount * (dl - L);
            const float wr = R + amount * (dr - R);

            // ...then push the stereo image itself wider via mid/side.
            const float mid  = 0.5f * (wl + wr);
            const float side = 0.5f * (wl - wr) * (1.0f + amount * 1.2f);
            L = mid + side;
            R = mid - side;
        }

    private:
        Allpass lA, lB, rA, rB;
    };

    //==========================================================================
    /** One drum: excitation + modal skin + body/air coloration + dynamics. */
    class TablaDrum
    {
    public:
        enum class Type { Bayan, Dayan };

        void prepare (double sampleRate, Type t)
        {
            sr = sampleRate;
            type = t;
            const int maxModes = (t == Type::Dayan) ? kMaxDayanModes : kMaxBayanModes;
            bank.prepare (sampleRate, maxModes);
            for (auto& b : body) b.prepare (sampleRate);
            air.prepare (sampleRate);
            reset();
        }

        void reset()
        {
            excRemaining = 0;
            excDurSamps  = 1;
            excLevel = excHardness = 0.0f;
            excNoiseLP = 0.0f;
            energyEnv = 0.0f;
            pitchMul = pitchMulTarget = 1.0f;
            decayMul = decayMulTarget = 1.0f;
            bendSemis = bendSemisTarget = 0.0f;
            for (auto& b : body) b.reset();
            air.reset();
        }

        //-- Rebuild the modal table from user parameters (control rate) ------
        void rebuild (float tuningSemis, float syahi, float sizeMul,
                      float dampingAmt, float brightness,
                      float sustainMul, float decayGlobal)
        {
            const bool dayan = (type == Type::Dayan);

            // Ideal circular-membrane ratios (Bessel-zero based).
            static const float ideal[] =
            { 1.000f,1.593f,2.135f,2.295f,2.653f,2.917f,3.155f,3.500f,3.598f,3.652f,
              4.058f,4.230f,4.601f,4.831f,5.001f,5.412f,5.550f,5.911f,6.155f,6.501f,
              6.700f,7.010f,7.300f,7.600f,7.900f,8.200f,8.500f,8.800f,9.100f,9.400f };

            // Harmonic targets the syahi loading pulls the low modes toward.
            static const float harm[]  =
            { 1.0f,2.0f,3.0f,4.0f,5.0f,6.0f,7.0f,8.0f,9.0f };
            // How strongly each low mode is pulled (syahi mass acts most on lows).
            static const float pull[]  =
            { 1.0f,1.0f,0.9f,0.85f,0.8f,0.6f,0.4f,0.3f,0.2f };

            const int modes = dayan ? kMaxDayanModes : kMaxBayanModes;
            bank.setModeCount (modes);

            const float baseHz = (dayan ? 320.0f : 92.0f)
                               * std::pow (2.0f, tuningSemis / 12.0f)
                               / juce::jmax (0.5f, sizeMul);   // bigger skin = lower

            const float t60Low = (dayan ? 0.95f : 1.75f) * sustainMul * decayGlobal;

            for (int i = 0; i < modes; ++i)
            {
                float ratio = ideal[i];
                if (i < (int) (sizeof (harm) / sizeof (float)))
                {
                    const float s = syahi * (dayan ? 1.0f : 0.65f) * pull[i];
                    ratio = ideal[i] + (harm[i] - ideal[i]) * s;
                }

                const float freq = baseHz * ratio;

                // Frequency-dependent damping: higher modes die faster.
                float t60 = t60Low * std::pow (ratio, -0.7f * (0.6f + dampingAmt));

                // Syahi lengthens the harmonic (pitched) low modes, and gently
                // tames the messy high inharmonic ones.
                if (dayan)
                {
                    if (i < 6)  t60 *= (1.0f + 0.8f * syahi);
                    else        t60 *= (1.0f - 0.3f * syahi);
                }
                else
                {
                    // Same idea as the dayan: let syahi loading make the low,
                    // harmonically-locked modes (the ones we amplitude-boost
                    // below) outlast the upper inharmonic modes, so the ear
                    // settles on the bass pitch instead of the clang.
                    if (i < 4)  t60 *= (1.0f + 0.6f * syahi);
                    else        t60 *= (1.0f - 0.4f * syahi);
                }

                // Amplitude: lower modes louder; brightness tilts HF up.
                float a = std::pow (0.74f, (float) i) * (1.0f + brightness * 0.05f * (float) i);
                if (!dayan && i < 4) a *= 1.6f;               // bayan low-end weight

                bank.setMode (i, freq, juce::jmax (0.03f, t60), a);
            }

            // Body (shell) + air-cavity resonators.
            if (dayan)
            {
                body[0].set (baseHz * 0.5f, 4.0f, 4.0f);
                body[1].set (baseHz * 2.7f, 6.0f, 3.0f);
                air.set     (baseHz * 1.3f, 3.0f, 2.5f);
            }
            else
            {
                body[0].set (baseHz * 0.8f, 3.0f, 5.0f);
                body[1].set (baseHz * 1.9f, 5.0f, 3.5f);
                air.set     (baseHz * 0.6f, 2.5f, 4.0f);
            }
        }

        //-- Trigger a stroke -------------------------------------------------
        void strike (const StrikeParams& s, float brightnessTilt, float artic,
                     juce::Random& rng, float randomness)
        {
            const float jitter = 1.0f + randomness * (rng.nextFloat() - 0.5f) * 0.15f;

            // Per-hit humanisation: real players never land on exactly the same
            // spot with exactly the same contact each time. Nudge position and
            // hardness independently (in addition to the level/duration jitter
            // above) so repeated identical bols don't sound machine-stamped.
            const float posJitter  = randomness * (rng.nextFloat() - 0.5f) * 0.08f;  // +/-4% of radius, at max randomness
            const float hardJitter = randomness * (rng.nextFloat() - 0.5f) * 0.12f;
            const float decayJitterMul = 1.0f + randomness * (rng.nextFloat() - 0.5f) * 0.15f;

            const float pos  = juce::jlimit (0.0f, 1.0f, s.position + posJitter);
            const float hard = juce::jlimit (0.0f, 1.0f, s.hardness + hardJitter);

            bank.setInputGainsFromPosition (pos, brightnessTilt + (s.brightness - 0.5f));

            // Harder contact = stiffer, shorter contact time (crude Hertzian-contact
            // stand-in: duration shrinks with hardness rather than staying fixed).
            const float hardnessDurMul = 1.0f - 0.3f * hard;

            excDurSamps  = juce::jmax (1, (int) (s.durationMs * artic * jitter * hardnessDurMul
                                                  * 0.001f * (float) sr));
            excRemaining = excDurSamps;
            excLevel     = s.level * jitter;
            excHardness  = hard;

            // Muted stroke: choke existing ring + shorten future decay.
            if (s.muteAmount > 0.0f)
                bank.chokeEnergy (1.0f - 0.6f * s.muteAmount);

            decayMulTarget = juce::jlimit (0.06f, 3.0f, s.decayScale * decayJitterMul);
            bendSemisTarget = s.bendTarget;
        }

        void setPressureBend (float semis)   { bendSemisTarget = semis; }
        void setDecayHold     (float mul)     { decayMulTarget  = juce::jlimit (0.06f, 3.0f, mul); }

        //-- Per-sample audio -------------------------------------------------
        inline float process (float externalDrive, float tensionAmt,
                              float bodyAmt, float airAmt, juce::Random& rng)
        {
            // 1) excitation sample: shaped contact pulse + hardness-scaled noise
            float exc = externalDrive;
            if (excRemaining > 0)
            {
                const float t   = 1.0f - (float) excRemaining / (float) excDurSamps;
                const float env = 0.5f - 0.5f * std::cos (tabla::kTwoPi * t); // raised cosine
                const float click = (excRemaining == excDurSamps) ? 1.0f : 0.0f;
                const float noise = (rng.nextFloat() * 2.0f - 1.0f);

                // Colour the contact noise by hardness rather than only scaling its
                // level: a soft pad/palm smears the noise burst toward low frequencies
                // (fingers/pads damp the high end of the contact), a hard fingertip/nail
                // leaves it full-band. One cheap one-pole tracker, no extra filter object.
                excNoiseLP += 0.25f * (noise - excNoiseLP);
                const float colouredNoise = excNoiseLP + excHardness * (noise - excNoiseLP);

                exc += excLevel * env * (0.6f * click + (0.5f + 0.8f * excHardness) * colouredNoise);
                --excRemaining;
            }

            // 2) tension nonlinearity: energy raises pitch a touch (attack chirp)
            pitchMulTarget = std::pow (2.0f, bendSemis / 12.0f) * (1.0f + tensionAmt * energyEnv);

            // 3) modal skin
            float y = bank.process (exc);

            // 4) coloration
            const float col = body[0].process (y) + body[1].process (y);
            y += bodyAmt * col * 0.5f + airAmt * air.process (y) * 0.5f;

            // energy tracker for nonlinearity
            energyEnv += 0.002f * (std::abs (y) - energyEnv);
            return y;
        }

        /** Control-rate smoothing of pitch/decay/bend + coeff refresh. */
        void updateControl()
        {
            const float sm = 0.2f;
            decayMul  += sm * (decayMulTarget  - decayMul);
            bendSemis += sm * (bendSemisTarget - bendSemis);
            pitchMul  += sm * (pitchMulTarget  - pitchMul);
            bank.updateCoeffs (pitchMul, decayMul);
        }

        Type getType() const { return type; }

    private:
        double sr = 48000.0;
        Type   type = Type::Dayan;
        ModalBank bank;
        std::array<BodyResonator, 2> body;
        BodyResonator air;

        int   excRemaining = 0, excDurSamps = 1;
        float excLevel = 0, excHardness = 0;
        float excNoiseLP = 0.0f;   // one-pole tracker used to colour the contact noise by hardness
        float energyEnv = 0;
        float pitchMul = 1, pitchMulTarget = 1;
        float decayMul = 1, decayMulTarget = 1;
        float bendSemis = 0, bendSemisTarget = 0;
    };

    //==========================================================================
    /** Which drum, which stroke shape — the "orchestration" layer that turns
        the two physical drums into named bols. */
    enum class Bol { Na, Ta, Tin, Tun, Te, Ti, Ge, Ghe, Dha, Dhin, Dhage, Tirakita, NumBols };

    struct SubStroke { int drum; StrikeParams p; float timeMs; };

    /** Expand a bol into one or more time-offset sub-strokes.
        drum: 0 = bayan (bass), 1 = dayan (treble).                          */
    inline std::vector<SubStroke> bolToStrokes (Bol b, float vel)
    {
        auto D = [] (float pos, float lvl, float hard, float dur, float bright,
                     float mute, float decay) -> StrikeParams
        {
            StrikeParams p; p.position = pos; p.level = lvl; p.hardness = hard;
            p.durationMs = dur; p.brightness = bright; p.muteAmount = mute;
            p.decayScale = decay; return p;
        };

        std::vector<SubStroke> out;
        switch (b)
        {
            // --- Dayan strokes ---
            case Bol::Na:   out.push_back ({1, D(0.92f, vel, 0.7f, 4.0f, 0.7f, 0.6f, 0.5f),  0}); break; // rim, ringing edge
            case Bol::Ta:   out.push_back ({1, D(0.80f, vel, 0.8f, 3.5f, 0.8f, 0.2f, 0.8f),  0}); break; // open rim tone
            case Bol::Tin:  out.push_back ({1, D(0.55f, vel, 0.6f, 5.0f, 0.5f, 0.15f,1.1f),  0}); break; // between syahi/rim, ringing
            case Bol::Tun:  out.push_back ({1, D(0.05f, vel, 0.5f, 6.0f, 0.3f, 0.0f, 1.4f),  0}); break; // centre, full resonant "toom"
            case Bol::Te:   out.push_back ({1, D(0.45f, vel, 0.9f, 2.5f, 0.9f, 0.9f, 0.18f), 0}); break; // flat slap, dead
            case Bol::Ti:   out.push_back ({1, D(0.35f, vel, 0.85f,2.5f, 0.85f,0.85f,0.2f),  0}); break; // one-finger dead
            // --- Bayan strokes ---
            case Bol::Ge:   out.push_back ({0, D(0.30f, vel, 0.5f, 7.0f, 0.3f, 0.0f, 1.3f),  0}); break; // open bass ring
            case Bol::Ghe:  out.push_back ({0, D(0.30f, vel, 0.5f, 7.0f, 0.3f, 0.0f, 1.3f),  0}); break; // (alias of ge)
            // --- Combined (both hands) ---
            case Bol::Dha:  out.push_back ({0, D(0.30f, vel, 0.5f, 7.0f, 0.3f, 0.0f, 1.3f),  0});
                            out.push_back ({1, D(0.80f, vel, 0.8f, 3.5f, 0.8f, 0.2f, 0.8f),  0}); break; // ge + na
            case Bol::Dhin: out.push_back ({0, D(0.30f, vel, 0.5f, 7.0f, 0.3f, 0.0f, 1.3f),  0});
                            out.push_back ({1, D(0.55f, vel, 0.6f, 5.0f, 0.5f, 0.15f,1.1f),  0}); break; // ge + tin
            case Bol::Dhage:out.push_back ({0, D(0.30f, vel, 0.5f, 7.0f, 0.3f, 0.0f, 1.3f),  0});
                            out.push_back ({1, D(0.80f, vel, 0.8f, 3.5f, 0.8f, 0.2f, 0.8f),  0});
                            out.push_back ({0, D(0.30f, vel*0.9f,0.5f,7.0f,0.3f, 0.0f, 1.3f),90}); break; // dha then ge
            // --- Compound roll ---
            case Bol::Tirakita:
                            out.push_back ({1, D(0.40f, vel,      0.85f,2.2f,0.85f,0.8f,0.2f),  0});   // ti
                            out.push_back ({1, D(0.85f, vel*0.85f,0.8f, 2.5f,0.8f, 0.3f,0.6f), 55});   // ra (na-ish)
                            out.push_back ({0, D(0.30f, vel*0.9f, 0.5f, 6.0f,0.3f, 0.0f,1.2f),110});   // ki (ge)
                            out.push_back ({1, D(0.80f, vel*0.8f, 0.8f, 2.8f,0.8f, 0.3f,0.7f),165});   // ta
                            break;
            default: break;
        }
        return out;
    }
} // namespace tabla

//==============================================================================
class TablaAudioProcessor : public juce::AudioProcessor
{
public:
    TablaAudioProcessor();
    ~TablaAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TablaSynth"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //-- Called from the editor when the user clicks a drum -------------------
    void triggerUIStrike (int drum, float radius, float velocity);
    //-- Called from the editor while dragging in slide mode (semitones) ------
    void setSlideBend (int drum, float semitones);
    //-- Editor reads these for the strike-flash animation -------------------
    std::atomic<float> bayanFlash { 0.0f }, dayanFlash { 0.0f };

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int kFirstBolNote = 60;   // C3 = first bol

private:
    void triggerBol (tabla::Bol b, float velocity, int sampleOffset);
    float applyVelCurve (float v) const;

    double sr = 48000.0;
    tabla::TablaDrum bayan, dayan;
    juce::Random rng;

    // Scheduled sub-strokes (bols + rolls) with sample timestamps.
    struct Scheduled { int drum; tabla::StrikeParams p; juce::int64 when; };
    std::vector<Scheduled> schedule;
    juce::int64 samplePos = 0;

    // Lock-free UI-strike queue (message thread -> audio thread).
    struct UIStrike { int drum; float radius; float vel; };
    juce::AbstractFifo uiFifo { 128 };
    std::array<UIStrike, 128> uiBuf;

    // Nonlinear stage: 2x oversampled soft saturation.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    std::atomic<float> slideBendBayan { 0.0f }, slideBendDayan { 0.0f };

    float sympSmoothed = 0.0f;
    juce::LinearSmoothedValue<float> outGain;
    tabla::StereoWidener widener;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TablaAudioProcessor)
};
