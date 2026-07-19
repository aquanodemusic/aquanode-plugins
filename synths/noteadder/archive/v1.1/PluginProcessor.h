#pragma once
#include <JuceHeader.h>

class NoteAdderProcessor : public juce::AudioProcessor
{
public:
    static constexpr int NUM_ROWS = 7;
    static constexpr int NUM_SCALES = 9;

    NoteAdderProcessor();
    ~NoteAdderProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()  const override { return true; }
    const juce::String getName() const override;
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ── Mode ──────────────────────────────────────────────────
    juce::AudioParameterInt* modeParam;

    // ── Manual ────────────────────────────────────────────────
    juce::AudioParameterBool* enabledParams[NUM_ROWS];
    juce::AudioParameterInt* semiParams[NUM_ROWS];
    juce::AudioParameterInt* octaveParams[NUM_ROWS];

    // ── Scale ─────────────────────────────────────────────────
    juce::AudioParameterInt* rootNoteParam;
    juce::AudioParameterInt* scaleTypeParam;
    juce::AudioParameterInt* noteCountParam;
    juce::AudioParameterBool* discardParam;
    juce::AudioParameterBool* discardInputParam;
    juce::AudioParameterBool* randomSkipParam;
    juce::AudioParameterBool* lockBassParam;

    // 0=Normal 1=Picker 2=VoiceLeader 3=Drop2 4=Cycle
    juce::AudioParameterInt* inversionModeParam;
    juce::AudioParameterInt* inversionPickerParam;   // 0–7

    static const int SCALE_INTERVALS[NUM_SCALES][7];
    static const int SCALE_SIZES[NUM_SCALES];

private:
    std::array<std::array<std::vector<int>, 128>, 16> activeAddedNotes;
    std::array<std::array<bool, 128>, 16>             discardedNotes;
    juce::Random     rng;
    std::vector<int> lastChordNotes;   // for voice leading
    int              cycleIndex = 0;   // for cycle mode

    std::vector<int> computeAddedNotes(int note);
    bool             isNoteInScale(int note) const;

    // Applies rotation inversion (inv times) in-place, sorted ascending
    static void rotateInversion(std::vector<int>& notes, int inv);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteAdderProcessor)
};