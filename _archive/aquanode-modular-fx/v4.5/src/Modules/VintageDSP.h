#pragma once

// VintageDSP - the DSP behind the "Vintage" module family (Dual VCO, Dual Filter,
// Skew LFO, Analog EG, PWM Osc, Warm Ladder, Ensemble, Stereo Phaser, Cross
// Delay, Smooth Hall).
//
// Together they make classic two-layer analog pads: a dual-oscillator layer
// through a resonant high-pass and low-pass (the high-pass can be kicked
// around by pink noise -> sample & hold clocked by an LFO) over a PWM pad with
// an ensemble and a stereo phaser, sent to a stereo delay and a smooth hall.
//
// No JUCE in here on purpose: the classes are plain C++, so they can also be
// tested and rendered outside the plugin.
//
// Every class is a plain value type processing one sample at a time. Nothing
// allocates on the audio thread except the delay-based effects, which size
// their buffers in prepare().

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vintagedsp
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

//==============================================================================
// small helpers
//==============================================================================
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Pade-style tanh, accurate to ~1e-3 in [-3,3] and saturating cleanly outside
inline float fastTanh (float x)
{
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline double noteToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

// "knob" helpers: presets describe many controls as 0..1. These
// are the curves used to turn those numbers into physical values.
inline double expMap (double k, double lo, double hi) { return lo * std::pow (hi / lo, k); }

// Filter-cutoff knob: ~10 octaves, 25 Hz .. 19 kHz
inline double knobToCutoffHz (double k) { return 25.0 * std::pow (2.0, 9.6 * k); }

// Envelope-time knob: 1 ms .. 10 s, exponential (audio-taper pot)
inline double knobToEnvMs (double k) { return expMap (k, 1.0, 10000.0); }

// tiny xorshift so voices can draw their own random numbers lock-free
struct Rng
{
    uint32_t s { 0x9E3779B9u };
    void seed (uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi()  { return uni() * 2.0f - 1.0f; }                              // [-1,1)
};

//==============================================================================
// PolyBLEP oscillator core
//==============================================================================
inline double polyBlep (double t, double dt)
{
    if (t < dt)       { t /= dt;             return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return t * t + t + t + 1.0; }
    return 0.0;
}

struct BlepOsc
{
    double phase { 0.0 };

    void reset (double p = 0.0) { phase = p; }

    // advance and return the naive phase before advancing (for derived shapes)
    double tick (double inc) { const double p = phase; phase += inc; if (phase >= 1.0) phase -= 1.0; return p; }

    static float saw (double p, double dt) { return (float) (2.0 * p - 1.0 - polyBlep (p, dt)); }

    // width = fraction of the cycle spent high
    static float pulse (double p, double dt, double width)
    {
        double v = p < width ? 1.0 : -1.0;
        v += polyBlep (p, dt);
        double q = p - width; if (q < 0.0) q += 1.0;
        v -= polyBlep (q, dt);
        return (float) v;
    }

    // band-limited-enough triangle: integrated square would need state; at
    // audio rates a naive triangle's aliasing is ~-40 dB down and harmless here
    static float tri (double p) { return (float) (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); }
};

//==============================================================================
// Slow random drift ("Analog" control): a smoothed random walk in cents
//==============================================================================
struct AnalogDrift
{
    Rng rng; float value { 0.0f }, target { 0.0f }; int countdown { 0 };

    void reset (uint32_t seed) { rng.seed (seed); value = target = rng.bi(); countdown = 0; }

    // returns -1..1, wandering on a ~0.3 s time scale
    float next (double sr)
    {
        if (--countdown <= 0) { target = rng.bi(); countdown = (int) (sr * (0.15 + 0.3 * rng.uni())); }
        value += (target - value) * (float) (3.0 / sr);
        return value;
    }
};

//==============================================================================
// Dual VCO - two VCOs and their mixer
//   VCO1: Triangle / Saw / Pulse (PW knob) / Noise,  feet 32' 16' 8' 4'
//   VCO2: Saw / Pulse 50% / Pulse 10% / Ring Mod,     feet 16' 8' 4' 2'
// 8' plays the key's own pitch. VCO2 Pitch is +-12 semitones.
//==============================================================================
struct DualVco
{
    enum Wave1 { W1Tri = 0, W1Saw, W1Pulse, W1Noise };
    enum Wave2 { W2Saw = 0, W2Pulse50, W2Pulse10, W2Ring };

    BlepOsc o1, o2; AnalogDrift drift1, drift2; Rng noise;

    void reset (uint32_t seed)
    {
        Rng r; r.seed (seed * 7919u + 1u);
        o1.reset (r.uni()); o2.reset (r.uni());
        drift1.reset (seed * 31u + 3u); drift2.reset (seed * 57u + 11u);
        noise.seed (seed * 101u + 5u);
    }

    // note: MIDI note; pitchMod in semitones (MG / EG / bend already summed)
    float process (double sr, double note, double pitchMod,
                   int wave1, float pw1, int scale1,
                   int wave2, float pitch2Semis, int scale2,
                   float fineCents, float level1, float level2, float analog)
    {
        static const double feet1[4] = { -24.0, -12.0, 0.0, 12.0 };
        static const double feet2[4] = { -12.0, 0.0, 12.0, 24.0 };
        const double d1 = drift1.next (sr) * analog * 8.0;     // up to +-8 cents
        const double d2 = drift2.next (sr) * analog * 8.0;

        const double n1 = note + pitchMod + feet1[std::clamp (scale1, 0, 3)] + (fineCents + d1) * 0.01;
        const double n2 = note + pitchMod + feet2[std::clamp (scale2, 0, 3)] + pitch2Semis + (fineCents + d2) * 0.01;
        const double dt1 = std::min (0.45, noteToHz (n1) / sr);
        const double dt2 = std::min (0.45, noteToHz (n2) / sr);

        const double p1 = o1.tick (dt1), p2 = o2.tick (dt2);

        float s1 = 0.0f;
        switch (wave1)
        {
            case W1Tri:   s1 = BlepOsc::tri (p1); break;
            case W1Saw:   s1 = BlepOsc::saw (p1, dt1); break;
            case W1Pulse: s1 = BlepOsc::pulse (p1, dt1, 0.5 - 0.45 * clampf (pw1, 0.0f, 1.0f)); break;
            default:      s1 = noise.bi(); break;
        }

        float s2 = 0.0f;
        switch (wave2)
        {
            case W2Saw:     s2 = BlepOsc::saw (p2, dt2); break;
            case W2Pulse50: s2 = BlepOsc::pulse (p2, dt2, 0.5); break;
            case W2Pulse10: s2 = BlepOsc::pulse (p2, dt2, 0.1); break;
            default:        // ring modulator: VCO1 square times VCO2 square
                s2 = BlepOsc::pulse (p1, dt1, 0.5) * BlepOsc::pulse (p2, dt2, 0.5); break;
        }
        return level1 * s1 + level2 * s2;
    }
};

//==============================================================================
// Resonant 12 dB state-variable core with a vintage misbehaviour: the
// resonance path goes through a soft clipper, so high Peak settings scream and
// compress instead of blowing up (as diode-limited analog filters do).
// Zero-delay-feedback (Simper/TPT) structure: stable under fast modulation,
// which matters because the S&H throws the cutoff around in single steps.
//==============================================================================
struct ResonantSvf
{
    float ic1 { 0.0f }, ic2 { 0.0f };
    void reset() { ic1 = ic2 = 0.0f; }

    // returns { lp, hp }
    std::array<float, 2> process (float x, double cutoffHz, float peak, double sr)
    {
        const double fc = std::clamp (cutoffHz, 8.0, sr * 0.45);
        const float g = (float) std::tan (kPi * fc / sr);
        // Peak 0..1 -> damping. ~0.75 sits at the edge of self-oscillation,
        // as 7-8 does on classic analog filters.
        const float p = clampf (peak, 0.0f, 1.0f);
        // above ~0.92 the damping goes slightly negative: the filter sings on
        // its own, and the soft-clipped band-pass state sets the amplitude
        const float edge = clampf ((p - 0.92f) / 0.08f, 0.0f, 1.0f);
        const float k = 2.0f * std::max (0.012f, (1.0f - p) * (1.0f - p) * (1.0f - 0.35f * p)) - 0.06f * edge;

        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;

        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;

        // diode-ish limiting of the resonant (band-pass) state
        ic1 = 1.6f * fastTanh ((2.0f * v1 - ic1) * 0.625f);
        ic2 = 2.0f * v2 - ic2;

        return { v2, x - k * v1 - v2 };
    }
};

// The dual filter section: HPF then LPF in series, each with a Peak.
struct DualFilterCore
{
    ResonantSvf hpf, lpf;
    void reset() { hpf.reset(); lpf.reset(); }

    float process (float x, double hpfHz, float hpfPeak, double lpfHz, float lpfPeak, double sr)
    {
        // the filters are driven hard by hot VCO levels: a gentle input clip
        const float in = 1.25f * fastTanh (x * 0.8f);
        const float h = hpf.process (in, hpfHz, hpfPeak, sr)[1];
        return lpf.process (h, lpfHz, lpfPeak, sr)[0];
    }
};

//==============================================================================
// Skew LFO - a modulation generator. One Waveform knob skews a triangle
// from falling sawtooth (0) through triangle (0.5) to rising ramp (1), and the
// same knob sets the duty cycle of the pulse output. The pulse is what clocks
// the sample & hold in the Vaporization patch.
//==============================================================================
struct SkewLfo
{
    double phase { 0.0 };
    void reset (double p = 0.0) { phase = p; }

    // returns { triOut (-1..1), pulseOut (0/1) }, advancing by freq/sr
    std::array<float, 2> process (double freqHz, float waveform, double sr)
    {
        const double s = 0.02 + 0.96 * std::clamp ((double) waveform, 0.0, 1.0);
        const double p = phase;
        phase += freqHz / sr;
        phase -= std::floor (phase);
        const double tri = p < s ? (p / s) : (1.0 - (p - s) / (1.0 - s));
        return { (float) (tri * 2.0 - 1.0), p < s ? 1.0f : 0.0f };
    }
};

// tempo divisions offered by Skew LFO (and Cross Delay's sync): beats per cycle
inline double divisionBeats (int i)
{
    static const double beats[10] = { 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[std::clamp (i, 0, 9)];
}

//==============================================================================
// Analog EG - RC-style envelope with Delay, Attack, Hold, Decay, Sustain,
// Release. Attack charges toward 1.3 and stops at 1 (the classic convex
// analog attack); decay and release are exponential. Times are the time to
// (nearly) complete the segment.
//==============================================================================
struct AnalogEG
{
    enum Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };
    Stage stage { Idle }; float level { 0.0f }; double timer { 0.0 };

    void reset() { stage = Idle; level = 0.0f; timer = 0.0; }
    void gateOn (bool legato = false) { if (! legato || stage == Idle || stage == Release) { stage = Delay; timer = 0.0; } }
    void gateOff() { if (stage != Idle) stage = Release; }
    bool active() const { return stage != Idle; }

    float process (double sr, float delayMs, float attackMs, float holdMs,
                   float decayMs, float sustain, float releaseMs)
    {
        const double dt = 1.0 / sr;
        switch (stage)
        {
            case Idle: break;
            case Delay:
                timer += dt;
                if (timer * 1000.0 >= delayMs) { stage = Attack; timer = 0.0; }
                break;
            case Attack:
            {
                // charge toward 1.3: reaching 1.0 takes ln(1.3/0.3)=1.466 tau
                const double tau = std::max (0.0002, attackMs * 0.001 / 1.466);
                level += (float) ((1.3 - level) * (1.0 - std::exp (-dt / tau)));
                if (level >= 1.0f) { level = 1.0f; stage = Hold; timer = 0.0; }
                break;
            }
            case Hold:
                timer += dt;
                if (timer * 1000.0 >= holdMs) stage = Decay;
                break;
            case Decay:
            {
                const double tau = std::max (0.0002, decayMs * 0.001 / 4.6);   // ~ -40 dB in decayMs
                level += (float) ((sustain - level) * (1.0 - std::exp (-dt / tau)));
                if (std::abs (level - sustain) < 1.0e-4f) { level = sustain; stage = Sustain; }
                break;
            }
            case Sustain:
                level = sustain;   // follows the knob live
                break;
            case Release:
            {
                const double tau = std::max (0.0002, releaseMs * 0.001 / 4.6);
                level *= (float) std::exp (-dt / tau);
                if (level < 1.0e-5f) { level = 0.0f; stage = Idle; }
                break;
            }
        }
        return level;
    }
};

//==============================================================================
// PWM Osc - a polysynth VCO: Saw / PW / PWM at 16' 8' 4', plus the square
// sub-oscillator one or two octaves down. PWM runs from its own triangle LFO.
//==============================================================================
struct PwmOsc
{
    enum Wave { Saw = 0, PW, PWM };
    BlepOsc main, sub; double lfoPhase { 0.0 }; AnalogDrift drift;

    void reset (uint32_t seed)
    {
        Rng r; r.seed (seed * 2654435761u + 17u);
        main.reset (r.uni()); sub.reset (0.0); lfoPhase = r.uni();
        drift.reset (seed * 13u + 7u);
    }

    float process (double sr, double note, double pitchMod, int octave, int wave,
                   float pwAmount, double pwmHz, int subMode, float subLevel, float analog)
    {
        static const double feet[3] = { -12.0, 0.0, 12.0 };
        const double n = note + pitchMod + feet[std::clamp (octave, 0, 2)] + drift.next (sr) * analog * 0.08;
        const double dt = std::min (0.45, noteToHz (n) / sr);

        // the sub is derived from the main phase so it stays locked to it
        const double p = main.tick (dt);
        if (main.phase < p) ++cycles;

        lfoPhase += pwmHz / sr; lfoPhase -= std::floor (lfoPhase);
        const float lfo = BlepOsc::tri (lfoPhase) * 0.5f + 0.5f;    // 0..1

        float s = 0.0f;
        const float pw = clampf (pwAmount, 0.0f, 1.0f);
        switch (wave)
        {
            case Saw: s = BlepOsc::saw (p, dt); break;
            case PW:  s = BlepOsc::pulse (p, dt, 0.5 - 0.45 * pw); break;
            default:  s = BlepOsc::pulse (p, dt, 0.5 - 0.45 * pw * lfo); break;
        }

        if (subMode > 0)
        {
            // flip-flop divider: the sub's phase is the main phase counted
            // over 2 or 4 cycles, exactly as an analog divider does the VCO
            const int div = subMode == 1 ? 2 : 4;
            const double sp = ((double) (cycles % (uint32_t) div) + p) / div;
            s += subLevel * BlepOsc::pulse (sp, dt / div, 0.5);
        }
        return s;
    }

    uint32_t cycles { 0 };
};

//==============================================================================
// Warm Ladder - 24 dB resonant low-pass after classic polysynth chips: a four
// pole ladder with saturating stages; resonance thins the low end as the
// real chip does.
//==============================================================================
struct WarmLadderCore
{
    float s[4] { 0, 0, 0, 0 }; float lastY { 0.0f };
    void reset() { for (auto& v : s) v = 0.0f; lastY = 0.0f; }

    float process (float x, double cutoffHz, float reso, double sr)
    {
        const double fc = std::clamp (cutoffHz, 10.0, sr * 0.42);
        const float g = (float) (1.0 - std::exp (-kTwoPi * fc / sr));
        const float k = 4.7f * clampf (reso, 0.0f, 1.0f);        // self-oscillates from ~0.9
        const float in = fastTanh (x - k * lastY) ;
        s[0] += g * (in - s[0]);
        s[1] += g * (fastTanh (s[0]) - s[1]);
        s[2] += g * (s[1] - s[2]);
        s[3] += g * (s[2] - s[3]);
        lastY = s[3];
        return s[3] * (1.0f + 0.5f * k);   // partial make-up for the resonance loss
    }
};

//==============================================================================
// modulated fractional delay line used by the chorus-type effects
//==============================================================================
struct DelayLine
{
    std::vector<float> buf; int w { 0 }, mask { 0 };

    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // delay in samples >= 1, cubic (Hermite) interpolation
    float read (double d) const
    {
        const double rp = (double) w - d;
        const int i = (int) std::floor (rp);
        const float f = (float) (rp - i);
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)];
        const float y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
};

struct OnePole   // TPT one-pole, lp and hp outputs
{
    float s { 0.0f };
    void reset() { s = 0.0f; }
    float lp (float x, double hz, double sr)
    {
        const float g = (float) std::tan (kPi * std::clamp (hz, 1.0, sr * 0.45) / sr);
        const float v = (x - s) * g / (1.0f + g);
        const float y = v + s; s = y + v; return y;
    }
    float hp (float x, double hz, double sr) { return x - lp (x, hz, sr); }
};

//==============================================================================
// Ensemble - a three-phase BBD ensemble: three short delays,
// each swept by a slow LFO and a faster shimmer LFO 120 degrees apart, spread
// across the stereo field. A gentle BBD-style low-pass darkens the wet path.
//==============================================================================
struct EnsembleCore
{
    DelayLine line; double slow { 0.0 }, fast { 0.0 }; OnePole bbdL, bbdR; double sr { 48000.0 };

    void prepare (double newSr) { sr = newSr; line.prepare ((int) (sr * 0.03)); reset(); }
    void reset() { line.clear(); slow = 0.0; fast = 0.25; bbdL.reset(); bbdR.reset(); }

    // mode: 0 = Chorus, 1 = Phase, 2 = Ensemble (a vintage polysynth's three effects)
    // speed 0..1, depth 0..1, spread 0..1, mix 0..1 (wet amount)
    std::array<float, 2> process (float inL, float inR, float speed, float depth, float spread, float mix, int mode = 2)
    {
        const double slowHz = expMap (speed, 0.08, 2.5);
        const double fastHz = slowHz * 9.3;     // the shimmer rides at roughly 6 Hz for mid speeds
        slow += slowHz / sr; slow -= std::floor (slow);
        fast += fastHz / sr; fast -= std::floor (fast);

        const float mono = 0.5f * (inL + inR);
        line.push (mono);

        // Chorus: one slow sweep, two taps in anti-phase. Phase: short delays
        // swept further for a flanging, phasey colour. Ensemble: three taps
        // 120 degrees apart, slow sweep plus a faster shimmer.
        const double baseMs  = mode == 1 ? 2.2 : 7.0;
        const double sweepMs = mode == 1 ? 1.8 : 3.2;
        const double fastMix = mode == 2 ? 0.25 : 0.0;

        float tap[3];
        for (int i = 0; i < 3; ++i)
        {
            const double ph = mode == 2 ? i / 3.0 : i * 0.5;
            const double m = (1.0 - fastMix) * std::sin (kTwoPi * (slow + ph)) + fastMix * std::sin (kTwoPi * (fast + ph));
            const double ms = baseMs + depth * sweepMs * m;
            tap[i] = line.read (std::max (2.0, ms * 0.001 * sr));
        }
        if (mode != 2)
        {
            // two-tap modes: left leans on the 0-degree tap, right on the 180
            const float a = tap[0], b = tap[1];
            tap[1] = 0.5f * (a + b);
            tap[2] = b;
        }

        const float c = tap[1];
        float wl = 0.5f * (tap[0] + c) + spread * 0.5f * (tap[0] - tap[2]);
        float wr = 0.5f * (tap[2] + c) + spread * 0.5f * (tap[2] - tap[0]);
        wl = bbdL.lp (wl, 9000.0, sr);
        wr = bbdR.lp (wr, 9000.0, sr);
        return { inL * (1.0f - 0.5f * mix) + wl * mix, inR * (1.0f - 0.5f * mix) + wr * mix };
    }
};

//==============================================================================
// Stereo Phaser - a chain of first-order allpasses per
// channel, an LFO whose right channel can run at a phase offset from the left,
// Manual (the centre), Depth, Resonance (bipolar feedback) and a High Damp
// low-pass inside the feedback loop.
//==============================================================================
struct StereoPhaser
{
    static constexpr int kMaxStages = 12;
    float ap[2][kMaxStages] {}; float fb[2] {}; OnePole damp[2]; double lfo { 0.0 }; double sr { 48000.0 };

    void prepare (double newSr) { sr = newSr; reset(); }
    void reset() { for (auto& c : ap) for (auto& s : c) s = 0.0f; fb[0] = fb[1] = 0.0f; damp[0].reset(); damp[1].reset(); lfo = 0.0; }

    // lfoShape: 0 = triangle, 1 = sine
    std::array<float, 2> process (float inL, float inR, double rateHz, float lrPhaseDeg,
                                  double manualHz, float depth, float resonance,
                                  double highDampHz, float wet, int stages, int lfoShape)
    {
        lfo += rateHz / sr; lfo -= std::floor (lfo);
        float out[2]; const float in[2] { inL, inR };
        stages = std::clamp (stages, 2, kMaxStages);

        for (int c = 0; c < 2; ++c)
        {
            double ph = lfo + (c == 1 ? lrPhaseDeg / 360.0 : 0.0); ph -= std::floor (ph);
            const float l = lfoShape == 0 ? BlepOsc::tri (ph) : (float) std::sin (kTwoPi * ph);
            // depth sweeps up to +-3 octaves around Manual
            const double hz = std::clamp (manualHz * std::pow (2.0, l * depth * 3.0), 20.0, sr * 0.45);
            const float t = (float) std::tan (kPi * hz / sr);
            const float a = (t - 1.0f) / (t + 1.0f);

            float x = in[c] + fb[c] * resonance * 0.95f;
            for (int s = 0; s < stages; ++s)
            {
                const float y = a * x + ap[c][s];
                ap[c][s] = x - a * y;
                x = y;
            }
            fb[c] = damp[c].lp (fastTanh (x), highDampHz, sr);
            // phaser = dry + allpassed: notches where they cancel
            out[c] = in[c] * (1.0f - wet) + 0.5f * (in[c] + x) * wet;
        }
        return { out[0], out[1] };
    }
};

//==============================================================================
// Cross Delay - a stereo / cross delay: two lines with their own
// time and feedback; in Cross mode each line feeds the other. High and Low
// Damp shape the repeats; Spread narrows the wet image toward mono.
//==============================================================================
struct CrossDelay
{
    DelayLine l, r; OnePole lpL, lpR, hpL, hpR; float fbL { 0.0f }, fbR { 0.0f };
    float smTimeL { -1.0f }, smTimeR { -1.0f }; double sr { 48000.0 };

    void prepare (double newSr) { sr = newSr; l.prepare ((int) (sr * 2.8)); r.prepare ((int) (sr * 2.8)); reset(); }
    void reset() { l.clear(); r.clear(); lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); fbL = fbR = 0.0f; smTimeL = smTimeR = -1.0f; }

    std::array<float, 2> process (float inL, float inR, float timeLms, float timeRms,
                                  float feedbackL, float feedbackR, bool cross,
                                  double highDampHz, double lowDampHz, float spread,
                                  float inputTrim, float wet)
    {
        // glide the times so turning the knobs pitch-bends like tape instead of clicking
        if (smTimeL < 0.0f) { smTimeL = timeLms; smTimeR = timeRms; }
        const float k = (float) (1.0 - std::exp (-1.0 / (0.08 * sr)));
        smTimeL += (timeLms - smTimeL) * k;
        smTimeR += (timeRms - smTimeR) * k;

        const float yl = l.read (std::max (1.0, smTimeL * 0.001 * sr));
        const float yr = r.read (std::max (1.0, smTimeR * 0.001 * sr));

        float dl = hpL.hp (lpL.lp (yl, highDampHz, sr), lowDampHz, sr);
        float dr = hpR.hp (lpR.lp (yr, highDampHz, sr), lowDampHz, sr);

        const float xl = inL * inputTrim, xr = inR * inputTrim;
        if (cross) { l.push (xl + fastTanh (dr * feedbackR)); r.push (xr + fastTanh (dl * feedbackL)); }
        else       { l.push (xl + fastTanh (dl * feedbackL)); r.push (xr + fastTanh (dr * feedbackR)); }

        // spread: 1 = hard L/R, 0 = both repeats in the centre
        const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * spread;
        const float wl = m + s, wr = m - s;
        return { inL * (1.0f - wet) + wl * wet, inR * (1.0f - wet) + wr * wet };
    }
};

//==============================================================================
// Smooth Hall - an 8-line feedback delay network with slowly modulated line
// lengths (the "smooth" part: no metallic flutter), an input diffuser, a
// pre-delay, RT60-correct per-line damping and a pre-EQ (low/high shelf gains
// on what goes in, as in classic studio reverbs).
//==============================================================================
struct SmoothHall
{
    static constexpr int N = 8;
    DelayLine lines[N]; DelayLine pre; DelayLine diff[4]; float diffState[4] {};
    OnePole damp[N]; OnePole eqLo[2], eqHi[2];
    double modPh[N] {}; double sr { 48000.0 };
    double len[N] {};

    void prepare (double newSr)
    {
        sr = newSr;
        // mutually prime-ish lengths 29..87 ms
        static const double ms[N] = { 29.7, 37.1, 41.1, 43.7, 53.3, 61.0, 71.9, 86.9 };
        for (int i = 0; i < N; ++i) { len[i] = ms[i] * 0.001 * sr; lines[i].prepare ((int) (len[i] + sr * 0.01)); modPh[i] = i / (double) N; }
        pre.prepare ((int) (sr * 0.25));
        static const double dms[4] = { 4.7, 3.6, 12.7, 9.3 };
        for (int i = 0; i < 4; ++i) diff[i].prepare ((int) (dms[i] * 0.001 * sr) + 8);
        reset();
    }

    void reset()
    {
        for (auto& d : lines) d.clear();
        pre.clear(); for (auto& d : diff) d.clear();
        for (auto& f : damp) f.reset();
        for (auto& f : eqLo) f.reset();
        for (auto& f : eqHi) f.reset();
        for (auto& s : diffState) s = 0.0f;
    }

    // Schroeder allpass on a delay line
    float allpass (int i, float x, double ms, float g)
    {
        const float d = diff[i].read (ms * 0.001 * sr);
        const float v = x + g * d;
        diff[i].push (v);
        return d - g * v;
    }

    std::array<float, 2> process (float inL, float inR, float rt60s, double highDampHz,
                                  float preDelayMs, float lowGainDb, float highGainDb, float wet)
    {
        // pre-EQ: shelving made from one-pole splits (low < 250 Hz, high > 4 kHz)
        const float gl = std::pow (10.0f, lowGainDb / 20.0f), gh = std::pow (10.0f, highGainDb / 20.0f);
        float x[2] { inL, inR };
        for (int c = 0; c < 2; ++c)
        {
            const float lo = eqLo[c].lp (x[c], 250.0, sr);
            const float hi = eqHi[c].hp (x[c], 4000.0, sr);
            x[c] = x[c] + (gl - 1.0f) * lo + (gh - 1.0f) * hi;
        }

        pre.push (0.5f * (x[0] + x[1]));
        float m = pre.read (std::max (1.0, preDelayMs * 0.001 * sr));
        m = allpass (0, m, 4.7, 0.70f);
        m = allpass (1, m, 3.6, 0.70f);
        m = allpass (2, m, 12.7, 0.62f);
        m = allpass (3, m, 9.3, 0.62f);

        // read the network (slight length modulation, +-0.3 ms at ~0.1-0.4 Hz)
        float y[N];
        for (int i = 0; i < N; ++i)
        {
            modPh[i] += (0.11 + 0.037 * i) / sr; modPh[i] -= std::floor (modPh[i]);
            const double d = len[i] + 0.0003 * sr * std::sin (kTwoPi * modPh[i]);
            y[i] = lines[i].read (d);
        }

        // Householder mixing: y - 2/N * sum
        float sum = 0.0f; for (float v : y) sum += v;
        const float h = sum * (2.0f / N);

        const float rt = std::max (0.1f, rt60s) * 1.06f;   // measured: the diffusers shave ~6% off
        for (int i = 0; i < N; ++i)
        {
            const float g = (float) std::pow (10.0, -3.0 * (len[i] / sr) / rt);    // RT60 gain per pass
            float v = (y[i] - h) * g;
            v = damp[i].lp (v, highDampHz, sr);
            const float injection = (i & 1) ? m : -m;
            lines[i].push (v + injection * 0.35f);
        }

        const float wl = 0.6f * (y[0] - y[2] + y[4] - y[6]) + 0.2f * (y[1] - y[5]);
        const float wr = 0.6f * (y[1] - y[3] + y[5] - y[7]) + 0.2f * (y[2] - y[6]);
        return { inL * (1.0f - wet) + wl * wet, inR * (1.0f - wet) + wr * wet };
    }
};

} // namespace vintagedsp
