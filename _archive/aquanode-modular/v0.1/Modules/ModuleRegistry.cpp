#include "ModuleRegistry.h"

// Each AQUANODE_REGISTER_MODULE(ClassName, ...) defines an external "C"
// registration symbol named  ClassName##_aqRegistered . We declare all of
// them here and odr-use every one, which forces the linker to keep each
// module's translation unit (and therefore run its registration).
//
// To add a new module: add one extern line + one touch() line below. That's
// the only bookkeeping needed anywhere outside the module's own .h/.cpp pair.

extern "C" bool AudioInModule_aqRegistered;
extern "C" bool AudioOutModule_aqRegistered;
extern "C" bool OscillatorModule_aqRegistered;
extern "C" bool SamplerModule_aqRegistered;
extern "C" bool SVFFilterModule_aqRegistered;
extern "C" bool LadderFilterModule_aqRegistered;
extern "C" bool DelayModule_aqRegistered;
extern "C" bool ReverbModule_aqRegistered;
extern "C" bool ChorusModule_aqRegistered;
extern "C" bool PhaserModule_aqRegistered;
extern "C" bool FlangerModule_aqRegistered;
extern "C" bool LFOModule_aqRegistered;
extern "C" bool ADSRModule_aqRegistered;
extern "C" bool SplitterModule_aqRegistered;
extern "C" bool VolumeModule_aqRegistered;
extern "C" bool PanningModule_aqRegistered;

namespace aquanode
{

int forceLinkAllModules()
{
    // volatile so the compiler cannot fold these references away
    volatile bool sink = false;

    sink ^= AudioInModule_aqRegistered;
    sink ^= AudioOutModule_aqRegistered;
    sink ^= OscillatorModule_aqRegistered;
    sink ^= SamplerModule_aqRegistered;
    sink ^= SVFFilterModule_aqRegistered;
    sink ^= LadderFilterModule_aqRegistered;
    sink ^= DelayModule_aqRegistered;
    sink ^= ReverbModule_aqRegistered;
    sink ^= ChorusModule_aqRegistered;
    sink ^= PhaserModule_aqRegistered;
    sink ^= FlangerModule_aqRegistered;
    sink ^= LFOModule_aqRegistered;
    sink ^= ADSRModule_aqRegistered;
    sink ^= SplitterModule_aqRegistered;
    sink ^= VolumeModule_aqRegistered;
    sink ^= PanningModule_aqRegistered;

    return sink ? 1 : 0;
}

} // namespace aquanode
