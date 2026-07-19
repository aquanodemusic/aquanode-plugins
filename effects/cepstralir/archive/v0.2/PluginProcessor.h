#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>

//==============================================================================
class CepstralIRProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    CepstralIRProcessor();
    ~CepstralIRProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public API for the editor

    // Load a sample file and extract IR
    bool loadSampleFile(const juce::File& file);

    // Save the extracted IR as a WAV file
    bool saveImpulseResponse(const juce::File& outputFile);

    // Get the extracted IR buffer (for waveform display)
    const juce::AudioBuffer<float>& getImpulseResponse() const { return irBuffer; }

    // Get the loaded source buffer
    const juce::AudioBuffer<float>& getSourceBuffer() const { return sourceBuffer; }

    // Re-run extraction using the already-loaded source buffer
    bool reprocessIR();

    // Status
    bool hasSourceLoaded() const { return sourceLoaded; }
    bool hasIRExtracted() const { return irExtracted; }
    juce::String getStatusMessage() const { return statusMessage; }

    // Parameters
    juce::AudioParameterFloat* irLengthParam;      // IR length factor (0.1 - 1.0)
    juce::AudioParameterFloat* smoothingParam;     // Cepstral smoothing (1 - 64 quefrency bands)
    juce::AudioParameterBool* applyWindowParam;   // Apply windowing to IR
    juce::AudioParameterInt* fftSizeParam;       // FFT size selector
    juce::AudioParameterBool* linearPhaseParam;   // Use linear-phase (centered) vs minimum-phase

    double sourceSampleRate = 44100.0;
    int    sourceNumChannels = 1;

private:
    //==============================================================================
    // Cepstral analysis pipeline

    // Step 1: Compute the cepstrum of the source signal
    void computeCepstrum(const std::vector<float>& signal,
        std::vector<float>& cepstrum,
        int fftSize);

    // Step 2: Extract minimum-phase spectral envelope via liftering
    void lifterCepstrum(const std::vector<float>& cepstrum,
        std::vector<float>& lifteredCepstrum,
        int numCoeffs);

    // Step 3: Convert liftered cepstrum back to minimum-phase IR
    void cepstrumToIR(const std::vector<float>& lifteredCepstrum,
        std::vector<float>& ir,
        int fftSize);

    // Step 3b: Convert liftered cepstrum to linear-phase (centered) IR
    void cepstrumToLinearPhaseIR(const std::vector<float>& lifteredCepstrum,
        std::vector<float>& ir,
        int fftSize);

    // Step 4: Apply windowing to the IR
    void applyTukeyWindow(std::vector<float>& ir, float taperRatio = 0.1f);
    void applyLinearPhaseWindow(std::vector<float>& ir);

    // Intelligent tail truncation - finds where IR becomes just noise
    int findIRTailCutoff(const std::vector<float>& ir, float noiseFloorDb = -60.0f);

    // FFT helpers
    void fftForward(std::vector<std::complex<float>>& data);
    void fftInverse(std::vector<std::complex<float>>& data);
    static int nextPowerOfTwo(int n);

    // Normalization
    void normalizeBuffer(std::vector<float>& buf);

    //==============================================================================
    juce::AudioBuffer<float> sourceBuffer;
    juce::AudioBuffer<float> irBuffer;

    bool sourceLoaded = false;
    bool irExtracted = false;
    juce::String statusMessage = "Load a sample file to begin.";

    juce::AudioFormatManager formatManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CepstralIRProcessor)
};