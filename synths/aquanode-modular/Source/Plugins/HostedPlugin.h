#pragma once

#include <JuceHeader.h>
#include "../Modules/ModuleCore.h"

//==============================================================================
// GUI versions of the merged plugins
//
// The Plugins section of the sidebar offers each of the plugins that were
// merged into Aquanode Modular in two versions. The PATCH version builds the
// plugin out of ordinary modules ("Aquanova - Oscillator", ...). The GUI
// version is this module: the original plugin, running whole inside one card
// with its own window drawn on it. Its knobs are the plugin's own, so they
// take no cables - the card's sockets are only what the original plugin had
// as ports: Midi In for the synths, Audio In / Audio Out, the Vocoder's second
// input and NoteAdder's Midi Out.
//
// The plugin runs in blocks of kBlockSize samples behind the per-sample
// patch, so its audio (and NoteAdder's MIDI) comes out kBlockSize samples
// late - 0.7 ms at 48 kHz. Everything it needs from the host is passed on:
// the MIDI its Midi In hears (sample-accurate inside its block), tempo and
// transport as a play head, and its whole state is saved with the patch.
//==============================================================================
namespace aquanode::plugins
{
    struct HostedSpec
    {
        juce::String pluginId;          // catalog key, "aquanova"
        juce::String typeId;            // "plugin.aquanova"
        juce::String displayName;       // "Aquanova"
        juce::String description;
        juce::Colour accent;
        ModuleSection section { ModuleSection::Oscillator };

        bool midiIn { false };
        bool midiOut { false };
        bool audioIn { false };         // the plugin's main input
        bool sideIn { false };          // a second input bus (Vocode's modulator)
        juce::String sideInLabel { "Mod In" };

        int defaultWidth { 800 }, defaultHeight { 500 };   // until its editor has been opened once

        std::function<juce::AudioProcessor*()> create;
    };

    ModuleDescriptor makeHostedDescriptor (const HostedSpec& spec);

    class HostedPluginModule : public SynthModule
    {
    public:
        static constexpr int kBlockSize = 32;

        explicit HostedPluginModule (const HostedSpec& spec);
        ~HostedPluginModule() override;

        // the hosted processor itself - for the "convert to patch" path, which
        // reads its parameters, and for the editor card
        juce::AudioProcessor* getPlugin() const { return plugin.get(); }
        const HostedSpec& getSpec() const { return spec; }

        //=== SynthModule ======================================================
        void prepare (double newSampleRate) override;
        void reset() override;
        void blockStart() override;
        VoiceMode voiceMode() const override { return VoiceMode::Global; }
        void processSample (const StereoFrame* inputs, StereoFrame* outputs) override;

        bool wantsRawMidi() const override { return spec.midiIn; }
        void rawMidiMessage (const juce::MidiMessage& message) override;

        bool isMidiNoteDriver() const override { return spec.midiOut; }
        bool midiSourceReplacesInput() const override { return spec.midiOut; }
        int midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents) override;
        void midiAllNotesOff() override;

        std::unique_ptr<juce::Component> createExtraContentComponent() override;
        int preferredModuleWidth() const override;
        int extraContentHeight() const override;

        juce::String saveCustomState() const override;
        void loadCustomState (const juce::String& state) override;

        // the plugin's own audio (Slicer's sample), saved with the patch
        bool hasAudioToSave() const override;
        std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override;
        void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override;

        // the card's editor reports its size here when it opens or resizes
        void noteEditorSize (int w, int h);

    private:
        struct PlayHead : public juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override { return info; }
            PositionInfo info;
        };

        void runBlock();
        void updatePlayHead();

        const HostedSpec& spec;
        std::shared_ptr<juce::AudioProcessor> plugin;
        PlayHead playHead;

        juce::AudioBuffer<float> io;      // the plugin's process buffer
        juce::MidiBuffer midiInBuffer, midiOutScratch;
        std::array<std::array<float, kBlockSize>, 2> outFifo {};
        int pos { 0 };                    // sample inside the current block
        int mainIns { 0 }, outs { 0 }, sideChannels { 0 };
        // the side-chain bus is on only while a cable feeds it, as in a DAW
        // with nothing routed to the side chain (Vocode then auto-vocodes)
        std::atomic<int> sideIns { 0 };
        std::atomic<bool> sideRelayoutPending { false };
        void setSideChainEnabled (bool enabled);
        int inAudioIdx { -1 }, inSideIdx { -1 }, outAudioIdx { -1 }, numOutputs { 1 };
        int samplesThisHostBlock { 0 };
        juce::int64 sampleCounter { 0 };
        bool prepared { false };

        // NoteAdder's output, due one block after it was produced
        struct PendingNote { juce::int64 due; MidiDriverEvent ev; };
        std::vector<PendingNote> pendingOut;
        juce::int64 driverCounter { 0 };

        std::atomic<int> editorW, editorH;

        JUCE_DECLARE_NON_COPYABLE (HostedPluginModule)
    };
}

// Defines and registers the GUI module of one plugin. specFn returns a
// reference to a static HostedSpec; the class name doubles as the
// registration symbol, so it has to be listed in ModuleRegistry.cpp.
#define AQUANODE_HOSTED_PLUGIN(ClassName, specFn) \
    class ClassName final : public aquanode::plugins::HostedPluginModule \
    { \
    public: \
        ClassName() : aquanode::plugins::HostedPluginModule (specFn()) {} \
    }; \
    static aquanode::ModuleDescriptor ClassName##Descriptor() { return aquanode::plugins::makeHostedDescriptor (specFn()); } \
    AQUANODE_REGISTER_MODULE (ClassName, ClassName##Descriptor)
