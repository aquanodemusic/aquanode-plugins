#pragma once

#include <JuceHeader.h>
#include <vector>
#include <array>
#include <complex>
#include <algorithm>

//==============================================================================
// Grainfreeze Audio Processor
// Phase vocoder-based time stretching with freeze mode
//==============================================================================

class GrainfreezeAudioProcessor : public juce::AudioProcessor
{
public:
    GrainfreezeAudioProcessor();
    ~GrainfreezeAudioProcessor() override;

    //==============================================================================
    // Audio Processing

    // Called before playback starts to initialize at given sample rate and buffer size
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;

    // Called when playback stops to release resources
    void releaseResources() override;

    // Checks if a given channel layout configuration is supported
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Main audio processing callback - processes one block of audio
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    // Editor

    // Creates the plugin's graphical user interface
    juce::AudioProcessorEditor* createEditor() override;

    // Returns true since this plugin has a GUI
    bool hasEditor() const override;

    //==============================================================================
    // Plugin Info

    // Returns the plugin name
    const juce::String getName() const override;

    // Returns false - this plugin doesn't accept MIDI
    bool acceptsMidi() const override;

    // Returns false - this plugin doesn't produce MIDI
    bool producesMidi() const override;

    // Returns false - this plugin is not a MIDI effect
    bool isMidiEffect() const override;

    // Returns 0 - this plugin has no tail
    double getTailLengthSeconds() const override;

    //==============================================================================
    // Program/Preset Management

    // Returns number of preset programs (always 1)
    int getNumPrograms() override;

    // Returns current program index (always 0)
    int getCurrentProgram() override;

    // Sets current program (no-op)
    void setCurrentProgram(int index) override;

    // Returns program name at index
    const juce::String getProgramName(int index) override;

    // Changes program name (no-op)
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    // State Persistence

    // Saves plugin state to memory block for DAW project save
    void getStateInformation(juce::MemoryBlock& destData) override;

    // Restores plugin state from memory block when DAW project loads
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Audio File Management

    // Loads an audio file into the internal buffer
    void loadAudioFile(const juce::File& file);

    // Returns reference to loaded audio buffer
    const juce::AudioBuffer<float>& getLoadedAudio() const { return loadedAudio; }

    // Returns true if audio file is loaded
    bool isAudioLoaded() const { return audioLoaded; }

    //==============================================================================
    // Playback Control

    // Sets playhead position (0.0 to 1.0 normalized). fromParameter is true
    // when this call originates from the automatable playheadParam itself
    // syncing into the engine (host automation/session restore) -- in that
    // case we don't re-push the value back into the parameter.
    void setPlayheadPosition(float normalizedPosition, bool fromParameter = false);

    // Gets current playhead position (0.0 to 1.0 normalized)
    float getPlayheadPosition() const { return playheadPosition.load(); }

    // Starts or stops playback
    void setPlaying(bool shouldPlay);

    // Returns true if currently playing
    bool isPlaying() const { return playing; }

    //==============================================================================
    // Playhead Scrubbing State (UI <-> host automation coordination)

    // Called by the editor while the user is actively dragging the waveform
    // or the playhead slider, so the engine doesn't fight the drag with its
    // own per-block automation sync.
    void setUserScrubbing(bool shouldScrub) { userScrubbing = shouldScrub; }
    bool isUserScrubbing() const { return userScrubbing; }

    //==============================================================================
    // Spectrum Data Access

    // Returns the current FFT magnitudes for visualization
    const std::vector<float>& getSpectrumMagnitudes() const { return spectrumMagnitudes; }

    // Returns current FFT size
    int getCurrentFftSize() const { return currentFftSize; }

    // Returns current sample rate
    double getCurrentSampleRate() const { return currentSampleRate; }

    //==============================================================================
    // Parameters - Primary Controls

    // Normalized playback position (0.0-1.0). Fully host-automatable, and
    // kept in sync with the engine's actual playback position each block so
    // hosts can both record and drive it (see setPlayheadPosition()).
    juce::AudioParameterFloat* playheadParam;

    juce::AudioParameterFloat* timeStretch;      // Time stretching factor (0.1-4.0)
    juce::AudioParameterFloat* grainSizeParam;   // Grain size (reserved for future use)
    juce::AudioParameterFloat* hopSizeParam;     // Hop size divisor (2.0-16.0)
    juce::AudioParameterChoice* fftSizeParam;    // FFT window size (512-65536)
    juce::AudioParameterBool* freezeModeParam;   // Freeze mode on/off
    juce::AudioParameterFloat* glideParam;       // Freeze mode glide time (0-1000ms)

    // Reconstruction algorithm: Spectral (original raw magnitude/phase
    // reconstruction) or Cepstral (homomorphic envelope, smoother/less
    // phasey). Applies to both freeze and normal time-stretched playback;
    // choosing Spectral leaves either mode exactly as it originally was.
    juce::AudioParameterChoice* freezeTypeParam;

    // Cepstral lifter cutoff, 0-100%. Only used when freezeTypeParam is
    // Cepstral; the UI repurposes the HF Boost slider's slot to show this
    // instead, since HF Boost's own compensation isn't meaningful once the
    // magnitude spectrum has been cepstrally smoothed. Low = only the coarse
    // spectral envelope survives per grain (smoothest, fewest artifacts),
    // high = closer to the grain's own harmonic detail (brighter, a bit more
    // texture creeps back in).
    juce::AudioParameterFloat* characterParam;

    // Stereo analysis toggle: off (default) mixes L+R down to mono before
    // analysis/resynthesis, exactly as originally. On processes each channel
    // through its own independent FFT chain (needs a stereo source loaded).
    juce::AudioParameterBool* stereoModeParam;

    // Phase lock toggle: off (default) is the original behaviour, where
    // every bin's synthesis phase evolves independently -- this is what
    // gives phase vocoder stretching/freezing its characteristic smear.
    // On locks each spectral neighbourhood's phase to its peak bin (identity
    // phase locking), which keeps transients much sharper at the cost of a
    // slightly more "clustered"/less diffuse texture.
    juce::AudioParameterBool* phaseLockParam;

    // Parameters - Advanced Controls
    juce::AudioParameterFloat* hfBoostParam;         // High-frequency boost 0-100% (Spectral freeze type only)
    juce::AudioParameterFloat* microMovementParam;   // Freeze micro-movement 0-100%
    juce::AudioParameterChoice* windowTypeParam;     // Window function type
    juce::AudioParameterFloat* crossfadeLengthParam; // Crossfade length 1-8 hops

private:
    //==============================================================================
    // Audio Data

    juce::AudioBuffer<float> loadedAudio;  // Buffer containing loaded audio file
    bool audioLoaded = false;              // Flag indicating if audio is loaded
    double loadedFileSampleRate = 44100.0; // Native sample rate of the loaded file,
                                            // used to keep playback pitch correct
                                            // when it differs from the host/DAW rate

    //==============================================================================
    // Playhead Parameter Sync

    bool userScrubbing = false;            // True while the user is dragging the
                                            // waveform or playhead slider
    float lastSyncedPlayheadValue = 0.0f;  // Last playheadParam value we either
                                            // pushed or consumed, to avoid feedback
                                            // loops between the engine and the param

    //==============================================================================
    // Playback State

    std::atomic<float> playheadPosition{ 0.0f };  // Thread-safe playhead for UI
    bool playing = false;                          // Playback active flag
    double playbackPosition = 0.0;                 // Current playback position in samples

    //==============================================================================
    // MIDI Playback State (held notes pitched relative to C4/MIDI 60)

    std::vector<int> heldMidiNotes;      // Currently held notes, last-pressed at the back
    int currentMidiNote = 60;            // Note currently driving playback (60 = C4)
    float midiPitchRatio = 1.0f;         // 2^((note-60)/12); 1.0 = no shift, at C4
    bool midiNoteActive = false;         // True while any MIDI note is held

    // Scratch buffers the phase vocoder engine renders into at the
    // pitch-shifted sample count, before being resampled back to the block's
    // real length. Sized per-block in processTimeStretch().
    std::vector<float> pitchScratchL;
    std::vector<float> pitchScratchR;

    // Resamplers used to convert the engine's pitch-shifted output back to
    // the block's real sample count (see processTimeStretch()).
    juce::LagrangeInterpolator pitchInterpolatorL;
    juce::LagrangeInterpolator pitchInterpolatorR;

    //==============================================================================
    // Freeze Mode State

    bool isInFreezeMode = false;                          // Current freeze mode state
    double freezeTargetPosition = 0.0;                    // Target position for gliding
    double freezeCurrentPosition = 0.0;                   // Current smoothed position
    juce::SmoothedValue<double> smoothedFreezePosition;   // Smoother for glide effect

    float freezeMicroMovement = 0.0f;  // Current micro-movement offset
    int freezeMicroCounter = 0;        // Counter for micro-movement updates

    //==============================================================================
    // Phase Vocoder Configuration

    int currentFftSize = 4096;      // Current FFT size in samples
    int currentHopSize = 512;       // Current hop size in samples
    double currentSampleRate = 44100.0;  // Current sample rate
    int activeChannelCount = 1;     // 1 = mono-mixed analysis, 2 = independent stereo analysis

    // FFT Processing Objects
    std::unique_ptr<juce::dsp::FFT> fftAnalysis;   // FFT object for analysis
    std::unique_ptr<juce::dsp::FFT> fftSynthesis;  // FFT object for synthesis

    // Processing Buffers - index 0 is used for both mono processing and the
    // left channel of stereo processing; index 1 is only used (and only
    // allocated meaningfully) when stereo processing is active.
    std::array<std::vector<float>, 2> analysisFrame;     // Windowed input frame for FFT
    std::vector<float> synthesisFrame;    // Output frame from IFFT (unused, reserved)
    std::array<std::vector<float>, 2> fftBuffer;         // FFT work buffer (real/imag interleaved)
    std::array<std::vector<float>, 2> outputAccum;       // Overlap-add accumulator
    std::vector<float> crossfadeBuffer;   // Buffer for smooth transitions (unused, reserved)

    // Phase Tracking for Phase Vocoder
    std::array<std::vector<float>, 2> previousPhase;     // Previous frame phases for unwrapping
    std::array<std::vector<float>, 2> synthesisPhase;    // Accumulated synthesis phases

    // Spectrum Data for Visualization
    std::vector<float> spectrumMagnitudes; // Current FFT magnitudes

    // Window Function
    std::vector<float> window;            // Window coefficients (Hann or Blackman-Harris)

    // Grain Processing State
    int outputWritePos = 0;  // Current write position in output accumulator
    int grainCounter = 0;    // Counts down samples until next grain

    // Crossfade State (for smooth playhead jumps)
    bool needsCrossfade = false;  // Flag indicating crossfade in progress
    int crossfadeCounter = 0;     // Current position in crossfade
    int crossfadeSamples = 0;     // Total length of crossfade in samples

    //==============================================================================
    // Internal Processing Methods

    // Processes time stretching or freeze mode for a block of samples
    void processTimeStretch(juce::AudioBuffer<float>& outputBuffer, int numSamples);

    // Performs phase vocoder analysis/synthesis on one grain
    void performPhaseVocoder();

    // Creates window function based on current window type parameter
    void createWindow();

    // Creates Hann window (good general purpose)
    void createHannWindow();

    // Creates Blackman-Harris window (better frequency resolution)
    void createBlackmanHarrisWindow();

    // Updates FFT size and reallocates all related buffers
    void updateFftSize();

    // Updates hop size immediately when parameter changes
    void updateHopSize();

    // Cepstral lifter: smooths a log-magnitude spectrum by keeping only its
    // low-quefrency cepstral coefficients (real cepstrum -> lifter -> back to
    // log magnitude). This is a homomorphic spectral envelope estimate, so
    // reconstructing from it instead of the raw per-bin magnitude removes the
    // spiky, phasey artifacts that a held/near-static phase vocoder grain
    // otherwise produces.
    static void cepstralSmoothMagnitude(const std::vector<float>& logMag,
                                         std::vector<float>& outSmoothedLogMag,
                                         juce::dsp::FFT& fftEngine, int fftSize,
                                         float characterNorm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrainfreezeAudioProcessor)
};