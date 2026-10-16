#pragma once

#include <JuceHeader.h>
#include <map>
#include <algorithm>
#include <vector>
#include "Modules/ModuleCore.h"

//==============================================================================
// One patched instance of a module in the graph.
struct ModuleInstance
{
    int id { 0 };
    const aquanode::RegisteredModule* reg { nullptr };
    std::unique_ptr<aquanode::SynthModule> dsp;
    juce::Point<int> position;

    // editor-only: module drawn as just its header, controls hidden. Lives
    // here rather than in the ModuleComponent so it survives a canvas rebuild
    // and gets saved with the patch alongside the position.
    bool collapsed { false };

    // resolved voice lane (Flexible modules resolve at graph compile time)
    bool perVoice { false };
    int numIn { 1 }, numOut { 1 };

    // instance ids of every Midi Add / Arp / Always Midi / Discard Midi
    // patched into this module's "Add Midi In" socket. Empty = nothing
    // patched, so the module just hears the keyboard. Several sources may
    // feed one generator at once: it then answers to all of them (three
    // Always Midi drones, or an Arp AND a Midi Add, on one oscillator).
    // midiSourceReplaces is true when ANY connected source answers yes to
    // "do my notes replace the played ones?" (Arp, Always Midi, Discard
    // Midi) - one replacing source is enough to take the keyboard away.
    std::vector<int> midiSourceIds;
    bool midiSourceReplaces { false };

    bool listensTo (int sourceInstanceId) const
    {
        return std::find (midiSourceIds.begin(), midiSourceIds.end(), sourceInstanceId)
                 != midiSourceIds.end();
    }

    // audio thread only: did this module get a note-on for engine voice v?
    // A gated module (a generator with a Midi In) stays silent on every voice
    // it was not given a note for - otherwise its envelope, shared by the
    // whole voice, would make it sound at a stale pitch on notes that were
    // meant for some OTHER generator (the "phantom note" bug).
    bool voiceHeard[aquanode::kMaxVoices] {};

    // runtime socket buffers (audio thread only)
    std::vector<aquanode::StereoFrame> inBuf, outBuf, prevOutBuf;              // global lane
    std::vector<aquanode::StereoFrame> outV, prevOutV;   // per-voice lanes, [voice * numOut + socket]

    const aquanode::ModuleDescriptor& descriptor() const { return reg->descriptor; }
};

struct CableInfo
{
    int fromModule { 0 };
    juce::String fromSocket;
    int toModule { 0 };
    juce::String toSocket;
};

// modulation of a knob: any output -> a rotary parameter, scaled by depth
// (-1..1 of the parameter's full range). Delivered in the global lane;
// per-voice sources are voice-summed like any other global consumer.
struct ParamCableInfo
{
    int fromModule { 0 };
    juce::String fromSocket;
    int toModule { 0 };
    juce::String paramId;
    float depth { 0.3f };
};

//==============================================================================
class AquanodeModularAudioProcessor : public juce::AudioProcessor,
                                      public juce::ChangeBroadcaster
{
public:
    AquanodeModularAudioProcessor();
    ~AquanodeModularAudioProcessor() override;

    //=== AudioProcessor =======================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Aquanode Modular"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 21.0; }   // covers max 20s ADSR release

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //=== patch model (message thread) =========================================
    int addModule (const juce::String& typeId, juce::Point<int> position);
    void removeModule (int instanceId);                 // removes touching cables too
    int cloneModule (int instanceId);                   // params + sample, no cables
    void setModulePosition (int instanceId, juce::Point<int> position);

    bool addCable (int fromModule, const juce::String& fromSocket,
                   int toModule, const juce::String& toSocket);
    void removeCable (int cableIndex);

    bool addParamCable (int fromModule, const juce::String& fromSocket,
                        int toModule, const juce::String& paramId, float depth);
    void removeParamCable (int index);
    void setParamCableDepth (int index, float depth);
    const std::vector<ParamCableInfo>& getParamCables() const { return paramCables; }

    std::vector<int> getModuleIds() const;
    ModuleInstance* getInstance (int instanceId) const;
    const std::vector<CableInfo>& getCables() const { return cables; }

    bool exportPatchToZip (const juce::File& zipFile);
    bool importPatchFromZip (const juce::File& zipFile);

    // Stream variants: used by the Android build to write to / read from a
    // document the user picked via the system file dialog (Storage Access
    // Framework). Same .zip container as the File versions -> fully compatible.
    bool exportPatchToStream (juce::OutputStream& out);
    bool importPatchFromStream (juce::InputStream& in);

    // Blank slate. Instrument build: one default Oscillator -> Audio Out.
    // Effect build (AQUANODE_FX_BUILD): Audio In -> Audio Out pass-through.
    void initializePatch();

    // On-screen keyboard (the bottom keyboard every build shows, toggled by
    // the Keys button). The editor attaches a MidiKeyboardComponent to this
    // state; processBlock merges its events into the incoming MIDI so clicks
    // and taps play exactly like host/keyboard notes. It also records the
    // incoming stream, so on-screen keys light up when a real controller plays.
    juce::MidiKeyboardState keyboardState;

    // The Sidebar / Keys panel toggles live here rather than in the editor, so
    // closing and reopening a plugin window keeps them as they were left.
    bool uiSidebarVisible { true };
    bool uiKeyboardVisible { true };

private:
    //=== compiled process graph (published to the audio thread) ==============
    enum class EdgeConversion { Direct, AudioToMod, ModToAudio, ModToMod };

    struct CompiledEdge
    {
        int srcNode { 0 };          // index into ProcessGraph::nodes (topological order)
        int srcSock { 0 };
        int dstSock { 0 };
        bool feedback { false };    // DFS back-edge -> read previous sample's value
        EdgeConversion conversion { EdgeConversion::Direct };
    };

    struct CompiledParamEdge
    {
        int srcNode { 0 };
        int srcSock { 0 };
        int paramIdx { 0 };
        float scale { 0.0f };       // depth * (param max - min)
        bool feedback { false };

        // absolute cables (Curve CV): the source's 0..1 is the knob's whole
        // range, blended with the knob's own setting by |depth|
        bool absolute { false };
        float depth { 0.0f };
        float minValue { 0.0f }, maxValue { 1.0f };
    };

    // a MIDI note driver (Arp, Always Midi, Piano Roll) whose notes reach
    // something; ticked once per sample so it can emit notes on its own clock
    struct MidiDriverRef
    {
        int instanceId { -1 };
        aquanode::SynthModule* dsp { nullptr };
    };

    // How one node takes part in MIDI routing, frozen into the compiled graph
    // so the audio thread never reads the instances' routing fields while the
    // message thread is rebuilding them.
    struct NodeMidiRouting
    {
        std::vector<int> sources;       // instance ids patched into its Midi In
        bool replaces { false };        // a source that takes the played keys away
        bool gated { false };           // silent on voices it got no note-on for
        bool hitsInGlobalLane { false };// global-lane drum: notes arrive as hits

        bool listensTo (int id) const
        {
            return std::find (sources.begin(), sources.end(), id) != sources.end();
        }
    };

    // One MIDI stream: everything a module with a Midi Out sends on. Its notes
    // start voices on the sound-making modules listening to it directly
    // (hasConsumers), and are handed to every MIDI processor listening to it,
    // which in turn send on streams of their own.
    struct MidiStream
    {
        int sourceId { -1 };
        bool hasConsumers { false };
        std::vector<int> processors;    // node indices of listening MIDI processors
    };

    struct ProcessGraph
    {
        std::vector<std::shared_ptr<ModuleInstance>> nodes;      // topological order
        std::vector<NodeMidiRouting> midi;                       // per node
        std::vector<MidiStream> streams;
        std::vector<int> keyboardProcessors;    // processors fed by the played keys (empty Midi In)

        const MidiStream* findStream (int sourceId) const
        {
            for (const auto& st : streams)
                if (st.sourceId == sourceId)
                    return &st;
            return nullptr;
        }

        // does this source start voices on anything that makes sound?
        bool feeds (int sourceId) const
        {
            const auto* st = findStream (sourceId);
            return st != nullptr && st->hasConsumers;
        }

        std::vector<MidiDriverRef> midiDrivers;
        std::vector<std::vector<CompiledEdge>> incoming;         // per node
        std::vector<std::vector<CompiledParamEdge>> incomingParams;
        juce::uint64 serial { 0 };
    };

    void rebuildGraph();
    void purgeRetired();
    void clearPatch();
    static void initInstanceBuffers (ModuleInstance& inst);

    //=== global voice manager (audio thread only; G2-style whole-patch voices)
    struct VoiceState
    {
        int note { -1 };            // the pitch this voice sounds
        int midiSource { -1 };      // the stream that started it, -1 = the played keys
        bool active { false };
        bool held { false };
        double countdown { 0.0 };       // seconds of tail left after note-off
        juce::uint64 order { 0 };
    };

    // MIDI routing (audio thread). A note event travels from its source
    // through every processor chain it is patched into; wherever it meets
    // sound-making modules it starts or releases a voice.
    static constexpr int kMaxMidiChainDepth = 16;
    void keyboardEvent (const ProcessGraph& graph, const aquanode::MidiDriverEvent& e);
    void handleControlMessage (const ProcessGraph& graph, const juce::MidiMessage& msg);

    // non-note MIDI (bend, wheels, CCs, sustain), audio thread only
    aquanode::MidiControlState midiState;
    bool sustainedKeys[128] {};     // released while the pedal was down
    void emitMidi (const ProcessGraph& graph, int sourceId, const aquanode::MidiDriverEvent& e, int depth);
    void feedProcessor (const ProcessGraph& graph, int nodeIndex, const aquanode::MidiDriverEvent& e, int depth);

    void refreshActiveVoices();
    void startVoice (const ProcessGraph& graph, int note, int midiSource, float velocity01);
    void releaseVoices (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int midiSource, int note);
    void engineAllNotesOff (const ProcessGraph& graph);
    void deactivateVoice (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int v);
    double computeVoiceTail (const std::vector<std::shared_ptr<ModuleInstance>>& nodes) const;

    VoiceState voiceStates[aquanode::kMaxVoices];
    juce::uint64 voiceOrderCounter { 0 };
    int activeVoiceList[aquanode::kMaxVoices] {};
    int numActiveVoices { 0 };

    std::unique_ptr<juce::XmlElement> buildPatchXml (bool embedSamplesAsBase64,
                                                     juce::ZipFile::Builder* zipBuilder);
    bool importFromZipObject (juce::ZipFile& zip);   // shared by File + stream import
    void applyPatchXml (const juce::XmlElement& xml,
                        const std::function<std::shared_ptr<juce::AudioBuffer<float>> (const juce::String& fileRef, double& rate)>& sampleLoader);

    static bool writeSampleAsWav (const juce::AudioBuffer<float>& buffer, double rate, juce::MemoryBlock& dest);
    static std::shared_ptr<juce::AudioBuffer<float>> readWavFromMemory (const void* data, size_t size, double& rate);

    //=== state ================================================================
    std::vector<std::shared_ptr<ModuleInstance>> instances;      // message thread owns
    std::vector<CableInfo> cables;
    std::vector<ParamCableInfo> paramCables;

    // twelve host-automatable modulation slots, claimed by DAW Mod modules
    std::array<juce::AudioParameterFloat*, 12> dawModParams { };
    std::array<float, 12> dawModSnapshot { };   // audio thread: one read per block
    juce::uint64 audioLastGraphSerial { 0 };   // audio thread only: param-mod zeroing on graph swap
    int nextInstanceId { 1 };

    juce::SpinLock graphLock;
    std::shared_ptr<const ProcessGraph> renderGraph;             // audio thread copies under lock
    juce::uint64 graphSerial { 0 };
    std::atomic<juce::uint64> audioAdoptedSerial { 0 };

    // retired objects kept alive until the audio thread has adopted a newer graph
    std::vector<std::pair<juce::uint64, std::shared_ptr<void>>> graveyard;

    juce::AudioBuffer<float> inputScratch;
    double currentSampleRate { 44100.0 };
    bool isPrepared { false };
    std::atomic<bool> resetAllPending { false };   // set on module delete / lane change -> audio thread wipes all voice/DSP state
    std::map<int, bool> lastResolvedLanes;         // instance id -> perVoice, for lane-change detection

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AquanodeModularAudioProcessor)
};
