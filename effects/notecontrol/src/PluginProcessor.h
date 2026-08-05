#pragma once
#include <JuceHeader.h>
#include <array>
#include <limits>

class NoteControlAudioProcessor : public juce::AudioProcessor
{
public:
    NoteControlAudioProcessor();
    ~NoteControlAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "NoteControl"; }
    bool  acceptsMidi()  const override { return false; }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // -----------------------------------------------------------------------
    // FFT constants
    // -----------------------------------------------------------------------
    static constexpr int fftOrder = 12;           // 4096-point FFT
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 4;  // 75 % overlap
    static constexpr int numBins = fftSize / 2 + 1;
    static constexpr int maxCh = 2;

    // -----------------------------------------------------------------------
    // Note routing  (message-thread writes → call rebuildBinShiftTable after)
    //   noteRouting[inputNote 0-11] = outputNote (0-11),  or -1 to mute
    //   0=C  1=C#  2=D  3=D#  4=E  5=F  6=F#  7=G  8=G#  9=A  10=A#  11=B
    // -----------------------------------------------------------------------
    int  noteRouting[12];
    void resetRouting();                 // identity (each note → itself)
    void rebuildBinShiftTable();         // call after editing noteRouting
    void applyPentatonicRouting();       // snap all notes to nearest C-major pentatonic
    void shiftInputs (int semitones);    // scroll rows   up/down  — which input notes trigger
    void shiftOutputs(int semitones);    // scroll columns left/right — where they land

    // -----------------------------------------------------------------------
    // Spectrum display
    // -----------------------------------------------------------------------
    void getFFTData(float* out, int numBinsOut);

    // -----------------------------------------------------------------------
    // Exposed bin-note metadata (read-only, valid after prepareToPlay)
    //   binNoteClass[b] = 0..11 (C..B)  or  -1 if unmapped
    // -----------------------------------------------------------------------
    int binNoteClass[numBins]{};

    // -----------------------------------------------------------------------
    // Frequency gate  (normalised log-scale 0–1, matching spectrum display)
    //   Bins outside [gateStart, gateEnd] are passed through unchanged.
    //   0 = bin 1 (lowest audible), 1 = bin numBins-1 (Nyquist)
    // -----------------------------------------------------------------------
    std::atomic<float> gateStart { 0.0f };
    std::atomic<float> gateEnd   { 1.0f };

    // Helper: convert normalised gate position → bin index
    static int gatePosTobin(float t)
    {
        const float logMax = std::log(static_cast<float>(numBins - 1));
        return juce::jlimit(1, numBins - 1,
                            static_cast<int>(std::exp(t * logMax)));
    }

    // -----------------------------------------------------------------------
    // Enhance mode  (soft Gaussian bin weights + phase coherence)
    // -----------------------------------------------------------------------
    std::atomic<bool> enhanceMode{ false };

    static constexpr int   maxContrib = 3;     // max notes blended per bin
    static constexpr float kGaussSigma = 50.0f; // cents — half a semitone

    // -----------------------------------------------------------------------
    // Parameters
    // -----------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    std::atomic<float>* pDryWet{ nullptr };

    double currentSampleRate = 44100.0;

private:
    // -----------------------------------------------------------------------
    // FFT engine
    // -----------------------------------------------------------------------
    std::unique_ptr<juce::dsp::FFT> fftEngine;

    // -----------------------------------------------------------------------
    // Per-channel WOLA buffers
    //   inputFifo   : ring buffer of fftSize samples
    //   outputAccum : OLA accumulator ring (length fftSize*2)
    //   workBuf     : retained for future use
    // -----------------------------------------------------------------------
    std::vector<float> inputFifo[maxCh];
    std::vector<float> outputAccum[maxCh];
    std::vector<float> workBuf[maxCh];   // reserved for future use
    int  fifoWritePos[maxCh]{};
    int  accumReadPos[maxCh]{};             // output read head
    int  accumWritePos[maxCh]{};             // OLA write head
    int  hopSampleCount = 0;                 // samples since last FFT frame

    std::vector<float> window;       // Hann analysis/synthesis window, length fftSize

    // -----------------------------------------------------------------------
    // Per-bin shift lookup  (HARD mode — integer scatter)
    //   MUTE_SENTINEL : zero out this bin (note is muted)
    //   0             : passthrough
    //   other int     : target bin = b + binShiftAudio[b]
    // -----------------------------------------------------------------------
    static constexpr int MUTE_SENTINEL = std::numeric_limits<int>::min();

    int binShiftAudio[numBins]{};   // read on audio thread
    int binShiftPending[numBins]{};   // written by message thread, swapped in atomically

    // -----------------------------------------------------------------------
    // Per-bin soft table  (ENHANCE mode — Gaussian weighted scatter)
    //   Each bin contributes to up to maxContrib destination bins with
    //   a Gaussian weight based on cents distance.  Weights are normalised
    //   so energy is conserved across the crossfade region.
    //   If ALL contributors for a bin are muted the bin is silenced.
    // -----------------------------------------------------------------------
    struct WeightedShift
    {
        int   shift;    // bin shift (same computation as hard mode)
        float weight;   // normalised Gaussian weight [0..1]
    };

    WeightedShift softTableAudio[numBins][maxContrib]{};
    WeightedShift softTablePending[numBins][maxContrib]{};
    int           softCountAudio[numBins]{};
    int           softCountPending[numBins]{};

    std::atomic<bool>     shiftTableDirty{ false };
    juce::CriticalSection shiftTableLock;

    void computeBinNoteAssignments();  // populates binNoteClass[]
    void buildPendingShiftTable();     // populates binShiftPending[] from noteRouting
    void buildPendingSoftTable();      // populates softTablePending[] (enhance mode)

    // -----------------------------------------------------------------------
    // Spectrum display
    // -----------------------------------------------------------------------
    float smoothedMag[numBins]{};
    juce::CriticalSection displayLock;

    void allocateBuffers();
    void createWindow();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteControlAudioProcessor)
};