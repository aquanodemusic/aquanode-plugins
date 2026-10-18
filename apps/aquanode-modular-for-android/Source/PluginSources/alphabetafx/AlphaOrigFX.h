#pragma once
// ============================================================================
//  AlphaOrigFX - the ORIGINAL-mode drive, filter and chorus for AlphaBetaFX
//
//  These are the same DSP blocks the AlphaBeta synth uses in its ORIGINAL
//  engine (model of the LinPlug FreeAlpha 3.3, see AlphaBeta/Source/
//  AlphaOrigEngine.h). Copied here, with only the two small measured tables
//  they need, so the FX project stays self-contained and does not have to pull
//  in the synth's wavetable data.
//
//  If you retune these in AlphaOrigEngine.h / AlphaOrigData.h, mirror the
//  change here so both plugins keep sounding the same.
//
//  Differences from the synth versions:
//   * Chorus::processStereo() takes a real stereo input (the synth's voices are
//     mono, so its chorus reads L only). For L == R the output is identical.
// ============================================================================
#include <cmath>
#include <vector>
#include <algorithm>

namespace AlphaOrigFX {

constexpr double kPi = 3.14159265358979323846;

// ---- measured tables (from AlphaOrigData.h) --------------------------------
// log2(SVF frequency coefficient) as a polynomial in the internal cutoff u (highest power first)
inline const double kCutPoly[7] = {-25.9538068,70.6251571,-78.5133028,43.8277998,-12.4606743,11.5334146,-9.04474143};
// Drive shaper y = A*g*x / (1 + |g*x|^n)^(1/n) for drive = 0.05, 0.10 ... 1.00: {A, g, n}
inline const float kDrive[20][3] = {{10.0f,0.0969049f,0.533104f},{10.0f,0.102689f,0.49795f},{9.99839f,0.111133f,0.462499f},{8.39271f,0.143556f,0.448406f},{6.07513f,0.21448f,0.452933f},{4.32726f,0.330141f,0.461151f},{3.05948f,0.519553f,0.473634f},{2.16983f,0.827945f,0.49097f},{1.56142f,1.32206f,0.51371f},{1.15272f,2.09472f,0.542301f},{0.881148f,3.26945f,0.577005f},{0.701794f,5.01177f,0.617813f},{0.583959f,7.56436f,0.664363f},{0.507255f,11.3433f,0.715876f},{0.458308f,17.1865f,0.77111f},{0.428285f,27.039f,0.828356f},{0.411178f,46.2165f,0.88555f},{0.402665f,93.2149f,0.940983f},{0.39939f,259.255f,0.998995f},{0.398714f,672.659f,1.08374f}};

// ---- knob laws of the original (chorus time / rate) ------------------------
inline double expMap(double v, double mn, double mx, double k) {
    return mn + (mx - mn) * (std::exp(k * v) - 1.0) / (std::exp(k) - 1.0);
}
inline double invExpMap(double x, double mn, double mx, double k) {
    double v = std::log(1.0 + (x - mn) * (std::exp(k) - 1.0) / (mx - mn)) / k;
    return std::min(1.0, std::max(0.0, v));
}
constexpr double kChorusMsMin = 0.01, kChorusMsMax = 6.0;   // Law::chorusMs
constexpr double kChorusHzMin = 0.02, kChorusHzMax = 4.0;   // Law::chorusHz
constexpr double kChorusLawK  = 4.0;

// ---- filter coefficient laws ------------------------------------------------
inline double cutoffCoef(double u) {            // u: internal cutoff (0 = 78 Hz display, 1 = 20 kHz display)
    if (u <= 0.0) return 0.00116;
    if (u >= 0.992) return 1.0;
    double y = kCutPoly[0];
    for (int i = 1; i < 7; ++i) y = y * u + kCutPoly[i];
    return std::min(1.0, std::exp2(y));
}
inline double cutoffCoefAt(double u, double fs) {
    const double F = cutoffCoef(u);
    if (std::abs(fs - 44100.0) < 1.0 || u >= 0.992) return F;
    const double fc = 44100.0 / kPi * std::asin(std::min(1.0, 0.5 * F));
    return std::min(1.0, 2.0 * std::sin(kPi * std::min(fc, 0.24 * fs) / fs));
}
// display cutoff in Hz -> internal u (same as the synth's knobToU, but from Hz)
inline double hzToU(double hz) { return std::log2(std::max(1.0, hz) / 78.125) / 8.0; }
inline double resoDamp(double r) { r = std::min(1.0, std::max(0.0, r)); double a = 1.0 - 0.9 * r; return a * a; }

// ---- Chamberlin SVF (LP state clamped to +-1, as in the original) -----------
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

// LP24+ ("alternative design"): two SVF low-pass stages, 2x oversampled
struct LP24Plus {
    SVF a, b;
    void reset() { a.reset(); b.reset(); }
    inline double process(double x, double F, double r) {
        r = std::min(1.0, std::max(0.0, r));
        const double Fa = F * (0.3792 + F * (-0.0012 + 0.1064 * F));
        const double Fb = F * (0.3790 + F * (0.4771 + 0.2933 * F));
        const double r2 = r * r, r8 = r2 * r2 * r2 * r2;
        const double qb = (std::pow(1.0 - r, 1.9) - 0.02 * r8) * (1.0 - 0.2384 * F);
        double y = 0.0;
        for (int i = 0; i < 2; ++i) y = a.processLPfree(b.processLP(x, Fb, qb), Fa, 1.0);
        return y;
    }
};

// One channel of the original filter section: LP12 / LP24 / LP24+ / BP12 / HP12
struct Filter {
    SVF f1, f2; LP24Plus lad;
    void reset() { f1.reset(); f2.reset(); lad.reset(); }
    inline double process(int type, double x, double F, double q, double r) {
        switch (type) {
        case 1:  return f2.processLPfree(f1.processLP(x, F, q), F, 1.0);
        case 2:  return lad.process(x, F, r);
        case 3:  return f1.processBP(x, F, q);
        case 4:  return f1.processHP(x, F, q);
        default: return f1.processLP(x, F, q);
        }
    }
};

// ---- Drive (pre-filter saturation, "Hard" mode) -----------------------------
class Drive {
public:
    void set(double d) {
        if (std::abs(d - amount) < 1e-9 && built) return;
        amount = d; built = true;
        if (d <= 0.0) { bypass = true; return; }
        bypass = false;
        double x = d / 0.05 - 1.0; int i = (int)std::floor(x); double fr = x - i;
        auto P = [](int j, int c) { j = std::max(0, std::min(19, j)); return (double)kDrive[j][c]; };
        double A, g, n;
        if (x < 0.0) { A = P(0, 0); g = P(0, 1); n = P(0, 2); mixLin = 1.0 + x; }
        else {
            A = std::exp((1 - fr) * std::log(P(i, 0)) + fr * std::log(P(i + 1, 0)));
            g = std::exp((1 - fr) * std::log(P(i, 1)) + fr * std::log(P(i + 1, 1)));
            n = (1 - fr) * P(i, 2) + fr * P(i + 1, 2); mixLin = 0.0;
        }
        for (int k = 0; k <= kSize; ++k) {
            double xv = (double)k / kSize * kMax;
            table[k] = (float)(A * g * xv / std::pow(1.0 + std::pow(g * xv, n), 1.0 / n));
        }
    }
    inline double process(double x) const {
        if (bypass) return x;
        double ax = std::abs(x), pos = ax / kMax * kSize, y;
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

// ---- Chorus: 2 triangle-swept taps per side --------------------------------
// Stereo-input version: the left taps read the left delay line, the right taps
// the right one. With L == R this is sample-identical to the synth's chorus.
class Chorus {
public:
    void prepare(double sr) {
        fs = sr;
        size_t need = (size_t)(8 + 2.5 * 6.0 * sr / 1000.0) + 16, sz = 1;
        while (sz < need) sz <<= 1;
        for (auto& b : buf) b.assign(sz, 0.0f);
        mask = sz - 1; w = 0;
        for (int i = 0; i < 4; ++i) inc[i] = tapHz(0.5, i) / fs;
        reset();
    }
    void reset(double hzNow = -1.0) {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 0.0f);
        for (int i = 0; i < 4; ++i) {
            ph[i] = kTap[i].phase0;
            if (hzNow > 0.0) inc[i] = tapHz(hzNow, i) / fs;
        }
    }
    void processStereo(float* L, float* R, int n, double wet, double ms, double hz) {
        if (buf[0].empty()) return;
        const double dry = 1.0 - 0.62 * wet, wg = wet * (1.0 - 0.62 * wet);
        const double D = ms * fs / 1000.0, dt = 1.0 / fs;
        for (int s = 0; s < n; ++s) {
            const float xl = L[s], xr = R[s];
            buf[0][w] = xl; buf[1][w] = xr;
            double taps[4];
            for (int i = 0; i < 4; ++i) {
                const Tap& t = kTap[i];
                double p = ph[i], tri = p < 0.5 ? 2 * p : 2 - 2 * p;
                double d = 8.0 + D * (t.lo + (t.hi - t.lo) * tri);
                taps[i] = read(i < 2 ? 0 : 1, d);
                const double before = ph[i];
                ph[i] += inc[i];
                if (ph[i] >= 1.0) { ph[i] -= 1.0; inc[i] = tapHz(hz, i) * dt; }
                else if (before < 0.5 && ph[i] >= 0.5) inc[i] = tapHz(hz, i) * dt;
            }
            L[s] = (float)(dry * xl + wg * (taps[0] + taps[1]));
            R[s] = (float)(dry * xr + wg * (taps[2] + taps[3]));
            w = (w + 1) & mask;
        }
    }
private:
    static double tapHz(double hz, int i) {
        static const float mult[4] = { 1.0f, 0.7f, 1.3f, 1.0f };
        const float v = (float)hz * mult[i] * 100.0f;
        return std::max(0.01, (double)(int)v / 100.0);
    }
    struct Tap { double lo, hi, rate, phase0; };
    static constexpr Tap kTap[4] = {
        { 0.3335, 1.6670, 1.0012, 0.25 },   // L
        { 0.3424, 2.3962, 0.6984, 0.75 },   // L
        { 0.1420, 0.9942, 1.2991, 0.25 },   // R
        { 0.3336, 1.6669, 1.0014, 0.75 } }; // R
    std::vector<float> buf[2]; size_t mask = 0, w = 0; double fs = 44100.0; double ph[4] = {}, inc[4] = {};
    inline double read(int ch, double d) const {
        double rp = (double)w - d; while (rp < 0) rp += (double)buf[ch].size();
        size_t i = (size_t)rp; double f = rp - (double)i;
        float a = buf[ch][i & mask], b = buf[ch][(i + 1) & mask];
        return a + (b - a) * f;
    }
};

} // namespace AlphaOrigFX
