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
#include "AquaVibrioNoteLayers.h"

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

    struct Coefs { float b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };

    void setCoefs (const Coefs& c) { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }
    void set (Type type, double sr, float freq, float q, float gainDb) { setCoefs (design (type, sr, freq, q, gainDb)); }

    static Coefs design (Type type, double sr, float freq, float q, float gainDb)
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

        return { (float) (B0 / A0), (float) (B1 / A0), (float) (B2 / A0), (float) (A1 / A0), (float) (A2 / A0) };
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
    ~Engine();

    void prepare (double sampleRate, int blockSize);
    void reset();

    void bindTo (juce::AudioProcessorValueTreeState& apvts);

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

    //== note layers ===========================================================
    // The manager publishes a new layout; the engine switches to it at the
    // start of the next block and reports which one it is using, so the
    // manager knows when older layouts can be deleted.
    void publishLayers (const NoteLayerSnapshot* next) { pendingLayers.store (next, std::memory_order_release); }
    juce::uint64 getLayersInUse() const { return layersInUse.load (std::memory_order_acquire); }

    // Effect chains beyond the base one cost memory (a five-second delay line,
    // the granulator's buffer ...), so they are created only once layers ask
    // for effects of their own, and recycled when their tails have died away.
    // Both jobs need allocation, so they happen here, on the message thread.
    void serviceChains();
    void requestChains (int count);
    int  getNumChains() const { return numChains.load (std::memory_order_acquire); }
    int  getChainsInUse() const;

    static constexpr int kPolyVoices = 16;
    static constexpr int kMaxChains = kPolyVoices + 2;   // every voice its own, the base, one spare

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

    //== block-rate settings ===================================================
    // Everything the per-sample code needs, already in musical units. One of
    // these exists per note layer (the base patch is layer slot 0).
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
        float delaySend, delayColour; int delayMode; bool delayTape; float delayMs;
        float reverbSend; bool reverbOn;
        float secondOut;
        float granMix;
    };

    struct ModSlot
    {
        Src source { Src::Off };
        std::array<Dest, 3> dest { Dest::None, Dest::None, Dest::None };
        std::array<float, 3> amount {};
    };

    // Module settings that used to be written once per block for every voice.
    // With layers they belong to a layer, so they are kept here and written
    // into the (shared, per-voice-state) modules whenever the voice being
    // rendered belongs to a different layer than the previous one.
    struct VoiceModuleSettings
    {
        float unison[2], detune[2], spread[2], mode[2], formant[2];
        float initPhase;
        bool  osc3Fixed; float osc3Wave, osc3Shape;
        float subWave;
        float filterType[2];
        float env3[5], env4[5];
        float lfoShape[3], lfoMode[3], lfoEnvMode[3], lfoKeytrigger[3];
        float lfoPhaseSpread;
    };

    // Module settings for one effects rack.
    struct FxSettings
    {
        float distTone, distHighCut; bool distToneOn;
        Biquad::Coefs distPre, distPost, charA, charB, eqLow, eqMid, eqHigh;
        float vowelReso, vowelSize, combFeedback;
        float vocBands, vocAttack, vocRelease, vocBright, vocCarrierHz, vocModHz,
              vocCarrierSpread, vocModSpread, vocCarrierQ, vocModQ;
        float widthWidth, widthBassMono;
        float phStages, phRate, phDepth, phCentre, phFeedback, phSpread;
        float chRate, chDepth, chDelay, chSpread, chVoices, chLfoShape;
        float dlTime, dlTap, dlModRate, dlModDepth, dlRatio, dlMode, tapeWow, tapeFlutter, tapeDrive;
        float rvRoom, rvDecay, rvDamping, rvPre, rvColour, rvFeedback;
        float grGrains, grSize, grPos, grSpray, grFreeze, grPitch, grPitchDisp, grStereo;
    };

    struct LayerState
    {
        int  uid { 0 };
        bool computed { false };
        bool fxSameAsBase { true };
        Block b {};
        std::array<ModSlot, 6> matrix {};
        VoiceModuleSettings vm {};
        FxSettings fx {};
    };

    //== voices ================================================================
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
        // (envelopes and LFOs rates): last sample's values, one sample late
        ModArray prevMod {};

        int slot { 0 };    // which note layer it plays
        int chain { 0 };   // which effect chain it feeds

        // Per-sample conversions (pitch to ratio, cutoff to Hz, pan angle to
        // gains ...) recomputed only when their input moves. Reset with the
        // voice; every new note starts empty.
        struct Memos
        {
            aquanode::Memo<float, float> lfoOctaves[3], detune, ratio1, ratio2, syncRatio, osc3Ratio,
                                         noiseLp, noiseHp, cutoff1, cutoff2, noteHz;
            aquanode::Memo<float, std::pair<float, float>> pan;
        } memo;
    };

    std::array<Voice, kPolyVoices> voices;
    juce::uint64 ageCounter { 0 };
    int lastVoice { -1 };           // newest voice
    float lastNote { 60.0f };       // where the next portamento starts from
    float lastVelocity { 0.8f };

    // Voices are rendered grouped by layer, so the shared modules are only
    // re-pointed at a layer's settings when the layer changes.
    std::array<int, kPolyVoices> order {};
    int numOrdered { 0 };
    bool orderDirty { true };
    int appliedSlot { -1 };
    void rebuildOrder();

    int  allocateVoice();
    void startVoice (int v, int note, float velocity, bool retriggerEnvelopes, bool glide);
    void releaseVoice (int v, float releaseVelocity);
    void renderVoice (int v, const LayerState& L, float& outL, float& outR, ModArray& modOut);
    float punchCoef { 0.999f };

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
    void readMatrix (std::array<ModSlot, 6>& matrix) const;

    std::array<Dest, 128> destForByte {};
    std::array<Src, 128>  srcForByte {};

    // Parameter table indices of the matrix slots and the arp's step grid.
    struct SlotIndices { int src { -1 }; int dest[3] { -1, -1, -1 }; int amt[3] { -1, -1, -1 }; };
    std::array<SlotIndices, 6> slotIndices {};
    struct StepIndices { int length { -1 }, velocity { -1 }, on { -1 }; };
    std::array<StepIndices, 32> stepIndices {};

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

    //== per-voice modules =====================================================
    // These keep separate state for every voice; the engine writes the
    // settings of the voice's layer into them just before processing it.
    std::unique_ptr<aquanode::SynthModule> osc1, osc2, osc3, subOsc, noise;
    std::unique_ptr<aquanode::SynthModule> filter1, filter2;
    std::unique_ptr<aquanode::SynthModule> envFilter, envAmp, env3, env4;
    std::unique_ptr<aquanode::SynthModule> lfo1, lfo2, lfo3;
    std::unique_ptr<aquanode::SynthModule> envFollow;   // the input follower: global

    std::array<VirusLfoModule*, 3> lfoTyped {};
    VirusEnvModule* ampEnvTyped { nullptr };

    // module parameter indices, resolved once
    struct OscIdx { int ratio, wave, shape, pw, sync, syncRatio, detune, spread, unison, mode, formant, volume, initPhase; };
    OscIdx o1 {}, o2 {}, o3 {};
    int subRatio { -1 }, subWave { -1 };
    struct FltIdx { int cutoff, resonance, type; };
    FltIdx f1 {}, f2 {};
    struct EnvIdx { int attack, decay, sustain, release, slope; };
    EnvIdx eIdx {};
    struct LfoIdx { int rate, symmetry, shape, mode, phaseSpread, envMode, keytrigger; };
    LfoIdx lIdx {};

    void applyVoiceModules (const LayerState& L);
    void forgetAppliedValues();
    std::array<float, 48> appliedValues {};   // what applyVoiceModules last wrote, slot by slot

    //== effect chains =========================================================
    // One complete effects rack. Chain 0 is the base patch's and is always
    // running; the others are lent to layers whose effects differ.
    struct FxChain
    {
        enum State { Clean = 0, InUse = 1, Dirty = 2 };
        std::atomic<int> state { Clean };

        std::unique_ptr<aquanode::SynthModule> distortion, phaser, chorus, delay, reverb, tapeWobble,
                                               vowelBank, combBank, vocoder, stereoWidth, granulator;
        FrequencyShifter freqShifter;
        Biquad eqLow, eqMid, eqHigh, charA, charB, distPre, distPost;
        OnePole delayColour;
        std::array<OnePole, 6> bankStages, bankStagesHp;
        OnePole bankBpLp;
        Biquad bankPeak;
        float lastPeakHz { -1.0f }, lastPeakReso { -1.0f };
        double bankRingPhase { 0.0 };
        float smoothedBankFreq { -1.0f };
        int delayIdle { 1 << 30 }, reverbIdle { 1 << 30 }, granIdle { 1 << 30 };

        // binding (audio thread)
        int ownerUid { -1 };                 // the layer it belongs to; 0 is the base patch
        const LayerState* settings { nullptr };
        LayerState frozen {};                // the owner is gone: its last settings, for the tail
        bool orphan { false };
        int numVoices { 0 };
        int lastVoice { -1 };
        juce::uint64 lastVoiceAge { 0 };
        juce::int64 voicelessSince { 0 };
        int silentSamples { 0 };
        float dcL { 0.0f }, dcR { 0.0f };
        int fadeLeft { 0 };                  // > 0: being retired, fading out
        float busL { 0.0f }, busR { 0.0f }, otherL { 0.0f }, otherR { 0.0f };

        struct Memos
        {
            aquanode::Memo<float, float> bankHz, bankStereo, bankGL, bankGR, delayColour;
        } memo;

        void createModules();
        void prepare (double sampleRate);
        void reset();
    };

    std::array<std::unique_ptr<FxChain>, kMaxChains> chains;
    std::atomic<int> numChains { 0 };
    std::atomic<int> wantedChains { 1 };
    juce::CriticalSection chainLock;          // message thread vs prepare

    int distDrive { -1 }, phaserWet { -1 }, chorusWet { -1 }, combFreq { -1 }, vowelPos { -1 };

    int  chainForSlot (int slot);
    void bindChains();
    void applyFxModules (FxChain& c);
    void processChain (FxChain& c, bool isBase, float& l, float& r);
    void processFilterBank (FxChain& c, const Block& b, float& l, float& r, float freqMod, float resoMod);
    void retireChain (FxChain& c);
    juce::int64 sampleClock { 0 };

    float dcX[2] {}, dcY[2] {}, dcX2[2] {}, dcY2[2] {};
    juce::Random random;

    //== note layers ===========================================================
    std::atomic<const NoteLayerSnapshot*> pendingLayers { nullptr };
    std::atomic<juce::uint64> layersInUse { 0 };
    NoteLayerSnapshot defaultLayers;
    const NoteLayerSnapshot* layout { &defaultLayers };

    std::vector<LayerState> states;              // one per layout slot, preallocated
    std::vector<std::vector<float>> slotValues;  // the raw parameter values each slot plays
    std::vector<float> live;                     // the APVTS, read once per block
    std::vector<std::atomic<float>*> paramPtrs;
    const float* curValues { nullptr };          // what raw() reads while computing a slot

    const float* baseValues() const
    {
        return layout->sessionOpen && layout->stash.size() == live.size() ? layout->stash.data() : live.data();
    }

    const LayerState& base() const { return states[0]; }
    int  slotOf (int note) const { return layout->slotForNote[(size_t) juce::jlimit (0, 127, note)]; }
    void ensureComputed (int slot);
    void computeSettings (LayerState& L);
    void updateGlobals();
    void beginBlock();
    void markAllDirty();

    //== parameter access ======================================================
    // Keyed by the table's own static id strings, so a lookup is a hash of a
    // string_view: no std::string, no allocation.
    std::unordered_map<std::string_view, int> bindings;

    float raw (const char* id, float fallback = 0.0f) const
    {
        const auto it = bindings.find (std::string_view (id));
        return it != bindings.end() && curValues != nullptr ? curValues[it->second] : fallback;
    }

    float valueAt (int index, float fallback) const
    {
        return index >= 0 && curValues != nullptr ? curValues[index] : fallback;
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
