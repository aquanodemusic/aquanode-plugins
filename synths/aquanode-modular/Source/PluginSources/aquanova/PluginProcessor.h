#pragma once
#include <atomic>
#include <JuceHeader.h>
#include "Parameters.h"
#include "SynthEngine.h"

// SysEx support is parked for now. The reader/writer still lives in SysEx.cpp
// and SysExMap.cpp (excluded from the build in the .jucer) and can come back
// once the byte-to-parameter map is finished.
// #include "SysEx.h"

namespace aquanova {

//==============================================================================
class AquaNovaProcessor : public juce::AudioProcessor
{
public:
    AquaNovaProcessor();
    ~AquaNovaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "AquaNova"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 4.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return patchName; }
    void changeProgramName (int, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    /** Everything the synth knows lives here. The APVTS is the single source of
        truth: the host saves it, the editor edits it, the DSP reads it. */
    juce::AudioProcessorValueTreeState apvts;

    /** Backs the on-screen keyboard: a click queues a note here, and every
        note already in the MIDI buffer - host input included - is reflected
        back so the same keys light up for either source. */
    juce::MidiKeyboardState keyboardState;

    /** Saves / loads the complete state as a .aquanova file (XML), so patches
        can be kept and shared without involving the host. */
    bool savePatch (const juce::File& file);
    bool loadPatch (const juce::File& file);

    juce::String getPatchName() const                   { return patchName; }
    void setPatchName (const juce::String& n);

    /** Lets the on-screen mod wheel drive the same path a real MIDI CC1 would.
        Safe to call from the message thread; picked up at the start of the
        next processBlock() and turned into an ordinary controller event, so
        it reaches the engine exactly like a hardware mod wheel would. */
    void setModWheelFromUI (float value01) noexcept    { uiModWheel = juce::jlimit (0.0f, 1.0f, value01); }

    std::function<void()> onPatchChanged;

private:
    SynthEngine engine;
    juce::String patchName { "Init" };
    bool wasPlaying = false;

    std::atomic<float> uiModWheel { 0.0f };
    float lastSentModWheel = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaNovaProcessor)
};

} // namespace aquanova
