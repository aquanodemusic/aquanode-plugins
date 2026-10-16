#include "MeasuredEnvModule.h"

namespace aquavibrio
{

using namespace aquanode;

static ModuleDescriptor measuredEnvDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.measuredenv";
    d.displayName = "Measured Envelope";
    d.description =
        "The Original engine's envelope: linear attack, exponential (dB-linear) decay and release, "
        "and a sustain that drifts at a set rate in dB per second.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 2;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeRotary ("attack",  "Attack",  0.05f, 60000.0f, 5.0f, 0, "ms", true),
        makeRotary ("decay",   "Decay",   0.01f, 100000.0f, 25.0f, 0, "dB/s", true),
        makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 1.0f, 0),
        makeRotary ("release", "Release", 0.01f, 100000.0f, 25.0f, 0, "dB/s", true),
        makeRotary ("sustainTime", "Sustain Time", -100000.0f, 100000.0f, 0.0f, 1, "dB/s"),
        makeRotary ("modulator", "Modulator", 0.0f, 1.0f, 0.0f, 0)
    };
    return d;
}

} // namespace aquavibrio

using MeasuredEnvModule = aquavibrio::MeasuredEnvModule;
AQUANODE_REGISTER_MODULE (MeasuredEnvModule, aquavibrio::measuredEnvDescriptor)
