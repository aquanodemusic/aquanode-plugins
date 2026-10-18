#pragma once

#include <JuceHeader.h>
#include "HostedPlugin.h"

class AquanodeModularAudioProcessor;

//==============================================================================
// The plugins merged into Aquanode Modular, in the order the sidebar's Plugins
// section lists them. Each one comes in two versions:
//
//  - GUI: the original plugin running whole in one card (HostedPlugin.h).
//    Its presets load through the plugin's own Load button, as always.
//
//  - Patch: the plugin rebuilt from modules of its own, listed under the
//    ordinary sections as "PluginName - Kind" and wired the way the original
//    is wired inside. A patch builder makes that wiring, either from the
//    plugin's default sound, from a preset file (read by the original plugin's
//    own loader, so every format it reads works here too), or from a GUI card
//    already in the patch ("Convert to Patch").
//==============================================================================
namespace aquanode::plugins
{
    // What a patch builder made, and where the original plugin's ports ended
    // up: the sockets that stand for its Audio Out (usually one Audio Out of
    // its last effect), its inputs, and the generators that play its notes.
    // The caller wires these the way the GUI card was wired.
    struct SocketRef { int module { -1 }; juce::String socket; };

    struct PatchPorts
    {
        juce::String name;                  // display name of what was built
        std::vector<SocketRef> audioOut;    // outputs that stand for the plugin's output
        std::vector<SocketRef> audioIn;     // inputs fed by what fed its Audio In
        std::vector<SocketRef> sideIn;      // ... its second input (Vocode's Mod In)
        std::vector<SocketRef> midiIn;      // Midi Ins that hear what its Midi In heard
        std::vector<SocketRef> midiOut;     // NoteAdder's Midi Out
    };

    // Reads the original plugin's state into its patch modules. The processor
    // given is a fully loaded instance of the original plugin (its default
    // sound, a preset loaded by its own code, or a copy of a GUI card's live
    // state); origin is where the modules start on the canvas.
    using PatchBuildFn = std::function<PatchPorts (AquanodeModularAudioProcessor& patch,
                                                  juce::AudioProcessor& original,
                                                  juce::Point<int> origin)>;

    // Loads a preset file into a fresh instance of the original plugin with
    // the plugin's own loader. false = the file was not understood.
    using PresetLoadFn = std::function<bool (juce::AudioProcessor& original, const juce::File& file)>;

    struct CatalogEntry
    {
        juce::String id;                // "aquanova"
        juce::String name;              // "Aquanova"
        juce::String guiTypeId;         // "plugin.aquanova"
        juce::String presetPatterns;    // "*.aquanova;*.syx" - empty: no preset files

        // set by the plugin's own translation unit (Plugin<Name>.cpp); empty
        // until that plugin has a patch version
        PatchBuildFn buildPatch;
        PresetLoadFn loadPreset;

        bool hasPatchVersion() const { return buildPatch != nullptr; }
    };

    // all of them, in sidebar order
    std::vector<CatalogEntry>& catalog();
    CatalogEntry* findEntry (const juce::String& id);

    // A plugin's GUI-module spec with everything but its create function
    // filled in; each Plugin<Name>.cpp completes it.
    HostedSpec baseSpec (const juce::String& id);

    // A fresh instance of the original plugin (the same code the GUI card
    // runs), prepared at the given rate; nullptr if the id is unknown.
    std::unique_ptr<juce::AudioProcessor> createOriginal (const juce::String& id, double sampleRate);

    //=== registration from the plugins' translation units ====================
    // Each Plugin<Name>.cpp registers its create function (and later its
    // patch builder) during static initialisation through one of these.
    bool registerCreate (const juce::String& id, std::function<juce::AudioProcessor*()> create);
    bool registerPatchVersion (const juce::String& id, PatchBuildFn build, PresetLoadFn load);

    // an effect plugin (vs an instrument)
    bool isEffectPluginId (const juce::String& id);

    // A plugin whose state keeps only a file path for its audio (Slicer)
    // can hand the audio itself to Aquanode Modular, which then saves it in
    // the patch like any loaded sample - so it survives other machines and
    // Android's document URIs.
    struct AudioAccess
    {
        std::function<std::shared_ptr<const juce::AudioBuffer<float>> (juce::AudioProcessor&, double& rate)> get;
        std::function<void (juce::AudioProcessor&, std::shared_ptr<juce::AudioBuffer<float>>, double rate)> set;
    };
    bool registerAudioAccess (const juce::String& id, AudioAccess access);
    const AudioAccess* audioAccessFor (const juce::String& id);

    //=== what the sidebar does ================================================
    // Adds the plugin's GUI card to the patch, wired to Audio Out (and fed
    // from Audio In for the effects); returns its instance id or -1.
    int addGuiVersion (AquanodeModularAudioProcessor& patch, const juce::String& id, juce::Point<int> where);

    // Builds the patch version - from the plugin's default sound when file is
    // empty, else from that preset file - into the existing patch (replace =
    // false) or a cleared one. Returns the display name, empty on failure.
    juce::String addPatchVersion (AquanodeModularAudioProcessor& patch, const juce::String& id,
                                  const juce::File& presetFile, bool replace);

    // Replaces a GUI card by the patch version of its current sound.
    juce::String convertGuiToPatch (AquanodeModularAudioProcessor& patch, int guiInstanceId);
}

//==============================================================================
// Used once at the end of each Plugin<Name>.cpp: the GUI module of the plugin
// whose sources that file compiled into namespace ns (whose own
// createPluginFilter() makes the processor), and its entry in the catalog's
// creator table. ClassName must also be listed in ModuleRegistry.cpp.
//==============================================================================
#define AQP_DEFINE_PLUGIN(ClassName, idString, ns) \
    static const aquanode::plugins::HostedSpec& ClassName##Spec() \
    { \
        static const aquanode::plugins::HostedSpec s = [] \
        { \
            auto x = aquanode::plugins::baseSpec (idString); \
            x.create = [] () -> juce::AudioProcessor* { return ns::createPluginFilter(); }; \
            return x; \
        }(); \
        return s; \
    } \
    static const bool ClassName##CreatorRegistered = aquanode::plugins::registerCreate ( \
        idString, [] () -> juce::AudioProcessor* { return ns::createPluginFilter(); }); \
    AQUANODE_HOSTED_PLUGIN (ClassName, ClassName##Spec)
