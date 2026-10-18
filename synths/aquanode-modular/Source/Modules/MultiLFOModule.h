#pragma once

#include "ModuleCore.h"

// Multi LFO (typeId "util.multilfo"; part of the violet
// Colorbass family) - a tempo-synced shape LFO for colour-bass movement:
// plucky saw-down chops, triangles, gates, stair
// steps, sixteenth chops and random step patterns, each a list of points with
// a curvature per segment. Divisions are in note values (1/4 = one beat) and
// follow the host tempo; with no host (standalone, Android) the BPM knob
// stands in. Retrigger restarts the shape on every note; One Shot runs it
// once and holds the last value, like an extra envelope.
//
// Roll draws a brand new random shape (it switches Shape to "Rolled"); the
// rolled shape is stored with the patch.
//
// Per voice, so every note gets its own copy of the shape from its start.
// Outputs: 0 = Mod Out (Min..Max), 1 = Inv Out (Max..Min).
class MultiLFOModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pShape = 0, pRoll, pSync, pDivision, pRate, pMode,
                      pMin, pMax, pSmooth, pPhase, pBpm };

    struct Point { float x, y, power; };
    using Shape = std::vector<Point>;

    static constexpr int kTableSize = 1024;

    MultiLFOModule();

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void prepare (double sr) override { SynthModule::prepare (sr); reset(); }
    void reset() override;
    void blockStart() override;
    void voiceNoteOn (int v, int note, bool retrigger) override;
    void voiceReset (int v) override;

    void processVoiceSample (int v, const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    void setParameter (const juce::String& id, float value) override;
    void uiButtonClicked (const juce::String& paramId) override;

    static const juce::StringArray& shapeChoices();
    static const juce::StringArray& divisionChoices();
    static double divisionBeats (int index, int syncMode);

    // message thread
    void rollShape (juce::uint32 newSeed = 0);   // 0 = system random
    juce::uint32 getSeed() const { return seed; }
    std::shared_ptr<const std::array<float, kTableSize>> currentTable() const;
    float getUiPhase() const { return uiPhase.load (std::memory_order_relaxed); }

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int extraContentHeight() const override { return 56; }

    juce::String saveCustomState() const override;
    void loadCustomState (const juce::String& state) override;

    static Shape builtInShape (int index);
    static Shape randomShape (juce::uint32 seed);

private:
    void rebuildTable();

    juce::uint32 seed { 0x1F0u };
    mutable juce::SpinLock tableLock;
    std::shared_ptr<const std::array<float, kTableSize>> published, audioTable;
    std::vector<std::shared_ptr<const std::array<float, kTableSize>>> retired;

    double phase[aquanode::kMaxVoices] {};
    float slew[aquanode::kMaxVoices] {};
    bool done[aquanode::kMaxVoices] {};
    double beatsPerSecond { 140.0 / 60.0 };
    std::atomic<float> uiPhase { 0.0f };
};
