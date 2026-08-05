#pragma once

#include <JuceHeader.h>

class SpectralCompareAudioProcessor : public juce::AudioProcessor,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::AsyncUpdater
{
public:
    SpectralCompareAudioProcessor();
    ~SpectralCompareAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // -----------------------------------------------------------------------
    // APVTS
    // -----------------------------------------------------------------------
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // -----------------------------------------------------------------------
    // FFT parameters
    // -----------------------------------------------------------------------
    int fftOrder = 11;
    int fftSize  = 1 << fftOrder;   // 2048 default
    int hopSize  = fftSize / 4;
    int numBins  = fftSize / 2 + 1;
    static constexpr int maxBins = (1 << 13) / 2 + 1;  // 4097 (FFT 8192)

    void setFFTSize(int newSize);

    // Smoothed magnitude data for display — call from message thread only
    void getMainFFTData     (float* out, int numBinsOut);
    void getSidechainFFTData(float* out, int numBinsOut);
    void getMorphedFFTData  (float* out, int numBinsOut);  // only meaningful when morphAmount > 0

    // Convenience getter for the editor (replaces direct atomic access)
    float getMorphAmount() const { return morphParam->load(std::memory_order_relaxed); }

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    void   setBackgroundColor       (juce::Colour c);
    void   setGridColor             (juce::Colour c);
    void   setMainSpectrumColor     (juce::Colour c);
    void   setSidechainSpectrumColor(juce::Colour c);
    void   setDeltaColor            (juce::Colour c);
    void   setMorphColor            (juce::Colour c);
    void   setSidebarColor          (juce::Colour c);
    juce::Colour getBackgroundColor()        const { return backgroundColor; }
    juce::Colour getGridColor()              const { return gridColor; }
    juce::Colour getMainSpectrumColor()      const { return mainSpectrumColor; }
    juce::Colour getSidechainSpectrumColor() const { return sidechainSpectrumColor; }
    juce::Colour getDeltaColor()             const { return deltaColor; }
    juce::Colour getMorphColor()             const { return morphColor; }
    juce::Colour getSidebarColor()           const { return sidebarColor; }
    void resetColors();

    double currentSampleRate = 44100.0;

private:
    // -----------------------------------------------------------------------
    // Cached raw parameter pointers (set once in constructor, read-only after)
    // -----------------------------------------------------------------------
    std::atomic<float>* morphParam        = nullptr;
    std::atomic<float>* envelopeWidthParam= nullptr;
    std::atomic<float>* smoothMainParam   = nullptr;
    std::atomic<float>* smoothSidechainParam = nullptr;
    std::atomic<float>* smoothMorphParam  = nullptr;

    // Pending FFT size requested by the "fftSize" parameter change
    std::atomic<int> pendingFFTSize { 0 };

    // AsyncUpdater: calls setFFTSize on the message thread
    void parameterChanged(const juce::String& paramID, float newValue) override;
    void handleAsyncUpdate() override;

    // -----------------------------------------------------------------------
    // FFT objects
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftMain;
    std::unique_ptr<juce::dsp::FFT> fftSidechain;

    std::vector<float> mainFifo;
    std::vector<float> sidechainFifo;

    // analysisFrame: 2*fftSize scratch for the forward FFT (interleaved re/im).
    // preFftFrame:   fftSize — windowed time-domain snapshot saved *before*
    //                performRealOnlyForwardTransform overwrites analysisFrame.
    //                Used by the morph=0 shortcut to OLA directly, skipping IFFT.
    std::vector<float> analysisFrame;
    std::vector<float> preFftFrame;

    // morphFrame: 2*fftSize — receives a copy of the FFT spectrum, gets
    //             magnitude-morphed per-bin, then is IFFT'd for OLA output.
    //             Only touched when morph > 0.
    std::vector<float> morphFrame;

    std::vector<float> window;
    int mainFifoIndex      = 0;
    int sidechainFifoIndex = 0;

    void allocateBuffers();
    void createWindow();

    // -----------------------------------------------------------------------
    // OLA synthesis
    // -----------------------------------------------------------------------
    // outputOLA: fftSize circular overlap-add accumulator.
    // olaNorm:   hopSize — precomputed Σ(window²) over the 4 overlapping frames.
    //            Dividing by this on read gives perfect reconstruction (morph=0).
    std::vector<float> outputOLA;
    std::vector<float> olaNorm;
    int outputReadPos = 0;

    // -----------------------------------------------------------------------
    // Display double-buffers  (protected by displayLock)
    // -----------------------------------------------------------------------
    std::vector<float> smoothedMain;
    std::vector<float> smoothedSidechain;
    std::vector<float> smoothedMorphed;        // per-bin morphed magnitude → cyan curve
    std::vector<float> instantaneousSidechain; // raw per-frame magnitudes (kept for display)
    std::vector<float> morphEnvelopeSidechain; // frequency-smoothed envelope, the morph target
    juce::CriticalSection displayLock;

    // -----------------------------------------------------------------------
    // Colors
    // -----------------------------------------------------------------------
    juce::Colour backgroundColor       { 0xffffffff };
    juce::Colour gridColor             { 0xff444444 };
    juce::Colour mainSpectrumColor     { 0xff00ff00 };
    juce::Colour sidechainSpectrumColor{ 0xffff00ff };
    juce::Colour deltaColor            { 0xffffff00 };
    juce::Colour morphColor            { 0xff00e5ff };
    juce::Colour sidebarColor          { 0xff1a1a1a };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralCompareAudioProcessor)
};
