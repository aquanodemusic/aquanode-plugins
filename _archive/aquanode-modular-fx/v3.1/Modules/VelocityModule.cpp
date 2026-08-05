#include "VelocityModule.h"

static aquanode::ModuleDescriptor velocityDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.velocity";
    d.displayName = "Velocity";
    d.description =
        "Outputs each voice's note-on velocity as a unipolar 0..1 signal. Into an Oscillator's "
        "Env In for touch-sensitive levels, or onto any knob for velocity-controlled brightness.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 6;
    d.sockets = { modOut ("modOut", "Mod Out") };
    return d;
}

AQUANODE_REGISTER_MODULE (VelocityModule, velocityDescriptor)
