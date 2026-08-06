#pragma once
#include <JuceHeader.h>

//==============================================================================
static constexpr int EVO_POINTS = 32;

//==============================================================================
// Fast inline pseudo-random number generator for block-level synthesis
inline float fastRand()
{
    static unsigned int seed = 123456789;
    seed = (214013 * seed + 2531011);
    return ((seed >> 16) & 0x7FFF) / 32768.f;
}

//==============================================================================
// Structure to hold precomputed block-level coefficients for optimized rendering
struct BlockEnvCoeffs
{
    float decCoeff = 0.0f;
    float relCoeff = 0.0f;
    float dec2Coeff = 0.0f;
    float rel2Coeff = 0.0f;
    float fdecCoeff = 0.0f;
    float frelCoeff = 0.0f;
    float penvDecCoeff = 0.0f;
    float twDecCoeff = 0.0f;
    float twRelCoeff = 0.0f;
    double dm = 1.0;
    double sA = 1.0;
    double sB = 1.0;
    double octA = 1.0;
    double octB = 1.0;
};

//==============================================================================
// Two cascaded biquads = 4-pole (24 dB/oct) lowpass
struct StereoBiquad
{
    float x1L = 0, x2L = 0, y1L = 0, y2L = 0, x1R = 0, x2R = 0, y1R = 0, y2R = 0;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    void setLowpass(float freq, float q, double sr)
    {
        float w = 2.f * juce::MathConstants<float>::pi * freq / (float)sr;
        w = juce::jlimit(0.001f, juce::MathConstants<float>::pi * 0.98f, w);
        float cw = std::cos(w), alpha = std::sin(w) / (2.f * q);
        float b0c = (1.f - cw) * 0.5f, b1c = 1.f - cw, b2c = b0c, a0 = 1.f + alpha;
        b0 = b0c / a0; b1 = b1c / a0; b2 = b2c / a0;
        a1 = -2.f * cw / a0; a2 = (1.f - alpha) / a0;
    }
    float processL(float in) { float o = b0 * in + b1 * x1L + b2 * x2L - a1 * y1L - a2 * y2L; x2L = x1L;x1L = in;y2L = y1L;y1L = o; return o; }
    float processR(float in) { float o = b0 * in + b1 * x1R + b2 * x2R - a1 * y1R - a2 * y2R; x2R = x1R;x1R = in;y2R = y1R;y1R = o; return o; }
    void reset() { x1L = x2L = y1L = y2L = x1R = x2R = y1R = y2R = 0; }
};

struct StereoFilter4Pole
{
    StereoBiquad s1, s2;
    void setLowpass(float freq, float q, double sr)
    {
        float q1 = q * 0.7071f;
        float q2 = q * 1.3066f;
        s1.setLowpass(freq, q1, sr);
        s2.setLowpass(freq, q2, sr);
    }
    float processL(float in) { return s2.processL(s1.processL(in)); }
    float processR(float in) { return s2.processR(s1.processR(in)); }
    void reset() { s1.reset(); s2.reset(); }
};

//==============================================================================
struct TranswaveVoice
{
    bool   active = false;
    int    midiNote = 0;
    float  velocity = 1.0f;

    double phaseA = 0.0;
    double phaseB = 0.0;

    // Per-voice curve scanning state
    double curvePhase = 0.0;
    int    scanDir = 1;
    bool   curveFinished = false;
    float  evoPhaseCarryStart = 0.0f;

    float  frameOffset = 0.0f;
    float  velFrameOffset = 0.0f;

    // Amplitude envelope
    enum class Env { Idle, Attack, Decay, Sustain, Release } envStage = Env::Idle;
    float envLevel = 0.0f;
    float releaseStartLevel = 0.0f;

    // Filter envelope
    Env   fenvStage = Env::Idle;
    float fenvLevel = 0.0f;
    float fenvReleaseStart = 0.0f;

    // Pitch envelope
    float penvLevel = 0.0f;
    bool  penvDone = false;

    // Transwave position envelope (Fizmo-style direct frame-position ADSR)
    Env   twenvStage    = Env::Idle;
    float twenvLevel    = 0.0f;
    float twenvRelStart = 0.0f;

    // OSC 2 (B) amplitude envelope
    Env   env2Stage = Env::Idle;
    float env2Level = 0.0f;
    float rel2StartLevel = 0.0f;

    // Per-voice filter
    StereoFilter4Pole voiceFilter;

    // Glide state
    double glideStartFreq = 0.0;
    double glideTargetFreq = 0.0;
    float  glideProgress = 1.0f;

    void noteOn(int note, float vel, double prevFreq = 0.0, float glideTime = 0.f,
        float velFrameAmt = 0.f, bool carryPhase = false, float carryPhaseVal = 0.f)
    {
        midiNote = note;
        velocity = vel;
        active = true;
        phaseA = phaseB = 0.0;
        envStage = Env::Attack;   envLevel = 0.0f;
        env2Stage = Env::Attack;  env2Level = 0.0f; rel2StartLevel = 0.0f;
        fenvStage = Env::Attack;  fenvLevel = 0.0f; fenvReleaseStart = 0.0f;
        twenvStage = Env::Attack; twenvLevel = 0.0f; twenvRelStart = 0.0f;
        penvLevel = 1.0f;         penvDone = false;
        if (carryPhase)
            curvePhase = (double)carryPhaseVal;
        else
            curvePhase = 0.0;
        scanDir = 1;  curveFinished = false;
        frameOffset = 0.0f;
        velFrameOffset = vel * velFrameAmt;
        glideTargetFreq = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        if (prevFreq > 0.0 && glideTime > 0.001f) {
            glideStartFreq = prevFreq;
            glideProgress = 0.0f;
        }
        else {
            glideStartFreq = glideTargetFreq;
            glideProgress = 1.0f;
        }
    }
    void noteOff()
    {
        releaseStartLevel = envLevel;
        envStage = Env::Release;
        rel2StartLevel = env2Level;
        env2Stage = Env::Release;
        fenvReleaseStart = fenvLevel;
        fenvStage = Env::Release;
        twenvRelStart = twenvLevel;
        twenvStage = Env::Release;
    }
};

//==============================================================================
struct StereoChorus
{
    static constexpr int MAX_DELAY = 4096;
    float bufL[MAX_DELAY] = {}, bufR[MAX_DELAY] = {};
    int   writePos = 0;
    double lfoPhase = 0.0;

    void reset() { std::fill(bufL, bufL + MAX_DELAY, 0.f); std::fill(bufR, bufR + MAX_DELAY, 0.f); writePos = 0; lfoPhase = 0.0; }

    void process(float& L, float& R, float rate, float depth, double sr)
    {
        float ds = depth * (float)(sr * 0.025f);
        float bd = ds * 0.5f + 1.f;
        lfoPhase += rate / sr;
        if (lfoPhase > 1.0) lfoPhase -= 1.0;
        float lL = (float)std::sin(lfoPhase * juce::MathConstants<double>::twoPi), lR = -lL;
        float dL = bd + lL * ds * 0.5f, dR = bd + lR * ds * 0.5f;
        bufL[writePos % MAX_DELAY] = L;
        bufR[writePos % MAX_DELAY] = R;
        auto ri = [&](float* b, float d) -> float {
            float rf = (float)writePos - d;
            while (rf < 0) rf += MAX_DELAY;
            int r0 = (int)rf % MAX_DELAY, r1 = (r0 + 1) % MAX_DELAY;
            float fr = rf - (int)rf;
            return b[r0] + fr * (b[r1] - b[r0]);
            };
        float wL = ri(bufL, dL), wR = ri(bufR, dR);
        ++writePos;
        L += wL * 0.5f;
        R += wR * 0.5f;
    }
};

//==============================================================================
enum class ScanMode { Forward = 0, FwdStay = 1, BackForth = 2, BwdStay = 3, Backward = 4 };

//==============================================================================
class TranswaveAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    TranswaveAudioProcessor();
    ~TranswaveAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()    const override { return true; }
    const juce::String getName() const override { return "PhizmOsc Transwave Engine"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int  getNumPrograms()   override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // --- Wavetable loading ---
    void         loadWavetable(const juce::File& file, int singleCycleSamples, int slot);
    bool         loadWavetableFromMemory(const juce::MemoryBlock& fileData, const juce::String& originalFileName, int singleCycleSamples, int slot);
    bool         isWavetableLoaded(int slot) const;
    int          getNumFrames(int slot) const;
    int          getCycleSamples(int slot) const;
    juce::String getWavetableName(int slot) const;
    juce::String getWavetableFilePath(int slot) const;

    float getCurrentEvoFramePos(int osc = 0) const;
    bool  getFrameSamples(int slot, int frameIndex, std::vector<float>& out) const;
    bool  getWavetableOverview(int slot, int displayWidth, int displayHeight, std::vector<float>& out) const;
    float sampleFrameNearest(int slot, float frameIndex, double phase);

    std::atomic<int> activeSlot{ 0 };

    // --- Evolution curves ---
    std::atomic<float> evoCurve[EVO_POINTS];
    std::atomic<float> evoCurveB[EVO_POINTS];
    void  setCurvePoint(int idx, float val, int osc = 0);
    float getCurvePoint(int idx, int osc = 0) const { return (osc == 0 ? evoCurve : evoCurveB)[idx].load(); }
    float evalCurve(float t, int osc = 0) const;

    std::atomic<float> evoPlayhead{ 0.0f };

    // --- Preset persistence ---
    static juce::File getPresetsDirectory();
    bool savePreset(const juce::File& destFile);
    bool loadPreset(const juce::File& srcFile);

    // --- User sample folder (persisted in plugin/project state, NOT in shared presets) ---
    juce::String getSampleFolder() const { return sampleFolder; }
    void setSampleFolder(const juce::String& path) { sampleFolder = path; }

    // --- User preset folder (overrides getPresetsDirectory() when set) ---
    juce::String getPresetFolder() const { return presetFolder; }
    void setPresetFolder(const juce::String& path) { presetFolder = path; }
    // Returns custom preset folder if set, otherwise falls back to the default Documents dir.
    juce::File getEffectivePresetsDirectory() const;

    // GUI window size — backed by the "guiWidth" APVTS parameter so the DAW
    // saves and restores it automatically. Height is derived from the fixed
    // 1190:804 aspect ratio.
    int  getGuiWidth()  const;
    int  getGuiHeight() const;
    void setGuiWidth(int w);

    // Last-loaded preset name — persisted in project state so it survives
    // DAW reload (not stored in the .phizm file itself, only in DAW state).
    juce::String getCurrentPresetName() const { return currentPresetName; }
    void         setCurrentPresetName(const juce::String& name) { currentPresetName = name; }

    // Cycle sizes backed by APVTS so they survive DAW project reloads.
    int  getCycleSizeParam(int slot) const;
    void setCycleSizeParam(int slot, int size);

private:
    struct WavetableSlot
    {
        std::vector<std::vector<float>> frames;
        int   numFrames = 0, cycleSamples = 0;
        bool  loaded = false;
        juce::String name;          // display name (no extension)
        juce::String filePath;      // absolute path, local-machine only (never persisted in shared presets)
        juce::String fileName;      // original file name WITH extension, e.g. "MyWave.wav" - safe to embed/share
        juce::MemoryBlock originalFileData; // raw bytes of the original file, kept so presets can embed it
        mutable juce::CriticalSection lock;
    };
    WavetableSlot wt[2];

    static constexpr int MAX_VOICES = 16;
    TranswaveVoice voices[MAX_VOICES];
    double currentSampleRate = 44100.0;

    juce::SmoothedValue<float> gainSmooth;
    StereoFilter4Pole filter;
    StereoChorus      chorus;
    juce::Reverb      reverb;

    double pitchLFOPhase = 0.0;

    // --- Raw param pointers ---
    std::atomic<float>* pEvoTime = nullptr;
    std::atomic<float>* pEvoStepped = nullptr;
    std::atomic<float>* pEvoSteppedB = nullptr;
    std::atomic<float>* pEvoLFORate = nullptr;
    std::atomic<float>* pEvoLFODepth = nullptr;
    std::atomic<float>* pPosLFORate = nullptr;
    std::atomic<float>* pPosLFODepth = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pBitCrush = nullptr;
    std::atomic<float>* pGrit = nullptr;
    std::atomic<float>* pDetune = nullptr;
    std::atomic<float>* pPitchLFO = nullptr;
    std::atomic<float>* pPitchLFORate = nullptr;
    std::atomic<float>* pScanStyle = nullptr;
    std::atomic<float>* pJumpProb = nullptr;
    std::atomic<float>* pFilterFreq = nullptr;
    std::atomic<float>* pFilterQ = nullptr;
    std::atomic<float>* pFilterAtt = nullptr;
    std::atomic<float>* pFilterDec = nullptr;
    std::atomic<float>* pFilterSus = nullptr;
    std::atomic<float>* pFilterRel = nullptr;
    std::atomic<float>* pFilterEnvAmt = nullptr;
    std::atomic<float>* pFilterLFODep = nullptr;
    std::atomic<float>* pPitchEnvAmt = nullptr;
    std::atomic<float>* pPitchEnvAtt = nullptr;
    std::atomic<float>* pPitchEnvDec = nullptr;
    std::atomic<float>* pOscMix = nullptr;
    std::atomic<float>* pSpread = nullptr;
    std::atomic<float>* pStereoWidth = nullptr;
    std::atomic<float>* pUniDetune = nullptr;
    std::atomic<float>* pStereoPhase = nullptr;
    std::atomic<float>* pChorusRate = nullptr;
    std::atomic<float>* pChorusDepth = nullptr;
    std::atomic<float>* pRingMod = nullptr;
    std::atomic<float>* pReverbSize = nullptr;
    std::atomic<float>* pReverbDamp = nullptr;
    std::atomic<float>* pReverbWet = nullptr;

    // New params
    std::atomic<float>* pAttack2 = nullptr;
    std::atomic<float>* pDecay2 = nullptr;
    std::atomic<float>* pSustain2 = nullptr;
    std::atomic<float>* pRelease2 = nullptr;
    std::atomic<float>* pOctaveA = nullptr;
    std::atomic<float>* pOctaveB = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pMono = nullptr;
    std::atomic<float>* pNoise = nullptr;

    // Engine mode toggles
    std::atomic<float>* pFrameInterp = nullptr;
    std::atomic<float>* pFilterPerVoice = nullptr;
    std::atomic<float>* pVelToFrame = nullptr;
    std::atomic<float>* pEvoPhaseCarry = nullptr;

    // Transwave position envelope
    std::atomic<float>* pTwAtt    = nullptr;
    std::atomic<float>* pTwDec    = nullptr;
    std::atomic<float>* pTwSus    = nullptr;
    std::atomic<float>* pTwRel    = nullptr;
    std::atomic<float>* pTwAmt    = nullptr;
    std::atomic<float>* pTwVelAmt = nullptr;

    // Misc (row 2, cols 1-6)
    std::atomic<float>* pFrameSnap    = nullptr;   // frame quantise steps
    std::atomic<float>* pTwToFilter   = nullptr;   // TW env → filter cutoff amount
    std::atomic<float>* pEvoBPhaseOff = nullptr;   // Osc B curve phase offset vs A
    std::atomic<float>* pKeytrack     = nullptr;   // MIDI note → frame position
    std::atomic<float>* pEvoTimeB     = nullptr;   // independent evo time for Osc B
    std::atomic<float>* pEvoRestart   = nullptr;   // toggle: reset curve on note-on
    std::atomic<float>* pWtSmooth     = nullptr;   // wavetable cycle edge smoothing A (0-1 → 0-5%)
    std::atomic<float>* pWtSmoothB    = nullptr;   // wavetable cycle edge smoothing B

    // Block-level LFO values computed once per block
    float blockPosLFO = 0.0f;
    float blockPitchLFO = 0.0f;

    // Mono glide state
    double lastNoteFreq = 0.0;
    bool   monoActive = false;

    // Per-voice LFO phases 
    double voiceEvoLFOPhase[MAX_VOICES] = {};
    double voicePosLFOPhase[MAX_VOICES] = {};

    // Block-level coefficients cache
    BlockEnvCoeffs blk;

    void  synthesiseVoice(TranswaveVoice& v, int vi,
        float posLFOMod, double pitchMult,
        float& outL, float& outR, const BlockEnvCoeffs& bc);
    float applyBitCrush(float s, float bits);
    float sampleFrameRaw(const WavetableSlot& s, float frameIndex, double phase, bool interp, float smoothAmt = 0.f) const;

    // Shared implementation used by both loadWavetable() (from disk) and
    // loadWavetableFromMemory() (extracted from a preset zip). Keeps the
    // raw original bytes + original file name around so presets can later
    // re-embed the wavetable without needing the original absolute path.
    bool  loadWavetableFromReader(std::unique_ptr<juce::AudioFormatReader> reader,
                                   juce::MemoryBlock originalBytes,
                                   const juce::String& originalFileName,
                                   const juce::String& displayFilePath,
                                   int cycleSamplesRequested, int slot);

    juce::String sampleFolder;
    juce::String presetFolder;
    juce::String currentPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TranswaveAudioProcessor)
};