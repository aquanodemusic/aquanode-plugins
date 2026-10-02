#pragma once

#include "ModuleCore.h"

// Transwave - a wavetable oscillator whose table is GENERATED rather than
// loaded, using the transwave generator from Phizmo: a table is rolled from a
// seed and one of twelve archetypes (Resonant Sweep, Formant, Bell Stack,
// Vocal Morph...), built frame by frame as harmonic spectra that morph in the
// log-amplitude domain, so a resonance slides across the table instead of
// crossfading. Same seed + archetype + frame count = the same table, so a
// patch stores three numbers instead of the audio.
//
// Playback is band-limited: every frame is also rendered as a stack of
// progressively duller copies straight from its spectrum, and the copy whose
// top harmonic still fits under Nyquist for the note is read - bright tables
// stay clean at the top of the keyboard.
//
// Position is where in the table a note starts; Scan moves through the table
// by itself once the note is playing (Forward, Backward, Ping-Pong or Once, a
// full pass every Scan Time seconds); Pos In adds any modulation on top - a
// Curve CV or ADSR here is the classic transwave sweep.
// Inputs: 0 = Pos In, 1 = Env In, 2 = Add Midi In. Output: 0 = Audio Out.
class TranswaveModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pVolume = 0, pArchetype, pFrames, pPosition, pPosDepth, pScanMode, pScanTime,
                      pVoices, pGlide, pRoll };
    enum ScanMode { sOff = 0, sForward, sBackward, sPingPong, sOnce };

    static constexpr int kBaseLength = 2048;
    static constexpr int kNumMips = 6;          // 2048, 1024 ... 64 samples per cycle

    struct Table
    {
        int numFrames { 0 };
        std::vector<std::array<std::vector<float>, kNumMips>> frames;   // [frame][mip]
        juce::uint32 seed { 0 };
        int archetype { 0 };                    // the one actually used (Auto resolved)
        int framesSetting { 0 };
        int archetypeSetting { 0 };
    };

    TranswaveModule();
    ~TranswaveModule() override;

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

    // archetype / frame count changes rebuild the table (message thread)
    void setParameter (const juce::String& id, float value) override;
    void uiButtonClicked (const juce::String& paramId) override;

    //=== table (message thread) ================================================
    void rollNewSeed();
    void regenerateIfNeeded();      // builds when seed/archetype/frames changed
    juce::uint32 getSeed() const { return seed; }
    std::shared_ptr<const Table> currentTable() const;
    static const juce::StringArray& archetypeChoices();   // "Auto" + the twelve
    static const juce::StringArray& frameChoices();       // "Auto", 16 ... 256

    float getUiPosition() const { return uiPosition.load (std::memory_order_relaxed); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 84; }

    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

private:
    float readTable (const Table& t, float position01, double phase01, double freqHz) const;

    juce::uint32 seed { 0 };
    std::vector<std::shared_ptr<const Table>> retired;   // freed on the message thread only

    mutable juce::SpinLock tableLock;
    std::shared_ptr<const Table> published;

    struct Rebuilder;
    std::unique_ptr<Rebuilder> rebuilder;   // coalesces knob changes into one rebuild

    // audio thread
    std::shared_ptr<const Table> audioTable;
    aquanode::ModuleVoicePool pool;
    aquanode::ModuleGlide glide;
    aquanode::ModuleVoiceGate gate;
    double phase[aquanode::kMaxVoices] {};
    double scan[aquanode::kMaxVoices] {};      // 0..1 progress of the self-scan
    int scanDir[aquanode::kMaxVoices] {};

    std::atomic<float> uiPosition { 0.0f };
    bool uiVoiceShown { false };     // a voice drew the display this block (audio thread)
};
