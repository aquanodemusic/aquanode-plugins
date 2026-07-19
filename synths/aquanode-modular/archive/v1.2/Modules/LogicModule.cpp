#include "LogicModule.h"

static aquanode::ModuleDescriptor logicDescriptor()
{
    using namespace aquanode;
    ModuleDescriptor d;
    d.typeId = "util.logic";
    d.displayName = "Logic";
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
