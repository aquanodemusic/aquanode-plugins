#include "DelayModule.h"

using namespace aquanode;

static const double kBeatsPerChoice[] = { 0.25, 0.5, 1.0, 2.0, 4.0 };   // 1/16 .. 1/1 (1/1 = 4 beats)

void DelayModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int size = juce::jmax (1, (int) (sr * 10.0));   // 10 s max (covers 1/1 down to 24 BPM)
    for (int c = 0; c < 2; ++c)
    {
        line[c].assign ((size_t) size, 0.0f);
        smoothedDelay[c] = sr * 0.5;
        hpState[c] = 0.0f;
    }
    writePos = 0;
}

void DelayModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int size = (int) line[0].size();
    if (size < 2)
    {
        outputs[0] = inputs[0];
        return;
    }

    const double bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;   // 120 BPM fallback
    const float feedback = param (pFeedback);
    const float wet = param (pDryWet) * 0.01f;

    const float hpCut = param (pHighPass);
    const float hpCoeff = std::exp (-juce::MathConstants<float>::twoPi * hpCut / (float) sampleRate);

    for (int c = 0; c < 2; ++c)
    {
        const int choice = juce::jlimit (0, 4, (int) param (c == 0 ? pTimeL : pTimeR));
        const double target = juce::jlimit (1.0, (double) size - 2.0,
                                            (60.0 / bpm) * kBeatsPerChoice[choice] * sampleRate);
        smoothedDelay[c] += 0.0005 * (target - smoothedDelay[c]);

        double readPos = (double) writePos - smoothedDelay[c];
        while (readPos < 0.0) readPos += size;
        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % size;
        const float frac = (float) (readPos - i0);
        const float delayed = line[c][(size_t) i0] + (line[c][(size_t) i1] - line[c][(size_t) i0]) * frac;

        // one-pole high-pass in the feedback loop
        hpState[c] = hpCoeff * hpState[c] + (1.0f - hpCoeff) * delayed;
        const float hpOut = delayed - hpState[c];

        const float in = inputs[0][(size_t) c];
        line[c][(size_t) writePos] = juce::jlimit (-4.0f, 4.0f, in + hpOut * feedback);

        outputs[0][(size_t) c] = in * (1.0f - wet) + delayed * wet;
    }

    writePos = (writePos + 1) % size;
}

static ModuleDescriptor delayDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.delay";
    d.displayName = "Delay";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 0;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    const juce::StringArray times { "1/16", "1/8", "1/4", "1/2", "1/1" };
    d.params = {
        makeSteppedList ("timeL", "Time L", times, 2, 0),
        makeSteppedList ("timeR", "Time R", times, 2, 0),
        makeRotary ("feedback", "Feedback", 0.0f, 1.2f, 0.3f, 0),
        makeRotary ("highPass", "High-Pass", 20.0f, 2000.0f, 100.0f, 0, "Hz", true),
        makeRotary ("dryWet", "Dry/Wet", 0.0f, 100.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DelayModule, delayDescriptor)
