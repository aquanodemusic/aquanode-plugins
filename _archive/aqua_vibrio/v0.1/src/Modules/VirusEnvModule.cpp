#include "VirusEnvModule.h"

namespace aquavibrio
{

using namespace aquanode;

static ModuleDescriptor virusEnvDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.virusenv";
    d.displayName = "Virus Envelope";
    d.description =
        "An ADSR whose sustain is not flat. Sustain Time runs from Fall through Infinite to Rise: "
        "at the centre the sustain holds, to the left it keeps decaying while the key is held, to "
        "the right it climbs back up. Pads that bloom and plucks that keep dying away both come "
        "from that one control.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 1;
    d.sockets = { modOut ("modOut", "Mod Out") };
    d.params = {
        makeRotary ("attack",  "Attack",  0.1f, 20000.0f, 10.0f,  0, "ms", true),
        makeRotary ("decay",   "Decay",   0.1f, 20000.0f, 100.0f, 0, "ms", true),
        makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 0.7f, 0),
        makeRotary ("release", "Release", 0.1f, 20000.0f, 300.0f, 0, "ms", true),
        makeRotary ("sustainTime", "Sustain Time", -1.0f, 1.0f, 0.0f, 1)
    };
    return d;
}

} // namespace aquavibrio

// The registration macro expands to an `extern "C"` variable, which has to
// sit at global scope - inside a namespace MSVC rejects it, and the error it
// reports points at <xlocale> rather than here. The vendored modules all do
// the same thing; they just have no namespace to be inside.
using VirusEnvModule = aquavibrio::VirusEnvModule;
AQUANODE_REGISTER_MODULE (VirusEnvModule, aquavibrio::virusEnvDescriptor)
