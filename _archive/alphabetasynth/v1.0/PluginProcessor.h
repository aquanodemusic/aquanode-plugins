#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <cmath>

// ==============================================================
//  Utility
// ==============================================================

inline float tanhA(float x) {
    // Pad� approximant � fast, accurate for |x| < 4
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float polyBlep(float phase, float dt) {
    if (phase < dt) { float t = phase / dt;        return  t + t - t * t - 1.0f; }
    if (phase > 1.0f - dt) { float t = (phase - 1.0f) / dt; return  t * t + t + t + 1.0f; }
    return 0.0f;
}

// ==============================================================
//  Anti-aliased single oscillator (PolyBLEP)
// ==============================================================
class MonoOsc {
public:
    enum Wave { SINE = 0, TRI, SAW, SQUARE, NOISE };
    Wave wave = SINE;

    void reset() { phase = 0.0f; triAcc = 0.0f; }

    // phaseOffset = FM modulation index * modulator output (in cycles).
    // Adds to the output phase so OSC2 can phase-modulate OSC1 (true 2-op FM).
    float tick(float freq, float sr, float phaseOffset = 0.0f) {
        float dt = freq / sr;
        if (dt <= 0.0f || dt >= 0.5f) return 0.0f;
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;

        // ph is the phase-modulated output phase, wrapped to [0, 1)
        float ph = phase + phaseOffset;
        ph -= std::floor(ph);

        switch (wave) {
        case SINE:
            return std::sin(juce::MathConstants<float>::twoPi * ph);
        case TRI: {
            // Triangle is integrated; apply FM offset to the square driver instead
            float sq = (ph < 0.5f ? 1.0f : -1.0f);
            sq += polyBlep(ph, dt);
            sq -= polyBlep(std::fmod(ph + 0.5f, 1.0f), dt);
            triAcc = triAcc * (1.0f - 2e-4f) + 4.0f * dt * sq;
            return juce::jlimit(-1.0f, 1.0f, triAcc);
        }
        case SAW: {
            // ph carries the FM offset; polyBLEP correction at the ph discontinuity
            float s = 2.0f * ph - 1.0f;
            s -= polyBlep(ph, dt);
            return s;
        }
        case SQUARE: {
            float s = (ph < 0.5f ? 1.0f : -1.0f);
            s += polyBlep(ph, dt);
            s -= polyBlep(std::fmod(ph + 0.5f, 1.0f), dt);
            return s;
        }
        case NOISE:
            return rng.nextFloat() * 2.0f - 1.0f;
        }
        return 0.0f;
    }

private:
    float phase = 0.0f;
    float triAcc = 0.0f;
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
//  State Variable Filter  (Simper/Cytomic � highly stable)
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
}

// ==============================================================
//  Synth Sound
// ==============================================================
struct AlphaSound : public juce::SynthesiserSound {
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// ==============================================================
//  Synth Voice
// ==============================================================
class AlphaVoice : public juce::SynthesiserVoice {
public:
    // ---- Parameters set by the processor each block ----
    MonoOsc::Wave o1wA = MonoOsc::SAW, o1wB = MonoOsc::SINE;
    MonoOsc::Wave o2wA = MonoOsc::SAW, o2wB = MonoOsc::SINE;
    int o1octA = 0, o1octB = 0, o2octA = 0, o2octB = 0;
    float o1morph = 0.0f, o2morph = 0.0f;
    float o1detune = 0.0f, o2detune = 0.0f;   // cents
    float oscMix = 0.5f, drive = 0.0f, fm = 0.0f;
    float glideTime = 0.0f;
    int   filterType = 1;                    // 0=LP12,1=LP24,2=LP24+,3=BP,4=HP
    int   resCurve = 1;                    // 0=quadratic, 1=cubic
    float filterCutoff = 2000.0f, filterRes = 0.3f;
    float filterDepth = 1.0f;
    ADSRFade::Params fEnvP, aEnvP;
    float ampVol = 0.8f, ampVelSens = 0.5f;
    int   voiceIdx = 0;                      // for panning in spread
    float stereoSpread = 0.0f;

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
        float target = (float)juce::MidiMessage::getMidiNoteInHertz(midiNote);

        if (glideTime > 0.001f && currentFreq > 10.0f)
            targetFreq = target;
        else { currentFreq = target; targetFreq = target; }

        o1a.reset(); o1b.reset(); o2a.reset(); o2b.reset();
        ladder.reset(); svf.reset();
        // Negative depth: start at envelope peak so the filter begins fully
        // closed and slowly opens during decay ("bass slowly incoming").
        // Positive depth: normal attack-up behaviour (filter sweeps high→low).
        if (filterDepth < -0.001f)
            fEnv.noteOnFromPeak();
        else
            fEnv.noteOn();
        aEnv.noteOn();
        alive = true;
    }

    void stopNote(float, bool allowTail) override {
        if (allowTail) { fEnv.noteOff(); aEnv.noteOff(); }
        else { clearCurrentNote(); alive = false; }
    }

    void renderNextBlock(juce::AudioBuffer<float>& buf, int start, int num) override {
        if (!alive && !aEnv.isActive()) { clearCurrentNote(); return; }

        float sr = (float)getSampleRate();
        float sri = 1.0f / sr;

        // Pre-compute per-block constants
        o1a.wave = o1wA; o1b.wave = o1wB;
        o2a.wave = o2wA; o2b.wave = o2wB;

        float f1Am = std::pow(2.0f, (float)o1octA);
        float f1Bm = std::pow(2.0f, (float)o1octB + o1detune / 1200.0f);
        float f2Am = std::pow(2.0f, (float)o2octA);
        float f2Bm = std::pow(2.0f, (float)o2octB + o2detune / 1200.0f);

        float glCoeff = (glideTime > 0.001f)
            ? (1.0f - std::exp(-sri / glideTime)) : 1.0f;

        // Stereo panning from spread + voice index
        float panL = 1.0f, panR = 1.0f;
        if (stereoSpread > 0.001f) {
            float panPos = ((voiceIdx % 2 == 0) ? 1.0f : -1.0f) * stereoSpread * 0.4f;
            panL = std::sqrt(0.5f * (1.0f - panPos));
            panR = std::sqrt(0.5f * (1.0f + panPos));
        }

        bool hasStereo = (buf.getNumChannels() >= 2);

        for (int i = 0; i < num; ++i) {
            // Glide
            currentFreq += (targetFreq - currentFreq) * glCoeff;

            // OSC2 – FM modulator. Runs at its own pitch (set by oct/detune controls).
            // In classic 2-op FM, the modulator:carrier frequency ratio determines
            // the harmonic spectrum; OSC2's oct/detune knobs set that ratio.
            float f2a = currentFreq * f2Am;
            float f2b = currentFreq * f2Bm;
            float osc2 = o2a.tick(f2a, sr) * (1.0f - o2morph) + o2b.tick(f2b, sr) * o2morph;

            // True 2-op FM: OSC2 phase-modulates OSC1.
            // fm is the modulation index β (0..10). Multiplied by osc2 [-1..1],
            // this gives a phase offset in cycles fed directly into tick().
            // At β=1 the first sideband is loudest; β=5–10 gives very rich FM tones.
            float fmPhase = osc2 * fm;
            float f1a = currentFreq * f1Am;
            float f1b = currentFreq * f1Bm;
            float osc1 = o1a.tick(f1a, sr, fmPhase) * (1.0f - o1morph)
                + o1b.tick(f1b, sr, fmPhase) * o1morph;

            // Mix: OSC2 fades out of audio as FM rises (it becomes a pure modulator).
            // Clamp fade to [0,1] so it works for fm > 1.
            float fmFade = juce::jlimit(0.0f, 1.0f, fm);
            float osc2Gain = oscMix * (1.0f - fmFade);
            float osc1Gain = 1.0f - osc2Gain;
            float sig = osc1 * osc1Gain + osc2 * osc2Gain;

            // Pre-filter drive (soft saturation)
            float driveG = 1.0f + drive * 6.0f;
            sig = tanhA(sig * driveG) / (0.4f + drive * 0.6f + 0.6f);

            // Filter envelope
            float envV = fEnv.tick(fEnvP);

            // Env modulates cutoff in octaves (filterDepth: -1 to +1, �4 oct range)
            float cutMod = filterCutoff * std::pow(2.0f, envV * filterDepth * 4.0f);
            cutMod = juce::jlimit(20.0f, 20000.0f, cutMod);

            float filtered;
            ladder.resCurve = resCurve;
            svf.resCurve = resCurve;
            switch (filterType) {
            case 0:  ladder.setParams(cutMod, filterRes); filtered = ladder.process12(sig);     break;
            case 2:  ladder.setParams(cutMod, filterRes); filtered = ladder.process24plus(sig); break;
            case 3:  svf.setParams(cutMod, filterRes);    filtered = svf.process(sig).bp;       break;
            case 4:  svf.setParams(cutMod, filterRes);    filtered = svf.process(sig).hp;       break;
            default: ladder.setParams(cutMod, filterRes); filtered = ladder.process24(sig);     break;
            }

            // Amp envelope
            float amp = aEnv.tick(aEnvP);
            float velF = 1.0f - ampVelSens + ampVelSens * velocity;
            float out = filtered * amp * velF * ampVol * 0.5f;

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
    MonoOsc o1a, o1b, o2a, o2b;
    MoogLadder ladder;
    SVFilter   svf;
    ADSRFade   fEnv, aEnv;
    float currentFreq = 440.0f;
    float targetFreq = 440.0f;
    float velocity = 1.0f;
    bool  alive = false;
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

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::Synthesiser synth;
    SimpleChorus      chorus;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateVoiceParams();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaBetaAudioProcessor)
};