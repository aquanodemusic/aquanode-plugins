#include "WavetableModule.h"

using namespace aquanode;

void WavetableModule::blockStart()
{
    latchedSample = getLoadedSample();
    if (latchedSample != nullptr && latchedSample->getNumSamples() > 0)
    {
        tableData = latchedSample->getReadPointer (0);
        tableLen = latchedSample->getNumSamples();
    }
    else
    {
        tableData = nullptr;
        tableLen = 0;
    }
}

// one single-cycle frame, linearly interpolated
float WavetableModule::readFrame (int frame, double phase01) const
{
    static const int sizes[4] = { 256, 512, 1024, 2048 };
    const int frameSize = sizes[juce::jlimit (0, 3, (int) param (pFrameSize))];

    const int base = frame * frameSize;
    const double pos = phase01 * frameSize;
    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % frameSize;
    const float frac = (float) (pos - i0);

    const int a = base + i0;
    const int b = base + i1;
    const float s0 = a < tableLen ? tableData[a] : 0.0f;
    const float s1 = b < tableLen ? tableData[b] : 0.0f;
    return s0 + (s1 - s0) * frac;
}

void WavetableModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
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

void WavetableModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const double freq = midiNoteToHz ((double) glide.next (v, glideMillis(), isMonoVoice(),
                                                           ! pool.isMuted (v), sampleRate));

    phase[v] += freq / sampleRate;
    phase[v] -= std::floor (phase[v]);

    if (tableData == nullptr || tableLen <= 0)
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    static const int sizes[4] = { 256, 512, 1024, 2048 };
    const int frameSize = sizes[juce::jlimit (0, 3, (int) param (pFrameSize))];
    const int numFrames = juce::jmax (1, tableLen / frameSize);

    // read phase, optionally warped inside the frame
    double rp = phase[v] + (double) inputs[0][0];      // FM In
    rp -= std::floor (rp);

    const float warp = param (pWarp) * 0.01f;
    if (warp > 0.001f)
    {
        // bend the first half of the cycle into a smaller window: the wave
        // completes early and holds, which brightens it like a pulse squeeze
        const double bp = juce::jmax (0.02, 0.5 * (1.0 - warp));
        rp = rp < bp ? 0.5 * rp / bp : 0.5 + 0.5 * (rp - bp) / (1.0 - bp);
    }

    // Position scans the stack and crossfades between adjacent frames
    const float posNorm = juce::jlimit (0.0f, 1.0f,
        param (pPosition) * 0.01f + param (pPosMod) * 0.01f * inputs[1][0]);
    const float fpos = posNorm * (float) (numFrames - 1);
    const int f0 = juce::jlimit (0, numFrames - 1, (int) fpos);
    const int f1 = juce::jmin (numFrames - 1, f0 + 1);
    const float mix = fpos - (float) f0;

    const float y = readFrame (f0, rp) * (1.0f - mix) + readFrame (f1, rp) * mix;

    const bool envConnected = isInputConnected (2);
    const float gain = gate.next (v, envConnected, inputs[2][0], sampleRate);

    const float out = y * gain * param (pVolume);
    outputs[0][0] = out;
    outputs[0][1] = out;
}

static ModuleDescriptor wavetableDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.wavetable";
    d.displayName = "Wavetable";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 15;
    d.sockets = {
        modIn    ("fmIn",      "FM In"),
        modIn    ("posIn",     "Pos In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",   "Volume",   0.0f, 1.0f, 0.8f, 0),
        makeCombo  ("frameSize", "Frame",   { "256", "512", "1024", "2048" }, 3, 0, 2),
        makeRotary ("position", "Position", 0.0f, 100.0f, 0.0f, 0, "%"),
        makeRotary ("posMod",   "Pos Mod",  -100.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("warp",     "Warp",     0.0f, 100.0f, 0.0f, 1, "%"),
        makeRotary ("voices",   "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 1, {}, false, 1.0f).noMod(),
        makeRotary ("glide",    "Glide",    0.0f, 1000.0f, 0.0f, 2, "ms").visibleWhen ("voices", 1.0f),
        makeButton ("loadSample", "Load", 0, 3)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (WavetableModule, wavetableDescriptor)
