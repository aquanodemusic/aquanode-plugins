#pragma once

#include "ModuleCore.h"

// Spectral Lock - a polyphonic filterbank resynthesizer / retuner. The
// input's mid band (Range Low..High) is split into narrow log-spaced bands
// (36 per octave, two cascaded band-passes each), every band's level is
// followed, and each band sings it back through its own sine oscillator - retuned onto the 12 x 12 pitch-class
// matrix around Root, or magnetised to the notes held on Midi In. The low and
// high tails pass through dry (Mute Low / Mute High remove them).
//
// The matrix lives in the card: columns are the note a band is at (relative
// to Root), rows the note it is moved to; the strip above it picks Root and
// the Scale menu stamps a scale into it. The band view on top shows the
// oscillator bank live; the lit buttons under it are Mute Lo / Mute Hi, MIDI
// (follow the held notes instead of the matrix) and Freeze.
//
// Amount blends the tonal resynthesis against the raw noise/transients, Mix
// against the untouched input. Isolate gates out quiet, unpitched bands,
// Glide slides each band to its new pitch, Shimmer adds a phase-locked
// octave, Spray a fixed per-band detune scatter, Tilt pivots around 700 Hz.
// Zero latency. Global effect.
// Inputs: 0 = Audio In, 1 = Midi In. Output: 0 = Audio Out.
class SpectralLockModule : public aquanode::SynthModule
{
public:
    enum ParamIndex
    {
        pMix = 0, pAmount, pIsolate, pGlide, pLevel,
        pPitch, pBend, pBendRange, pShimmer, pSpray,
        pWidth, pTilt, pRangeLo, pRangeHi,
        pMuteLow, pMuteHigh, pMidi, pFreeze,
        pRoot, pMatrix0   // matrix0 .. matrix11 follow
    };

    static constexpr int numScales = 12;
    static constexpr int kDefaultScale = 9;   // a fresh Spectral Lock starts on Penta Minor
    static const char* const scaleNames[numScales];
    static const int scaleTables[numScales][12];
    static constexpr int numDisplayBins = 120;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    bool acceptsGlobalMidiNotes() const override { return true; }
    void voiceNoteOn (int slot, int note, bool retrigger) override;
    void voiceNoteOff (int slot) override;
    void midiAllNotesOff() override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 330; }
    int extraContentHeight() const override { return 292; }

    // UI feedback
    float displayBin (int i) const { return display[(size_t) juce::jlimit (0, numDisplayBins - 1, i)].load (std::memory_order_relaxed); }
    bool midiIsActive() const { return midiActive.load (std::memory_order_relaxed); }

private:
    static constexpr int bandsPerOctave = 36;
    static constexpr float bankLowHz = 60.0f, bankHighHz = 4800.0f, bandQ = 21.0f;
    static constexpr int maxBands = 240;
    static constexpr int tableSize = 2048;
    static constexpr int kCtrlBlock = 64;

    struct Band
    {
        float fc = 0, noteIn = 0, detune = 0, panL = 0.707f, panR = 0.707f;
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0;          // RBJ band-pass (b1 = 0)
        float s1a = 0, s2a = 0, s1b = 0, s2b = 0;
        float envCoef = 0, envSq = 0, amp = 0, gate = 1;
        float phase = 0, freq = 0, targetFreq = 0, ampGain = 0;
        bool muted = false;
    };

    void buildFilterbank();
    void updateRangeWindow (float loHz, float hiHz);
    void updateBandMapping();
    void controlUpdate();

    float sine (float p01) const noexcept
    {
        const float p = p01 * (float) tableSize;
        const int i = (int) p;
        const float f = p - (float) i;
        return sineTable[(size_t) i] + f * (sineTable[(size_t) i + 1] - sineTable[(size_t) i]);
    }

    std::array<Band, maxBands> bands;
    std::array<float, tableSize + 1> sineTable {};
    int numBands { 0 }, firstBand { 0 }, lastBand { -1 };

    juce::dsp::LinkwitzRileyFilter<float> loSplit, hiSplit;

    bool noteHeld[128] {};
    int slotNote[aquanode::kMaxVoices];
    std::vector<int> heldSorted;

    float envCoefGate = 0, gateAttack = 0, gateRelease = 0, gateThresh = 0, glideCoef = 0;
    float globalPeak = 0, blockPeak = 0, transFast = 0, transSlow = 0, audioGate = 1;
    float transFastC = 0, transSlowC = 0, agC = 0;
    float smMix = 1, smAmount = 1, smLevel = 1, smShimmer = 0, smCoef = 0;
    int ctrlLeft { 0 };

    std::array<std::atomic<float>, numDisplayBins> display {};
    std::atomic<bool> midiActive { false };
};
