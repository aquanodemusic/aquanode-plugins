#include "ModuleRegistry.h"

// Each AQUANODE_REGISTER_MODULE(ClassName, ...) defines an external "C"
// registration symbol named  ClassName##_aqRegistered . Every one of them is
// declared and odr-used below, which forces the linker to keep each module's
// translation unit (and therefore run its registration) - MSVC otherwise
// discards them and the module list comes up empty.
//
// To add a module: add its class name to the matching list. Nothing else
// outside the module's own .h/.cpp needs touching.

#define AQUANODE_CORE_MODULES(X) \
    X (AudioInModule) \
    X (AudioOutModule) \
    X (OscillatorModule) \
    X (SamplerModule) \
    X (SVFFilterModule) \
    X (LadderFilterModule) \
    X (DelayModule) \
    X (ReverbModule) \
    X (ChorusModule) \
    X (PhaserModule) \
    X (FlangerModule) \
    X (LFOModule) \
    X (ADSRModule) \
    X (SplitterModule) \
    X (BandSplitterModule) \
    X (VolumeModule) \
    X (PolySumModule) \
    X (PanningModule) \
    X (GranulateModule) \
    X (GranulationModule) \
    X (RecordModule) \
    X (ADelaySRModule) \
    X (AquatonReverbModule) \
    X (SpringerModule) \
    X (LiquidChorModule) \
    X (ResonateModule) \
    X (CenterCombModule) \
    X (PrecisionUtilityModule) \
    X (KeyTrackModule) \
    X (VelocityModule) \
    X (GateModule) \
    X (NoiseModule) \
    X (SlewModule) \
    X (RingModModule) \
    X (WaveshaperModule) \
    X (MixerModule) \
    X (ClockModule) \
    X (StepSeqModule) \
    X (EuclidModule) \
    X (SampleHoldModule) \
    X (QuantizerModule) \
    X (LogicModule) \
    X (EnvFollowModule) \
    X (CrossfadeModule) \
    X (CompressorModule) \
    X (KickModule) \
    X (ModalDrumModule) \
    X (PluckModule) \
    X (HatsModule) \
    X (AnyFMModule) \
    X (PitchEQModule) \
    X (VocoderModule) \
    X (SpecFilterModule) \
    X (SpecGateModule) \
    X (SpecEnhanceModule) \
    X (SpecDelayModule) \
    X (SpecMorphModule) \
    X (SpecFreezeModule) \
    X (CepstralMorphModule) \
    X (BitcrushModule) \
    X (SRReduceModule) \
    X (MidiAddModule) \
    X (DCOffsetModule) \
    X (DawModModule) \
    X (SnareModule) \
    X (ClapModule) \
    X (PhaseDistortModule) \
    X (ComplexOscModule) \
    X (UnisonModule) \
    X (FormantModule) \
    X (BowedModule) \
    X (WindModule) \
    X (WavetableModule) \
    X (AdditiveModule) \
    X (ArpModule) \
    X (AlwaysMidiModule) \
    X (StereoWidthModule) \
    X (AnalogDriftModule) \
    X (AudioThruModule) \
    X (LimiterModule) \
    X (DiscardMidiModule) \
    X (KeyboardMidiModule) \
    X (PianoRollModule) \
    X (LowpassModule) \
    X (HighpassModule) \
    X (NotchModule) \
    X (BellModule) \
    X (MidiControlsModule) \
    X (CurveCVModule) \
    X (TranswaveModule) \
    X (DXOperatorModule) \
    X (DrawLFOModule) \
    X (BellEQModule) \
    X (CombFilterModule) \
    X (FormantFilterModule) \
    X (TapeWobbleModule) \
    X (AquaFilterModule) \
    X (DropletsModule) \
    X (ChordWavetableModule) \
    X (MultiLFOModule) \
    X (ThreeBandCompModule) \
    X (DXPitchEGModule) \
    X (DXLFOModule) \
    X (DXEnvelopeModule) \
    X (ResonateBankModule) \
    X (SpectralLockModule) \
    X (IRForgeModule) \
    X (FeltPianoModule) \
    X (MidPianoModule) \
    X (HardPianoModule) \
    X (WarpOscModule) \
    X (CurveEnvModule) \
    X (RandomLFOModule) \
    X (EchoModule) \
    X (ShelfEQModule) \
    X (DualVcoModule) \
    X (DualFilterModule) \
    X (SkewLFOModule) \
    X (AnalogEGModule) \
    X (PWMOscModule) \
    X (WarmLadderModule) \
    X (EnsembleModule) \
    X (StereoPhaserModule) \
    X (CrossDelayModule) \
    X (SmoothHallModule)

// The GUI versions of the plugins merged into Aquanode Modular (src/Plugins)
#define AQUANODE_PLUGIN_GUI_MODULES(X) \
    X (AquanovaGuiModule) \
    X (AquaVibrioGuiModule) \
    X (AlphaBetaGuiModule) \
    X (PhizmoGuiModule) \
    X (PhizmOscGuiModule) \
    X (VirtualDX7GuiModule) \
    X (NoteAdderGuiModule) \
    X (SoundscapeGuiModule) \
    X (SlicerGuiModule) \
    X (CombWeaveGuiModule) \
    X (GrainFreezeGuiModule) \
    X (SampleFieldGuiModule) \
    X (ChorusVerbGuiModule) \
    X (CenterCombGuiModule) \
    X (FlowEQGuiModule) \
    X (TapElectricGuiModule) \
    X (VocodeGuiModule) \
    X (AlphaBetaFXGuiModule) \
    X (AutoMorphEQGuiModule) \
    X (FullFilterGuiModule) \
    X (PhorestGuiModule)

// The patch versions' modules ("PluginName - Kind", src/Plugins/<Name>)
#define AQUANODE_PLUGIN_PATCH_MODULES(X) \
    X (ChorusVerbChorusModule) \
    X (ChorusVerbDelayModule) \
    X (ChorusVerbPhaserModule) \
    X (ChorusVerbReverbModule) \
    X (TapElectricHumModule) \
    X (TapElectricNoiseModule) \
    X (TapElectricTapeInModule) \
    X (CenterCombBankModule) \
    X (FlowEQBellModule) \
    X (FlowEQCurveModule) \
    X (VocodeVocoderModule) \
    X (AlphaBetaOscModule) \
    X (AlphaBetaDriveModule) \
    X (AlphaBetaFilterModule) \
    X (AlphaBetaEnvModule) \
    X (AlphaBetaLfoModule) \
    X (AlphaBetaMatrixModule) \
    X (AlphaBetaAmpModule) \
    X (AlphaBetaChorusModule) \
    X (AlphaBetaOutputModule) \
    X (AquanovaOscModule) \
    X (AquanovaNoiseModule) \
    X (AquanovaRingModule) \
    X (AquanovaMixerModule) \
    X (AquanovaFilterModule) \
    X (AquanovaAmpModule) \
    X (AquanovaEnvModule) \
    X (AquanovaLfoModule) \
    X (AquanovaVocoderModule) \
    X (AquanovaEqModule) \
    X (AquanovaDistortionModule) \
    X (AquanovaCombModule) \
    X (AquanovaChorusModule) \
    X (AquanovaDelayModule) \
    X (AquanovaReverbModule) \
    X (AquanovaFxMixModule) \
    X (AquanovaPannerModule) \
    X (AquanovaOutputModule) \
    X (AquanovaArpModule) \
    X (PhizmOscOscModule) \
    X (PhizmOscScannerModule) \
    X (PhizmOscMixerModule) \
    X (PhizmOscFilterModule) \
    X (PhizmOscBusFilterModule) \
    X (PhizmOscEnvModule) \
    X (PhizmOscPitchEnvModule) \
    X (PhizmOscLfoModule) \
    X (PhizmOscBusModule) \
    X (PhizmOscChorusModule) \
    X (PhizmOscReverbModule) \
    X (NoteAdderFilterModule) \
    X (NoteAdderChordModule) \
    X (NoteAdderTransposeModule) \
    X (SlicerPlayerModule) \
    X (VDX7FunctionModule) \
    X (VDX7ChordModule) \
    X (PhizmoSoundModule) \
    X (AquaVibrioVoiceModule) \
    X (AquaVibrioEffectsModule) \
    X (SoundscapeSlotModule) \
    X (SoundscapeHabitatModule) \
    X (SoundscapeOutputModule) \
    X (PhizmoBusModule) \
    X (PhizmoInsertModule) \
    X (PhizmoReverbModule) \
    X (PhizmoOutputModule) \
    X (CombWeaveOscModule) \
    X (CombWeaveOutputModule) \
    X (GrainFreezePlayerModule) \
    X (SampleFieldModule) \
    X (AlphaBetaFXFilterModule) \
    X (AlphaBetaFXChorusModule) \
    X (AutoMorphEQBandModule) \
    X (AutoMorphEQOutputModule) \
    X (FullFilterBankModule) \
    X (PhorestPhaserModule)

#define AQUANODE_DECLARE_REGISTRATION(ClassName) extern "C" bool ClassName##_aqRegistered;
AQUANODE_CORE_MODULES (AQUANODE_DECLARE_REGISTRATION)
AQUANODE_PLUGIN_GUI_MODULES (AQUANODE_DECLARE_REGISTRATION)
AQUANODE_PLUGIN_PATCH_MODULES (AQUANODE_DECLARE_REGISTRATION)

namespace aquanode
{

int forceLinkAllModules()
{
    // volatile so the compiler cannot fold these references away
    volatile bool sink = false;

   #define AQUANODE_TOUCH_REGISTRATION(ClassName) sink = sink ^ ClassName##_aqRegistered;
    AQUANODE_CORE_MODULES (AQUANODE_TOUCH_REGISTRATION)
    AQUANODE_PLUGIN_GUI_MODULES (AQUANODE_TOUCH_REGISTRATION)
    AQUANODE_PLUGIN_PATCH_MODULES (AQUANODE_TOUCH_REGISTRATION)
   #undef AQUANODE_TOUCH_REGISTRATION

    return sink ? 1 : 0;
}

} // namespace aquanode
