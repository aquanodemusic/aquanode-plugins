#pragma once

#include "ModuleCore.h"

// Chord Wavetable (typeId "osc.chordwavetable"; part of the
// violet Colorbass family) - a wavetable oscillator whose table is GENERATED as a stack
// of chord tones taken from the harmonic series: every frame is a sparse
// handful of harmonics whose pitches land on the tones of an extended chord
// above the played note (minor 9: harmonics 1 2 3 4 6 7 8 9 12 14 16 18 19
// 24 ...), falling off at a few dB per octave, sometimes with the
// fundamental left out. Keyframes differ in voicing, so Position moves the
// chord's emphasis around. A purely generic engine: nothing sampled.
//
// On top of the table sit the usual colour-bass tools:
//   * Unison with chord STACKS - the unison voices are not only detuned
//     copies, they are spread over a chord (Octave, Power, Major, Minor,
//     Harmonic series...), so a 16-voice minor stack two octaves up is the
//     classic glittery colour-bass top layer.
//   * Spectral Morph - Random Amp, Low Pass, High Pass or Phase Disperse,
//     pre-rendered as a second table axis so it can be swept per voice by an
//     LFO with no FFT on the audio thread (Random Amp is the shimmer).
//   * Level In - multiplies the output, for rhythmic chops from a
//     Multi LFO without giving up the ADSR on Env In.
//
// Same seed + Chord + Keys + Bright + Density + Morph type = the same table,
// so a patch stores a few numbers instead of audio.
//
// Inputs: 0 = Pos In, 1 = Morph In, 2 = Pitch In, 3 = Level In, 4 = Env In,
//         5 = Add Midi In. Output: 0 = Audio Out (stereo).
class ChordWavetableModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pChord, pRoll, pKeys, pMorphType,
                      pBright, pDensity, pPosition, pPosDepth, pTranspose,
                      pMorph, pMorphDepth, pPitchDepth, pUnison, pDetune,
                      pStack, pSpread, pVoices, pGlide };

    enum MorphType { mOff = 0, mRandomAmp, mLowPass, mHighPass, mPhaseDisperse, kNumMorphTypes };

    static constexpr int kBaseLength = 2048;
    static constexpr int kNumMips    = 6;                     // 2048 ... 64 samples per cycle
    static constexpr int kMipStride  = 2048 + 1024 + 512 + 256 + 128 + 64;
    static constexpr int kMaxUnison  = 16;

    struct Table
    {
        int numFrames { 0 };
        int numMorphs { 1 };
        std::vector<float> data;             // [frame][morph][mip...] packed, kMipStride per (frame, morph)
        juce::uint32 seed { 0 };
        int chord { 0 };                     // resolved chord (Auto resolved), index into chordChoices()-1
        int keys { 1 };                      // resolved keyframe count
        int chordSetting { 0 }, keysSetting { 0 }, morphType { 0 };
        int brightSetting { 0 }, densitySetting { 0 };
        int numHarmonics { 0 };              // harmonics in the first frame (for the display)

        const float* cycle (int frame, int morph, int mip) const
        {
            static const int mipOffset[kNumMips] = { 0, 2048, 3072, 3584, 3840, 3968 };
            return data.data() + ((size_t) (frame * numMorphs + morph) * kMipStride) + mipOffset[mip];
        }
    };

    ChordWavetableModule();
    ~ChordWavetableModule() override;

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override;
    void blockStart() override;

    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceNoteOff (int v) override;
    void voiceReset (int v) override;
    void voiceVelocity (int v, float velocity01) override { gate.setVelocity (v, velocity01); }
    double voiceTailSeconds() const override { return 0.02; }

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // generation settings rebuild the table (message thread, coalesced)
    void setParameter (const juce::String& id, float value) override;
    void uiButtonClicked (const juce::String& paramId) override;

    //=== table (message thread) ================================================
    void rollNewSeed();
    void setSeed (juce::uint32 newSeed);           // used by the patch generator
    void regenerateIfNeeded();
    juce::uint32 getSeed() const { return seed; }
    std::shared_ptr<const Table> currentTable() const;

    static const juce::StringArray& chordChoices();     // "Auto" + chords
    static const juce::StringArray& keysChoices();      // "Auto", 1 ... 6
    static const juce::StringArray& morphChoices();
    static const juce::StringArray& stackChoices();

    float getUiPosition() const { return uiPosition.load (std::memory_order_relaxed); }
    float getUiMorph() const    { return uiMorph.load (std::memory_order_relaxed); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 84; }

    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

private:
    void updateUnisonLayout();     // audio thread, once per block

    juce::uint32 seed { 0 };
    std::vector<std::shared_ptr<const Table>> retired;

    mutable juce::SpinLock tableLock;
    std::shared_ptr<const Table> published;

    struct Rebuilder;
    std::unique_ptr<Rebuilder> rebuilder;

    // audio thread
    std::shared_ptr<const Table> audioTable;
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase[aquanode::kMaxVoices][kMaxUnison] {};

    int unisonCount { 1 };
    float unisonRatio[kMaxUnison] {};      // frequency multiplier: stack interval + detune
    float unisonPanL[kMaxUnison] {}, unisonPanR[kMaxUnison] {};
    float unisonGain { 1.0f };
    int lastUnison { -1 }, lastStack { -1 };
    float lastDetune { -1.0f }, lastSpread { -1.0f };

    juce::Random phaseRng { 0x00C0B0A5 };
    std::atomic<float> uiPosition { 0.0f }, uiMorph { 0.0f };
    bool uiVoiceShown { false };
};
