/*
  ==============================================================================
    ResonateModelCore.h - the "Modelled" resonator engine of the Resonate
    module (ResonateBankModule). JUCE-free. Model derived purely from
    black-box impulse-response measurements (system identification).
  ==============================================================================
*/
#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace aqresonate
{

// Signal flow per voice (per sample):
//       u   = input (already scaled by 0.25 and high-passed, see InputStage)
//       w   = u + s * g * yPrev          s = +1 (Mode A) or -1 (Mode B)
//       r   = delay(w, D)                integer delay + 1st-order allpass
//       y   = F(r)                       F = ((1+z^-1)/2)^2 * LP1(p1) * LP1(p2)
//       yPrev = y                        (the feedback path carries one extra
//                                         unit delay -> loop = z^-1 z^-D F)
// 
//   D is chosen so the loop phase delay at f0 equals P + delta(note,color),
//   i.e. the fundamental is compensated for F's phase but upper partials
//   keep F's dispersion (progressively sharp) -- the characteristic "twang".

// Auto-generated from impulse-response measurements (ResLab, Oct 2026).
namespace ResFit
{
static constexpr int kSurfN = 12;
static const double kPole1[kSurfN] = { -2.90221071, 1.82227628, 1.00875786, 7.18037179, -0.837008126, 0.0339687581, -22.2239368, -0.164835064, -0.04363341, -0.0199878702, 27.2553931, -11.2300086 };
static const double kPole2[kSurfN] = { 0.807818202, -3.38623323, -0.524719465, 18.6455095, 1.0518945, 0.0702800675, -57.1460665, -0.728007247, -0.209760468, 0.00342133193, 72.1570035, -31.4256017 };
static const double kPole1B[kSurfN] = { -2.74484746, 0.759024831, 0.907980079, 10.3842906, -0.390989387, -0.0846904618, -28.5140545, -0.749522417, 0.00668891571, 0.0593447467, 34.9858411, -14.7284851 };
static const double kPole2B[kSurfN] = { 1.02823056, -5.32898675, -0.729976558, 23.1404255, 2.1624347, 0.000416408549, -62.9026696, -1.89499033, -0.273943789, 0.0799862128, 78.9726392, -34.940829 };
static const double kDecayPoly[6] = { -0.00962237535, 0.170228349, -1.1263012, 3.24781787, -5.1040708, 4.43158024 }; // highest power first, in ln(x) -> ln(-ln g)
static const double kConstK = 0.975009167, kConstN0 = 37.3212314, kConstB = 0.997710281;
static const int kDeltaNotes[6] = { 24, 36, 48, 60, 72, 84 };
static const int kDeltaCols[12] = { 0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
static const double kDelta[6][12] = {
    { 1.4968, 1.7531, 1.6629, 1.3633, 1.1892, 1.0958, 1.0468, 1.0164, 0.9998, 0.9932, 0.9908, 0.9895 },
    { 2.1227, 1.9847, 1.7585, 1.3953, 1.2066, 1.1101, 1.0564, 1.0245, 1.0079, 1.0003, 0.9967, 0.9947 },
    { 1.8696, 1.7699, 1.6076, 1.3345, 1.1886, 1.1128, 1.0657, 1.0336, 1.0147, 1.0055, 1.0012, 0.9981 },
    { 1.5950, 1.5386, 1.4404, 1.2578, 1.1566, 1.1049, 1.0721, 1.0449, 1.0220, 1.0098, 1.0041, 0.9999 },
    { 1.3888, 1.3576, 1.3071, 1.1921, 1.1259, 1.1000, 1.0769, 1.0607, 1.0317, 1.0177, 1.0069, 1.0024 },
    { 1.2613, 1.2655, 1.2182, 1.1506, 1.1233, 1.0972, 1.0819, 1.0674, 1.0578, 1.0230, 1.0113, 1.0138 }
};
}

namespace ResModel
{

static constexpr double kRefRate = 48000.0;
static constexpr double kPi = 3.14159265358979323846;

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- Color filter poles (smooth surfaces fitted at 48 kHz) -------------------
inline void colorPoles(double color, double note, double& p1, double& p2)
{
    // Two fitted surfaces: A (plain fit, accurate for dark colors) and
    // B (fit weighted by how much per-pass error accumulates in long, bright
    // tails). Crossfaded over Color 40..70.
    const double u = clampd(color, 0.0, 100.0) / 100.0;
    const double v = (clampd(note, 24.0, 84.0) - 48.0) / 24.0;
    const double b[ResFit::kSurfN] = { 1.0, u, v, u*u, u*v, v*v, u*u*u, u*u*v, u*v*v, v*v*v, u*u*u*u, u*u*u*u*u };
    double qa1 = 0, qa2 = 0, qb1 = 0, qb2 = 0;
    for (int i = 0; i < ResFit::kSurfN; ++i)
    {
        qa1 += ResFit::kPole1[i] * b[i];  qa2 += ResFit::kPole2[i] * b[i];
        qb1 += ResFit::kPole1B[i] * b[i]; qb2 += ResFit::kPole2B[i] * b[i];
    }
    double t = clampd((color - 40.0) / 30.0, 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);
    p1 = (1.0 - t) * (1.0 - std::exp(qa1)) + t * (1.0 - std::exp(qb1));
    p2 = (1.0 - t) * std::tanh(qa2) + t * std::tanh(qb2);
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
    int i = 0; while (i < 4 && n > ResFit::kDeltaNotes[i + 1]) ++i;
    int j = 0; while (j < 10 && c > ResFit::kDeltaCols[j + 1]) ++j;
    const double tn = (n - ResFit::kDeltaNotes[i]) / double(ResFit::kDeltaNotes[i + 1] - ResFit::kDeltaNotes[i]);
    const double tc = (c - ResFit::kDeltaCols[j]) / double(ResFit::kDeltaCols[j + 1] - ResFit::kDeltaCols[j]);
    const double a = ResFit::kDelta[i][j] * (1 - tc) + ResFit::kDelta[i][j + 1] * tc;
    const double b = ResFit::kDelta[i + 1][j] * (1 - tc) + ResFit::kDelta[i + 1][j + 1] * tc;
    return a * (1 - tn) + b * tn;
}

// --- Decay -> per-period loop gain ------------------------------------------
inline double lnMinusLnG(double x)   // universal curve: ln(-ln g) as fn of x
{
    const double lx = std::log(x);
    double y = 0.0;
    for (int i = 0; i < 6; ++i) y = y * lx + ResFit::kDecayPoly[i];
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
    const double x = decay * std::pow(2.0, (note - ResFit::kConstN0) / 24.0 * ResFit::kConstB);
    const double g = universalGain(x);
    if (g <= 0.0) return 0.0;
    return std::exp(ResFit::kConstK * std::log(g)); // -ln g scaled by kConstK
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
    double apX1 = 0, apY1 = 0;        // allpass interpolator state

    void prepare(double sr)
    {
        sampleRate = sr;
        buf.assign(kBufSize, 0.0f);
        reset();
    }
    void reset()
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
        w = 0; a1 = a2 = l1 = l2 = yPrev = 0.0; apX1 = apY1 = 0.0;
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
        D = clampd(T - 1.0 - pdF, 0.5, double(kBufSize - 4));
    }

    inline float process(float u)
    {
        const double win = double(u) + sign * g * yPrev;
        buf[w] = float(win);

        // Fractional read: integer delay Di plus a 1st-order allpass for the
        // remaining d in [0.5, 1.5). The allpass is magnitude-flat, so it adds
        // no extra high-frequency loss per round trip (which would otherwise
        // accumulate and darken sustained tones and tails).
        const int m = kBufSize - 1;
        const int di = int(D - 0.5);
        const double d = D - di;                    // 0.5 .. 1.5
        const double a = (1.0 - d) / (1.0 + d);
        const double xi = buf[(w - di) & m];
        const double r = a * xi + apX1 - a * apY1;
        apX1 = xi; apY1 = r;
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
} // namespace ResModel

} // namespace aqresonate
