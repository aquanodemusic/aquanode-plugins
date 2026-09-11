/*
    FlowEQ - PluginProcessor.h

    Three bell/peak filters (cyan, teal, brown), each with three freely
    drawn, cyclic modulation curves (frequency absolute 20-20000Hz,
    gain -24..+24dB, Q multiplicative), 128 points each. Every curve has
    its own cycle time (0.01-120s) or tempo sync (1/32 .. 8/1,
    triplet/dotted).
*/

#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================
static constexpr int   kNumFilters      = 3;
static constexpr int   kNumCurvePoints  = 128;
static constexpr float kMinFreq         = 20.0f;
static constexpr float kMaxFreq         = 20000.0f;
static constexpr float kMinGainDb       = -24.0f;
static constexpr float kMaxGainDb       = 24.0f;
static constexpr float kMinQ            = 0.10f;
static constexpr float kMaxQ            = 10.0f;
static constexpr float kMinTimeSec      = 0.01f;
static constexpr float kMaxTimeSec      = 120.0f;

// Fixed default frequencies of the three filters, doubling as their "rest
// line" when the freq curve hasn't been drawn on.
static constexpr float kDefaultFreqs[kNumFilters] = { 100.0f, 1000.0f, 10000.0f };

// Per-filter colours (used in the editor, just reference IDs here)
enum class FilterColourId { Cyan = 0, Teal = 1, Brown = 2 };

//==============================================================================
// Generic, PERIODIC Catmull-Rom interpolation over "size" control points,
// read via a lambda (works for std::array AND raw wavetable frame pointers
// alike). The transition from the last point back to the first is treated
// as a perfectly normal, continuous segment (no phase jump) - this is the
// structural basis for click-free cyclic playback in BOTH modes (128 as
// well as 2048 points).
template <typename GetSampleFn>
static inline float catmullRomPeriodic (int size, float position, GetSampleFn getSample) noexcept
{
    auto n = (float) size;
    float p = juce::jlimit (0.0f, 0.999999f, position) * n;
    int i1 = (int) p;
    float frac = p - (float) i1;

    auto wrap = [size] (int idx) { return ((idx % size) + size) % size; };

    float p0 = getSample (wrap (i1 - 1));
    float p1 = getSample (wrap (i1));
    float p2 = getSample (wrap (i1 + 1));
    float p3 = getSample (wrap (i1 + 2));

    float t = frac;
    float t2 = t * t;
    float t3 = t2 * t;

    float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    float a1 =        p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    float a2 = -0.5f * p0 + 0.5f * p2;
    float a3 = p1;

    return juce::jlimit (0.0f, 1.0f, a0 * t3 + a1 * t2 + a2 * t + a3);
}

//==============================================================================
// A single, paintable 128-point curve, normalised 0..1 in memory.
// The meaning of the normalised value depends on the CurveTarget (see below).
struct FlowCurveData
{
    std::array<float, kNumCurvePoints> points;

    FlowCurveData() { reset(); }

    // "Nothing drawn" == rest line == neutral. For Freq the neutral value
    // is only resolved at mapping time (using the filter's base value), so
    // 0.5 (= centre) is used here as the generic "unpainted" default; the
    // mapper knows its own neutral mapping per CurveTarget.
    void reset() { points.fill (0.5f); }

    // Catmull-Rom interpolation between the 128 control points for smooth,
    // periodic (click-free) movement. position is in [0, 1).
    float getInterpolated (float position) const noexcept
    {
        return catmullRomPeriodic (kNumCurvePoints, position,
                                    [this] (int i) { return points[(size_t) i]; });
    }
};

//==============================================================================
// A loaded wavetable file: N frames of 2048 samples each (bipolar -1..1).
// Once loaded the object is immutable -> lock-free readable from the audio
// thread, as long as only the shared_ptr itself (on reload) is synchronised.
class WavetableSet
{
public:
    static constexpr int kFrameSize = 2048;

    bool loadFromFile (const juce::File& file)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
            return false;

        int totalSamples = (int) reader->lengthInSamples;
        if (totalSamples < 1)
            return false;

        // If the file isn't an exact multiple of kFrameSize, the last frame
        // isn't discarded but zero-padded instead.
        int frames = (totalSamples + kFrameSize - 1) / kFrameSize;

        data.setSize (1, frames * kFrameSize);
        data.clear();
        reader->read (&data, 0, totalSamples, 0, true, false);
        numFrames = frames;
        name = file.getFileNameWithoutExtension();
        fullPath = file.getFullPathName();
        return true;
    }

    // Returns a pointer to the kFrameSize samples of the requested frame.
    const float* getFramePointer (int frameIndex) const noexcept
    {
        frameIndex = juce::jlimit (0, juce::jmax (0, numFrames - 1), frameIndex);
        return data.getReadPointer (0, frameIndex * kFrameSize);
    }

    int numFrames = 0;
    juce::AudioBuffer<float> data;
    juce::String name, fullPath;
};

//==============================================================================
// Simple one-pole smoother (block-rate), prevents hard jumps in the filter
// coefficients - e.g. at the cycle wrap, with hard-edged drawn curves, or
// when switching the wavetable frame during playback.
struct OnePoleSmoother
{
    float value = 0.0f;
    bool initialised = false;

    void reset() { initialised = false; }
    void snapTo (float v) { value = v; initialised = true; }

    float process (float target, float alpha) noexcept
    {
        if (! initialised) { value = target; initialised = true; return value; }
        value += (target - value) * alpha;
        return value;
    }
};

//==============================================================================
// What a curve controls - determines how its 0..1 value is mapped onto the
// real parameter.
enum class CurveTarget { Frequency, Gain, Q };

// Runs cyclically (phase 0..1) over an adjustable time and reads the
// associated FlowCurveData to deliver the current modulated value.
class ModulationEngine
{
public:
    void prepare (double sr) { sampleRate = sr; }

    void reset() { phase = 0.0; }

    // cycleTimeSeconds: the actual cycle time (either from the seconds
    // parameter or precomputed from tempo sync).
    void advance (int numSamples, double cycleTimeSeconds)
    {
        if (cycleTimeSeconds <= 0.0001) cycleTimeSeconds = 0.0001;
        double inc = (double) numSamples / (sampleRate * cycleTimeSeconds);
        phase += inc;
        phase -= std::floor (phase);
    }

    float getCurrentValue (const FlowCurveData& curve) const
    {
        return curve.getInterpolated ((float) phase);
    }

    double getPhase() const noexcept { return phase; }
    void setPhase (double p) noexcept { phase = p; }

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
};

//==============================================================================
// Rhythmic note values for tempo sync, including triplet/dotted.
struct SyncDivision
{
    juce::String label;
    double beatsInQuarterNotes; // length of the value in quarter notes
};

const std::vector<SyncDivision>& getSyncDivisions();

// Converts a sync choice index + BPM into seconds.
double syncDivisionToSeconds (int choiceIndex, double bpm);

//==============================================================================
class FlowEQAudioProcessor final : public juce::AudioProcessor
{
public:
    FlowEQAudioProcessor();
    ~FlowEQAudioProcessor() override;

    //== juce::AudioProcessor ================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //== Parameter Layout =====================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createParameterLayout() };

    //== Curve access (editor reads/writes directly) ==========================
    FlowCurveData& getCurve (int filterIndex, CurveTarget target);
    const FlowCurveData& getCurve (int filterIndex, CurveTarget target) const;

    // Smooths a freehand-drawn curve in place (5-tap periodic weighted
    // moving average). No-op while the curve is in wavetable (read-only) mode.
    void smoothCurve (int filterIndex, CurveTarget target);

    //== Wavetable API =========================================================
    // Loads a wavetable file (mono WAV, N*2048 samples) and automatically
    // switches this curve into wavetable mode (instead of freehand-128).
    bool loadWavetable (int filterIndex, CurveTarget target, const juce::File& file);

    void setUseWavetable (int filterIndex, CurveTarget target, bool shouldUse);
    bool isUsingWavetable (int filterIndex, CurveTarget target) const;

    void setWavetableFrame (int filterIndex, CurveTarget target, int frameIndex);
    int  getWavetableFrame (int filterIndex, CurveTarget target) const;
    int  getWavetableNumFrames (int filterIndex, CurveTarget target) const;
    juce::String getWavetableName (int filterIndex, CurveTarget target) const;

    // Returns the currently active control points (128 drawn OR 2048 from
    // the active wavetable frame, normalised 0..1) for drawing in the editor.
    // readOnly is true in wavetable mode (editor must not paint then).
    int getActiveNumPoints (int filterIndex, CurveTarget target) const;
    float getActivePointNormalized (int filterIndex, CurveTarget target, int pointIndex) const;

    // Currently selected filter in the UI (0..2), not automatable, but
    // still saved in the state.
    std::atomic<int> selectedFilter { 0 };

    // For the editor display: current modulation phase & value per curve,
    // so a small dot can travel across the drawn curve.
    struct LiveModState { double phase = 0.0; float value = 0.5f; };
    LiveModState getLiveState (int filterIndex, CurveTarget target) const;

    // Resets the processor + state completely to factory defaults.
    void resetAll();

    // For the editor: read the current host BPM (fallback 120).
    double getCurrentBpm() const { return currentBpm.load(); }

    // For the white sum curve / coloured individual curves in the editor:
    // computes the magnitude response (in dB) across the frequency range
    // for the current (live-modulated) state.
    void getFilterMagnitudeResponse (int filterIndex, const std::vector<double>& frequencies,
                                      std::vector<double>& magnitudesDb) const;
    void getTotalMagnitudeResponse (const std::vector<double>& frequencies,
                                     std::vector<double>& magnitudesDb) const;

private:
    //== DSP ==================================================================
    using FilterBand = juce::dsp::IIR::Filter<float>;
    using FilterCoeffs = juce::dsp::IIR::Coefficients<float>;

    std::array<juce::dsp::ProcessorDuplicator<FilterBand, FilterCoeffs>, kNumFilters> filters;
    std::array<std::array<ModulationEngine, 3>, kNumFilters> modEngines; // [filter][Freq,Gain,Q]
    std::array<std::array<FlowCurveData, 3>, kNumFilters> curves;            // [filter][Freq,Gain,Q]

    // Wavetable status per curve. shared_ptr so the audio thread never
    // reads half-finished data on reload (atomic swap of the whole immutable
    // object instead of in-place mutation).
    std::array<std::array<std::shared_ptr<WavetableSet>, 3>, kNumFilters> wavetables;
    std::array<std::array<std::atomic<int>, 3>, kNumFilters> wavetableFrame;
    std::array<std::array<std::atomic<bool>, 3>, kNumFilters> useWavetable;
    juce::SpinLock wavetableSwapLock; // protects only the brief shared_ptr access

    // Smoothing of the final freq/gain/Q values -> no clicks at wrap,
    // hard-edged curves, or frame changes.
    std::array<std::array<OnePoleSmoother, 3>, kNumFilters> valueSmoothers;
    static constexpr float kSmoothingTimeSeconds = 0.012f;

    mutable std::array<std::array<std::atomic<float>, 3>, kNumFilters> lastLiveValue;
    mutable std::array<std::array<std::atomic<double>, 3>, kNumFilters> lastLivePhase;

    std::atomic<double> currentBpm { 120.0 };
    double sampleRate = 44100.0;

    void updateFilterCoefficients (int filterIndex, float liveFreq, float liveGainDb, float liveQ);
    double resolveCycleSeconds (int filterIndex, int curveIdx); // reads params + sync if applicable

    // Returns the current normalised (0..1) modulation value of a curve at
    // the given phase - regardless of whether a freehand or wavetable
    // source is active. Uses the same periodic interpolation both times.
    float getModulatedNormalized (int filterIndex, int curveIdx, double phase) const;

    juce::AudioParameterFloat*  getFreqParam  (int i) const;
    juce::AudioParameterFloat*  getGainParam  (int i) const;
    juce::AudioParameterFloat*  getQParam     (int i) const;
    juce::AudioParameterFloat*  getTimeParam  (int i, CurveTarget t) const;
    juce::AudioParameterBool*   getSyncParam  (int i, CurveTarget t) const;
    juce::AudioParameterChoice* getSyncDivParam (int i, CurveTarget t) const;

    static juce::String paramIdBase (int filterIndex, CurveTarget t);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlowEQAudioProcessor)
};
