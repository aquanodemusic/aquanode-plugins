#include "PianoModelCore.h"

using namespace aquanode;
namespace piano = aquanode::piano;
using cd = std::complex<double>;

//==============================================================================
struct alignas (32) PianoModel::Strike
{
    enum class Phase { Idle, Contact, Ring };
    Phase phase { Phase::Idle };

    int note { 60 }, S { 1 }, M { 0 };
    double baseSemis { 60.0 }, f1 { 0.0 }, v0 { 1.0 }, hammerMass { 3e-3 };

    // --- contact: hammer + lumped string -----------------------------------
    double yh { 0 }, vh { 0 }, K { 0 }, p { 2.5 }, h { 0 };
    int step { 0 }, minSteps { 0 }, maxSteps { 0 };
    double lastD { 0 }, lastF { 0 };
    double q[kMaxPartials], v[kMaxPartials];
    double cH[kMaxPartials], sH[kMaxPartials];     // exact step at the oversampled rate
    double c1[kMaxPartials], s1[kMaxPartials];     // ... and at the sample rate (passive modes)
    double wd[kMaxPartials], sig[kMaxPartials], phi[kMaxPartials], g[kMaxPartials];
    double WL[kMaxPartials], WR[kMaxPartials];     // lumped bridge+radiation weights

    // --- per partial, for the coupled unisons ------------------------------
    double w[kMaxPartials];
    double wbL[kMaxPartials][kMaxStrings], wbR[kMaxPartials][kMaxStrings];
    double eps[kMaxPartials][kMaxStrings];
    double cents[kMaxStrings];
    double after { 0.3 };
    int eigenNext { 0 };
    cd lam[kMaxPartials][kMaxStrings], pL[kMaxPartials][kMaxStrings], pR[kMaxPartials][kMaxStrings];

    // --- body modes (complex state, Im = displacement-like) -----------------
    double bzr[kBodyComps], bzi[kBodyComps];
    double bhr[kBodyComps], bhi[kBodyComps];       // per oversampled step
    double bSigma[kBodyComps], bOmega[kBodyComps];
    double bmr[kBodyComps], bmi[kBodyComps];       // per sample (with damper)
    double bgL[kBodyComps], bgR[kBodyComps], bDamp[kBodyComps];

    // --- ringing: damped complex oscillators --------------------------------
    int Kc { 0 };
    alignas (32) float sr[kMaxComps], si[kMaxComps], mr[kMaxComps], mi[kMaxComps];
    alignas (32) float cLr[kMaxComps], cLi[kMaxComps], cRr[kMaxComps], cRi[kMaxComps];
    float sigma[kMaxComps], omega[kMaxComps], damp[kMaxComps];
    double peak { 0 };

    // --- the knock: band-passed noise, L and R independent ------------------
    double knockAmp { 0 }, knockMul { 0 };
    double kb0 { 0 }, kb2 { 0 }, ka1 { 0 }, ka2 { 0 };
    double kzL[2] {}, kzR[2] {};
    double klp { 0 }, klpL[2] {}, klpR[2] {};      // a two-pole low-pass after it
    uint32_t noise { 1 };
    double clickAmp { 0 }, clickMul { 0 };
    double cb0 { 0 }, cb2 { 0 }, ca1 { 0 }, ca2 { 0 };
    double czL[2] {}, czR[2] {};

    // --- release / retune --------------------------------------------------
    bool released { false }, ghost { false };
    int relSamples { 0 };
    float dampFrac { 0 }, appliedFrac { -1 };
    double pitchFactor { 1.0 }, appliedPitch { -1.0 };
    bool appliedGhost { false };
    int ctrl { 0 }, cull { 0 };

    // a fresh blow: only what the setup does not overwrite needs clearing
    // (the struct is ~45 kB, too big to rebuild on the audio thread's stack)
    void clearForStrike()
    {
        phase = Phase::Idle;
        yh = vh = 0.0;
        step = 0;
        lastD = lastF = 0.0;
        eigenNext = 0;
        Kc = 0;
        peak = 0.0;
        released = ghost = appliedGhost = false;
        relSamples = 0;
        dampFrac = 0.0f;
        appliedFrac = -1.0f;
        pitchFactor = 1.0;
        appliedPitch = -1.0;
        ctrl = cull = 0;
        std::fill (std::begin (q), std::end (q), 0.0);
        std::fill (std::begin (v), std::end (v), 0.0);
    }
};

namespace
{
    // the coupled-unison eigenproblem of one partial:  M = D - kappa * ones,
    // D = diag(d_s). Its eigenvalues are the roots of the monic
    //   Q(l) = prod (l - d_s) + kappa * sum_s prod_{t != s} (l - d_t)
    // and v_k,s = 1 / (d_s - l_k). Durand-Kerner from d_s - kappa.
    void solveUnisons (const cd* d, int S, double kappa, cd* lam)
    {
        auto Q = [&] (cd l)
        {
            cd prodAll (1.0, 0.0), sum (0.0, 0.0);
            for (int s = 0; s < S; ++s)
            {
                prodAll *= (l - d[s]);
                cd pr (1.0, 0.0);
                for (int t = 0; t < S; ++t)
                    if (t != s) pr *= (l - d[t]);
                sum += pr;
            }
            return prodAll + kappa * sum;
        };

        for (int s = 0; s < S; ++s)
            lam[s] = d[s] - kappa + cd (1.0e-7 * (s + 1), 1.0e-7 * (s + 1));

        for (int it = 0; it < 80; ++it)
        {
            double worst = 0.0;
            for (int i = 0; i < S; ++i)
            {
                cd den (1.0, 0.0);
                for (int j = 0; j < S; ++j)
                    if (j != i) den *= (lam[i] - lam[j]);
                if (std::abs (den) < 1.0e-300)
                    den = cd (1.0e-300, 0.0);
                const cd delta = Q (lam[i]) / den;
                lam[i] -= delta;
                worst = std::max (worst, std::abs (delta) / std::max (1.0, std::abs (lam[i])));
            }
            if (worst < 1.0e-14)
                break;
        }
    }

    // small dense complex inverse (S <= 3), Gauss-Jordan with pivoting
    bool invert (cd a[3][3], cd inv[3][3], int n)
    {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                inv[i][j] = (i == j) ? cd (1.0, 0.0) : cd (0.0, 0.0);

        for (int col = 0; col < n; ++col)
        {
            int piv = col;
            for (int r = col + 1; r < n; ++r)
                if (std::abs (a[r][col]) > std::abs (a[piv][col])) piv = r;
            if (std::abs (a[piv][col]) < 1.0e-300)
                return false;
            if (piv != col)
                for (int j = 0; j < n; ++j) { std::swap (a[piv][j], a[col][j]); std::swap (inv[piv][j], inv[col][j]); }
            const cd pv = a[col][col];
            for (int j = 0; j < n; ++j) { a[col][j] /= pv; inv[col][j] /= pv; }
            for (int r = 0; r < n; ++r)
                if (r != col)
                {
                    const cd f = a[r][col];
                    for (int j = 0; j < n; ++j) { a[r][j] -= f * a[col][j]; inv[r][j] -= f * inv[col][j]; }
                }
        }
        return true;
    }

    // prepares partial n's eigenvalues and output projections (pL, pR) so that
    // the hand-over from contact to ringing is a multiply per component
    void prepareUnisons (PianoModel::Strike& st, int n)
    {
        const int S = st.S;
        const double sigP = st.sig[n];
        if (S == 1)
        {
            st.lam[n][0] = cd (-sigP, st.w[n]);
            st.pL[n][0] = cd (st.wbL[n][0], 0.0);
            st.pR[n][0] = cd (st.wbR[n][0], 0.0);
            return;
        }

        const double sigA = st.after * sigP;
        const double kappa = (sigP - sigA) / S;
        cd d[3];
        for (int s = 0; s < S; ++s)
            d[s] = cd (-sigA, st.w[n] * std::pow (2.0, st.cents[s] / 1200.0));

        cd lam[3];
        solveUnisons (d, S, kappa, lam);

        cd V[3][3], Vi[3][3];
        for (int s = 0; s < S; ++s)
            for (int k = 0; k < S; ++k)
                V[s][k] = 1.0 / (d[s] - lam[k]);

        // projections before V is destroyed by the inversion
        cd wvL[3], wvR[3];
        for (int k = 0; k < S; ++k)
        {
            wvL[k] = wvR[k] = cd (0.0, 0.0);
            for (int s = 0; s < S; ++s)
            {
                wvL[k] += st.wbL[n][s] * V[s][k];
                wvR[k] += st.wbR[n][s] * V[s][k];
            }
        }

        if (! invert (V, Vi, S))
        {
            // degenerate (should not happen with the minimum detune): plain strings
            for (int s = 0; s < S; ++s)
            {
                st.lam[n][s] = d[s] - kappa;
                st.pL[n][s] = cd (st.wbL[n][s] * st.eps[n][s], 0.0);
                st.pR[n][s] = cd (st.wbR[n][s] * st.eps[n][s], 0.0);
            }
            return;
        }

        for (int k = 0; k < S; ++k)
        {
            cd u (0.0, 0.0);
            for (int s = 0; s < S; ++s)
                u += Vi[k][s] * st.eps[n][s];     // strike imbalance between the unisons
            st.lam[n][k] = lam[k];
            st.pL[n][k] = u * wvL[k];
            st.pR[n][k] = u * wvR[k];
        }
    }
}

//==============================================================================
PianoModel::PianoModel (const piano::Voicing& v)
    : voicing (v), strikes (new Strike[(size_t) kMaxVoices * 2])
{
}

PianoModel::~PianoModel() = default;

//==============================================================================
// the voicing tables: given at seven anchor keys, interpolated in between
// (linearly, or in log for lengths and densities)
double PianoModel::ip (double x, const double (&v)[7]) const
{
    const auto& a = voicing.anchors;
    if (x <= a[0]) return v[0];
    if (x >= a[6]) return v[6];
    int i = 0;
    while (x > a[i + 1]) ++i;
    const double t = (x - a[i]) / (a[i + 1] - a[i]);
    return v[i] + t * (v[i + 1] - v[i]);
}

double PianoModel::ipl (double x, const double (&v)[7]) const
{
    double lv[7];
    for (int i = 0; i < 7; ++i) lv[i] = std::log (v[i]);
    return std::exp (ip (x, lv));
}

double PianoModel::eqGainDb (double f) const
{
    const auto& eqF = voicing.eqF;
    const auto& eqDb = voicing.eqDb;
    const double lf = std::log (juce::jlimit (eqF[0], eqF[7], f));
    int i = 0;
    while (i < 6 && lf > std::log (eqF[i + 1])) ++i;
    const double l0 = std::log (eqF[i]), l1 = std::log (eqF[i + 1]);
    return eqDb[i] + (lf - l0) / (l1 - l0) * (eqDb[i + 1] - eqDb[i]);
}

void PianoModel::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void PianoModel::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
}

void PianoModel::voiceReset (int v)
{
    pool.resetVoice (v);
    strikes[(size_t) v * 2].phase = Strike::Phase::Idle;
    strikes[(size_t) v * 2 + 1].phase = Strike::Phase::Idle;
    voices[v].pendingNote = -1;
}

void PianoModel::voiceNoteOn (int v, int note, bool)
{
    pool.noteOn (v, voiceLimit());
    auto& vi = voices[v];
    auto& cur = strikes[(size_t) v * 2 + (size_t) vi.slot];

    // a string still ringing in this voice is let go (fast damping) while the
    // new blow lands on the other slot
    if (cur.phase == Strike::Phase::Ring)
    {
        cur.ghost = true;
        vi.slot = 1 - vi.slot;
    }
    else if (cur.phase == Strike::Phase::Contact)
        cur.phase = Strike::Phase::Idle;     // re-struck within milliseconds

    auto& next = strikes[(size_t) v * 2 + (size_t) vi.slot];
    next.phase = Strike::Phase::Idle;
    vi.pendingNote = note;
}

void PianoModel::voiceVelocity (int v, float velocity01)
{
    voices[v].velocity = juce::jlimit (0.0f, 1.0f, velocity01);
}

void PianoModel::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    auto& vi = voices[v];
    auto& st = strikes[(size_t) v * 2 + (size_t) vi.slot];
    if (st.phase == Strike::Phase::Idle || st.released)
        return;
    st.released = true;
    st.relSamples = 0;

    // the damper felt landing knocks the case a little
    const double damperScale = st.note > voicing.damperTop ? 0.0 : param (pDamper) * 0.01;
    if (damperScale > 0.01)
    {
        const double impulse = 0.035 * st.hammerMass * damperScale;
        for (int b = 0; b < kNumBodyModes; ++b)
            st.bzr[b] += impulse;
    }
}

double PianoModel::voiceTailSeconds() const
{
    const double d = param (pDamper) * 0.01;
    if (d < 0.02)
        return 20.0;
    const double damped = 0.15 + voicing.damperRamp + 1.3 * 6.91 / (voicing.damperSigma0 * d);
    // undamped top keys ring on after release; their short strings die
    // within a few seconds
    return juce::jmin (20.0, voicing.damperTop < 127 ? juce::jmax (damped, 4.0) : damped);
}

//==============================================================================
void PianoModel::startStrike (Strike& st, int note, float velocity, double semis)
{
    const double SR = sampleRate;
    const double x = semis;           // the voicing follows the pitch the note starts at

    const double hardness = param (pHardness) * 0.01 - 0.5;        // -0.5 .. 0.5
    const double strikeScale = param (pStrike) * 0.01;
    const double bright = param (pBright) * 0.01;                  // -1 .. 1
    const double width = param (pWidth) * 0.01;
    const double decayScale = juce::jmax (0.05, (double) param (pDecay) * 0.01);
    const double dampingScale = param (pDamping) * 0.01;
    const double unisonScale = param (pUnison) * 0.01;
    const double inharmScale = param (pInharm) * 0.01;
    const double bodyScale = param (pBody) * 0.01;
    const double resonanceScale = param (pResonance) * 0.01;
    static const int partialChoices[5] = { 24, 40, 64, 96, 128 };
    const int maxPartials = partialChoices[juce::jlimit (0, 4, (int) std::lround (param (pPartials)))];

    st.clearForStrike();
    st.note = note;
    st.baseSemis = semis;
    st.after = juce::jlimit (0.05, 0.95, (double) param (pAfter) * 0.01);

    const double fEt = 440.0 * std::pow (2.0, (x - 69.0) / 12.0);
    st.f1 = fEt * std::pow (2.0, ip (x, voicing.stretchCents) / 1200.0);
    const double L = ipl (x, voicing.length);
    const double mu = ipl (x, voicing.density);
    const double T = mu * (2.0 * L * st.f1) * (2.0 * L * st.f1);
    const int key = juce::roundToInt (x);
    st.S = key <= voicing.oneStringTop ? 1 : (key <= voicing.twoStringTop ? 2 : 3);

    for (int s = 0; s < st.S; ++s)
    {
        const double c = st.S == 1 ? 0.0 : (st.S == 2 ? voicing.detune2[s] : voicing.detune3[s]);
        // a hair of detune always stays, so the unison modes never coincide
        st.cents[s] = c * unisonScale + (st.S > 1 ? (s - 0.5 * (st.S - 1)) * 0.015 : 0.0);
    }

    const double beta = juce::jlimit (0.03, 0.45, ip (x, voicing.beta) * strikeScale);
    st.hammerMass = ip (x, voicing.hammerMass);
    st.K = std::exp (ip (x, voicing.logK) + hardness * 3.0 + bright * voicing.brightLogK);
    st.p = voicing.hertzP + hardness * 0.6;
    const double sigma0 = ip (x, voicing.sigma0) / decayScale;
    const double Bc = ip (x, voicing.B) * inharmScale;
    const double regGain = std::pow (10.0, (ip (x, voicing.regDb) + voicing.outputDb) / 20.0);

    st.h = 1.0 / (SR * kOversample);
    const double fMax = std::min (16000.0, 0.46 * SR);
    const double Mn = mu * L / 2.0;

    juce::Random rng (1000 + note);
    const double pan = juce::jlimit (-1.0, 1.0, (x - 60.0) / 36.0) * voicing.panRange * (width / 0.8);
    const double quarter = juce::MathConstants<double>::pi / 4.0;

    int M = 0;
    for (int n = 1; n <= maxPartials; ++n)
    {
        const double f = n * st.f1 * std::sqrt (1.0 + Bc * n * n);
        if (f >= fMax)
            break;
        const double sigP = sigma0 + voicing.sigmaF * dampingScale * (f / 2000.0) * (f / 2000.0);
        const double w = juce::MathConstants<double>::twoPi * f;
        const double wdn = std::sqrt (std::max (1.0, w * w - sigP * sigP));
        const int i = M;

        st.w[i] = w;
        st.sig[i] = sigP;
        st.wd[i] = wdn;
        {
            const double e = std::exp (-sigP * st.h);
            st.cH[i] = e * std::cos (wdn * st.h);
            st.sH[i] = e * std::sin (wdn * st.h);
            const double e1 = std::exp (-sigP / SR);
            st.c1[i] = e1 * std::cos (wdn / SR);
            st.s1[i] = e1 * std::sin (wdn / SR);
        }
        st.phi[i] = std::sin (n * juce::MathConstants<double>::pi * beta);
        st.g[i] = st.phi[i] / (Mn * w * w);

        // bridge force (string slope at the bridge) through the radiation
        // curve; Brightness opens (or closes) its top and tilts it a little
        const double octAbove = std::max (0.0, std::log2 (f / voicing.brightFromHz));
        const double eqDb = eqGainDb (f)
                          + bright * std::min (voicing.brightOpenCapDb, voicing.brightOpenDbPerOct * octAbove)
                          + bright * 1.5 * std::log2 (std::max (20.0, f) / 500.0);
        const double wb = T * n * juce::MathConstants<double>::pi / L * std::pow (10.0, eqDb / 20.0) * regGain;

        st.WL[i] = st.WR[i] = 0.0;
        for (int s = 0; s < st.S; ++s)
        {
            // every (partial, string) radiates from a slightly different spot
            const double th = quarter * (1.0 + pan) + quarter * width * (rng.nextDouble() * 2.0 - 1.0);
            st.wbL[i][s] = wb * std::cos (th);
            st.wbR[i][s] = wb * std::sin (th);
            st.eps[i][s] = 1.0 + (st.S > 1 ? 0.12 * (rng.nextDouble() * 2.0 - 1.0) : 0.0);
            // the lumped string is heard through the same imbalance the
            // unisons get afterwards, so the hand-over is seamless
            st.WL[i] += st.wbL[i][s] * st.eps[i][s];
            st.WR[i] += st.wbR[i][s] * st.eps[i][s];
        }
        ++M;
    }
    st.M = M;

    // body: a short thump and a slow sympathetic bed at the same frequencies
    for (int set = 0; set < 2; ++set)
    {
        const double t60 = set == 0 ? voicing.bodyT60 : voicing.bodySlowT60;
        const double gset = set == 0 ? 1.0 : voicing.bodySlowGain * resonanceScale;
        for (int b = 0; b < kNumBodyModes; ++b)
        {
            const int k = set * kNumBodyModes + b;
            const double bs = 6.91 / t60;
            const double bw = juce::MathConstants<double>::twoPi * voicing.bodyF[b];
            const double bwd = std::sqrt (bw * bw - bs * bs);
            st.bSigma[k] = bs;
            st.bOmega[k] = bwd;
            const double e = std::exp (-bs * st.h);
            st.bhr[k] = e * std::cos (bwd * st.h);
            st.bhi[k] = e * std::sin (bwd * st.h);
            const double e1 = std::exp (-bs / SR);
            st.bmr[k] = e1 * std::cos (bwd / SR);
            st.bmi[k] = e1 * std::sin (bwd / SR);
            const double th = set == 0 ? quarter + quarter * 0.9 * (rng.nextDouble() * 2.0 - 1.0)
                                       : quarter + quarter * 0.9 * (rng.nextDouble() * 2.0 - 1.0);
            const double G = voicing.bodyGain[b] * voicing.bodyLevel * bodyScale * 2000.0 * gset / bwd;
            st.bgL[k] = G * std::cos (th);
            st.bgR[k] = G * std::sin (th);
            st.bDamp[k] = voicing.damperSigma0 + voicing.damperSigmaF * voicing.bodyF[b] / 1000.0;
            st.bzr[k] = st.bzi[k] = 0.0;
        }
    }

    // the hammer is thrown
    st.v0 = 0.4 * std::pow (12.0, (double) velocity);
    st.yh = 0.0;
    st.vh = st.v0;
    st.minSteps = ((int) (std::max (0.012, 1.3 / st.f1) * SR * kOversample) / kOversample + 1) * kOversample;
    st.maxSteps = (int) (0.12 * SR * kOversample);
    st.step = 0;
    st.eigenNext = 0;
    // the knock of the blow and the click of the felt, louder with a faster hammer
    st.knockAmp = 0.0;
    st.clickAmp = 0.0;
    st.noise = 0x9e3779b9u ^ (uint32_t) (note * 7919 + 17);
    if (voicing.clickLevel > 0.0)
    {
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (200.0, 0.45 * SR, voicing.clickHz) / SR;
        const double alpha = std::sin (w0) / (2.0 * 0.7);
        const double a0 = 1.0 + alpha;
        st.cb0 = alpha / a0;
        st.cb2 = -alpha / a0;
        st.ca1 = -2.0 * std::cos (w0) / a0;
        st.ca2 = (1.0 - alpha) / a0;
        st.czL[0] = st.czL[1] = st.czR[0] = st.czR[1] = 0.0;
        st.clickAmp = voicing.clickLevel * std::pow (st.v0 / 2.28, 1.6) * std::pow (10.0, voicing.outputDb / 20.0);
        st.clickMul = std::exp (-1.0 / (juce::jmax (0.001, voicing.clickDecay) * SR));
    }
    if (voicing.knockLevel > 0.0)
    {
        const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (20.0, 0.45 * SR, voicing.knockHz) / SR;
        const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.1, voicing.knockQ));
        const double a0 = 1.0 + alpha;
        st.kb0 = alpha / a0;
        st.kb2 = -alpha / a0;
        st.ka1 = -2.0 * std::cos (w0) / a0;
        st.ka2 = (1.0 - alpha) / a0;
        st.kzL[0] = st.kzL[1] = st.kzR[0] = st.kzR[1] = 0.0;
        st.klp = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (0.45 * SR, 2.0 * voicing.knockHz) / SR);
        st.klpL[0] = st.klpL[1] = st.klpR[0] = st.klpR[1] = 0.0;
        st.knockAmp = voicing.knockLevel * std::pow (st.v0 / 2.28, 1.3) * std::pow (10.0, voicing.outputDb / 20.0);
        st.knockMul = std::exp (-1.0 / (juce::jmax (0.002, voicing.knockDecay) * SR));
    }

    st.phase = Strike::Phase::Contact;
}

//==============================================================================
void PianoModel::renderStrike (Strike& st, double semis, float& outL, float& outR)
{
    double L = 0.0, R = 0.0;

    if (st.phase == Strike::Phase::Contact)
    {
        const int M = st.M;
        const int A = std::min (M, kActiveModes);

        // what is heard while the hammer is on the string: the modes' motion
        // without their static give under the force - that part only exists
        // where the hammer presses and reaches the bridge as the string's
        // waves, not at once (heard directly it was a loud click at every blow)
        for (int n = 0; n < M; ++n)
        {
            const double dyn = st.q[n] - st.lastF * st.g[n];
            L += st.WL[n] * dyn;
            R += st.WR[n] * dyn;
        }

        // the far modes barely move the contact point: they ride along once a
        // sample on the averaged force
        double ysFar = 0.0, vsFar = 0.0;
        for (int n = A; n < M; ++n)
        {
            ysFar += st.phi[n] * st.q[n];
            vsFar += st.phi[n] * st.v[n];
        }

        double forceSum = 0.0;
        for (int sub = 0; sub < kOversample; ++sub)
        {
            double ys = ysFar, vs = vsFar;
            for (int n = 0; n < A; ++n)
            {
                ys += st.phi[n] * st.q[n];
                vs += st.phi[n] * st.v[n];
            }
            const double d = st.yh - ys;
            double F = 0.0;
            if (d > 0.0)
                F = std::max (0.0, st.K * std::pow (d, st.p) * (1.0 + voicing.hcAlpha * (st.vh - vs)));
            st.lastD = d;

            // hammer (it drives S strings at once)
            st.vh -= (st.S * F / st.hammerMass) * st.h;
            st.yh += st.vh * st.h;

            // near modes: exact step under a constant force
            for (int n = 0; n < A; ++n)
            {
                const double qeq = F * st.g[n];
                const double z = st.q[n] - qeq;
                const double vz = st.v[n] + st.sig[n] * z;
                const double qn = qeq + st.cH[n] * z + st.sH[n] * vz / st.wd[n];
                const double vn = st.cH[n] * st.v[n] - st.sH[n] * (st.wd[n] * z + st.sig[n] * vz / st.wd[n]);
                st.q[n] = qn;
                st.v[n] = vn;
            }

            // the case hears the blow
            for (int k = 0; k < kBodyComps; ++k)
            {
                const double zr = st.bzr[k] * st.bhr[k] - st.bzi[k] * st.bhi[k] + F * st.h;
                const double zi = st.bzr[k] * st.bhi[k] + st.bzi[k] * st.bhr[k];
                st.bzr[k] = zr;
                st.bzi[k] = zi;
            }
            forceSum += F;
            st.lastF = F;
            ++st.step;
        }

        const double Favg = forceSum / kOversample;
        for (int n = A; n < M; ++n)
        {
            const double qeq = Favg * st.g[n];
            const double z = st.q[n] - qeq;
            const double vz = st.v[n] + st.sig[n] * z;
            const double qn = qeq + st.c1[n] * z + st.s1[n] * vz / st.wd[n];
            const double vn = st.c1[n] * st.v[n] - st.s1[n] * (st.wd[n] * z + st.sig[n] * vz / st.wd[n]);
            st.q[n] = qn;
            st.v[n] = vn;
        }

        // meanwhile, a partial's unison eigenproblem per sample
        if (st.eigenNext < M)
            prepareUnisons (st, st.eigenNext++);

        for (int k = 0; k < kBodyComps; ++k)
        {
            L += st.bzi[k] * st.bgL[k];
            R += st.bzi[k] * st.bgR[k];
        }

        const bool hammerGone = st.lastD <= 0.0 && st.step >= st.minSteps && st.vh <= 0.0;
        if (hammerGone || st.step >= st.maxSteps)
        {
            while (st.eigenNext < M)
                prepareUnisons (st, st.eigenNext++);

            // hand over: complex modal amplitude of the lumped string at the
            // end of contact, spread onto the coupled unison modes
            int Kc = 0;
            double peak = 0.0;
            for (int n = 0; n < M; ++n)
            {
                const cd C (st.q[n], -(st.v[n] + st.sig[n] * st.q[n]) / st.wd[n]);
                for (int k = 0; k < st.S; ++k)
                {
                    const cd cl = C * st.pL[n][k], cr = C * st.pR[n][k];
                    st.cLr[Kc] = (float) cl.real(); st.cLi[Kc] = (float) cl.imag();
                    st.cRr[Kc] = (float) cr.real(); st.cRi[Kc] = (float) cr.imag();
                    st.sr[Kc] = 1.0f; st.si[Kc] = 0.0f;
                    st.sigma[Kc] = (float) -st.lam[n][k].real();
                    st.omega[Kc] = (float) st.lam[n][k].imag();
                    st.damp[Kc] = (float) (voicing.damperSigma0 + voicing.damperSigmaF
                                           * std::abs (st.lam[n][k].imag()) / juce::MathConstants<double>::twoPi / 1000.0);
                    peak = std::max (peak, std::abs (cl) + std::abs (cr));
                    ++Kc;
                }
            }

            // drop what nobody will hear (-120 dB re the loudest component)
            const double floorMag = peak * 1.0e-6;
            int k = 0;
            while (k < Kc)
            {
                const double mag = std::hypot (st.cLr[k], st.cLi[k]) + std::hypot (st.cRr[k], st.cRi[k]);
                if (mag < floorMag)
                {
                    --Kc;
                    st.sr[k] = st.sr[Kc]; st.si[k] = st.si[Kc];
                    st.cLr[k] = st.cLr[Kc]; st.cLi[k] = st.cLi[Kc]; st.cRr[k] = st.cRr[Kc]; st.cRi[k] = st.cRi[Kc];
                    st.sigma[k] = st.sigma[Kc]; st.omega[k] = st.omega[Kc]; st.damp[k] = st.damp[Kc];
                }
                else
                    ++k;
            }
            // pad to the SIMD width with silent components
            int padded = (Kc + 7) & ~7;
            for (int j = Kc; j < padded; ++j)
            {
                st.sr[j] = st.si[j] = 0.0f;
                st.cLr[j] = st.cLi[j] = st.cRr[j] = st.cRi[j] = 0.0f;
                st.mr[j] = st.mi[j] = 0.0f;
                st.sigma[j] = st.omega[j] = st.damp[j] = 0.0f;
            }
            st.Kc = padded;
            st.peak = peak;
            st.appliedPitch = -1.0;     // multipliers get built on the next control tick
            st.appliedFrac = -1.0f;
            st.ctrl = 0;
            st.phase = Strike::Phase::Ring;
        }
    }
    else if (st.phase == Strike::Phase::Ring)
    {
        // --- control, every 16 samples: retune, damper, ghosting, culling
        if (--st.ctrl < 0)
        {
            st.ctrl = 15;
            const double SR = sampleRate;
            const double pf = std::pow (2.0, (semis - st.baseSemis) / 12.0);
            const double damperScale = st.note > voicing.damperTop ? 0.0 : param (pDamper) * 0.01;

            if (st.released && damperScale > 0.0)
            {
                // mean of the ramp over this block
                const double t0 = st.relSamples / SR, t1 = (st.relSamples + 16) / SR;
                const double r0 = std::min (1.0, t0 / voicing.damperRamp), r1 = std::min (1.0, t1 / voicing.damperRamp);
                st.dampFrac = (float) (0.5 * (r0 + r1) * damperScale);
            }
            else
                st.dampFrac = 0.0f;
            if (st.released)
                st.relSamples += 16;

            const bool pitchChanged = std::abs (pf - st.appliedPitch) > 1.0e-6;
            const bool dampChanged = std::abs (st.dampFrac - st.appliedFrac) > 1.0e-6f || st.ghost != st.appliedGhost;
            if (pitchChanged || dampChanged)
            {
                const double ghostRate = st.ghost ? 60.0 : 0.0;
                for (int k = 0; k < st.Kc; ++k)
                {
                    const double mag = std::exp (-((double) st.sigma[k] + st.dampFrac * (double) st.damp[k] + ghostRate) / SR);
                    const double ph = (double) st.omega[k] * pf / SR;
                    st.mr[k] = (float) (mag * std::cos (ph));
                    st.mi[k] = (float) (mag * std::sin (ph));
                }
                for (int k = 0; k < kBodyComps; ++k)
                {
                    const double mag = std::exp (-(st.bSigma[k] + st.dampFrac * st.bDamp[k] + ghostRate) / SR);
                    st.bmr[k] = mag * std::cos (st.bOmega[k] / SR);
                    st.bmi[k] = mag * std::sin (st.bOmega[k] / SR);
                }
                st.appliedPitch = pf;
                st.appliedFrac = st.dampFrac;
                st.appliedGhost = st.ghost;
            }

            // every 1024 samples: let go of components that have died away
            if (++st.cull >= 64)
            {
                st.cull = 0;
                const double floorSq = (st.peak * 3.0e-6) * (st.peak * 3.0e-6);
                int live = 0;
                for (int k = 0; k < st.Kc; ++k)
                {
                    const double s2 = (double) st.sr[k] * st.sr[k] + (double) st.si[k] * st.si[k];
                    const double c2 = (double) st.cLr[k] * st.cLr[k] + (double) st.cLi[k] * st.cLi[k]
                                    + (double) st.cRr[k] * st.cRr[k] + (double) st.cRi[k] * st.cRi[k];
                    if (s2 * c2 >= floorSq)
                    {
                        if (live != k)
                        {
                            st.sr[live] = st.sr[k]; st.si[live] = st.si[k];
                            st.mr[live] = st.mr[k]; st.mi[live] = st.mi[k];
                            st.cLr[live] = st.cLr[k]; st.cLi[live] = st.cLi[k];
                            st.cRr[live] = st.cRr[k]; st.cRi[live] = st.cRi[k];
                            st.sigma[live] = st.sigma[k]; st.omega[live] = st.omega[k]; st.damp[live] = st.damp[k];
                        }
                        ++live;
                    }
                }
                const int padded = (live + 7) & ~7;
                for (int j = live; j < padded; ++j)
                {
                    st.sr[j] = st.si[j] = st.mr[j] = st.mi[j] = 0.0f;
                    st.cLr[j] = st.cLi[j] = st.cRr[j] = st.cRi[j] = 0.0f;
                    st.sigma[j] = st.omega[j] = st.damp[j] = 0.0f;
                }
                st.Kc = padded;

                double bodyE = 0.0;
                for (int k = 0; k < kBodyComps; ++k)
                    bodyE += (st.bzr[k] * st.bzr[k] + st.bzi[k] * st.bzi[k])
                           * (st.bgL[k] * st.bgL[k] + st.bgR[k] * st.bgR[k]);
                if (st.Kc == 0 && bodyE < floorSq && st.knockAmp < 1.0e-7)
                {
                    st.phase = Strike::Phase::Idle;
                    outL = outR = 0.0f;
                    return;
                }
            }
        }

        // --- the oscillator bank
        using V = juce::dsp::SIMDRegister<float>;
        constexpr int W = (int) V::size();
        V accL = V::expand (0.0f), accR = V::expand (0.0f);
        for (int k = 0; k < st.Kc; k += W)
        {
            const V sr = V::fromRawArray (st.sr + k), si = V::fromRawArray (st.si + k);
            const V mr = V::fromRawArray (st.mr + k), mi = V::fromRawArray (st.mi + k);
            accL += V::fromRawArray (st.cLr + k) * sr - V::fromRawArray (st.cLi + k) * si;
            accR += V::fromRawArray (st.cRr + k) * sr - V::fromRawArray (st.cRi + k) * si;
            (sr * mr - si * mi).copyToRawArray (st.sr + k);
            (sr * mi + si * mr).copyToRawArray (st.si + k);
        }
        L = accL.sum();
        R = accR.sum();

        for (int k = 0; k < kBodyComps; ++k)
        {
            L += st.bzi[k] * st.bgL[k];
            R += st.bzi[k] * st.bgR[k];
            const double zr = st.bzr[k] * st.bmr[k] - st.bzi[k] * st.bmi[k];
            const double zi = st.bzr[k] * st.bmi[k] + st.bzi[k] * st.bmr[k];
            st.bzr[k] = zr;
            st.bzi[k] = zi;
        }
    }

    auto white = [&st]
    {
        st.noise = st.noise * 1664525u + 1013904223u;
        return (double) (int32_t) st.noise * (1.0 / 2147483648.0);
    };

    if (st.clickAmp > 1.0e-7)
    {
        const double inL = white(), inR = white();
        const double yL = st.cb0 * inL + st.czL[0];
        st.czL[0] = -st.ca1 * yL + st.czL[1];
        st.czL[1] = st.cb2 * inL - st.ca2 * yL;
        const double yR = st.cb0 * inR + st.czR[0];
        st.czR[0] = -st.ca1 * yR + st.czR[1];
        st.czR[1] = st.cb2 * inR - st.ca2 * yR;
        L += st.clickAmp * yL;
        R += st.clickAmp * yR;
        st.clickAmp *= st.clickMul;
    }

    if (st.knockAmp > 1.0e-7)
    {
        // transposed direct form II band-pass, one per channel
        const double inL = white(), inR = white();
        const double yL = st.kb0 * inL + st.kzL[0];
        st.kzL[0] = -st.ka1 * yL + st.kzL[1];
        st.kzL[1] = st.kb2 * inL - st.ka2 * yL;
        const double yR = st.kb0 * inR + st.kzR[0];
        st.kzR[0] = -st.ka1 * yR + st.kzR[1];
        st.kzR[1] = st.kb2 * inR - st.ka2 * yR;
        st.klpL[0] += st.klp * (yL - st.klpL[0]);
        st.klpL[1] += st.klp * (st.klpL[0] - st.klpL[1]);
        st.klpR[0] += st.klp * (yR - st.klpR[0]);
        st.klpR[1] += st.klp * (st.klpR[0] - st.klpR[1]);
        L += st.knockAmp * st.klpL[1];
        R += st.knockAmp * st.klpR[1];
        st.knockAmp *= st.knockMul;
    }

    outL = (float) L;
    outR = (float) R;
}

//==============================================================================
void PianoModel::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    outputs[0][0] = outputs[0][1] = 0.0f;
    if (pool.isSilent (v))
        return;

    auto& vi = voices[v];
    const double offset = (double) inputs[0][0] * 60.0 + (double) pitchBendSemitones();

    if (vi.pendingNote >= 0)
    {
        startStrike (strikes[(size_t) v * 2 + (size_t) vi.slot], vi.pendingNote, vi.velocity, vi.pendingNote + offset);
        vi.pendingNote = -1;
    }

    float sumL = 0.0f, sumR = 0.0f;
    for (int s = 0; s < 2; ++s)
    {
        auto& st = strikes[(size_t) v * 2 + (size_t) s];
        if (st.phase == Strike::Phase::Idle)
            continue;
        float l = 0.0f, r = 0.0f;
        // a note bends relative to the pitch it was struck at
        renderStrike (st, st.note + offset, l, r);
        sumL += l;
        sumR += r;
    }

    // Env In patched: the envelope shapes the piano on top of its own physics
    const float env = isInputConnected (1) ? inputs[1][0] : 1.0f;
    const float gain = param (pVolume) * voiceGain * env;
    outputs[0][0] = sumL * gain;
    outputs[0][1] = sumR * gain;
}

//==============================================================================
ModuleDescriptor PianoModel::makeDescriptor (const juce::String& typeId, const juce::String& name,
                                              const juce::String& description, int sidebarOrder,
                                              float defaultAfter, int defaultPartials)
{
    ModuleDescriptor d;
    d.typeId = typeId;
    d.displayName = name;
    d.description = description;
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = sidebarOrder;
    d.sockets = {
        modIn    ("pitchIn",   "Pitch In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",     0.0f, 2.0f, 1.0f, 0),
        makeRotary ("hardness", "Hardness",   0.0f, 100.0f, 50.0f, 0, "%", false, 1.0f),
        makeRotary ("strike",   "Strike",     40.0f, 250.0f, 100.0f, 0, "%", false, 1.0f),
        makeRotary ("bright",   "Brightness", -100.0f, 100.0f, 0.0f, 0, "%", false, 1.0f),
        makeRotary ("width",    "Width",      0.0f, 100.0f, 80.0f, 0, "%", false, 1.0f),

        makeRotary ("decay",    "Decay",      25.0f, 400.0f, 100.0f, 1, "%", true, 1.0f),
        makeRotary ("damping",  "Damping",    0.0f, 300.0f, 100.0f, 1, "%", false, 1.0f),
        makeRotary ("after",    "Aftersound", 5.0f, 95.0f, defaultAfter, 1, "%", false, 1.0f),
        makeRotary ("unison",   "Unison",     0.0f, 400.0f, 100.0f, 1, "%", false, 1.0f),
        makeRotary ("inharm",   "Inharm",     0.0f, 400.0f, 100.0f, 1, "%", false, 1.0f),

        makeRotary ("body",      "Body",      0.0f, 300.0f, 100.0f, 2, "%", false, 1.0f),
        makeRotary ("resonance", "Sympathy",  0.0f, 400.0f, 100.0f, 2, "%", false, 1.0f),
        makeRotary ("damper",    "Damper",    0.0f, 200.0f, 100.0f, 2, "%", false, 1.0f),
        makeSteppedList ("partials", "Partials", { "24", "40", "64", "96", "128" }, defaultPartials, 2).noMod(),
        makeRotary ("voices",   "Voices",     1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod()
    };
    return d;
}

juce::String PianoModel::controlsDescription()
{
    return " Hardness is the felt, Brightness opens or closes the top (and hardens or softens the felt with it), "
           "Strike where the hammer hits, Inharm the string stiffness, Unison the detune of the 2-3 strings per "
           "note (beats and the long after-sound), Decay / Damping / Aftersound the losses, Body / Sympathy the "
           "case and its slow sympathetic ring, Damper how the felt dampers stop a released note (0 = they stay "
           "up). Env In takes an ADSR like every generator; unpatched, the piano rings by itself. Partials trades "
           "detail for CPU.";
}
