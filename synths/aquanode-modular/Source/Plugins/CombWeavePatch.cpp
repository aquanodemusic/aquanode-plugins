//==============================================================================
// CombWeave - Patch version
//
// CombWeave stacks sines on every note in two banks - "comb oscillators" of
// up to 128 partials spread linearly, exponentially or as harmonics, folded
// back into the audible range (Wrap / Mirror), each bank high-passed - and
// sums the banks into a hard limit. The patch version is that, module by
// module:
//
//   CombWeave - Comb Osc (oscillator 1) --+
//                                         +--> CombWeave - Output
//   CombWeave - Comb Osc (oscillator 2) --+
//
// Each Comb Osc plays one bank with CombWeave's own engine (its five voices,
// the partial layout, Note Lock, the harmonic filter and rolloff, attack and
// release, the bank's high-pass); its knobs are that bank's parameters and
// take cables. The Output module sums whatever is patched into it and clips
// at 0 dB, as the plugin does after its two banks.
//
// Where the patch differs: in the plugin the second bank takes the voice slot
// the first one chose, so a note that steals a slot of bank 1 also cuts a
// still-ringing note of bank 2. Here each bank keeps its own five voices.
//==============================================================================
#include "PluginCombWeaveConfig.h"

namespace aqp_combweave
{
    #include AQP_COMBWEAVE_SRC(PluginProcessor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "combweave" };
    using CW = aqp_combweave::CombWeaveAudioProcessor;

    ModuleDescriptor base (const juce::String& id, const juce::String& kind, ModuleSection s, int order)
    {
        ModuleDescriptor d;
        d.typeId = "plugin.combweave." + id;
        d.displayName = kit::moduleName (kPlugin, kind);
        d.section = s;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400 + order;
        return d;
    }

    // a bank's knobs: CombWeave's oscillator 1 ids (oscillator 2 adds "2")
    const char* const kBankIds[] = { "amount", "hpFreq", "spreadMode", "spreadHz", "spreadRatio", "volume",
                                     "bidirectional", "freqMode", "attack", "release", "rolloff", "tune",
                                     "noteLock", "harmonicFilter" };
}

//==============================================================================
// CombWeave - Comb Osc
//==============================================================================
class CombWeaveOscModule final : public kit::InstanceModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("osc", "Comb Osc", ModuleSection::Oscillator, 0);
        d.description = "One of CombWeave's two oscillator banks, played by the plugin's own engine: on every "
                        "note a fundamental and up to 128 sine partials, spread by Hz (Linear), by a ratio "
                        "(Exponential) or as the harmonic series, optionally mirrored below the note too "
                        "(Both Ways). Out-of-range partials are dropped (Regular) or folded back (Wrap, Mirror); "
                        "Note Lock snaps every partial to the nearest note. Five voices, attack / release, a "
                        "high-pass on the bank. Patch both banks into the CombWeave Output.";
        d.sockets = { midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("amount", "Partials", 0.0f, 128.0f, 7.0f, 0, {}, false, 1.0f),
            makeCombo ("spreadMode", "Spread", { "Linear", "Exponential", "Harmonics" }, 0, 0, 2),
            makeRotary ("spreadHz", "Spread Hz", 1.0f, 5000.0f, 110.0f, 0, "Hz", true).visibleWhen ("spreadMode", 0.0f),
            makeRotary ("spreadRatio", "Ratio", 1.001f, 4.0f, 2.0f, 0, "x", true).visibleWhen ("spreadMode", 1.0f),
            makeRotary ("rolloff", "Rolloff", 0.0f, 1.0f, 0.0f, 0),

            makeCombo ("freqMode", "Range", { "Regular", "Wrap", "Mirror" }, 0, 1, 2),
            makeCombo ("bidirectional", "Direction", { "Up", "Both Ways" }, 0, 1, 2),
            makeCombo ("harmonicFilter", "Partials Kept", { "All", "Evens Only", "Odds Only" }, 0, 1, 2),

            makeRotary ("tune", "Fine Tune", -100.0f, 100.0f, 0.0f, 2, "ct"),
            makeCombo ("noteLock", "Note Lock", { "Off", "On" }, 0, 2, 2),
            makeRotary ("attack", "Attack", 1.0f, 5000.0f, 10.0f, 2, "ms", true),
            makeRotary ("release", "Release", 1.0f, 5000.0f, 300.0f, 2, "ms", true),
            makeRotary ("hpFreq", "High-Pass", 20.0f, 2000.0f, 20.0f, 2, "Hz", true),
            makeRotary ("volume", "Volume", -60.0f, 12.0f, -12.0f, 2, "dB")
        };
        return d;
    }

    CombWeaveOscModule() : InstanceModule (kPlugin, true)
    {
        if (auto* cw = dynamic_cast<CW*> (plugin.get()))
            cw->osc1Only.store (true);
    }
};

//==============================================================================
// CombWeave - Output
//==============================================================================
class CombWeaveOutputModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        auto d = base ("output", "Output", ModuleSection::Utility, 1);
        d.description = "CombWeave's last stage: the banks patched in are summed and clipped at 0 dB, as in "
                        "the plugin.";
        d.sockets = { audioIn ("audioIn", "Audio In"), audioOut ("audioOut", "Audio Out") };
        return d;
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }

    void processSample (const StereoFrame* in, StereoFrame* out) override
    {
        out[0] = { juce::jlimit (-1.0f, 1.0f, in[0][0]), juce::jlimit (-1.0f, 1.0f, in[0][1]) };
    }
};

AQUANODE_REGISTER_MODULE (CombWeaveOscModule,    CombWeaveOscModule::descriptor)
AQUANODE_REGISTER_MODULE (CombWeaveOutputModule, CombWeaveOutputModule::descriptor)

//==============================================================================
namespace aquanode::plugins::combweave
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int output = b.add ("plugin.combweave.output", 720, 140);

        PatchPorts ports;
        ports.name = "CombWeave";
        for (int bank = 0; bank < 2; ++bank)
        {
            const int osc = b.add ("plugin.combweave.osc", 0, bank * 330);
            const juce::String suffix = bank == 0 ? "" : "2";
            for (auto* id : kBankIds)
                b.set (osc, id, kit::param (original, id + suffix));
            b.cable (osc, "audioOut", output, "audioIn");
            ports.midiIn.push_back ({ osc, "midiIn" });
        }
        ports.audioOut = { { output, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
