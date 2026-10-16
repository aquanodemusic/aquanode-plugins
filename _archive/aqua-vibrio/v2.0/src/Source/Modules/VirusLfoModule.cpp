#include "VirusLfoModule.h"

namespace aquavibrio
{

using namespace aquanode;

static ModuleDescriptor virusLfoDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.viruslfo";
    d.displayName = "Virus LFO";
    d.description =
        "An LFO with the Virus's full shape list - the usual five plus the oscillator wave table - "
        "and the four controls that make it behave like the hardware's: Symmetry skews the cycle "
        "itself, Mode shares one phase across voices or gives each its own, Keytrigger restarts it "
        "at a chosen phase, and Env Mode runs a single cycle so the LFO becomes an extra envelope.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 2;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeRotary ("rate", "Rate", 0.01f, 200.0f, 2.0f, 0, "Hz", true),
        makeRotary ("shape", "Shape", 0.0f, 67.0f, 0.0f, 0, {}, false, 1.0f),
        makeRotary ("symmetry", "Symmetry", -0.95f, 0.95f, 0.0f, 1),
        makeCombo  ("mode", "Mode", { "Poly", "Mono" }, 0, 1, 2),
        makeCombo  ("envMode", "Env Mode", { "Off", "One Shot" }, 0, 1, 2),
        makeRotary ("keytrigger", "Keytrigger", -1.0f, 1.0f, -1.0f, 2),
        makeRotary ("phaseSpread", "Phase Spread", 0.0f, 1.0f, 0.0f, 2)
    };
    return d;
}

} // namespace aquavibrio

// The registration macro expands to an `extern "C"` variable, which has to
// sit at global scope - inside a namespace MSVC rejects it, and the error it
// reports points at <xlocale> rather than here. The vendored modules all do
// the same thing; they just have no namespace to be inside.
using VirusLfoModule = aquavibrio::VirusLfoModule;
AQUANODE_REGISTER_MODULE (VirusLfoModule, aquavibrio::virusLfoDescriptor)
