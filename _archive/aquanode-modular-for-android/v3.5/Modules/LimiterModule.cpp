#include "LimiterModule.h"

static aquanode::ModuleDescriptor limiterDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "fx.limiter";
    d.displayName = "Limiter";
    d.description =
        "A fast stereo-linked peak limiter: instant attack, so nothing gets through above the "
        "Ceiling, plus a hard clamp as a safety net. Zero latency by design - a lookahead stage "
        "would add delay the modular graph cannot compensate for, so this is a brickwall clamp "
        "rather than a transparent mastering limiter.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 21;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("gain",    "Gain",    0.0f, 24.0f, 0.0f, 0, "dB"),
        makeRotary ("ceiling", "Ceiling", -30.0f, 0.0f, -0.3f, 0, "dB"),
        makeRotary ("release", "Release", 1.0f, 1000.0f, 80.0f, 0, "ms", true)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LimiterModule, limiterDescriptor)
