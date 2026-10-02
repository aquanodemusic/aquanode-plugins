#include "AquaFilterModule.h"

using namespace aquanode;

float AquaFilterModule::ladderProcess (LadderState& s, float x, float tune, float res4) const
{
    x -= res4 * s.y[3];
    x = tanhA (x);
    s.y[0] += tune * (x            - tanhA (s.y[0]));
    s.y[1] += tune * (tanhA (s.y[0]) - tanhA (s.y[1]));
    s.y[2] += tune * (tanhA (s.y[1]) - tanhA (s.y[2]));
    s.y[3] += tune * (tanhA (s.y[2]) - tanhA (s.y[3]));
    return s.y[3];
}

void AquaFilterModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float modIn = inputs[1][0];
    const float depth = param (pModDepth) * 0.01f;

    const float cutoff = juce::jlimit (20.0f, juce::jmin (20000.0f, (float) (sampleRate * 0.45)),
        param (pCutoff) * std::pow (2.0f, depth * modIn * 5.0f));

    const int   type  = (int) param (pType);       // 0=LP12 1=LP24 2=LP24+ 3=BP 4=HP
    const int   curve = (int) param (pResCurve);   // 0=Quadratic 1=Cubic
    const float r     = shapeResonance (param (pResonance), curve);
    const float drive = param (pDrive);

    // pre-filter saturation with gain compensation, exactly as in the plugin
    const float driveGain = 1.0f + drive * 6.0f;
    const float driveComp = 1.0f / (1.0f + drive * 0.6f);

    const float sr = (float) sampleRate;

    if (type <= 2)   // ladder modes: Huovilainen coefficients
    {
        const float fc  = cutoff / sr;
        const float fc2 = fc * fc;
        const float fc3 = fc2 * fc;
        const float fcr = 1.8730f * fc3 + 0.4955f * fc2 - 0.6490f * fc + 0.9988f;
        const float acr = -3.9364f * fc2 + 1.8409f * fc + 0.9968f;
        const float tune = juce::jlimit (0.0f, 1.0f,
            1.0f - std::exp (-juce::MathConstants<float>::twoPi * fcr * fc));
        const float res4 = 4.2f * r * acr;

        for (int c = 0; c < 2; ++c)
        {
            const float in = tanhA (inputs[0][(size_t) c] * driveGain) * driveComp;
            auto& s = ladder[v][c];
            float y;

            if (type == 0)   // LP12: two ladder stages, half feedback
            {
                float x = in - res4 * 0.5f * s.y[1];
                x = tanhA (x);
                s.y[0] += tune * (x            - tanhA (s.y[0]));
                s.y[1] += tune * (tanhA (s.y[0]) - tanhA (s.y[1]));
                y = s.y[1];
            }
            else if (type == 1)   // LP24
            {
                y = ladderProcess (s, in, tune, res4);
            }
            else   // LP24+: extra saturation on the ladder output
            {
                y = tanhA (ladderProcess (s, in, tune, res4) * 1.35f) / 1.35f;
            }

            outputs[0][(size_t) c] = y;
        }
    }
    else   // SVF modes (Simper): BP / HP
    {
        const float g  = std::tan (juce::MathConstants<float>::pi * cutoff / sr);
        const float k  = 2.0f * (1.0f - r * 0.985f);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        for (int c = 0; c < 2; ++c)
        {
            const float in = tanhA (inputs[0][(size_t) c] * driveGain) * driveComp;
            auto& s = svf[v][c];

            const float v3 = in - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;

            outputs[0][(size_t) c] = (type == 3) ? v1                    // BP
                                                 : in - k * v1 - v2;     // HP
        }
    }
}

//==============================================================================
static ModuleDescriptor aquaFilterDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "filter.aquafilter";
    d.displayName = "AquaFilter";
    d.description =
        "The filter core of the AquaFilter plugin: a tanh-stage ladder in 12/24 dB lowpass "
        "plus an extra-saturated 24+ mode, and an SVF for bandpass and highpass, with Drive "
        "saturating before the filter. Mod In sweeps the cutoff - an ADSR, LFO or Env Follow. "
        "Res Curve reshapes the resonance knob rather than the sound: Quadratic bites earlier, "
        "Cubic keeps the lower half tamer.";
    d.section = ModuleSection::Filter;
    d.sidebarOrder = 7;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        modIn ("cutoffMod", "Cutoff"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("cutoff",    "Cutoff",    20.0f, 20000.0f, 2000.0f, 0, "Hz", true),
        makeRotary ("resonance", "Resonance", 0.0f, 1.0f, 0.3f, 0),
        makeRotary ("drive",     "Drive",     0.0f, 1.0f, 0.0f, 0),
        makeRotary ("modDepth",  "Mod Depth", -100.0f, 100.0f, 0.0f, 0, "%"),
        makeCombo  ("type",     "Filter Type", { "LP12", "LP24", "LP24+", "BP", "HP" }, 1, 1, 2),
        makeCombo  ("resCurve", "Res Curve",   { "Quadratic", "Cubic" }, 1, 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (AquaFilterModule, aquaFilterDescriptor)
