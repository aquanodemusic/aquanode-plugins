#include "ModuleRegistry.h"

// Each AQUANODE_REGISTER_MODULE(ClassName, ...) defines an external "C"
// registration symbol named  ClassName##_aqRegistered . We declare all of
// them here and odr-use every one, which forces the linker to keep each
// module's translation unit (and therefore run its registration).
//
// To add a new module: add one extern line + one touch() line below. That's
// the only bookkeeping needed anywhere outside the module's own .h/.cpp pair.

extern "C" bool VirusOscModule_aqRegistered;
extern "C" bool VirusEnvModule_aqRegistered;
extern "C" bool VirusLfoModule_aqRegistered;
extern "C" bool VirusDistortionModule_aqRegistered;
extern "C" bool BellEQModule_aqRegistered;
extern "C" bool GranulationModule_aqRegistered;
extern "C" bool EnvFollowModule_aqRegistered;
extern "C" bool StereoWidthModule_aqRegistered;
extern "C" bool RingModModule_aqRegistered;
extern "C" bool ChorusModule_aqRegistered;
extern "C" bool WaveshaperModule_aqRegistered;
extern "C" bool PhaserModule_aqRegistered;
extern "C" bool VocoderModule_aqRegistered;
extern "C" bool ArpModule_aqRegistered;
extern "C" bool FormantFilterModule_aqRegistered;
extern "C" bool AquaFilterModule_aqRegistered;
extern "C" bool ReverbModule_aqRegistered;
extern "C" bool ADelaySRModule_aqRegistered;
extern "C" bool DelayModule_aqRegistered;
extern "C" bool WavetableModule_aqRegistered;
extern "C" bool CombFilterModule_aqRegistered;
extern "C" bool SVFFilterModule_aqRegistered;
extern "C" bool TapeWobbleModule_aqRegistered;
extern "C" bool AnalogDriftModule_aqRegistered;
extern "C" bool OscillatorModule_aqRegistered;
extern "C" bool LFOModule_aqRegistered;
extern "C" bool NoiseModule_aqRegistered;
extern "C" bool ADSRModule_aqRegistered;

namespace aquanode
{

int forceLinkAllModules()
{
    // volatile so the compiler cannot fold these references away
    volatile bool sink = false;

    sink ^= VirusOscModule_aqRegistered;
    sink ^= VirusEnvModule_aqRegistered;
    sink ^= VirusLfoModule_aqRegistered;
    sink ^= VirusDistortionModule_aqRegistered;
    sink ^= BellEQModule_aqRegistered;
    sink ^= EnvFollowModule_aqRegistered;
    sink ^= StereoWidthModule_aqRegistered;
    sink ^= RingModModule_aqRegistered;
    sink ^= ChorusModule_aqRegistered;
    sink ^= WaveshaperModule_aqRegistered;
    sink ^= PhaserModule_aqRegistered;
    sink ^= VocoderModule_aqRegistered;
    sink ^= ArpModule_aqRegistered;
    sink ^= FormantFilterModule_aqRegistered;
    sink ^= AquaFilterModule_aqRegistered;
    sink ^= ReverbModule_aqRegistered;
    sink ^= ADelaySRModule_aqRegistered;
    sink ^= DelayModule_aqRegistered;
    sink ^= WavetableModule_aqRegistered;
    sink ^= CombFilterModule_aqRegistered;
    sink ^= SVFFilterModule_aqRegistered;
    sink ^= TapeWobbleModule_aqRegistered;
    sink ^= GranulationModule_aqRegistered;
    sink ^= AnalogDriftModule_aqRegistered;
    sink ^= OscillatorModule_aqRegistered;
    sink ^= LFOModule_aqRegistered;
    sink ^= NoiseModule_aqRegistered;
    sink ^= ADSRModule_aqRegistered;

    return sink ? 1 : 0;
}

} // namespace aquanode
