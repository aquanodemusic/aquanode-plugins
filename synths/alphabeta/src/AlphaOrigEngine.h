#pragma once
// ============================================================================
//  AlphaOrigEngine - a model of the LinPlug FreeAlpha 3.3 sound engine
//
//  Every constant in here comes from black-box measurements of the original
//  plugin (rendered headless and analysed); see ENGINE_NOTES.md for details.
//  The engine is plain C++ without JUCE so it can be tested on its own.
//
//  Signal flow per voice:
//    2 oscillators (2 table waves each, shared warped phase, 4 mips/octave)
//      -> balance -> x0.35 -> drive -> Chamberlin SVF (LP12/LP24/LP24+/BP/HP)
//      -> amp envelope x velocity x main amp
//  Global: sum -> volume -> 4-tap triangle chorus -> DC blocker -> limiter
// ============================================================================
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include "AlphaOrigData.h"

namespace AlphaOrig {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
//  Original knob laws (normalized 0..1 <-> display values)
// ---------------------------------------------------------------------------
inline double expMap(double v, double mn, double mx, double k) {
    return mn + (mx - mn) * (std::exp(k * v) - 1.0) / (std::exp(k) - 1.0);
}
inline double invExpMap(double x, double mn, double mx, double k) {
    double v = std::log(1.0 + (x - mn) * (std::exp(k) - 1.0) / (mx - mn)) / k;
    return std::min(1.0, std::max(0.0, v));
}
namespace Law {
    inline double cutoffHz(double v)   { return expMap(v, 40.0, 20000.0, 4.0); }     // display Hz
    inline double envTime(double v)    { return expMap(v, 0.001, 10.0, 4.0); }       // seconds
    inline double sustain(double v)    { return (std::exp(2.0 * v) - 1.0) / (std::exp(2.0) - 1.0); }
    inline double fade(double v)       { return 20.0 * v - 10.0; }                   // -10..10
    inline double lfoHz(double v)      { return expMap(v, 0.01, 32.0, 4.0); }
    inline double lfoAttack(double v)  { return expMap(v, 0.0, 10.0, 4.0); }
    inline double glideTime(double v)  { return expMap(v, 0.001, 4.0, 5.0); }
    inline double glideBend(double v)  { return 96.0 * (v - 0.5); }                  // semitones
    inline double spread(double v)     { return expMap(v, 0.0, 50.0, 4.0); }        // display cents
    inline double detune(double v)     { return expMap(v, 0.0, 100.0, 5.0); }       // display cents
    inline double chorusMs(double v)   { return expMap(v, 0.01, 6.0, 4.0); }
    inline double chorusHz(double v)   { return expMap(v, 0.02, 4.0, 4.0); }
    inline double velocity(double v)   { return (1.0 - std::exp(-2.0 * v)) / (1.0 - std::exp(-2.0)); } // 0..1
    inline double volumeDb(double v)   { return 0.2 * (std::floor(181.0 * v + 0.5) - 91.0); }          // 181 steps of 0.2 dB, 0 dB at 91/181; v == 0 -> silent
    // inverses (for preset import)
    inline double invSustain(double s) { return std::log(1.0 + s * (std::exp(2.0) - 1.0)) / 2.0; }
    inline double invVelocity(double p){ return -std::log(1.0 - p * (1.0 - std::exp(-2.0))) / 2.0; }
}

// Discrete value lists, in the order of the original parameters
inline const char* const kRangeNames[5]  = { "-2", "-1", "0", "+1", "+2" };
inline const char* const kFilterNames[5] = { "LP12", "LP24", "LP24+", "BP12", "HP12" };
inline const char* const kLfoWaveNames[6] = { "Sine", "Triangle", "Sawtooth", "Square", "Noise", "Sample&Hold" };
inline const char* const kGlideModeNames[4] = { "Off", "Held", "On", "Bend" };
inline const char* const kVoicesNames[13] = { "mono","2","3","4","5","6","7","8","9","10","11","12","all" };
constexpr int kNumSync = 40;
inline const char* const kSyncNames[kNumSync] = {
    "Off","16/1*","16/1","16/1T","8/1*","8/1","8/1T","4/1*","4/1","4/1T","2/1*","2/1","2/1T","1/1*","1/1","1/1T",
    "1/2*","1/2","1/2T","1/4*","1/4","1/4T","1/8*","1/8","1/8T","1/16*","1/16","1/16T","1/32*","1/32","1/32T","1/64",
    "5/16","7/16","9/16","5/8","11/16","13/16","7/8","15/16" };
inline double syncBeats(int i) {   // length in quarter notes
    static const double t[kNumSync] = { 0, 96, 64, 128.0/3, 48, 32, 64.0/3, 24, 16, 32.0/3, 12, 8, 16.0/3, 6, 4, 8.0/3,
        3, 2, 4.0/3, 1.5, 1, 2.0/3, 0.75, 0.5, 1.0/3, 0.375, 0.25, 1.0/6, 0.1875, 0.125, 1.0/12, 0.0625,
        1.25, 1.75, 2.25, 2.5, 2.75, 3.25, 3.5, 3.75 };
    return t[std::max(0, std::min(kNumSync - 1, i))];
}

// Modulation matrix (FreeAlpha subset)
enum Src { SRC_OFF = 0, SRC_NOTE_LIN, SRC_NOTE_LOG, SRC_VELOCITY, SRC_AFTERTOUCH, SRC_PITCHWHEEL, SRC_MODWHEEL,
           SRC_AMPENV, SRC_FILTENV, SRC_LFO1, SRC_CONSTANT, NUM_SRC };
enum Dst { DST_OFF = 0, DST_OSC1_AMP, DST_OSC1_PITCH, DST_OSC1_SYM, DST_OSC2_AMP, DST_OSC2_PITCH, DST_OSC2_SYM,
           DST_CUTOFF, DST_RESONANCE, DST_MAIN_AMP, DST_MAIN_PITCH, DST_DEPTH1, DST_DEPTH2, DST_DEPTH3, DST_DEPTH4,
           DST_DEPTH5, DST_DEPTH6, DST_DEPTH7, DST_DEPTH8, DST_LFO_SPEED, NUM_DST };
inline const char* const kSrcNames[NUM_SRC] = { "- - -", "Note played(lin)", "Note played(log)", "Velocity", "Aftertouch",
    "Pitch Wheel", "Modulation Wheel", "Amp Envelope", "Filter Envelope", "LFO 1", "Constant" };
inline const char* const kDstNames[NUM_DST] = { "- - -", "Osc 1 Amplitude", "Osc 1 Pitch", "Osc 1 Symmetry",
    "Osc 2 Amplitude", "Osc 2 Pitch", "Osc 2 Symmetry", "Filter Cutoff", "Filter Resonance", "Main Amplitude",
    "Main Pitch", "Matrix Depth 1", "Matrix Depth 2", "Matrix Depth 3", "Matrix Depth 4", "Matrix Depth 5",
    "Matrix Depth 6", "Matrix Depth 7", "Matrix Depth 8", "LFO 1 Speed" };
constexpr int kNumRows = 11;

// ---------------------------------------------------------------------------
//  Patch: everything the engine needs, in the original's units
// ---------------------------------------------------------------------------
struct EnvParams { double attack = 0.001, decay = 0.001, sustain = 1.0, fade = 0.0, release = 0.001; };
struct Row { int src = SRC_OFF; int dst = DST_OFF; double amount = 0.0; };   // amount: -1..1 as displayed

struct Patch {
    int wave[2][2]   = { { 2, 3 }, { 2, 3 } };   // [osc][A/B], 0..29
    int range[2][2]  = { { 0, 0 }, { 0, 0 } };   // -2..+2 octaves
    double mix[2]    = { 0.5, 0.5 };             // "Osc N Mix": 0 = wave A, 1 = wave B
    int filterType   = 0;                        // 0 LP12, 1 LP24, 2 LP24+, 3 BP12, 4 HP12
    double cutoff    = 1.0;                      // knob 0..1
    double resonance = 0.0;                      // 0..1
    double envDepth  = 0.0;                      // -1..1
    EnvParams ampEnv, filEnv;
    int lfoWave      = 1;
    double lfoHz     = 1.0;
    int lfoSync      = 0;                        // index into kSyncNames, 0 = free
    double lfoAttack = 0.0;                      // seconds
    std::array<Row, kNumRows> rows{};
    double volume    = 0.5;                      // knob 0..1 (0.5 = 0 dB)
    double velSens   = 0.73;                     // 0..1
    int glideMode    = 0;                        // 0 off, 1 held, 2 on, 3 bend
    double glideTime = 0.03;                     // seconds
    double glideBend = 0.0;                      // semitones
    double drive     = 0.0;                      // 0..1
    double spread    = 0.0;                      // display cents (0..50)
    double detune    = 0.0;                      // display cents (0..100)
    double balance   = 0.5;                      // 0 = osc 1, 1 = osc 2
    int voices       = 0;                        // 0 = all (32), 1 = mono, 2..12
    double chorusWet = 0.6, chorusMs = 2.0, chorusHz = 0.5;
    double bendRange = 2.0;                      // fixed in FreeAlpha
    double tempo     = 120.0;                    // set by the host each block
};

// ---------------------------------------------------------------------------
//  Wavetables: 1024 samples, linear interpolation, 4 mip zones per octave,
//  every zone table peak-normalised (as in the original)
// ---------------------------------------------------------------------------
class Tables {
public:
    static constexpr int N = 1024;
    // One set per sample rate (measured: zone boundaries are fixed in Hz, while
    // the harmonic limit of each zone scales with the sample rate)
    static const Tables& get(double sr = 44100.0) {
        static std::mutex m;
        static std::map<long, std::unique_ptr<Tables>> cache;
        std::lock_guard<std::mutex> lock(m);
        auto& t = cache[std::lround(sr)];
        if (!t) t.reset(new Tables(sr));
        return *t;
    }
    const float* table(int wave, int zone) const { return data.data() + ((size_t)wave * AlphaOrigData::kNumZones + (size_t)zone) * (N + 1); }

    // zone for an oscillator frequency in Hz (boundaries independent of the sample rate)
    static int zoneFor(double hz) {
        if (hz < 110.0 * (1.0 - 1e-9)) return 0;
        int z = (int)std::floor(4.0 * std::log2(hz / 110.0) + 1e-6) + 1;
        return std::min(z, AlphaOrigData::kNumZones - 1);
    }
    // Harmonics per zone: the measured table at 44.1 kHz; at other rates the
    // rule that reproduces both the 44.1 and 48 kHz measurements:
    // floor(0.845 * (sr/2) / f_low - 1), f_low = lower edge of the zone
    static int harmonicLimit(int zone, double sr) {
        const int k = AlphaOrigData::kZoneHarm[zone];
        if (k <= 0) return 0;
        if (zone == 0 || std::abs(sr - 44100.0) < 1.0) return k;
        const double fLow = 110.0 * std::exp2((zone - 1) / 4.0);
        return std::min(N / 2 - 1, std::max(1, (int)std::floor(0.845 * 0.5 * sr / fLow - 1.0)));
    }
private:
    std::vector<float> data;
    explicit Tables(double sr) {
        using namespace AlphaOrigData;
        data.assign((size_t)kNumWaves * kNumZones * (N + 1), 0.0f);
        std::vector<double> cosT(N), sinT(N);
        for (int n = 0; n < N; ++n) { cosT[n] = std::cos(2 * kPi * n / N); sinT[n] = std::sin(2 * kPi * n / N); }
        std::vector<double> acc(N);
        for (int w = 0; w < kNumWaves; ++w) {
            for (int z = 0; z < kNumZones; ++z) {
                float* t = data.data() + ((size_t)w * kNumZones + z) * (N + 1);
                int K = std::min(harmonicLimit(z, sr), kNumHarm[w]);
                if (K <= 0) continue;                                   // silent zone
                std::fill(acc.begin(), acc.end(), 0.0);
                for (int k = 1; k <= K; ++k) {
                    double a = kAmp[w][k - 1]; if (a == 0.0) continue;
                    double ca = a * std::cos(kPh[w][k - 1]), sa = a * std::sin(kPh[w][k - 1]);
                    for (int n = 0; n < N; ++n) {                       // a*cos(2pi k n/N + ph)
                        int idx = (k * n) & (N - 1);
                        acc[n] += ca * cosT[idx] - sa * sinT[idx];
                    }
                }
                double pk = 0.0;
                for (double v : acc) pk = std::max(pk, std::abs(v));
                double g = pk > 0.0 ? 1.0 / pk : 0.0;
                for (int n = 0; n < N; ++n) t[n] = (float)(acc[n] * g);
                t[N] = t[0];
            }
        }
    }
};

inline float tableRead(const float* t, double ph) {        // ph in [0,1)
    double x = ph * Tables::N;
    int i = (int)x; double f = x - i;
    return (float)(t[i] + (t[i + 1] - t[i]) * f);
}

// Symmetry: piecewise-linear phase warp, midpoint moved to s
inline double symWarp(double p, double s) {
    return p < s ? 0.5 * p / s : 0.5 + 0.5 * (p - s) / (1.0 - s);
}
inline double symFromMod(double m) {                         // includes the built-in offset of the original
    return std::min(0.974, std::max(0.026, 0.48985 + 0.482 * m));
}

// ---------------------------------------------------------------------------
//  Envelope: ADSR + fade. Segments are evaluated on a 576-sample grid
//  (at 44.1 kHz, i.e. 13.06 ms) and interpolated linearly in between.
// ---------------------------------------------------------------------------
class Envelope {
public:
    enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
    void setSampleRate(double sr) { fs = sr; tick = std::max(1, (int)std::lround(576.0 * sr / 44100.0)); }
    bool active() const { return stage != IDLE; }
    Stage getStage() const { return stage; }
    double value() const { return cur; }

    void noteOn(const EnvParams& p, bool hardReset) {
        if (hardReset) cur = 0.0;
        startSegment(ATTACK, p);
    }
    void noteOff(const EnvParams& p) {
        if (stage == IDLE || stage == RELEASE) return;
        relFromSustain = (stage == SUSTAIN);
        startSegment(RELEASE, p);
    }
    void kill() { stage = IDLE; cur = 0.0; }

    double next(const EnvParams& p) {
        if (stage == IDLE) return 0.0;
        if (stage == SUSTAIN && fadeLen == 0) {                   // plain sustain follows the knob
            cur = p.sustain; return cur;
        }
        if (toTick <= 0) planTick(p);
        cur += inc; --toTick; ++pos;
        if (pos >= len) finishSegment(p);
        return cur;
    }
private:
    double fs = 44100.0; int tick = 576;
    Stage stage = IDLE;
    double cur = 0.0, start = 0.0, inc = 0.0;
    int len = 1, pos = 0, toTick = 0;
    int fadeLen = 0; double fadeTarget = 0.0;
    bool relFromSustain = true; double relX0 = 1.0;

    static constexpr double kAtt = 1.41, kDec = 3.03, kRel = 3.03, kFade = 1.1;

    double shape(double u) const {
        u = std::min(1.0, std::max(0.0, u));
        switch (stage) {
        case ATTACK:  return start + (1.0 - start) * (1.0 - std::pow(1.0 - u, kAtt));
        case DECAY:   return target + (1.0 - target) * std::pow(1.0 - u, kDec);
        case SUSTAIN: return fadeTarget > start ? fadeTarget - (fadeTarget - start) * std::pow(1.0 - u, kFade)
                                                : start * std::pow(1.0 - u, kFade);
        case RELEASE: return relFromSustain ? start * std::pow(1.0 - u, kRel)
                                            : std::pow(std::max(0.0, relX0 * (1.0 - u)), kRel);
        default:      return 0.0;
        }
    }
    double target = 0.0;
    void startSegment(Stage s, const EnvParams& p) {
        stage = s; start = cur; pos = 0; toTick = 0; fadeLen = 0;
        switch (s) {
        case ATTACK:  len = std::max(1, (int)std::lround(p.attack * fs)); break;
        case DECAY:   target = p.sustain; len = std::max(1, (int)std::lround(p.decay * fs)); break;
        case SUSTAIN: {
            double f = p.fade;
            if (std::abs(f) < 1e-6) { fadeLen = 0; len = 1; cur = p.sustain; return; }
            fadeTarget = f > 0 ? 1.0 : 0.0;
            fadeLen = std::max(1, (int)std::lround((10.0 - std::abs(f)) * fs));
            len = fadeLen; break;
        }
        case RELEASE:
            if (relFromSustain) len = std::max(1, (int)std::lround(p.release * fs));
            else { relX0 = std::cbrt(std::max(0.0, cur)); len = std::max(1, (int)std::lround(p.release * fs * relX0)); }
            break;
        default: break;
        }
    }
    void planTick(const EnvParams&) {
        int n = std::min(tick, len - pos);
        double endV = shape((double)(pos + n) / (double)len);
        inc = (endV - cur) / (double)n; toTick = n;
    }
    void finishSegment(const EnvParams& p) {
        switch (stage) {
        case ATTACK:  cur = 1.0; startSegment(DECAY, p); break;
        case DECAY:   cur = p.sustain; startSegment(SUSTAIN, p); break;
        case SUSTAIN: cur = fadeTarget; pos = len; toTick = 1 << 30; inc = 0.0; break;   // fade finished: hold
        case RELEASE: kill(); break;
        default: break;
        }
    }
};

// ---------------------------------------------------------------------------
//  LFO (poly: restarts at note-on). Noise = linear interpolation between
//  random values, S&H = random steps; both 8 values per cycle.
// ---------------------------------------------------------------------------
class LFO {
public:
    void reset(uint32_t seed) {
        phase = 0.0; time = 0.0; rng = seed * 2654435761u + 12345u;
        seg = 0; prev = 0.0f; nextV = rnd(); hold = rnd();
    }
    float tick(int wave, double hz, double attack, double dt) {
        double p = phase;
        float v;
        switch (wave) {
        case 0: v = (float)std::sin(2 * kPi * p); break;
        case 1: v = (float)(p < 0.25 ? 4 * p : (p < 0.75 ? 2 - 4 * p : 4 * p - 4)); break;
        case 2: v = (float)(1.0 - 2.0 * p); break;
        case 3: v = p < 0.5 ? 1.0f : -1.0f; break;
        case 4: { double s = p * 8.0 - std::floor(p * 8.0); v = (float)(prev + (nextV - prev) * s); break; }
        default: v = hold; break;
        }
        if (attack > 1e-6) v *= (float)std::min(1.0, time / attack);
        time += dt;
        phase += hz * dt;
        int newSeg = (int)std::floor(phase * 8.0);
        while (seg < newSeg) { ++seg; prev = nextV; nextV = rnd(); hold = rnd(); }
        if (phase >= 1.0) { phase -= std::floor(phase); seg = (int)std::floor(phase * 8.0); }
        return v;
    }
private:
    double phase = 0.0, time = 0.0;
    uint32_t rng = 1;
    int seg = 0; float prev = 0.0f, nextV = 0.0f, hold = 0.0f;
    float rnd() { rng = rng * 1664525u + 1013904223u; return (float)((rng >> 8) * (1.0 / 8388608.0)) - 1.0f; }
};

// ---------------------------------------------------------------------------
//  Filter: Chamberlin state-variable filter with the coefficient laws of the
//  original. The low-pass state is clamped to +-1 (this is what limits the
//  resonance in the original).
// ---------------------------------------------------------------------------
inline double cutoffCoef(double u) {            // u: internal cutoff (0 = 78 Hz display, 1 = 20 kHz display)
    if (u <= 0.0) return 0.00116;
    if (u >= 0.992) return 1.0;
    const double* c = AlphaOrigData::kCutPoly;
    double y = c[0];
    for (int i = 1; i < 7; ++i) y = y * u + c[i];
    return std::min(1.0, std::exp2(y));
}
// At other sample rates the coefficient keeps the same cutoff in Hz (measured
// at 48 kHz); the fully open position stays at the stability limit F = 1.
inline double cutoffCoefAt(double u, double fs) {
    const double F = cutoffCoef(u);
    if (std::abs(fs - 44100.0) < 1.0 || u >= 0.992) return F;
    const double fc = 44100.0 / kPi * std::asin(std::min(1.0, 0.5 * F));
    return std::min(1.0, 2.0 * std::sin(kPi * std::min(fc, 0.24 * fs) / fs));
}
inline double knobToU(double knob) { return std::log2(Law::cutoffHz(knob) / 78.125) / 8.0; }
inline double resoDamp(double r) { r = std::min(1.0, std::max(0.0, r)); double a = 1.0 - 0.9 * r; return a * a; }

struct SVF {
    double lp = 0, bp = 0;
    void reset() { lp = bp = 0; }
    inline double processLP(double x, double F, double q) {
        double hp = x - lp - q * bp; bp += F * hp; lp += F * bp;
        lp = std::min(1.0, std::max(-1.0, lp));
        return lp;
    }
    inline double processLPfree(double x, double F, double q) {
        double hp = x - lp - q * bp; bp += F * hp; lp += F * bp; return lp;
    }
    inline double processBP(double x, double F, double q) {
        double hp = x - lp - q * bp; bp += F * hp; lp += F * bp;
        lp = std::min(1.0, std::max(-1.0, lp));
        return 0.995 * bp;
    }
    inline double processHP(double x, double F, double q) {
        double hp = (1.0 - 0.5 * F) * (x - lp - q * bp); bp += F * hp; lp += F * bp;
        lp = std::min(1.0, std::max(-1.0, lp));
        return hp;
    }
};

// Matrix pitch: the sum of main + oscillator pitch modulation is clamped to
// +-1 and mapped to semitones by a slightly non-linear law (measured with a
// constant source at 25 amounts; key independent). ~48.18 semitones per unit.
inline double pitchModSemis(double m) {
    static const double M[25] = { -1.0, -0.9, -0.85, -0.75, -0.6, -0.5, -0.4, -0.3, -0.25, -0.2, -0.1, 0.0,
                                  0.1, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.7, 0.75, 0.85, 0.9, 0.95, 1.0 };
    static const double D[25] = { 0.073, 0.196, 0.225, 0.240, 0.196, 0.149, 0.101, 0.059, 0.042, 0.028, 0.009, 0.0,
                                  0.000, 0.006, 0.010, 0.013, 0.017, 0.014, -0.003, -0.037, -0.062, -0.130, -0.174, -0.227, -0.287 };
    m = std::min(1.0, std::max(-1.0, m));
    int i = 0; while (i < 23 && m > M[i + 1]) ++i;
    const double t = (m - M[i]) / (M[i + 1] - M[i]);
    return 48.18 * m + D[i] + t * (D[i + 1] - D[i]);
}

// LP24+ ("alternative design"): two Chamberlin SVF low-pass stages in series,
// running 2x oversampled. Fitted to measured transfer functions (0.1-0.7 dB
// rms up to resonance 0.8) and to resonant renders (self-oscillation):
//  * stage 1 carries the resonance and is clamped to +-1 (like the other
//    filters); its damping follows (1-r)^1.9, going slightly negative at the
//    top so the filter self-oscillates at full resonance
//  * stage 2 has fixed damping (q = 1)
//  * the two stages get slightly different frequency coefficients that
//    spread apart as the cutoff rises
struct LP24Plus {
    SVF a, b;
    void reset() { a.reset(); b.reset(); }
    inline double process(double x, double F, double r) {
        r = std::min(1.0, std::max(0.0, r));
        const double Fa = F * (0.3792 + F * (-0.0012 + 0.1064 * F));   // plain stage
        const double Fb = F * (0.3790 + F * (0.4771 + 0.2933 * F));    // resonant stage
        const double r2 = r * r, r8 = r2 * r2 * r2 * r2;
        const double qb = (std::pow(1.0 - r, 1.9) - 0.02 * r8) * (1.0 - 0.2384 * F);
        double y = 0.0;
        for (int i = 0; i < 2; ++i) y = a.processLPfree(b.processLP(x, Fb, qb), Fa, 1.0);
        return y;
    }
};

// ---------------------------------------------------------------------------
//  Drive (pre-filter saturation, "Hard" mode)
// ---------------------------------------------------------------------------
class Drive {
public:
    void set(double d) {
        if (std::abs(d - amount) < 1e-9 && built) return;
        amount = d; built = true;
        if (d <= 0.0) { bypass = true; return; }
        bypass = false;
        // interpolate measured curves (log A, log g, n) between the 0.05 steps
        double x = d / 0.05 - 1.0; int i = (int)std::floor(x); double fr = x - i;
        auto P = [](int j, int c) { j = std::max(0, std::min(19, j)); return (double)AlphaOrigData::kDrive[j][c]; };
        double A, g, n;
        if (x < 0.0) { A = P(0, 0); g = P(0, 1); n = P(0, 2); mixLin = 1.0 + x; }   // below 0.05: crossfade from bypass
        else {
            A = std::exp((1 - fr) * std::log(P(i, 0)) + fr * std::log(P(i + 1, 0)));
            g = std::exp((1 - fr) * std::log(P(i, 1)) + fr * std::log(P(i + 1, 1)));
            n = (1 - fr) * P(i, 2) + fr * P(i + 1, 2); mixLin = 0.0;
        }
        for (int k = 0; k <= kSize; ++k) {
            double xv = (double)k / kSize * kMax;
            double y = A * g * xv / std::pow(1.0 + std::pow(g * xv, n), 1.0 / n);
            table[k] = (float)y;
        }
    }
    inline double process(double x) const {
        if (bypass) return x;
        double ax = std::abs(x), pos = ax / kMax * kSize;
        double y;
        if (pos >= kSize) y = table[kSize];
        else { int i = (int)pos; double f = pos - i; y = table[i] + (table[i + 1] - table[i]) * f; }
        y = x < 0 ? -y : y;
        return mixLin > 0.0 ? mixLin * x + (1.0 - mixLin) * y : y;
    }
private:
    static constexpr int kSize = 2048; static constexpr double kMax = 4.0;
    float table[kSize + 1] = {};
    double amount = -1.0, mixLin = 0.0; bool bypass = true, built = false;
};

// ---------------------------------------------------------------------------
//  Chorus: 2 modulated taps per side on one delay line. Each tap moves along a
//  triangle between its own limits (measured): delay = 8 + D * (lo..hi).
// ---------------------------------------------------------------------------
class Chorus {
public:
    void prepare(double sr) {
        fs = sr;
        size_t need = (size_t)(8 + 2.5 * 6.0 * sr / 1000.0) + 16, sz = 1;
        while (sz < need) sz <<= 1;
        buf.assign(sz, 0.0f); mask = sz - 1; w = 0;
        for (int i = 0; i < 4; ++i) inc[i] = tapHz(0.5, i) / fs;          // the original starts at its default 0.5 Hz
        reset();
    }
    void reset(double hzNow = -1.0) {
        std::fill(buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < 4; ++i) {
            ph[i] = kTap[i].phase0;
            if (hzNow > 0.0) inc[i] = tapHz(hzNow, i) / fs;          // measured: a reset restarts the sweeps at the current speed
        }
    }
    void process(float* L, float* R, int n, double wet, double ms, double hz) {
        if (buf.empty()) return;
        const double dry = 1.0 - 0.62 * wet, wg = wet * (1.0 - 0.62 * wet);
        const double D = ms * fs / 1000.0, dt = 1.0 / fs;
        for (int s = 0; s < n; ++s) {
            float x = L[s];                                   // voices are mono: L == R here
            buf[w] = x;
            double taps[4];
            for (int i = 0; i < 4; ++i) {
                const Tap& t = kTap[i];
                double p = ph[i], tri = p < 0.5 ? 2 * p : 2 - 2 * p;
                double d = 8.0 + D * (t.lo + (t.hi - t.lo) * tri);
                taps[i] = read(d);
                // measured: a new rate only takes effect when a sweep turns around.
                // The original keeps each sweep's rate at 0.01 Hz resolution (its
                // presets store them): 1.0 / 0.7 / 1.3 / 1.0 x the chorus rate.
                const double before = ph[i];
                ph[i] += inc[i];
                if (ph[i] >= 1.0) { ph[i] -= 1.0; inc[i] = tapHz(hz, i) * dt; }
                else if (before < 0.5 && ph[i] >= 0.5) inc[i] = tapHz(hz, i) * dt;
            }
            L[s] = (float)(dry * x + wg * (taps[0] + taps[1]));
            R[s] = (float)(dry * x + wg * (taps[2] + taps[3]));
            w = (w + 1) & mask;
        }
    }
private:
    static double tapHz(double hz, int i) {
        static const float mult[4] = { 1.0f, 0.7f, 1.3f, 1.0f };      // float maths, truncated, as in the original
        const float v = (float)hz * mult[i] * 100.0f;
        return std::max(0.01, (double)(int)v / 100.0);
    }
    struct Tap { double lo, hi, rate, phase0; };
    static constexpr Tap kTap[4] = {
        { 0.3335, 1.6670, 1.0012, 0.25 },   // L   (rates refined from 4 Hz click tracking:
        { 0.3424, 2.3962, 0.6984, 0.75 },   // L    ~1.0, 0.7, 1.3, 1.0 times the rate knob)
        { 0.1420, 0.9942, 1.2991, 0.25 },   // R
        { 0.3336, 1.6669, 1.0014, 0.75 } }; // R
    std::vector<float> buf; size_t mask = 0, w = 0; double fs = 44100.0; double ph[4] = {}, inc[4] = {};
    inline double read(double d) const {
        double rp = (double)w - d; while (rp < 0) rp += (double)buf.size();
        size_t i = (size_t)rp; double f = rp - (double)i;
        float a = buf[i & mask], b = buf[(i + 1) & mask];
        return a + (b - a) * f;
    }
};

// ---------------------------------------------------------------------------
//  Output limiter (approximation of the original's "Limiter" stage)
// ---------------------------------------------------------------------------
class Limiter {
public:
    void prepare(double sr) { att = 1.0 - std::exp(-1.0 / (0.0007 * sr)); rel = 1.0 - std::exp(-1.0 / (0.08 * sr)); env = 0; }
    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) {
            double pk = std::max(std::abs((double)L[i]), std::abs((double)R[i]));
            env += (pk > env ? att : rel) * (pk - env);
            double g = 1.0;
            if (env > kT) g = kT * std::pow(env / kT, 1.0 / kRatio) / env;
            L[i] = (float)(L[i] * g); R[i] = (float)(R[i] * g);
        }
    }
private:
    static constexpr double kT = 1.05, kRatio = 2.0;
    double att = 0.03, rel = 0.0003, env = 0.0;
};

// ---------------------------------------------------------------------------
//  Voice
// ---------------------------------------------------------------------------
struct MidiState { double pitchWheel = 0, modWheel = 0, aftertouch = 0; };

class Voice {
public:
    bool isActive() const { return amp.active(); }
    int key = -1; bool held = false; uint64_t age = 0;

    void prepare(double sr) { fs = sr; tables = &Tables::get(sr); amp.setSampleRate(sr); fil.setSampleRate(sr); }

    void start(int k, int vel, const Patch& p, double glideFromSemis, bool glide, bool legato, uint32_t seed) {
        const bool wasActive = amp.active();
        key = k; velocity = vel; held = true;
        targetSemis = (double)k;
        if (p.glideMode == 3) { curSemis = targetSemis + p.glideBend; glideRate = std::abs(p.glideBend) / std::max(1e-4, p.glideTime); }
        else if (glide) { curSemis = glideFromSemis; glideRate = std::abs(targetSemis - curSemis) / std::max(1e-4, p.glideTime); }
        else { curSemis = targetSemis; glideRate = 0.0; }
        if (!legato) {
            phase[0] = phase[1] = 0.0;
            for (auto& sp : spreadPh) sp = 0.0;
            for (int i = 0; i < 8; ++i) spreadPh[i] = 0.0;
            if (!wasActive) { f1.reset(); f2.reset(); lad.reset(); }
            // same voice re-used while sounding: attack continues from the current level
            amp.noteOn(p.ampEnv, !wasActive); fil.noteOn(p.filEnv, !wasActive);
            lfo.reset(seed); noteTime = 0.0;
        }
        updateZones(p);
        firstBlock = true;
    }
    void legatoTo(int k, const Patch& p, bool glide) {
        key = k; targetSemis = (double)k;
        if (glide) glideRate = std::abs(targetSemis - curSemis) / std::max(1e-4, p.glideTime);
        else { curSemis = targetSemis; glideRate = 0; }
        updateZones(p);
    }
    void release(const Patch& p) { held = false; amp.noteOff(p.ampEnv); fil.noteOff(p.filEnv); }
    void kill() { amp.kill(); fil.kill(); key = -1; }
    double currentSemis() const { return curSemis; }

    void updateZones(const Patch& p) { updateZonesAt(p, (double)key); }
    void updateZonesAt(const Patch& p, double semis) {
        for (int o = 0; o < 2; ++o) {
            int r0 = p.range[o][0], r1 = p.range[o][1];
            int rmin = std::min(r0, r1);
            masterRange[o] = rmin;
            mult[o][0] = 1 << (r0 - rmin); mult[o][1] = 1 << (r1 - rmin);
            for (int w = 0; w < 2; ++w) {
                double hz = 440.0 * std::exp2((semis - 69.0) / 12.0 + p.range[o][w]);
                zone[o][w] = Tables::zoneFor(hz);
                tab[o][w] = tables->table(p.wave[o][w], zone[o][w]);
            }
        }
    }

    // Render n samples (n <= kBlock), adding into out
    void render(float* out, int n, const Patch& p, const MidiState& midi, const Drive& drive) {
        if (!amp.active()) return;
        const double dt = 1.0 / fs;
        // ---- glide (linear in semitones, constant time) ----
        double g0 = curSemis;
        if (glideRate > 0.0) {
            double step = glideRate * dt * n;
            if (std::abs(targetSemis - curSemis) <= step) { curSemis = targetSemis; glideRate = 0.0; }
            else curSemis += (targetSemis > curSemis ? step : -step);
        }
        // ---- LFO (per sample would be overkill: evaluate per block, ramp) ----
        double lfoHz = p.lfoSync > 0 ? (p.tempo / 60.0) / syncBeats(p.lfoSync) : p.lfoHz;
        // ---- matrix (evaluated once per block with the envelope values at block start) ----
        double env0 = amp.value(), fenv0 = fil.value();
        double src[NUM_SRC] = {};
        src[SRC_NOTE_LIN] = src[SRC_NOTE_LOG] = (key - 63) / 48.0;
        src[SRC_VELOCITY] = 2.0 * velocity / 127.0 - 1.0;
        src[SRC_AFTERTOUCH] = midi.aftertouch; src[SRC_PITCHWHEEL] = midi.pitchWheel; src[SRC_MODWHEEL] = midi.modWheel;
        src[SRC_AMPENV] = env0; src[SRC_FILTENV] = fenv0; src[SRC_CONSTANT] = 1.0;
        src[SRC_LFO1] = lastLfo;
        double depth[8] = {};
        for (const Row& r : p.rows)
            if (r.dst >= DST_DEPTH1 && r.dst <= DST_DEPTH8 && r.src != SRC_OFF) depth[r.dst - DST_DEPTH1] += src[r.src] * r.amount;
        double pitch1 = 0, pitch2 = 0, pitchM = 0, sym1 = 0, sym2 = 0, cut = 0, res = 0, speed = 0;
        double amp1 = 1, amp2 = 1, ampM = 1;
        for (int i = 0; i < kNumRows; ++i) {
            const Row& r = p.rows[(size_t)i];
            if (r.src == SRC_OFF || r.dst == DST_OFF) continue;
            if (r.dst >= DST_DEPTH1 && r.dst <= DST_DEPTH8) continue;
            double a = r.amount; if (i < 8) a += depth[i];          // measured: no clamp after the depth rows
            double m = a * src[r.src];
            switch (r.dst) {
            case DST_OSC1_AMP:   amp1 *= 1.0 + m; break;            // may go negative (phase flip)
            case DST_OSC2_AMP:   amp2 *= 1.0 + m; break;
            case DST_MAIN_AMP:   ampM *= 1.0 + m; break;
            case DST_OSC1_PITCH: pitch1 += m; break;
            case DST_OSC2_PITCH: pitch2 += m; break;
            case DST_MAIN_PITCH: pitchM += m; break;
            case DST_OSC1_SYM:   sym1 += m; break;
            case DST_OSC2_SYM:   sym2 += m; break;
            case DST_CUTOFF:     cut += m; break;
            case DST_RESONANCE:  res += m; break;
            case DST_LFO_SPEED:  speed += m; break;
            default: break;
            }
        }
        lfoHz *= std::exp2(4.0 * speed);
        // ---- oscillator frequencies ----
        double det = 4.015 * std::floor(p.detune * 10.0 + 1e-6) / 10.0 / 100.0;   // semitones each way (display resolution 0.1 ct)
        double base = curSemis + p.bendRange * midi.pitchWheel; (void)g0;
        for (int o = 0; o < 2; ++o) {                            // ranges may change while a note sounds
            const int r0 = p.range[o][0], r1 = p.range[o][1], rmin = std::min(r0, r1);
            masterRange[o] = rmin; mult[o][0] = 1 << (r0 - rmin); mult[o][1] = 1 << (r1 - rmin);
        }
        double hz1 = 440.0 * std::exp2((base + pitchModSemis(pitchM + pitch1) + det - 69.0) / 12.0 + masterRange[0]);
        double hz2 = 440.0 * std::exp2((base + pitchModSemis(pitchM + pitch2) - det - 69.0) / 12.0 + masterRange[1]);
        // mip zone from the actual (modulated) frequency of each wave - measured: the
        // original follows matrix pitch (constant offsets, LFO, glide) block by block
        {
            const double hzO[2] = { hz1, hz2 };
            for (int o = 0; o < 2; ++o)
                for (int w = 0; w < 2; ++w) {
                    zone[o][w] = Tables::zoneFor(hzO[o] * mult[o][w]);
                    tab[o][w] = tables->table(p.wave[o][w], zone[o][w]);
                }
        }
        double inc1T = hz1 / fs, inc2T = hz2 / fs;
        double s1T = symFromMod(sym1), s2T = symFromMod(sym2);
        // ---- filter coefficient targets ----
        double u0 = knobToU(p.cutoff) + cut;
        double fe = fenv0;                                        // env moves the cutoff toward the ends
        double uT = p.envDepth >= 0 ? u0 + p.envDepth * fe * (1.0 - u0) : u0 * (1.0 + p.envDepth * fe);
        double FT = cutoffCoefAt(uT, fs);
        double qT = resoDamp(p.resonance * (1.0 + res));   // measured: resonance modulation scales the knob value
        const double rT = std::min(1.0, std::max(0.0, p.resonance * (1.0 + res)));
        if (firstBlock) { Fcur = FT; qcur = qT; rcur = rT; a1c = amp1; a2c = amp2; amc = ampM; inc1 = inc1T; inc2 = inc2T; s1 = s1T; s2 = s2T; firstBlock = false; }
        const double di1 = (inc1T - inc1) / n, di2 = (inc2T - inc2) / n, ds1 = (s1T - s1) / n, ds2 = (s2T - s2) / n;
        double dF = (FT - Fcur) / n, dq = (qT - qcur) / n, dr = (rT - rcur) / n;
        double da1 = (amp1 - a1c) / n, da2 = (amp2 - a2c) / n, dam = (ampM - amc) / n;
        const double bal = p.balance, velGain = 1.0 - p.velSens * (1.0 - velocity / 127.0);
        const double mixA = p.mix[0], mixB = p.mix[1];
        const bool spreadOn = p.spread > 1e-6;
        const int ftype = p.filterType;
        for (int i = 0; i < n; ++i) {
            // oscillators
            double o1, o2;
            if (!spreadOn) {
                o1 = oscSample(0, phase[0], inc1, s1, mixA);
                o2 = oscSample(1, phase[1], inc2, s2, mixB);
            } else {
                o1 = o2 = 0.0;
                // measured: 3 copies per oscillator, all starting in phase, each at half level;
                // the outer copies are offset linearly in frequency (per display cent of spread)
                static const double d1[3] = { 0.0, -0.001008, 0.000948 }, d2[3] = { 0.0, -0.0011704, 0.0010982 };
                for (int c = 0; c < 3; ++c) o1 += oscSample(0, c == 0 ? phase[0] : spreadPh[c], inc1 * (1.0 + d1[c] * p.spread), s1, mixA);
                for (int c = 0; c < 3; ++c) o2 += oscSample(1, c == 0 ? phase[1] : spreadPh[3 + c], inc2 * (1.0 + d2[c] * p.spread), s2, mixB);
                o1 *= 0.5; o2 *= 0.5;
            }
            a1c += da1; a2c += da2; amc += dam; Fcur += dF; qcur += dq; rcur += dr;
            inc1 += di1; inc2 += di2; s1 += ds1; s2 += ds2;
            double x = 0.35 * (o1 * (1.0 - bal) * a1c + o2 * bal * a2c);
            x = drive.process(x);
            double y;
            switch (ftype) {
            case 1:  y = f2.processLPfree(f1.processLP(x, Fcur, qcur), Fcur, 1.0); break;
            case 2:  y = lad.process(x, Fcur, rcur); break;
            case 3:  y = f1.processBP(x, Fcur, qcur); break;
            case 4:  y = f1.processHP(x, Fcur, qcur); break;
            default: y = f1.processLP(x, Fcur, qcur); break;
            }
            double e = amp.next(p.ampEnv);
            fil.next(p.filEnv);
            lastLfo = lfo.tick(p.lfoWave, lfoHz, p.lfoAttack, dt);
            out[i] += (float)(y * e * velGain * amc);
            if (!amp.active()) { key = -1; break; }
        }
        noteTime += n * dt;
    }
private:
    double fs = 44100.0;
    int velocity = 100;
    double curSemis = 60, targetSemis = 60, glideRate = 0;
    double phase[2] = {}, spreadPh[8] = {};
    int masterRange[2] = {}, mult[2][2] = { { 1, 1 }, { 1, 1 } }, zone[2][2] = {};
    const float* tab[2][2] = {};
    const Tables* tables = &Tables::get();
    Envelope amp, fil;
    LFO lfo; double lastLfo = 0.0, noteTime = 0.0;
    SVF f1, f2; LP24Plus lad;
    double Fcur = 1.0, qcur = 1.0, rcur = 0.0, a1c = 1, a2c = 1, amc = 1, inc1 = 0, inc2 = 0, s1 = 0.49, s2 = 0.49; bool firstBlock = true;

    inline double oscSample(int o, double& ph, double inc, double s, double mix) {
        double wph = symWarp(ph, s);
        double a = 0.0, b = 0.0;
        if (mix < 1.0) { double q = wph * mult[o][0]; q -= std::floor(q); a = tableRead(tab[o][0], q); }
        if (mix > 0.0) { double q = wph * mult[o][1]; q -= std::floor(q); b = tableRead(tab[o][1], q); }
        ph += inc; if (ph >= 1.0) ph -= std::floor(ph);
        return a + (b - a) * mix;
    }
};

// ---------------------------------------------------------------------------
//  Engine
// ---------------------------------------------------------------------------
class Engine {
public:
    static constexpr int kMaxVoices = 32;
    static constexpr int kBlock = 16;

    Patch patch;

    void prepare(double sampleRate, int maxBlock) {
        fs = sampleRate; (void)Tables::get(fs);
        for (auto& v : voices) v.prepare(fs);
        chorus.prepare(fs); limiter.prepare(fs);
        mono.assign((size_t)std::max(maxBlock, kBlock), 0.0f);
        hpR = 1.0 - 2.0 * kPi * 9.57 / fs; hpX[0] = hpX[1] = hpY[0] = hpY[1] = 0.0;
        reset();
    }
    void reset() {
        for (auto& v : voices) v.kill();
        chorus.reset(patch.chorusHz); heldKeys.clear();
    }

    // ---- MIDI ----
    void noteOn(int key, int vel) {
        if (vel <= 0) { noteOff(key); return; }
        const int maxV = patch.voices == 0 ? kMaxVoices : patch.voices;
        const bool glideAllowed = patch.glideMode == 2 || (patch.glideMode == 1 && anyHeld());
        if (maxV == 1) {                                       // mono, last-note priority
            Voice& v = voices[0];
            bool legato = v.isActive() && anyHeld();
            heldKeys.erase(std::remove(heldKeys.begin(), heldKeys.end(), key), heldKeys.end());
            heldKeys.push_back(key);
            if (legato) { v.legatoTo(key, patch, glideAllowed); v.held = true; }
            else v.start(key, vel, patch, lastSemis, glideAllowed && lastSemis > 0, false, ++seed);
            if (!legato) { v.kill(); v.start(key, vel, patch, lastSemis, glideAllowed && lastSemis > 0, false, seed); }
            lastSemis = key; return;
        }
        heldKeys.push_back(key);
        Voice* v = nullptr;
        for (auto& c : voices) if (c.isActive() && c.key == key) { v = &c; break; }    // same key: re-use the voice
        if (v == nullptr) {
            int active = 0; for (auto& c : voices) if (c.isActive()) ++active;
            if (active < maxV) for (auto& c : voices) if (!c.isActive()) { v = &c; break; }
            if (v == nullptr) {                                  // steal: oldest released, else oldest
                Voice* best = nullptr;
                for (auto& c : voices) if (c.isActive() && !c.held && (!best || c.age < best->age)) best = &c;
                if (!best) for (auto& c : voices) if (c.isActive() && (!best || c.age < best->age)) best = &c;
                v = best; if (v) v->kill();
            }
        }
        if (!v) return;
        v->age = ++ageCounter;
        v->start(key, vel, patch, lastSemis, glideAllowed && lastSemis > 0, false, ++seed);
        lastSemis = key;
    }
    void noteOff(int key) {
        heldKeys.erase(std::remove(heldKeys.begin(), heldKeys.end(), key), heldKeys.end());
        const int maxV = patch.voices == 0 ? kMaxVoices : patch.voices;
        if (maxV == 1) {
            Voice& v = voices[0];
            if (v.key != key) return;
            if (!heldKeys.empty()) { v.legatoTo(heldKeys.back(), patch, patch.glideMode == 2 || patch.glideMode == 1); lastSemis = heldKeys.back(); }
            else v.release(patch);
            return;
        }
        for (auto& v : voices) if (v.isActive() && v.key == key && v.held) v.release(patch);
    }
    void allNotesOff() { heldKeys.clear(); for (auto& v : voices) if (v.isActive()) v.release(patch); }
    void pitchWheel(int value14) { midi.pitchWheel = std::max(-1.0, std::min(1.0, (value14 - 8192) / 8192.0)); }
    void controller(int cc, int value) { if (cc == 1) midi.modWheel = value / 127.0; if (cc == 123 || cc == 120) allNotesOff(); }
    void channelPressure(int value) { midi.aftertouch = value / 127.0; }

    // ---- audio ----
    void process(float* L, float* R, int n) {
        drive.set(patch.drive);
        std::fill(L, L + n, 0.0f);
        for (int off = 0; off < n; off += kBlock) {
            int m = std::min(kBlock, n - off);
            for (auto& v : voices) if (v.isActive()) v.render(L + off, m, patch, midi, drive);
        }
        // volume
        const double vg = patch.volume <= 0.0 ? 0.0 : std::pow(10.0, Law::volumeDb(patch.volume) / 20.0);
        for (int i = 0; i < n; ++i) L[i] = (float)(L[i] * vg);
        std::copy(L, L + n, R);
        if (patch.chorusWet > 1e-6) chorus.process(L, R, n, patch.chorusWet, patch.chorusMs, patch.chorusHz);
        else chorus.process(L, R, n, 0.0, patch.chorusMs, patch.chorusHz);   // keep the line running
        // DC blocker (measured: 1st-order high-pass at 9.6 Hz)
        for (int i = 0; i < n; ++i) {
            double l = L[i], r = R[i];
            double yl = l - hpX[0] + hpR * hpY[0]; hpX[0] = l; hpY[0] = yl; L[i] = (float)yl;
            double yr = r - hpX[1] + hpR * hpY[1]; hpX[1] = r; hpY[1] = yr; R[i] = (float)yr;
        }
        limiter.process(L, R, n);
    }
    int activeVoices() const { int a = 0; for (auto& v : voices) if (v.isActive()) ++a; return a; }
private:
    double fs = 44100.0;
    std::array<Voice, kMaxVoices> voices;
    MidiState midi;
    Drive drive; Chorus chorus; Limiter limiter;
    std::vector<float> mono;
    std::vector<int> heldKeys;
    double lastSemis = -1.0;
    uint64_t ageCounter = 0; uint32_t seed = 1;
    double hpR = 0.9986, hpX[2] = {}, hpY[2] = {};
    bool anyHeld() const { return !heldKeys.empty(); }
};

} // namespace AlphaOrig
