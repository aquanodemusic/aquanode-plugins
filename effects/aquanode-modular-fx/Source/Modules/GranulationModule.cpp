#include "GranulationModule.h"

using namespace aquanode;

void GranulationModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int cap = juce::jmax (1024, (int) (sr * maxWindowSeconds));
    bufL.assign ((size_t) cap, 0.0f);
    bufR.assign ((size_t) cap, 0.0f);
    writePos = 0;
    spawnCounter = 0.0;
    for (auto& g : grains)
        g = Grain();
}

void GranulationModule::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    writePos = 0;
    spawnCounter = 0.0;
    for (auto& g : grains)
        g = Grain();
}

void GranulationModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const int cap = (int) bufL.size();
    if (cap < 8)
    {
        outputs[0] = inputs[0];
        return;
    }

    // record the dry input into the rolling buffer grains are read from
    const int writeIdx = (int) (writePos % (juce::int64) cap);
    bufL[(size_t) writeIdx] = inputs[0][0];
    bufR[(size_t) writeIdx] = inputs[0][1];
    ++writePos;

    const int nGrains  = juce::jlimit (1, maxGrains, (int) param (pGrains));
    const int grainLen = juce::jmax (8, (int) (param (pSize) * 0.001 * sampleRate));

    const double windowSeconds = juce::jlimit (0.05, maxWindowSeconds, (double) param (pWindow));
    const juce::int64 windowSamples = juce::jmax ((juce::int64) grainLen + 2,
                                                  (juce::int64) (windowSeconds * sampleRate));
    const juce::int64 availableLen = juce::jmin (windowSamples, writePos);
    const juce::int64 windowStart = writePos - availableLen;

    // ---- grain spawning: nGrains overlapping across one grain length -------
    spawnCounter -= 1.0;
    if (spawnCounter <= 0.0 && availableLen > (juce::int64) grainLen + 2)
    {
        spawnCounter += juce::jmax (1.0, (double) grainLen / (double) nGrains);

        for (auto& g : grains)
        {
            if (g.active)
                continue;

            const float sprayAmt = param (pSpray) * 0.01f;
            const float posFrac = juce::jlimit (0.0f, 1.0f,
                param (pPosition) * 0.01f + (random.nextFloat() * 2.0f - 1.0f) * sprayAmt * 0.5f);

            g.active = true;
            g.pos = (double) windowStart + posFrac * (double) (availableLen - 2);
            g.age = 0;
            g.length = grainLen;

            // pitch: base semitone knob plus per-grain random dispersion
            const float dispSemis = (random.nextFloat() * 2.0f - 1.0f)
                                    * param (pPitchDisp) * 0.01f * 12.0f;
            g.inc = std::pow (2.0, (double) (param (pPitch) + dispSemis) / 12.0);

            // stereo spread: random per-grain pan
            const float pan = (random.nextFloat() * 2.0f - 1.0f) * param (pStereo) * 0.01f;
            g.gainL = pan > 0.0f ? 1.0f - pan : 1.0f;
            g.gainR = pan < 0.0f ? 1.0f + pan : 1.0f;
            break;
        }
    }

    // ---- render active grains ----------------------------------------------
    float sumL = 0.0f, sumR = 0.0f;

    for (auto& g : grains)
    {
        if (! g.active)
            continue;

        if (g.age >= g.length || g.pos < (double) windowStart || g.pos >= (double) (writePos - 1))
        {
            g = Grain();
            continue;
        }

        // Hann window over the grain's life
        const float window = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                             * (float) g.age / (float) g.length);

        const juce::int64 i0 = (juce::int64) g.pos;
        const float frac = (float) (g.pos - (double) i0);
        const int c0 = (int) (i0 % (juce::int64) cap);
        const int c1 = (int) ((i0 + 1) % (juce::int64) cap);
        const float l = bufL[(size_t) c0] + (bufL[(size_t) c1] - bufL[(size_t) c0]) * frac;
        const float r = bufR[(size_t) c0] + (bufR[(size_t) c1] - bufR[(size_t) c0]) * frac;

        sumL += l * window * g.gainL;
        sumR += r * window * g.gainR;

        g.pos += g.inc;
        ++g.age;
    }

    // overlapping grains stack; normalise so the grain count sets texture, not level
    const float norm = 1.0f / std::sqrt ((float) nGrains);
    const float wet = param (pDryWet) * 0.01f;

    outputs[0][0] = inputs[0][0] * (1.0f - wet) + sumL * norm * wet;
    outputs[0][1] = inputs[0][1] * (1.0f - wet) + sumR * norm * wet;
}

static ModuleDescriptor granulationDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.granulation";
    d.displayName = "Granulation";
    d.description =
        "Live granular effect: continuously records the incoming audio into a rolling buffer and "
        "blends a cloud of Hann-windowed grains, read back from that buffer, in with the dry "
        "signal. Adapted from the Sample Granulator Module, but as a live-input mode.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 25;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("grains",    "Grains",     1.0f, 32.0f, 8.0f, 0, {}, false, 1.0f),
        makeRotary ("size",      "Size",       5.0f, 500.0f, 80.0f, 0, "ms", true),
        makeRotary ("position",  "Position",   0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("spray",     "Spray",      0.0f, 100.0f, 10.0f, 0, "%"),
        makeRotary ("window",    "Window",     0.05f, 5.0f, 1.0f, 1, "s", true),
        makeRotary ("pitch",     "Pitch",      -24.0f, 24.0f, 0.0f, 1, "st"),
        makeRotary ("pitchDisp", "Pitch Disp", 0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("stereo",    "Stereo",     0.0f, 100.0f, 30.0f, 1, "%"),
        makeRotary ("dryWet",    "Dry/Wet",    0.0f, 100.0f, 50.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (GranulationModule, granulationDescriptor)
