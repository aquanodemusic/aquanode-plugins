#include "DXLFOModule.h"
#include "DXDisplays.h"

using namespace aquanode;
namespace dx = dxnative;

void DXLFOModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        held[v] = false;
    phase = age = fadeGain = 0.0f;
    lastVoice = kMaxVoices;
}

void DXLFOModule::voiceNoteOn (int v, int note, bool retrigger)
{
    juce::ignoreUnused (note, retrigger);

    // the delay counts from the first key of a phrase: a key pressed while
    // no other key is held restarts it, a key added to a chord does not
    bool anyHeld = false;
    for (int i = 0; i < kMaxVoices; ++i)
        anyHeld = anyHeld || (i != v && held[i]);
    if (! anyHeld)
        age = 0.0f;

    // LFO Key Sync: every key restarts the wave from the top of its cycle
    if (param (pKeySync) > 0.5f)
        phase = 0.0f;

    held[v] = true;
}

float DXLFOModule::waveValue() const
{
    const float p = phase;
    switch (juce::jlimit (0, 5, (int) std::lround (param (pWave))))
    {
        case 0:  return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);   // triangle
        case 1:  return 1.0f - 2.0f * p;                                       // saw down
        case 2:  return 2.0f * p - 1.0f;                                       // saw up
        case 3:  return (p < 0.5f) ? 1.0f : -1.0f;                             // square
        case 4:  return std::sin (dx::kTwoPi * p);                             // sine
        default: return sampleHold;                                            // S/H
    }
}

void DXLFOModule::advanceShared()
{
    const float dt = (float) (1.0 / sampleRate);
    phase += dx::lfoSpeedToHz ((int) std::lround (param (pSpeed))) * dt;
    while (phase >= 1.0f)
    {
        phase -= 1.0f;
        // sample and hold: a fresh random value every cycle
        rng = rng * 1664525u + 1013904223u;
        sampleHold = (float) ((rng >> 9) & 0xFFFF) / 32767.5f - 1.0f;
    }

    // delay: hold the LFO out of circuit, then fade it in
    float hold = 0.0f, fade = 0.0f;
    dx::lfoDelayTimes ((int) std::lround (param (pDelay)), hold, fade);
    age += dt;
    if (age < hold)          fadeGain = 0.0f;
    else if (fade <= 0.0f)   fadeGain = 1.0f;
    else                     fadeGain = juce::jmin (1.0f, (age - hold) / fade);
}

void DXLFOModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // every active voice is processed once per sample, in ascending order:
    // the first one of a sample moves the shared wave on
    if (v <= lastVoice)
        advanceShared();
    lastVoice = v;

    const float w = waveValue() * fadeGain;

    // PMD, plus the wheel (a controller assigned to pitch, DX7 style)
    const float pmd = juce::jlimit (0.0f, 1.0f, param (pPmd) / 99.0f + juce::jmax (0.0f, inputs[0][0]));
    const float pitchSemis = w * pmd * dx::pmsSemitones ((int) std::lround (param (pPms)));

    // AMD is strongly non-linear on the DX7: half travel is about a seventh.
    // A controller assigned to AMP adds to the depth (Amp In); EG BIAS holds
    // the AMS operators down by a steady amount (Hold In) - whichever holds
    // an operator down further wins, as in VirtualDX7's native engine.
    const float amd = juce::jlimit (0.0f, 1.0f, std::pow (juce::jlimit (0.0f, 99.0f, param (pAmd)) / 99.0f, 2.9f)
                                                + juce::jmax (0.0f, inputs[1][0]));
    const float am = juce::jmax (juce::jlimit (0.0f, 1.0f, (waveValue() * 0.5f + 0.5f) * fadeGain) * amd,
                                 juce::jlimit (0.0f, 1.0f, inputs[2][0]));

    outputs[0] = { pitchSemis / 60.0f, pitchSemis / 60.0f };
    outputs[1] = { am, am };
    outputs[2] = { w, w };
}

std::unique_ptr<juce::Component> DXLFOModule::createExtraContentComponent()
{
    return std::make_unique<dxui::LfoView> (*this);
}

//==============================================================================
static ModuleDescriptor dxLFODescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.dxlfo";
    d.displayName = "DX LFO";
    d.description =
        "The DX7's LFO, shared by every note: Speed and Delay follow the hardware's curves, Delay counts "
        "from the first key of a phrase and Key Sync restarts the wave on every key. Pitch Out (wave x PMD "
        "x PMS) goes to the DX Operators' Pitch In, AM Out (x AMD) to their AM In, where each operator's AMS "
        "sets the depth. Wheel In adds to PMD - patch a Midi Controls' Wheel Out for mod-wheel vibrato.";
    d.section = ModuleSection::Utility;
    d.family = ModuleFamily::DX;
    d.sidebarOrder = 41;
    d.sockets = {
        modIn  ("wheelIn",  "Wheel In"),
        modIn  ("ampIn",    "Amp In"),
        modIn  ("holdIn",   "Hold In"),
        modOut ("pitchOut", "Pitch Out"),
        modOut ("amOut",    "AM Out"),
        modOut ("lfoOut",   "LFO Out")
    };

    juce::StringArray pms;
    for (int i = 0; i <= 7; ++i) pms.add (juce::String (i));

    d.params = {
        makeCombo ("wave", "Wave", { "Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S/H" }, 0, 0, 2),
        makeRotary ("speed", "Speed", 0.0f, 99.0f, 35.0f, 0, {}, false, 1.0f),
        makeRotary ("delay", "Delay", 0.0f, 99.0f, 0.0f, 0, {}, false, 1.0f),
        makeRotary ("pmd", "PMD", 0.0f, 99.0f, 0.0f, 1, {}, false, 1.0f),
        makeRotary ("amd", "AMD", 0.0f, 99.0f, 0.0f, 1, {}, false, 1.0f),
        makeSteppedList ("pms", "PMS", pms, 3, 1),
        makeCombo ("keySync", "Key Sync", { "Free Run", "Key Sync" }, 1, 1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DXLFOModule, dxLFODescriptor)
