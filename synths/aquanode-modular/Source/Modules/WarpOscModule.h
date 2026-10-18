#pragma once

#include "ModuleCore.h"

// Warp Osc - a band-limited morphing oscillator with phase warping and
// unison: the general-purpose "modern synth" voice.
//
//   SHAPES  the Shape knob morphs Sine -> Triangle -> Saw -> Square -> Pulse.
//   CUSTOM  two single-cycle spectra (up to 64 harmonics each) stored with
//           the patch; Shape morphs from the first to the second. The genre
//           preset generators fill these from analysed source waves.
//   WARP    bends the read phase before the table is read:
//             Sync      hard-syncs a faster copy of the wave (1x..8x)
//             Bend      plays the cycle on a power curve
//             Squeeze   crushes the first half into a narrower window
//             Pulse     plays the cycle in a shrinking part of the period
//             Formant   a synced copy under a smooth window (vowel-ish)
//             Quantize  steps the phase (lo-fi)
//             Mirror    folds the cycle back on itself
//           Tables are chosen per note for the effective frequency, so the
//           warps stay clean of aliasing as far as the warp itself allows.
//   UNISON  up to 16 stacked copies, Detune is the total spread in cents,
//           Spread the stereo width, Blend the outer voices' level against
//           the centre. Stacked copies start at random phases.
//   MORPH   reshapes the wave's spectrum before it is played:
//             Random Amp  gives the harmonics random levels: at 0 % the
//                         wave is untouched; turning it up draws ever stronger
//                         random spectral landscapes over it - clusters of loud
//                         harmonics (formant-like) over quiet stretches, lifted
//                         towards the top - a new one every few percent,
//                         gliding smoothly between them. The landscapes never
//                         change, so the same amount always sounds the same. A
//                         cable (envelope, LFO) on Morph Amt is what makes it
//                         talk. The fundamental never drops below a third of
//                         its level, so the note keeps its pitch.
//
// Pitch In follows the patch convention (semitones / 60); FM In offsets the
// read phase by its value in cycles (true phase modulation, like
// Oscillator); Shape In moves the Shape knob by In Amt. Env In works like
// every generator's: patched, it is the amplitude; unpatched, a gate.
// Per-voice generator. Inputs: 0 = Pitch In, 1 = FM In, 2 = Shape In,
// 3 = Env In, 4 = Add Midi In. Output: 0 = Audio Out.
class WarpOscModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pVolume = 0, pShape, pWarp, pTranspose, pFine,
        pWave, pWarpMode, pUnison,
        pDetune, pSpread, pBlend, pShapeMod, pVoices, pGlide,
        pMorphType, pMorph
    };

    enum MorphType { morphNone = 0, morphRandomAmp };

    enum WarpMode { warpOff = 0, warpSync, warpBend, warpSqueeze, warpPulse, warpFormant, warpQuantize, warpMirror };

    static constexpr int kTableSize = 2048;
    static constexpr int kNumMips = 11;            // 1024 .. 1 harmonics
    static constexpr int kMaxUnison = 16;
    static constexpr int kCustomHarmonics = 64;
    // the morph is stored as tables at kMorphKnots evenly spaced amounts and
    // read between the two around the knob (linear between them, exactly as
    // if the harmonic levels themselves were interpolated)
    static constexpr int kMorphKnots = 9;

    WarpOscModule();

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override;
    void reset() override;
    void voiceReset (int v) override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override;
    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.05; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // the custom spectra: "warp1;A:re,im,...;B:re,im,..." (two-sided complex
    // Fourier coefficients of harmonics 1..64; B optional)
    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

    // generators set the custom frames directly (message thread)
    void setCustomSpectra (const std::vector<std::complex<float>>& frameA,
                           const std::vector<std::complex<float>>& frameB);

    // one mip-mapped table set
    struct TableSet
    {
        std::array<std::array<float, kTableSize + 1>, kNumMips> mip;
    };

    // a wave at every morph knot (knot 0 = the plain wave)
    using MorphSets = std::array<std::unique_ptr<TableSet>, kMorphKnots>;

    // the Random Amp level of harmonic h (>= 1) at a knot
    static float randomAmpGain (int harmonic, int knot);
    // coefficients with a knot's levels applied
    static std::vector<std::complex<float>> applyRandomAmp (const std::vector<std::complex<float>>& c, int knot);
    static void buildMorphSets (MorphSets& sets, const std::vector<std::complex<float>>& coefs);

private:
    void updateUnisonLayout();
    static void buildTableSet (TableSet& set, const std::vector<std::complex<float>>& coefs, bool normalise);

    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;

    double phase[aquanode::kMaxVoices][kMaxUnison] {};

    // unison layout, rebuilt when its knobs move
    int layoutCount { -1 };
    float layoutDetune { -1.0f }, layoutSpread { -1.0f }, layoutBlend { -1.0f };
    double ratio[kMaxUnison] {};
    float gainL[kMaxUnison] {}, gainR[kMaxUnison] {};

    // custom frames: double-buffered so a load never tears a running read
    MorphSets custom[2][2];                        // [buffer][frame A/B][knot]
    std::atomic<int> activeCustom { 0 };
    std::vector<std::complex<float>> customA, customB;
    juce::Random rng;
};
