#include "AquaVibrioRandom.h"

#include <unordered_map>

namespace aquavibrio
{

namespace
{
    constexpr int kBi = 64;   // the centre of a bipolar 0..127 parameter

    /** An inclusive range the generator picks from. */
    struct R
    {
        int lo, hi;
        int pick (juce::Random& rng) const { return lo + rng.nextInt (juce::jmax (1, hi - lo + 1)); }
    };

    enum Flags : unsigned
    {
        None        = 0,
        Glide       = 1u << 0,
        MonoKey     = 1u << 1,
        ArpOn       = 1u << 2,
        FilterLfo   = 1u << 3,   // LFO 2 on cutoff
        PwmLfo      = 1u << 4,   // LFO 1 on pulse width - the pad shimmer
        Vibrato     = 1u << 5,   // LFO 3 on pitch
        AutoPan     = 1u << 6,
        HyperSaw    = 1u << 7,   // the stacked-saw model
        Wavetable   = 1u << 8,
        FmVoice     = 1u << 9,   // oscillator 1 phase-modulates oscillator 2
        SyncVoice   = 1u << 10,
        RingVoice   = 1u << 11,
        NoiseVoice  = 1u << 12,  // noise is the whole sound, not a seasoning
        SubHeavy    = 1u << 13,
        Punchy      = 1u << 14,
        Saturate    = 1u << 15,
        VowelBank   = 1u << 16,  // the filter bank's vowel filter
        CombBank    = 1u << 17,
        GranularFx  = 1u << 18,
        RiseUp      = 1u << 19,  // a long one-way upward gesture
        Inharmonic  = 1u << 20,  // non-octave oscillator intervals
        Octaves     = 1u << 21,  // oscillator 2 an octave up
        SlowSweep   = 1u << 22,  // deep slow filter envelope at high resonance
        KeyTrack    = 1u << 23   // filter follows the keyboard
    };

    struct Profile
    {
        const char* label;
        const char* kind;        // which name pool the patch name comes from

        int  semitone;           // oscillator transpose, in semitones
        int  sawBias;            // percent chance the oscillators sit saw-side
        R    detune;             // oscillator 2 fine detune
        R    pulseWidth;
        R    shape;              // 0 = the table wave, 127 = fully saw/pulse

        R    cutoff, reso;
        int  filterMode;         // index into filter1Mode, -1 = pick one
        int  routing;            // index into filterRouting
        R    envAmt;             // filter envelope depth

        R    ampA, ampD, ampS, ampR;
        R    fltA, fltD, fltS, fltR;

        R    sub, noise, ring, osc3;

        R    chorus, delay, reverb, dist, phaser;

        int  unison;             // 0 = off, else the voice count
        unsigned flags;
    };

    //==========================================================================
    // The whole personality of the generator lives in this table.
    //==========================================================================
    const Profile kProfiles[] =
    {
    // --- low end -------------------------------------------------------------
    { "Bass", "Bass", -12, 75, {0,6}, {40,80}, {70,127},
      {40,62}, {10,45}, 0, 0, {25,55},
      {0,0}, {55,85}, {70,110}, {20,45},
      {0,0}, {35,60}, {0,30}, {25,45},
      {30,70}, {0,10}, {0,0}, {0,0},
      {0,15}, {0,20}, {0,20}, {0,35}, {0,0}, 0, (MonoKey | Punchy | Saturate | KeyTrack) },

    { "Sub Bass", "Sub", -12, 30, {0,2}, {45,55}, {0,40},
      {32,52}, {5,20}, 0, 0, {10,35},
      {0,4}, {60,95}, {95,127}, {25,50},
      {0,0}, {40,65}, {20,50}, {25,45},
      {70,110}, {0,0}, {0,0}, {0,0},
      {0,10}, {0,12}, {0,20}, {0,20}, {0,0}, 0, (MonoKey | SubHeavy | Glide) },

    { "Acid Bass", "Acid", -12, 90, {0,3}, {45,70}, {90,127},
      {30,55}, {75,110}, 0, 0, {45,75},
      {0,0}, {40,70}, {10,50}, {18,35},
      {0,0}, {25,50}, {0,12}, {20,38},
      {0,30}, {0,8}, {0,0}, {0,0},
      {0,12}, {10,40}, {0,25}, {20,55}, {0,0}, 0, (MonoKey | Glide | Saturate | ArpOn) },

    { "Reese Bass", "Reese", -12, 95, {18,45}, {40,70}, {100,127},
      {35,58}, {20,55}, 0, 0, {20,45},
      {0,6}, {60,95}, {90,127}, {25,50},
      {0,0}, {40,70}, {40,80}, {30,50},
      {20,60}, {0,10}, {0,0}, {0,20},
      {10,45}, {0,25}, {0,25}, {10,45}, {0,25}, 2, (MonoKey | Saturate) },

    { "FM Bass", "FM", -12, 40, {0,4}, {45,60}, {0,50},
      {45,70}, {15,50}, 0, 0, {25,55},
      {0,0}, {50,80}, {60,100}, {20,40},
      {0,0}, {30,55}, {0,25}, {20,40},
      {20,60}, {0,8}, {0,0}, {0,0},
      {0,15}, {0,25}, {0,25}, {0,30}, {0,0}, 0, (MonoKey | FmVoice | Punchy) },

    { "Wobble Bass", "Wobble", -12, 85, {5,20}, {40,75}, {95,127},
      {35,60}, {55,95}, 0, 0, {35,70},
      {0,6}, {60,95}, {95,127}, {25,45},
      {0,0}, {45,70}, {60,100}, {25,45},
      {20,60}, {0,10}, {0,0}, {0,0},
      {0,20}, {0,25}, {0,25}, {15,50}, {0,0}, 0, (MonoKey | FilterLfo | Saturate) },

    // --- melodic -------------------------------------------------------------
    { "Lead", "Lead", 0, 70, {3,12}, {45,80}, {90,127},
      {70,100}, {25,65}, 0, 0, {20,50},
      {0,12}, {55,90}, {95,127}, {30,55},
      {0,20}, {45,75}, {50,90}, {30,55},
      {0,25}, {0,10}, {0,0}, {0,20},
      {20,60}, {20,55}, {20,55}, {0,25}, {0,20}, 3, (MonoKey | Glide | Vibrato) },

    { "Sync Lead", "Sync", 0, 60, {0,5}, {45,80}, {85,127},
      {75,105}, {20,55}, 0, 0, {30,65},
      {0,10}, {50,85}, {90,127}, {28,50},
      {0,15}, {40,70}, {30,70}, {28,50},
      {0,20}, {0,8}, {0,0}, {0,0},
      {10,45}, {15,50}, {15,45}, {0,30}, {0,20}, 0, (MonoKey | SyncVoice | Vibrato) },

    { "Psy Lead", "Psy", 0, 85, {4,16}, {40,75}, {100,127},
      {60,95}, {45,85}, 0, 0, {35,70},
      {0,8}, {45,75}, {60,110}, {22,42},
      {0,10}, {30,60}, {20,60}, {22,42},
      {0,25}, {0,12}, {0,20}, {0,25},
      {15,50}, {25,65}, {15,45}, {15,50}, {10,40}, 2, (MonoKey | Glide | ArpOn | Saturate) },

    { "Hoover", "Hoover", -12, 90, {20,50}, {40,70}, {100,127},
      {50,80}, {40,80}, 0, 0, {40,75},
      {0,10}, {55,85}, {85,120}, {30,50},
      {0,10}, {40,70}, {40,80}, {30,50},
      {10,45}, {0,10}, {0,0}, {0,20},
      {20,60}, {20,55}, {20,55}, {20,60}, {0,30}, 4, (HyperSaw | Glide | MonoKey) },

    { "Brass", "Brass", 0, 80, {3,10}, {45,70}, {85,127},
      {55,85}, {15,45}, 0, 0, {35,65},
      {8,25}, {50,85}, {85,120}, {30,55},
      {5,20}, {40,70}, {50,90}, {30,50},
      {0,20}, {0,10}, {0,0}, {0,20},
      {15,45}, {10,40}, {25,65}, {0,25}, {0,15}, 2, None },

    { "Strings", "Strings", 0, 85, {5,16}, {45,75}, {90,127},
      {60,90}, {10,40}, 0, 0, {20,45},
      {25,55}, {60,95}, {100,127}, {45,75},
      {15,40}, {50,80}, {70,110}, {40,70},
      {0,20}, {0,12}, {0,0}, {0,25},
      {30,70}, {10,40}, {35,80}, {0,15}, {0,25}, 4, (PwmLfo | AutoPan) },

    { "Organ", "Organ", 0, 25, {0,4}, {45,60}, {0,45},
      {70,105}, {5,30}, 0, 0, {5,25},
      {0,6}, {70,110}, {115,127}, {10,25},
      {0,0}, {50,80}, {90,127}, {15,30},
      {50,90}, {0,8}, {0,0}, {40,90},
      {25,65}, {0,25}, {15,45}, {0,25}, {0,20}, 0, (Octaves) },

    { "Clav", "Clav", 0, 75, {0,6}, {30,55}, {95,127},
      {60,90}, {35,70}, 0, 0, {40,70},
      {0,0}, {35,60}, {20,55}, {15,30},
      {0,0}, {22,45}, {0,15}, {15,30},
      {0,20}, {0,10}, {0,0}, {0,0},
      {10,40}, {10,40}, {10,35}, {10,40}, {10,40}, 0, (Punchy | KeyTrack) },

    { "Electric Piano", "Keys", 0, 20, {0,4}, {45,60}, {0,40},
      {60,90}, {5,30}, 0, 0, {20,45},
      {0,0}, {55,85}, {35,75}, {25,45},
      {0,0}, {40,65}, {10,40}, {25,45},
      {10,40}, {0,6}, {0,0}, {0,0},
      {25,60}, {10,35}, {20,55}, {0,20}, {0,15}, 0, (FmVoice | Punchy) },

    // --- sustained -----------------------------------------------------------
    { "Pad", "Pad", 0, 70, {6,20}, {40,80}, {70,127},
      {55,85}, {10,40}, 0, 0, {20,50},
      {45,85}, {70,110}, {100,127}, {70,105},
      {30,65}, {60,95}, {80,120}, {60,95},
      {0,25}, {0,15}, {0,0}, {0,25},
      {35,80}, {15,50}, {45,95}, {0,15}, {10,40}, 4, (PwmLfo | AutoPan | Vibrato) },

    { "Warm Pad", "Warm", 0, 80, {4,14}, {45,70}, {60,110},
      {45,72}, {8,35}, 0, 0, {15,40},
      {50,90}, {70,110}, {105,127}, {75,110},
      {35,70}, {60,95}, {90,127}, {65,100},
      {20,55}, {0,12}, {0,0}, {0,20},
      {40,85}, {10,40}, {50,100}, {0,12}, {10,35}, 3, (PwmLfo | AutoPan) },

    { "Glass Pad", "Glass", 12, 25, {3,12}, {45,65}, {0,45},
      {75,110}, {15,45}, 0, 0, {15,40},
      {40,80}, {70,110}, {95,127}, {70,105},
      {25,60}, {60,95}, {80,120}, {60,95},
      {0,15}, {0,20}, {0,25}, {0,30},
      {40,85}, {20,55}, {55,105}, {0,10}, {15,50}, 3, (Wavetable | AutoPan | Vibrato) },

    { "Choir", "Choir", 0, 50, {4,14}, {45,70}, {30,90},
      {55,85}, {20,50}, 0, 0, {15,40},
      {40,80}, {70,110}, {105,127}, {65,100},
      {30,65}, {60,95}, {90,127}, {60,95},
      {0,15}, {0,20}, {0,0}, {0,20},
      {35,75}, {10,40}, {50,100}, {0,10}, {10,35}, 3, (VowelBank | AutoPan) },

    { "Drone", "Drone", -12, 60, {8,30}, {40,80}, {60,127},
      {35,75}, {30,75}, -1, 0, {20,55},
      {60,100}, {80,120}, {110,127}, {80,120},
      {40,80}, {70,110}, {90,127}, {70,110},
      {30,80}, {0,35}, {0,35}, {0,40},
      {30,80}, {20,60}, {50,110}, {0,35}, {15,55}, 2, (SlowSweep | AutoPan | Inharmonic) },

    // --- struck and short ----------------------------------------------------
    { "Pluck", "Pluck", 0, 65, {2,10}, {40,70}, {70,127},
      {60,95}, {20,55}, 0, 0, {35,70},
      {0,0}, {35,62}, {0,30}, {20,40},
      {0,0}, {22,45}, {0,10}, {18,35},
      {0,25}, {0,20}, {0,0}, {0,20},
      {15,50}, {15,50}, {25,70}, {0,20}, {0,20}, 2, (Punchy | KeyTrack) },

    { "Bell", "Bell", 12, 15, {0,8}, {45,60}, {0,35},
      {75,110}, {10,40}, 0, 0, {15,40},
      {0,0}, {60,95}, {0,25}, {45,80},
      {0,0}, {40,70}, {0,20}, {35,65},
      {0,10}, {0,10}, {20,70}, {0,30},
      {10,40}, {15,50}, {40,90}, {0,10}, {0,20}, 0, (FmVoice | Inharmonic) },

    { "Metallic", "Metal", 0, 30, {5,25}, {40,70}, {0,60},
      {65,100}, {25,65}, -1, 2, {25,60},
      {0,0}, {50,85}, {10,45}, {40,75},
      {0,0}, {35,65}, {0,25}, {30,60},
      {0,15}, {0,25}, {40,95}, {0,35},
      {15,50}, {20,60}, {35,85}, {0,30}, {10,45}, 0, (RingVoice | CombBank | Inharmonic) },

    { "Stab", "Stab", 0, 85, {3,12}, {40,75}, {90,127},
      {55,85}, {30,65}, 0, 0, {40,75},
      {0,0}, {40,65}, {0,35}, {20,40},
      {0,0}, {25,48}, {0,10}, {18,35},
      {0,25}, {0,15}, {0,0}, {0,25},
      {15,50}, {20,55}, {25,65}, {10,45}, {0,25}, 3, (Punchy | Saturate) },

    { "Blip", "Blip", 12, 40, {0,8}, {35,70}, {40,110},
      {70,110}, {30,70}, -1, 0, {45,80},
      {0,0}, {20,42}, {0,15}, {12,28},
      {0,0}, {14,32}, {0,8}, {12,25},
      {0,15}, {0,25}, {0,30}, {0,25},
      {10,40}, {25,70}, {20,60}, {0,30}, {10,45}, 0, (Punchy | ArpOn) },

    // --- movement and effects ------------------------------------------------
    { "Arp", "Arp", 0, 70, {2,12}, {40,75}, {70,127},
      {60,95}, {25,60}, 0, 0, {35,70},
      {0,0}, {35,60}, {0,30}, {18,35},
      {0,0}, {24,48}, {0,15}, {18,35},
      {0,25}, {0,15}, {0,0}, {0,20},
      {15,50}, {25,70}, {25,70}, {0,30}, {0,25}, 2, (ArpOn | Punchy) },

    { "Reso Sweep", "Sweep", 0, 80, {4,16}, {40,75}, {85,127},
      {25,50}, {85,120}, 0, 0, {70,100},
      {10,35}, {70,110}, {100,127}, {50,85},
      {45,80}, {85,120}, {90,127}, {60,95},
      {0,25}, {0,20}, {0,0}, {0,20},
      {20,60}, {20,60}, {40,95}, {10,45}, {10,45}, 2, (SlowSweep | MonoKey) },

    { "Riser", "Riser", 0, 85, {8,30}, {40,80}, {90,127},
      {20,45}, {55,95}, 0, 0, {85,120},
      {60,100}, {90,127}, {115,127}, {40,75},
      {80,115}, {95,127}, {110,127}, {50,85},
      {0,25}, {20,60}, {0,25}, {0,25},
      {25,70}, {25,70}, {60,110}, {10,45}, {15,55}, 4, (RiseUp | SlowSweep) },

    { "Formant", "Vox", 0, 70, {3,14}, {40,75}, {70,127},
      {55,90}, {20,55}, 0, 0, {20,50},
      {15,50}, {60,95}, {90,127}, {40,75},
      {10,35}, {50,80}, {60,100}, {35,70},
      {0,20}, {0,20}, {0,0}, {0,25},
      {20,60}, {20,55}, {35,85}, {0,25}, {10,40}, 2, (VowelBank | FilterLfo) },

    { "Noise FX", "Noise", 0, 50, {0,20}, {40,80}, {40,110},
      {30,90}, {40,90}, -1, 0, {50,90},
      {20,70}, {60,110}, {60,120}, {40,90},
      {15,60}, {50,95}, {40,100}, {35,80},
      {0,10}, {95,127}, {0,30}, {0,20},
      {15,55}, {25,70}, {50,110}, {0,35}, {15,55}, 0, (NoiseVoice | FilterLfo) },

    { "Atmosphere", "Air", 0, 45, {6,25}, {40,80}, {30,100},
      {45,90}, {25,70}, -1, 0, {25,60},
      {55,100}, {80,120}, {100,127}, {80,120},
      {35,75}, {70,110}, {80,127}, {65,105},
      {0,20}, {15,50}, {0,30}, {0,30},
      {35,85}, {30,80}, {70,120}, {0,25}, {20,60}, 3, (AutoPan | GranularFx | SlowSweep) },

    { "Granular", "Grain", 0, 55, {3,15}, {40,75}, {40,110},
      {55,95}, {20,55}, 0, 0, {20,50},
      {20,60}, {70,110}, {95,127}, {60,100},
      {15,45}, {55,90}, {70,110}, {50,85},
      {0,20}, {0,30}, {0,25}, {0,25},
      {20,60}, {25,65}, {45,100}, {0,25}, {10,45}, 2, (GranularFx | AutoPan | Wavetable) }
    };

    static_assert (sizeof (kProfiles) / sizeof (kProfiles[0]) == (size_t) RandomPreset::Anything,
                   "every character needs a profile");

    //==========================================================================
    // Parameter access by id, through the table so ranges and enum offsets are
    // always the ones the plugin actually has.
    //==========================================================================
    const ParamInfo* infoFor (const juce::String& id)
    {
        static std::unordered_map<std::string, const ParamInfo*> map = []
        {
            std::unordered_map<std::string, const ParamInfo*> m;
            for (const auto& p : allParameters())
                m[std::string (p.id)] = &p;
            return m;
        }();

        const auto it = map.find (id.toStdString());
        return it != map.end() ? it->second : nullptr;
    }

    void setP (juce::AudioProcessorValueTreeState& state, const juce::String& id, int value)
    {
        const auto* info = infoFor (id);
        if (info == nullptr)
            return;

        auto* p = state.getParameter (id);
        if (p == nullptr)
            return;

        const int clamped = juce::jlimit (info->minValue, info->maxValue, value);

        // Enum parameters are AudioParameterChoice, indexed from zero; every
        // other one is an AudioParameterInt over the hardware range.
        const float target = info->scale == ValueScale::Enum ? (float) (clamped - info->minValue)
                                                             : (float) clamped;
        p->setValueNotifyingHost (p->convertTo0to1 (target));
    }

    void resetToDefaults (juce::AudioProcessorValueTreeState& state)
    {
        for (const auto& info : allParameters())
            setP (state, info.id, info.defaultValue);
    }

    // A steady patch volume per character, so flicking through the list does
    // not jump 12 dB between patches.
    int volumeFor (unsigned flags)
    {
        if ((flags & NoiseVoice) != 0) return 120;
        if ((flags & SubHeavy) != 0)   return 110;
        if ((flags & Saturate) != 0)   return 96;
        return 104;
    }
}

//==============================================================================
juce::StringArray RandomPreset::characterNames()
{
    juce::StringArray names;

    for (const auto& p : kProfiles)
        names.add (p.label);

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
    const unsigned f = pr.flags;

    resetToDefaults (state);

    //== oscillators ===========================================================
    const int model = (f & HyperSaw)  != 0 ? 1
                    : (f & Wavetable) != 0 ? 2
                                           : 0;

    for (int osc = 1; osc <= 2; ++osc)
    {
        const juce::String o ("Osc" + juce::String (osc));

        setP (state, o + "Mode", model);
        setP (state, o + "Shape", pr.shape.pick (rng));
        setP (state, o + "Pulsewidth", pr.pulseWidth.pick (rng));
        setP (state, o + "WaveSelect", rng.nextInt (100) < pr.sawBias ? 0 : rng.nextInt (24));
        setP (state, o + "Keyfollow", 96);

        if (model == 1)   // HyperSaw: the stack is the sound
        {
            setP (state, o + "HypersawDensity", 60 + rng.nextInt (68));
            setP (state, o + "HypersawDetunespread", 45 + rng.nextInt (60));
        }
        else if (model == 2)
        {
            setP (state, o + "WavetableWavetableselect", rng.nextInt (100));
            setP (state, o + "WavetableWavetableindex", rng.nextInt (128));
        }
    }

    // Pitch. Oscillator 2 is the one that moves: a fine detune for width, an
    // octave for weight, or a deliberately odd interval for the inharmonic
    // characters.
    setP (state, "Osc1Semitone", kBi + pr.semitone);

    int osc2Semi = pr.semitone;
    if ((f & Octaves) != 0)          osc2Semi += 12;
    else if ((f & Inharmonic) != 0)  osc2Semi += (rng.nextBool() ? 7 : 0) + (rng.nextBool() ? 5 : 14);
    else if (rng.nextInt (100) < 15) osc2Semi += rng.nextBool() ? 12 : 7;

    setP (state, "Osc2Semitone", kBi + osc2Semi);
    setP (state, "Osc2Detune", pr.detune.pick (rng));
    setP (state, "OscBalance", 48 + rng.nextInt (33));

    setP (state, "SuboscillatorVolume", pr.sub.pick (rng));
    setP (state, "SuboscillatorShape", rng.nextInt (100) < 70 ? 0 : 1);
    setP (state, "NoiseVolume", pr.noise.pick (rng));
    setP (state, "NoiseColor", kBi + rng.nextInt (50) - 25);
    setP (state, "RingmodulatorVolume", pr.ring.pick (rng));

    if (pr.osc3.hi > 0)
    {
        setP (state, "Osc3Mode", 1 + rng.nextInt (5));    // Slave, or one of the simple waves
        setP (state, "Osc3Volume", pr.osc3.pick (rng));
        setP (state, "Osc3Semitone", kBi + pr.semitone + (rng.nextBool() ? 0 : 12));
    }

    if ((f & RingVoice) != 0)
        setP (state, "RingmodulatorVolume", 60 + rng.nextInt (60));

    if ((f & FmVoice) != 0)
    {
        setP (state, "Osc2FmAmount", 45 + rng.nextInt (70));
        setP (state, "OscFmMode", rng.nextInt (3));
        setP (state, "FmFiltEnvAmt", kBi + 15 + rng.nextInt (40));   // the struck DX index
        setP (state, "Osc2Semitone", kBi + pr.semitone + (rng.nextBool() ? 12 : 7));
    }

    if ((f & SyncVoice) != 0)
    {
        setP (state, "Osc2Sync", 1);
        setP (state, "Osc2Semitone", kBi + pr.semitone + 5 + rng.nextInt (14));
        setP (state, "Osc2HsawFiltEnvSyncFreq", kBi + 10 + rng.nextInt (40));
    }

    if ((f & NoiseVoice) != 0)
    {
        setP (state, "OscMainvolume", 20 + rng.nextInt (30));   // oscillators down, noise up
        setP (state, "NoiseVolume", 100 + rng.nextInt (28));
    }

    if ((f & Saturate) != 0)
    {
        setP (state, "SaturationCurve", 1 + rng.nextInt (6));
        setP (state, "OscMainvolume", 75 + rng.nextInt (40));
    }

    setP (state, "UnisonMode", pr.unison);
    if (pr.unison > 0)
    {
        setP (state, "UnisonDetune", 20 + rng.nextInt (50));
        setP (state, "UnisonPanSpread", 60 + rng.nextInt (68));
    }

    //== filter ================================================================
    const int mode = pr.filterMode >= 0 ? pr.filterMode
                                        : rng.nextInt (100) < 60 ? 0 : rng.nextInt (8);

    setP (state, "Filter1Mode", mode);
    setP (state, "Filter2Mode", mode);
    setP (state, "FilterRouting", pr.routing);
    setP (state, "Cutoff", pr.cutoff.pick (rng));
    setP (state, "Filter1Resonance", pr.reso.pick (rng));
    setP (state, "Filter1EnvAmt", pr.envAmt.pick (rng));
    setP (state, "Filter2CutoffLink", 1);
    setP (state, "OffsetForFilterlink", kBi + rng.nextInt (24) - 12);
    setP (state, "Flt1EnvamtVelocity", kBi + rng.nextInt (30));

    if ((f & KeyTrack) != 0)
        setP (state, "Filter1Keyfollow", kBi + 20 + rng.nextInt (35));

    if ((f & SlowSweep) != 0)
    {
        setP (state, "Filter1Resonance", 85 + rng.nextInt (35));
        setP (state, "Filter1EnvAmt", 80 + rng.nextInt (45));
    }

    //== envelopes =============================================================
    setP (state, "AmpEnvAttack",  pr.ampA.pick (rng));
    setP (state, "AmpEnvDecay",   pr.ampD.pick (rng));
    setP (state, "AmpEnvSustain", pr.ampS.pick (rng));
    setP (state, "AmpEnvRelease", pr.ampR.pick (rng));
    setP (state, "FilterEnvAttack",  pr.fltA.pick (rng));
    setP (state, "FilterEnvDecay",   pr.fltD.pick (rng));
    setP (state, "FilterEnvSustain", pr.fltS.pick (rng));
    setP (state, "FilterEnvRelease", pr.fltR.pick (rng));
    setP (state, "AmpVelocity", kBi + 10 + rng.nextInt (40));

    if ((f & RiseUp) != 0)
    {
        // One long way up and no way back: a slow rise on both envelopes.
        setP (state, "AmpEnvAttack", 95 + rng.nextInt (25));
        setP (state, "FilterEnvAttack", 100 + rng.nextInt (27));
        setP (state, "AmpEnvSustain", 127);
        setP (state, "FilterEnvSustain", 127);
    }

    //== LFOs ==================================================================
    if ((f & FilterLfo) != 0)
    {
        setP (state, "Lfo2Rate", 25 + rng.nextInt (60));
        setP (state, "Lfo2Shape", rng.nextInt (100) < 70 ? rng.nextInt (2) : 4);
        setP (state, "Cutoff1Lfo2Amount", kBi + 20 + rng.nextInt (44));
        if (rng.nextBool())
            setP (state, "Lfo2Clock", 3 + rng.nextInt (4));   // in time with the host
    }

    if ((f & PwmLfo) != 0)
    {
        setP (state, "Lfo1Rate", 20 + rng.nextInt (40));
        setP (state, "Lfo1Shape", rng.nextInt (2));
        setP (state, "PwLfo1Amount", kBi + 15 + rng.nextInt (40));
        setP (state, "Lfo1Mode", 0);   // poly: each voice drifts on its own
    }

    if ((f & Vibrato) != 0)
    {
        setP (state, "Lfo3Rate", 55 + rng.nextInt (30));
        setP (state, "Lfo3Destination", 1);            // Osc 1 + 2 pitch
        setP (state, "OscLfo3Amount", 8 + rng.nextInt (22));
        setP (state, "Lfo3FadeInTime", 30 + rng.nextInt (50));
    }

    if ((f & AutoPan) != 0)
    {
        setP (state, "Lfo2Rate", 10 + rng.nextInt (35));
        setP (state, "PanLfo2Amount", kBi + 12 + rng.nextInt (35));
    }

    // One useful mod wheel route on every patch: the wheel opens the filter,
    // which is what a player reaches for first.
    setP (state, "Assign1Source", 3);                  // Mod Wheel
    setP (state, "Assign1Destination", 24);            // Filter 1 Cutoff
    setP (state, "Assign1Amount", kBi + 20 + rng.nextInt (35));

    //== effects ===============================================================
    setP (state, "ChorusType", 1 + rng.nextInt (4));
    setP (state, "ChorusMix", pr.chorus.pick (rng));
    setP (state, "ChorusRate", 30 + rng.nextInt (50));
    setP (state, "ChorusDepth", 20 + rng.nextInt (60));

    setP (state, "PhaserMix", pr.phaser.pick (rng));
    setP (state, "PhaserRate", 20 + rng.nextInt (50));

    setP (state, "DelayMode", 1 + rng.nextInt (5));
    setP (state, "DelaySend", pr.delay.pick (rng));
    setP (state, "DelayFeedback", 30 + rng.nextInt (55));
    setP (state, "DelayClock", 3 + rng.nextInt (5));   // musical by default
    setP (state, "DelayColor", kBi + rng.nextInt (40) - 20);

    setP (state, "ReverbMode", 1);
    setP (state, "ReverbSend", pr.reverb.pick (rng));
    setP (state, "ReverbType", rng.nextInt (4));
    setP (state, "ReverbTime", 40 + rng.nextInt (70));
    setP (state, "ReverbDamping", 20 + rng.nextInt (70));

    if (pr.dist.hi > 0)
    {
        setP (state, "DistortionCurve", 1 + rng.nextInt (8));
        setP (state, "DistortionIntensity", pr.dist.pick (rng));
        setP (state, "PatchDistortionMix", 70 + rng.nextInt (58));
    }

    if ((f & VowelBank) != 0)
    {
        setP (state, "FilterBankType", 3);             // Vowel Filter
        setP (state, "FilterBankFrequency", rng.nextInt (128));
        setP (state, "FilterBankMix", 70 + rng.nextInt (58));
    }
    else if ((f & CombBank) != 0)
    {
        setP (state, "FilterBankType", 4);             // Comb Filter
        setP (state, "FilterBankFrequency", 30 + rng.nextInt (70));
        setP (state, "FilterBankResonance", 50 + rng.nextInt (60));
        setP (state, "FilterBankMix", 60 + rng.nextInt (60));
    }

    if ((f & GranularFx) != 0)
    {
        setP (state, "Atomizer", 45 + rng.nextInt (60));       // granulator mix
        setP (state, "GranulatorGrains", 40 + rng.nextInt (80));
        setP (state, "GranulatorSize", 30 + rng.nextInt (70));
        setP (state, "GranulatorSpray", 20 + rng.nextInt (80));
        setP (state, "GranulatorStereo", 50 + rng.nextInt (78));
        if (rng.nextInt (100) < 30)
            setP (state, "GranulatorPitch", kBi + (rng.nextBool() ? 12 : -12));
    }

    if (rng.nextInt (100) < 35)
    {
        setP (state, "CharacterType", rng.nextInt (9));
        setP (state, "BassIntensity", 20 + rng.nextInt (60));
    }

    //== amp, keyboard and arpeggiator =========================================
    setP (state, "PatchVolume", volumeFor (f));
    setP (state, "PunchIntensity", (f & Punchy) != 0 ? 45 + rng.nextInt (60) : 0);
    setP (state, "KeyMode", (f & MonoKey) != 0 && rng.nextInt (100) < 70 ? 1 + rng.nextInt (4) : 0);
    setP (state, "PortamentoTime", (f & Glide) != 0 ? 25 + rng.nextInt (35) : 0);

    if ((f & ArpOn) != 0)
    {
        setP (state, "ArpMode", 1 + rng.nextInt (6));
        setP (state, "ArpClock", 4 + rng.nextInt (3));
        setP (state, "ArpOctaveRange", rng.nextInt (3));
        setP (state, "ArpNoteLength", 40 + rng.nextInt (60));
        setP (state, "ArpPatternSelct", rng.nextInt (100) < 60 ? 1 : 2 + rng.nextInt (62));
        setP (state, "ArpSwing", rng.nextInt (100) < 40 ? 20 + rng.nextInt (60) : 0);
    }
}

//==============================================================================
void RandomPreset::superRandomise (juce::AudioProcessorValueTreeState& state, juce::Random& rng)
{
    for (const auto& info : allParameters())
    {
        const int v = info.minValue + rng.nextInt (juce::jmax (1, info.maxValue - info.minValue + 1));
        setP (state, info.id, v);
    }

    // The few guards that keep it a sound rather than an accident: something
    // has to be audible, the envelope has to open, and the output has to come
    // back down to a sane level.
    setP (state, "PatchVolume", 70 + rng.nextInt (30));
    setP (state, "OscMainvolume", 50 + rng.nextInt (50));
    setP (state, "AmpEnvAttack", rng.nextInt (60));
    setP (state, "AmpEnvSustain", 60 + rng.nextInt (68));
    setP (state, "AmpEnvRelease", 20 + rng.nextInt (60));
    setP (state, "Cutoff", 60 + rng.nextInt (68));
    setP (state, "DistortionIntensity", rng.nextInt (50));
    setP (state, "Atomizer", rng.nextInt (60));
    setP (state, "DelayFeedback", rng.nextInt (80));
    setP (state, "ReverbSend", rng.nextInt (80));
    setP (state, "KeyMode", rng.nextInt (100) < 70 ? 0 : 1 + rng.nextInt (4));

    // Step bytes and the soft knob configuration are not sound; leaving them
    // random just makes patches unreadable.
    for (const auto& info : allParameters())
        if (juce::String (info.name).startsWith ("Step ") || juce::String (info.id).startsWith ("SoftKnob"))
            setP (state, info.id, info.defaultValue);
}

//==============================================================================
juce::String RandomPreset::nameFor (Character character, juce::Random& rng)
{
    static const char* prefixes[] =
    {
        "Tidal", "Lagoon", "Drift", "Reef", "Saline", "Current", "Fathom",
        "Azure", "Kelp", "Undertow", "Cove", "Shoal", "Brine", "Glacier",
        "Abyssal", "Coral", "Marine", "Vapour", "Aqua", "Deepwater"
    };

    const juce::String kind = juce::isPositiveAndBelow ((int) character, (int) Anything)
                                ? kProfiles[(int) character].kind : "Patch";

    return juce::String (prefixes[rng.nextInt (juce::numElementsInArray (prefixes))])
            + " " + kind + " " + juce::String (rng.nextInt (89) + 10);
}

juce::String RandomPreset::wildName (juce::Random& rng)
{
    static const char* words[] =
    {
        "Unstable", "Feral", "Broken Compass", "No Rules", "Wild Tide",
        "Rogue Wave", "Static Bloom", "Misfire", "Accident", "Sea Fret"
    };

    return juce::String (words[rng.nextInt (juce::numElementsInArray (words))])
            + " " + juce::String (rng.nextInt (899) + 100);
}

} // namespace aquavibrio
