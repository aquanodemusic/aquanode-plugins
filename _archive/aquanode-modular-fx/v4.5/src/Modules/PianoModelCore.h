#pragma once

#include "ModuleCore.h"

// PianoModelCore - the physical piano model behind Felt Piano, Mid Piano and
// Hard Piano: one engine, running in real time; each module brings its own
// voicing (the measured tables in its .cpp).
//
// Every note is played by a hammer and a set of strings:
//
//   HAMMER   a felt spring with a power-law stiffness (Hertz contact) and
//            Hunt-Crossley loss, thrown at the string at a speed set by the
//            velocity. Hardness changes the felt.
//   STRINGS  a stiff string as a bank of modes, f_n = n f1 sqrt(1 + B n^2)
//            (Inharm scales B), struck at Strike along its length, so modes
//            with a node there stay quiet. The hammer and the string talk to
//            each other sample by sample (4x oversampled) while they touch.
//   UNISONS  one, two or three strings per note (by register), detuned by
//            Unison and coupled through the bridge: per partial the coupled
//            system is diagonalised, which gives the prompt sound, the long
//            after-sound (Aftersound sets its share of the loss) and the beats.
//   BODY     a radiation curve on the bridge force (Brightness opens or
//            closes its top and hardens or softens the felt), a short case
//            "thump" from the hammer's blow, and a slow sympathetic bed
//            (Sympathy).
//   DAMPER   on release the felt damper lands with a short ramp and a soft
//            knock in the case; Damper 0 leaves the strings ringing.
//
// A voicing is what makes a model sound like one piano: its string scaling,
// hammer, losses, radiation curve and register levels at seven anchor keys,
// measured on a recording of that piano.
//
// After the hammer has left the string everything is linear, so the rest of
// the note runs as a bank of exactly damped complex oscillators (SIMD), and
// the coupled-string eigenproblems are solved a partial per sample DURING the
// contact - no CPU spike when the hammer lets go. Components that have died
// away are dropped as the note decays. Partials caps the mode count (CPU).
//
// Env In works like every generator's: patched, the envelope (an ADSR...)
// shapes the piano's output on top of its own physics; unpatched, the piano
// rings and is damped by itself. Pitch In follows the patch convention
// (semitones / 60); the strings are built for the pitch a note starts at and
// bend with it afterwards.
// Per-voice generator. Inputs: 0 = Pitch In, 1 = Env In, 2 = Add Midi In.
// Output: 0 = Audio Out.
namespace aquanode::piano
{
    struct Voicing
    {
        double anchors[7];                  // MIDI keys the tables are given at

        // strings
        double B[7], stretchCents[7];
        double length[7], density[7];       // m, kg/m
        double beta[7];                     // strike point, fraction of the length
        double sigma0[7];                   // fundamental loss, amplitude rate 1/s
        double sigmaF;                      // extra loss at 2 kHz (quadratic in f)
        int oneStringTop, twoStringTop;     // keys up to these have 1 / 2 strings
        double detune2[2], detune3[3];      // unison detune in cents at Unison 100 %

        // hammer
        double hammerMass[7], logK[7];
        double hertzP, hcAlpha;

        // radiation curve (dB at log-spaced frequencies) and register levels
        double eqF[8], eqDb[8];
        double regDb[7];

        // what Brightness +100 % does: the top of the radiation curve opens by
        // up to brightOpenDb (above brightFromHz, in dB per octave up to the
        // cap), the felt hardens by brightLogK nepers; -100 % mirrors it
        double brightOpenDbPerOct, brightOpenCapDb, brightFromHz, brightLogK;

        // body
        double bodyF[10], bodyGain[10];
        double bodyT60, bodySlowT60, bodySlowGain, bodyLevel;

        // the knock of the blow: band-passed noise from the action and the
        // board (band-pass at knockHz, then two low-pass poles at twice that),
        // decaying with knockDecay seconds (0 level = none)
        double knockLevel, knockHz, knockQ, knockDecay;

        // the click of the felt meeting the string: a shorter, bright burst
        // (band-pass at clickHz, Q 0.7), decaying with clickDecay seconds
        double clickLevel, clickHz, clickDecay;

        // damper and stereo; keys above damperTop have no damper (the top of
        // a real piano rings on when let go)
        double damperSigma0, damperSigmaF, damperRamp;
        int damperTop;
        double panRange;

        double outputDb;                    // overall level, so the models sit together
    };

}

class PianoModel : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pVolume = 0, pHardness, pStrike, pBright, pWidth,
        pDecay, pDamping, pAfter, pUnison, pInharm,
        pBody, pResonance, pDamper, pPartials, pVoices
    };

    static constexpr int kMaxPartials = 128;
    static constexpr int kMaxStrings = 3;
    static constexpr int kMaxComps = kMaxPartials * kMaxStrings;   // 384, a multiple of 8
    static constexpr int kNumBodyModes = 10;
    static constexpr int kBodyComps = 2 * kNumBodyModes;            // thump + slow bed
    static constexpr int kOversample = 4;
    static constexpr int kActiveModes = 24;                         // touch the hammer at 4x

    explicit PianoModel (const aquanode::piano::Voicing& voicing);
    ~PianoModel() override;

    // the voicing in use; replacing it is for the test harness, which fits
    // voicings with it (message thread, before playing)
    const aquanode::piano::Voicing& getVoicing() const { return voicing; }
    void setVoicing (const aquanode::piano::Voicing& v) { voicing = v; }

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override;
    void voiceVelocity (int v, float velocity01) override;
    double voiceTailSeconds() const override;

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs,
                             aquanode::StereoFrame* outputs) override;

    struct Strike;   // one hammer blow and its strings (PianoModelCore.cpp)

    // what the shared controls do, for the end of each model's description
    static juce::String controlsDescription();

    // the descriptor all three models share (type id, name and text differ)
    static aquanode::ModuleDescriptor makeDescriptor (const juce::String& typeId, const juce::String& name,
                                                      const juce::String& description, int sidebarOrder,
                                                      float defaultAfter, int defaultPartials);

private:
    struct VoiceInfo
    {
        int slot { 0 };            // which of the voice's two strikes is current
        int pendingNote { -1 };    // struck on the next sample (needs Pitch In)
        float velocity { 0.7f };
    };

    void startStrike (Strike& st, int note, float velocity, double semis);
    void renderStrike (Strike& st, double semis, float& outL, float& outR);

    double ip (double x, const double (&v)[7]) const;
    double ipl (double x, const double (&v)[7]) const;
    double eqGainDb (double f) const;

    aquanode::piano::Voicing voicing;
    std::unique_ptr<Strike[]> strikes;     // two per voice: a re-struck note lets the old one die
    VoiceInfo voices[aquanode::kMaxVoices];
    aquanode::ModuleVoicePool pool;
};
