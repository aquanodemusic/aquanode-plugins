#pragma once
#include <JuceHeader.h>

//==============================================================================
static constexpr int EVO_POINTS = 32;

struct TranswaveVoice
{
    bool   active = false;
    int    midiNote = 0;
    float  velocity = 1.0f;

    double phaseA = 0.0;
    double phaseB = 0.0;
    double framePhase = 0.0;

    bool  jumpActive         = false;
    float jumpFramePosNorm   = 0.0f;
    float frameOffset        = 0.0f;

    enum class Env { Idle, Attack, Decay, Sustain, Release } envStage = Env::Idle;
    float envLevel           = 0.0f;
    float releaseStartLevel  = 0.0f;
    float envTime            = 0.0f;

    void noteOn (int note, float vel)
    {
        midiNote  = note; velocity  = vel; active = true;
        phaseA = phaseB = 0.0;
        envStage = Env::Attack; envLevel = 0.0f; envTime = 0.0f;
        jumpActive = false; jumpFramePosNorm = 0.0f; frameOffset = 0.0f;
    }
    void noteOff() { releaseStartLevel = envLevel; envStage = Env::Release; envTime = 0.0f; }
};

//==============================================================================
struct StereoBiquad
{
    float x1L=0,x2L=0,y1L=0,y2L=0, x1R=0,x2R=0,y1R=0,y2R=0;
    float b0=1,b1=0,b2=0,a1=0,a2=0;

    void setLowpass (float freq, float q, double sr)
    {
        float w = 2.f*juce::MathConstants<float>::pi*freq/(float)sr;
        w = juce::jlimit(0.001f, juce::MathConstants<float>::pi*0.98f, w);
        float cw=std::cos(w), alpha=std::sin(w)/(2.f*q);
        float b0c=(1.f-cw)*0.5f, b1c=1.f-cw, b2c=b0c, a0=1.f+alpha;
        b0=b0c/a0; b1=b1c/a0; b2=b2c/a0;
        a1=-2.f*cw/a0; a2=(1.f-alpha)/a0;
    }
    float processL (float in) { float o=b0*in+b1*x1L+b2*x2L-a1*y1L-a2*y2L; x2L=x1L;x1L=in;y2L=y1L;y1L=o; return o; }
    float processR (float in) { float o=b0*in+b1*x1R+b2*x2R-a1*y1R-a2*y2R; x2R=x1R;x1R=in;y2R=y1R;y1R=o; return o; }
    void reset() { x1L=x2L=y1L=y2L=x1R=x2R=y1R=y2R=0; }
};

//==============================================================================
struct StereoChorus
{
    static constexpr int MAX_DELAY = 4096;
    float bufL[MAX_DELAY]={}, bufR[MAX_DELAY]={};
    int   writePos=0;
    double lfoPhase=0.0;

    void reset() { std::fill(bufL,bufL+MAX_DELAY,0.f); std::fill(bufR,bufR+MAX_DELAY,0.f); writePos=0; lfoPhase=0.0; }

    void process (float& L, float& R, float rate, float depth, double sr)
    {
        float ds = depth*(float)(sr*0.025f);
        float bd = ds*0.5f+1.f;
        lfoPhase += rate/sr; if(lfoPhase>1.0) lfoPhase-=1.0;
        float lL=(float)std::sin(lfoPhase*juce::MathConstants<double>::twoPi), lR=-lL;
        float dL=bd+lL*ds*0.5f, dR=bd+lR*ds*0.5f;
        bufL[writePos%MAX_DELAY]=L; bufR[writePos%MAX_DELAY]=R;
        auto ri=[&](float* b,float d)->float{
            float rf=(float)writePos-d; while(rf<0)rf+=MAX_DELAY;
            int r0=(int)rf%MAX_DELAY,r1=(r0+1)%MAX_DELAY; float fr=rf-(int)rf;
            return b[r0]+fr*(b[r1]-b[r0]);};
        float wL=ri(bufL,dL), wR=ri(bufR,dR);
        ++writePos; L+=wL*0.5f; R+=wR*0.5f;
    }
};

//==============================================================================
// Scan modes (5):  0=Forward  1=Fwd Stay  2=Back&Forth  3=Bwd Stay  4=Backward
enum class ScanMode { Forward=0, FwdStay=1, BackForth=2, BwdStay=3, Backward=4 };

//==============================================================================
class TranswaveAudioProcessor : public juce::AudioProcessor,
                                 public juce::AudioProcessorValueTreeState::Listener
{
public:
    TranswaveAudioProcessor();
    ~TranswaveAudioProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "TranswaveEngine"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int  getNumPrograms()  override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged (const juce::String& paramID, float newValue) override;

    // --- Wavetable loading ---
    void loadWavetable (const juce::File& file, int singleCycleSamples, int slot);
    bool         isWavetableLoaded  (int slot) const;
    int          getNumFrames       (int slot) const;
    int          getCycleSamples    (int slot) const;
    juce::String getWavetableName   (int slot) const;
    juce::String getWavetableFilePath(int slot) const;

    float getCurrentEvoFramePos() const;  // 0..1 current position along curve
    bool  getFrameSamples      (int slot, int frameIndex, std::vector<float>& out) const;
    bool  getWavetableOverview (int slot, int displayWidth, int displayHeight, std::vector<float>& out) const;
    float sampleFrameNearest   (int slot, float frameIndex, double phase);

    // Which slot is active
    std::atomic<int> activeSlot { 0 };

    // --- Evolution curve (32 points, 0..1 each) ---
    // Thread-safe: editor writes, audio thread reads atomically
    std::atomic<float> evoCurve[EVO_POINTS];
    void setCurvePoint (int idx, float val);
    float getCurvePoint (int idx) const { return evoCurve[idx].load(); }
    // Interpolate curve at normalised position t (0..1)
    float evalCurve (float t) const;

    // Current playhead position along curve (0..1), for display
    std::atomic<float> evoPlayhead { 0.0f };

    // --- Preset persistence ---
    static juce::File getPresetsDirectory();
    bool savePreset (const juce::File& destFile);
    bool loadPreset (const juce::File& srcFile);

private:
    struct WavetableSlot
    {
        std::vector<std::vector<float>> frames;
        int   numFrames=0, cycleSamples=0;
        bool  loaded=false;
        juce::String name, filePath;
        mutable juce::CriticalSection lock;
    };
    WavetableSlot wt[2];

    static constexpr int MAX_VOICES = 16;
    TranswaveVoice voices[MAX_VOICES];
    double currentSampleRate = 44100.0;

    juce::SmoothedValue<float> gainSmooth;
    StereoBiquad filter;
    StereoChorus chorus;
    juce::Reverb reverb;

    // LFO phases
    double evoLFOPhase=0.0, posLFOPhase=0.0, pitchLFOPhase=0.0;
    // Curve playhead state
    double curvePhase=0.0;   // 0..1 along the curve
    int    scanDir=1;        // for Back&Forth
    bool   curveFinished=false; // for Stay modes

    // --- Raw param pointers ---
    std::atomic<float>* pEvoTime      = nullptr;  // 0.1..100s (log)
    std::atomic<float>* pEvoStepped   = nullptr;  // 0/1 toggle
    std::atomic<float>* pEvoLFORate   = nullptr;
    std::atomic<float>* pEvoLFODepth  = nullptr;
    std::atomic<float>* pPosLFORate   = nullptr;
    std::atomic<float>* pPosLFODepth  = nullptr;
    std::atomic<float>* pAttack       = nullptr;
    std::atomic<float>* pDecay        = nullptr;
    std::atomic<float>* pSustain      = nullptr;
    std::atomic<float>* pRelease      = nullptr;
    std::atomic<float>* pGain         = nullptr;
    std::atomic<float>* pBitCrush     = nullptr;
    std::atomic<float>* pGrit         = nullptr;
    std::atomic<float>* pDetune       = nullptr;
    std::atomic<float>* pPitchLFO     = nullptr;
    std::atomic<float>* pPitchLFORate = nullptr;
    std::atomic<float>* pScanStyle    = nullptr;
    std::atomic<float>* pJumpProb     = nullptr;
    std::atomic<float>* pFilterFreq   = nullptr;
    std::atomic<float>* pFilterQ      = nullptr;
    std::atomic<float>* pSpread       = nullptr;
    std::atomic<float>* pStereoWidth  = nullptr;
    std::atomic<float>* pUniDetune    = nullptr;
    std::atomic<float>* pStereoPhase  = nullptr;
    std::atomic<float>* pChorusRate   = nullptr;
    std::atomic<float>* pChorusDepth  = nullptr;
    std::atomic<float>* pReverbSize   = nullptr;
    std::atomic<float>* pReverbDamp   = nullptr;
    std::atomic<float>* pReverbWet    = nullptr;

    void  synthesiseVoice (TranswaveVoice& v, int slot,
                           float framePosNorm, float posLFOMod, double pitchMult,
                           float& outL, float& outR);
    float applyBitCrush   (float s, float bits);
    float sampleFrameRaw  (const WavetableSlot& s, float frameIndex, double phase) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranswaveAudioProcessor)
};
