#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>
#include "ModMatrix.h"
#include "Wavetable.h"
#include "InternalWaves.h"

// ==============================================================
//  Utility
// ==============================================================

inline float tanhA(float x) {
    // Pade approximant - fast, accurate for |x| < 4
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float polyBlep(float phase, float dt) {
    if (phase < dt) { float t = phase / dt;        return  t + t - t * t - 1.0f; }
    if (phase > 1.0f - dt) { float t = (phase - 1.0f) / dt; return  t * t + t + t + 1.0f; }
    return 0.0f;
}

// Phase warp used for "Symmetry". sym = 0.5 leaves the phase untouched;
// other values move the waveform's midpoint to raw phase `sym`.
inline float symWarp(float ph, float sym) {
    return ph < sym ? 0.5f * ph / sym
                    : 0.5f + 0.5f * (ph - sym) / (1.0f - sym);
}

// ==============================================================
//  Anti-aliased single oscillator (PolyBLEP) with symmetry
// ==============================================================
class MonoOsc {
public:
    // 0..5 as below; 6.. = internal waves (see InternalWaves.h), which play
    // through the table path just like a user wavetable.
    enum Wave { SINE = 0, TRI, SAW, SQUARE, NOISE, TABLE };
    int wave = SINE;

    void reset(float startPhase = 0.0f) { phase = startPhase; triAcc = -1.0f; }

    // phaseOffset = FM modulation index * modulator output (in cycles).
    // sym = waveform symmetry 0.05..0.95 (0.5 = the plain waveform).
    // wt / wtPos are used by the TABLE wave (falls back to sine without a table).
    float tick(float freq, float sr, float phaseOffset = 0.0f, float sym = 0.5f,
               const Wavetable* wt = nullptr, float wtPos = 0.0f) {
        float dt = freq / sr;
        if (dt <= 0.0f || dt >= 0.5f) return 0.0f;
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;

        float ph = phase + phaseOffset;
        ph -= std::floor(ph);

        switch (wave) {
        case SINE:
            return std::sin(juce::MathConstants<float>::twoPi * symWarp(ph, sym));
        case TRI: {
            // Integrated pulse. The pulse width follows symmetry; its DC is
            // removed so the integrator stays centred, then the result is
            // normalised back to +-1 (identical to before at sym = 0.5).
            float sq = (ph < sym ? 1.0f : -1.0f);
            sq += polyBlep(ph, dt);
            float p2 = ph - sym; if (p2 < 0.0f) p2 += 1.0f;
            sq -= polyBlep(p2, dt);
            sq -= (2.0f * sym - 1.0f);
            triAcc = triAcc * (1.0f - 2e-4f) + 4.0f * dt * sq;
            float norm = 1.0f / (4.0f * sym * (1.0f - sym));
            return juce::jlimit(-1.0f, 1.0f, triAcc * norm);
        }
        case SAW: {
            float s = 2.0f * symWarp(ph, sym) - 1.0f;
            s -= polyBlep(ph, dt);
            return s;
        }
        case SQUARE: {
            // Symmetry = pulse width
            float s = (ph < sym ? 1.0f : -1.0f);
            s += polyBlep(ph, dt);
            float p2 = ph - sym; if (p2 < 0.0f) p2 += 1.0f;
            s -= polyBlep(p2, dt);
            return s;
        }
        case NOISE:
            return rng.nextFloat() * 2.0f - 1.0f;
        default:   // TABLE and internal waves
            if (wt != nullptr) return wt->sample(wtPos, symWarp(ph, sym), dt);
            return std::sin(juce::MathConstants<float>::twoPi * symWarp(ph, sym));
        }
    }

private:
    float phase = 0.0f;
    float triAcc = -1.0f;
    juce::Random rng{ 0 };
};

// ==============================================================
//  Moog Ladder Filter  (Huovilainen coefficients, tanh stages)
//  LP12 / LP24 / LP24+ modes
// ==============================================================
class MoogLadder {
public:
    void setSampleRate(float sr) { sampleRate = sr; }

    // 0 = quadratic (smooth), 1 = cubic (steep/aggressive)
    int resCurve = 1;

    void reset() {
        for (auto& s : y) s = 0.0f;
    }

    void setParams(float cutHz, float resonance) {
        cutHz = juce::jlimit(20.0f, sampleRate * 0.45f, cutHz);

        float r;
        if (resCurve == 0) {
            // Quadratic – smooth, musical
            r = resonance * resonance * 0.6f + resonance * 0.4f;
            r = juce::jlimit(0.0f, 0.992f, r);
        }
        else {
            // Cubic – steep, self-oscillates in top quarter
            float r2 = resonance * resonance;
            float r3 = r2 * resonance;
            r = r3 * 0.80f + r2 * 0.12f + resonance * 0.08f;
            r = juce::jlimit(0.0f, 0.995f, r);
        }

        float fc = cutHz / sampleRate;
        float fc2 = fc * fc;
        float fc3 = fc2 * fc;
        // Huovilainen frequency response corrections
        float fcr = 1.8730f * fc3 + 0.4955f * fc2 - 0.6490f * fc + 0.9988f;
        acr = -3.9364f * fc2 + 1.8409f * fc + 0.9968f;
        tune = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * fcr * fc);
        tune = juce::jlimit(0.0f, 1.0f, tune);
        res4 = 4.2f * r * acr;
    }

    // 24 dB/oct Low Pass
    float process24(float x) {
        x -= res4 * y[3];
        x = tanhA(x);
        y[0] += tune * (x - tanhA(y[0]));
        y[1] += tune * (tanhA(y[0]) - tanhA(y[1]));
        y[2] += tune * (tanhA(y[1]) - tanhA(y[2]));
        y[3] += tune * (tanhA(y[2]) - tanhA(y[3]));
        return y[3];
    }

    // 24 dB/oct LP + extra saturation ("bubbly" mode)
    float process24plus(float x) {
        float out = process24(x);
        return tanhA(out * 1.35f) / 1.35f;
    }

    // 12 dB/oct Low Pass (2 stages, feedback from stage 1)
    float process12(float x) {
        float r2 = res4 * 0.5f;
        x -= r2 * y[1];
        x = tanhA(x);
        y[0] += tune * (x - tanhA(y[0]));
        y[1] += tune * (tanhA(y[0]) - tanhA(y[1]));
        return y[1];
    }

private:
    float sampleRate = 44100.0f;
    float y[4] = {};
    float tune = 0.0f, acr = 1.0f, res4 = 0.0f;
};

// ==============================================================
//  State Variable Filter  (Simper/Cytomic - highly stable)
//  Band Pass / High Pass modes
// ==============================================================
class SVFilter {
public:
    void setSampleRate(float sr) { sampleRate = sr; }

    // 0 = quadratic (smooth), 1 = cubic (steep)
    int resCurve = 1;

    void reset() { ic1 = ic2 = 0.0f; }

    void setParams(float cutHz, float resonance) {
        // Apply the same taper as MoogLadder so both filter banks feel consistent
        float r;
        if (resCurve == 0) {
            r = resonance * resonance * 0.6f + resonance * 0.4f;
            r = juce::jlimit(0.0f, 0.992f, r);
        }
        else {
            float r2 = resonance * resonance;
            r = r2 * resonance * 0.80f + r2 * 0.12f + resonance * 0.08f;
            r = juce::jlimit(0.0f, 0.992f, r);
        }
        float fc = juce::jlimit(10.0f, sampleRate * 0.49f, cutHz);
        g = std::tan(juce::MathConstants<float>::pi * fc / sampleRate);
        k = 2.0f * (1.0f - r * 0.985f);
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
//  ADSR + Fade Envelope
// ==============================================================
class ADSRFade {
public:
    struct Params {
        float attack = 0.01f;
        float decay = 0.3f;
        float sustain = 0.7f;
        float release = 0.5f;
        float fade = 0.5f;  // 0 = fade to 0, 0.5 = hold, 1 = fade to 1
    };

    void setSampleRate(float sr) { sampleRate = sr; }

    void noteOn() { stage = ATTACK; level = 0.0f; }
    // Start at peak and decay – used when filterDepth < 0 so filter
    // begins fully closed and opens as the envelope falls.
    void noteOnFromPeak() { stage = DECAY; level = 1.0f; }
    void retrigger() { stage = ATTACK; }          // attack from the current level
    void noteOff() { if (stage != IDLE) stage = RELEASE; }

    float tick(const Params& p) {
        switch (stage) {
        case ATTACK: {
            float step = p.attack > 1e-4f ? 1.0f / (p.attack * sampleRate) : 1.0f;
            level += step;
            if (level >= 1.0f) { level = 1.0f; stage = DECAY; }
            break;
        }
        case DECAY: {
            float step = p.decay > 1e-4f ? 1.0f / (p.decay * sampleRate) : 1.0f;
            level -= step;
            if (level <= p.sustain) { level = p.sustain; stage = SUSTAIN; }
            break;
        }
        case SUSTAIN: {
            if (p.fade < 0.48f) {
                float speed = (0.5f - p.fade) * 2.0f;
                level -= speed * 0.3f / sampleRate;
                if (level < 0.0f) level = 0.0f;
            }
            else if (p.fade > 0.52f) {
                float speed = (p.fade - 0.5f) * 2.0f;
                level += speed * 0.3f / sampleRate;
                if (level > 1.0f) level = 1.0f;
            }
            break;
        }
        case RELEASE: {
            float step = p.release > 1e-4f ? 1.0f / (p.release * sampleRate) : 1.0f;
            level -= step;
            if (level <= 0.0f) { level = 0.0f; stage = IDLE; }
            break;
        }
        case IDLE:
            level = 0.0f;
            break;
        }
        return level;
    }

    bool isActive() const { return stage != IDLE; }

private:
    enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE } stage = IDLE;
    float level = 0.0f;
    float sampleRate = 44100.0f;
};

// ==============================================================
//  Simple Stereo Chorus (self-contained, no juce::dsp::Chorus)
// ==============================================================
class SimpleChorus {
public:
    void prepare(double sr, int maxBlock) {
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

    void setWet(float v) { wet = v; }
    void setTime(float ms) { centreD = ms * 0.001f * sampleRate; }
    void setRate(float hz) { lfoInc = hz / sampleRate; }

    void process(juce::AudioBuffer<float>& b) {
        if (b.getNumChannels() < 2 || wet < 1e-4f) return;
        float* L = b.getWritePointer(0);
        float* R = b.getWritePointer(1);
        int n = b.getNumSamples();
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
    float centreD = 310.0f; // in samples
    float wet = 0.0f;
    int wPos = 0;
};

// ==============================================================
//  Parameter ID strings
// ==============================================================
namespace PID {
    // OSC1
    inline const juce::String O1WA = "o1_wave_a";
    inline const juce::String O1OCA = "o1_oct_a";
    inline const juce::String O1WB = "o1_wave_b";
    inline const juce::String O1OCB = "o1_oct_b";
    inline const juce::String O1MRP = "o1_morph";
    inline const juce::String O1DET = "o1_detune";
    // OSC2
    inline const juce::String O2WA = "o2_wave_a";
    inline const juce::String O2OCA = "o2_oct_a";
    inline const juce::String O2WB = "o2_wave_b";
    inline const juce::String O2OCB = "o2_oct_b";
    inline const juce::String O2MRP = "o2_morph";
    inline const juce::String O2DET = "o2_detune";
    // Mix
    inline const juce::String MIX = "osc_mix";
    inline const juce::String DRV = "drive";
    inline const juce::String FM = "fm";
    inline const juce::String SPR = "spread";
    // Filter
    inline const juce::String FCUT = "f_cut";
    inline const juce::String FRES = "f_res";
    inline const juce::String FTYP = "f_type";
    inline const juce::String FATT = "f_att";
    inline const juce::String FDEC = "f_dec";
    inline const juce::String FSUS = "f_sus";
    inline const juce::String FREL = "f_rel";
    inline const juce::String FFAD = "f_fade";
    inline const juce::String FDEP = "f_depth";
    // Amp
    inline const juce::String AVOL = "a_vol";
    inline const juce::String AVEL = "a_vel";
    inline const juce::String AATT = "a_att";
    inline const juce::String ADEC = "a_dec";
    inline const juce::String ASUS = "a_sus";
    inline const juce::String AREL = "a_rel";
    inline const juce::String AFAD = "a_fade";
    // Chorus
    inline const juce::String HWET = "h_wet";
    inline const juce::String HTIM = "h_time";
    inline const juce::String HRAT = "h_rate";
    // Glide
    inline const juce::String GLID = "glide";
    // Resonance curve
    inline const juce::String RTYP = "r_type";
    // Oscillator pitch (semitones)
    inline const juce::String O1PIT = "o1_pitch";
    inline const juce::String O2PIT = "o2_pitch";
    // --- New in 1.2: Alpha 3 features ---
    inline const juce::String NOISE = "noise";      // noise mix (Alpha "noise" dial)
    inline const juce::String RING = "ringmod";     // osc2 ring modulation amount
    inline const juce::String FFM = "f_fm";         // filter cutoff FM depth
    inline const juce::String FFMS = "f_fm_src";    // filter FM source
    inline const juce::String BEND = "bend_range";  // pitch wheel range
    // --- New in 1.3 ---
    inline const juce::String UNI = "unison";       // Alpha "spread": 3 detuned voices per note
    inline const juce::String O1WTP = "o1_wt_pos";  // OSC1 wave A wavetable position
    inline const juce::String O2WTP = "o2_wt_pos";   // OSC2 wave A (id kept from 1.3)
    inline const juce::String O1BWTP = "o1b_wt_pos"; // OSC1 wave B (1.5)
    inline const juce::String O2BWTP = "o2b_wt_pos"; // OSC2 wave B (1.5)
    inline const juce::String TUNE = "tune";         // master tune, cents (1.5)
    // --- New in 1.4 ---
    inline const juce::String VMODE = "voice_mode"; // Poly / Mono / Legato
    inline const juce::String ANALOG = "analog";    // Alpha "analogness"
}

// ==============================================================
//  Synth Sound
// ==============================================================
struct AlphaSound : public juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// ==============================================================
//  Linear ramp for control-rate values (avoids zipper noise)
// ==============================================================
struct Ramp {
    float cur = 0.0f, inc = 0.0f;
    void jump(float v) { cur = v; inc = 0.0f; }
    void go(float target, int n) { inc = (target - cur) / (float)n; }
    float next() { cur += inc; return cur; }
};

// ==============================================================
//  Synth Voice
// ==============================================================
class AlphaVoice : public juce::SynthesiserVoice {
public:
    // ---- Parameters set by the processor each block ----
    int o1wA = MonoOsc::SAW, o1wB = MonoOsc::SINE;
    int o2wA = MonoOsc::SAW, o2wB = MonoOsc::SINE;
    int o1octA = 0, o1octB = 0, o2octA = 0, o2octB = 0;
    float o1morph = 0.0f, o2morph = 0.0f;
    float o1detune = 0.0f, o2detune = 0.0f;   // cents
    float o1pitch = 0.0f, o2pitch = 0.0f;     // semitones
    float oscMix = 0.5f, drive = 0.0f, fm = 0.0f;
    float noiseMix = 0.0f, ringMix = 0.0f;
    float unison = 0.0f;                     // 0 = off, 1 = widest detune
    // Wavetable slots: 0 = OSC1 A, 1 = OSC1 B, 2 = OSC2 A, 3 = OSC2 B
    std::array<float, 4> wtPos{};            // positions (0..1)
    std::array<const Wavetable*, 4> wtSlot{};// user tables, valid for the current block
    float tuneCents = 0.0f;                  // master tune
    float analog = 0.0f;                     // 0..1 "analogness"
    bool  glideOnStart = true;               // false: new notes jump (legato mode, staccato)
    float filterFM = 0.0f; int filterFMSrc = 0; // 0=Osc1, 1=Osc2, 2=Noise
    float glideTime = 0.0f;
    int   filterType = 1;                    // 0=LP12,1=LP24,2=LP24+,3=BP,4=HP
    int   resCurve = 1;                      // 0=quadratic, 1=cubic
    float filterCutoff = 2000.0f, filterRes = 0.3f;
    float filterDepth = 1.0f;
    ADSRFade::Params fEnvP, aEnvP;
    float ampVol = 0.8f, ampVelSens = 0.5f;
    int   voiceIdx = 0;                      // for panning in spread
    float stereoSpread = 0.0f;

    const Mod::Context* ctx = nullptr;       // shared modulation context

    AlphaVoice() {
        fEnv.setSampleRate(44100.0f);
        aEnv.setSampleRate(44100.0f);
    }

    void prepareVoice(double sr) {
        float f = (float)sr;
        fEnv.setSampleRate(f);
        aEnv.setSampleRate(f);
        ladder.setSampleRate(f);
        svf.setSampleRate(f);
    }

    bool canPlaySound(juce::SynthesiserSound* s) override {
        return dynamic_cast<AlphaSound*>(s) != nullptr;
    }

    void startNote(int midiNote, float vel, juce::SynthesiserSound*, int) override {
        velocity = vel;
        noteNumber = midiNote;
        polyAftertouch = 0.0f;
        float target = (float)juce::MidiMessage::getMidiNoteInHertz(midiNote);

        if (glideOnStart && glideTime > 0.001f && currentFreq > 10.0f)
            targetFreq = target;
        else { currentFreq = target; targetFreq = target; }

        // Analog: a small random offset per note (pitch and cutoff)
        noteDetune = analog * 6.0f * (rng.nextFloat() * 2.0f - 1.0f);        // cents
        noteCutMul = std::exp2(analog * 0.2f * (rng.nextFloat() * 2.0f - 1.0f));

        // A voice that is still sounding (stolen, or retriggered in mono) keeps its
        // oscillator phases, filter state and envelope levels, so there is no click.
        const bool sounding = aEnv.isActive();

        if (!sounding) {
            for (int c = 0; c < UNI; ++c) {
                float ph = c == 0 ? 0.0f : 0.31f * (float)c + 0.07f * (float)(voiceIdx % 5);
                o1a[c].reset(ph); o1b[c].reset(ph); o2a[c].reset(ph); o2b[c].reset(ph);
            }
            ladder.reset(); svf.reset();
        }
        // Negative depth: start at envelope peak so the filter begins fully
        // closed and slowly opens during decay ("bass slowly incoming").
        // Positive depth: normal attack-up behaviour (filter sweeps high->low).
        if (filterDepth < -0.001f)       fEnv.noteOnFromPeak();
        else if (sounding)               fEnv.retrigger();
        else                             fEnv.noteOn();
        if (sounding) aEnv.retrigger(); else aEnv.noteOn();

        // Modulation state
        for (int k = 0; k < Mod::NUM_LFOS; ++k) polyLfoPhase[k] = 0.0;
        lfoSeed = (uint64_t)(++noteCounter) * 7919ull + (uint64_t)voiceIdx;
        noteTime = 0.0f;
        ctrlCountdown = 0;
        firstControl = true;
        alive = true;
    }

    // Mono/legato: move to a new note without restarting the envelopes
    void legatoTo(int midiNote, bool glide) {
        noteNumber = midiNote;
        targetFreq = (float)juce::MidiMessage::getMidiNoteInHertz(midiNote);
        if (!glide || glideTime <= 0.001f) currentFreq = targetFreq;
    }

    void stopNote(float, bool allowTail) override {
        if (allowTail) { fEnv.noteOff(); aEnv.noteOff(); }
        else { clearCurrentNote(); alive = false; }
    }

    void aftertouchChanged(int v) override { polyAftertouch = (float)v / 127.0f; }

    void renderNextBlock(juce::AudioBuffer<float>& buf, int start, int num) override {
        if (!alive && !aEnv.isActive()) { clearCurrentNote(); return; }

        const float sr = (float)getSampleRate();
        const float sri = 1.0f / sr;

        for (int c = 0; c < UNI; ++c) {
            o1a[c].wave = o1wA; o1b[c].wave = o1wB;
            o2a[c].wave = o2wA; o2b[c].wave = o2wB;
        }
        // Table source per wave slot: user table, internal wave, or none
        auto tableFor = [](int w, const Wavetable* user) -> const Wavetable* {
            return w == MonoOsc::TABLE ? user : InternalWaves::forWave(w);
        };
        const Wavetable* t1A = tableFor(o1wA, wtSlot[0]);
        const Wavetable* t1B = tableFor(o1wB, wtSlot[1]);
        const Wavetable* t2A = tableFor(o2wA, wtSlot[2]);
        const Wavetable* t2B = tableFor(o2wB, wtSlot[3]);

        const float glCoeff = (glideTime > 0.001f)
            ? (1.0f - std::exp(-sri / glideTime)) : 1.0f;

        float panL = 1.0f, panR = 1.0f;
        if (stereoSpread > 0.001f) {
            float panPos = ((voiceIdx % 2 == 0) ? 1.0f : -1.0f) * stereoSpread * 0.4f;
            panL = std::sqrt(0.5f * (1.0f - panPos));
            panR = std::sqrt(0.5f * (1.0f + panPos));
        }
        const bool hasStereo = (buf.getNumChannels() >= 2);

        for (int i = 0; i < num; ++i) {
            // ---- Control-rate modulation update ----
            if (ctrlCountdown <= 0) {
                int n = juce::jmin(Mod::CTRL_RATE, num - i);
                updateModulation(start + i, n, sr);
                ctrlCountdown = n;
            }
            --ctrlCountdown;

            // Glide
            currentFreq += (targetFreq - currentFreq) * glCoeff;
            const float baseFreq = currentFreq * mainPitchMul;

            // Oscillators. Copy 0 is the plain voice; copies 1 and 2 are the
            // unison voices (Alpha "spread"), detuned against copy 0.
            const float ring = rRing.next();
            const float g1 = rOsc1Amp.next(), g2 = rOsc2Amp.next();
            const float side = rUniSide.next();
            const int copies = (side > 1e-4f || uniOn) ? UNI : 1;
            const float wp1A = wtPosNow[0], wp1B = wtPosNow[1], wp2A = wtPosNow[2], wp2B = wtPosNow[3];

            // Mix: OSC2 fades out of audio as FM rises (it becomes a pure modulator)
            const float fmFade = juce::jlimit(0.0f, 1.0f, fm);
            const float osc2Gain = oscMix * (1.0f - fmFade);
            const float osc1Gain = 1.0f - osc2Gain;

            float sig = 0.0f, osc1 = 0.0f, osc2 = 0.0f;
            for (int c = 0; c < copies; ++c) {
                const float bf = baseFreq * uniMul[c];
                // OSC2 - also the FM modulator for OSC1
                float o2 = o2a[c].tick(bf * f2Am, sr, 0.0f, sym2, t2A, wp2A) * (1.0f - o2morph)
                         + o2b[c].tick(bf * f2Bm, sr, 0.0f, sym2, t2B, wp2B) * o2morph;
                // True 2-op FM: OSC2 phase-modulates OSC1
                float fmPhase = o2 * fm;
                float o1 = o1a[c].tick(bf * f1Am, sr, fmPhase, sym1, t1A, wp1A) * (1.0f - o1morph)
                         + o1b[c].tick(bf * f1Bm, sr, fmPhase, sym1, t1B, wp1B) * o1morph;
                // Ring modulator on OSC2 (Alpha "ringmod": 0 = osc2, 1 = osc1*osc2)
                float o2Out = o2 * (1.0f - ring) + o1 * o2 * ring;
                float mixC = o1 * osc1Gain * g1 + o2Out * osc2Gain * g2;
                if (c == 0) { sig = mixC; osc1 = o1; osc2 = o2; }
                else        sig += mixC * side;
            }
            // keep loudness roughly constant as the unison copies fade in
            if (copies > 1) sig *= 1.0f / std::sqrt(1.0f + 2.0f * side * side);

            // Noise (Alpha: full right = only noise)
            float white = nextNoise();
            float nz = rNoise.next();
            sig = sig * (1.0f - nz) + white * nz;

            // Pre-filter drive (soft saturation)
            float driveG = 1.0f + drive * 6.0f;
            sig = tanhA(sig * driveG) / (0.4f + drive * 0.6f + 0.6f);

            // Filter envelope
            float envV = fEnv.tick(fEnvP);
            lastFiltEnv = envV;

            // Env modulates cutoff in octaves (filterDepth: -1..+1, 4 oct range)
            float octs = envV * filterDepth * 4.0f + rCutOct.next();
            // Audio-rate filter FM
            if (ffmNow > 0.0001f) {
                float fsrc = filterFMSrc == 0 ? osc1 : (filterFMSrc == 1 ? osc2 : white);
                octs += fsrc * ffmNow * 4.0f;
            }
            float cutMod = filterCutoff * noteCutMul * std::exp2(octs);
            cutMod = juce::jlimit(20.0f, 20000.0f, cutMod);

            float filtered;
            ladder.resCurve = resCurve;
            svf.resCurve = resCurve;
            switch (filterType) {
            case 0:  ladder.setParams(cutMod, resNow); filtered = ladder.process12(sig);     break;
            case 2:  ladder.setParams(cutMod, resNow); filtered = ladder.process24plus(sig); break;
            case 3:  svf.setParams(cutMod, resNow);    filtered = svf.process(sig).bp;       break;
            case 4:  svf.setParams(cutMod, resNow);    filtered = svf.process(sig).hp;       break;
            default: ladder.setParams(cutMod, resNow); filtered = ladder.process24(sig);     break;
            }

            // Amp envelope
            float amp = aEnv.tick(aEnvP);
            lastAmpEnv = amp;
            float velF = 1.0f - ampVelSens + ampVelSens * velocity;
            float out = filtered * amp * velF * ampVol * 0.5f * rMainAmp.next();

            if (buf.getNumChannels() >= 1) buf.addSample(0, start + i, out * panL);
            if (hasStereo)                 buf.addSample(1, start + i, out * panR);

            if (!aEnv.isActive()) {
                clearCurrentNote();
                alive = false;
                return;
            }
        }
    }

    void pitchWheelMoved(int) override {}
    void controllerMoved(int, int) override {}

private:
    // --------------------------------------------------------------
    //  Modulation: evaluate the matrix once every CTRL_RATE samples
    // --------------------------------------------------------------
    float lfoValue(int k, int sampleInBlock, float sr) {
        const auto& lp = ctx->lfo[(size_t)k];
        double ph;
        uint64_t seed;
        if (lp.poly) {
            ph = polyLfoPhase[k];
            seed = lfoSeed * 31ull + (uint64_t)k;
        } else {
            ph = ctx->monoPhase0[(size_t)k] + ctx->monoInc[(size_t)k] * sampleInBlock;
            seed = 1000ull + (uint64_t)k;         // shared by all voices
        }
        float v = Mod::lfoShape(lp.wave, ph, seed);
        // LFO attack: fade the depth in after note-on
        if (lp.attack > 0.001f)
            v *= juce::jmin(1.0f, noteTime / lp.attack);
        juce::ignoreUnused(sr);
        return v;
    }

    float sourceValue(int s, const std::array<float, Mod::NUM_LFOS>& lfo) const {
        const auto& m = ctx->midi;
        switch (s) {
        case Mod::SRC_NOTE_LOG:   return juce::jlimit(-1.0f, 1.0f, std::exp2(((float)noteNumber - 60.0f) / 24.0f) - 1.0f);
        case Mod::SRC_NOTE_LIN:   return juce::jlimit(-1.0f, 1.0f, ((float)noteNumber - 60.0f) / 60.0f);
        case Mod::SRC_VELOCITY:   return velocity;
        case Mod::SRC_AFTERTOUCH: return juce::jmax(polyAftertouch, m.channelPressure);
        case Mod::SRC_PITCHWHEEL: return m.pitchWheel;
        case Mod::SRC_MODWHEEL:   return m.cc[1];
        case Mod::SRC_BREATH:     return m.cc[2];
        case Mod::SRC_FOOT:       return m.cc[4];
        case Mod::SRC_EXPRESSION: return m.cc[11];
        case Mod::SRC_CC16:       return m.cc[16] * 2.0f - 1.0f;
        case Mod::SRC_CC17:       return m.cc[17] * 2.0f - 1.0f;
        case Mod::SRC_CC18:       return m.cc[18] * 2.0f - 1.0f;
        case Mod::SRC_CC19:       return m.cc[19] * 2.0f - 1.0f;
        case Mod::SRC_AMP_ENV:    return lastAmpEnv;
        case Mod::SRC_FILT_ENV:   return lastFiltEnv;
        case Mod::SRC_LFO1:       return lfo[0];
        case Mod::SRC_LFO2:       return lfo[1];
        case Mod::SRC_LFO3:       return lfo[2];
        case Mod::SRC_CONSTANT:   return 1.0f;
        default:                  return 0.0f;
        }
    }

    void updateModulation(int sampleInBlock, int n, float sr) {
        std::array<float, Mod::NUM_DESTS> d{};

        if (ctx != nullptr) {
            std::array<float, Mod::NUM_LFOS> lfo{};
            for (int k = 0; k < Mod::NUM_LFOS; ++k)
                lfo[(size_t)k] = lfoValue(k, sampleInBlock, sr);

            // Pass 1: slots that scale the depth of slots 1-3
            std::array<float, 3> depthSum{};
            std::array<bool, 3>  depthUsed{};
            for (const auto& sl : ctx->slots) {
                if (sl.dst >= Mod::DST_MDEPTH1 && sl.dst <= Mod::DST_MDEPTH3 && sl.src != Mod::SRC_OFF) {
                    int k = sl.dst - Mod::DST_MDEPTH1;
                    depthSum[(size_t)k] += sl.amt * sourceValue(sl.src, lfo);
                    depthUsed[(size_t)k] = true;
                }
            }
            // Pass 2: everything else
            for (int i = 0; i < Mod::NUM_SLOTS; ++i) {
                const auto& sl = ctx->slots[(size_t)i];
                if (sl.src == Mod::SRC_OFF || sl.dst == Mod::DST_OFF) continue;
                if (sl.dst >= Mod::DST_MDEPTH1 && sl.dst <= Mod::DST_MDEPTH3) continue;
                float scale = 1.0f;
                if (i < 3 && depthUsed[(size_t)i])
                    scale = juce::jlimit(-1.0f, 1.0f, depthSum[(size_t)i]);
                d[(size_t)sl.dst] += sl.amt * scale * sourceValue(sl.src, lfo);
            }

            // Advance poly LFO phases (speed modulation applies to LFO 1 and 2)
            for (int k = 0; k < Mod::NUM_LFOS; ++k) {
                const auto& lp = ctx->lfo[(size_t)k];
                if (!lp.poly) continue;
                double hz = Mod::lfoRateHz(lp, ctx->bpm);
                if (k == 0) hz *= std::exp2(d[Mod::DST_LFO1_SPEED] * Mod::LFO_SPEED_OCT_FULL);
                if (k == 1) hz *= std::exp2(d[Mod::DST_LFO2_SPEED] * Mod::LFO_SPEED_OCT_FULL);
                polyLfoPhase[k] += hz * (double)n / (double)sr;
            }
        }
        noteTime += (float)n / sr;

        // ---- Pitch (semitones) ----
        const float bend = ctx != nullptr ? ctx->midi.pitchWheel * ctx->bendRange : 0.0f;
        // Analog: slow random pitch drift on top of the per-note offset
        if (analog > 0.0001f) {
            driftTimer -= (float)n / sr;
            if (driftTimer <= 0.0f) { driftTarget = rng.nextFloat() * 2.0f - 1.0f; driftTimer = 0.15f + 0.3f * rng.nextFloat(); }
            drift += (driftTarget - drift) * juce::jmin(1.0f, (float)n / sr * 3.0f);
        }
        const float analogCents = noteDetune + analog * 4.0f * drift + tuneCents;
        mainPitchMul = std::exp2((d[Mod::DST_MAIN_PITCH] * Mod::PITCH_SEMIS_FULL + bend + analogCents * 0.01f) / 12.0f);
        const float p1 = d[Mod::DST_OSC1_PITCH] * Mod::PITCH_SEMIS_FULL;
        const float p2 = d[Mod::DST_OSC2_PITCH] * Mod::PITCH_SEMIS_FULL;
        f1Am = std::exp2((float)o1octA + (o1pitch + p1) / 12.0f);
        f1Bm = std::exp2((float)o1octB + (o1pitch + p1) / 12.0f + o1detune / 1200.0f);
        f2Am = std::exp2((float)o2octA + (o2pitch + p2) / 12.0f);
        f2Bm = std::exp2((float)o2octB + (o2pitch + p2) / 12.0f + o2detune / 1200.0f);

        // ---- Unison (Alpha "spread") ----
        // Copies 1/2 sit above/below copy 0 and drift slowly, so the
        // detuning never settles into a static beat.
        {
            const float u = juce::jlimit(0.0f, 1.0f, unison + d[Mod::DST_UNISON]);
            uniOn = u > 0.0005f;
            const float cents = 30.0f * u;
            const float dtSec = (float)n / sr;
            uniDrift[0] += 0.13f * dtSec; uniDrift[1] += 0.19f * dtSec;
            for (auto& ph : uniDrift) if (ph > 1.0f) ph -= 1.0f;
            const float tw = juce::MathConstants<float>::twoPi;
            uniMul[1] = std::exp2(+cents * (0.85f + 0.15f * std::sin(tw * uniDrift[0])) / 1200.0f);
            uniMul[2] = std::exp2(-cents * (0.85f + 0.15f * std::sin(tw * uniDrift[1])) / 1200.0f);
            const float tSide = juce::jmin(1.0f, u * 12.0f);
            if (firstControl) rUniSide.jump(tSide); else rUniSide.go(tSide, n);
        }

        // ---- Wavetable positions ----
        // "Osc N WT Pos" moves both wave slots of that oscillator
        for (int k = 0; k < 4; ++k)
            wtPosNow[(size_t)k] = juce::jlimit(0.0f, 1.0f, wtPos[(size_t)k] + d[k < 2 ? Mod::DST_OSC1_WTPOS : Mod::DST_OSC2_WTPOS]);

        // ---- Symmetry ----
        sym1 = juce::jlimit(0.05f, 0.95f, 0.5f + 0.45f * d[Mod::DST_OSC1_SYM]);
        sym2 = juce::jlimit(0.05f, 0.95f, 0.5f + 0.45f * d[Mod::DST_OSC2_SYM]);

        // ---- Filter ----
        resNow = juce::jlimit(0.0f, 1.0f, filterRes + d[Mod::DST_RESONANCE]);
        ffmNow = juce::jlimit(0.0f, 1.0f, filterFM + d[Mod::DST_CUTOFF_FM]);

        // ---- Ramped targets ----
        const float tOsc1 = juce::jlimit(0.0f, 4.0f, 1.0f + d[Mod::DST_OSC1_AMP]);
        const float tOsc2 = juce::jlimit(0.0f, 4.0f, 1.0f + d[Mod::DST_OSC2_AMP]);
        const float tMain = juce::jlimit(0.0f, 4.0f, 1.0f + d[Mod::DST_MAIN_AMP]);
        const float tNoise = juce::jlimit(0.0f, 1.0f, noiseMix + d[Mod::DST_NOISE_AMP]);
        const float tRing = juce::jlimit(0.0f, 1.0f, ringMix + d[Mod::DST_OSC2_RING]);
        const float tCut = d[Mod::DST_CUTOFF] * Mod::CUTOFF_OCT_FULL;

        if (firstControl) {
            rOsc1Amp.jump(tOsc1); rOsc2Amp.jump(tOsc2); rMainAmp.jump(tMain);
            rNoise.jump(tNoise); rRing.jump(tRing); rCutOct.jump(tCut);
            firstControl = false;
        } else {
            rOsc1Amp.go(tOsc1, n); rOsc2Amp.go(tOsc2, n); rMainAmp.go(tMain, n);
            rNoise.go(tNoise, n); rRing.go(tRing, n); rCutOct.go(tCut, n);
        }
    }

    float nextNoise() {
        noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
        return (float)(int32_t)noiseState * (1.0f / 2147483648.0f);
    }

    static constexpr int UNI = 3;            // voice + 2 unison copies
    MonoOsc o1a[UNI], o1b[UNI], o2a[UNI], o2b[UNI];
    float uniMul[UNI] = { 1.0f, 1.0f, 1.0f };
    float uniDrift[2] = { 0.0f, 0.5f };
    bool  uniOn = false;
    std::array<float, 4> wtPosNow{};
    Ramp  rUniSide;
    juce::Random rng;
    float noteDetune = 0.0f, noteCutMul = 1.0f;
    float drift = 0.0f, driftTarget = 0.0f, driftTimer = 0.0f;
    MoogLadder ladder;
    SVFilter   svf;
    ADSRFade   fEnv, aEnv;
    float currentFreq = 440.0f;
    float targetFreq = 440.0f;
    float velocity = 1.0f;
    bool  alive = false;

    // modulation state
    int   noteNumber = 60;
    float polyAftertouch = 0.0f;
    float lastAmpEnv = 0.0f, lastFiltEnv = 0.0f;
    float noteTime = 0.0f;
    int   ctrlCountdown = 0;
    bool  firstControl = true;
    std::array<double, Mod::NUM_LFOS> polyLfoPhase{};
    uint64_t lfoSeed = 0;
    inline static uint64_t noteCounter = 0;
    uint32_t noiseState = 0x12345678u;

    float mainPitchMul = 1.0f;
    float f1Am = 1.0f, f1Bm = 1.0f, f2Am = 1.0f, f2Bm = 1.0f;
    float sym1 = 0.5f, sym2 = 0.5f;
    float resNow = 0.3f, ffmNow = 0.0f;
    Ramp rOsc1Amp, rOsc2Amp, rMainAmp, rNoise, rRing, rCutOct;
};

// ==============================================================
//  Synthesiser that keeps track of controller state for the matrix
// ==============================================================
class AlphaSynth : public juce::Synthesiser {
public:
    Mod::MidiState* midi = nullptr;

    enum Mode { POLY = 0, MONO, LEGATO };

    // Called once per block from the audio thread
    void setMode(int m) {
        if (m == mode) return;
        allNotesOff(0, true);
        mode = m;
    }

    // Mono / Legato: one voice, last-note priority, returns to held notes.
    //   Mono   - glides on every note (if Glide > 0), envelopes restart on
    //            each new key but continue from their current level.
    //   Legato - glides only while keys overlap; overlapping notes don't
    //            restart the envelopes (single trigger).
    void noteOn(int ch, int note, float vel) override {
        if (mode == POLY) { juce::Synthesiser::noteOn(ch, note, vel); return; }
        const juce::ScopedLock sl(lock);
        auto* v = dynamic_cast<AlphaVoice*>(getVoice(0));
        auto* snd = getSound(0).get();
        if (v == nullptr || snd == nullptr) return;

        const bool overlapping = !held.isEmpty() && v->isVoiceActive();
        held.removeFirstMatchingValue(note);
        held.add(note);

        if (mode == LEGATO && overlapping) {
            v->legatoTo(note, true);
        } else {
            v->glideOnStart = (mode == MONO);
            startVoice(v, snd, ch, note, vel);
        }
    }

    void noteOff(int ch, int note, float vel, bool allowTailOff) override {
        if (mode == POLY) { juce::Synthesiser::noteOff(ch, note, vel, allowTailOff); return; }
        const juce::ScopedLock sl(lock);
        auto* v = dynamic_cast<AlphaVoice*>(getVoice(0));
        const bool wasCurrent = !held.isEmpty() && held.getLast() == note;
        held.removeFirstMatchingValue(note);
        if (v == nullptr) return;
        if (held.isEmpty())
            stopVoice(v, vel, allowTailOff);
        else if (wasCurrent)
            v->legatoTo(held.getLast(), true);   // fall back to the last held key
    }

    void allNotesOff(int ch, bool allowTailOff) override {
        held.clearQuick();
        juce::Synthesiser::allNotesOff(ch, allowTailOff);
    }

    void handleController(int ch, int cc, int value) override {
        if (midi != nullptr && cc >= 0 && cc < 128) midi->cc[(size_t)cc] = (float)value / 127.0f;
        juce::Synthesiser::handleController(ch, cc, value);
    }
    void handlePitchWheel(int ch, int value) override {
        if (midi != nullptr) midi->pitchWheel = juce::jlimit(-1.0f, 1.0f, (float)(value - 8192) / 8191.0f);
        juce::Synthesiser::handlePitchWheel(ch, value);
    }
    void handleChannelPressure(int ch, int value) override {
        if (midi != nullptr) midi->channelPressure = (float)value / 127.0f;
        juce::Synthesiser::handleChannelPressure(ch, value);
    }

private:
    int mode = POLY;
    juce::Array<int> held;   // held keys, oldest first
};

// ==============================================================
//  Audio Processor
// ==============================================================
class AlphaBetaAudioProcessor : public juce::AudioProcessor {
public:
    AlphaBetaAudioProcessor();
    ~AlphaBetaAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
        if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
            return false;
        return true;
    }

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AlphaBeta"; }
    bool  acceptsMidi()  const override { return true; }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int  getNumPrograms()   override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // Patch name shown on the CRT (stored in the state tree)
    juce::String getPatchName() const { return apvts.state.getProperty("patchName", "init").toString(); }
    void setPatchName(const juce::String& n) { apvts.state.setProperty("patchName", n, nullptr); }

    // ---- Wavetables (message thread) ----
    // Slots: 0 = OSC1 wave A, 1 = OSC1 wave B, 2 = OSC2 wave A, 3 = OSC2 wave B.
    // Returns an error message or "". frameSize 0 = auto (Serum header, else 2048 rules).
    static constexpr int NUM_WT_SLOTS = 4;
    juce::String loadWavetable(int slot, const juce::File& file, int frameSize = 0);
    void clearWavetable(int slot);
    juce::String getWavetableName(int slot) const;

    juce::AudioProcessorValueTreeState apvts;

private:
    // Wavetable hand-over: the message thread swaps wtCurrent under the lock,
    // the audio thread picks it up with a try-lock. Every table also lives in
    // wtKeepAlive until nobody else references it, so the audio thread never
    // deletes one.
    juce::CriticalSection wtLock;
    std::array<Wavetable::Ptr, NUM_WT_SLOTS> wtCurrent, wtAudio;
    juce::ReferenceCountedArray<Wavetable> wtKeepAlive;
    void setWavetable(int slot, Wavetable::Ptr wt);
    static int slotOf(const juce::ValueTree& child);   // handles 1.3/1.4 presets
    void rebuildWavetablesFromState();

    AlphaSynth    synth;
    SimpleChorus  chorus;
    Mod::Context  modCtx;
    std::array<double, Mod::NUM_LFOS> monoLfoPhase{};
    // Cached parameter pointers (no string lookups on the audio thread)
    struct SlotPtrs { std::atomic<float>* src; std::atomic<float>* amt; std::atomic<float>* dst; };
    struct LfoPtrs { std::atomic<float>* wave; std::atomic<float>* rate; std::atomic<float>* sync;
                     std::atomic<float>* att; std::atomic<float>* mode; };
    std::array<SlotPtrs, Mod::NUM_SLOTS> slotP{};
    std::array<LfoPtrs, Mod::NUM_LFOS> lfoP{};
    std::atomic<float>* bendP = nullptr;
    std::array<double, 40> syncBeatsTable{};   // Mod::syncNames() has 40 entries
    double        sampleRateHz = 44100.0;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateVoiceParams();
    void updateModContext(int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaBetaAudioProcessor)
};
