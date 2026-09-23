#pragma once
//==============================================================================
//  AquaVibrioEngine.h
//
//  A Virus-style voice engine built from Aquanode modules plus a handful of
//  small hand-written DSP pieces. Patch compatibility with the hardware is a
//  non-goal: every knob is mapped for musical, predictable behaviour instead.
//
//  Signal path per voice:
//
//      Osc 1 ─┐                           ┌─ Filter 1 ─ Saturation ─ Filter 2 ─┐
//      Osc 2 ─┤                           │     (serial, parallel or split)    │
//      Osc 3 ─┼─ mixer ─ Osc Volume ──────┤                                    ├─ Amp ─ Pan
//      Sub   ─┤  (ring mod, noise, input) └────────────────────────────────────┘
//      Noise ─┘
//
//  Global rack (per sample):
//      Input ─ Distortion ─ Filter Bank ─ Vocoder ─ Character ─ EQ ─ Phaser
//            ─ Chorus ─ (+ Delay send) ─ (+ Reverb send) ─ Granulator ─ limiter
//
//  Real-time rules kept throughout: nothing on the per-sample path looks a
//  parameter up by string, compares strings or allocates. Parameters are read
//  once per block into plain numbers; module parameters that change per voice
//  are written by index.
//==============================================================================

#include <JuceHeader.h>
#include <string_view>
#include "Aquanode/ModuleCore.h"
#include "AquaVibrioParameters.h"
#include "AquaVibrioScaling.h"
#include "AquaVibrioExtras.h"
#include "Modules/VirusOscModule.h"
#include "Modules/VirusEnvModule.h"
#include "Modules/VirusLfoModule.h"

namespace aquavibrio
{

//==============================================================================
// A small RBJ biquad, used by the EQ, the Character stage and the filter bank.
//==============================================================================
struct Biquad
{
    float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 };
    float z1[2] {}, z2[2] {};

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

    float process (float x, int ch)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }

    enum class Type { LowShelf, HighShelf, Peak, LowPass, HighPass, BandPass };

    void set (Type type, double sr, float freq, float q, float gainDb)
    {
        freq = juce::jlimit (10.0f, (float) (sr * 0.45), freq);
        q = juce::jmax (0.05f, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * freq / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;

        switch (type)
        {
            case Type::Peak:
                B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
                A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case Type::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - s); A0 = (A + 1) + (A - 1) * cw + s;
                A1 = -2 * ((A - 1) + (A + 1) * cw);    A2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case Type::HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - s); A0 = (A + 1) - (A - 1) * cw + s;
                A1 = 2 * ((A - 1) - (A + 1) * cw);     A2 = (A + 1) - (A - 1) * cw - s; break;
            }
            case Type::LowPass:
                B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::HighPass:
                B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::BandPass:
                B0 = alpha; B1 = 0; B2 = -alpha;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
        }

        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
};

// Topology-preserving one-pole low pass: stable at any cutoff, cheap enough to
// cascade six deep for the Filter Bank's variable slopes.
struct OnePole
{
    float s[2] {};
    void reset() { s[0] = s[1] = 0.0f; }

    static float coef (float hz, double sr)
    {
        const float g = std::tan (juce::MathConstants<float>::pi
                                  * juce::jlimit (5.0f, (float) (sr * 0.45), hz) / (float) sr);
        return g / (1.0f + g);
    }

    float lp (float x, float G, int ch)
    {
        const float v = (x - s[ch]) * G;
        const float y = v + s[ch];
        s[ch] = y + v;
        return y;
    }
};

//==============================================================================
class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int blockSize);
    void reset();

    void bindTo (juce::AudioProcessorValueTreeState& apvts);
    void updateParameters();

    void setSecondOutput (float* const* channels) { secondOut = channels; }

    void setTempo (double bpm)
    {
        hostTempoValid = bpm > 1.0;
        if (hostTempoValid)
            hostTempo = bpm;
    }

    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote, float releaseVelocity = 0.0f);
    void allNotesOff();

    void renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    //== modulation matrix vocabulary ==========================================
    // Everything the matrix (and LFO 1/2's own Assign) can reach. Anything in
    // the hardware list that is not here is simply ignored.
    enum class Dest
    {
        None,
        PatchVolume, Panorama, Transpose,
        Osc1Shape, Osc1Pw, Osc1Pitch, Osc1WtIndex,
        Osc2Shape, Osc2Pw, Osc2Pitch, Osc2Detune, Osc2Fm, FiltEnvOsc2Pitch, FiltEnvFmSync, Osc2WtIndex,
        OscBalance, SubVolume, OscVolume, NoiseVolume, RingMod, Osc3Volume, Osc3Pitch, NoiseColor, Punch,
        Cutoff1, Cutoff2, Reso1, Reso2, Filter1EnvAmt, Filter2EnvAmt, FilterBalance,
        FiltAttack, FiltDecay, FiltSustain, FiltSlope, FiltRelease,
        AmpAttack, AmpDecay, AmpSustain, AmpSlope, AmpRelease,
        Lfo1Rate, Lfo1Contour, Lfo1Osc1, Lfo1Osc2, Lfo1Pw, Lfo1Reso, Lfo1FiltGain, Lfo1Amt,
        Lfo2Rate, Lfo2Contour, Lfo2Shape, Lfo2Fm, Lfo2Cutoff1, Lfo2Cutoff2, Lfo2Pan, Lfo2Amt,
        Lfo3Rate, Lfo3Amt,
        ChorusMix, DelaySend, ReverbSend, PhaserMix, DistIntensity, BankFreq, BankReso,
        Count
    };

    enum class Src
    {
        Off, PitchBend, ChanPressure, ModWheel, Breath, Controller3, FootPedal, DataEntry,
        Balance, Controller9, Expression, Controller12, Controller13, Controller14,
        Controller15, Controller16, HoldPedal, PortamentoSwitch, SostenutoPedal,
        AmpEnv, FilterEnv, Env3, Env4,
        Lfo1Bi, Lfo2Bi, Lfo3Bi, Lfo1Uni, Lfo2Uni, Lfo3Uni,
        VelocityOn, VelocityOff, KeyFollow, RandomPerNote, Const1, Const10,
        Count
    };

private:
    using ModArray = std::array<float, (size_t) Dest::Count>;

    //== voices ================================================================
    static constexpr int kPolyVoices = 16;

    struct Voice
    {
        int   note { -1 };
        float velocity { 0.0f };
        float releaseVelocity { 0.0f };
        bool  held { false };        // key (or arp) still down
        bool  sustained { false };   // key up, but the sustain pedal holds it
        bool  active { false };
        juce::uint64 age { 0 };

        float glideNote { 60.0f };   // current (sliding) pitch, in notes
        float punch { 0.0f };
        float random { 0.0f };
        float lfo3Fade { 0.0f };

        // per-voice DSP state
        float noiseLp[2] {}, noiseHp[2] {};
        float satHold[2] {}; int satCount { 0 };
        OnePole satFilter;

        // modulation that feeds things evaluated before the matrix is known
        // (envelopes and LFO rates): last sample's values, one sample late
        ModArray prevMod {};
    };

    std::array<Voice, kPolyVoices> voices;
    juce::uint64 ageCounter { 0 };
    int lastVoice { -1 };           // newest voice: drives the global FX destinations
    float lastNote { 60.0f };       // where the next portamento starts from
    float lastVelocity { 0.8f };

    int  allocateVoice();
    void startVoice (int v, int note, float velocity, bool retriggerEnvelopes, bool glide);
    void releaseVoice (int v, float releaseVelocity);
    void renderVoice (int v, float& outL, float& outR, ModArray& modOut);
    void processFilterBank (float& l, float& r, float freqMod, float resoMod);
    float punchCoef { 0.999f };
    int delayIdle { 1 << 30 }, reverbIdle { 1 << 30 };

    // key handling around the voices: mono note stack, hold, sustain pedal
    void keyDown (int note, float velocity);
    void keyUp (int note, float releaseVelocity);
    std::vector<int> monoStack;     // reserved in the constructor, no audio-thread growth
    std::array<bool, 128> keyIsDown {};
    bool sustainPedal { false };
    bool holdLatched { false };

    //== arpeggiator ===========================================================
    struct Arp
    {
        std::array<int, 64> held {};    // in the order they were pressed
        int numHeld { 0 };
        bool latched { false };         // Hold: keys are up but the pattern keeps going
        double samplesToNextStep { 0.0 };
        double samplesToGateOff { -1.0 };
        int step { -1 };                // position in the note sequence
        int patternStep { 0 };          // position in the rhythm pattern
        bool goingUp { true };
        std::array<int, 16> sounding {};
        int numSounding { 0 };
        juce::Random rng;
    };

    Arp arp;
    bool arpWasRunning { false };
    void arpKeyDown (int note);
    void arpKeyUp (int note);
    void arpAdvance();
    void arpReleaseSounding();
    int  arpBuildSequence (int* out) const;
    bool arpStepIsOn (int patternStep, float& velocityScale, float& lengthScale) const;

    //== matrix ================================================================
    void buildMatrixTables();
    void readMatrix();

    std::array<Dest, 128> destForByte {};
    std::array<Src, 128>  srcForByte {};

    struct ModSlot
    {
        Src source { Src::Off };
        std::array<Dest, 3> dest { Dest::None, Dest::None, Dest::None };
        std::array<float, 3> amount {};
    };
    std::array<ModSlot, 6> matrix {};

    struct SlotPointers { std::atomic<float>* src { nullptr }; std::atomic<float>* dest[3] {}; std::atomic<float>* amt[3] {}; };
    std::array<SlotPointers, 6> slotPointers {};

    struct StepPointers { std::atomic<float>* length { nullptr }; std::atomic<float>* velocity { nullptr }; std::atomic<float>* on { nullptr }; };
    std::array<StepPointers, 32> stepPointers {};

    // User-pattern steps, copied once per block
    std::array<float, 32> stepLength {}, stepVelocity {};
    std::array<bool, 32> stepOn {};
    int userPatternLength { 16 };

    //== live controller state =================================================
    float pitchBend { 0.0f }, channelPressure { 0.0f };
    std::array<float, 128> controllers {};
    float inputL { 0.0f }, inputR { 0.0f };
    float followerValue { 0.0f };
    float* const* secondOut { nullptr };

    //== modules ===============================================================
    std::unique_ptr<aquanode::SynthModule> osc1, osc2, osc3, subOsc, noise;
    std::unique_ptr<aquanode::SynthModule> filter1, filter2;
    std::unique_ptr<aquanode::SynthModule> envFilter, envAmp, env3, env4;
    std::unique_ptr<aquanode::SynthModule> lfo1, lfo2, lfo3;
    std::unique_ptr<aquanode::SynthModule> distortion, phaser, chorus, delay, reverb, tapeWobble;
    std::unique_ptr<aquanode::SynthModule> vowelBank, combBank, vocoder, envFollow, stereoWidth;

    std::array<VirusLfoModule*, 3> lfoTyped {};
    VirusEnvModule* ampEnvTyped { nullptr };

    FrequencyShifter freqShifter;
    // The granulator (fx.granulation): a rolling buffer of what just played,
    // resprayed as a cloud of windowed grains. It replaces the Atomizer,
    // whose slicer never audibly did anything.
    std::unique_ptr<aquanode::SynthModule> granulator;
    int granIdx[8] {};
    float granMix { 0.0f };
    int granIdle { 1 << 30 };

    // module parameter indices, resolved once
    struct OscIdx { int ratio, wave, shape, pw, sync, syncRatio, detune, spread, unison, mode, formant, volume; };
    OscIdx o1 {}, o2 {}, o3 {};
    int subRatio { -1 };
    struct FltIdx { int cutoff, resonance, type; };
    FltIdx f1 {}, f2 {};
    struct EnvIdx { int attack, decay, sustain, release, slope; };
    EnvIdx eIdx {};
    struct LfoIdx { int rate, symmetry; };
    LfoIdx lIdx {};
    int distDrive { -1 }, phaserWet { -1 }, chorusWet { -1 }, combFreq { -1 }, vowelPos { -1 };

    //== global DSP state ======================================================
    Biquad eqLow, eqMid, eqHigh, charA, charB, distPre, distPost;
    bool distToneOn { false };
    OnePole delayColour;
    std::array<OnePole, 6> bankStages, bankStagesHp;
    OnePole bankBpLp;
    Biquad bankPeak;
    float lastPeakHz { -1.0f }, lastPeakReso { -1.0f };
    double bankRingPhase { 0.0 };
    float dcX[2] {}, dcY[2] {};
    float smoothedBankFreq { -1.0f };
    juce::Random random;

    //== block-rate state ======================================================
    // Everything the per-sample code needs, already in musical units.
    struct Block
    {
        // oscillators
        int   osc1Mode, osc2Mode, osc3Mode;
        float osc1Semi, osc2Semi, osc3Semi, osc2DetuneCents, osc3DetuneCents;
        float osc1Track, osc2Track;
        float osc1Shape, osc2Shape, osc1ShapeVel, osc2ShapeVel;
        float osc1Pw, osc2Pw, pwVel;
        float osc1Wave, osc2Wave;                 // classic wave select, 0..63
        float osc1WtBase, osc2WtBase, osc1WtIdx, osc2WtIdx, osc1Interp, osc2Interp;
        float osc1FormantSpread, osc2FormantSpread;
        bool  osc2Sync;
        float syncFreq, syncEnv;                  // Sync Frequency knob 0..1, env amount -1..1
        float osc2EnvPitch;                       // knob, -1..1
        float fmAmount, fmEnv, fmVel; int fmMode;
        float balance, oscVolume, subVolume, noiseVolume, ringVolume, osc3Volume, noiseColour;

        // filters
        float cut1, cut2, res1, res2, env1, env2, env1Vel, env2Vel, res1Vel, res2Vel;
        float key1, key2, keyBase;
        float pol1, pol2;
        bool  link; float linkOffset;
        int   routing;           // 0 serial 4, 1 serial 6, 2 parallel, 3 split
        int   type1, type2;
        float filterBalance;
        int   satCurve;

        // envelopes (bytes, so the matrix can offset them)
        float fA, fD, fS, fSlope, fR, aA, aD, aS, aSlope, aR;
        float ampMs[3], filtMs[3];   // attack, decay, release in ms, before modulation

        // LFOs
        float lfoRateByte[3]; float lfoClockHz[3]; float lfoHz[3]; float lfoKeyFollow[3]; float lfoContour[2];
        float lfo1Osc1, lfo1Osc2, lfo1Pw, lfo1Reso, lfo1Gain, lfo1Assign; int lfo1Dest;
        float lfo2Shape, lfo2Fm, lfo2Cut1, lfo2Cut2, lfo2Pan, lfo2Assign; int lfo2Dest;
        float lfo3Amount, lfo3FadeSeconds; int lfo3Dest;

        // amp and common
        float patchVolume, pan, panVel, ampVel, punch;
        float transpose, bendUp, bendDown; bool bendExp;
        float glideCoef; bool glideOn;
        int   keyMode;           // 0 poly, 1..4 mono, 5 hold

        // arpeggiator
        int   arpMode, arpPattern, arpOctaves; float arpStepBeats, arpGate, arpSwing; bool arpHold;

        // effects
        int   inputMode, inputSelect; float inputRing;
        int   followerMode;
        int   distCurve; float distDrive, distMix;
        int   bankType; float bankFreq, bankReso, bankMix, bankPoles, bankSlope, bankStereo;
        int   vocoderMode; float vocoderBalance;
        int   charType; float charAmount;
        bool  eqOn;
        float phaserMix; int chorusType; float chorusMix;
        float delaySend, delayColour; int delayMode; bool delayTape;
        float reverbSend; bool reverbOn;
        float secondOut;
    };

    Block b {};

    //== parameter access ======================================================
    // Keyed by the table's own static id strings, so a lookup is a hash of a
    // string_view: no std::string, no allocation.
    std::unordered_map<std::string_view, std::atomic<float>*> bindings;

    float raw (const char* id, float fallback = 0.0f) const
    {
        const auto it = bindings.find (std::string_view (id));
        return it != bindings.end() && it->second != nullptr ? it->second->load() : fallback;
    }

    static float load (const std::atomic<float>* p, float fallback)
    {
        return p != nullptr ? p->load() : fallback;
    }

    double sampleRate { 44100.0 };
    double hostTempo { 120.0 };
    double tempoBpm { 120.0 };
    bool hostTempoValid { false };
    bool bound { false };

    std::array<aquanode::StereoFrame, 4> in {}, out {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace aquavibrio
