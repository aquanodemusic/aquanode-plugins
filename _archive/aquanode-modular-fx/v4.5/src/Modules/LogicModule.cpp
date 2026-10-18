#include "LogicModule.h"

static aquanode::ModuleDescriptor logicDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.logic";
    d.displayName = "Logic";
    d.description =
        "Combines two gate or mod signals with boolean and comparison operations, outputting a "
        "clean 0/1 that can gate audio directly. A In and B In want gates. A Clock AND a Euclid, "
        "or the XOR of two clocks, is where generative rhythms come from.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 18;
    d.sockets = {
        modIn  ("aIn",    "A In"),
        modIn  ("bIn",    "B In"),
        modOut ("modOut", "Mod Out")
    };
    d.params = {
        makeCombo ("op", "Operation",
                   { "AND", "OR", "XOR", "NAND", "NOR", "A > B", "NOT A" }, 0, 0, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (LogicModule, logicDescriptor)
