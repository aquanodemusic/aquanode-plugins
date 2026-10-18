#pragma once

#include "ModuleCore.h"

// IRForge - an impulse-response foundry and convolver in one module.
//
// Two audio inputs. Whatever arrives on Rec In can be recorded (REC / STOP in
// the card, up to three minutes); the take - or a sound file picked with
// LOAD - is forged into an impulse response, and Audio In is convolved with
// it. With no IR yet the module passes Audio In through dry.
//
// Forging (on a background thread, never on the audio thread):
//   source range (Range Start / End, at most three minutes of it)
//     -> averaged magnitude spectrum (Hann frames of 2^FFT, half overlap)
//     -> real cepstrum -> lifter (CHARACTER) -> minimum- or linear-phase IR
//   = the "core". Low Character keeps only the smooth envelope (the room,
//   the cabinet); high Character keeps every harmonic, so convolving noise
//   with it makes the noise sing the source's chord.
// Shaping (cheap, also on the thread): Length, Decay, Reverse, Stretch, Low /
//   High Cut, Tilt, Predelay, Width, then peak normalisation.
//
// What is saved with the patch is the forged IR (as audio, like a sampler's
// sample; as long as Length asks for, plus a little) - never the source
// sound. A patch reopens with its IR and the Shape knobs still work on it;
// Character, FFT and the range need a source again (record or load one)
// before they re-forge.
//
// Character: the lifter runs geometrically from
// 4 quefrency samples to half the analysis size, so FFT also sets how much
// harmonic detail 100% can hold (the 256k default resolves a few Hz).
//
// The convolution runs in 64-sample blocks (JUCE's non-uniform partitioned
// engine), so the wet AND the dry path are 64 samples late; IR swaps
// crossfade. Gain Match keeps the wet level steady across IRs.
//
// Global effect. Inputs: 0 = Rec In, 1 = Audio In. Output: 0 = Audio Out.
class IRForgeModule : public aquanode::SynthModule,
                      private juce::Thread
{
public:
    enum ParamIndex
    {
        pCharacter = 0, pLength, pDecay, pMix, pOutput,
        pFft, pStretch, pPredelay, pTilt, pWidth,
        pLowCut, pHighCut,
        pRangeStart, pRangeEnd,
        pLinear, pReverse, pMatch
    };

    static constexpr double kMaxRangeSeconds = 180.0;   // the most of a source that is forged
    static constexpr double kMaxRecordSeconds = 180.0;
    static constexpr int kMinFftOrder = 12, kMaxFftOrder = 19;
    static constexpr int kBlock = 64;                   // convolution block = latency
    static constexpr int kDisplayPoints = 160;

    IRForgeModule();
    ~IRForgeModule() override;

    void prepare (double sr) override;
    void reset() override;
    void processSample (const aquanode::StereoFrame* inputs, aquanode::StereoFrame* outputs) override;

    // the forged core IR is what the patch keeps
    bool hasAudioToSave() const override;
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override;
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override;

    std::unique_ptr<juce::Component> createExtraContentComponent() override;
    int preferredModuleWidth() const override { return 330; }
    int extraContentHeight() const override { return 172; }

    //=== called by the card (message thread) ==================================
    void toggleRecording();
    void openSourceChooser();
    void clearAll();
    void loadSourceData (std::shared_ptr<juce::MemoryBlock> fileData, const juce::String& name);

    //=== read by the card (message thread) ====================================
    struct SourceInfo
    {
        juce::String name;
        double seconds = 0.0;
        std::vector<float> ovMin, ovMax;   // whole-source overview
    };
    std::shared_ptr<const SourceInfo> sourceInfo() const;

    struct IRView
    {
        double seconds = 0.0;
        std::array<float, kDisplayPoints> spectrumDb {};   // 20 Hz..20 kHz, log spaced
        std::array<float, kDisplayPoints> envelopeDb {};   // over the IR's length
    };
    std::shared_ptr<const IRView> irView() const;

    bool isRecording() const { return recording.load (std::memory_order_relaxed); }
    double recordedSeconds() const;
    bool isBusy() const { return busy.load (std::memory_order_relaxed); }
    bool isLoading() const { return loading.load (std::memory_order_relaxed); }
    bool hasIR() const { return irReady.load (std::memory_order_relaxed); }
    bool hasSource() const { return sourceInfo() != nullptr; }
    int sourceGeneration() const { return sourceGen.load (std::memory_order_relaxed); }
    juce::String lastError() const;
    bool recInputConnected() const { return isInputConnected (0); }

    // the forged range in seconds, as the build thread will use it
    juce::Range<double> effectiveRange() const;

private:
    struct Source;            // a loaded file (kept encoded) or a recorded take
    struct ForgeKey;
    struct ShapeKey;

    void run() override;
    void forgeIfNeeded();
    void shapeIfNeeded();
    void decodeLoadRequest();
    void finishRecordingIfStopped();

    std::shared_ptr<const Source> currentSource() const;
    void publishCore (std::shared_ptr<juce::AudioBuffer<float>> core, double rate, bool linear);
    void installIR (juce::AudioBuffer<float>&& ir, double rate);

    //=== sources ==============================================================
    mutable juce::SpinLock stateLock;               // guards the shared_ptrs below
    std::shared_ptr<const Source> source;
    std::shared_ptr<const SourceInfo> info;
    std::shared_ptr<const juce::AudioBuffer<float>> core;   // the forged core IR
    double coreRate { 0.0 };
    bool coreLinear { false };                      // centred (linear phase) or front-loaded
    std::shared_ptr<const IRView> view;
    juce::String errorText;

    std::atomic<int> sourceGen { 0 }, coreGen { 0 };

    // a picked file waiting to be decoded by the thread
    juce::CriticalSection loadLock;
    std::shared_ptr<juce::MemoryBlock> pendingLoad;
    juce::String pendingName;

    //=== recording (audio thread writes, message thread arms) =================
    juce::CriticalSection recLock;                   // arming vs. picking the take up
    juce::AudioBuffer<float> recBuffer;
    std::atomic<bool> recording { false };
    std::atomic<bool> takePending { false };
    std::atomic<int> recPos { 0 };
    double recRate { 44100.0 };

    //=== the convolver ========================================================
    juce::CriticalSection convLock;                 // prepare vs. loadImpulseResponse
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 2048 } };
    std::atomic<bool> convPrepared { false };
    std::atomic<bool> irReady { false };
    std::atomic<float> energyGain { 1.0f };          // 1 / RMS of the installed IR
    static constexpr float kMatchLevel = 0.3f;       // Gain Match: wet level for a full-band input

    float inBuf[2][kBlock] {}, outBuf[2][kBlock] {}, dlyBuf[2][kBlock] {};
    int blockPos { 0 };
    float fade { 0.0f }, fadeCoef { 0.001f }, matchGain { 1.0f }, smCoef { 0.001f };
    float smMix { 1.0f }, smOut { 1.0f };

    //=== build thread state ====================================================
    std::unique_ptr<ForgeKey> lastForge;
    std::unique_ptr<ShapeKey> lastShape;
    std::vector<float> cachedLogMag[2];
    juce::String cachedSpectrumKey;
    std::atomic<bool> busy { false }, loading { false };
    std::atomic<double> engineRate { 44100.0 };
    std::atomic<bool> reshapeRequested { false };

    std::unique_ptr<juce::FileChooser> sourceChooser;
};
