#pragma once
//==============================================================================
//  AquaVibrioEngine.h
//
//  The voice engine. Almost nothing here is new DSP - the oscillators,
//  filters, envelopes and LFOs are Aquanode modules, driven per voice through
//  their own `processVoiceSample` interface. What this file does is wire them
//  into the Virus's fixed architecture and translate 0..127 hardware values
//  into the units those modules speak.
//
//  Signal path per voice:
//
//      Osc1 ─┐
//      Osc2 ─┼─ mixer ─ Filter 1 ─ Filter 2 ─ amp env ─ pan ─ out
//      Sub  ─┤          (serial, parallel or split)
//      Noise┘
//
//  Modules used, all from the Aquanode module set unchanged:
//      osc.drift          x2   oscillators (unison stack gives us HyperSaw)
//      filter.aquafilter  x2   Filter 1 and Filter 2
//      util.adsr          x4   filter, amp, env 3, env 4
//      util.lfo           x3   LFO 1..3
//==============================================================================

#include <JuceHeader.h>
#include "Aquanode/ModuleCore.h"
#include "AquaVibrioParameters.h"
#include "AquaVibrioExtras.h"
#include "Modules/VirusOscModule.h"
#include "Modules/VirusEnvModule.h"
#include "Modules/VirusLfoModule.h"

namespace aquavibrio
{

//==============================================================================
// Hardware value (0..127) to musical unit. The Virus stores everything as a
// byte; every module wants Hz, milliseconds or cents.
//==============================================================================
namespace scaling
{
    inline float unit (int v)            { return juce::jlimit (0.0f, 1.0f, (float) v / 127.0f); }
    inline float bipolar (int v)         { return juce::jlimit (-1.0f, 1.0f, ((float) v - 64.0f) / 64.0f); }

    // Cutoff, MEASURED from the hardware. Noise through the filter, each
    // spectrum divided by the same patch with the filter wide open, and the
    // -3 dB point read off the ratio:
    //
    //   byte  32 -> 140 Hz    64 ->  808 Hz    96 -> 4565 Hz
    //   byte  48 -> 355 Hz    80 -> 1895 Hz   112 -> 10.9 kHz
    //
    // That is 26 Hz doubling every 12.9 bytes - close to the old guess in the
    // middle but about 20% low across most of the range, and wrong at the top
    // where the old curve ran out at 20 kHz early.
    inline float cutoffHz (float v01)
    {
        const float byte = juce::jlimit (0.0f, 1.0f, v01) * 127.0f;
        return juce::jlimit (20.0f, 20000.0f, 25.9f * std::pow (2.0f, byte / 12.9f));
    }

    // Envelope times, MEASURED from the hardware rather than guessed.
    //
    // Rendering single notes through the real firmware and timing how long
    // the amplifier took to reach 90% gives, after subtracting the analyser's
    // own 11.8 ms window: byte 32 -> 4 ms, 48 -> 16 ms, 64 -> 77 ms,
    // 80 -> 311 ms. That is a straight exponential, doubling every 7.65
    // bytes - not the cubic curve that was here before, which made byte 94 an
    // eight-second attack instead of about one second and turned most of the
    // factory bank into clicks.
    inline float envMs (int v)
    {
        return 0.22f * std::pow (2.0f, (float) juce::jlimit (0, 127, v) / 7.65f);
    }

    // 0.1 Hz .. 100 Hz
    inline float lfoRateHz (int v)       { return 0.1f * std::pow (1000.0f, unit (v)); }

    inline float semitones (int v)       { return (float) v - 64.0f; }
    inline float cents (int v)           { return bipolar (v) * 50.0f; }
    inline float ratioFromCents (float c){ return std::pow (2.0f, c / 1200.0f); }
}

//==============================================================================
class Engine
{
public:
    Engine();

    void prepare (double sampleRate, int blockSize);
    void reset();

    // Pulled once per block on the audio thread - no string lookups, the
    // pointers are resolved when the engine is built.
    void updateParameters();
    void bindTo (juce::AudioProcessorValueTreeState& apvts);

    // Optional second stereo output, or nullptr when the host has not enabled
    // that bus. Second Output Balance decides how the patch is split.
    void setSecondOutput (float* const* channels) { secondOut = channels; }

    void setTempo (double bpm)
    {
        hostTempoValid = bpm > 1.0;
        if (hostTempoValid)
            tempoBpm = bpm;
    }

    void noteOn (int midiNote, float velocity);
    void noteOff (int midiNote, float releaseVelocity = 0.0f);
    void allNotesOff();

    void renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

private:
    //== voices ================================================================
    struct Voice
    {
        int   note      { -1 };
        float velocity  { 0.0f };
        bool  held      { false };
        bool  active    { false };
        float releaseCountdown { 0.0f };   // samples left after note-off
        juce::uint64 age { 0 };
    };

    std::array<aquanode::SynthModule*, 31> allModules();
    void advanceArpeggiator();
    void advanceUserPattern();
    int  allocateVoice();
    void renderVoice (int v, float& outL, float& outR);
    void renderEffects (float& l, float& r);

    //== modulation matrix =====================================================
    // The hardware offers 124 destinations and 40 sources. We support the ones
    // the engine actually has something to modulate; the rest are read,
    // stored and saved, they simply do nothing yet. Both tables are built by
    // NAME at startup from the same choice lists the panel uses, so the byte
    // values in a patch can never drift out of step with our meanings.
    enum class Dest
    {
        None, PatchVolume, Panorama, OscBalance, OscVolume, SubVolume, NoiseVolume,
        RingModVolume, Osc3Volume,
        OscPitch, Osc1Pitch, Osc2Pitch, Osc1Shape, Osc2Shape, Osc2Detune,
        Cutoff1, Cutoff2, Reso1, Reso2, FilterBalance, Filter1EnvAmt, Filter2EnvAmt,
        Lfo1Rate, Lfo2Rate, Lfo3Rate, Lfo1Amount, Lfo2Amount, Lfo3Amount,
        AmpAttack, AmpDecay, AmpSustain, AmpRelease,
        FilterAttack, FilterDecay, FilterSustain, FilterRelease
    };

    // All 40 hardware sources. The MIDI controller ones share one path: the
    // engine keeps a live table of controller values and each source just
    // names the controller number it watches.
    enum class Src
    {
        Off, PitchBend, ChanPressure, ModWheel,
        Breath, Controller3, FootPedal, DataEntry, Balance, Controller9,
        Expression, Controller12, Controller13, Controller14, Controller15,
        Controller16, HoldPedal, PortamentoSwitch, SostenutoPedal,
        AmpEnv, FilterEnv, Env3, Env4,
        Lfo1Bi, Lfo2Bi, Lfo3Bi, Lfo1Uni, Lfo2Uni, Lfo3Uni,
        VelocityOn, VelocityOff, KeyFollow, RandomPerNote,
        Const1, Const10,
        Count
    };

    struct ModSlot
    {
        Src source { Src::Off };
        std::array<Dest, 3> dest { Dest::None, Dest::None, Dest::None };
        std::array<float, 3> amount { 0.0f, 0.0f, 0.0f };
    };

    struct ModTargets
    {
        float patchVolume { 0.0f }, panorama { 0.0f }, oscBalance { 0.0f };
        float oscVolume { 0.0f }, subVolume { 0.0f }, noiseVolume { 0.0f };
        float osc1Pitch { 0.0f }, osc2Pitch { 0.0f };
        float osc1Shape { 0.0f }, osc2Shape { 0.0f }, osc2Detune { 0.0f };
        float cutoff1 { 0.0f }, cutoff2 { 0.0f }, reso1 { 0.0f }, reso2 { 0.0f };
        float ringModVolume { 0.0f }, osc3Volume { 0.0f }, filterBalance { 0.0f };
        float filter1EnvAmt { 0.0f }, filter2EnvAmt { 0.0f };
        float lfo1Rate { 0.0f }, lfo2Rate { 0.0f }, lfo3Rate { 0.0f };
        float lfo1Amount { 0.0f }, lfo2Amount { 0.0f }, lfo3Amount { 0.0f };
        float ampAttack { 0.0f }, ampDecay { 0.0f }, ampSustain { 0.0f }, ampRelease { 0.0f };
        float filterAttack { 0.0f }, filterDecay { 0.0f }, filterSustain { 0.0f }, filterRelease { 0.0f };
    };

    void buildMatrixTables();
    void buildLfoDestTable();
    void readMatrix();
    void applyMatrix (const std::array<float, (size_t) Src::Count>& sources, ModTargets& t) const;

    std::vector<Dest> destForByte;   // 0..127 -> what we do with it
    std::vector<Dest> lfoDestForByte;   // the LFOs' own Assign Dest list
    std::vector<Src>  srcForByte;
    std::array<ModSlot, 6> matrix {};

    // live controller state, updated from MIDI
    float pitchBend { 0.0f }, channelPressure { 0.0f };
    // Live CC values, 0..1. Volume, balance and expression start where the
    // MIDI spec says they rest, not at zero - otherwise an instrument is
    // silent until something happens to send a controller.
    std::array<float, 128> controllers = [] {
        std::array<float, 128> c {};
        c[7]  = 1.0f;   // channel volume
        c[8]  = 0.5f;   // balance, centred
        c[11] = 1.0f;   // expression
        return c;
    }();
    std::array<float, aquanode::kMaxVoices> voiceRandom {};
    std::array<float, aquanode::kMaxVoices> lastAmpEnv {};
    std::array<float, aquanode::kMaxVoices> lfo3Fade {};   // LFO 3 fade-in progress, per voice
    std::array<float, aquanode::kMaxVoices> voiceReleaseVelocity {};

    // Keys the player is physically holding, as opposed to the notes the
    // arpeggiator is producing from them.
    std::array<bool, 128> heldKeys {};
    bool arpRunning { false };

    // User-pattern playback. Pattern 0 on the hardware is the 32-step user
    // pattern, which the arp module knows nothing about - so when it is
    // selected the engine runs its own step clock instead of the module's.
    bool userPattern { false };
    int  userStep { 0 };
    double userStepCounter { 0.0 };
    int  userHeldIndex { 0 };
    std::array<int, 8> userActiveNotes { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<double, 8> userNoteOffAt {};
    int  userActiveCount { 0 };

    // The plugin's audio input, kept for the vocoder and the input follower.
    float inputL { 0.0f }, inputR { 0.0f };
    float* const* secondOut { nullptr };
    float lastVelocity { 0.8f };

    // Output DC blocker state. Rectifying distortion curves, asymmetric
    // saturation and narrow pulses all leave an offset; the hardware's output
    // stage is AC coupled and ours has to be too.
    float dcX1L { 0.0f }, dcY1L { 0.0f }, dcX1R { 0.0f }, dcY1R { 0.0f };
    float followerValue { 0.0f };   // input follower's live level, a matrix source in waiting

    std::array<Voice, aquanode::kMaxVoices> voices;
    juce::uint64 ageCounter { 0 };
    int activeVoiceLimit { 16 };

    //== modules ===============================================================
    std::unique_ptr<aquanode::SynthModule> osc1, osc2;
    std::unique_ptr<aquanode::SynthModule> filter1, filter2;
    std::unique_ptr<aquanode::SynthModule> envFilter, envAmp, env3, env4;
    std::unique_ptr<aquanode::SynthModule> lfo1, lfo2, lfo3;
    std::unique_ptr<aquanode::SynthModule> osc3, subOsc, noise, ringMod;

    // Wavetable models of oscillators 1 and 2. Both exist all the time; which
    // pair is heard depends on each oscillator's Mode, so switching model
    // mid-note does not have to rebuild anything.
    std::unique_ptr<aquanode::SynthModule> wt1, wt2;

    // The arpeggiator is a MIDI note driver: it emits notes on its own clock
    // and the engine turns them into voices, exactly as it does for the
    // player's keyboard.
    std::unique_ptr<aquanode::SynthModule> arp;
    std::unique_ptr<aquanode::SynthModule> distortion, eq, phaser, chorus, delay, reverb;
    std::unique_ptr<aquanode::SynthModule> filterBank, combBank, vocoder;
    std::unique_ptr<aquanode::SynthModule> tapeWobble, bankRingMod, bankFilter;
    std::unique_ptr<aquanode::SynthModule> envFollow, stereoWidth, characterShaper;

    // The two things no module covers.
    FrequencyShifter freqShifter;
    Atomizer atomizer;

    // parameter indices inside those modules, resolved once
    struct OscIndices { int volume, fmRatio, waveform, unison, detune, spread, drift; };
    struct FltIndices { int cutoff, resonance, drive, modDepth, type; };
    struct EnvIndices { int attack, decay, sustain, release; };
    struct LfoIndices { int rate, waveform, offset, level; };

    OscIndices osc1Idx {}, osc2Idx {};
    FltIndices flt1Idx {}, flt2Idx {};
    EnvIndices envIdx {};
    LfoIndices lfoIdx {};

    //== cached patch values ===================================================
    // Raw 0..127 values, refreshed once per block so the per-sample loop never
    // touches the APVTS.
    struct Cached
    {
        int osc1Wave, osc1Shape, osc1Semi, osc1Density, osc1Spread, osc1Mode;
        int osc2Wave, osc2Shape, osc2Semi, osc2Detune, osc2Density, osc2Mode;
        int oscBalance, oscVolume, subVolume, noiseVolume;
        int flt1Cut, flt1Res, flt1EnvAmt, flt1KeyFol, flt1Mode;
        int flt2Cut, flt2Res, flt2EnvAmt, flt2KeyFol, flt2Mode;
        int filterRouting, saturation;
        int envFA, envFD, envFS, envFR;
        int envAA, envAD, envAS, envAR;
        int lfo1Rate, lfo1Shape, lfo1Amt, lfo1Dest;
        int lfo2Rate, lfo2Shape, lfo2Amt;
        int lfo3Rate, lfo3Shape, lfo3Amt;
        int unisonMode, unisonDetune, unisonSpread;
        int patchVolume, panorama, keyMode;
        int osc3Mode, osc3Volume, osc3Semi, osc3Detune;
        int noiseColor, ringModVolume;
        int distCurve, distIntensity, distMix;
        int eqLowGain, eqLowFreq, eqMidGain, eqMidFreq, eqMidQ, eqHighGain, eqHighFreq;
        int chorusMix, chorusRate, chorusDepth, chorusDelay, chorusFeedback;
        int phaserMix, phaserRate, phaserDepth, phaserFreq, phaserFeedback, phaserMode;
        int delaySend, delayTime, delayFeedback, delayColor, delayMode, delayClock;
        int reverbSend, reverbTime, reverbDamping, reverbType, reverbPredelay;
        int arpMode, arpPattern, arpOctaves, arpNoteLength, arpSwing, arpClock, arpHold;
        int filterBankType, filterBankFreq, filterBankReso, filterBankMix;
        int vocoderMode, vocoderBands, vocoderAttack, vocoderRelease, vocoderBalance;
        int osc1Interp, osc2Interp, osc1WtIndex, osc2WtIndex;
        int atomizerAmount, arpPatternLength;
        int transpose, portamento, benderUp, benderDown;
        int osc1KeyFol, osc2KeyFol, osc2FmAmount, oscFmMode, fmAmountVelocity;
        int filterBalance, filter1EnvPolarity, filter2EnvPolarity;
        int ampVelocity, panoramaVelocity, punchIntensity;
        int bassIntensity, bassTune;
        int osc1ShapeVel, osc2ShapeVel;
        int osc1Pw, osc2Pw, pwVelocity, osc2Sync, syncFreq;
        int osc1Formant, osc2Formant, osc1WtSelect, osc2WtSelect;
        int ampSustainTime, filterSustainTime;
        int characterType, characterIntensity, characterTune;
        int followerMode, followerLevel, followerAttack, followerRelease;
        int secondOutputBalance;
        int lfo1Osc1Amt, lfo1Osc2Amt, lfo1PwAmt, lfo1ResoAmt, lfo1FiltGainAmt;
        int lfo2FmAmt, lfo2Cutoff1Amt, lfo2Cutoff2Amt, lfo2PanAmt, lfo2ShapeAmt;
        int lfo1Clock, lfo2Clock, lfo3Clock;
        int lfo1KeyFol, lfo2KeyFol, lfo3KeyFol;
        int flt1EnvAmtVel, flt2EnvAmtVel, reso1Vel, reso2Vel;
        int filterLink, filterLinkOffset, filterKeytrackBase;
        int lfo1Symmetry, lfo2Symmetry, lfo3FadeIn, lfo2Dest, lfo2DestAmt, lfo3Dest;
        int lfo1Mode, lfo2Mode, lfo3Mode, lfo1EnvMode, lfo2EnvMode;
        int lfo1Keytrigger, lfo2Keytrigger, unisonLfoPhase, oscInitPhase;
        int osc2FiltEnvAmt, fmFiltEnvAmt, osc2FiltEnvPitch, osc2HsawSpread;
        int osc1WtSync, osc2WtSync, osc1WtDetune, osc2WtDetune, osc2HsawSyncFreq;
        int chorusType, chorusLfoShape, chorusXOver, chorusAmount, chorusDistance;
        int chorusMicAngle, chorusLowHighBal, chorusMix2, chorusSpeed;
        int delayType, delayLfoShape, delayRate, delayDepth;
        int tapeTime, tapeFeedback, tapeCentre, tapeBandwidth, tapeRatio;
        int reverbMode, reverbColor, reverbClock, reverbFeedback;
        int phaserSpread, distTreble, distHighCut, distTone;
        int vocCarrierFreq, vocModFreq, vocModOffset, vocCarrierQ, vocModQ;
        int vocCarrierSpread, vocModSpread, vocSpectralBalance, vocLink;
        int bankFreq2, bankVowelFreq, bankFilterFreq, bankStereoPhase;
        int bankFilterType, bankPoles, bankShapeL, bankShapeR, bankSlope;
        int inputMode, inputSelect, inputRingMod;
        int channelVolume, balance, partVolume, partDetune, cutoff2Offset;
        int benderScale, controlSmooth, clockTempo;
        int softKnob1Dest, softKnob2Dest, softKnob3Dest;
        int softKnob1Value, softKnob2Value, softKnob3Value;
    };

    Cached c {};

    // A raw pointer per parameter, looked up once. `raw()` reads the live
    // value without a lock or a string compare.
    struct Binding { std::atomic<float>* value { nullptr }; };
    std::unordered_map<std::string, Binding> bindings;

    bool hasBinding (const juce::String& id) const
    {
        auto it = bindings.find (id.toStdString());
        return it != bindings.end() && it->second.value != nullptr;
    }

    int raw (const char* id, int fallback = 0) const
    {
        auto it = bindings.find (id);
        return it != bindings.end() && it->second.value != nullptr
                 ? juce::roundToInt (it->second.value->load()) : fallback;
    }

    double sampleRate { 44100.0 };
    double tempoBpm { 120.0 };
    bool hostTempoValid { false };
    juce::Random random;   // audio thread only, for the noise generator

    // scratch for the module socket interface
    std::array<aquanode::StereoFrame, 4> in {}, out {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace aquavibrio
