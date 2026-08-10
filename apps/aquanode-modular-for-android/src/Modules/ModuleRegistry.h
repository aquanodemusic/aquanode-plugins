#pragma once

namespace aquanode
{
    // Referencing this from the processor guarantees every module translation
    // unit is linked in, which in turn guarantees each module's registration
    // (defined via AQUANODE_REGISTER_MODULE) actually runs. Without a hard
    // reference like this, MSVC's linker discards the "unused" registration
    // objects and the module list comes up empty.
    int forceLinkAllModules();
}
