/*
  ==============================================================================

    PluginProcessor.h
    Soundscape Ecology — a procedural nature synthesizer.

    Structure follows Krause / Pijanowski soundscape ecology: every sound in a
    landscape belongs to one of three source classes, and the plugin has one
    slot for each.

        SLOT I   BIOPHONY      sounds made by living organisms
        SLOT II  GEOPHONY      non-biological natural sound
        SLOT III ANTHROPHONY   human-generated sound

    Signal flow (why each block exists):

      Per slot:
        Articulation layer      (rate / phrase / jitter / swing / attack /
                                 decay — the RHYTHM, kept deliberately separate
                                 from the physics, because species identity in
                                 real bioacoustics lives in the rhythm rather
                                 than the spectrum: Kanda 1960 found Japanese
                                 cicada spectra overlap heavily and it is the
                                 pulse pattern that distinguishes species)
            -> Generator        (one of three engines, see below)
            -> Field            (population scatter: N individuals at N
                                 distances, weakly coupled so choruses
                                 partially synchronise; distance sets 1/d gain,
                                 propagation delay and HF air absorption)
      Sum of slots
            -> Niche allocator  (Krause's acoustic niche hypothesis: nudge each
                                 slot's carrier into unoccupied spectral space
                                 so the mix reads as a healthy habitat)
            -> Habitat          (FDN ambience — forest sound is diffusion and
                                 absorption, not tail)
            -> Output

    Three generator engines cover everything:

      ENGINE A  stochastic excitation -> resonant body
                crickets, katydids, cicadas, frogs, rain, surf, fire, metal
      ENGINE B  nonlinear oscillator + vocal tract
                birds and owls; controlled by a closed loop drawn in the
                (air-sac pressure, labial tension) plane, after Mindlin
      ENGINE C  turbulence + aeolian resonance
                wind, breath, tyre-road noise

    Model parameters were fitted to reference recordings. Notable measured
    values are cited inline at the point of use.

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
#include <algorithm>   // std::sort, for the alphabetical preset ordering

namespace soundeco
{
    constexpr int   kNumSlots = 3;
    constexpr int   kMaxIndividuals = 48;
    constexpr int   kMaxModes = 96;
    constexpr int   kNumModelKnobs = 8;   // physics knobs per slot
    constexpr int   kDrawPoints = 64;  // resolution of the draw canvases
    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kSpeedOfSound = 343.0f;
    constexpr float MathPi = 3.14159265358979323846f;

    //==========================================================================
    // Model catalogue. Each slot exposes only the models of its own class.
    //==========================================================================
    // PortedBurst is a fourth engine, deliberately separate from the other
    // three: it shares no code or state with the ModalBank / Resonator
    // machinery (StochasticModal's "harp, mirror, tymbal, plate" bank). It
    // exists for models that build their texture entirely from independent
    // Butterworth-filtered noise layers rather than an approximation built
    // from the plugin's general-purpose resonant body. See ButterBandpass
    // below and the "Thunder" model in kGeophony.
    enum class Engine { StochasticModal, NonlinearVocal, TurbulentAeolian, PortedBurst };

    struct ModelDesc
    {
        const char* name;
        Engine      engine;
        // labels for the eight physics knobs; nullptr hides the knob
        std::array<const char*, kNumModelKnobs> knobs;
    };

    // ---- SLOT I : BIOPHONY --------------------------------------------------
    static const ModelDesc kBiophony[] =
    {
        { "Cricket",    Engine::StochasticModal,
          { "Carrier", "Harp Q", "Tooth Jitter", "File Taper",
            "Strike Tune", "Pulses", "Body", "Radiation" } },
        { "Katydid",    Engine::StochasticModal,
          { "Carrier", "Mirror Q", "Tooth Jitter", "File Taper",
            "Strike Tune", "Pulses", "Body", "Radiation" } },
        { "Cicada",     Engine::StochasticModal,
          { "Tymbal Freq", "Tymbal Q", "Ribs", "Muscle Rate",
            "Rate Drift", "Air Sac", "Second Band", "Bell" } },
        { "Frog",       Engine::StochasticModal,
          { "Larynx", "Sac Inflate", "Pulse Rate", "Sac Q",
            "Rasp", "Pulses", "Body", "Radiation" } },
        { "Songbird",   Engine::NonlinearVocal,
          { "Low F", "High F", "Gesture Phase", "Wander",
            "Beak Open", "Trachea", "Biphonation", "Breath" } },
        { "Owl",        Engine::NonlinearVocal,
          { "F0", "Tremulant", "Trem Depth", "Trem Onset",
            "2nd Harm", "Pitch Rise", "Rise Time", "Breath" } },
            // Appended so existing model indices, and therefore every stored
            // preset, keep pointing at the same models.
            { "Anuran",     Engine::StochasticModal,
              { "Fold Rate", "Pulse Rate", "Dominant", "Radiator",
                "Fibrous Mass", "Drive", "Open Quotient", "FM Sweep" } },
    };

    // ---- SLOT II : GEOPHONY -------------------------------------------------
    static const ModelDesc kGeophony[] =
    {
        { "Rain",       Engine::StochasticModal,
          { "Drop Size", "Size Spread", "Bubble Mix", "Impact",
            "Canopy", "Density", "Bed", "Chirp" } },
        { "Surf",       Engine::StochasticModal,
          { "Bubble Size", "Size Spread", "Swell Rate", "Swell Depth",
            "Roar", "Density", "Break", "Chirp" } },
        { "Stream",     Engine::StochasticModal,
          { "Bubble Size", "Size Spread", "Flow", "Turbulence",
            "Roar", "Density", "Gravel", "Chirp" } },
        { "Wind",       Engine::TurbulentAeolian,
          { "Speed", "Gustiness", "Obstacle", "Spread",
            "Aeolian Q", "Turbulence", "Hiss", "Tilt" } },
        { "Fire",       Engine::StochasticModal,
          { "Crackle Size", "Size Spread", "Crackle Rate", "Body",
            "Rumble", "Density", "Hiss", "Spit" } },
            // Appended deliberately: exact ports of the reference rain and surf.
            // They go at the END so the existing model indices — and therefore
            // every stored preset — keep pointing at the same models.
            { "Rainfall",   Engine::StochasticModal,
              { "Drop Size", "Size Spread", "Bubble Mix", "Click",
                "Canopy", "Density", "Bed", "Chirp" } },
            { "Ocean Surf", Engine::StochasticModal,
              { "Bubble Size", "Size Spread", "Swell Rate", "Swell Depth",
                "Roar", "Density", "Break", "Chirp" } },
                // Rain landing on something that RINGS. Rain on a tin roof, a tent, a
                // car bonnet, a window — each drop is a small impulse into a thin
                // resonant panel, and it is the panel that gives the sound its
                // character rather than the water.
                { "Rain Surface", Engine::StochasticModal,
                  { "Drop Size", "Size Spread", "Surface Pitch", "Impact",
                    "Surface Decay", "Density", "Bed", "Inharmonic" } },
                    // A distant strike: a quiet pre-strike murmur whose tremolo closes in
                    // on the strike, a crack built from low thumps plus ONE shared noise
                    // burst duplicated through several band-pass copies (so it reads as
                    // one textured crack rather than a pile of little claps), and a long
                    // multi-band rumbling tail with a handful of filtered re-arrival
                    // swells. Runs on the StochasticModal engine, routing its excitation
                    // through the shared ModalBank/Resonator body used for insects, frogs
                    // and struck metal. Appended at the end for the same reason as
                    // Rainfall/Ocean Surf/Rain Surface above: existing model indices, and
                    // therefore stored presets, keep pointing at the same models.
                    { "Rumble",    Engine::StochasticModal,
                      { "Strike Position", "Rumble Band", "Approach", "Punch",
                        "Crack Spread", "Crackle Decay", "Tail Decay", "Rearrival" } },
                        // A second, independent strike model: the same three-stage shape as
                        // Rumble above (murmur, crack, rumbling tail with re-arrival swells),
                        // but built entirely from independent Butterworth-filtered noise
                        // layers instead of the shared ModalBank/Resonator body. Runs on the
                        // PortedBurst engine (see generateSample / triggerEvent) and touches
                        // no Resonator/ModalBank/bank/aux state. Same eight-knob layout and
                        // labels as "Rumble" above, so it slots into the GUI with zero editor
                        // changes. Appended at the end, per the append-only rule, so existing
                        // presets keep pointing at the same models.
                        { "Thunder", Engine::PortedBurst,
                          { "Strike Position", "Rumble Band", "Approach", "Punch",
                            "Crack Spread", "Crackle Decay", "Tail Decay", "Rearrival" } },
    };

    // ---- SLOT III : ANTHROPHONY --------------------------------------------
    static const ModelDesc kAnthrophony[] =
    {
        { "Distant Road", Engine::TurbulentAeolian,
          { "Speed", "Traffic", "Tyre Tone", "Ground Comb",
            "Comb Depth", "Engine", "Distance", "Tilt" } },
        { "Plate Shear",  Engine::StochasticModal,
          { "Plate F0", "Aspect", "Drive", "Roughness",
            "Mode Split", "Decay", "Decay Spread", "Sub Rumble" } },
        { "Plate Impact", Engine::StochasticModal,
          { "Plate F0", "Aspect", "Hardness", "Density",
            "Mode Split", "Decay", "Decay Spread", "Brightness" } },
        { "Metal Bar",    Engine::StochasticModal,
          { "Pitch", "Strike Hard", "Density", "Beating",
            "Mode Split", "Decay", "Decay Spread", "Brightness" } },
        { "Bell",         Engine::StochasticModal,
          { "Pitch", "Inharmonic", "Hardness", "Strike Pos",
            "Mode Split", "Decay", "Decay Spread", "Brightness" } },
        { "Machinery",    Engine::StochasticModal,
          { "Housing", "Stretch", "Tone", "Clatter",
            "Resonance", "Rate", "Hiss", "Load" } },
    };

    // Any slot can hold any class. The three columns default to one class each
    // because that is the useful starting layout, but nothing depends on it —
    // three biophony slots is a legitimate scene.
    enum class Phony { Biophony = 0, Geophony = 1, Anthrophony = 2 };
    constexpr int kNumPhony = 3;
    // Must be >= the largest of the three model arrays above. NOTE: this was
    // 8 while kGeophony already held 9 entries (through "Thunder"), which
    // silently clamped the model-index parameter's range to 0..7 and made
    // "Rumble" unreachable from automation/host recall even though it
    // appeared fine in the combo box locally. Raised to 10 to fit the new
    // "Thunder" entry (index 9) as well as fix that latent bug.
    constexpr int kMaxModelsPerClass = 10;

    inline const ModelDesc* modelsForClass(int phony, int& count)
    {
        switch (phony)
        {
        case 0:  count = (int)std::size(kBiophony);    return kBiophony;
        case 1:  count = (int)std::size(kGeophony);    return kGeophony;
        default: count = (int)std::size(kAnthrophony); return kAnthrophony;
        }
    }

    inline const char* phonyName(int phony)
    {
        switch (phony) {
        case 0: return "Biophony"; case 1: return "Geophony";
        default: return "Anthrophony";
        }
    }

    inline const char* phonyDescription(int phony)
    {
        switch (phony)
        {
        case 0:  return "sounds made by living organisms";
        case 1:  return "non-biological natural sound";
        default: return "human-generated sound";
        }
    }

    //==========================================================================
    // Parameter IDs
    //==========================================================================
    namespace pid
    {
        // per slot, suffixed with the slot index
        inline juce::String model(int s) { return "s" + juce::String(s) + "_model"; }
        inline juce::String phony(int s) { return "s" + juce::String(s) + "_phony"; }
        inline juce::String enabled(int s) { return "s" + juce::String(s) + "_on"; }
        inline juce::String level(int s) { return "s" + juce::String(s) + "_level"; }
        inline juce::String knob(int s, int k) { return "s" + juce::String(s) + "_k" + juce::String(k); }

        // articulation layer — the RHYTHM, independent of the physics
        inline juce::String rate(int s) { return "s" + juce::String(s) + "_rate"; }
        inline juce::String phrase(int s) { return "s" + juce::String(s) + "_phrase"; }
        inline juce::String jitter(int s) { return "s" + juce::String(s) + "_jitter"; }
        // Was "Swing" and never read by the DSP. Now HOLD: the fraction of an
        // event that sustains before the syllable train begins. This is what
        // minmin-zemi needs — a long held "miiin" then "min min min min".
        inline juce::String swing(int s) { return "s" + juce::String(s) + "_swing"; }
        inline juce::String rhythm(int s) { return "s" + juce::String(s) + "_rhythm"; }
        inline juce::String attack(int s) { return "s" + juce::String(s) + "_attack"; }
        inline juce::String decay(int s) { return "s" + juce::String(s) + "_decay"; }
        inline juce::String duration(int s) { return "s" + juce::String(s) + "_dur"; }

        // the Field — population and space
        inline juce::String population(int s) { return "s" + juce::String(s) + "_pop"; }
        inline juce::String spread(int s) { return "s" + juce::String(s) + "_spread"; }
        inline juce::String distance(int s) { return "s" + juce::String(s) + "_dist"; }
        inline juce::String diversity(int s) { return "s" + juce::String(s) + "_div"; }
        inline juce::String synchrony(int s) { return "s" + juce::String(s) + "_sync"; }
        inline juce::String parallax(int s) { return "s" + juce::String(s) + "_para"; }
        // Height above ground, in metres. Sets the ground-reflection comb and
        // the elevation timbre cue.
        inline juce::String elevation(int s) { return "s" + juce::String(s) + "_elev"; }

        // draw canvases
        inline juce::String drawPitchOn(int s) { return "s" + juce::String(s) + "_drawp"; }
        inline juce::String drawAmpOn(int s) { return "s" + juce::String(s) + "_drawa"; }
        inline juce::String drawDepth(int s) { return "s" + juce::String(s) + "_drawd"; }

        // global
        static const juce::String temperature = "temperature";  // deg C — Dolbear
        static const juce::String humidity = "humidity";     // air absorption
        static const juce::String timeOfDay = "timeofday";    // 0..1 preset morph
        static const juce::String niche = "niche";        // acoustic niche allocator
        // How strongly the weather couples to everything else.
        static const juce::String weather = "weather";
        static const juce::String habSize = "habsize";
        static const juce::String habDamping = "habdamping";
        static const juce::String habMix = "habmix";
        static const juce::String midiMode = "midimode";
        static const juce::String midiAttack = "midiattack";
        static const juce::String midiRelease = "midirelease";
        static const juce::String midiVelSens = "midivel";
        static const juce::String midiRoot = "midiroot";
        static const juce::String midiPitch = "midipitchamt";
        static const juce::String output = "output";
        static const juce::String width = "width";
    }

    //==========================================================================
    // DSP building blocks
    //==========================================================================

    // Two-pole resonator (constant skirt bandpass). The single most reused
    // block in the plugin: harps, mirrors, tymbals, air sacs, plates.
    struct Resonator
    {
        float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        float gain = 1.0f;

        void set(float freq, float q, double sr) noexcept
        {
            freq = juce::jlimit(10.0f, (float)sr * 0.47f, freq);
            q = juce::jmax(0.5f, q);
            const float w = kTwoPi * freq / (float)sr;
            const float alpha = std::sin(w) / (2.0f * q);
            const float norm = 1.0f / (1.0f + alpha);
            b0 = q * alpha * norm;
            a1 = -2.0f * std::cos(w) * norm;
            a2 = (1.0f - alpha) * norm;
        }

        inline float process(float x) noexcept
        {
            // transposed direct form II, bandpass (b1 = 0, b2 = -b0)
            const float y = b0 * x + z1;
            z1 = z2 - a1 * y;
            z2 = -b0 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    //==========================================================================
    // Butterworth filters — used ONLY by the PortedBurst engine below.
    //
    // These implement a real 2-pole Butterworth low-pass / high-pass
    // (bilinear-transformed analogue prototype, maximally flat passband).
    // This is a genuinely different filter TYPE from
    // Resonator above — Resonator is a constant-skirt bandpass tuned for a
    // resonant body (a harp, a mirror, a tymbal) and rings at its own Q;
    // a Butterworth pair has a flat passband and falls off smoothly at the
    // edges, which is what makes a band of filtered noise sound "broad and
    // smooth" rather than "ringing". Deliberately kept separate from
    // ModalBank/Resonator so the PortedBurst engine shares no DSP code with
    // the modal engine.
    //==========================================================================
    struct Butter2LP
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void set(float fc, double sr) noexcept
        {
            fc = juce::jlimit(1.0f, (float)sr * 0.49f, fc);
            const float w0 = std::tan(MathPi * fc / (float)sr);   // bilinear pre-warp
            const float w0sq = w0 * w0;
            const float k = 1.41421356f;                          // sqrt(2): Butterworth Q
            const float norm = 1.0f / (1.0f + k * w0 + w0sq);
            b0 = w0sq * norm;
            b1 = 2.0f * b0;
            b2 = b0;
            a1 = 2.0f * (w0sq - 1.0f) * norm;
            a2 = (1.0f - k * w0 + w0sq) * norm;
        }

        inline float process(float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    struct Butter2HP
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        void set(float fc, double sr) noexcept
        {
            fc = juce::jlimit(1.0f, (float)sr * 0.49f, fc);
            const float w0 = std::tan(MathPi * fc / (float)sr);
            const float w0sq = w0 * w0;
            const float k = 1.41421356f;
            const float norm = 1.0f / (1.0f + k * w0 + w0sq);
            b0 = norm;
            b1 = -2.0f * b0;
            b2 = b0;
            a1 = 2.0f * (w0sq - 1.0f) * norm;
            a2 = (1.0f - k * w0 + w0sq) * norm;
        }

        inline float process(float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    // A bandpass built from a Butterworth low-pass in series with a
    // Butterworth high-pass. Self-contained; shares nothing with
    // Resonator / ModalBank / bank / aux.
    struct ButterBandpass
    {
        Butter2HP hp;
        Butter2LP lp;

        void set(float loHz, float hiHz, double sr) noexcept
        {
            hp.set(juce::jmax(1.0f, loHz), sr);
            lp.set(juce::jmin((float)sr * 0.49f - 10.0f, hiHz), sr);
        }

        inline float process(float x) noexcept { return lp.process(hp.process(x)); }
        void reset() noexcept { hp.reset(); lp.reset(); }
    };

    struct OnePole
    {
        float a = 0.0f, z = 0.0f;
        void setCutoff(float fc, double sr) noexcept
        {
            a = std::exp(-kTwoPi * juce::jlimit(1.0f, (float)sr * 0.49f, fc) / (float)sr);
        }
        inline float lp(float x) noexcept { z = (1.0f - a) * x + a * z; return z; }
        inline float hp(float x) noexcept { return x - lp(x); }
        void reset() noexcept { z = 0.0f; }
    };

    // A bank of resonators standing in for a vibrating body.
    struct ModalBank
    {
        struct Mode { Resonator r; float gain = 0.0f; bool active = false; };
        std::array<Mode, kMaxModes> modes;
        int numModes = 0;

        void clear() noexcept { numModes = 0; for (auto& m : modes) { m.r.reset(); m.active = false; } }

        void add(float freq, float q, float gain, double sr, bool coherent = true) noexcept
        {
            if (numModes >= kMaxModes || freq >= sr * 0.47) return;
            auto& m = modes[(size_t)numModes++];
            m.r.set(freq, q, sr);
            // A constant-skirt bandpass has PEAK GAIN EQUAL TO Q at STEADY
            // STATE — i.e. when driven continuously at its own resonance.
            // Models that repeatedly strike a mode at its own rate (crickets,
            // cicadas: the tooth/rib rate IS the resonance) build toward that
            // steady-state gain and scream without compensation. Measured
            // across Q 14..140: 1/sqrt(Q) tames that coherent buildup while
            // keeping impulsive response roughly flat.
            //
            // But a SINGLE impulse (a struck plate, a stick-slip release, a
            // machinery clatter) never reaches steady state, and the filter's
            // own b0 coefficient is Q-INVARIANT for impulsive input — Q only
            // controls how long the ring lasts, not how loud the strike is.
            // Applying the steady-state 1/sqrt(Q) formula there was punishing
            // a long, quiet-but-real bell or plate resonance by up to -33 dB
            // simply for having a long T60, which is backwards: it made the
            // physically bigger, more resonant objects the quietest.
            if (coherent)
            {
                m.gain = gain / std::sqrt(juce::jmax(0.5f, q));
            }
            else
            {
                // Separately: the filter's own b0 coefficient scales with
                // sin(w0), i.e. roughly with frequency for anything well
                // below Nyquist. A 90 Hz mode gets about 37 dB less impulse
                // amplitude than a 4 kHz mode for the identical gain
                // parameter -- nothing to do with Q, just the biquad's own
                // coefficient shrinking at low frequency. This is what kept
                // Bell, Plate and Frog quiet even after the coherent fix,
                // since their fundamentals sit at 14-90 Hz.
                //
                // Compensating against an absolute flat target overcorrected
                // anything already in a reasonable range (Metal Bar's ~500 Hz
                // fundamental went from fine to clipping 25% of samples).
                // Instead compensate RELATIVE to ~2 kHz, where models were
                // already calibrated and sounding right: frequencies at or
                // above that get no boost at all, only genuinely low
                // fundamentals are lifted, and the lift is capped so a
                // near-DC mode cannot spike.
                const float w0 = juce::MathConstants<float>::twoPi * freq / (float)sr;
                const float w0Ref = juce::MathConstants<float>::twoPi * 2000.0f / (float)sr;
                const float s = juce::jmax(0.018f, std::sin(w0));
                const float sRef = std::sin(w0Ref);
                const float boost = juce::jlimit(1.0f, 10.0f, sRef / s);
                m.gain = gain * boost;
            }
            m.active = true;
        }

        inline float process(float exc) noexcept
        {
            float out = 0.0f;
            for (int i = 0; i < numModes; ++i)
                out += modes[(size_t)i].gain * modes[(size_t)i].r.process(exc);
            return out;
        }
    };

    // Q from a target T60. Measured plates show per-mode T60 spanning
    // 0.03 s to 11.5 s within one object, so this is set per mode, not globally.
    inline float qFromT60(float freq, float t60) noexcept
    {
        return juce::jmax(0.6f, juce::MathConstants<float>::pi * freq * t60 / 6.9078f);
    }

    //==========================================================================
    // Per-model loudness calibration.
    //
    // Measured directly: at matched settings (one individual, close, moderate
    // rate/duration), raw output level varied by nearly 60 dB between models
    // before the modal-bank fixes above, and still ~38 dB after them. Some of
    // that gap is architectural rather than a bug (Rain is a dense grain
    // stream, Bell is a handful of high modes) and correcting it fully would
    // risk destabilising models that already sound right. This table closes
    // most of the remaining gap with a single per-model multiplier, capped at
    // +-20 dB so no correction is large enough to be risky on its own.
    //
    // Index by [phony][model index within that class].
    inline float modelLoudnessGain(int phony, int model) noexcept
    {
        // Index 3 (legacy Frog) sits well below the others even after the
        // modal-bank fixes -- its pulse-into-formant architecture is simply
        // quiet. Measured at -31.7 LUFS against a -16.7 reference, so it needs
        // roughly +15 dB beyond the usual cap. Only the Frog Pond preset uses
        // this model, so the larger correction is contained.
        static const float bio[] = { 6.31f, 6.31f, 2.88f, 35.0f, 4.03f, 2.02f, 6.31f };
        // Index 8 ("Rumble") and index 9 ("Thunder") were previously left at
        // 1.0, unmeasured — a routing bug meant neither model was producing
        // its intended signal, so their level had never actually been heard
        // at full strength. With that fixed, both need the same kind of
        // boost as the other multi-band textures above.
        static const float geo[] = { 1.15f, 2.32f, 1.07f, 6.31f, 6.31f, 6.31f, 3.80f, 2.66f, 3.16f, 3.16f };
        static const float ant[] = { 6.31f, 5.82f, 6.31f, 0.42f, 6.31f, 6.31f };
        const float* table = (phony == 0) ? bio : (phony == 1) ? geo : ant;
        const int n = (phony == 0) ? 7 : (phony == 1) ? 10 : 6;
        if (model < 0 || model >= n) return 1.0f;
        return table[model];
    }

    //==========================================================================
    // Bubble grains.
    //
    // Water was previously made by firing impulses into a resonator bank, but
    // the measured Python damping gives Q ~= 11.5 at every frequency while the
    // C++ mapped up to Q = 72. A high-Q resonator fed impulses rings — which
    // is exactly why the water sounded like metal sheets.
    //
    // A real bubble is a damped sinusoid that sweeps UPWARD as it collapses,
    // so it is modelled directly as a grain rather than as a filter. The
    // upward chirp is most of the character of moving water and had no
    // implementation at all.
    //==========================================================================
    struct BubbleGrain
    {
        float phase = 0.0f, freq = 0.0f, chirpInc = 0.0f;
        float amp = 0.0f, decayMul = 0.0f;
        bool  active = false;

        inline float tick(double sr) noexcept
        {
            if (!active) return 0.0f;
            const float y = std::sin(phase) * amp;
            phase += kTwoPi * freq / (float)sr;
            if (phase > kTwoPi) phase -= kTwoPi;
            freq += chirpInc;
            amp *= decayMul;
            if (amp < 1.0e-4f) active = false;
            return y;
        }
    };

    //==========================================================================
    // Exact port of the reference rain/surf grain.
    //
    // Each drop is a 4 ms noise click, high-passed at 2.2 kHz and decaying with
    // a 0.7 ms time constant, PLUS an entrained Minnaert bubble that sweeps
    // upward. The composite is peak-normalised before use, exactly as the
    // reference bank is. The bubble is why rain sounds silvery rather than
    // like static.
    //==========================================================================
    struct DropGrain
    {
        // bubble part
        float phase = 0.0f, freq = 0.0f, chirpInc = 0.0f;
        float bubAmp = 0.0f, bubDecay = 0.0f;
        // click part
        float clickAmp = 0.0f, clickDecay = 0.0f;
        OnePole clickHP;
        float gain = 1.0f;
        // Every grain gets its own pan. The reference places each drop
        // independently, giving thousands of positions; spatialising only per
        // individual gave as many positions as there were individuals, which
        // is why the C++ sounded narrow next to it.
        float panL = 0.707f, panR = 0.707f;
        bool  active = false;

        void start(float f, float chirp, float bubMix, float clickLevel,
            float amp, double sr, juce::Random& rng, float width = 1.0f) noexcept
        {
            const float pan = (rng.nextFloat() * 2.0f - 1.0f) * juce::jlimit(0.0f, 1.0f, width);
            panL = std::cos((pan + 1.0f) * MathPi * 0.25f);
            panR = std::sin((pan + 1.0f) * MathPi * 0.25f);
            freq = juce::jlimit(20.0f, (float)sr * 0.45f, f);
            const float tau = juce::jmax(1.0e-4f, 0.00042f * (12000.0f / freq));
            bubDecay = std::exp(-1.0f / (tau * (float)sr));
            chirpInc = freq * chirp / juce::jmax(1.0f, 5.0f * tau * (float)sr);
            bubAmp = bubMix;
            phase = 0.0f;
            // 0.7 ms click decay, as in the reference
            clickDecay = std::exp(-1.0f / (0.0007f * (float)sr));
            clickAmp = clickLevel;
            clickHP.setCutoff(2200.0f, sr);
            clickHP.reset();
            // the reference peak-normalises each drop; approximate that by
            // dividing by the sum of the two contributions
            gain = amp / juce::jmax(0.35f, clickLevel + bubMix);
            active = true;
            juce::ignoreUnused(rng);
        }

        inline float tick(double sr, float noise) noexcept
        {
            if (!active) return 0.0f;
            float y = 0.0f;
            if (clickAmp > 1.0e-4f)
            {
                y += clickHP.hp(noise) * clickAmp * 0.9f;
                clickAmp *= clickDecay;
            }
            if (bubAmp > 1.0e-4f)
            {
                y += std::sin(phase) * bubAmp;
                phase += kTwoPi * freq / (float)sr;
                if (phase > kTwoPi) phase -= kTwoPi;
                freq += chirpInc;
                bubAmp *= bubDecay;
            }
            else if (clickAmp <= 1.0e-4f) active = false;
            return y * gain;
        }
    };

    struct DropPool
    {
        static constexpr int kMaxDrops = 96;
        std::array<DropGrain, kMaxDrops> drops;
        int next = 0;

        void spawn(float freq, float chirp, float bubMix, float clickLevel,
            float amp, double sr, juce::Random& rng, float width = 1.0f) noexcept
        {
            drops[(size_t)next].start(freq, chirp, bubMix, clickLevel, amp, sr, rng, width);
            next = (next + 1) % kMaxDrops;
        }

        // Returns mid, and writes the stereo difference to `side`.
        inline float process(double sr, juce::Random& rng, float& side) noexcept
        {
            const float noise = rng.nextFloat() * 2.0f - 1.0f;
            float l = 0.0f, r = 0.0f;
            for (auto& d : drops)
            {
                const float y = d.tick(sr, noise);
                l += y * d.panL;
                r += y * d.panR;
            }
            // Preserve the mid LEVEL when splitting: a centre-panned grain
            // contributes y*(panL+panR) = y*sqrt(2) to (l+r), so scale by
            // 1/sqrt(2) rather than 1/2 or the grains lose 3 dB against the
            // bed and the texture goes dull.
            side += 0.70710678f * (l - r);
            return 0.70710678f * (l + r);
        }

        void reset() noexcept { for (auto& d : drops) { d.active = false; d.bubAmp = d.clickAmp = 0.0f; } }
    };

    struct BubblePool
    {
        static constexpr int kMaxGrains = 32;
        std::array<BubbleGrain, kMaxGrains> grains;
        int next = 0;

        // Minnaert: f = 3.26 / r (metres). Damping measured from the reference
        // implementation is tau = 0.42 ms * (12000 / f), i.e. a constant Q of
        // about 11.5 across the whole size range.
        void spawn(float freq, float chirp, float amp, float damping, double sr) noexcept
        {
            auto& g = grains[(size_t)next];
            next = (next + 1) % kMaxGrains;
            g.freq = juce::jlimit(20.0f, (float)sr * 0.45f, freq);
            const float tau = juce::jmax(1.0e-4f,
                0.00042f * (12000.0f / g.freq) / juce::jmax(0.05f, damping));
            g.decayMul = std::exp(-1.0f / (tau * (float)sr));
            // sweeps upward across its lifetime, roughly 5 tau
            g.chirpInc = g.freq * chirp / juce::jmax(1.0f, 5.0f * tau * (float)sr);
            g.amp = amp;
            g.phase = 0.0f;
            g.active = true;
        }

        inline float process(double sr) noexcept
        {
            float out = 0.0f;
            for (auto& g : grains) out += g.tick(sr);
            return out;
        }

        void reset() noexcept { for (auto& g : grains) { g.active = false; g.amp = 0.0f; } }
    };

    //==========================================================================
    // Stick-slip friction excitation — the physical basis of shearing,
    // creaking and groaning metal. A spring loads at a rate set by the drive
    // velocity; when the load exceeds a randomly varying static threshold it
    // slips, releasing an impulse and dropping to the kinetic level. This
    // relaxation cycle is why creaking metal stutters in a way no LFO
    // reproduces.
    //==========================================================================
    struct StickSlip
    {
        float load = 0.0f;
        float muK = 0.4f;
        juce::Random rng;
        int  judder = 0;
        float judderAmp = 0.0f;
        int   judderCountdown = 0;

        inline float process(float driveRate, float roughness) noexcept
        {
            float out = 0.0f;
            load += juce::jmax(1.0e-7f, driveRate);
            const float thresh = 1.0f + roughness * 0.8f * (rng.nextFloat() * 2.0f - 1.0f);
            if (load >= thresh)
            {
                out = (load - muK) * (0.5f + 0.5f * rng.nextFloat());
                load = muK * (0.7f + 0.3f * rng.nextFloat());
                if (rng.nextFloat() < 0.30f * roughness)
                {
                    judder = 2 + rng.nextInt(4);
                    judderAmp = out;
                    judderCountdown = 20 + rng.nextInt(380);
                }
            }
            if (judder > 0 && --judderCountdown <= 0)
            {
                out += judderAmp * (0.15f + 0.35f * rng.nextFloat());
                --judder;
                judderCountdown = 20 + rng.nextInt(380);
            }
            return out;
        }
        void reset() noexcept { load = 0.0f; judder = 0; }
    };

    //==========================================================================
    // Field individual: one animal / one sound source at one place.
    //
    // Distance sets 1/d gain, propagation delay and HF air absorption.
    // Azimuth sets pan, narrowed with distance (the parallax cue that makes a
    // field feel deep rather than merely wide).
    //==========================================================================
    struct FieldIndividual
    {
        // placement
        float distance = 10.0f, azimuth = 0.0f, elevation = 0.15f;
        // Ground reflection. Every outdoor source has one, not just cars:
        // sound reaches the ear twice, once directly and once bounced off the
        // ground, and the interference between them is most of why a
        // recording sounds like OUTDOORS rather than like a dry sample.
        float groundDelay = 0.0f, groundGain = 0.0f;
        bool  ownGroundComb = false;   // models that compute their own
        float gain = 1.0f, panL = 0.7f, panR = 0.7f;
        OnePole air;
        std::vector<float> delayLine;
        int delayWrite = 0, delaySamples = 0;

        // per-individual model variation (DIVERSITY)
        float carrierScale = 1.0f, qScale = 1.0f, rateScale = 1.0f;

        // Kuramoto phase for chorus coupling
        float phase = 0.0f, omega = 1.0f;

        // event state
        bool  active = false;
        int   eventSample = 0, eventLength = 0;
        float eventAmp = 1.0f;

        // per-individual generator state
        ModalBank bank;
        Resonator aux[4];
        OnePole   tilt;
        OnePole   tilt2;
        StickSlip friction;
        BubblePool bubbles;
        DropPool   drops;
        OnePole    bedLP, bedLP2, canopyLP;
        OnePole    groundLP;   // damping of the ground-reflected ray
        float      swellPhaseA = 0.0f, swellPhaseB = 0.0f;
        float     oscPhase = 0.0f, oscPhase2 = 0.0f;
        float     subPhase = 0.0f;
        int       ribIndex = 0;
        float     env = 0.0f, envInc = 0.0f;

        // short line for the ground-reflection comb (Distant Road) and for
        // any model needing a real delay rather than an allpass approximation
        std::vector<float> combLine;
        int   combWrite = 0;
        float rumble = 0.0f, rumble2 = 0.0f;   // fire / machinery low-frequency state
        // Anuran larynx: two fold oscillators (the second loaded by the
        // fibrous mass) plus a completely independent arytenoid clock that
        // modulates amplitude.
        float foldPhase = 0.0f, massPhase = 0.0f, arytenoidPhase = 0.0f;
        // Stereo difference produced by grain models this sample. Kept beside
        // the mono path so the Field still handles distance and propagation
        // delay, while the decorrelated width survives to the output.
        float sideOut = 0.0f;

        // Rumble. The crack's thumps and its band-pass crack-texture copies
        // are struck modes and reuse the general-purpose `bank` (added in
        // triggerEvent, driven by a shared excitation in generateSample)
        // just like Bell/Plate/Metal Bar do. The tail needs five
        // INDEPENDENTLY noise-driven bands, one more than aux[4] provides,
        // so it gets its own small block of state here.
        Resonator thunderTailBand4;                         // 5th tail band (0-3 use aux[0..3])
        std::array<float, 5> thunderTailWalk{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
        OnePole   thunderPreHP, thunderPreLP;                // pre-rumble 40-300 Hz band
        float     thunderTremPhase = 0.0f;                   // pre-rumble tremolo, ramps in toward the strike
        bool      thunderStruck = false;                     // has the crack fired yet this event?
        int       thunderBurstSamples = 0;                   // core-burst excitation window (crack texture)
        int       thunderThumpSamples[3]{ -1, -1, -1 };      // per-thump countdown to its own tiny delay
        Resonator thunderReburstBand;                        // tail re-arrival swell: another filtered copy
        bool      thunderReburstActive = false;
        int       thunderReburstSamplesLeft = 0;
        float     thunderReburstAmp = 0.0f;

        // ---- Thunder: independent Butterworth-noise strike -----------------
        // Runs on Engine::PortedBurst and touches none of bank/aux/Resonator
        // above. "tv5" labels this model's own private state. Two short
        // excitation buffers are rendered once per event: the crack's shared
        // core burst and the tail's shared re-arrival burst. Everything
        // downstream filters COPIES of these same buffers through
        // independent Butterworth bandpasses, which is what gives one
        // cohesive event texture rather than many independently-random
        // mini-transients.
        std::vector<float> tv5Core;              // ~0.22s shared crack excitation
        std::vector<float> tv5Rebursts;          // ~0.35s shared tail re-arrival excitation

        ButterBandpass tv5PreBand;                // pre-rumble 40-300 Hz murmur band
        float tv5PreWalk = 0.0f;                  // causal stand-in for the offline smoothed random walk
        float tv5TremPhase = 0.0f;                // pre-rumble tremolo, ramps in toward the strike

        bool  tv5Struck = false;
        int   tv5CrackPos = -1;                   // samples elapsed since the strike (crack core index)
        static constexpr int kTv5CrackBands = 5;
        ButterBandpass tv5CrackBand[kTv5CrackBands];
        float tv5CrackStretch[kTv5CrackBands]{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };  // per-band decay jitter, drawn once per event

        static constexpr int kTv5Thumps = 3;
        int   tv5ThumpElapsed[kTv5Thumps]{ -1, -1, -1 };  // samples since each thump's own onset; -1 = not yet due

        static constexpr int kTv5TailBands = 5;
        ButterBandpass tv5TailBand[kTv5TailBands];
        float tv5TailWalk[kTv5TailBands]{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

        // A couple of concurrent rolling re-arrival swells (capping it at
        // two voices lets overlapping tails not truncate one another
        // without turning into an independent cloud).
        struct Tv5Reburst
        {
            bool active = false;
            int  pos = 0;              // sample index into tv5Rebursts
            float amp = 0.0f;
            float etSecs = 0.0f;       // event time this reburst fired, for its own decay
            ButterBandpass band;
        };
        static constexpr int kTv5RebusrtVoices = 2;
        std::array<Tv5Reburst, kTv5RebusrtVoices> tv5ReburstVoice;

        // fractional-delay read from the comb line
        inline float combRead(float delayInSamples) const noexcept
        {
            const int size = (int)combLine.size();
            if (size < 4) return 0.0f;
            const float d = juce::jlimit(1.0f, (float)size - 2.0f, delayInSamples);
            const int i0 = (int)d;
            const float fr = d - (float)i0;
            int r0 = combWrite - i0;      while (r0 < 0) r0 += size;
            int r1 = r0 - 1;              if (r1 < 0) r1 += size;
            return combLine[(size_t)r0] * (1.0f - fr) + combLine[(size_t)r1] * fr;
        }

        inline void combWriteSample(float x) noexcept
        {
            if (combLine.empty()) return;
            combLine[(size_t)combWrite] = x;
            if (++combWrite >= (int)combLine.size()) combWrite = 0;
        }

        void prepare(double sr, float maxDistance)
        {
            const int maxDelay = (int)(sr * (maxDistance / kSpeedOfSound) + 4.0);
            delayLine.assign((size_t)juce::jmax(8, maxDelay), 0.0f);
            delayWrite = 0;
            // ground-reflection path differences stay under ~30 ms
            combLine.assign((size_t)juce::jmax(16, (int)(sr * 0.035)), 0.0f);
            combWrite = 0;
            rumble = rumble2 = 0.0f;
            air.reset(); tilt.reset(); tilt2.reset(); friction.reset(); bubbles.reset(); drops.reset();
            groundLP.setCutoff(4500.0f, sr);   // the reflected ray is duller than the direct one
            bedLP.reset(); bedLP2.reset(); canopyLP.reset(); groundLP.reset();
            swellPhaseA = swellPhaseB = 0.0f;
            bank.clear();
            for (auto& r : aux) r.reset();

            thunderTailBand4.reset(); thunderReburstBand.reset();
            thunderTailWalk.fill(0.0f);
            thunderPreHP.reset(); thunderPreLP.reset();
            thunderTremPhase = 0.0f;
            thunderStruck = false;
            thunderBurstSamples = 0;
            thunderThumpSamples[0] = thunderThumpSamples[1] = thunderThumpSamples[2] = -1;
            thunderReburstActive = false;
            thunderReburstSamplesLeft = 0;
            thunderReburstAmp = 0.0f;

            tv5Core.clear(); tv5Rebursts.clear();
            tv5PreBand.reset(); tv5PreWalk = 0.0f; tv5TremPhase = 0.0f;
            tv5Struck = false; tv5CrackPos = -1;
            for (auto& b : tv5CrackBand) b.reset();
            for (auto& s : tv5CrackStretch) s = 1.0f;
            for (auto& e : tv5ThumpElapsed) e = -1;
            for (auto& b : tv5TailBand) b.reset();
            for (auto& w : tv5TailWalk) w = 0.0f;
            for (auto& rv : tv5ReburstVoice) { rv.active = false; rv.pos = 0; rv.amp = 0.0f; rv.etSecs = 0.0f; rv.band.reset(); }
        }

        void place(float d, float az, float elevMetres, float humidity,
            float parallax, double sr)
        {
            distance = juce::jmax(0.6f, d);
            azimuth = juce::jlimit(-1.0f, 1.0f, az);
            elevation = juce::jlimit(0.02f, 40.0f, elevMetres);

            gain = 1.0f / std::pow(distance, 0.85f);

            // air absorption: HF cutoff falls with distance, faster in humid air
            float fc = 18000.0f / (1.0f + std::pow(distance / 6.0f, 0.9f + 0.5f * humidity));
            // Elevated sources clear the grass, scrub and ground clutter that
            // absorb high frequencies along a low path, so they arrive
            // brighter as well as differently combed.
            fc *= 1.0f + 0.9f * juce::jmin(1.0f, elevation / 10.0f);
            air.setCutoff(juce::jmax(fc, 700.0f), sr);

            delaySamples = juce::jlimit(0, (int)delayLine.size() - 2,
                (int)(sr * distance / kSpeedOfSound));

            // --- ground reflection -------------------------------------
            // Path difference between the direct ray and the one bouncing off
            // the ground, from true geometry. A cricket at 3 m combs at about
            // 3.6 kHz, right inside its own band; a bird at 8 m gets a dense
            // 57 Hz comb, which is the overhead signature. Distant low sources
            // push the comb above hearing, which is why far things sound plain.
            const float hs = juce::jmax(0.02f, elevation);
            const float hr = 1.6f;                       // ears
            const float horiz = juce::jmax(0.5f,
                std::sqrt(juce::jmax(0.0f,
                    distance * distance - (hs - hr) * (hs - hr))));
            const float direct = std::sqrt(horiz * horiz + (hs - hr) * (hs - hr));
            const float reflected = std::sqrt(horiz * horiz + (hs + hr) * (hs + hr));
            groundDelay = juce::jlimit(1.0f, (float)combLine.size() - 3.0f,
                (reflected - direct) / kSpeedOfSound * (float)sr);
            // grass and soil absorb; the reflection is never a full mirror
            groundGain = 0.55f / (1.0f + reflected / juce::jmax(1.0f, horiz) * 0.15f);

            const float width = 1.0f / (1.0f + distance / (14.0f * juce::jmax(0.05f, parallax)));
            const float pan = juce::jlimit(-1.0f, 1.0f, azimuth * width);
            panL = std::cos((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
            panR = std::sin((pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f);
        }

        // push a mono sample through distance filtering + delay, get L/R
        // Mono path through distance filtering and propagation delay, plus a
        // side signal added straight to the output. Distance still collapses
        // the image because `sideOut` is scaled by the same width factor the
        // panning uses.
        inline void spatialiseStereo(float in, float side, float& l, float& r) noexcept
        {
            float src = in;
            if (!ownGroundComb && !combLine.empty())
            {
                combWriteSample(src);
                // the reflected ray arrives later, quieter, and duller
                const float refl = combRead(groundDelay);
                src = src - groundLP.lp(refl) * groundGain;
            }
            const float filtered = air.lp(src) * gain;
            delayLine[(size_t)delayWrite] = filtered;
            int readPos = delayWrite - delaySamples;
            if (readPos < 0) readPos += (int)delayLine.size();
            const float out = delayLine[(size_t)readPos];
            if (++delayWrite >= (int)delayLine.size()) delayWrite = 0;
            // Distance collapses a POINT source toward the centre, but a
            // diffuse field (rain, surf) stays wide however far away it is —
            // you are inside it, not looking at it. Collapse the side channel
            // far more gently than the panning, or distant water goes mono.
            const float sw = side * gain * (1.0f / (1.0f + distance / 90.0f));
            l += out * panL + sw;
            r += out * panR - sw;
        }

        inline void spatialise(float in, float& l, float& r) noexcept
        {
            const float filtered = air.lp(in) * gain;
            delayLine[(size_t)delayWrite] = filtered;
            int readPos = delayWrite - delaySamples;
            if (readPos < 0) readPos += (int)delayLine.size();
            const float out = delayLine[(size_t)readPos];
            if (++delayWrite >= (int)delayLine.size()) delayWrite = 0;
            l += out * panL;
            r += out * panR;
        }
    };

    //==========================================================================
    // Weather.
    //
    // Without this the three slots are independent layers that happen to play
    // at once. In a real habitat a gust does everything simultaneously: the
    // canopy moves, the wind rises, and the animals go quiet — birds and
    // insects genuinely reduce calling during gusts and rain, because the
    // signal would be masked and calling costs energy. One shared process
    // driving all three slots is what turns layers into a place.
    //==========================================================================
    struct Weather
    {
        float gust = 0.4f;        // 0..1, slow 1/f-ish
        float rain = 0.0f;        // how much rain is currently falling
        float target = 0.4f;
        int   countdown = 0;
        juce::Random rng{ 4242 };
        OnePole smooth;

        void prepare(double sr)
        {
            // very slow: gusts arrive over seconds, not milliseconds
            smooth.setCutoff(0.12f, sr);
            smooth.reset();
            gust = target = 0.4f;
            countdown = 0;
        }

        // advance once per block; the gust is a slow random walk toward
        // successive targets, which reads as weather rather than as an LFO
        void update(int numSamples, double sr)
        {
            countdown -= numSamples;
            if (countdown <= 0)
            {
                target = std::pow(rng.nextFloat(), 1.4f);      // calm more often than gusty
                countdown = (int)(sr * (0.8 + 4.5 * rng.nextFloat()));
            }
            for (int i = 0; i < numSamples; ++i) gust = smooth.lp(target);
            gust = juce::jlimit(0.0f, 1.0f, gust);
        }

        // Combined masking pressure on calling animals.
        float quietening() const noexcept
        {
            return juce::jlimit(0.0f, 1.0f, gust * 0.75f + rain * 0.9f);
        }
    };

    //==========================================================================
    // Habitat — FDN ambience. Forest sound is diffusion and absorption rather
    // than a long tail, so this is short, dense and heavily HF-damped.
    //==========================================================================
    class Habitat
    {
    public:
        void prepare(double sampleRate, int blockSize);
        void setParams(float size, float damping, float mix);
        void process(juce::AudioBuffer<float>& buffer);
        void reset();

    private:
        static constexpr int kNumLines = 8;
        std::array<std::vector<float>, kNumLines> lines;
        std::array<int, kNumLines>   writePos{ };
        std::array<int, kNumLines>   lengths{ };
        std::array<OnePole, kNumLines> damp;
        double sr = 44100.0;
        float  feedback = 0.6f, mixAmount = 0.2f;
        juce::SmoothedValue<float> mixSmoothed;
    };

    //==========================================================================
    // Presets
    //
    // A preset stores a full parameter state, optionally plus a NIGHT VARIANT.
    // Time of Day then interpolates between the two — but only across the
    // parameters that actually differ, so anything the preset does not morph
    // stays under the user's hands rather than being yanked around.
    //==========================================================================
    struct PresetValue { const char* id; float value; };

    struct FactoryPreset
    {
        const char* name;
        const char* description;
        std::vector<PresetValue> day;
        std::vector<PresetValue> night;   // may be empty -> no morph
    };

    const std::vector<FactoryPreset>& getFactoryPresets();
}

//==============================================================================
class SoundscapeEcologyAudioProcessor;

class PresetManager
{
public:
    explicit PresetManager(SoundscapeEcologyAudioProcessor& p);

    juce::StringArray getAllNames() const;
    int  getNumFactory() const;
    void loadByIndex(int index);
    void loadNext(int direction);
    int  getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const;

    void saveUserPreset(const juce::String& name);
    void deleteUserPreset(const juce::String& name);
    void loadFromFile(const juce::File& file);
    void refreshUserPresets();
    void installFactoryPresetsToDisk();

    // Time of Day morph. Applies only to parameters that differ between the
    // day and night variants of the loaded preset.
    void applyMorph(float t);
    bool hasMorph() const noexcept { return !morphKeys.isEmpty(); }

    static juce::File getPresetDirectory();

private:
    void captureState(std::map<juce::String, float>& dest) const;
    void applyValues(const std::vector<soundeco::PresetValue>& values);
    void resetToDefaults();
    void computeMorphKeys();

    SoundscapeEcologyAudioProcessor& processor;
    int currentIndex = 0;
    juce::StringArray userPresets;

    std::map<juce::String, float> dayState, nightState;
    juce::StringArray morphKeys;
    float lastMorph = -1.0f;
};

namespace soundeco
{
    struct SlotParams
    {
        int   model = 0;
        int   phony = 0;   // which class this slot holds
        float knobs[kNumModelKnobs]{ };
        float rate = 1.0f, phrase = 1.0f, jitter = 0.2f, swing = 0.0f;
        float rhythm = 0.0f;   // 0 = shallow AM, 1 = sharp decaying syllables
        float attack = 0.1f, decay = 0.4f, duration = 0.5f;
        int   population = 8;
        float spread = 0.5f, distance = 0.4f, diversity = 0.3f;
        float synchrony = 0.3f, parallax = 0.5f, level = 0.8f;
        float elevation = 0.15f;   // metres above ground
        bool  enabled = true;
        bool  drawPitch = false, drawAmp = false;
        float drawDepth = 0.5f;
        const float* pitchCurve = nullptr;
        const float* ampCurve = nullptr;
    };
}

//==============================================================================
class SoundscapeEcologyAudioProcessor : public juce::AudioProcessor,
    private juce::AsyncUpdater
{
public:
    SoundscapeEcologyAudioProcessor();
    ~SoundscapeEcologyAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Draw canvases. Owned here so they persist with the plugin state.
    // [slot][0] = pitch curve, [slot][1] = amplitude curve.
    std::array<std::array<float, soundeco::kDrawPoints>, soundeco::kNumSlots> pitchCurves;
    std::array<std::array<float, soundeco::kDrawPoints>, soundeco::kNumSlots> ampCurves;
    void resetCurves(int slot);

    // metering / visualisation for the editor
    std::atomic<float> slotActivity[soundeco::kNumSlots]{ };
    std::atomic<float> chorusOrder[soundeco::kNumSlots]{ };  // Kuramoto R

    // Ask the chorus phases to restage so sound arrives immediately. Called
    // by PresetManager on preset load, and internally on note-on.
    void requestRetrigger() noexcept { retriggerPending = true; }

    // Subtly randomise one slot. Nudges the physics and articulation around
    // their CURRENT values rather than scattering them across the full range,
    // so a scene stays recognisably itself and only shifts character. The
    // model choice, class, level and population are left alone, since those
    // are structural decisions rather than shading.
    void randomiseSlot(int slot, float amount = 0.18f);

    // Presets
    std::unique_ptr<PresetManager> presets;
    std::function<void()> onPresetChanged;   // editor refresh hook

    // Dolbear's law: chirps per second from temperature in Celsius.
    // Calibrated on the snowy tree cricket; other species have their own
    // slopes, which is a feature rather than a bug.
    static float dolbearChirpRate(float celsius) noexcept
    {
        const float f = celsius * 9.0f / 5.0f + 32.0f;
        return juce::jmax(1.0f, f - 40.0f) / 14.0f;
    }

private:
    void readParameters();
    void renderSlot(int slot, juce::AudioBuffer<float>& out, int numSamples);
    void triggerEvent(int slot, soundeco::FieldIndividual& ind);
    float generateSample(int slot, soundeco::FieldIndividual& ind, float t01);
    void configureIndividual(int slot, soundeco::FieldIndividual& ind, int index);
    void updateField(int slot);
    void applyNiche();
    void handleAsyncUpdate() override;
    // Called when the gate opens, so a note produces sound immediately
    // instead of waiting for the next chorus cycle.
    void retriggerField(int slot);
    bool retriggerPending = false;

    std::atomic<float> pendingMorph{ -1.0f };
    float lastSeenTimeOfDay = -1.0f;

    //--------------------------------------------------------------------
    // MIDI gating.
    //
    // A soundscape is a field of many individuals rather than a monophonic
    // voice, so MIDI does not allocate voices here. Instead notes open and
    // close a global gate, and optionally transpose every model's carrier.
    // Held notes sustain the habitat; releasing lets it decay naturally.
    //--------------------------------------------------------------------
    enum class MidiMode { FreeRun = 0, Gated = 1, GatedPitch = 2 };

    std::array<int, 128> noteVelocity{ };
    int   heldNotes = 0;
    int   lastNoteNumber = 60;
    float gateEnv = 0.0f;       // 0..1, smoothed
    float gateTarget = 0.0f;
    float velocityGain = 1.0f;
    float midiPitchRatio = 1.0f;       // multiplies every carrier
    std::vector<float> gateBuffer;

    void buildGateEnvelope(juce::MidiBuffer& midi, int numSamples);
    void handleNoteOn(int note, float velocity);
    void handleNoteOff(int note);

    double currentSampleRate = 44100.0;
    int    currentBlockSize = 512;

    soundeco::SlotParams slotParams[soundeco::kNumSlots];
    int  lastModel[soundeco::kNumSlots]{ -1, -1, -1 };
    int  lastPopulation[soundeco::kNumSlots]{ -1, -1, -1 };

    std::array<std::vector<soundeco::FieldIndividual>, soundeco::kNumSlots> field;
    soundeco::Habitat habitat;
    soundeco::Weather weather;
    float weatherCoupling = 0.0f;

    juce::Random rng;
    juce::AudioBuffer<float> slotBuffer;

    float globalTemperature = 22.0f, globalHumidity = 0.5f;
    float nicheAmount = 0.0f;
    float nicheShift[soundeco::kNumSlots]{ 1.0f, 1.0f, 1.0f };

    juce::SmoothedValue<float> outputGain, widthAmount;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoundscapeEcologyAudioProcessor)
};