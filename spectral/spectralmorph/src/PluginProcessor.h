#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MorphEngine.h"

class SpectralMorphAudioProcessor : public juce::AudioProcessor,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AsyncUpdater
{
public:
    SpectralMorphAudioProcessor();
    ~SpectralMorphAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;
    MorphEngine engine;

    bool sidechainConnected() const noexcept { return sideConnected.load(); }

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;

    std::atomic<float>* pMorph = nullptr;
    std::atomic<float>* pClarity = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pMaxBoost = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutGain = nullptr;
    std::atomic<float>* pFlip = nullptr;
    std::atomic<float>* pDynamics = nullptr;
    std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pFftSize = nullptr;
    std::atomic<float>* pOverlap = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pMorphMode = nullptr;   // see MorphEngine::Mode

    // Vocoder / Inject / Partials
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pFlatten = nullptr;
    std::atomic<float>* pSibilance = nullptr;
    std::atomic<float>* pFill = nullptr;
    std::atomic<float>* pFold = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pLock = nullptr;
    std::atomic<float>* pPeakFloor = nullptr;

    juce::AudioBuffer<float> mainScratch, sideScratch;
    juce::SmoothedValue<float> outputGain;
    std::atomic<bool> sideConnected { false };
    std::atomic<int>  pendingLatency { -1 };
    int activeOrder = -1, activeOverlap = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralMorphAudioProcessor)
};
