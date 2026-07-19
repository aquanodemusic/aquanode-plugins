#include "PhaseDistortModule.h"

using namespace aquanode;

// The core of phase distortion: read the phase faster through the first part
// of the cycle and slower through the rest. dcw 0 -> straight line (a plain
// cosine comes out); dcw -> 1 -> the index rushes to the halfway point and
// crawls after it, which is exactly a sawtooth's spectrum.
double PhaseDistortModule::warpSaw (double p, double dcw)
{
    const double bp = juce::jmax (0.001, 0.5 * (1.0 - dcw));   // breakpoint
    return p < bp ? 0.5 * p / bp
                  : 0.5 + 0.5 * (p - bp) / (1.0 - bp);
}

float PhaseDistortModule::shape (int mode, double p, double dcw)
{
    const double twoPi = juce::MathConstants<double>::twoPi;

    switch (mode)
    {
        case mSaw:
            return (float) -std::cos (twoPi * warpSaw (p, dcw));

        case mSquare:
        {
            // a plateau in the middle of each half: the index stops moving, so
            // the cosine holds its value and the edges square off
            const double bp = juce::jmax (0.001, 0.5 * (1.0 - dcw));
            double x;
            if (p < bp)             x = 0.5 * p / bp;
            else if (p < 0.5)       x = 0.5;
            else if (p < 0.5 + bp)  x = 0.5 + 0.5 * (p - 0.5) / bp;
            else                    x = 1.0;
            return (float) -std::cos (twoPi * x);
        }

        case mPulse:
        {
            // like square but asymmetric: the plateau eats most of the cycle
            const double bp = juce::jmax (0.001, 0.5 * (1.0 - dcw));
            const double x = p < bp ? p / bp : 1.0;
            return (float) -std::cos (twoPi * x);
        }

        case mDblSine:
            // two cosine cycles crammed into the warped index
            return (float) -std::cos (2.0 * twoPi * warpSaw (p, dcw));

        case mSawPulse:
        {
            // the CZ's "double" shape: a saw-warped cosine gated by a pulse
            const double x = warpSaw (p, dcw);
            const double gateEdge = 0.5 + 0.5 * dcw;
            return (float) (-std::cos (twoPi * x) * (p < gateEdge ? 1.0 : 0.0));
        }

        default:
        {
            // Reso 1/2/3: a sine at an integer multiple of the pitch, windowed
            // once per fundamental cycle. The multiple is what DCW sweeps, so
            // the peak glides through the spectrum like a resonant filter.
            const double ratio = 1.0 + dcw * 15.0;
            const double car = std::sin (twoPi * p * ratio);

            double window;
            if (mode == mReso1)      window = 1.0 - p;                       // saw window
            else if (mode == mReso2) window = 0.5 * (1.0 - std::cos (twoPi * p));  // hann-ish
            else                     window = 1.0 - std::abs (2.0 * p - 1.0);      // triangle

            return (float) (car * window);
        }
    }
}

void PhaseDistortModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void PhaseDistortModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double freq = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                           ! pool.isMuted (v), sampleRate));

    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    // knob values glide over ~12 ms so turning DCW while notes sound is
    // crackle-free; param() folds knob-modulation cables in, which get the
    // same de-zippering. The audio-rate DCW In signal itself stays direct.
    dcwSm[v]    += dcwSmoothCoeff * (param (pDcw)    - dcwSm[v]);
    dcwModSm[v] += dcwSmoothCoeff * (param (pDcwMod) - dcwModSm[v]);

    // DCW In adds to the knob, scaled by its own depth control
    const double dcw = juce::jlimit (0.0, 0.999,
        (double) (dcwSm[v] * 0.01f + dcwModSm[v] * 0.01f * inputs[0][0]));

    const float y = shape ((int) param (pMode), phase[v], dcw);

    const bool envConnected = isInputConnected (1);
    const float gain = gate.next (v, envConnected, inputs[1][0], sampleRate);

    const float out = y * gain * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor phaseDistortDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.pd";
    d.displayName = "Phase Distort";
    d.description =
        "Phase distortion: a cosine read with a warped phase index. Digitally Controlled Waveforms "
        "(DCW) shape it while the pitch stays put - a filter sweep from a module with no filter. "
        "DCW In wants an LFO or ADSR for the classic sweep; Env In wants an ADSR.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 10;
    d.sockets = {
        modIn    ("dcwIn",     "DCW In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 0),
        makeRotary ("dcw",    "DCW",    0.0f, 100.0f, 50.0f, 0, "%"),
        makeCombo  ("mode",   "Mode",   { "Saw", "Square", "Pulse", "Dbl Sine",
                                          "Saw Pulse", "Reso 1", "Reso 2", "Reso 3" }, 0, 0, 2),
        makeRotary ("dcwMod", "DCW Mod", -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("voices", "Voices", 1.0f, (float) kMaxVoices, (float) kMaxVoices, 3, {}, false, 1.0f).noMod(),
        makeRotary ("glide",  "Glide",  0.0f, 1000.0f, 0.0f, 3, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PhaseDistortModule, phaseDistortDescriptor)
