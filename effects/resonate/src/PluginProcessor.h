/*
  ==============================================================================
    Ableton-Style Resonator Plugin -- v4 (four engines)

    Engines (selectable). Only the selected engine runs and receives input;
    on a switch the previous engine rings out naturally (no input) and is
    switched off once its tail is below -70 dB for 100 ms:
      0 Modelled  -- measured model of Ableton Resonators (v3.1, ResonatorCore.h)
      1 Native    -- original v2 DSP, verbatim (NativeEngine.h)
      2 Crisp     -- beta DSP with chorus/LFO, DC Center, Exp Decay, Smooth,
                     2x oversampling (CrispEngine.h)
      3 Dark      -- first modelled version, v3.0 (ResonatorCoreDark.h)

    Every engine has its OWN complete parameter set in the APVTS:
      Modelled: unprefixed IDs (same as before, old presets load here)
      Native  : "nat_" prefix     Crisp: "crs_" prefix     Dark: "drk_" prefix
    Global (shared by all engines): engine, midi_enabled, gain, drywet, wet_only.
    Presets and host state store everything, i.e. all four engines at once.
  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>
#include "ResonatorCore.h"
#include "ResonatorCoreDark.h"
#include "NativeEngine.h"
#include "CrispEngine.h"

//==============================================================================
class ResonateAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int MAX_RESONATORS = 7;
    static constexpr int NUM_ENGINES = 4;
    enum EngineId { EngineModelled = 0, EngineNative = 1, EngineCrisp = 2, EngineDark = 3 };

    // Parameter-ID prefix of each engine's own parameter set.
    static juce::String enginePrefix(int engine)
    {
        switch (engine)
        {
            case EngineNative: return "nat_";
            case EngineCrisp:  return "crs_";
            case EngineDark:   return "drk_";
            default:           return {};
        }
    }

    ResonateAudioProcessor();
    ~ResonateAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

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

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    enum FilterType { Lowpass = 0, Highpass, Bandpass, Notch };
    enum ProcessingMode { ModeA = 0, ModeB = 1 };

    //==========================================================================
    bool savePresetToFile(const juce::File& file);
    bool loadPresetFromFile(const juce::File& file);
    static juce::File getDefaultPresetDirectory();

    int getMidiNoteForResonator(int index) const
    {
        return (index >= 0 && index < MAX_RESONATORS) ? midiDisplayNote[index].load() : -1;
    }

    // Number of engines currently running (selected + ringing out); for diagnostics.
    int getNumRunningEngines() const
    {
        int n = 0;
        for (bool a : engineActive) n += a ? 1 : 0;
        return n;
    }

    int getSelectedEngine() const
    {
        return juce::jlimit(0, NUM_ENGINES - 1,
            static_cast<int>(parameters.getRawParameterValue("engine")->load() + 0.5f));
    }

private:
    juce::AudioProcessorValueTreeState parameters;
    double currentSampleRate = 44100.0;

    //==========================================================================
    // Cached parameter pointers for one engine's parameter set
    struct ResRefs
    {
        std::atomic<float>* enabled = nullptr; std::atomic<float>* pitch = nullptr;
        std::atomic<float>* fine = nullptr;    std::atomic<float>* gain = nullptr;
        std::atomic<float>* decay = nullptr;   std::atomic<float>* color = nullptr;
        std::atomic<float>* cnst = nullptr;    std::atomic<float>* pan = nullptr;
    };
    struct EngineRefs
    {
        std::atomic<float>* note = nullptr;     std::atomic<float>* decay = nullptr;
        std::atomic<float>* color = nullptr;    std::atomic<float>* constMode = nullptr;
        std::atomic<float>* mode = nullptr;     std::atomic<float>* perRes = nullptr;
        std::atomic<float>* filterOn = nullptr; std::atomic<float>* filterFreq = nullptr;
        std::atomic<float>* filterType = nullptr; std::atomic<float>* width = nullptr;
        ResRefs res[MAX_RESONATORS];
        // Crisp only (null for the others)
        std::atomic<float>* center = nullptr;  std::atomic<float>* expDecay = nullptr;
        std::atomic<float>* os2x = nullptr;    std::atomic<float>* smooth = nullptr;
        std::atomic<float>* chorus = nullptr;  std::atomic<float>* lfoRate = nullptr;
        std::atomic<float>* lfoDepth = nullptr;
    };
    EngineRefs refs[NUM_ENGINES];
    void cacheRefs();

    // Plain snapshot of one engine's common settings for this block
    struct Snapshot
    {
        double note = 48, decay = 75, color = 45;
        bool   constMode = false, perRes = false, filterOn = false;
        double filterFreq = 1000; int filterType = 0; int mode = 0; float width = 1.0f;
        struct R { bool enabled = false; int pitch = 0; double fine = 0, gain = 0, decay = 50, color = 50; bool cnst = false; double pan = 0; } res[MAX_RESONATORS];
    };
    Snapshot snapshot(int engine) const;
    double midiNoteFor(int idx) const;

    //==========================================================================
    // Engines
    template <class VoiceT, class StageT, class BiquadT>
    struct ModelBank
    {
        VoiceT  voice[MAX_RESONATORS];
        float   resGain[MAX_RESONATORS] = {};
        bool    resOn[MAX_RESONATORS] = {};
        StageT  stage[2];
        BiquadT filt[2];
        bool    filterOn = false;
        float   width = 1.0f;
        float   panL[MAX_RESONATORS] = {}, panR[MAX_RESONATORS] = {};
    };
    ModelBank<ResModel::Voice, ResModel::InputStage, ResModel::InputBiquad>             modelled;
    ModelBank<ResModelDark::Voice, ResModelDark::InputStage, ResModelDark::InputBiquad> dark;

    template <class Bank> void prepareModelBank(Bank& b, double sr);
    template <class Bank> void updateModelBank(Bank& b, const Snapshot& s, double sr);
    template <class Bank> void renderModelBank(Bank& b, const float* const in[2], int n, int nIn, int nOut, float* outL, float* outR);

    struct NativeBank
    {
        NativeEngine::NativeResonate res[MAX_RESONATORS];
        NativeEngine::NativeSVF      filter;
        bool  filterOn = false; int filterType = 0; int mode = 0;
        float width = 1.0f;
        float panL[MAX_RESONATORS] = {}, panR[MAX_RESONATORS] = {};
    } native;
    void updateNative(const Snapshot& s, double sr);
    void renderNative(const float* const in[2], int n, int nIn, int nOut, float* outL, float* outR);

    CrispEngine::Engine crisp;
    CrispEngine::Params crispParams(const Snapshot& s) const;

    static void computePans(const Snapshot& s, float* panL, float* panR);

    // ── Engine hand-over ─────────────────────────────────────────────────────
    // Only the selected engine receives input. A previously selected engine
    // keeps running with no input so its tail rings out naturally; once its
    // output stays below -70 dB for 100 ms it fades out over 30 ms, is reset
    // and switched off (no CPU). Safety: a tail still ringing after 10 s is
    // faded out over 300 ms.
    juce::AudioBuffer<float> engineOut[NUM_ENGINES];
    juce::AudioBuffer<float> engineIn;                    // per-engine input scratch
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> engineInGain[NUM_ENGINES];   // 5 ms input hand-over
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> engineOutFade[NUM_ENGINES];  // fade before switch-off
    bool engineActive[NUM_ENGINES] = {};
    int  silentSamples[NUM_ENGINES] = {};
    int  ringOutSamples[NUM_ENGINES] = {};
    static constexpr double INPUT_HANDOVER_SECONDS = 0.005;
    static constexpr double SILENCE_HOLD_SECONDS   = 0.1;
    static constexpr double MAX_RINGOUT_SECONDS    = 10.0;
    static constexpr double SAFETY_FADE_SECONDS    = 0.3;
    static constexpr double SILENCE_FADE_SECONDS   = 0.03;   // fade before switch-off below -70 dB
    static constexpr float  SILENCE_THRESHOLD      = 3.1623e-4f;   // -70 dBFS

    void resetEngine(int engine, bool clearOutput = true);
    void renderEngine(int engine, const float* const in[2], int n, int nIn, int nOut);

    //==========================================================================
    // MIDI voice allocation (shared by all engines)
    struct MidiVoice { int note = -1; bool active = false; juce::uint32 order = 0; };
    MidiVoice     voices[MAX_RESONATORS];
    int           heldNote[MAX_RESONATORS] = { -1, -1, -1, -1, -1, -1, -1 };
    juce::uint32  voiceCounter = 0;
    int           rrPointer = 0;
    bool          midiEnabled = true;
    bool          allocEnabled[MAX_RESONATORS] = { true, false, false, false, false, false, false };
    std::atomic<int> midiDisplayNote[MAX_RESONATORS];

    void handleMidiMessages(const juce::MidiBuffer& midiMessages);

    // Converts presets/state from older versions (v2/v3 unprefixed, beta) in place.
    void upgradeStateXml(juce::XmlElement& xml) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ResonateAudioProcessor)
};
