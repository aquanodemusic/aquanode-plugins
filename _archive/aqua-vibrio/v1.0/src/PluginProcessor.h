#pragma once

#include <JuceHeader.h>
#include "AquaVibrioParameters.h"
#include "AquaVibrioPatch.h"
#include "AquaVibrioEngine.h"
#include "Aquanode/ModuleRegistry.h"

namespace aquavibrio
{

//==============================================================================
//  AquaVibrioProcessor
//
//  The processor owns the parameter tree, the patch bank and the voice
//  engine. It does very little itself: the engine reads the APVTS directly
//  once per block, so nothing has to be copied across on every parameter
//  change.
//==============================================================================
class AquaVibrioProcessor : public juce::AudioProcessor
{
public:
    AquaVibrioProcessor();
    ~AquaVibrioProcessor() override = default;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Aqua Vibrio"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== patches ===============================================================
    juce::AudioProcessorValueTreeState& getState() { return apvts; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    // Preset (.json) handling. loadBank scans the given file's folder (or
    // the folder itself, if a folder is passed) for sibling *.json presets,
    // the same way a hardware bank used to hold multiple singles, so prev /
    // next and the patch list keep working the same way. Returns how many
    // presets were found, and leaves the requested file (if any) selected.
    int loadBank (const juce::File& file);
    void selectPatch (int index);
    void saveCurrentPatch (const juce::File& file, const juce::String& name);

    int getCurrentPatchIndex() const { return currentPatch; }
    juce::String getPatchName (int index) const;
    juce::String getCurrentPatchName() const;

private:
    // `engine` below is a plain member with no explicit initializer, so it is
    // default-constructed during member-initialisation - BEFORE this class's
    // constructor body ever runs. That means a forceLinkAllModules() call in
    // the constructor body would already be too late: Engine's constructor
    // asks the (still-empty) ModuleFactory for modules the moment it starts
    // running. Members are constructed strictly in declaration order
    // regardless of the constructor's initialiser-list order, so this guard,
    // declared immediately above `engine`, is what actually guarantees every
    // module has registered before Engine's constructor begins.
    struct ModuleLinkGuard { ModuleLinkGuard() { aquanode::forceLinkAllModules(); } };
    ModuleLinkGuard moduleLinkGuard;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    Engine engine;

    // The current folder of presets, in the order shown in the patch list.
    std::vector<juce::File> presetFiles;
    juce::StringArray presetNames;
    int currentPatch { -1 };

    double currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaVibrioProcessor)
};

} // namespace aquavibrio
