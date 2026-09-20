#include "RandomPreset.h"

namespace aquanova {

//==============================================================================
namespace
{
    constexpr int kBi = 64;   // the centre of a bipolar 0..127 parameter

    /** An inclusive range the generator picks from. */
    struct R
    {
        int lo, hi;
        int pick (juce::Random& rng) const
        {
            return lo + rng.nextInt (juce::jmax (1, hi - lo + 1));
        }
    };

    enum Flags : unsigned
    {
        None        = 0,
        RingMod     = 1u << 0,   // inharmonic ring modulation
        Inharmonic  = 1u << 1,   // detuned, non-octave oscillator intervals
        ForceSplit  = 1u << 2,   // always use a split / formant filter type
        ResSweep    = 1u << 3,   // deep slow envelope sweep at high resonance
        FilterLfo   = 1u << 4,   // LFO on cutoff
        PwmLfo      = 1u << 5,   // LFO on pulse width - the classic pad shimmer
        Vibrato     = 1u << 6,   // gentle LFO on pitch
        ArpOn       = 1u << 7,
        CombFx      = 1u << 8,
        AutoPan     = 1u << 9,
        GatedVerb   = 1u << 10,
        Glide       = 1u << 11,
        FastRepeat  = 1u << 12,  // AD repeat, for rhythmic movement
        OneShotUp   = 1u << 13,  // a rising, non-looping gesture
        BreathNoise = 1u << 14,  // a little filtered noise mixed in with the tone
        FmVoice     = 1u << 15,  // oscillators 1 and 2 phase-modulate oscillator 3
        SingleOsc   = 1u << 16,  // one oscillator only, no stacking
        SineHarmonics = 1u << 17 // fundamental + tuned octave/fifth overtones,
                                 // pitch-enveloped so they bend down on release
    };

    struct Profile
    {
        const char* label;
        const char* kind;

        int octave;        // 0..4, where 2 is unison pitch
        int thirdOffset;   // octave offset applied to oscillator 3
        int sawBias;       // percent chance an oscillator is a saw

        R detune;          // cents-ish spread between oscillators
        R pulseWidth;
        R soften;          // 127 = fully open

        R cutoff, reso;    // 64 is about 380 Hz, 80 = 1 kHz, 95 = 2.5 kHz
        int slope;         // 0 = 12 dB, 1 = 18, 2 = 24
        float splitChance;
        R overdrive;

        R ampA, ampD, ampS, ampR;
        R fltA, fltD, fltS, fltR;
        R envDepth;        // how far envelope 2 opens the filter

        R reverb, delay, chorus, dist;

        int unison;        // 0 = off, otherwise voice count
        float syncChance;
        unsigned flags;
    };

    //==========================================================================
    // The whole personality of the generator lives in this table.
    //==========================================================================
    const Profile kProfiles[] =
    {
    // --- low end -------------------------------------------------------------
    { "Bass", "Bass", 1, 0, 70, {1,4}, {50,80}, {100,127},
      {58,76}, {15,55}, 2, 0.0f, {10,45},
      {0,0}, {55,80}, {95,120}, {20,40},
      {0,0}, {40,65}, {45,80}, {25,45}, {20,40},
      {0,12}, {0,0}, {0,0}, {5,30}, 2, 0.0f, None },

    { "Sub Bass", "Sub", 0, 1, 20, {0,2}, {45,55}, {60,95},
      {48,62}, {5,25}, 2, 0.0f, {0,20},
      {0,4}, {60,90}, {105,127}, {20,45},
      {0,0}, {50,75}, {70,100}, {30,50}, {10,25},
      {0,8}, {0,0}, {0,0}, {0,18}, 0, 0.0f, None },

    { "Acid Bass", "Acid", 1, 0, 85, {0,2}, {50,70}, {110,127},
      {52,72}, {75,110}, 2, 0.0f, {25,60},
      {0,0}, {40,65}, {20,60}, {18,35},
      {0,0}, {30,55}, {0,25}, {20,40}, {40,62},
      {5,20}, {10,35}, {0,0}, {10,35}, 0, 0.0f, Glide },

    // A real 303: one oscillator, 24 dB low pass, a short decay-only envelope on
    // the cutoff, high resonance, glide between notes and a little grit after it.
    { "303", "303", 1, 0, 60, {0,0}, {48,52}, {118,127},
      {40,62}, {88,118}, 2, 0.0f, {20,55},
      {0,0}, {55,80}, {105,127}, {14,26},
      {0,0}, {28,50}, {0,10}, {16,30}, {48,63},
      {0,15}, {5,30}, {0,0}, {15,45}, 0, 0.0f, (Glide | SingleOsc | ArpOn) },

    { "Wobble Bass", "Wobble", 1, 0, 75, {2,6}, {40,75}, {105,127},
      {55,78}, {55,95}, 2, 0.0f, {15,50},
      {0,6}, {60,90}, {100,127}, {20,40},
      {0,0}, {50,75}, {70,100}, {25,45}, {15,30},
      {0,15}, {0,20}, {0,0}, {10,40}, 2, 0.0f, FilterLfo },

    // --- melodic -------------------------------------------------------------
    { "Lead", "Lead", 2, 1, 60, {1,4}, {50,80}, {105,127},
      {78,100}, {25,70}, 2, 0.05f, {5,30},
      {0,12}, {55,85}, {105,127}, {30,55},
      {0,25}, {45,80}, {50,90}, {30,55}, {12,32},
      {15,40}, {15,45}, {0,35}, {0,20}, 3, 0.10f, Vibrato },

    { "Sync Lead", "Sync", 2, 0, 55, {0,3}, {45,80}, {110,127},
      {80,104}, {20,55}, 1, 0.0f, {5,35},
      {0,10}, {50,80}, {100,127}, {28,50},
      {0,20}, {45,75}, {45,85}, {28,50}, {18,42},
      {12,35}, {15,45}, {0,30}, {0,25}, 2, 1.0f, Vibrato },

    // Mellow and breathy: a near-sine tone, gentle attack, a little noise for
    // breath, vibrato that arrives late. No glide: portamento here slides
    // per-voice regardless of poly/mono, so on a sustained pad-like patch it
    // reads as random pitch drift between notes rather than a deliberate
    // glissando - not what we want for a clean, breathy tone.
    { "Flute", "Flute", 3, 0, 5, {0,2}, {46,54}, {58,82},
      {62,80}, {0,15}, 0, 0.0f, {0,0},
      {28,48}, {60,85}, {108,127}, {30,52},
      {25,45}, {60,85}, {95,120}, {30,50}, {6,16},
      {30,65}, {5,25}, {15,50}, {0,0}, 0, 0.0f,
      (Vibrato | BreathNoise | SingleOsc) },

    // A single "sine-ish" fundamental (square, heavily filtered) plus two
    // quiet tuned overtones - an octave and an octave-plus-fifth above - so
    // it reads as a plain harmonic tone rather than a buzzy synth wave. No
    // vibrato and no glide: the character comes from Osc1PitchEnv3 further
    // down, which holds all three oscillators steady while the note is held
    // and lets them bend down together on release - the "sweep down when you
    // let go" effect, done as a pitch envelope rather than portamento.
    { "Sine Flute", "Sine", 3, 0, 0, {0,0}, {48,52}, {80,110},
      {58,72}, {0,10}, 2, 0.0f, {0,0},
      {20,45}, {50,80}, {110,127}, {70,100},
      {20,40}, {40,70}, {100,127}, {40,70}, {0,0},
      {40,70}, {0,20}, {0,15}, {0,0}, 0, 0.0f,
      (BreathNoise | SineHarmonics) },

    // Classic FM: oscillators 1 and 2 phase-modulate oscillator 3 at simple
    // ratios, with a struck envelope on the modulators - the DX recipe.
    { "FM Classic", "FM", 2, 0, 10, {0,1}, {45,55}, {115,127},
      {90,120}, {0,20}, 0, 0.0f, {0,0},
      {0,10}, {70,100}, {30,80}, {45,75},
      {0,0}, {45,75}, {0,30}, {40,65}, {8,20},
      {30,65}, {10,40}, {10,45}, {0,0}, 0, 0.0f, (FmVoice | Inharmonic) },

    { "Brass", "Brass", 2, 0, 85, {1,4}, {50,75}, {110,127},
      {68,88}, {10,35}, 1, 0.0f, {0,0},
      {25,45}, {55,80}, {100,122}, {35,60},
      {20,40}, {55,80}, {70,100}, {40,60}, {15,35},
      {15,40}, {0,20}, {35,75}, {0,0}, 2, 0.0f, None },

    { "Strings", "Strings", 2, 1, 90, {4,9}, {45,70}, {95,120},
      {70,90}, {5,28}, 1, 0.05f, {0,0},
      {35,60}, {60,90}, {105,127}, {55,85},
      {30,55}, {60,90}, {90,120}, {55,80}, {8,22},
      {35,70}, {0,20}, {50,95}, {0,0}, 4, 0.0f, PwmLfo },

    { "Organ", "Organ", 2, 2, 10, {0,2}, {45,55}, {115,127},
      {76,100}, {0,20}, 0, 0.0f, {0,15},
      {0,4}, {90,120}, {120,127}, {8,22},
      {0,8}, {80,110}, {110,127}, {10,25}, {4,14},
      {10,35}, {0,15}, {20,60}, {0,12}, 0, 0.0f, None },

    { "Clav", "Clav", 2, 0, 25, {0,2}, {20,40}, {115,127},
      {74,96}, {35,75}, 2, 0.0f, {10,35},
      {0,0}, {42,62}, {10,45}, {18,34},
      {0,0}, {32,52}, {0,25}, {20,36}, {30,52},
      {10,30}, {10,35}, {0,30}, {5,25}, 0, 0.0f, None },

    { "Electric Piano", "EP", 2, 1, 20, {0,2}, {35,55}, {105,125},
      {72,94}, {5,25}, 1, 0.0f, {0,12},
      {0,0}, {70,95}, {25,60}, {35,60},
      {0,0}, {55,80}, {15,45}, {35,55}, {16,34},
      {20,50}, {0,20}, {25,65}, {0,10}, 0, 0.0f, None },

    // --- sustained -----------------------------------------------------------
    { "Pad", "Pad", 2, 1, 50, {4,9}, {50,80}, {85,115},
      {72,92}, {5,30}, 1, 0.22f, {0,0},
      {60,92}, {70,100}, {110,127}, {75,105},
      {50,85}, {70,100}, {80,115}, {70,100}, {8,22},
      {45,85}, {0,25}, {55,100}, {0,0}, 0, 0.0f, PwmLfo },

    { "Glass Pad", "Glass", 3, 0, 15, {2,6}, {18,38}, {118,127},
      {92,118}, {8,30}, 0, 0.12f, {0,0},
      {45,80}, {75,105}, {95,125}, {70,105},
      {40,75}, {70,100}, {75,110}, {65,95}, {6,18},
      {50,90}, {15,45}, {45,90}, {0,0}, 2, 0.0f, (RingMod | PwmLfo) },

    { "Warm Pad", "Warm", 2, 1, 45, {5,10}, {50,75}, {68,92},
      {58,74}, {5,25}, 2, 0.18f, {0,0},
      {65,95}, {75,105}, {112,127}, {80,110},
      {55,85}, {75,105}, {85,118}, {75,105}, {6,18},
      {45,85}, {0,20}, {55,100}, {0,0}, 3, 0.0f, PwmLfo },

    { "Drone", "Drone", 1, 1, 60, {6,12}, {45,80}, {95,125},
      {64,88}, {25,70}, 2, 0.20f, {0,20},
      {70,105}, {80,110}, {127,127}, {85,115},
      {60,95}, {80,110}, {100,127}, {80,110}, {8,20},
      {40,80}, {0,25}, {40,85}, {0,15}, 2, 0.0f, (FilterLfo | Inharmonic) },

    { "Choir", "Choir", 2, 1, 40, {3,8}, {40,60}, {90,115},
      {74,94}, {10,35}, 1, 1.0f, {0,0},
      {45,75}, {70,100}, {108,127}, {60,95},
      {40,70}, {70,100}, {85,115}, {60,90}, {6,18},
      {45,85}, {0,20}, {45,90}, {0,0}, 3, 0.0f, (ForceSplit | PwmLfo) },

    // --- struck and short ----------------------------------------------------
    { "Pluck", "Pluck", 2, 0, 65, {1,4}, {50,80}, {100,127},
      {82,106}, {25,65}, 2, 0.0f, {0,10},
      {0,0}, {38,58}, {0,0}, {25,45},
      {0,0}, {35,60}, {0,25}, {25,45}, {30,55},
      {25,55}, {10,30}, {0,35}, {0,0}, 0, 0.0f, GatedVerb },

    { "Bell", "Bell", 3, 0, 15, {0,0}, {20,45}, {100,127},
      {96,120}, {0,25}, 0, 0.0f, {0,0},
      {0,0}, {85,110}, {0,18}, {75,105},
      {0,0}, {75,100}, {0,20}, {70,100}, {10,25},
      {40,75}, {10,35}, {0,30}, {0,0}, 0, 0.0f, (RingMod | Inharmonic) },

    { "Metallic", "Metal", 2, 1, 20, {0,3}, {15,40}, {110,127},
      {88,115}, {25,60}, 1, 0.10f, {5,30},
      {0,2}, {70,100}, {0,30}, {60,95},
      {0,0}, {60,90}, {0,30}, {55,85}, {15,35},
      {35,75}, {15,45}, {0,35}, {5,30}, 0, 0.0f, (RingMod | Inharmonic | CombFx) },

    { "Stab", "Stab", 2, 0, 75, {2,6}, {45,75}, {110,127},
      {76,98}, {30,70}, 2, 0.10f, {5,30},
      {0,0}, {35,55}, {0,20}, {20,38},
      {0,0}, {28,48}, {0,18}, {20,38}, {32,58},
      {20,50}, {10,35}, {10,45}, {0,25}, 3, 0.0f, None },

    { "Blip", "Blip", 3, 0, 40, {0,3}, {25,60}, {115,127},
      {88,115}, {35,80}, 2, 0.0f, {0,20},
      {0,0}, {22,40}, {0,0}, {14,28},
      {0,0}, {20,38}, {0,10}, {14,28}, {35,60},
      {15,45}, {20,55}, {0,30}, {0,20}, 0, 0.0f, None },

    // --- movement and effects ------------------------------------------------
    { "Arp", "Arp", 2, 1, 55, {1,4}, {45,80}, {100,127},
      {76,98}, {25,65}, 2, 0.05f, {0,15},
      {0,8}, {35,55}, {30,70}, {22,40},
      {0,0}, {40,65}, {45,80}, {25,45}, {22,45},
      {18,45}, {25,60}, {0,35}, {0,15}, 0, 0.10f, ArpOn },

    { "Reso Sweep", "Sweep", 2, 1, 70, {2,6}, {45,80}, {105,127},
      {40,60}, {85,120}, 2, 0.0f, {10,40},
      {10,40}, {70,100}, {100,127}, {50,80},
      {60,100}, {85,115}, {60,100}, {70,100}, {45,63},
      {30,70}, {15,45}, {20,60}, {5,30}, 2, 0.0f, (ResSweep | FilterLfo) },

    { "Riser", "Riser", 1, 2, 60, {3,9}, {40,80}, {90,120},
      {36,56}, {60,105}, 2, 0.10f, {5,35},
      {95,120}, {90,120}, {127,127}, {40,70},
      {100,124}, {95,120}, {120,127}, {50,80}, {50,63},
      {45,90}, {20,55}, {30,75}, {0,25}, 3, 0.0f, (OneShotUp | ResSweep) },

    { "Formant", "Vox", 2, 1, 65, {2,6}, {45,75}, {95,125},
      {70,95}, {30,75}, 1, 1.0f, {0,20},
      {20,55}, {60,90}, {90,125}, {45,80},
      {20,50}, {60,90}, {70,110}, {45,75}, {15,38},
      {30,70}, {10,40}, {25,70}, {0,15}, 2, 0.0f, (ForceSplit | FilterLfo) },

    { "Noise FX", "FX", 2, 2, 50, {0,0}, {30,90}, {40,110},
      {62,112}, {45,100}, 2, 0.25f, {0,40},
      {0,70}, {45,95}, {40,110}, {45,95},
      {0,60}, {40,90}, {30,100}, {40,90}, {20,50},
      {30,80}, {20,70}, {0,45}, {10,55}, 0, 0.55f,
      (CombFx | AutoPan | FilterLfo | Inharmonic) },

    { "Atmosphere", "Atmos", 2, 2, 35, {4,10}, {30,70}, {70,110},
      {66,96}, {20,60}, 1, 0.30f, {0,15},
      {70,110}, {85,115}, {90,127}, {90,120},
      {60,100}, {80,115}, {70,115}, {85,115}, {12,34},
      {60,105}, {30,75}, {40,95}, {0,20}, 2, 0.0f,
      (FilterLfo | PwmLfo | AutoPan | FastRepeat) }
    };

    static_assert ((int) (sizeof (kProfiles) / sizeof (kProfiles[0]))
                       == (int) RandomPreset::Anything,
                   "profile table must cover every character before Anything");

    //==========================================================================
    void setParam (juce::AudioProcessorValueTreeState& state, int hardwareIndex, int value)
    {
        const int slot = paramSlotForIndex (hardwareIndex);
        if (slot < 0)
            return;

        const auto& d = kParams[slot];

        if (auto* p = state.getParameter (d.id))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (d.min, d.max, value)));
    }

    void resetToDefaults (juce::AudioProcessorValueTreeState& state)
    {
        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& d = kParams[i];
            if (auto* p = state.getParameter (d.id))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) d.def));
        }
    }

    /** Program volume per character, chosen from measured loudness so that
        flicking through the list does not jump 12 dB between patches. */
    int volumeFor (int character)
    {
        switch ((RandomPreset::Character) character)
        {
            case RandomPreset::SubBass:       return 96;
            case RandomPreset::Metallic:      return 98;
            case RandomPreset::Drone:         return 100;
            case RandomPreset::Organ:         return 102;
            case RandomPreset::Bass:          return 108;
            case RandomPreset::TB303:         return 110;
            case RandomPreset::FMClassic:     return 116;
            case RandomPreset::Flute:         return 124;
            case RandomPreset::SineFlute:     return 122;
            case RandomPreset::ElectricPiano: return 112;
            case RandomPreset::Brass:
            case RandomPreset::Choir:
            case RandomPreset::Formant:       return 122;
            case RandomPreset::Strings:
            case RandomPreset::WarmPad:       return 125;
            case RandomPreset::Pluck:
            case RandomPreset::Stab:
            case RandomPreset::Blip:          return 127;
            default:                          return 118;
        }
    }

    void setEnv (juce::AudioProcessorValueTreeState& state, int base,
                 const R& a, const R& d, const R& s, const R& r, juce::Random& rng)
    {
        setParam (state, base + 0, a.pick (rng));
        setParam (state, base + 1, d.pick (rng));
        setParam (state, base + 2, s.pick (rng));
        setParam (state, base + 3, r.pick (rng));
    }
}

//==============================================================================
juce::StringArray RandomPreset::characterNames()
{
    juce::StringArray names;

    for (int i = 0; i < (int) Anything; ++i)
        names.add (kProfiles[i].label);

    names.add ("Anything");
    names.add ("Super Random");
    return names;
}

//==============================================================================
void RandomPreset::generate (juce::AudioProcessorValueTreeState& state,
                             Character character, juce::Random& rng)
{
    if (character == Anything || ! juce::isPositiveAndBelow ((int) character, (int) Anything))
        character = (Character) rng.nextInt ((int) Anything);

    const auto& pr = kProfiles[(int) character];
    const bool has = true;
    juce::ignoreUnused (has);

    resetToDefaults (state);

    // ---------------------------------------------------------------- oscillators
    const int oscCount = (pr.flags & SingleOsc)  != 0 ? 1
                       : (pr.flags & (Inharmonic | SineHarmonics)) != 0 ? 3
                                                      : rng.nextInt (2) + 2;

    for (int i = 0; i < 3; ++i)
    {
        const int o = i * P::OscStride;
        const bool active = i < oscCount;

        setParam (state, P::Osc1Type + o, rng.nextInt (100) < pr.sawBias ? 1 : 0);
        setParam (state, P::Osc1Octave + o,
                  juce::jlimit (0, 4, pr.octave + (i == 2 ? pr.thirdOffset : 0)));
        setParam (state, P::Osc1Semitone + o, 12);

        // Detune stays small and deliberate. Wide random detune is what makes a
        // patch sound broken rather than rich.
        const int d = pr.detune.pick (rng);
        setParam (state, P::Osc1FineTune + o,
                  active ? kBi + (i == 1 ? d : (i == 2 ? -d : 0)) : kBi);

        setParam (state, P::Osc1MixLevel + o, active ? 85 + rng.nextInt (31) : 0);
        setParam (state, P::Osc1PulseWidth + o, pr.pulseWidth.pick (rng));
        setParam (state, P::Osc1Soften + o, pr.soften.pick (rng));
        setParam (state, P::Osc1Sync + o, 0);
    }

    if ((pr.flags & Inharmonic) != 0)
    {
        setParam (state, P::Osc1Semitone + P::OscStride, 12 + 5 + rng.nextInt (5));
        setParam (state, P::Osc1Semitone + 2 * P::OscStride, 12 + 14 + rng.nextInt (6));
    }

    // A sine-like fundamental plus two quiet, deliberately harmonic overtones
    // instead of the usual detuned stack - an octave up (osc2) and an octave
    // plus a fifth up (osc3), each well below the fundamental in level.
    if ((pr.flags & SineHarmonics) != 0)
    {
        setParam (state, P::Osc1Semitone + P::OscStride, 12 + 12);
        setParam (state, P::Osc1Semitone + 2 * P::OscStride, 12 + 19);
        setParam (state, P::Osc1MixLevel + P::OscStride, 30 + rng.nextInt (20));
        setParam (state, P::Osc1MixLevel + 2 * P::OscStride, 10 + rng.nextInt (15));
    }

    if ((pr.flags & RingMod) != 0)
        setParam (state, P::Ring1x3Level, 50 + rng.nextInt (55));

    // Classic FM: simple whole-number ratios between modulator and carrier, and
    // a struck envelope on the modulator index - that is the whole DX trick.
    if ((pr.flags & FmVoice) != 0)
    {
        setParam (state, P::FM1x3, 1);
        if (rng.nextBool())
            setParam (state, P::FM2x3, 1);

        static const int ratios[] = { 0, 7, 12, 12, 19, 24 };
        const int numRatios = (int) (sizeof (ratios) / sizeof (ratios[0]));

        setParam (state, P::Osc1Semitone, 12 + ratios[rng.nextInt (numRatios)]);
        setParam (state, P::Osc1Semitone + P::OscStride, 12 + ratios[rng.nextInt (numRatios)]);
        setParam (state, P::Osc1Semitone + 2 * P::OscStride, 12);

        // The modulators are heard only through oscillator 3.
        setParam (state, P::Osc1MixLevel, 0);
        setParam (state, P::Osc1MixLevel + P::OscStride, 0);
        setParam (state, P::Osc1MixLevel + 2 * P::OscStride, 100 + rng.nextInt (28));

        // Envelope 2 shapes the modulator level, which is the FM "index".
        setParam (state, P::Osc1MixEnv2, kBi + 25 + rng.nextInt (30));
    }

    if ((pr.flags & BreathNoise) != 0)
    {
        setParam (state, P::NoiseMixLevel, 8 + rng.nextInt (22));
        setParam (state, P::NoiseSoften, 30 + rng.nextInt (35));
    }

    if (rng.nextFloat() < pr.syncChance)
    {
        setParam (state, P::Osc1Sync + P::OscStride, 45 + rng.nextInt (61));
        setParam (state, P::Osc1SyncSkew + P::OscStride, rng.nextInt (81));
        setParam (state, P::Osc1FormantWidth + P::OscStride, 30 + rng.nextInt (71));

        // A slow envelope on sync amount is what makes a sync lead sing.
        if (character == SyncLead)
            setParam (state, P::Osc1SyncEnv2 + P::OscStride, kBi + 20 + rng.nextInt (35));
    }

    // Noise is only ever a seasoning, except in the FX characters.
    if ((pr.flags & BreathNoise) == 0)
    setParam (state, P::NoiseMixLevel, character == NoiseFX     ? 70 + rng.nextInt (50)
                                     : character == Atmosphere  ? rng.nextInt (35)
                                     : character == Pluck       ? rng.nextInt (25)
                                                                : 0);
    setParam (state, P::NoiseSoften, 60 + rng.nextInt (68));

    // ---------------------------------------------------------------- filter
    int type = 0;

    if ((pr.flags & ForceSplit) != 0 || rng.nextFloat() < pr.splitChance)
    {
        type = 7 + rng.nextInt (5);                 // the split / formant pairs
        setParam (state, P::FilterWidth, 15 + rng.nextInt (60));
    }
    else if (character == NoiseFX)
    {
        type = 1 + rng.nextInt (6);
    }

    setParam (state, P::FilterType, type);
    setParam (state, P::FilterSlope, pr.slope);
    setParam (state, P::FilterCutoff, pr.cutoff.pick (rng));
    setParam (state, P::FilterResonance, pr.reso.pick (rng));
    setParam (state, P::FilterQNormalise, (pr.flags & ResSweep) != 0 ? 90 + rng.nextInt (38)
                                                                     : 50 + rng.nextInt (61));
    setParam (state, P::FilterOverdrive, pr.overdrive.pick (rng));
    setParam (state, P::FilterTracking, kBi + 10 + rng.nextInt (21));

    // ---------------------------------------------------------------- envelopes
    setEnv (state, P::Env1Attack, pr.ampA, pr.ampD, pr.ampS, pr.ampR, rng);
    setEnv (state, P::Env2Attack, pr.fltA, pr.fltD, pr.fltS, pr.fltR, rng);
    setEnv (state, P::Env3Attack, { 20, 90 }, { 40, 100 }, { 40, 110 }, { 40, 90 }, rng);

    setParam (state, P::FilterFreqEnv2, kBi + pr.envDepth.pick (rng));

    setParam (state, P::Env1Velocity, (pr.flags & (ArpOn | OneShotUp)) != 0 ? 20 + rng.nextInt (40)
                                                                            : 40 + rng.nextInt (56));

    // A one-shot gesture should not fall back when the key is released.
    if ((pr.flags & OneShotUp) != 0)
    {
        setParam (state, P::Env1SustainRate, kBi + 25 + rng.nextInt (30));
        setParam (state, P::Env2SustainRate, kBi + 30 + rng.nextInt (34));
    }

    // Looping attack/decay gives rhythmic movement without an arpeggiator.
    if ((pr.flags & FastRepeat) != 0 && rng.nextBool())
        setParam (state, P::Env3ADRepeat, 2 + rng.nextInt (6));

    // ---------------------------------------------------------------- LFOs
    for (int i = 0; i < 2; ++i)
    {
        const int l = i * P::LfoStride;
        setParam (state, P::LFO1Type + l, rng.nextInt (4));
        setParam (state, P::LFO1Range + l, 1);
        setParam (state, P::LFO1Speed + l, 20 + rng.nextInt (60));
        setParam (state, P::LFO1Delay + l, rng.nextInt (45));
        setParam (state, P::LFO1Soften + l, rng.nextInt (80));
    }

    if ((pr.flags & PwmLfo) != 0)
    {
        // Opposing pulse-width movement between two oscillators is the classic
        // slow shimmer, and it needs square waves to be audible.
        setParam (state, P::LFO1Type, 2);                      // triangle
        setParam (state, P::LFO1Speed, 12 + rng.nextInt (28));
        setParam (state, P::Osc1WidthLFO1, kBi + 12 + rng.nextInt (28));
        setParam (state, P::Osc1WidthLFO1 + P::OscStride, kBi - 12 - rng.nextInt (28));
    }

    if ((pr.flags & Vibrato) != 0)
    {
        setParam (state, P::LFO1Type, 2);

        if (character == Flute)
        {
            setParam (state, P::LFO1Delay, 55 + rng.nextInt (30));
            setParam (state, P::LFO1FadeMode, 1);
            setParam (state, P::LFO1Speed, 40 + rng.nextInt (20));
        }

        setParam (state, P::Osc1PitchLFO1, kBi + 2 + rng.nextInt (6));
        setParam (state, P::Osc1PitchLFO1 + P::OscStride, kBi + 2 + rng.nextInt (6));
    }

    if ((pr.flags & FilterLfo) != 0)
    {
        const int l2 = P::LfoStride;
        setParam (state, P::LFO1Type + l2, character == WobbleBass ? rng.nextInt (2) + 1 : 2);
        setParam (state, P::LFO1Speed + l2, character == WobbleBass ? 35 + rng.nextInt (35)
                                                                    : 8 + rng.nextInt (34));
        // Tempo-sync the wobble so it sits in the track rather than beside it.
        if (character == WobbleBass)
            setParam (state, P::LFO1Sync + l2, 4 + rng.nextInt (7));

        setParam (state, P::FilterFreqLFO2, kBi + 18 + rng.nextInt (35));
    }

    // Deep, slow envelope 3 sweep at high resonance - the filter as the sound.
    if ((pr.flags & ResSweep) != 0)
    {
        setParam (state, P::FilterFreqEnv3, kBi + 35 + rng.nextInt (28));
        setParam (state, P::Env3Attack, 70 + rng.nextInt (40));
        setParam (state, P::Env3Decay, 80 + rng.nextInt (40));
        setParam (state, P::Env3Sustain, 90 + rng.nextInt (38));
        setParam (state, P::FilterResEnv3, kBi + rng.nextInt (25));
    }

    // The "sweep down on release" effect: envelope 3 snaps up almost
    // instantly, holds near its peak for as long as the note is held (so the
    // pitch sits still while you play), then eases back down over a long,
    // curved release. Routing it into every oscillator's pitch by the same
    // small amount keeps the octave/fifth overtones in tune with the
    // fundamental all the way through the bend, rather than the tuning
    // smearing as it falls.
    if ((pr.flags & SineHarmonics) != 0)
    {
        setEnv (state, P::Env3Attack, { 0, 3 }, { 0, 10 }, { 122, 127 }, { 75, 105 }, rng);

        const int bend = 4 + rng.nextInt (5);
        for (int i = 0; i < 3; ++i)
            setParam (state, P::Osc1PitchEnv3 + i * P::OscStride, kBi + bend);
    }

    // ---------------------------------------------------------------- voice
    setParam (state, P::UnisonVoices, pr.unison);
    setParam (state, P::UnisonDetune, pr.unison > 0 ? 18 + rng.nextInt (50) : 0);
    setParam (state, P::PortamentoTime, (pr.flags & Glide) != 0 ? 8 + rng.nextInt (30) : 0);

    // Glide Type: "Normal" glides on every note change regardless of legato,
    // which is exactly the character a monophonic bass/lead line wants (the
    // classic 303 slide). Anything meant to be played as chords gets "Auto"
    // instead, so a stray portamento time doesn't drag one voice's pitch
    // into another's when notes overlap - only true legato playing glides.
    const bool monoStyleCharacter = character == Bass || character == SubBass
                                  || character == AcidBass || character == TB303
                                  || character == WobbleBass || character == Lead
                                  || character == SyncLead;
    setParam (state, P::GlideType, monoStyleCharacter ? 0 /* Normal */ : 1 /* Auto */);

    // ---------------------------------------------------------------- arpeggiator
    const bool arpOn = (pr.flags & ArpOn) != 0;
    setParam (state, P::ArpEnabled, arpOn ? 1 : 0);

    if (arpOn)
    {
        if (character == TB303)
        {
            // A 303 line is fast, tight and mostly within an octave.
            setParam (state, P::ArpSync, 4 + rng.nextInt (3));       // 16ths and friends
            setParam (state, P::ArpGateTime, 30 + rng.nextInt (35)); // short, so it clicks
            setParam (state, P::ArpOctaveRange, rng.nextInt (2));
            setParam (state, 255 /* note ordering */, rng.nextInt (6));
        }
        else
        {
            setParam (state, P::ArpSync, 4 + rng.nextInt (7));
            setParam (state, P::ArpGateTime, 40 + rng.nextInt (61));
            setParam (state, P::ArpOctaveRange, rng.nextInt (3));
            setParam (state, 255 /* note ordering */, rng.nextInt (6));
        }
    }

    // ---------------------------------------------------------------- effects
    setParam (state, P::EffectsDryLevel, 127);
    setParam (state, P::EffectsBypass, 0);
    setParam (state, P::FxOrder, rng.nextInt (19));
    setParam (state, P::EffectsMorph, rng.nextInt (128));

    setParam (state, P::ReverbSendLevel, pr.reverb.pick (rng));
    setParam (state, P::ReverbDecay, (pr.flags & GatedVerb) != 0 ? 20 + rng.nextInt (45)
                                                                 : 45 + rng.nextInt (70));
    setParam (state, P::ReverbHFDamp, 35 + rng.nextInt (61));
    setParam (state, P::ReverbEarlyRef, 1 + rng.nextInt (5));
    setParam (state, P::ReverbType, (pr.flags & GatedVerb) != 0 && rng.nextBool()
                                        ? rng.nextInt (4) : 4 + rng.nextInt (12));

    setParam (state, P::DelaySendLevel, pr.delay.pick (rng));
    setParam (state, P::DelayFeedback, 25 + rng.nextInt (46));
    setParam (state, P::DelayHFDamp, 40 + rng.nextInt (56));
    setParam (state, P::DelayWidth, 50 + rng.nextInt (78));
    setParam (state, P::DelaySync, rng.nextFloat() < 0.7f ? 4 + rng.nextInt (8) : 0);
    setParam (state, P::DelayTime, 20 + rng.nextInt (71));
    setParam (state, P::DelayRatio, rng.nextInt (11));

    setParam (state, P::ChorusSendLevel, pr.chorus.pick (rng));
    setParam (state, P::ChorusType, character == Pad || character == WarmPad
                                        ? 0 : rng.nextInt (3));
    setParam (state, P::ChorusSpeed, 8 + rng.nextInt (38));
    setParam (state, P::ChorusModDepth, 35 + rng.nextInt (61));
    setParam (state, P::ChorusFeedback, rng.nextInt (56));
    setParam (state, P::ChorusStereoWidth, 70 + rng.nextInt (58));
    setParam (state, P::ChorusDelay, 20 + rng.nextInt (71));

    setParam (state, P::DistortionLevel, pr.dist.pick (rng));
    setParam (state, P::DistortionOutput, 100);
    setParam (state, P::DistortionGainComp, 80);

    setParam (state, P::EQBass, kBi + (pr.octave <= 1 ? 5 + rng.nextInt (14)
                                                      : -6 + rng.nextInt (17)));
    setParam (state, P::EQTreble, kBi + (-8 + rng.nextInt (24)));

    if ((pr.flags & CombFx) != 0)
    {
        setParam (state, P::CombBoost, 20 + rng.nextInt (61));
        setParam (state, P::CombFrequency, 10 + rng.nextInt (101));
        setParam (state, P::CombSpeed, 10 + rng.nextInt (71));
        setParam (state, P::CombDepth, 20 + rng.nextInt (81));
        setParam (state, P::CombSpread, 30 + rng.nextInt (81));
    }

    if ((pr.flags & AutoPan) != 0)
    {
        setParam (state, P::PanType, rng.nextInt (4));
        setParam (state, P::PanningSpeed, 20 + rng.nextInt (71));
        setParam (state, P::PanningDepth, 30 + rng.nextInt (81));
    }

    setParam (state, P::Pan, kBi);
    setParam (state, P::ProgramVolume, volumeFor ((int) character));
    setParam (state, P::MasterVolume, 127);
}

//==============================================================================
void RandomPreset::superRandomise (juce::AudioProcessorValueTreeState& state, juce::Random& rng)
{
    // Everything goes, within each parameter's own legal range.
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = kParams[i];

        if (auto* p = state.getParameter (d.id))
        {
            const int v = d.min + rng.nextInt (juce::jmax (1, d.max - d.min + 1));
            p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
        }
    }

    // --- the short list of guards, each one earning its place:

    // Something has to be making a sound.
    setParam (state, P::Osc1MixLevel, 70 + rng.nextInt (58));

    // An amplitude envelope that never opens, or never closes, is not a patch.
    setParam (state, P::Env1Attack, rng.nextInt (90));
    setParam (state, P::Env1Sustain, 40 + rng.nextInt (88));
    setParam (state, P::Env1Release, 10 + rng.nextInt (90));
    setParam (state, P::Env1SustainTime, 127);
    setParam (state, P::Env1SustainRate, kBi + (-6 + rng.nextInt (13)));

    // Keep the filter somewhere a signal can get through it.
    setParam (state, P::FilterCutoff, 45 + rng.nextInt (83));
    setParam (state, P::FilterResonance, rng.nextInt (110));
    setParam (state, P::FilterOscsBypass, rng.nextInt (40));

    // Runaway feedback is not an interesting accident, it is just a loud one.
    setParam (state, P::DelayFeedback, rng.nextInt (85));
    setParam (state, P::ChorusFeedback, rng.nextInt (80));
    setParam (state, P::CombBoost, rng.nextInt (75));

    // Audible, and not louder than everything else in the session.
    setParam (state, P::EffectsBypass, 0);
    setParam (state, P::EffectsDryLevel, 80 + rng.nextInt (48));
    setParam (state, P::ProgramVolume, 90 + rng.nextInt (25));
    setParam (state, P::MasterVolume, 118);
    setParam (state, P::Pan, kBi);

    // Audio-in oscillator types would be silent without an input connected.
    for (int i = 0; i < 3; ++i)
    {
        const int o = i * P::OscStride;
        const int type = rng.nextInt (3);
        setParam (state, P::Osc1Type + o, type == 2 ? 4 : type);   // square, saw or double saw
        setParam (state, P::Osc1BendRange + o, 14);
    }
}

//==============================================================================
juce::String RandomPreset::wildName (juce::Random& rng)
{
    static const char* first[] =
    {
        "Feral", "Unstable", "Rogue", "Broken", "Mutant", "Accidental", "Untamed",
        "Haywire", "Improbable", "Stray", "Volatile", "Wayward"
    };

    static const char* second[] =
    {
        "Tide", "Signal", "Specimen", "Anomaly", "Artefact", "Bloom", "Static",
        "Creature", "Weather", "Machine", "Reef", "Transmission"
    };

    const int numFirst  = (int) (sizeof (first)  / sizeof (first[0]));
    const int numSecond = (int) (sizeof (second) / sizeof (second[0]));

    return juce::String (first[rng.nextInt (numFirst)])
             + " " + juce::String (second[rng.nextInt (numSecond)]);
}

//==============================================================================
juce::String RandomPreset::nameFor (Character character, juce::Random& rng)
{
    static const char* prefixes[] =
    {
        "Tidal", "Lagoon", "Drift", "Reef", "Saline", "Current", "Fathom",
        "Azure", "Kelp", "Undertow", "Cove", "Shoal", "Brine", "Glacier",
        "Marina", "Trench", "Estuary", "Harbour", "Spindrift", "Nautilus"
    };

    const int index = juce::isPositiveAndBelow ((int) character, (int) Anything)
                        ? (int) character : 0;

    const int numPrefixes = (int) (sizeof (prefixes) / sizeof (prefixes[0]));

    return juce::String (prefixes[rng.nextInt (numPrefixes)])
             + " " + juce::String (kProfiles[index].kind);
}

} // namespace aquanova
