#include "ClockModule.h"

using namespace aquanode;

void ClockModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // reset edge restarts the pair cycle
    const float resetIn = inputs[0][0];
    if (resetIn > 0.5f && lastReset <= 0.5f)
        pairPhase = 0.0;
    lastReset = resetIn;

    const double bpm = tempoBpm > 1.0 ? tempoBpm : 120.0;
    const double pulseSeconds = (60.0 / bpm) * seqDivisionBeats ((int) param (pDivision));
    const double inc = 1.0 / (2.0 * pulseSeconds * sampleRate);   // phase spans TWO pulses

    pairPhase += inc;
    if (pairPhase >= 1.0)
        pairPhase -= std::floor (pairPhase);

    // pulse A starts at 0, pulse B is swing-delayed within the second half
    const double swing = param (pSwing) * 0.01 * 0.5;             // 0..0.375 of pair
    const double width = param (pWidth) * 0.01 * 0.5;             // gate width in pair units
    const double bStart = 0.5 + swing;

    const bool high = (pairPhase < width)
                   || (pairPhase >= bStart && pairPhase < bStart + width);

    // 0.5 ms anti-click ramp (same scheme as the Gate module)
    const float step = (float) (1.0 / (0.0005 * sampleRate));
    const float target = high ? 1.0f : 0.0f;
    if (level < target)      level = juce::jmin (target, level + step);
    else if (level > target) level = juce::jmax (target, level - step);

    outputs[0][0] = level;
    outputs[0][1] = level;
}

static ModuleDescriptor clockDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.clock";
    d.displayName = "Clock";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 13;
    d.sockets = {
        modIn  ("resetIn", "Reset In"),
        modOut ("gateOut", "Gate Out")
    };
    d.params = {
        makeSteppedList ("division", "Division", seqDivisionChoices(), 8, 0),
        makeRotary ("swing", "Swing", 0.0f, 75.0f, 0.0f, 0, "%"),
        makeRotary ("width", "Width", 5.0f, 95.0f, 50.0f, 0, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ClockModule, clockDescriptor)
