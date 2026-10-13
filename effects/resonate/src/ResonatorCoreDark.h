/*
  ==============================================================================
    ResonatorCoreDark.h  --  the "Dark" engine: the first modelled version (v3.0, 4-point Lagrange read), kept verbatim.

    Model derived purely from black-box impulse-response measurements
    (system identification), not from any third-party source code.

    Signal flow per voice (per sample):

        u   = input (already scaled by 0.25 and high-passed, see InputStage)
        w   = u + s * g * yPrev          s = +1 (Mode A) or -1 (Mode B)
        r   = delay(w, D)                4-point Lagrange fractional read
        y   = F(r)                       F = ((1+z^-1)/2)^2 * LP1(p1) * LP1(p2)
        yPrev = y                        (the feedback path carries one extra
                                          unit delay -> loop = z^-1 z^-D F)

    D is chosen so the loop phase delay at f0 equals P + delta(note,color),
    i.e. the fundamental is compensated for F's phase but upper partials
    keep F's dispersion (progressively sharp) -- the characteristic "twang".
  ==============================================================================
*/
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include "ResFitTablesDark.h"

namespace ResModelDark
{
static constexpr double kRefRate = 48000.0;
static constexpr double kPi = 3.14159265358979323846;

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- Color filter poles (smooth surfaces fitted at 48 kHz) -------------------
inline void colorPoles(double color, double note, double& p1, double& p2)
{
    const double u = clampd(color, 0.0, 100.0) / 100.0;
    const double v = (clampd(note, 24.0, 84.0) - 48.0) / 24.0;
    const double b[ResFitDark::kSurfN] = { 1.0, u, v, u*u, u*v, v*v, u*u*u, u*u*v, u*v*v, v*v*v, u*u*u*u, u*u*u*u*u };
    double q1 = 0.0, q2 = 0.0;
    for (int i = 0; i < ResFitDark::kSurfN; ++i) { q1 += ResFitDark::kPole1[i] * b[i]; q2 += ResFitDark::kPole2[i] * b[i]; }
    p1 = 1.0 - std::exp(q1);
    p2 = std::tanh(q2);
}

// Re-map a pole designed at 48 kHz to another sample rate (keeps its cutoff).
inline double remapPole(double p, double sampleRate)
{
    const double r = kRefRate / sampleRate;
    if (p >= 0.0) return std::pow(std::max(p, 1e-12), r);
    return p * r; // negative (HF-shaping) pole: shrink towards 0 at higher rates
}

// --- Tuning offset delta(note,color) in samples @48k ------------------------
inline double tuningDelta(double note, double color)
{
    const double n = clampd(note, 24.0, 84.0), c = clampd(color, 0.0, 100.0);
    int i = 0; while (i < 4 && n > ResFitDark::kDeltaNotes[i + 1]) ++i;
    int j = 0; while (j < 10 && c > ResFitDark::kDeltaCols[j + 1]) ++j;
    const double tn = (n - ResFitDark::kDeltaNotes[i]) / double(ResFitDark::kDeltaNotes[i + 1] - ResFitDark::kDeltaNotes[i]);
    const double tc = (c - ResFitDark::kDeltaCols[j]) / double(ResFitDark::kDeltaCols[j + 1] - ResFitDark::kDeltaCols[j]);
    const double a = ResFitDark::kDelta[i][j] * (1 - tc) + ResFitDark::kDelta[i][j + 1] * tc;
    const double b = ResFitDark::kDelta[i + 1][j] * (1 - tc) + ResFitDark::kDelta[i + 1][j + 1] * tc;
    return a * (1 - tn) + b * tn;
}

// --- Decay -> per-period loop gain ------------------------------------------
inline double lnMinusLnG(double x)   // universal curve: ln(-ln g) as fn of x
{
    const double lx = std::log(x);
    double y = 0.0;
    for (int i = 0; i < 6; ++i) y = y * lx + ResFitDark::kDecayPoly[i];
    return y;
}
inline double universalGain(double x)
{
    if (x <= 0.0) return 0.0;
    const double xLo = 10.0, xHi = 200.0;
    if (x < xLo)
    {   // low end: g = x^2/(x^2+c), matched at xLo
        const double gLo = std::exp(-std::exp(lnMinusLnG(xLo)));
        const double c = xLo * xLo * (1.0 - gLo) / gLo;
        return x * x / (x * x + c);
    }
    if (x > xHi)
    {   // high end: -ln g = C / x^2, matched at xHi
        const double C = std::exp(lnMinusLnG(xHi)) * xHi * xHi;
        return std::exp(-C / (x * x));
    }
    return std::exp(-std::exp(lnMinusLnG(x)));
}
inline double loopGain(double decay, double note, bool constMode)
{
    decay = clampd(decay, 0.0, 100.0);
    if (!constMode)
        return universalGain(decay * std::pow(2.0, (note - 24.0) / 48.0));
    const double x = decay * std::pow(2.0, (note - ResFitDark::kConstN0) / 24.0 * ResFitDark::kConstB);
    const double g = universalGain(x);
    if (g <= 0.0) return 0.0;
    return std::exp(ResFitDark::kConstK * std::log(g)); // -ln g scaled by kConstK
}

// --- Input stage: x0.25 and 1st-order bilinear HP at ~327 Hz ----------------
struct InputStage
{
    double c = 0.9581, x1 = 0.0, y1 = 0.0;
    void prepare(double sampleRate)
    {
        const double fc = 327.0;
        const double K = std::tan(kPi * fc / sampleRate);
        c = (1.0 - K) / (1.0 + K);
        reset();
    }
    void reset() { x1 = y1 = 0.0; }
    inline float process(float x)
    {
        const double y = 0.5 * (1.0 + c) * (double(x) - x1) + c * y1;
        x1 = x; y1 = y;
        return float(0.25 * y);
    }
};

// --- Input "Filter" section: RBJ-style biquads -----------------------------
struct InputBiquad
{
    enum Type { LP = 0, HP = 1, BP = 2, Notch = 3 };
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(int type, double freq, double sampleRate)
    {
        freq = clampd(freq, 10.0, 0.45 * sampleRate);
        const double w0 = 2.0 * kPi * freq / sampleRate, cw = std::cos(w0), sw = std::sin(w0);
        const double Q = (type == LP || type == HP) ? 0.70710678 : (2.0 / 3.0);
        const double alpha = sw / (2.0 * Q);
        double B0, B1, B2, A0 = 1 + alpha, A1 = -2 * cw, A2 = 1 - alpha;
        switch (type)
        {
            case LP:    B0 = (1 - cw) / 2; B1 = 1 - cw;  B2 = (1 - cw) / 2; break;
            case HP:    B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; break;
            case BP:    B0 = alpha;        B1 = 0;       B2 = -alpha;       break;
            default:    B0 = 1;            B1 = -2 * cw; B2 = 1;            break;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
    void reset() { z1 = z2 = 0; }
    inline float process(float xf)
    {   // transposed direct form II
        const double x = xf, y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return float(y);
    }
};

// --- One resonator voice ----------------------------------------------------
struct Voice
{
    static constexpr int kBufSize = 1 << 16;   // enough for MIDI note 0 at 192 kHz
    std::vector<float> buf;
    int w = 0;
    double sampleRate = 48000.0;

    // coefficients
    double D = 100.0, g = 0.0, p1 = 0.0, p2 = 0.0, sign = 1.0;
    // filter state
    double a1 = 0, a2 = 0;            // averager history
    double l1 = 0, l2 = 0;            // one-pole states
    double yPrev = 0;

    void prepare(double sr)
    {
        sampleRate = sr;
        buf.assign(kBufSize, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        w = 0; a1 = a2 = l1 = l2 = yPrev = 0.0;
    }

    // note: fractional MIDI note incl. fine tune; decay/color 0..100
    void setParams(double note, double decay, double color, bool constMode, bool modeB)
    {
        const double f0 = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        const double P = sampleRate / clampd(f0, 1.0, 0.45 * sampleRate);

        double q1, q2; colorPoles(color, note, q1, q2);
        p1 = remapPole(q1, sampleRate); p2 = remapPole(q2, sampleRate);

        g = loopGain(decay, note, constMode);
        sign = modeB ? -1.0 : 1.0;

        // phase delay of F at the fundamental
        const double wv = 2.0 * kPi / P;
        auto onePolePD = [wv](double p) { return std::atan2(p * std::sin(wv), 1.0 - p * std::cos(wv)) / wv; };
        const double pdF = 1.0 + onePolePD(p1) + onePolePD(p2);   // 1.0 from ((1+z^-1)/2)^2

        const double delta = tuningDelta(note, color) * (sampleRate / kRefRate);
        const double T = P + delta;                 // total loop delay at f0
        D = clampd(T - 1.0 - pdF, 1.0, double(kBufSize - 4));
    }

    inline float process(float u)
    {
        const double win = double(u) + sign * g * yPrev;
        buf[w] = float(win);

        // 4-point Lagrange read at delay D (0 = current sample)
        const int di = int(D);
        const double f = D - di;               // 0..1
        const int m = kBufSize - 1;
        const double xm1 = buf[(w - di + 1) & m];  // delay di-1
        const double x0  = buf[(w - di) & m];      // delay di
        const double x1  = buf[(w - di - 1) & m];  // delay di+1
        const double x2  = buf[(w - di - 2) & m];  // delay di+2
        // Lagrange, evaluating at offset f from x0 towards x1 (taps at -1,0,1,2)
        const double c0 = -f * (f - 1) * (f - 2) / 6.0;
        const double c1 = (f + 1) * (f - 1) * (f - 2) / 2.0;
        const double c2 = -(f + 1) * f * (f - 2) / 2.0;
        const double c3 = (f + 1) * f * (f - 1) / 6.0;
        const double r = c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
        w = (w + 1) & m;

        // F: ((1+z^-1)/2)^2 then two one-poles (DC gain 1)
        const double s = 0.25 * (r + 2.0 * a1 + a2);
        a2 = a1; a1 = r;
        l1 = (1.0 - p1) * s + p1 * l1;
        l2 = (1.0 - p2) * l1 + p2 * l2;
        yPrev = l2;
        return float(l2);
    }
};
} // namespace ResModelDark
