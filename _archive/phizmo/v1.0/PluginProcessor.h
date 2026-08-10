#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
//==============================================================================
//  PHIZMO EFFECTS
//
//  The Phizmo has TWO effect processors:
//    1. a GLOBAL REVERB with 8 variations, and
//    2. an INSERT EFFECT chosen from 41 algorithms (chorus, flanger, DDL,
//       distortion, tunable speaker, chatter box, vocal morph, auto-wah,
//       vocoder, EQ and combinations), each with its own set of variations,
//       plus a live wet/dry Mix knob.
//
//  The reverb here is a port of the AquaReverb FDN from the aquanode-modular
//  project (8 modulated feedback-delay lines, input diffusion allpasses,
//  per-line damping, Householder feedback matrix) — a much better fit for the
//  the hardware's lush 24-bit ambience than a plain Freeverb.
//==============================================================================

namespace phz
{

//------------------------------------------------------------------ utilities
struct DelayLine
{
    std::vector<float> buf;
    int pos = 0, size = 0;

    void prepare(int maxSamples)
    {
        size = juce::jmax(8, maxSamples);
        buf.assign((size_t)size, 0.f);
        pos = 0;
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); pos = 0; }

    inline void write(float v)
    {
        if (size <= 0) return;
        buf[(size_t)pos] = v;
        if (++pos >= size) pos = 0;
    }
    // fractional read, `d` samples back
    inline float read(double d) const
    {
        if (size <= 0) return 0.f;
        double rp = (double)pos - d;
        while (rp < 0.0) rp += size;
        while (rp >= size) rp -= size;
        const int i0 = (int)rp;
        const int i1 = (i0 + 1) % size;
        const float fr = (float)(rp - std::floor(rp));
        return buf[(size_t)i0] * (1.f - fr) + buf[(size_t)i1] * fr;
    }
};

struct AllpassFx
{
    std::vector<float> buf;
    int pos = 0;
    void assign(int n) { buf.assign((size_t)juce::jmax(4, n), 0.f); pos = 0; }
    void clear() { std::fill(buf.begin(), buf.end(), 0.f); pos = 0; }
    inline float process(float in, float coeff)
    {
        if (buf.empty()) return in;
        const float d = buf[(size_t)pos];
        const float y = -coeff * in + d;
        buf[(size_t)pos] = in + coeff * y;
        if (++pos >= (int)buf.size()) pos = 0;
        return y;
    }
};

// one-pole allpass used by the phaser
struct Allpass1
{
    float z = 0.f;
    inline float process(float in, float a)
    {
        const float y = a * in + z;
        z = in - a * y;
        return y;
    }
    void clear() { z = 0.f; }
};

// simple state-variable biquad (bandpass / peak / lowpass) for formants & EQ
struct BiquadFx
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void clear() { x1 = x2 = y1 = y2 = 0.f; }

    void setBandpass(float f, float q, double sr)
    {
        f = juce::jlimit(20.f, (float)sr * 0.45f, f);
        const float w = 2.f * juce::MathConstants<float>::pi * f / (float)sr;
        const float cw = std::cos(w), al = std::sin(w) / (2.f * juce::jmax(0.05f, q));
        const float a0 = 1.f + al;
        b0 = al / a0; b1 = 0.f; b2 = -al / a0;
        a1 = -2.f * cw / a0; a2 = (1.f - al) / a0;
    }
    void setPeak(float f, float q, float gainDb, double sr)
    {
        f = juce::jlimit(20.f, (float)sr * 0.45f, f);
        const float A = std::pow(10.f, gainDb / 40.f);
        const float w = 2.f * juce::MathConstants<float>::pi * f / (float)sr;
        const float cw = std::cos(w), al = std::sin(w) / (2.f * juce::jmax(0.05f, q));
        const float a0 = 1.f + al / A;
        b0 = (1.f + al * A) / a0; b1 = -2.f * cw / a0; b2 = (1.f - al * A) / a0;
        a1 = -2.f * cw / a0; a2 = (1.f - al / A) / a0;
    }
    void setLowpass(float f, float q, double sr)
    {
        f = juce::jlimit(20.f, (float)sr * 0.45f, f);
        const float w = 2.f * juce::MathConstants<float>::pi * f / (float)sr;
        const float cw = std::cos(w), al = std::sin(w) / (2.f * juce::jmax(0.05f, q));
        const float a0 = 1.f + al;
        b0 = (1.f - cw) * 0.5f / a0; b1 = (1.f - cw) / a0; b2 = b0;
        a1 = -2.f * cw / a0; a2 = (1.f - al) / a0;
    }
    void setHighpass(float f, float q, double sr)
    {
        f = juce::jlimit(20.f, (float)sr * 0.45f, f);
        const float w = 2.f * juce::MathConstants<float>::pi * f / (float)sr;
        const float cw = std::cos(w), al = std::sin(w) / (2.f * juce::jmax(0.05f, q));
        const float a0 = 1.f + al;
        b0 = (1.f + cw) * 0.5f / a0; b1 = -(1.f + cw) / a0; b2 = b0;
        a1 = -2.f * cw / a0; a2 = (1.f - al) / a0;
    }
    inline float process(float in)
    {
        const float y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in; y2 = y1; y1 = y;
        return y;
    }
};

//==============================================================================
//  GLOBAL REVERB — 8 variations (ported FDN)
//==============================================================================
class PhizmoReverb
{
public:
    static constexpr int numLines = 8;
    static constexpr int numDiff  = 4;

    void prepare(double sr)
    {
        sampleRate = sr;
        maxLine = (int)(sr * 0.0797 * 8.0) + 512;
        for (int l = 0; l < numLines; ++l) lines[l].assign((size_t)maxLine, 0.f);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < numDiff; ++i)
                diff[c][i].assign((int)(kDiffMs[i] * 0.001 * sr) + 1);
        reset();
    }

    void reset()
    {
        for (int l = 0; l < numLines; ++l)
        {
            std::fill(lines[l].begin(), lines[l].end(), 0.f);
            wpos[l] = 0; lp[l] = 0.f;
            lfo[l] = (double)l / numLines;
        }
        for (int c = 0; c < 2; ++c) for (auto& a : diff[c]) a.clear();
    }

    // The the hardware's eight global reverb variations.
    static const char* variationName(int v)
    {
        // The the hardware's eight global reverb variations (r01-r08 in the manual)
        static const char* n[8] = { "Smooth Plate","Large Hall","Small Hall","Big Room",
                                    "Small Room","Reflections","Bright","Huge Place" };
        return n[juce::jlimit(0, 7, v)];
    }

    void setVariation(int v)
    {
        v = juce::jlimit(0, 7, v);
        //                       size   feedback damp  modRate modDepth
        static const float P[8][5] = {
            { 0.95f, 0.86f, 0.16f, 0.55f, 3.0f },   // r01 Smooth Plate (bright, fast mod)
            { 2.20f, 0.90f, 0.30f, 0.22f, 6.0f },   // r02 Large Hall
            { 1.45f, 0.84f, 0.38f, 0.26f, 4.5f },   // r03 Small Hall
            { 1.10f, 0.80f, 0.44f, 0.30f, 3.5f },   // r04 Big Room
            { 0.55f, 0.66f, 0.55f, 0.35f, 2.0f },   // r05 Small Room
            { 0.40f, 0.35f, 0.30f, 0.10f, 1.0f },   // r06 Reflections (short, sparse)
            { 1.30f, 0.85f, 0.06f, 0.40f, 4.0f },   // r07 Bright (very low damping)
            { 4.20f, 0.95f, 0.22f, 0.15f, 9.0f },   // r08 Huge Place
        };
        tSize = P[v][0]; tFeedback = P[v][1]; tDamping = P[v][2];
        tModRate = P[v][3]; tModDepth = P[v][4];
    }

    // direct parameter access, used by the insert-effect reverbs
    void setParams(float sz, float fb, float dmp, float mr, float md)
    { tSize = sz; tFeedback = fb; tDamping = dmp; tModRate = mr; tModDepth = md;
      // insert-effect reverbs want their settings immediately, not ramped
      size = sz; feedback = fb; damping = dmp; modRate = mr; modDepth = md; }

    // amount = wet level 0..1
    void process(float& inL, float& inR, float amount)
    {
        if (maxLine < 512 || amount <= 0.0001f) return;

        // Ramp the reverb coefficients toward their targets. Switching variation
        // or turning a reverb knob used to change size/feedback in one step,
        // which jumps the delay-line read position and crackles; a one-pole
        // glide over ~20 ms removes that without smearing the change audibly.
        const float sm = 1.f - std::exp(-1.f / (0.02f * (float)sampleRate));
        size     += sm * (tSize     - size);
        feedback += sm * (tFeedback - feedback);
        damping  += sm * (tDamping  - damping);
        modRate  += sm * (tModRate  - modRate);
        modDepth += sm * (tModDepth - modDepth);

        const float fb  = juce::jlimit(0.f, 0.98f, feedback);
        const float lpC = juce::jlimit(0.05f, 1.f, 1.f - damping * 0.9f);

        float dL = inL, dR = inR;
        for (int i = 0; i < numDiff; ++i)
        {
            dL = diff[0][i].process(dL, 0.62f);
            dR = diff[1][i].process(dR, 0.62f);
        }

        float lineOut[numLines], sum = 0.f;
        for (int l = 0; l < numLines; ++l)
        {
            lfo[l] += modRate / sampleRate;
            lfo[l] -= std::floor(lfo[l]);
            const double m = std::sin(lfo[l] * juce::MathConstants<double>::twoPi);
            const double dly = juce::jlimit(16.0, (double)maxLine - 4.0,
                kLineMs[l] * 0.001 * sampleRate * size + m * modDepth);
            lineOut[l] = readLine(l, dly);
            sum += lineOut[l];
        }

        const float h = 2.f / (float)numLines;
        for (int l = 0; l < numLines; ++l)
        {
            lp[l] += lpC * ((lineOut[l] - h * sum) - lp[l]);
            const float mixed = lp[l] * fb;
            const float inject = (l & 1) ? dR : dL;
            lines[l][(size_t)wpos[l]] = juce::jlimit(-4.f, 4.f, mixed + inject * 0.5f);
            wpos[l] = (wpos[l] + 1) % maxLine;
        }

        float wL = 0.f, wR = 0.f;
        for (int l = 0; l < numLines; ++l) { if (l & 1) wL += lineOut[l]; else wR += lineOut[l]; }
        wL *= 0.4f; wR *= 0.4f;

        inL = inL * (1.f - amount) + wL * amount;
        inR = inR * (1.f - amount) + wR * amount;
    }

private:
    float readLine(int l, double d) const
    {
        double rp = (double)wpos[l] - d;
        while (rp < 0.0) rp += maxLine;
        const int i0 = (int)rp % maxLine;
        const int i1 = (i0 + 1) % maxLine;
        const float fr = (float)(rp - std::floor(rp));
        return lines[l][(size_t)i0] * (1.f - fr) + lines[l][(size_t)i1] * fr;
    }

    static constexpr double kLineMs[numLines] =
        { 29.7, 37.1, 41.1, 43.7, 53.3, 61.9, 71.3, 79.7 };
    static constexpr double kDiffMs[numDiff] = { 4.7, 3.6, 12.7, 9.3 };

    std::vector<float> lines[numLines];
    int   wpos[numLines] {};
    float lp[numLines] {};
    double lfo[numLines] {};
    AllpassFx diff[2][numDiff];
    int    maxLine = 0;
    double sampleRate = 44100.0;

    float size = 1.f, feedback = 0.8f, damping = 0.4f, modRate = 0.25f, modDepth = 4.f;
    // Smoothing targets: setVariation()/setParams() write these, and process()
    // ramps the live values above toward them so a change never steps the
    // delay-line read position (which crackled).
    float tSize = 1.f, tFeedback = 0.8f, tDamping = 0.4f, tModRate = 0.25f, tModDepth = 4.f;
};

//==============================================================================
//  INSERT EFFECT — all 41 Phizmo algorithms
//
//  The list and ordering follow the original User's Guide (Chapter 5, "Choosing
//  an Insert Effect"). Most algorithms are CHAINS of a small set of building
//  blocks, which is why 41 algorithms need only about a dozen DSP primitives.
//  ">" in a name means "into": "DDL > Chorus" is delay feeding chorus.
//==============================================================================
class PhizmoInsert
{
public:
    enum Algo
    {
        ParametricEQ = 0, HallReverb, LargeRoom, SmallRoom, LargePlate, SmallPlate,
        NonLinear1, NonLinear2, GatedReverb, StereoChorus, Chorus8Voice,
        RevChorus, RevFlanger, RevPhaser, ChorusRev, FlangerRev, PhaserRev,
        EQRev, SpinnerRev, DDLChorus, DDLFlanger, DDLPhaser, DDLEQ, MultiTapDDL,
        DistChorus, DistFlanger, DistPhaser, DistAutowah, ResVCFDDL, DistVCFDDL,
        PitchDetune, ChatterBox, FormantMorph, RotarySpeaker, TunableSpeaker,
        GuitarAmp, DistDDLTrem, CompDDLTrem, EQCompGate, EQChoDDL, Vocoder,
        NumAlgos
    };

    static const char* algoName(int a)
    {
        static const char* n[NumAlgos] = {
            "Parametric EQ","Hall Reverb","Large Room","Small Room","Large Plate",
            "Small Plate","Non Linear Rev 1","Non Linear Rev 2","Gated Reverb",
            "Stereo Chorus","8 Voice Chorus","Reverb > Chorus","Reverb > Flanger",
            "Reverb > Phaser","Chorus > Reverb","Flanger > Reverb","Phaser > Reverb",
            "EQ > Reverb","Spinner > Reverb","DDL > Chorus","DDL > Flanger",
            "DDL > Phaser","DDL > EQ","Multi-tap DDL","Distortion > Chorus",
            "Distortion > Flanger","Distortion > Phaser","Distortion > Autowah",
            "ResVCF > DDL","Dist > VCF > DDL","Pitch Detune","Chatter Box",
            "Formant Morph","Rotary Speaker","Tunable Speaker","Guitar Amp",
            "Dist > DDL > Tremolo","Comp > DDL > Tremolo","EQ > Comp > Gate",
            "EQ Cho DDL","Vocoder"
        };
        return n[juce::jlimit(0, (int)NumAlgos - 1, a)];
    }

    // Per-algorithm output trim. Every algorithm has a different intrinsic
    // gain: the distortions and the resonant/feedback chains run far hotter
    // than the plain reverbs and choruses. These factors bring all 41 wet
    // paths to within a few dB of each other so switching algorithms does not
    // jump the level.
    static float algoTrim(int a)
    {
        static const float t[NumAlgos] = {
            0.90f,  // Parametric EQ
            0.85f, 0.85f, 0.90f, 0.85f, 0.90f,   // Hall, LgRoom, SmRoom, LgPlate, SmPlate
            0.85f, 0.80f, 0.90f,                 // NonLin 1, NonLin 2, Gated
            0.90f, 0.80f,                        // Chorus, 8-voice Chorus
            0.80f, 0.80f, 0.85f,                 // Rev>Cho, Rev>Flg, Rev>Pha
            0.80f, 0.80f, 0.85f,                 // Cho>Rev, Flg>Rev, Pha>Rev
            0.80f, 0.80f,                        // EQ>Rev, Spinner>Rev
            0.75f, 0.75f, 0.80f, 0.85f, 0.70f,   // DDL>Cho, DDL>Flg, DDL>Pha, DDL>EQ, MultiTap
            0.45f, 0.45f, 0.50f, 0.45f,          // Dist>Cho, Dist>Flg, Dist>Pha, Dist>Autowah
            0.55f, 0.35f,                        // ResVCF>DDL, Dist>VCF>DDL
            0.85f, 0.70f, 0.75f,                 // PitchDetune, ChatterBox, FormantMorph
            0.85f, 0.85f,                        // Rotary, Tunable Speaker
            0.50f,                               // Guitar Amp
            0.40f, 0.70f,                        // Dist>DDL>Trem, Comp>DDL>Trem
            0.75f, 0.70f,                        // EQ>Comp>Gate, EQ Cho DDL
            0.80f                                // Vocoder
        };
        return t[juce::jlimit(0, (int)NumAlgos - 1, a)];
    }

    // the hardware's 4-character display abbreviations
    static const char* algoShort(int a)
    {
        static const char* n[NumAlgos] = {
            "Para","HALL","Lroo","Sroo","LPLt","SPlt","nLr1","nLr2","GatE","Cho",
            "8Cho","rCho","rFLG","rPHS","Chor","FLGr","PHSr","Eqr","SPin","dCho",
            "dFLG","dPha","dEq","taP","dStC","dStF","dStP","dCry","rEsd","drES",
            "Pitc","CHat","ForF","roTS","tunE","Gutr","trLo","cdSt","qCGt","qChd","Codr"
        };
        return n[juce::jlimit(0, (int)NumAlgos - 1, a)];
    }

    void prepare(double sr)
    {
        sampleRate = sr;
        const int maxD = (int)(sr * 1.2) + 8;
        dl[0].prepare(maxD); dl[1].prepare(maxD);
        md[0].prepare((int)(sr * 0.05) + 8);
        md[1].prepare((int)(sr * 0.05) + 8);
        pd[0].prepare((int)(sr * 0.1) + 8);
        pd[1].prepare((int)(sr * 0.1) + 8);
        rev.prepare(sr);
        reset();
    }

    void reset()
    {
        dl[0].clear(); dl[1].clear(); md[0].clear(); md[1].clear();
        pd[0].clear(); pd[1].clear();
        for (auto& c : ap[0]) c.clear();
        for (auto& c : ap[1]) c.clear();
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 3; ++i) fmt[c][i].clear();
        eqL.clear(); eqR.clear(); lpL.clear(); lpR.clear(); hpL.clear(); hpR.clear();
        vcfL.clear(); vcfR.clear();
        rev.reset();
        phase = phase2 = phase3 = 0.0; pdPhase = 0.0;
        env = envFast = compEnv = gateEnv = 0.f;
        chatHold = 0.f; chatGate = 1.f; chatCount = 0;
        fbL = fbR = 0.f;
    }

    void process(float& L, float& R, int algo, float variation, float mix, float fMacro)
    {
        if (mix <= 0.0001f) return;
        const float dryL = L, dryR = R;
        float wL = L, wR = R;
        const float v = juce::jlimit(0.f, 1.f, variation + fMacro * 0.5f);

        switch (juce::jlimit(0, (int)NumAlgos - 1, algo))
        {
        case ParametricEQ:   doEQ(wL, wR, v); break;
        case HallReverb:     doReverb(wL, wR, v, 0); break;
        case LargeRoom:      doReverb(wL, wR, v, 1); break;
        case SmallRoom:      doReverb(wL, wR, v, 2); break;
        case LargePlate:     doReverb(wL, wR, v, 3); break;
        case SmallPlate:     doReverb(wL, wR, v, 4); break;
        case NonLinear1:     doReverb(wL, wR, v, 5); break;
        case NonLinear2:     doReverb(wL, wR, v, 6); break;
        case GatedReverb:    doReverb(wL, wR, v, 7); break;
        case StereoChorus:   doChorus(wL, wR, v); break;
        case Chorus8Voice:   doChorus8(wL, wR, v); break;

        // reverb FIRST, then modulation
        case RevChorus:      doReverb(wL, wR, v, 0); doChorus(wL, wR, v);  break;
        case RevFlanger:     doReverb(wL, wR, v, 0); doFlanger(wL, wR, v); break;
        case RevPhaser:      doReverb(wL, wR, v, 0); doPhaser(wL, wR, v);  break;
        // modulation FIRST, then reverb
        case ChorusRev:      doChorus(wL, wR, v);  doReverb(wL, wR, v, 0); break;
        case FlangerRev:     doFlanger(wL, wR, v); doReverb(wL, wR, v, 0); break;
        case PhaserRev:      doPhaser(wL, wR, v);  doReverb(wL, wR, v, 0); break;
        case EQRev:          doEQ(wL, wR, v);      doReverb(wL, wR, v, 0); break;
        case SpinnerRev:     doSpinner(wL, wR, v); doReverb(wL, wR, v, 0); break;

        case DDLChorus:      doDDL(wL, wR, v); doChorus(wL, wR, v);  break;
        case DDLFlanger:     doDDL(wL, wR, v); doFlanger(wL, wR, v); break;
        case DDLPhaser:      doDDL(wL, wR, v); doPhaser(wL, wR, v);  break;
        case DDLEQ:          doDDL(wL, wR, v); doEQ(wL, wR, v);      break;
        case MultiTapDDL:    doMultiTap(wL, wR, v); break;

        case DistChorus:     doDistortion(wL, wR, v); doChorus(wL, wR, v);  break;
        case DistFlanger:    doDistortion(wL, wR, v); doFlanger(wL, wR, v); break;
        case DistPhaser:     doDistortion(wL, wR, v); doPhaser(wL, wR, v);  break;
        case DistAutowah:    doDistortion(wL, wR, v); doAutoWah(wL, wR, v); break;
        case ResVCFDDL:      doResVCF(wL, wR, v); doDDL(wL, wR, v * 0.7f);  break;
        case DistVCFDDL:     doDistortion(wL, wR, v); doResVCF(wL, wR, v);
                             doDDL(wL, wR, v * 0.7f); break;

        case PitchDetune:    doPitchDetune(wL, wR, v); break;
        case ChatterBox:     doChatterBox(wL, wR, v); break;
        case FormantMorph:   doFormantMorph(wL, wR, v); break;
        case RotarySpeaker:  doRotary(wL, wR, v); break;
        case TunableSpeaker: doTunableSpeaker(wL, wR, v); break;
        case GuitarAmp:      doGuitarAmp(wL, wR, v); break;

        case DistDDLTrem:    doDistortion(wL, wR, v); doDDL(wL, wR, v * 0.6f);
                             doTremolo(wL, wR, v); break;
        case CompDDLTrem:    doCompressor(wL, wR, v); doDDL(wL, wR, v * 0.6f);
                             doTremolo(wL, wR, v); break;
        case EQCompGate:     doEQ(wL, wR, v); doCompressor(wL, wR, v);
                             doGate(wL, wR, v); break;
        case EQChoDDL:       doEQ(wL, wR, v); doChorus(wL, wR, v);
                             doDDL(wL, wR, v * 0.6f); break;
        case Vocoder:        doVocoder(wL, wR, v); break;
        default: break;
        }

        // Level-match the wet path, then catch any residual peak with a gentle
        // soft clip so no algorithm can hand back something that overloads the
        // summing stage downstream.
        const float trim = algoTrim(algo);
        wL *= trim; wR *= trim;
        auto soft = [](float x)
        {
            return (x > 1.f || x < -1.f) ? (x / (1.f + std::abs(x) - 1.f)) : x;
        };
        wL = soft(wL); wR = soft(wR);

        L = dryL * (1.f - mix) + wL * mix;
        R = dryR * (1.f - mix) + wR * mix;
    }

private:
    inline double advance(double& ph, float hz)
    {
        ph += hz / sampleRate;
        ph -= std::floor(ph);
        return ph;
    }

    //---------------------------------------------------------------- reverbs
    // type: 0 Hall, 1 LargeRoom, 2 SmallRoom, 3 LargePlate, 4 SmallPlate,
    //       5 NonLinear1, 6 NonLinear2, 7 Gated
    void doReverb(float& L, float& R, float v, int type)
    {
        //                     size   fb     damp   modR  modD
        static const float P[8][5] = {
            { 2.30f, 0.90f, 0.30f, 0.22f, 6.0f },   // Hall
            { 1.25f, 0.84f, 0.40f, 0.28f, 4.0f },   // Large Room
            { 0.60f, 0.70f, 0.52f, 0.34f, 2.0f },   // Small Room
            { 1.40f, 0.88f, 0.10f, 0.50f, 4.0f },   // Large Plate
            { 0.80f, 0.80f, 0.14f, 0.55f, 3.0f },   // Small Plate
            { 1.10f, 0.92f, 0.20f, 0.20f, 3.0f },   // Non Linear 1
            { 1.60f, 0.94f, 0.16f, 0.18f, 4.0f },   // Non Linear 2
            { 1.00f, 0.90f, 0.30f, 0.25f, 3.0f },   // Gated
        };
        const int t = juce::jlimit(0, 7, type);
        // variation scales the decay/size within the algorithm
        rev.setParams(P[t][0] * (0.6f + v * 0.9f),
                      juce::jlimit(0.f, 0.97f, P[t][1] * (0.85f + v * 0.2f)),
                      P[t][2], P[t][3], P[t][4]);
        float rl = L, rr = R;
        rev.process(rl, rr, 1.0f);          // fully wet tail

        if (t >= 5)
        {
            // Non-linear and gated reverbs shape the tail with an envelope
            // derived from the input, so the tail cuts off abruptly.
            const float rect = (std::abs(L) + std::abs(R)) * 0.5f;
            envFast += (rect > envFast ? 0.02f : 0.00012f) * (rect - envFast);
            if (t == 7)
            {
                // Gated: hard cut once the input envelope falls below threshold
                const float thr = 0.004f + v * 0.02f;
                gateEnv += ((envFast > thr ? 1.f : 0.f) - gateEnv) * 0.02f;
                rl *= gateEnv; rr *= gateEnv;
            }
            else
            {
                // Non-linear: level plateau that decays in steps rather than smoothly
                const float shape = (t == 5) ? 0.55f : 0.75f;
                const float g = juce::jlimit(0.f, 1.f, envFast * (18.f + v * 40.f));
                const float stepped = std::floor(g * 6.f) / 6.f;
                rl *= (shape + (1.f - shape) * stepped);
                rr *= (shape + (1.f - shape) * stepped);
            }
        }
        L = rl; R = rr;
    }

    //------------------------------------------------------------- modulation
    void doChorus(float& L, float& R, float v)
    {
        const float rate  = 0.15f + v * 1.6f;
        const float depth = (2.f + v * 8.f);
        const double p = advance(phase, rate);
        const float mL = (float)std::sin(p * juce::MathConstants<double>::twoPi);
        const float mR = (float)std::sin((p + 0.25) * juce::MathConstants<double>::twoPi);
        md[0].write(L); md[1].write(R);
        const double base = 0.012 * sampleRate;
        L = 0.5f * L + 0.7f * md[0].read(base + mL * depth * 0.001 * sampleRate);
        R = 0.5f * R + 0.7f * md[1].read(base + mR * depth * 0.001 * sampleRate);
    }

    // Eight taps at evenly spread LFO phases = the classic thick 8-voice chorus
    void doChorus8(float& L, float& R, float v)
    {
        const float rate  = 0.10f + v * 0.9f;
        const float depth = (2.5f + v * 7.f) * 0.001f * (float)sampleRate;
        const double p = advance(phase, rate);
        md[0].write(L); md[1].write(R);
        const double base = 0.014 * sampleRate;
        float sL = 0.f, sR = 0.f;
        for (int i = 0; i < 8; ++i)
        {
            const double ph = p + (double)i / 8.0;
            const float m = (float)std::sin(ph * juce::MathConstants<double>::twoPi);
            if (i & 1) sR += md[1].read(base + m * depth);
            else       sL += md[0].read(base + m * depth);
        }
        L = 0.45f * L + 0.32f * sL;
        R = 0.45f * R + 0.32f * sR;
    }

    void doFlanger(float& L, float& R, float v)
    {
        const float rate = 0.05f + v * 0.9f;
        const float fb   = 0.55f + v * 0.35f;
        const double p = advance(phase, rate);
        const float mL = (float)std::sin(p * juce::MathConstants<double>::twoPi);
        const float mR = (float)std::sin((p + 0.15) * juce::MathConstants<double>::twoPi);
        const double base = 0.0012 * sampleRate, sw = 0.0055 * sampleRate;
        md[0].write(L + fbL * fb);
        md[1].write(R + fbR * fb);
        const float oL = md[0].read(base + (mL * 0.5f + 0.5f) * sw);
        const float oR = md[1].read(base + (mR * 0.5f + 0.5f) * sw);
        fbL = oL; fbR = oR;
        L = 0.6f * L + 0.7f * oL;
        R = 0.6f * R + 0.7f * oR;
    }

    void doPhaser(float& L, float& R, float v)
    {
        const float rate = 0.08f + v * 1.2f;
        const double p = advance(phase, rate);
        const float lv = (float)std::sin(p * juce::MathConstants<double>::twoPi) * 0.5f + 0.5f;
        const float f = 220.f + lv * (1800.f + v * 3000.f);
        const float tn = std::tan(juce::MathConstants<float>::pi * f / (float)sampleRate);
        const float a = (1.f - tn) / (1.f + tn);
        const float fbAmt = 0.3f + v * 0.5f;
        float xL = L + fbL * fbAmt, xR = R + fbR * fbAmt;
        for (int i = 0; i < 6; ++i) { xL = ap[0][i].process(xL, a); xR = ap[1][i].process(xR, a); }
        fbL = xL; fbR = xR;
        L = 0.6f * L + 0.7f * xL;
        R = 0.6f * R + 0.7f * xR;
    }

    //------------------------------------------------------------------ delay
    void doDDL(float& L, float& R, float v)
    {
        const double t  = (0.03 + v * 0.52) * sampleRate;
        const float  fb = 0.15f + v * 0.6f;
        const float dL = dl[0].read(t), dR = dl[1].read(t * 1.02);
        dl[0].write(L + dR * fb);
        dl[1].write(R + dL * fb);
        L += dL * 0.8f;
        R += dR * 0.8f;
    }

    // four taps at rhythmically related times, alternating across the stereo field
    void doMultiTap(float& L, float& R, float v)
    {
        const double base = (0.05 + v * 0.35) * sampleRate;
        static const double frac[4] = { 0.25, 0.5, 0.75, 1.0 };
        static const float  amp[4]  = { 0.85f, 0.65f, 0.5f, 0.38f };
        float tL = 0.f, tR = 0.f;
        for (int i = 0; i < 4; ++i)
        {
            const float s = (i & 1) ? dl[1].read(base * frac[i]) : dl[0].read(base * frac[i]);
            if (i & 1) tR += s * amp[i]; else tL += s * amp[i];
        }
        dl[0].write(L + tR * (0.1f + v * 0.35f));
        dl[1].write(R + tL * (0.1f + v * 0.35f));
        L += tL * 0.7f;
        R += tR * 0.7f;
    }

    //------------------------------------------------------- drive / filter
    // Gain-compensated saturation. tanh(drive) saturates to 1.0 for any drive
    // above ~3, so dividing by it does nothing; 1/sqrt(drive) is the correct
    // equal-loudness compensation for a tanh waveshaper, which keeps the
    // perceived level roughly constant as the drive is swept.
    void doDistortion(float& L, float& R, float v)
    {
        const float drive = 1.5f + v * 30.f;
        const float comp  = 1.f / std::sqrt(drive);
        auto sat = [drive, comp](float x) { return std::tanh(x * drive) * comp; };
        hpL.setHighpass(120.f, 0.7f, sampleRate);
        hpR.setHighpass(120.f, 0.7f, sampleRate);
        lpL.setLowpass(2400.f + (1.f - v) * 6000.f, 0.7f, sampleRate);
        lpR.setLowpass(2400.f + (1.f - v) * 6000.f, 0.7f, sampleRate);
        L = lpL.process(sat(hpL.process(L)));
        R = lpR.process(sat(hpR.process(R)));
    }

    // Guitar amp: asymmetric clipping + speaker cabinet voicing
    void doGuitarAmp(float& L, float& R, float v)
    {
        const float drive = 2.f + v * 40.f;
        const float comp  = 1.f / std::sqrt(drive);
        auto amp = [drive](float x)
        {
            const float y = x * drive;
            return (y >= 0.f ? std::tanh(y) : std::tanh(y * 0.8f) * 0.85f);  // asymmetric
        };
        hpL.setHighpass(90.f, 0.7f, sampleRate);
        hpR.setHighpass(90.f, 0.7f, sampleRate);
        eqL.setPeak(2200.f, 1.6f, 3.f, sampleRate);   // presence bump (was +6 dB)
        eqR.setPeak(2200.f, 1.6f, 3.f, sampleRate);
        lpL.setLowpass(4200.f, 1.1f, sampleRate);     // cabinet roll-off
        lpR.setLowpass(4200.f, 1.1f, sampleRate);
        L = lpL.process(eqL.process(amp(hpL.process(L)))) * comp;
        R = lpR.process(eqR.process(amp(hpR.process(R)))) * comp;
    }

    // Parametric EQ, clamped to +/-6 dB with makeup so the boosted half of the
    // sweep does not run away into the algorithms that chain off it
    // (EQ>Reverb, DDL>EQ, EQ>Comp>Gate, EQ Cho DDL).
    void doEQ(float& L, float& R, float v)
    {
        const float f = 200.f * std::pow(30.f, v);
        const float g = -6.f + v * 12.f;
        const float makeup = (g > 0.f) ? std::pow(10.f, -g * 0.5f / 20.f) : 1.f;
        eqL.setPeak(f, 1.2f, g, sampleRate);
        eqR.setPeak(f, 1.2f, g, sampleRate);
        L = eqL.process(L) * makeup;
        R = eqR.process(R) * makeup;
    }

    // Resonant VCF used as an effect (fixed cutoff set by the variation)
    void doResVCF(float& L, float& R, float v)
    {
        const float f = 150.f * std::pow(40.f, v);
        const float q = 3.f + v * 8.f;
        vcfL.setLowpass(f, q, sampleRate);
        vcfR.setLowpass(f, q, sampleRate);
        L = vcfL.process(L);
        R = vcfR.process(R);
    }

    void doAutoWah(float& L, float& R, float v)
    {
        const float rect = (std::abs(L) + std::abs(R)) * 0.5f;
        env += (rect > env ? 0.005f : 0.0005f) * (rect - env);
        const float f = 200.f + juce::jlimit(0.f, 1.f, env * (4.f + v * 14.f)) * 3200.f;
        const float q = 2.f + v * 6.f;
        eqL.setBandpass(f, q, sampleRate);
        eqR.setBandpass(f, q, sampleRate);
        L = eqL.process(L) * 2.2f;
        R = eqR.process(R) * 2.2f;
    }

    //---------------------------------------------------------- dynamics
    void doCompressor(float& L, float& R, float v)
    {
        const float rect = juce::jmax(std::abs(L), std::abs(R));
        compEnv += (rect > compEnv ? 0.02f : 0.0008f) * (rect - compEnv);
        const float thr   = 0.30f - v * 0.25f;          // lower threshold = more squash
        const float ratio = 2.f + v * 8.f;
        float g = 1.f;
        if (compEnv > thr && compEnv > 1e-6f)
        {
            const float over = compEnv / thr;
            g = std::pow(over, (1.f / ratio) - 1.f);
        }
        const float makeup = 1.f + v * 1.5f;
        L *= g * makeup;
        R *= g * makeup;
    }

    void doGate(float& L, float& R, float v)
    {
        const float rect = juce::jmax(std::abs(L), std::abs(R));
        env += (rect > env ? 0.05f : 0.0006f) * (rect - env);
        const float thr = 0.005f + v * 0.06f;
        const float target = (env > thr) ? 1.f : 0.f;
        gateEnv += (target - gateEnv) * (target > gateEnv ? 0.05f : 0.004f);
        L *= gateEnv;
        R *= gateEnv;
    }

    //--------------------------------------------------------- pitch detune
    // Crossfaded ramping delay lines: a small constant pitch offset, which is
    // what the hardware's Pitch Detune algorithm produces.
    void doPitchDetune(float& L, float& R, float v)
    {
        const float cents = 3.f + v * 22.f;                 // detune spread
        const double ratioUp   = std::pow(2.0,  cents / 1200.0);
        const double ratioDown = std::pow(2.0, -cents / 1200.0);
        const double W = 0.045 * sampleRate;                // window length

        pd[0].write(L); pd[1].write(R);
        pdPhase += (1.0 - ratioUp);
        while (pdPhase <   0.0) pdPhase += W;
        while (pdPhase >=  W  ) pdPhase -= W;
        pdPhase2 += (1.0 - ratioDown);
        while (pdPhase2 <   0.0) pdPhase2 += W;
        while (pdPhase2 >=  W  ) pdPhase2 -= W;

        auto tap = [&](DelayLine& line, double ph)
        {
            const double d1 = ph;
            const double d2 = std::fmod(ph + W * 0.5, W);
            const float  g1 = (float)(1.0 - std::abs(2.0 * d1 / W - 1.0));
            const float  g2 = (float)(1.0 - std::abs(2.0 * d2 / W - 1.0));
            return line.read(d1 + 4.0) * g1 + line.read(d2 + 4.0) * g2;
        };
        const float upL   = tap(pd[0], pdPhase);
        const float downR = tap(pd[1], pdPhase2);
        L = 0.55f * L + 0.75f * upL;      // one side sharp, the other flat
        R = 0.55f * R + 0.75f * downR;
    }

    //------------------------------------------------------------- speakers
    void doTunableSpeaker(float& L, float& R, float v)
    {
        const float f = 120.f * std::pow(20.f, v);
        eqL.setPeak(f, 6.f, 14.f, sampleRate);
        eqR.setPeak(f, 6.f, 14.f, sampleRate);
        lpL.setLowpass(3800.f, 0.9f, sampleRate);
        lpR.setLowpass(3800.f, 0.9f, sampleRate);
        L = lpL.process(std::tanh(eqL.process(L) * 1.6f));
        R = lpR.process(std::tanh(eqR.process(R) * 1.6f));
    }

    void doRotary(float& L, float& R, float v)
    {
        const float hornHz = 0.8f + v * 6.5f;
        const float drumHz = hornHz * 0.28f;
        const double ph = advance(phase, hornHz);
        const double pdr = advance(phase2, drumHz);
        const float sH = (float)std::sin(ph * juce::MathConstants<double>::twoPi);
        const float sD = (float)std::sin(pdr * juce::MathConstants<double>::twoPi);
        md[0].write(L); md[1].write(R);
        const double base = 0.004 * sampleRate, dop = 0.0016 * sampleRate;
        float hL = md[0].read(base + (sH * 0.5f + 0.5f) * dop);
        float hR = md[1].read(base + (-sH * 0.5f + 0.5f) * dop);
        hpL.setHighpass(800.f, 0.7f, sampleRate); hpR.setHighpass(800.f, 0.7f, sampleRate);
        lpL.setLowpass(800.f, 0.7f, sampleRate);  lpR.setLowpass(800.f, 0.7f, sampleRate);
        L = hpL.process(hL) * (0.7f + 0.3f * sH) + lpL.process(L) * (0.8f + 0.2f * sD);
        R = hpR.process(hR) * (0.7f - 0.3f * sH) + lpR.process(R) * (0.8f - 0.2f * sD);
    }

    // Spinner: a faster, more extreme rotary with deeper doppler and panning
    void doSpinner(float& L, float& R, float v)
    {
        const float hz = 2.f + v * 9.f;
        const double ph = advance(phase3, hz);
        const float s = (float)std::sin(ph * juce::MathConstants<double>::twoPi);
        const float c = (float)std::cos(ph * juce::MathConstants<double>::twoPi);
        md[0].write(L); md[1].write(R);
        const double base = 0.003 * sampleRate, dop = 0.0026 * sampleRate;
        float aL = md[0].read(base + (s * 0.5f + 0.5f) * dop);
        float aR = md[1].read(base + (-s * 0.5f + 0.5f) * dop);
        L = aL * (0.6f + 0.4f * c);
        R = aR * (0.6f - 0.4f * c);
    }

    //-------------------------------------------------------------- formants
    static void vowelFormants(float t, float* f)
    {
        static const float V[5][3] = {
            { 730.f, 1090.f, 2440.f },   // A
            { 530.f, 1840.f, 2480.f },   // E
            { 270.f, 2290.f, 3010.f },   // I
            { 570.f,  840.f, 2410.f },   // O
            { 300.f,  870.f, 2240.f },   // U
        };
        t = juce::jlimit(0.f, 0.9999f, t) * 4.f;
        const int i0 = (int)t, i1 = juce::jmin(i0 + 1, 4);
        const float fr = t - (float)i0;
        for (int k = 0; k < 3; ++k) f[k] = V[i0][k] + (V[i1][k] - V[i0][k]) * fr;
    }

    void applyFormants(float& L, float& R, const float* f, float q, float wet)
    {
        float oL = 0.f, oR = 0.f;
        static const float amp[3] = { 1.0f, 0.65f, 0.35f };
        for (int k = 0; k < 3; ++k)
        {
            fmt[0][k].setBandpass(f[k], q, sampleRate);
            fmt[1][k].setBandpass(f[k], q, sampleRate);
            oL += fmt[0][k].process(L) * amp[k];
            oR += fmt[1][k].process(R) * amp[k];
        }
        L = L * (1.f - wet) + oL * wet * 3.0f;
        R = R * (1.f - wet) + oR * wet * 3.0f;
    }

    void doChatterBox(float& L, float& R, float v)
    {
        const int period = (int)(sampleRate / (4.f + v * 20.f));
        if (--chatCount <= 0)
        {
            chatCount = juce::jmax(16, period);
            chatHold = rng.nextFloat();
            chatGate = rng.nextFloat() > 0.25f ? 1.f : 0.15f;
        }
        float f[3]; vowelFormants(chatHold, f);
        applyFormants(L, R, f, 12.f, 1.f);
        L *= chatGate; R *= chatGate;
    }

    void doFormantMorph(float& L, float& R, float v)
    {
        const double p = advance(phase, 0.05f + v * 1.2f);
        const float t = (float)(0.5 + 0.5 * std::sin(p * juce::MathConstants<double>::twoPi));
        float f[3]; vowelFormants(t, f);
        applyFormants(L, R, f, 8.f, 0.9f);
    }

    void doVocoder(float& L, float& R, float v)
    {
        // The hardware vocoder analyses a microphone at the Audio Input jack.
        // A plugin instrument has no such input, so the formant bank is driven
        // by the signal's own envelope instead — same articulation, not a true
        // vocoder.
        const float rect = (std::abs(L) + std::abs(R)) * 0.5f;
        env += (rect > env ? 0.01f : 0.0009f) * (rect - env);
        const float t = juce::jlimit(0.f, 1.f, env * (3.f + v * 10.f));
        float f[3]; vowelFormants(t, f);
        applyFormants(L, R, f, 14.f, 1.f);
    }

    void doTremolo(float& L, float& R, float v)
    {
        const float rate = 0.3f + v * 11.f;
        const double p = advance(phase2, rate);
        const float s = (float)std::sin(p * juce::MathConstants<double>::twoPi);
        if (v < 0.5f) { const float g = 0.5f + 0.5f * s; L *= g; R *= g; }
        else          { L *= (0.5f + 0.5f * s); R *= (0.5f - 0.5f * s); }
    }

    //--------------------------------------------------------------- state
    double sampleRate = 44100.0;
    DelayLine dl[2], md[2], pd[2];
    Allpass1  ap[2][6];
    BiquadFx  fmt[2][3], eqL, eqR, lpL, lpR, hpL, hpR, vcfL, vcfR;
    PhizmoReverb rev;
    double phase = 0.0, phase2 = 0.0, phase3 = 0.0;
    double pdPhase = 0.0, pdPhase2 = 0.0;
    float  env = 0.f, envFast = 0.f, compEnv = 0.f, gateEnv = 0.f;
    float  fbL = 0.f, fbR = 0.f;
    float  chatHold = 0.f, chatGate = 1.f;
    int    chatCount = 0;
    juce::Random rng { 0x51230F };
};

} // namespace phz


//==============================================================================
// Fast 2^x for the per-sample modulation paths. Accurate to about 0.01%
// over the range used here, which is far finer than a cutoff sweep needs,
// and replaces a std::pow call per oscillator per sample.
inline float fastExp2(float x) noexcept
{
    x = juce::jlimit(-30.f, 30.f, x);
    const float xf = std::floor(x);
    const float f  = x - xf;
    // degree-4 minimax polynomial for 2^f on [0,1)
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    union { float f32; juce::int32 i32; } u;
    u.i32 = (juce::int32)((int)xf + 127) << 23;      // 2^floor(x)
    return p * u.f32;
}

static constexpr int EVO_POINTS = 32;

// A Preset holds four Sounds, each with two oscillators. File scope rather
// than class scope so it is complete before any member array that uses it —
// in-class member declarations are not a complete-class context for array
// bounds, so a later declaration would not have been visible above.
static constexpr int NUM_SOUNDS = 4;

//==============================================================================
// Fast inline pseudo-random number generator for block-level synthesis
inline float fastRand()
{
    static unsigned int seed = 123456789;
    seed = (214013 * seed + 2531011);
    return ((seed >> 16) & 0x7FFF) / 32768.f;
}

//==============================================================================
// Structure to hold precomputed block-level coefficients for optimized rendering
struct BlockEnvCoeffs
{
    float decCoeff = 0.0f;
    float relCoeff = 0.0f;
    float dec2Coeff = 0.0f;
    float rel2Coeff = 0.0f;
    float fdecCoeff = 0.0f;
    float frelCoeff = 0.0f;
    float penvDecCoeff = 0.0f;
    float twDecCoeff = 0.0f;
    float twRelCoeff = 0.0f;
    double dm = 1.0;
    double sA = 1.0;
    double sB = 1.0;
    double octA = 1.0;
    double octB = 1.0;
};


//==============================================================================
// A complete Phizmo "Sound" = the full per-Sound parameter snapshot (2 transwave
// oscillators + their pitch/wave/filter/amp/envelope/LFO settings).
//
// A Phizmo Preset holds FOUR Sounds. Only ONE is "in focus" for editing at a
// time; that focused Sound is mirrored by the live APVTS parameters (the edit
// buffer, exactly like the hardware). The other three keep their values here
// and continue to play. Each block the edit buffer is captured back into the
// focused Sound's snapshot.
//==============================================================================
struct SoundParams
{
    float attack = 0.f;
    float attack2 = 0.f;
    float bitCrush = 0.f;
    float decay = 0.f;
    float decay2 = 0.f;
    float detune = 0.f;
    float evoBPhaseOff = 0.f;
    float evoPhaseOff = 0.f;
    float evoSlew = 4.f;
    float evoSlewB = 4.f;
    float evoLFODepth = 0.f;
    float evoLFORate = 0.f;
    float evoTime = 0.f;
    float evoTimeB = 0.f;
    float filterAtt = 0.f;
    float filterDec = 0.f;
    float filterEnvAmt = 0.f;
    float filterFreq = 0.f;
    float filterKeytrack = 0.f;
    float filterLFODep = 0.f;
    float filterQ = 0.f;
    float filterRel = 0.f;
    float filterSus = 0.f;
    float filterType = 0.f;
    float fineA = 0.f;
    float fineB = 0.f;
    float frameInterp = 0.f;
    float frameSnap = 0.f;
    float glide = 0.f;
    float grit = 0.f;
    float jumpProb = 0.f;
    float keytrack = 0.f;
    float levelA = 0.f;
    float levelB = 0.f;
    float evoStepped = 0.f;    // per-oscillator stepped (no interpolation) scan
    float evoSteppedB = 0.f;
    float scanStyleB = 0.f;    // oscillator B scans independently of A
    float lfoShape = 0.f;
    float lfoSpeed = 0.f;
    float lfoSync  = 0.f;      // 0 = free running, 1..12 = note resolution
    float noiseRate = 1.f;     // the noise generator has its own rate
    float noiseSync = 0.f;
    float octaveA = 0.f;
    float octaveB = 0.f;
    float oscMix = 0.f;
    float panA = 0.f;
    float panB = 0.f;
    float pitchEnvAmt = 0.f;
    float pitchEnvDec = 0.f;
    float posLFODepth = 0.f;
    float release = 0.f;
    float release2 = 0.f;
    float ringMod = 0.f;
    float scanStyle = 0.f;
    float spread = 0.f;
    float stereoPhase = 0.f;
    float stereoWidth = 0.f;
    float sustain = 0.f;
    float sustain2 = 0.f;
    float tuneA = 0.f;
    float tuneB = 0.f;
    float twAmt = 0.f;
    float twAtt = 0.f;
    float twDec = 0.f;
    float twRel = 0.f;
    float twSus = 0.f;
    float twToFilter = 0.f;
    float twVelAmt = 0.f;
    float ampVelAmt = 0.6f;   // how much velocity scales amp-envelope loudness
    float uniDetune = 0.f;
    float waveModA = 0.f;
    float waveModB = 0.f;
    float waveStartA = 0.f;
    float waveStartB = 0.f;
    float wtSmooth = 0.f;
    float wtSmoothB = 0.f;
    float oscAOn = 1.f, oscBOn = 1.f;   // OSC 1 / OSC 2 on-off buttons
    // --- Modulation matrix: each destination has a selectable SOURCE and an
    // AMOUNT, per oscillator, exactly like the hardware's Modulation buttons.
    float waveModSrcA = 2.f, waveModSrcB = 2.f;     // default LFO
    float pitchModSrcA = 2.f, pitchModSrcB = 2.f;
    float pitchModAmtA = 0.f, pitchModAmtB = 0.f;
    float filtModSrcA = 2.f, filtModSrcB = 2.f;
    float filtModAmtA = 0.f, filtModAmtB = 0.f;
    float curveA[EVO_POINTS] = { 0.f };
    float curveB[EVO_POINTS] = { 0.f };

    // Layer settings (these live as real APVTS params, mirrored here)
    bool  enabled  = false;
    int   lowKey   = 0;
    int   highKey  = 127;
    float layerGain = 1.f;
};

//==============================================================================
// Two cascaded biquads = 4-pole (24 dB/oct) lowpass
struct StereoBiquad
{
    float x1L = 0, x2L = 0, y1L = 0, y2L = 0, x1R = 0, x2R = 0, y1R = 0, y2R = 0;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    void setLowpass(float freq, float q, double sr)
    {
        float w = 2.f * juce::MathConstants<float>::pi * freq / (float)sr;
        w = juce::jlimit(0.001f, juce::MathConstants<float>::pi * 0.98f, w);
        float cw = std::cos(w), alpha = std::sin(w) / (2.f * q);
        float b0c = (1.f - cw) * 0.5f, b1c = 1.f - cw, b2c = b0c, a0 = 1.f + alpha;
        b0 = b0c / a0; b1 = b1c / a0; b2 = b2c / a0;
        a1 = -2.f * cw / a0; a2 = (1.f - alpha) / a0;
    }
    void setBandpass(float freq, float q, double sr)
    {
        float w = 2.f * juce::MathConstants<float>::pi * freq / (float)sr;
        w = juce::jlimit(0.001f, juce::MathConstants<float>::pi * 0.98f, w);
        float cw = std::cos(w), alpha = std::sin(w) / (2.f * q);
        float a0 = 1.f + alpha;
        b0 = alpha / a0; b1 = 0.f; b2 = -alpha / a0;
        a1 = -2.f * cw / a0; a2 = (1.f - alpha) / a0;
    }
    void setHighpass(float freq, float q, double sr)
    {
        float w = 2.f * juce::MathConstants<float>::pi * freq / (float)sr;
        w = juce::jlimit(0.001f, juce::MathConstants<float>::pi * 0.98f, w);
        float cw = std::cos(w), alpha = std::sin(w) / (2.f * q);
        float b0c = (1.f + cw) * 0.5f, b1c = -(1.f + cw), b2c = b0c, a0 = 1.f + alpha;
        b0 = b0c / a0; b1 = b1c / a0; b2 = b2c / a0;
        a1 = -2.f * cw / a0; a2 = (1.f - alpha) / a0;
    }
    float processL(float in) { float o = b0 * in + b1 * x1L + b2 * x2L - a1 * y1L - a2 * y2L; x2L = x1L;x1L = in;y2L = y1L;y1L = o; return o; }
    float processR(float in) { float o = b0 * in + b1 * x1R + b2 * x2R - a1 * y1R - a2 * y2R; x2R = x1R;x1R = in;y2R = y1R;y1R = o; return o; }
    void reset() { x1L = x2L = y1L = y2L = x1R = x2R = y1R = y2R = 0; }
};

struct StereoFilter4Pole
{
    StereoBiquad s1, s2;
    void setLowpass(float freq, float q, double sr)
    {
        float q1 = q * 0.7071f;
        float q2 = q * 1.3066f;
        s1.setLowpass(freq, q1, sr);
        s2.setLowpass(freq, q2, sr);
    }
    // Coefficient recalculation costs four sin/cos plus divides, so it must not
    // run per sample. setType() now skips the work unless the request actually
    // differs audibly from the last one: a 0.15% cutoff step is far below what
    // can be heard on a sweep but removes almost all of the recalculation.
    float lastFreq = -1.f, lastQ = -1.f;
    int   lastType = -1;

    void setType(int type, float freq, float q, double sr)
    {
        if (type == lastType
            && std::abs(freq - lastFreq) < lastFreq * 0.0015f
            && std::abs(q - lastQ) < 0.002f)
            return;
        lastType = type; lastFreq = freq; lastQ = q;

        switch (type)
        {
        case 1: s1.setBandpass(freq, q, sr); s2.setBandpass(freq, q, sr); break;
        case 2: { float q1 = q * 0.7071f, q2 = q * 1.3066f;
                  s1.setHighpass(freq, q1, sr); s2.setHighpass(freq, q2, sr); } break;
        default: setLowpass(freq, q, sr); break;
        }
    }
    float processL(float in) { return s2.processL(s1.processL(in)); }
    float processR(float in) { return s2.processR(s1.processR(in)); }
    void reset() { s1.reset(); s2.reset(); lastFreq = lastQ = -1.f; lastType = -1; }
};

//==============================================================================
struct PhizmoVoice
{
    int soundIndex = 0;      // which of the 4 Phizmo Sounds this voice plays
    float  sahNoise = 0.f;   // stepped noise  (noiS)
    float  lpfNoise = 0.f;   // smoothed noise (LPF)
    double noisePhase = 0.0;
    bool   active = false;
    int    midiNote = 0;
    float  velocity = 1.0f;

    double phaseA = 0.0;
    double phaseB = 0.0;

    // Per-voice curve scanning state
    double curvePhase = 0.0;
    double curvePhaseB = 0.0;   // oscillator B scans on its own accumulated phase
    int    scanDirB = 1;
    bool   curveFinishedB = false;
    float  curveSmoothA = 0.f, curveSmoothB = 0.f;   // STEP-mode declick slew
    bool   curveWrapA = false, curveWrapB = false;   // brief declick when the scan
                                                     // jumps end-to-end (fwd/bwd)
    int    scanDir = 1;
    bool   curveFinished = false;
    float  evoPhaseCarryStart = 0.0f;

    float  frameOffset = 0.0f;
    float  velFrameOffset = 0.0f;

    // Amplitude envelope
    enum class Env { Idle, Attack, Decay, Sustain, Release } envStage = Env::Idle;
    float envLevel = 0.0f;
    float releaseStartLevel = 0.0f;

    // Filter envelope
    Env   fenvStage = Env::Idle;
    float fenvLevel = 0.0f;
    float fenvReleaseStart = 0.0f;

    // Pitch envelope
    float penvLevel = 0.0f;
    bool  penvDone = false;

    // Transwave position envelope (Phizmo-style direct frame-position ADSR)
    Env   twenvStage    = Env::Idle;
    float twenvLevel    = 0.0f;
    float twenvRelStart = 0.0f;

    // OSC 2 (B) amplitude envelope
    Env   env2Stage = Env::Idle;
    float env2Level = 0.0f;
    float rel2StartLevel = 0.0f;

    // Per-voice filter
    StereoFilter4Pole voiceFilter;    // oscillator A
    StereoFilter4Pole voiceFilterB;   // oscillator B (separate effect bus)

    // Glide state
    double glideStartFreq = 0.0;
    double glideTargetFreq = 0.0;
    float  glideProgress = 1.0f;

    void noteOn(int note, float vel, double prevFreq = 0.0, float glideTime = 0.f,
        float velFrameAmt = 0.f, bool carryPhase = false, float carryPhaseVal = 0.f,
        bool legato = false)
    {
        midiNote = note;
        velocity = vel;
        active = true;
        // In a legato glide the note changes pitch while still sounding, so the
        // envelopes, scan phase and declick slew keep running — only the glide
        // target below moves. Restarting them is what clicked on every slide.
        if (!legato)
        {
            phaseA = phaseB = 0.0;
            envStage = Env::Attack;   envLevel = 0.0f;
            env2Stage = Env::Attack;  env2Level = 0.0f; rel2StartLevel = 0.0f;
            fenvStage = Env::Attack;  fenvLevel = 0.0f; fenvReleaseStart = 0.0f;
            twenvStage = Env::Attack; twenvLevel = 0.0f; twenvRelStart = 0.0f;
            penvLevel = 1.0f;         penvDone = false;
            curvePhase = carryPhase ? (double)carryPhaseVal : 0.0;
            scanDir = 1;  curveFinished = false;
            // Oscillator B's scan state was previously left untouched here, so a
            // reused voice began osc-2's evolution from whatever phase the last
            // note left behind — the "random jump on trigger" on osc 2. Reset it
            // alongside A (the per-osc phase offset is applied downstream).
            curvePhaseB = carryPhase ? (double)carryPhaseVal : 0.0;
            scanDirB = 1; curveFinishedB = false;
            curveSmoothA = curveSmoothB = 0.f;
            curveWrapA = curveWrapB = false;
            frameOffset = 0.0f;
            velFrameOffset = vel * velFrameAmt;
        }
        glideTargetFreq = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
        if (prevFreq > 0.0 && glideTime > 0.001f) {
            glideStartFreq = prevFreq;
            glideProgress = 0.0f;
        }
        else {
            glideStartFreq = glideTargetFreq;
            glideProgress = 1.0f;
        }
    }
    void noteOff()
    {
        releaseStartLevel = envLevel;
        envStage = Env::Release;
        rel2StartLevel = env2Level;
        env2Stage = Env::Release;
        fenvReleaseStart = fenvLevel;
        fenvStage = Env::Release;
        twenvRelStart = twenvLevel;
        twenvStage = Env::Release;
    }
};

//==============================================================================
struct StereoChorus
{
    static constexpr int MAX_DELAY = 4096;
    float bufL[MAX_DELAY] = {}, bufR[MAX_DELAY] = {};
    int   writePos = 0;
    double lfoPhase = 0.0;

    void reset() { std::fill(bufL, bufL + MAX_DELAY, 0.f); std::fill(bufR, bufR + MAX_DELAY, 0.f); writePos = 0; lfoPhase = 0.0; }

    void process(float& L, float& R, float rate, float depth, double sr)
    {
        float ds = depth * (float)(sr * 0.025f);
        float bd = ds * 0.5f + 1.f;
        lfoPhase += rate / sr;
        if (lfoPhase > 1.0) lfoPhase -= 1.0;
        float lL = (float)std::sin(lfoPhase * juce::MathConstants<double>::twoPi), lR = -lL;
        float dL = bd + lL * ds * 0.5f, dR = bd + lR * ds * 0.5f;
        bufL[writePos % MAX_DELAY] = L;
        bufR[writePos % MAX_DELAY] = R;
        auto ri = [&](float* b, float d) -> float {
            float rf = (float)writePos - d;
            while (rf < 0) rf += MAX_DELAY;
            int r0 = (int)rf % MAX_DELAY, r1 = (r0 + 1) % MAX_DELAY;
            float fr = rf - (int)rf;
            return b[r0] + fr * (b[r1] - b[r0]);
            };
        float wL = ri(bufL, dL), wR = ri(bufR, dR);
        ++writePos;
        L += wL * 0.5f;
        R += wR * 0.5f;
    }
};

//==============================================================================
enum class ScanMode { Forward = 0, FwdStay = 1, BackForth = 2, BwdStay = 3, Backward = 4 };

//==============================================================================
class PhizmoAudioProcessor : public juce::AudioProcessor,
    public juce::AudioProcessorValueTreeState::Listener
{
public:
    PhizmoAudioProcessor();
    ~PhizmoAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor()    const override { return true; }
    const juce::String getName() const override { return "Phizmo Transwave Engine"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int  getNumPrograms()   override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& paramID, float newValue) override;

    // --- Wavetable loading ---
    void         loadWavetable(const juce::File& file, int singleCycleSamples, int slot);
    bool         loadWavetableFromMemory(const juce::MemoryBlock& fileData, const juce::String& originalFileName, int singleCycleSamples, int slot);
    bool         isWavetableLoaded(int slot) const;
    int          getNumFrames(int slot) const;
    int          getCycleSamples(int slot) const;
    juce::String getWavetableName(int slot) const;
    juce::String getWavetableFilePath(int slot) const;

    float getCurrentEvoFramePos(int osc = 0) const;
    float getCurrentScanPhase(int osc = 0) const;
    bool  getFrameSamples(int slot, int frameIndex, std::vector<float>& out) const;
    bool  getWavetableOverview(int slot, int displayWidth, int displayHeight, std::vector<float>& out) const;
    float sampleFrameNearest(int slot, float frameIndex, double phase);

    std::atomic<int> activeSlot{ 0 };

    // --- Evolution curves ---
    std::atomic<float> evoCurve[EVO_POINTS];
    std::atomic<float> evoCurveB[EVO_POINTS];
    void  setCurvePoint(int idx, float val, int osc = 0);
    float getCurvePoint(int idx, int osc = 0) const { return (osc == 0 ? evoCurve : evoCurveB)[idx].load(); }
    float evalCurve(float t, int osc = 0) const;

    std::atomic<float> evoPlayhead{ 0.0f };
    // One scan position per oscillator slot (sound * 2 + osc), so each
    // wavetable display follows the Sound it actually belongs to.
    std::atomic<float> evoFrameSlot[NUM_SOUNDS * 2];   // true frame (post-mod), for the viewer
    std::atomic<float> evoScanSlot [NUM_SOUNDS * 2];   // raw scan phase, for the editor dot

    // --- Preset persistence ---
    // Everything (file presets AND host project state) goes through one pair
    // of functions, so the two paths can never drift apart. The container is a
    // zip holding state.xml plus the raw bytes of every wavetable in use.
    static constexpr int kPresetFormatVersion = 2;
    static juce::File getPresetsDirectory();
    static juce::File getSamplesDirectory();
    bool savePreset(const juce::File& destFile);
    bool loadPreset(const juce::File& srcFile);

    // includeMachineLocalSettings: true for host state (keeps folder paths and
    // the preset name), false for a shared .phizmo file.
    void writePresetZip(juce::MemoryBlock& dest, bool includeMachineLocalSettings);
    bool readPresetZip(juce::InputStream& src, bool includeMachineLocalSettings);

    // Set while a preset is being installed. The audio thread checks this
    // before snapshotting the edit buffer, so a load can never be clobbered
    // half-way through by the next processBlock.
    std::atomic<bool> presetLoading{ false };

    // A/B compare against the last loaded or saved version of the Preset.
    // Each press swaps the live state with the stored one, so it toggles.
    void toggleCompare();
    bool isComparing() const { return comparing; }
    void storeCompareSnapshot();

    // Split feature (User's Guide p.17): map the enabled Sounds across the
    // keyboard in the factory zones, extending each to fill any gap above it.
    void applySplitZones();

    // --- User sample folder (persisted in plugin/project state, NOT in shared presets) ---
    juce::String getSampleFolder() const { return sampleFolder; }
    void setSampleFolder(const juce::String& path) { sampleFolder = path; }

    // --- User preset folder (overrides getPresetsDirectory() when set) ---
    juce::String getPresetFolder() const { return presetFolder; }
    void setPresetFolder(const juce::String& path) { presetFolder = path; }
    // Returns custom preset folder if set, otherwise falls back to the default Documents dir.
    juce::File getEffectivePresetsDirectory() const;

    // GUI window size — backed by the "guiWidth" APVTS parameter so the DAW
    // saves and restores it automatically. Height is derived from the fixed
    // 1190:804 aspect ratio.
    int  getGuiWidth()  const;
    int  getGuiHeight() const;
    void setGuiWidth(int w);

    // Last-loaded preset name — persisted in project state so it survives
    // DAW reload (not stored in the .phizmo file itself, only in DAW state).
    juce::String getCurrentPresetName() const { return currentPresetName; }
    void         setCurrentPresetName(const juce::String& name) { currentPresetName = name; }

    // Cycle sizes backed by APVTS so they survive DAW project reloads.
    int  getCycleSizeParam(int slot) const;
    void setCycleSizeParam(int slot, int size);

private:
    struct WavetableSlot
    {
        std::vector<std::vector<float>> frames;
        int   numFrames = 0, cycleSamples = 0;
        bool  loaded = false;
        juce::String name;          // display name (no extension)
        juce::String filePath;      // absolute path, local-machine only (never persisted in shared presets)
        juce::String fileName;      // original file name WITH extension, e.g. "MyWave.wav" - safe to embed/share
        juce::MemoryBlock originalFileData; // raw bytes of the original file, kept so presets can embed it
        mutable juce::CriticalSection lock;
    };
    // 8 wavetable slots: slot = sound * 2 + osc  (osc 0 = A, osc 1 = B)
    WavetableSlot wt[NUM_SOUNDS * 2];

    SoundParams   soundParams[NUM_SOUNDS];
    // cached layer params (avoids String building on the audio thread)
    std::atomic<float>* pSoundOn[NUM_SOUNDS]   = { nullptr };
    std::atomic<float>* pSoundLow[NUM_SOUNDS]  = { nullptr };
    std::atomic<float>* pSoundHigh[NUM_SOUNDS] = { nullptr };
    std::atomic<float>* pSoundGain[NUM_SOUNDS] = { nullptr };
    BlockEnvCoeffs blkS[NUM_SOUNDS];
    int  editSound = 0;                      // which Sound the knobs edit
    std::atomic<int> pendingApply{ -1 };     // request: push a Sound into the edit buffer
    std::atomic<bool> soundSwapInProgress{ false };  // guards the edit-buffer swap
    bool soundsInitialised = false;

public:
    int  getEditSound() const { return editSound; }
    void setEditSound(int s);                // capture current, then load Sound s
    bool isSoundEnabled(int s) const;
    void captureEditBuffer(int sound);       // APVTS -> soundParams[sound]
    void initialiseAllSounds();              // give every Sound a usable patch
    void applySoundToEditBuffer(int sound);  // soundParams[sound] -> APVTS
    float evalCurveS(float t, int osc, const SoundParams& sp) const;
    // Returns the current value of one of the 25 Phizmo modulators, in -1..1
    float modSource(int src, const PhizmoVoice& v, float lfoVal) const;

    // Shared note-resolution table (in beats) + helpers for the tempo-synced
    // LFO / noise rates and the six keyboard velocity curves.
    static const float kNoteBeats[12];
    static float syncedRate(int syncIndex, float freeHz, float bpm);
    float applyVelocityCurve(float vel) const;
    juce::XmlElement* createSoundsXml() const;
    // Embed / restore ALL 8 wavetable slots (4 Sounds x 2 oscillators)
    void addAllSlotsToZip(juce::ZipFile::Builder& zb, juce::XmlElement& xml);
    void restoreAllSlotsFromZip(juce::ZipFile& zip, const juce::XmlElement& xml);
    void restoreSoundsXml(const juce::XmlElement* root);
private:

    static constexpr int MAX_VOICES = 24;   // 24 voices x 2 oscillators = the
                                            // hardware's 48 simultaneous waves
    PhizmoVoice voices[MAX_VOICES];
    double currentSampleRate = 44100.0;

    // --- Phizmo effects: global reverb + insert algorithm ---
    phz::PhizmoReverb phizReverb;
    float limGain = 1.f;          // output limiter gain-reduction envelope
    StereoFilter4Pole noiseFilter;   // dedicated filter for the noise source

    // Reference version for the CMP button, plus which side we are hearing
    juce::MemoryBlock compareSnapshot;
    bool comparing = false;
    phz::PhizmoInsert phizInsert;
    std::atomic<float>* pFxAlgo = nullptr;
    std::atomic<float>* pFxVariation = nullptr;
    std::atomic<float>* pFxMix = nullptr;
    std::atomic<float>* pRevVariation = nullptr;
    std::atomic<float>* pRevAmount = nullptr;
    std::atomic<float>* pSoundFxBus[NUM_SOUNDS] = { nullptr };
    // Effect Bus is per OSCILLATOR on the hardware: inS / r1 / r2 / r3 / dry
    std::atomic<float>* pFxBusA[NUM_SOUNDS] = { nullptr };
    std::atomic<float>* pFxBusB[NUM_SOUNDS] = { nullptr };
    int lastRevVariation = -1;

    // --- Performance controllers (pitch bend wheel, mod wheel, aftertouch) ---
    std::atomic<float> bendNorm{ 0.f };      // -1 .. +1 from the pitch wheel
    std::atomic<float> modWheel{ 0.f };      // CC1
    std::atomic<float> chanPressure{ 0.f };  // channel aftertouch
    double globalLfoPhase = 0.0;             // Global LFO (vibrato)
    float  globalLfo = 0.f;
    std::atomic<float> footCtl{ 0.f }, sustainPed{ 0.f }, sostenutoPed{ 0.f };
    std::atomic<float> sys1{ 0.f }, sys2{ 0.f };
    std::atomic<float> patchSel{ 0.f };
    std::atomic<float>* pBendRange = nullptr;
    std::atomic<float>* pArpKeyboard = nullptr;
    std::atomic<float>* pLfoSync   = nullptr;
    std::atomic<float>* pNoiseRate = nullptr;
    std::atomic<float>* pNoiseSync = nullptr;
    std::atomic<float>* pVelCurve  = nullptr;
    std::atomic<float>* pMod_waveModSrcA = nullptr;
    std::atomic<float>* pMod_waveModSrcB = nullptr;
    std::atomic<float>* pMod_pitchModSrcA = nullptr;
    std::atomic<float>* pMod_pitchModSrcB = nullptr;
    std::atomic<float>* pMod_pitchModAmtA = nullptr;
    std::atomic<float>* pMod_pitchModAmtB = nullptr;
    std::atomic<float>* pMod_filtModSrcA = nullptr;
    std::atomic<float>* pMod_filtModSrcB = nullptr;
    std::atomic<float>* pMod_filtModAmtA = nullptr;
    std::atomic<float>* pMod_filtModAmtB = nullptr;
    std::atomic<float>* pOscAOn = nullptr;
    std::atomic<float>* pOscBOn = nullptr;

    juce::SmoothedValue<float> gainSmooth;
    juce::AudioBuffer<float> fxBusBuf;   // scratch: insert bus (0,1) + reverb bus (2,3)
    StereoChorus      chorus;
    juce::Reverb      reverb;

    double pitchLFOPhase = 0.0;

    // --- Raw param pointers ---
    std::atomic<float>* pEvoTime = nullptr;
    std::atomic<float>* pEvoStepped = nullptr;
    std::atomic<float>* pEvoSteppedB = nullptr;
    std::atomic<float>* pEvoLFORate = nullptr;
    std::atomic<float>* pEvoLFODepth = nullptr;
    std::atomic<float>* pPosLFORate = nullptr;
    std::atomic<float>* pPosLFODepth = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pSustain = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pBitCrush = nullptr;
    std::atomic<float>* pGrit = nullptr;
    std::atomic<float>* pDetune = nullptr;
    std::atomic<float>* pPitchLFO = nullptr;
    std::atomic<float>* pPitchLFORate = nullptr;
    std::atomic<float>* pNoiseFreq = nullptr;
    std::atomic<float>* pNoiseQ = nullptr;
    std::atomic<float>* pNoiseFilterType = nullptr;
    std::atomic<float>* pScanStyle = nullptr;
    std::atomic<float>* pScanStyleB = nullptr;
    std::atomic<float>* pJumpProb = nullptr;
    std::atomic<float>* pFilterFreq = nullptr;
    std::atomic<float>* pFilterQ = nullptr;
    std::atomic<float>* pFilterAtt = nullptr;
    std::atomic<float>* pFilterDec = nullptr;
    std::atomic<float>* pFilterSus = nullptr;
    std::atomic<float>* pFilterRel = nullptr;
    std::atomic<float>* pFilterEnvAmt = nullptr;
    std::atomic<float>* pFilterLFODep = nullptr;
    std::atomic<float>* pPitchEnvAmt = nullptr;
    std::atomic<float>* pPitchEnvAtt = nullptr;
    std::atomic<float>* pPitchEnvDec = nullptr;
    std::atomic<float>* pOscMix = nullptr;
    std::atomic<float>* pSpread = nullptr;
    std::atomic<float>* pStereoWidth = nullptr;
    std::atomic<float>* pUniDetune = nullptr;
    std::atomic<float>* pStereoPhase = nullptr;
    std::atomic<float>* pChorusRate = nullptr;
    std::atomic<float>* pChorusDepth = nullptr;
    std::atomic<float>* pRingMod = nullptr;
    std::atomic<float>* pReverbSize = nullptr;
    std::atomic<float>* pReverbDamp = nullptr;
    std::atomic<float>* pReverbWet = nullptr;

    // New params
    std::atomic<float>* pAttack2 = nullptr;
    std::atomic<float>* pDecay2 = nullptr;
    std::atomic<float>* pSustain2 = nullptr;
    std::atomic<float>* pRelease2 = nullptr;
    std::atomic<float>* pOctaveA = nullptr;
    std::atomic<float>* pOctaveB = nullptr;
    std::atomic<float>* pGlide = nullptr;
    std::atomic<float>* pMono = nullptr;
    std::atomic<float>* pNoise = nullptr;

    // Engine mode toggles
    std::atomic<float>* pFrameInterp = nullptr;
    std::atomic<float>* pVelToFrame = nullptr;
    std::atomic<float>* pEvoPhaseCarry = nullptr;

    // Transwave position envelope
    std::atomic<float>* pTwAtt    = nullptr;
    std::atomic<float>* pTwDec    = nullptr;
    std::atomic<float>* pTwSus    = nullptr;
    std::atomic<float>* pTwRel    = nullptr;
    std::atomic<float>* pTwAmt    = nullptr;
    std::atomic<float>* pTwVelAmt = nullptr;
    std::atomic<float>* pAmpVelAmt = nullptr;

    // Misc (row 2, cols 1-6)
    std::atomic<float>* pFrameSnap    = nullptr;   // frame quantise steps
    std::atomic<float>* pTwToFilter   = nullptr;   // TW env → filter cutoff amount
    std::atomic<float>* pEvoBPhaseOff = nullptr;   // Osc B curve phase offset vs A
    std::atomic<float>* pEvoPhaseOff = nullptr;    // Osc A curve phase (start) offset
    std::atomic<float>* pEvoSlew = nullptr;        // Osc A curve fade time (ms)
    std::atomic<float>* pEvoSlewB = nullptr;       // Osc B curve fade time (ms)
    std::atomic<float>* pKeytrack     = nullptr;   // MIDI note → frame position
    std::atomic<float>* pEvoTimeB     = nullptr;   // independent evo time for Osc B
    std::atomic<float>* pEvoRestart   = nullptr;   // toggle: reset curve on note-on
    std::atomic<float>* pWtSmooth     = nullptr;   // wavetable cycle edge smoothing A (0-1 → 0-5%)
    std::atomic<float>* pWtSmoothB    = nullptr;   // wavetable cycle edge smoothing B

    // --- Phizmo remake params ---
    std::atomic<float>* pTuneA = nullptr;   std::atomic<float>* pTuneB = nullptr;
    std::atomic<float>* pFineA = nullptr;   std::atomic<float>* pFineB = nullptr;
    std::atomic<float>* pWaveStartA = nullptr; std::atomic<float>* pWaveStartB = nullptr;
    std::atomic<float>* pWaveModA = nullptr;   std::atomic<float>* pWaveModB = nullptr;
    std::atomic<float>* pLevelA = nullptr;  std::atomic<float>* pLevelB = nullptr;
    std::atomic<float>* pPanA = nullptr;    std::atomic<float>* pPanB = nullptr;
    std::atomic<float>* pFilterKeytrack = nullptr;
    std::atomic<float>* pFilterType = nullptr;
    std::atomic<float>* pLfoShape = nullptr;
    std::atomic<float>* pLfoSpeed = nullptr;
    std::atomic<float>* pModSelect = nullptr;
    std::atomic<float>* pFizF = nullptr; std::atomic<float>* pFizI = nullptr;
    std::atomic<float>* pFizZ = nullptr; std::atomic<float>* pFizM = nullptr;
    std::atomic<float>* pFizO = nullptr; std::atomic<float>* pFizODest = nullptr;
    std::atomic<float>* pArpOn = nullptr;   std::atomic<float>* pArpMode = nullptr;
    std::atomic<float>* pArpRange = nullptr; std::atomic<float>* pArpRate = nullptr;
    std::atomic<float>* pTempo = nullptr;   std::atomic<float>* pArpGate = nullptr;
    std::atomic<float>* pEnvMode = nullptr;

    // --- Arpeggiator runtime state ---
    juce::SortedSet<int> arpHeldNotes;      // currently held MIDI notes
    std::vector<int>     arpSequence;        // expanded (range) play order
    int      arpIndex        = 0;
    double   arpSampleCounter = 0.0;         // samples until next arp step
    int      arpCurrentNote  = -1;           // note currently sounding (for note-off)
    float    arpCurrentVel   = 0.8f;
    double   arpGateSamples  = 0.0;          // samples until we release current note
    void     rebuildArpSequence();
    float    lfoValue(float phase01, int shape) const;   // Phizmo LFO shapes

    std::atomic<float> phizIWaveMod{ 0.f };   // shared so editor can show macro effect

    // Block-level LFO values computed once per block
    float blockPosLFO = 0.0f;
    float blockPitchLFO = 0.0f;

    // Mono glide state
    double lastNoteFreq = 0.0;
    bool   monoActive = false;

    // Per-voice LFO phases 
    double voiceEvoLFOPhase[MAX_VOICES] = {};
    double voicePosLFOPhase[MAX_VOICES] = {};

    // Block-level coefficients cache
    BlockEnvCoeffs blk;

    void  synthesiseVoice(PhizmoVoice& v, int vi,
        float posLFOMod, double pitchMult,
        float& outL, float& outR, float& outBL, float& outBR,
        const BlockEnvCoeffs& bc, const SoundParams& SP);
    float applyBitCrush(float s, float bits);
    float sampleFrameRaw(const WavetableSlot& s, float frameIndex, double phase, bool interp, float smoothAmt = 0.f) const;

    // Shared implementation used by both loadWavetable() (from disk) and
    // loadWavetableFromMemory() (extracted from a preset zip). Keeps the
    // raw original bytes + original file name around so presets can later
    // re-embed the wavetable without needing the original absolute path.
    bool  loadWavetableFromReader(std::unique_ptr<juce::AudioFormatReader> reader,
                                   juce::MemoryBlock originalBytes,
                                   const juce::String& originalFileName,
                                   const juce::String& displayFilePath,
                                   int cycleSamplesRequested, int slot);

    juce::String sampleFolder;
    juce::String presetFolder;
    juce::String currentPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhizmoAudioProcessor)
};