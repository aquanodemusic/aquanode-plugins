#pragma once

#include <JuceHeader.h>
#include "AquaVibrioParameters.h"
#include "AquaVibrioPatch.h"
#include "AquaVibrioEngine.h"
#include "AquaVibrioOriginalEngine.h"
#include "AquaVibrioNoteLayerManager.h"
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
class AquaVibrioProcessor : public juce::AudioProcessor,
                            private juce::Timer
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

    // True when the loaded list is a hardware bank (.mid / .syx) rather than
    // a folder of .json presets.
    bool isHardwareBank() const { return ! bankPatches.empty(); }
    // Sampled hardware waveforms for the Original engine.
    int setSampledWaveFolder (const juce::File& folder);
    juce::File getSampledWaveFolder() const { return sampledWaveFolder; }
    int getNumSampledWaves() const { return originalEngine.getNumSampledWaves(); }
    int getNumTIWavetables() const { return originalEngine.getNumTIWavetables(); }
    void serviceWavetables() { originalEngine.serviceWavetables(); }   // also from the timer

    bool isOriginalEngine() const { return engineMode != nullptr && engineMode->load() > 0.5f; }

    // Master volume (-24..+24 dB, not part of presets). Switching to the
    // Original engine sets it to kOriginalMasterDb, switching to Native to 0.
    static constexpr float kOriginalMasterDb = 12.0f;
    void setMasterVolumeDb (float db);
    void selectPatch (int index);
    void saveCurrentPatch (const juce::File& file, const juce::String& name);

    int getCurrentPatchIndex() const { return currentPatch; }
    int getNumPatches() const { return presetNames.size(); }
    juce::String getPatchName (int index) const;
    juce::String getCurrentPatchName() const;

    //== note layers ===========================================================
    NoteLayerManager& getNoteLayers() { return noteLayers; }

    // How many effects racks exist, and how many are running (for the panel
    // and the tests): one is the base patch's, the rest belong to layers.
    int getNumEffectChains() const { return engine.getNumChains(); }
    int getEffectChainsInUse() const { return engine.getChainsInUse(); }

private:
    void timerCallback() override;   // wavetables, and the master volume on an engine switch

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

    // The Original engine: a separate copy of the voice engine, recalibrated
    // against measurements of a reference instrument, so
    // ROM patches sound the way they were programmed. EngineMode picks which
    // one plays; the native engine is never touched by it.
    original::Engine originalEngine;
    std::atomic<float>* engineMode { nullptr };

    std::atomic<float>* masterVolume { nullptr };
    juce::LinearSmoothedValue<float> masterGain { 1.0f };
    std::atomic<int> engineSeen { 0 };   // the engine the master volume was last set for
    int activeEngine { -1 };

    // Declared after the engine it publishes to, so it is destroyed first.
    NoteLayerManager noteLayers { apvts, engine };

    // The current folder of presets, in the order shown in the patch list.
    std::vector<juce::File> presetFiles;
    std::vector<Patch> bankPatches;     // a hardware bank, when one is loaded
    juce::File sampledWaveFolder;
    juce::StringArray presetNames;
    int currentPatch { -1 };

    double currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquaVibrioProcessor)
};

} // namespace aquavibrio
