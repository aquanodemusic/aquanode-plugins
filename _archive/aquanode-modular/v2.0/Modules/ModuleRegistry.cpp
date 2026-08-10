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
extern "C" bool GranulateModule_aqRegistered;
extern "C" bool ADelaySRModule_aqRegistered;
extern "C" bool AquatonReverbModule_aqRegistered;
extern "C" bool SpringerModule_aqRegistered;
extern "C" bool LiquidChorModule_aqRegistered;
extern "C" bool ResonateModule_aqRegistered;
extern "C" bool CenterCombModule_aqRegistered;
extern "C" bool PrecisionUtilityModule_aqRegistered;
extern "C" bool KeyTrackModule_aqRegistered;
extern "C" bool VelocityModule_aqRegistered;
extern "C" bool GateModule_aqRegistered;
extern "C" bool NoiseModule_aqRegistered;
extern "C" bool SlewModule_aqRegistered;
extern "C" bool RingModModule_aqRegistered;
extern "C" bool WaveshaperModule_aqRegistered;
extern "C" bool MixerModule_aqRegistered;
extern "C" bool ClockModule_aqRegistered;
extern "C" bool StepSeqModule_aqRegistered;
extern "C" bool EuclidModule_aqRegistered;
extern "C" bool SampleHoldModule_aqRegistered;
extern "C" bool QuantizerModule_aqRegistered;
extern "C" bool LogicModule_aqRegistered;
extern "C" bool EnvFollowModule_aqRegistered;
extern "C" bool CrossfadeModule_aqRegistered;
extern "C" bool CompressorModule_aqRegistered;
extern "C" bool KickModule_aqRegistered;
extern "C" bool ModalDrumModule_aqRegistered;
extern "C" bool PluckModule_aqRegistered;
extern "C" bool HatsModule_aqRegistered;
extern "C" bool AnyFMModule_aqRegistered;
extern "C" bool PitchEQModule_aqRegistered;
extern "C" bool VocoderModule_aqRegistered;
extern "C" bool SpecFilterModule_aqRegistered;
extern "C" bool SpecGateModule_aqRegistered;
extern "C" bool SpecEnhanceModule_aqRegistered;
extern "C" bool SpecDelayModule_aqRegistered;
extern "C" bool SpecMorphModule_aqRegistered;
extern "C" bool BitcrushModule_aqRegistered;
extern "C" bool SRReduceModule_aqRegistered;
extern "C" bool MidiAddModule_aqRegistered;
extern "C" bool DCOffsetModule_aqRegistered;
extern "C" bool DawModModule_aqRegistered;
extern "C" bool SnareModule_aqRegistered;
extern "C" bool ClapModule_aqRegistered;
extern "C" bool PhaseDistortModule_aqRegistered;
extern "C" bool ComplexOscModule_aqRegistered;
extern "C" bool UnisonModule_aqRegistered;
extern "C" bool FormantModule_aqRegistered;
extern "C" bool BowedModule_aqRegistered;
extern "C" bool WavetableModule_aqRegistered;
extern "C" bool AdditiveModule_aqRegistered;
extern "C" bool ArpModule_aqRegistered;
extern "C" bool AlwaysMidiModule_aqRegistered;
extern "C" bool SnareModule_aqRegistered;
extern "C" bool ClapModule_aqRegistered;
extern "C" bool PhaseDistortModule_aqRegistered;
extern "C" bool ComplexOscModule_aqRegistered;
extern "C" bool UnisonModule_aqRegistered;
extern "C" bool FormantModule_aqRegistered;
extern "C" bool BowedModule_aqRegistered;
extern "C" bool WavetableModule_aqRegistered;
extern "C" bool AdditiveModule_aqRegistered;
extern "C" bool ArpModule_aqRegistered;

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
    sink ^= GranulateModule_aqRegistered;
    sink ^= ADelaySRModule_aqRegistered;
    sink ^= AquatonReverbModule_aqRegistered;
    sink ^= SpringerModule_aqRegistered;
    sink ^= LiquidChorModule_aqRegistered;
    sink ^= ResonateModule_aqRegistered;
    sink ^= CenterCombModule_aqRegistered;
    sink ^= PrecisionUtilityModule_aqRegistered;
    sink ^= KeyTrackModule_aqRegistered;
    sink ^= VelocityModule_aqRegistered;
    sink ^= GateModule_aqRegistered;
    sink ^= NoiseModule_aqRegistered;
    sink ^= SlewModule_aqRegistered;
    sink ^= RingModModule_aqRegistered;
    sink ^= WaveshaperModule_aqRegistered;
    sink ^= MixerModule_aqRegistered;
    sink ^= ClockModule_aqRegistered;
    sink ^= StepSeqModule_aqRegistered;
    sink ^= EuclidModule_aqRegistered;
    sink ^= SampleHoldModule_aqRegistered;
    sink ^= QuantizerModule_aqRegistered;
    sink ^= LogicModule_aqRegistered;
    sink ^= EnvFollowModule_aqRegistered;
    sink ^= CrossfadeModule_aqRegistered;
    sink ^= CompressorModule_aqRegistered;
    sink ^= KickModule_aqRegistered;
    sink ^= ModalDrumModule_aqRegistered;
    sink ^= PluckModule_aqRegistered;
    sink ^= HatsModule_aqRegistered;
    sink ^= AnyFMModule_aqRegistered;
    sink ^= PitchEQModule_aqRegistered;
    sink ^= VocoderModule_aqRegistered;
    sink ^= SpecFilterModule_aqRegistered;
    sink ^= SpecGateModule_aqRegistered;
    sink ^= SpecEnhanceModule_aqRegistered;
    sink ^= SpecDelayModule_aqRegistered;
    sink ^= SpecMorphModule_aqRegistered;
    sink ^= BitcrushModule_aqRegistered;
    sink ^= SRReduceModule_aqRegistered;
    sink ^= MidiAddModule_aqRegistered;
    sink ^= DCOffsetModule_aqRegistered;
    sink ^= DawModModule_aqRegistered;
    sink ^= SnareModule_aqRegistered;
    sink ^= ClapModule_aqRegistered;
    sink ^= PhaseDistortModule_aqRegistered;
    sink ^= ComplexOscModule_aqRegistered;
    sink ^= UnisonModule_aqRegistered;
    sink ^= FormantModule_aqRegistered;
    sink ^= BowedModule_aqRegistered;
    sink ^= WavetableModule_aqRegistered;
    sink ^= AdditiveModule_aqRegistered;
    sink ^= ArpModule_aqRegistered;
    sink ^= AlwaysMidiModule_aqRegistered;
    sink ^= SnareModule_aqRegistered;
    sink ^= ClapModule_aqRegistered;
    sink ^= PhaseDistortModule_aqRegistered;
    sink ^= ComplexOscModule_aqRegistered;
    sink ^= UnisonModule_aqRegistered;
    sink ^= FormantModule_aqRegistered;
    sink ^= BowedModule_aqRegistered;
    sink ^= WavetableModule_aqRegistered;
    sink ^= AdditiveModule_aqRegistered;
    sink ^= ArpModule_aqRegistered;

    return sink ? 1 : 0;
}

} // namespace aquanode
