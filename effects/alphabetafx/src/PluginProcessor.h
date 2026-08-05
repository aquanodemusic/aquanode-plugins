#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

// ==============================================================
//  Utility
// ==============================================================
inline float tanhA(float x) {
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// ==============================================================
//  Moog Ladder Filter  (Huovilainen coefficients, tanh stages)
// ==============================================================
class MoogLadder {
public:
    void setSampleRate(float sr) { sampleRate = sr; }
    int resCurve = 1;   // 0 = quadratic, 1 = cubic

    void reset() { for (auto& s : y) s = 0.0f; }

    void setParams(float cutHz, float resonance) {
        cutHz = juce::jlimit(20.0f, sampleRate * 0.45f, cutHz);
        float r;
        if (resCurve == 0) {
            r = resonance * resonance * 0.6f + resonance * 0.4f;
            r = juce::jlimit(0.0f, 0.992f, r);
        } else {
            float r2 = resonance * resonance;
            float r3 = r2 * resonance;
            r = r3 * 0.80f + r2 * 0.12f + resonance * 0.08f;
            r = juce::jlimit(0.0f, 0.995f, r);
        }
        float fc  = cutHz / sampleRate;
        float fc2 = fc * fc;
        float fc3 = fc2 * fc;
        float fcr = 1.8730f * fc3 + 0.4955f * fc2 - 0.6490f * fc + 0.9988f;
        acr  = -3.9364f * fc2 + 1.8409f * fc + 0.9968f;
        tune = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * fcr * fc);
        tune = juce::jlimit(0.0f, 1.0f, tune);
        res4 = 4.2f * r * acr;
    }

    float process24(float x) {
        x -= res4 * y[3];
        x = tanhA(x);
        y[0] += tune * (x         - tanhA(y[0]));
        y[1] += tune * (tanhA(y[0]) - tanhA(y[1]));
        y[2] += tune * (tanhA(y[1]) - tanhA(y[2]));
        y[3] += tune * (tanhA(y[2]) - tanhA(y[3]));
        return y[3];
    }

    float process24plus(float x) {
        return tanhA(process24(x) * 1.35f) / 1.35f;
    }

    float process12(float x) {
        float r2 = res4 * 0.5f;
        x -= r2 * y[1];
        x = tanhA(x);
        y[0] += tune * (x         - tanhA(y[0]));
        y[1] += tune * (tanhA(y[0]) - tanhA(y[1]));
        return y[1];
    }

private:
    float sampleRate = 44100.0f;
    float y[4] = {};
    float tune = 0.0f, acr = 1.0f, res4 = 0.0f;
};

// ==============================================================
//  State Variable Filter  (Simper/Cytomic – highly stable)
// ==============================================================
class SVFilter {
public:
    void setSampleRate(float sr) { sampleRate = sr; }
    int resCurve = 1;

    void reset() { ic1 = ic2 = 0.0f; }

    void setParams(float cutHz, float resonance) {
        float r;
        if (resCurve == 0) {
            r = resonance * resonance * 0.6f + resonance * 0.4f;
            r = juce::jlimit(0.0f, 0.992f, r);
        } else {
            float r2 = resonance * resonance;
            r = r2 * resonance * 0.80f + r2 * 0.12f + resonance * 0.08f;
            r = juce::jlimit(0.0f, 0.992f, r);
        }
        float fc = juce::jlimit(10.0f, sampleRate * 0.49f, cutHz);
        g  = std::tan(juce::MathConstants<float>::pi * fc / sampleRate);
        k  = 2.0f * (1.0f - r * 0.985f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    struct Out { float bp, hp; };
    Out process(float x) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v1, x - k * v1 - v2 };
    }

private:
    float sampleRate = 44100.0f;
    float g = 0.1f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// ==============================================================
//  Simple Stereo Chorus
// ==============================================================
class SimpleChorus {
public:
    void prepare(double sr, int /*maxBlock*/) {
        sampleRate = (float)sr;
        int maxD = (int)(sr * 0.12f) + 8;
        for (auto& b : buf) { b.assign(maxD, 0.0f); }
        wPos = 0;
        lfoPhase = 0.0f;
    }

    void reset() {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 0.0f);
        lfoPhase = 0.0f; wPos = 0;
    }

    void setWet (float v)  { wet = v; }
    void setTime(float ms) { centreD = ms * 0.001f * sampleRate; }
    void setRate(float hz) { lfoInc = hz / sampleRate; }

    void process(juce::AudioBuffer<float>& b) {
        if (b.getNumChannels() < 2 || wet < 1e-4f) return;
        float* L = b.getWritePointer(0);
        float* R = b.getWritePointer(1);
        int n  = b.getNumSamples();
        float depth = centreD * 0.35f;
        int sz = (int)buf[0].size();

        for (int i = 0; i < n; ++i) {
            float lfoL = std::sin(juce::MathConstants<float>::twoPi * lfoPhase);
            float lfoR = std::sin(juce::MathConstants<float>::twoPi * (lfoPhase + 0.25f));
            lfoPhase += lfoInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

            buf[0][wPos] = L[i];
            buf[1][wPos] = R[i];

            auto readFrac = [&](int ch, float delaySamp) -> float {
                float rp = (float)wPos - delaySamp;
                while (rp < 0) rp += sz;
                int ri = (int)rp % sz;
                int ri2 = (ri + 1) % sz;
                float f = rp - (float)(int)rp;
                return buf[ch][ri] * (1.0f - f) + buf[ch][ri2] * f;
            };

            float wL = readFrac(0, centreD + depth * lfoL);
            float wR = readFrac(1, centreD + depth * lfoR);

            L[i] = L[i] * (1.0f - wet) + wL * wet;
            R[i] = R[i] * (1.0f - wet) + wR * wet;

            wPos = (wPos + 1) % sz;
        }
    }

private:
    std::array<std::vector<float>, 2> buf;
    float sampleRate = 44100.0f;
    float lfoPhase = 0.0f, lfoInc = 0.01f;
    float centreD  = 310.0f;
    float wet = 0.0f;
    int   wPos = 0;
};

// ==============================================================
//  Filter Envelope  (amplitude-triggered OR MIDI-triggered ADSR)
//
//  AUTO mode: a built-in one-pole level follower watches the
//             input signal and auto-triggers on transients.
//  MIDI mode: noteOn() / noteOff() drive the state machine
//             directly, with velocity scaling the output.
//
//  The output (0..1, scaled by velocity in MIDI mode) modulates
//  the filter cutoff by up to ±3 octaves via the Amount knob.
// ==============================================================
class FilterEnvelope {
public:
    enum class State { Idle, Attack, Decay, Sustain, Release };

    void setSampleRate(float sr) {
        sampleRate = sr;
        levelAttCoeff = std::exp(-1.0f / (0.003f * sr));  // 3 ms follower attack (fixed)
        levelRelCoeff = std::exp(-1.0f / (0.080f * sr));  // default 80 ms – overridden by setRetriggerTime
    }

    void reset() {
        value         = 0.0f;
        levelSmooth   = 0.0f;
        velocityScale = 1.0f;
        state         = State::Idle;
        triggered     = false;
    }

    // Retrigger time: how fast the level follower decays between hits.
    // Short  → re-triggers almost instantly (good for fast rhythmic sources).
    // Long   → won't re-trigger until signal fully dies away.
    void setRetriggerTime(float ms) {
        float sec = juce::jlimit(0.005f, 1.0f, ms * 0.001f);
        levelRelCoeff = std::exp(-1.0f / (sec * sampleRate));
    }

    // Call once per block
    void setADSR(float atkSec, float dcySec, float sus, float relSec) {
        atkCoeff = makeCoeff(atkSec);
        dcyCoeff = makeCoeff(dcySec);
        susLevel = juce::jlimit(0.0f, 1.0f, sus);
        relCoeff = makeCoeff(relSec);
    }

    // Switch trigger source (call once per block)
    void setMidiMode(bool useMidi) { midiMode = useMidi; }

    // ---- MIDI trigger interface ----
    // Call these at the correct sample offset inside processBlock.
    // velocity is 0..1 (from MidiMessage::getFloatVelocity()).
    void noteOn(float velocity) {
        velocityScale = 0.3f + velocity * 0.7f;  // min 30 % even at pp
        state     = State::Attack;
        triggered = true;
    }

    void noteOff() {
        if (triggered) {
            state     = State::Release;
            triggered = false;
        }
    }

    // ---- Per-sample tick ----
    // inputAbs is used only in AUTO mode (pass 0 in MIDI mode if you like,
    // the level follower still runs so switching modes feels seamless).
    float process(float inputAbs) {

        // Always keep the level follower warm (smooth crossfade if user switches modes)
        float fc = (inputAbs > levelSmooth) ? levelAttCoeff : levelRelCoeff;
        levelSmooth = levelSmooth * fc + inputAbs * (1.0f - fc);

        // ---- AUTO mode: follower drives the trigger ----
        if (!midiMode) {
            const float kThreshold = 0.0008f;  // ~-62 dBFS
            bool active = (levelSmooth > kThreshold);

            if (active && !triggered) {
                state     = State::Attack;
                triggered = true;
                velocityScale = 1.0f;   // no velocity in auto mode
            } else if (!active && triggered) {
                if (state != State::Idle) state = State::Release;
                triggered = false;
            }
        }
        // (In MIDI mode the state is driven by noteOn / noteOff above)

        // ---- ADSR state machine ----
        switch (state) {
            case State::Attack:
                value = 1.0f - (1.0f - value) * atkCoeff;
                if (value >= 0.999f) { value = 1.0f; state = State::Decay; }
                break;

            case State::Decay:
                value = susLevel + (value - susLevel) * dcyCoeff;
                if (std::abs(value - susLevel) < 0.0003f) { value = susLevel; state = State::Sustain; }
                break;

            case State::Sustain:
                value = susLevel;
                break;

            case State::Release:
                value *= relCoeff;
                if (value < 0.0001f) { value = 0.0f; state = State::Idle; }
                break;

            default:
                value = 0.0f;
                break;
        }

        return value * velocityScale;
    }

private:
    float makeCoeff(float timeSec) const {
        return (timeSec > 0.001f) ? std::exp(-1.0f / (timeSec * sampleRate)) : 0.0f;
    }

    float sampleRate     = 44100.0f;
    float value          = 0.0f;
    float levelSmooth    = 0.0f;
    float velocityScale  = 1.0f;
    float levelAttCoeff  = 0.0f;
    float levelRelCoeff  = 0.0f;
    float atkCoeff       = 0.9f;
    float dcyCoeff       = 0.99f;
    float susLevel       = 0.5f;
    float relCoeff       = 0.99f;
    bool  triggered      = false;
    bool  midiMode       = false;
    State state          = State::Idle;
};

// ==============================================================
//  Parameter IDs
// ==============================================================
namespace FXPID {
    inline const juce::String DRV  = "drive";
    inline const juce::String FCUT = "f_cut";
    inline const juce::String FRES = "f_res";
    inline const juce::String FTYP = "f_type";
    inline const juce::String RTYP = "r_type";
    inline const juce::String HWET = "h_wet";
    inline const juce::String HTIM = "h_time";
    inline const juce::String HRAT = "h_rate";

    // Filter envelope
    inline const juce::String EON  = "env_on";
    inline const juce::String EATK = "env_atk";
    inline const juce::String EDCY = "env_dcy";
    inline const juce::String ESUS = "env_sus";
    inline const juce::String EREL = "env_rel";
    inline const juce::String EAMT = "env_amt";
    inline const juce::String EMID = "env_midi";  // false = AUTO, true = MIDI
    inline const juce::String ERTG = "env_rtg";   // retrigger time (ms) – AUTO mode only
}

// ==============================================================
//  Audio Processor
// ==============================================================
class AlphaBetaFXAudioProcessor : public juce::AudioProcessor {
public:
    AlphaBetaFXAudioProcessor();
    ~AlphaBetaFXAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        auto& in  = layouts.getMainInputChannelSet();
        auto& out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo()) return false;
        if (in  != juce::AudioChannelSet::stereo() &&
            in  != juce::AudioChannelSet::mono())   return false;
        return true;
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName()   const override { return "AlphaBetaFX"; }
    bool  acceptsMidi()            const override { return true;  }
    bool  producesMidi()           const override { return false; }
    bool  isMidiEffect()           const override { return false; }
    double getTailLengthSeconds()  const override { return 2.0; }  // allow env release tail

    int  getNumPrograms()          override { return 1; }
    int  getCurrentProgram()       override { return 0; }
    void setCurrentProgram(int)    override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    MoogLadder ladderL, ladderR;
    SVFilter   svfL,    svfR;
    SimpleChorus chorus;
    FilterEnvelope filterEnv;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaBetaFXAudioProcessor)
};
