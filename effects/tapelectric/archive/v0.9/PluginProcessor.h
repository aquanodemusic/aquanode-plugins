/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class TapElectricAudioProcessor  : public juce::AudioProcessor
{
public:
    TapElectricAudioProcessor();
    ~TapElectricAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    bool randomizeHarmonics();
    
    // APVTS - The modern way to manage parameters!
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameter IDs as constants for type safety
    static constexpr const char* PARAM_MIX = "mix";
    static constexpr const char* PARAM_FREQ_MODE = "freqMode";
    
    static constexpr const char* PARAM_HUM_VOLUME = "humVol";
    static constexpr const char* PARAM_HUM_PAN = "humPan";
    static constexpr const char* PARAM_HUM_STEREO = "humStereo";
    static constexpr const char* PARAM_HUM_DRIFT = "humDrift";
    
    static constexpr const char* PARAM_HARMONIC_1 = "h1";
    static constexpr const char* PARAM_HARMONIC_2 = "h2";
    static constexpr const char* PARAM_HARMONIC_3 = "h3";
    static constexpr const char* PARAM_HARMONIC_4 = "h4";
    static constexpr const char* PARAM_HARMONIC_5 = "h5";
    static constexpr const char* PARAM_HARMONIC_6 = "h6";
    
    static constexpr const char* PARAM_NOISE_VOLUME = "noiseVol";
    static constexpr const char* PARAM_NOISE_PAN = "noisePan";
    static constexpr const char* PARAM_NOISE_STEREO = "noiseStereo";
    
    static constexpr const char* PARAM_INPUT_VOLUME = "inputVol";
    static constexpr const char* PARAM_INPUT_DRIFT = "inputDrift";
    static constexpr const char* PARAM_INPUT_WOBBLE = "inputWobble";
    
    static constexpr const char* PARAM_EQ_FREQ_1 = "eqFreq1";
    static constexpr const char* PARAM_EQ_GAIN_1 = "eqGain1";
    static constexpr const char* PARAM_EQ_Q_1 = "eqQ1";
    
    static constexpr const char* PARAM_EQ_FREQ_2 = "eqFreq2";
    static constexpr const char* PARAM_EQ_GAIN_2 = "eqGain2";
    static constexpr const char* PARAM_EQ_Q_2 = "eqQ2";
    
    static constexpr const char* PARAM_EQ_FREQ_3 = "eqFreq3";
    static constexpr const char* PARAM_EQ_GAIN_3 = "eqGain3";
    static constexpr const char* PARAM_EQ_Q_3 = "eqQ3";
    
    // Per-harmonic volume randomization parameters
    static constexpr const char* PARAM_H1_RAND_AMT = "h1RandAmt";
    static constexpr const char* PARAM_H1_RAND_SPD = "h1RandSpd";
    static constexpr const char* PARAM_H2_RAND_AMT = "h2RandAmt";
    static constexpr const char* PARAM_H2_RAND_SPD = "h2RandSpd";
    static constexpr const char* PARAM_H3_RAND_AMT = "h3RandAmt";
    static constexpr const char* PARAM_H3_RAND_SPD = "h3RandSpd";
    static constexpr const char* PARAM_H4_RAND_AMT = "h4RandAmt";
    static constexpr const char* PARAM_H4_RAND_SPD = "h4RandSpd";
    static constexpr const char* PARAM_H5_RAND_AMT = "h5RandAmt";
    static constexpr const char* PARAM_H5_RAND_SPD = "h5RandSpd";
    static constexpr const char* PARAM_H6_RAND_AMT = "h6RandAmt";
    static constexpr const char* PARAM_H6_RAND_SPD = "h6RandSpd";

private:
    // Create the parameter layout for APVTS
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Atomic parameters for thread-safe reading in audio thread
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* freqModeParam = nullptr;
    
    std::atomic<float>* humVolumeParam = nullptr;
    std::atomic<float>* humPanParam = nullptr;
    std::atomic<float>* humStereoParam = nullptr;
    std::atomic<float>* humDriftParam = nullptr;
    
    std::atomic<float>* harmonic1Param = nullptr;
    std::atomic<float>* harmonic2Param = nullptr;
    std::atomic<float>* harmonic3Param = nullptr;
    std::atomic<float>* harmonic4Param = nullptr;
    std::atomic<float>* harmonic5Param = nullptr;
    std::atomic<float>* harmonic6Param = nullptr;
    
    std::atomic<float>* noiseVolumeParam = nullptr;
    std::atomic<float>* noisePanParam = nullptr;
    std::atomic<float>* noiseStereoParam = nullptr;
    
    std::atomic<float>* inputVolumeParam = nullptr;
    std::atomic<float>* inputDriftParam = nullptr;
    std::atomic<float>* inputWobbleParam = nullptr;
    
    std::atomic<float>* eqFreq1Param = nullptr;
    std::atomic<float>* eqGain1Param = nullptr;
    std::atomic<float>* eqQ1Param = nullptr;
    
    std::atomic<float>* eqFreq2Param = nullptr;
    std::atomic<float>* eqGain2Param = nullptr;
    std::atomic<float>* eqQ2Param = nullptr;
    
    std::atomic<float>* eqFreq3Param = nullptr;
    std::atomic<float>* eqGain3Param = nullptr;
    std::atomic<float>* eqQ3Param = nullptr;
    
    // Per-harmonic volume randomization parameters
    std::atomic<float>* h1RandAmtParam = nullptr;
    std::atomic<float>* h1RandSpdParam = nullptr;
    std::atomic<float>* h2RandAmtParam = nullptr;
    std::atomic<float>* h2RandSpdParam = nullptr;
    std::atomic<float>* h3RandAmtParam = nullptr;
    std::atomic<float>* h3RandSpdParam = nullptr;
    std::atomic<float>* h4RandAmtParam = nullptr;
    std::atomic<float>* h4RandSpdParam = nullptr;
    std::atomic<float>* h5RandAmtParam = nullptr;
    std::atomic<float>* h5RandSpdParam = nullptr;
    std::atomic<float>* h6RandAmtParam = nullptr;
    std::atomic<float>* h6RandSpdParam = nullptr;
    
    // DSP State
    double sampleRate = 44100.0;
    std::vector<double> phases;
    
    juce::Random randomL, randomR;
    
    double humDriftPhase = 0.0;
    double inputDriftPhase = 0.0;
    double inputWobblePhase = 0.0;
    
    // Volume randomization state (6 LFOs, one per harmonic)
    std::array<double, 6> harmonicModPhases = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<double, 6> harmonicModRates = {1.0, 1.13, 0.87, 1.21, 0.93, 1.07}; // Slightly different rates for variation
    
    std::vector<float> delayBufferL, delayBufferR;
    int delayWritePos = 0;
    
    // EQ filters (IIR biquad filters for each band, L/R)
    juce::dsp::IIR::Filter<float> eqFilter1L, eqFilter1R;
    juce::dsp::IIR::Filter<float> eqFilter2L, eqFilter2R;
    juce::dsp::IIR::Filter<float> eqFilter3L, eqFilter3R;
    
    void updateEQFilters();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapElectricAudioProcessor)
};
