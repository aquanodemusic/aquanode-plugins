#pragma once

// A handful of starter patches, embedded as plain text so PresetManager can
// drop them into the user's presets folder the first time the plugin runs
// (see PresetManager::installFactoryPresetsIfMissing). They use the same
// human-readable <K5Preset> format documented in PresetManager.h, so they
// double as a worked example if you want to hand-author your own - copy one
// of the matching .xml files from the "factory presets" output folder into
// your presets directory (Settings > open presets folder) and edit away.
namespace FactoryPresets
{
    struct Preset { const char* name; const char* xml; };

    static const Preset all[] =
    {
        { "Init", R"(<K5Preset name="Init">
</K5Preset>
)" },

        { "Glass Bells", R"(<K5Preset name="Glass Bells">
  <PARAM id="s1_level" value="0.9"/>
  <PARAM id="s1_ampA" value="0.002"/>
  <PARAM id="s1_ampD" value="0.6"/>
  <PARAM id="s1_ampS" value="0.0"/>
  <PARAM id="s1_ampR" value="1.2"/>
  <PARAM id="s1_harmMode" value="3"/>
  <PARAM id="s1_harmTilt" value="0.4"/>
  <PARAM id="s1_cutoff" value="9000"/>
  <PARAM id="s1_resonance" value="0.25"/>
  <PARAM id="s1_filtEnvAmount" value="0.3"/>
  <PARAM id="s1_filtA" value="0.001"/>
  <PARAM id="s1_filtD" value="0.5"/>
  <PARAM id="s1_filtS" value="0.0"/>
  <PARAM id="s1_filtR" value="0.8"/>
  <PARAM id="s2_level" value="0.5"/>
  <PARAM id="s2_detune" value="7"/>
  <PARAM id="s2_ampA" value="0.002"/>
  <PARAM id="s2_ampD" value="0.9"/>
  <PARAM id="s2_ampS" value="0.0"/>
  <PARAM id="s2_ampR" value="1.5"/>
  <PARAM id="s2_harmMode" value="3"/>
  <PARAM id="s2_harmTilt" value="0.5"/>
  <PARAM id="s2_cutoff" value="11000"/>
  <PARAM id="s2_resonance" value="0.2"/>
  <PARAM id="dftBand2_gain" value="0.4"/>
  <PARAM id="dftBand5_gain" value="0.3"/>
  <PARAM id="lfoShape" value="0"/>
  <PARAM id="lfoRate" value="5"/>
  <PARAM id="lfoDelay" value="0.3"/>
  <PARAM id="lfoTremolo" value="0.1"/>
  <PARAM id="masterGain" value="0.7"/>
</K5Preset>
)" },

        { "Warm Growl Bass", R"(<K5Preset name="Warm Growl Bass">
  <PARAM id="s1_level" value="0.9"/>
  <PARAM id="s1_ampA" value="0.005"/>
  <PARAM id="s1_ampD" value="0.3"/>
  <PARAM id="s1_ampS" value="0.9"/>
  <PARAM id="s1_ampR" value="0.25"/>
  <PARAM id="s1_harmMode" value="1"/>
  <PARAM id="s1_harmTilt" value="-0.5"/>
  <PARAM id="s1_cutoff" value="600"/>
  <PARAM id="s1_resonance" value="0.35"/>
  <PARAM id="s1_filtEnvAmount" value="0.5"/>
  <PARAM id="s1_slope24" value="1"/>
  <PARAM id="s1_filtA" value="0.01"/>
  <PARAM id="s1_filtD" value="0.4"/>
  <PARAM id="s1_filtS" value="0.2"/>
  <PARAM id="s1_filtR" value="0.3"/>
  <PARAM id="s2_level" value="0.7"/>
  <PARAM id="s2_ampA" value="0.01"/>
  <PARAM id="s2_ampD" value="0.3"/>
  <PARAM id="s2_ampS" value="0.9"/>
  <PARAM id="s2_ampR" value="0.3"/>
  <PARAM id="s2_harmMode" value="3"/>
  <PARAM id="s2_harmTilt" value="-0.9"/>
  <PARAM id="s2_cutoff" value="300"/>
  <PARAM id="dftBand0_gain" value="0.3"/>
  <PARAM id="lfoShape" value="0"/>
  <PARAM id="lfoRate" value="3"/>
  <PARAM id="lfoDelay" value="0.2"/>
  <PARAM id="lfoFilterMod" value="0.5"/>
  <PARAM id="masterGain" value="0.75"/>
</K5Preset>
)" },

        { "Airy Formant Pad", R"(<K5Preset name="Airy Formant Pad">
  <PARAM id="s1_level" value="0.7"/>
  <PARAM id="s1_ampA" value="1.2"/>
  <PARAM id="s1_ampD" value="1.5"/>
  <PARAM id="s1_ampS" value="0.8"/>
  <PARAM id="s1_ampR" value="2.5"/>
  <PARAM id="s1_harmMode" value="0"/>
  <PARAM id="s1_harmTilt" value="0.1"/>
  <PARAM id="s1_cutoff" value="3000"/>
  <PARAM id="s1_resonance" value="0.15"/>
  <PARAM id="s1_filtEnvAmount" value="0.1"/>
  <PARAM id="s1_filtA" value="1.0"/>
  <PARAM id="s1_filtD" value="1.0"/>
  <PARAM id="s1_filtS" value="0.6"/>
  <PARAM id="s1_filtR" value="2.0"/>
  <PARAM id="s2_level" value="0.6"/>
  <PARAM id="s2_detune" value="9"/>
  <PARAM id="s2_ampA" value="1.4"/>
  <PARAM id="s2_ampD" value="1.6"/>
  <PARAM id="s2_ampS" value="0.75"/>
  <PARAM id="s2_ampR" value="2.8"/>
  <PARAM id="s2_harmMode" value="0"/>
  <PARAM id="s2_cutoff" value="2600"/>
  <PARAM id="dftBand1_gain" value="0.5"/>
  <PARAM id="dftBand3_gain" value="0.4"/>
  <PARAM id="dftBand6_gain" value="0.3"/>
  <PARAM id="lfoShape" value="0"/>
  <PARAM id="lfoRate" value="0.6"/>
  <PARAM id="lfoDelay" value="1.0"/>
  <PARAM id="lfoTremolo" value="0.15"/>
  <PARAM id="lfoFilterMod" value="0.2"/>
  <PARAM id="masterGain" value="0.65"/>
</K5Preset>
)" },

        { "Metallic Pluck", R"(<K5Preset name="Metallic Pluck">
  <PARAM id="s1_level" value="0.85"/>
  <PARAM id="s1_ampA" value="0.001"/>
  <PARAM id="s1_ampD" value="0.25"/>
  <PARAM id="s1_ampS" value="0.0"/>
  <PARAM id="s1_ampR" value="0.3"/>
  <PARAM id="s1_harmMode" value="4"/>
  <PARAM id="s1_harmTilt" value="0.2"/>
  <PARAM id="s1_cutoff" value="7000"/>
  <PARAM id="s1_resonance" value="0.6"/>
  <PARAM id="s1_filtEnvAmount" value="0.6"/>
  <PARAM id="s1_slope24" value="1"/>
  <PARAM id="s1_filtA" value="0.001"/>
  <PARAM id="s1_filtD" value="0.2"/>
  <PARAM id="s1_filtS" value="0.0"/>
  <PARAM id="s1_filtR" value="0.25"/>
  <PARAM id="s1_pitchDepth" value="3"/>
  <PARAM id="s1_pitchA" value="0.001"/>
  <PARAM id="s1_pitchD" value="0.05"/>
  <PARAM id="s1_pitchS" value="0.0"/>
  <PARAM id="s1_pitchR" value="0.05"/>
  <PARAM id="s2_level" value="0.5"/>
  <PARAM id="s2_detune" value="19"/>
  <PARAM id="s2_harmMode" value="4"/>
  <PARAM id="s2_harmTilt" value="0.3"/>
  <PARAM id="s2_cutoff" value="8500"/>
  <PARAM id="s2_resonance" value="0.5"/>
  <PARAM id="s2_ampA" value="0.001"/>
  <PARAM id="s2_ampD" value="0.3"/>
  <PARAM id="s2_ampS" value="0.0"/>
  <PARAM id="s2_ampR" value="0.35"/>
  <PARAM id="dftBand4_gain" value="0.35"/>
  <PARAM id="dftBand8_gain" value="0.3"/>
  <PARAM id="lfoRate" value="6"/>
  <PARAM id="lfoFilterMod" value="0.3"/>
  <PARAM id="masterGain" value="0.7"/>
</K5Preset>
)" },
    };
}
