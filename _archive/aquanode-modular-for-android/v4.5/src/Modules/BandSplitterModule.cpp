#include "BandSplitterModule.h"

using namespace aquanode;

// RBJ cookbook, Q = 1/sqrt(2) (Butterworth). Two of these in series = LR4:
// at the crossover frequency each leg sits at -6 dB, and low+high sum back
// to a flat 0 dB with a smooth, symmetric phase rotation - no passband
// ripple, no pre-ringing, no block latency.
void BandSplitterModule::setButterworthLowpass (BiquadStage& s, double freq, double sr)
{
    const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
    const double cosw0 = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752);   // Q = 1/sqrt(2)

    const double b0 = (1.0 - cosw0) * 0.5;
    const double b1 =  1.0 - cosw0;
    const double b2 = (1.0 - cosw0) * 0.5;
    const double a0 =  1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 =  1.0 - alpha;

    s.b0 = (float) (b0 / a0); s.b1 = (float) (b1 / a0); s.b2 = (float) (b2 / a0);
    s.a1 = (float) (a1 / a0); s.a2 = (float) (a2 / a0);
}

void BandSplitterModule::setButterworthHighpass (BiquadStage& s, double freq, double sr)
{
    const double w0 = juce::MathConstants<double>::twoPi * freq / sr;
    const double cosw0 = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * 0.70710678118654752);

    const double b0 =  (1.0 + cosw0) * 0.5;
    const double b1 = -(1.0 + cosw0);
    const double b2 =  (1.0 + cosw0) * 0.5;
    const double a0 =  1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 =  1.0 - alpha;

    s.b0 = (float) (b0 / a0); s.b1 = (float) (b1 / a0); s.b2 = (float) (b2 / a0);
    s.a1 = (float) (a1 / a0); s.a2 = (float) (a2 / a0);
}

void BandSplitterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double nyquistGuard = sampleRate * 0.49;   // stay clear of the coefficient's pole at Nyquist

    const double split1 = juce::jlimit (20.0, nyquistGuard, (double) param (pSplit1));
    // keep Split 2 a fixed ratio above Split 1 so the two crossovers can't
    // collide (which would make Band 2 collapse to nothing and destabilise
    // the cascade)
    const double split2 = juce::jlimit (split1 * 1.05, nyquistGuard, (double) param (pSplit2));

    BiquadStage lpCoef1, hpCoef1, lpCoef2, hpCoef2;
    setButterworthLowpass  (lpCoef1, split1, sampleRate);
    setButterworthHighpass (hpCoef1, split1, sampleRate);
    setButterworthLowpass  (lpCoef2, split2, sampleRate);
    setButterworthHighpass (hpCoef2, split2, sampleRate);

    for (int c = 0; c < 2; ++c)
    {
        const float in = inputs[0][(size_t) c];

        lp1[v][c].a.b0 = lp1[v][c].b.b0 = lpCoef1.b0; lp1[v][c].a.b1 = lp1[v][c].b.b1 = lpCoef1.b1;
        lp1[v][c].a.b2 = lp1[v][c].b.b2 = lpCoef1.b2; lp1[v][c].a.a1 = lp1[v][c].b.a1 = lpCoef1.a1;
        lp1[v][c].a.a2 = lp1[v][c].b.a2 = lpCoef1.a2;

        hp1[v][c].a.b0 = hp1[v][c].b.b0 = hpCoef1.b0; hp1[v][c].a.b1 = hp1[v][c].b.b1 = hpCoef1.b1;
        hp1[v][c].a.b2 = hp1[v][c].b.b2 = hpCoef1.b2; hp1[v][c].a.a1 = hp1[v][c].b.a1 = hpCoef1.a1;
        hp1[v][c].a.a2 = hp1[v][c].b.a2 = hpCoef1.a2;

        lp2[v][c].a.b0 = lp2[v][c].b.b0 = lpCoef2.b0; lp2[v][c].a.b1 = lp2[v][c].b.b1 = lpCoef2.b1;
        lp2[v][c].a.b2 = lp2[v][c].b.b2 = lpCoef2.b2; lp2[v][c].a.a1 = lp2[v][c].b.a1 = lpCoef2.a1;
        lp2[v][c].a.a2 = lp2[v][c].b.a2 = lpCoef2.a2;

        hp2[v][c].a.b0 = hp2[v][c].b.b0 = hpCoef2.b0; hp2[v][c].a.b1 = hp2[v][c].b.b1 = hpCoef2.b1;
        hp2[v][c].a.b2 = hp2[v][c].b.b2 = hpCoef2.b2; hp2[v][c].a.a1 = hp2[v][c].b.a1 = hpCoef2.a1;
        hp2[v][c].a.a2 = hp2[v][c].b.a2 = hpCoef2.a2;

        const float band1 = lp1[v][c].process (in);
        const float aboveSplit1 = hp1[v][c].process (in);
        const float band2 = lp2[v][c].process (aboveSplit1);
        const float band3 = hp2[v][c].process (aboveSplit1);

        outputs[0][(size_t) c] = band1;
        outputs[1][(size_t) c] = band2;
        outputs[2][(size_t) c] = band3;
    }
}

static ModuleDescriptor bandSplitterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "effect.bandsplitter";
    d.displayName = "Band Splitter";
    d.description =
        "Splits one signal into 3 frequency bands using two cascaded Linkwitz-Riley "
        "4th-order crossovers - the same clean, phase-coherent splitting family that "
        "professional multiband tools use, so each band can be processed separately "
        "and summed back together without ripple. Split 2 is always kept above Split 1. "
        "Outputs: Band 1 (Low), Band 2 (Mid), Band 3 (High).";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 26;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("band1Out", "Band 1 (Low)"),
        audioOut ("band2Out", "Band 2 (Mid)"),
        audioOut ("band3Out", "Band 3 (High)")
    };
    d.params = {
        makeRotary ("split1", "Split 1", 20.0f,   2000.0f,  200.0f,  0, "Hz", true),
        makeRotary ("split2", "Split 2", 200.0f,  20000.0f, 2000.0f, 0, "Hz", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (BandSplitterModule, bandSplitterDescriptor)
