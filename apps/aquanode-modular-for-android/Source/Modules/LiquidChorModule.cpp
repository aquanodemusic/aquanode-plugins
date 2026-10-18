#include "LiquidChorModule.h"

using namespace aquanode;

void LiquidChorModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int size = juce::jmax (64, (int) (sr * 0.05));   // 50 ms > max 25 ms sweep
    for (int c = 0; c < 2; ++c)
        line[c].assign ((size_t) size, 0.0f);
    reset();
}

float LiquidChorModule::readInterp (int channel, double delaySamples) const
{
    const int size = (int) line[channel].size();
    double readPos = (double) writePos - delaySamples;
    while (readPos < 0.0) readPos += size;
    const int i0 = (int) readPos % size;
    const int i1 = (i0 + 1) % size;
    const float frac = (float) (readPos - std::floor (readPos));
    return line[channel][(size_t) i0] * (1.0f - frac) + line[channel][(size_t) i1] * frac;
}

void LiquidChorModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int size = (int) line[0].size();
    if (size < 8)
    {
        outputs[0] = inputs[0];
        return;
    }

    const float time1 = param (pTime1);
    const float time2 = param (pTime2);
    const float feedbackAmt = param (pFeedback) * 0.01f * 0.95f;
    const float wet = param (pDryWet) * 0.01f;
    const double lrPhNorm = param (pLrPhase) / 360.0;

    lfoPhase += param (pSpeed) / sampleRate;
    lfoPhase -= std::floor (lfoPhase);
    double phaseR = lfoPhase + lrPhNorm;
    phaseR -= std::floor (phaseR);

    const float lfoL = (float) std::sin (lfoPhase * juce::MathConstants<double>::twoPi);
    const float lfoR = (float) std::sin (phaseR * juce::MathConstants<double>::twoPi);

    const float ms2smp = (float) (sampleRate / 1000.0);

    // sweep between Time 1 and Time 2 under the LFO (original's mapping)
    const double dtL = juce::jlimit (1.0, (double) size - 2.0,
        (double) ((time1 + (time2 - time1) * (0.5f + 0.5f * lfoL)) * ms2smp));
    const double dtR = juce::jlimit (1.0, (double) size - 2.0,
        (double) ((time1 + (time2 - time1) * (0.5f + 0.5f * lfoR)) * ms2smp));

    const float dryL = inputs[0][0];
    const float dryR = inputs[0][1];

    line[0][(size_t) writePos] = juce::jlimit (-4.0f, 4.0f, dryL + fb[0] * feedbackAmt);
    line[1][(size_t) writePos] = juce::jlimit (-4.0f, 4.0f, dryR + fb[1] * feedbackAmt);

    const float wetL = readInterp (0, dtL);
    const float wetR = readInterp (1, dtR);
    fb[0] = wetL;
    fb[1] = wetR;

    writePos = (writePos + 1) % size;

    outputs[0][0] = dryL * (1.0f - wet) + wetL * wet;
    outputs[0][1] = dryR * (1.0f - wet) + wetR * wet;
}

static ModuleDescriptor liquidChorDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.liquidchor";
    d.displayName = "AquaChorus";
    d.description =
        "A liquid chorus/flanger: two delay lines sweeping between Time 1 and Time 2 under an "
        "LFO, with an L/R phase offset for width and feedback for flanging.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 8;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("time1",    "Time 1",   0.5f, 25.0f, 7.0f, 0, "ms", true),
        makeRotary ("time2",    "Time 2",   0.5f, 25.0f, 14.0f, 0, "ms", true),
        makeRotary ("speed",    "Speed",    0.01f, 10.0f, 0.5f, 0, "Hz", true),
        makeRotary ("feedback", "Feedback", 0.0f, 95.0f, 0.0f, 0, "%"),
        makeRotary ("dryWet",   "Dry/Wet",  0.0f, 100.0f, 50.0f, 0, "%"),
        makeRotary ("lrPhase",  "LR Phase", 0.0f, 360.0f, 90.0f, 1, {}, false, 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LiquidChorModule, liquidChorDescriptor)
