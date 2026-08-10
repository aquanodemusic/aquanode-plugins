#pragma once

#include <JuceHeader.h>
#include <map>
#include "Modules/ModuleCore.h"

//==============================================================================
// One patched instance of a module in the graph.
struct ModuleInstance
{
    int id { 0 };
    const aquanode::RegisteredModule* reg { nullptr };
    std::unique_ptr<aquanode::SynthModule> dsp;
    juce::Point<int> position;

    // resolved voice lane (Flexible modules resolve at graph compile time)
    bool perVoice { false };
    int numIn { 1 }, numOut { 1 };

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

    std::vector<int> getModuleIds() const;
    ModuleInstance* getInstance (int instanceId) const;
    const std::vector<CableInfo>& getCables() const { return cables; }

    bool exportPatchToZip (const juce::File& zipFile);
    bool importPatchFromZip (const juce::File& zipFile);

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

    struct ProcessGraph
    {
        std::vector<std::shared_ptr<ModuleInstance>> nodes;      // topological order
        std::vector<std::vector<CompiledEdge>> incoming;         // per node
        juce::uint64 serial { 0 };
    };

    void rebuildGraph();
    void purgeRetired();
    void clearPatch();
    static void initInstanceBuffers (ModuleInstance& inst);

    //=== global voice manager (audio thread only; G2-style whole-patch voices)
    struct VoiceState
    {
        int note { -1 };
        bool active { false };
        bool held { false };
        double countdown { 0.0 };       // seconds of tail left after note-off
        juce::uint64 order { 0 };
    };

    void refreshActiveVoices();
    void engineNoteOn  (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int note, float velocity01);
    void engineNoteOff (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int note);
    void engineAllNotesOff (const std::vector<std::shared_ptr<ModuleInstance>>& nodes);
    void deactivateVoice (const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int v);
    double computeVoiceTail (const std::vector<std::shared_ptr<ModuleInstance>>& nodes) const;

    VoiceState voiceStates[aquanode::kMaxVoices];
    juce::uint64 voiceOrderCounter { 0 };
    int activeVoiceList[aquanode::kMaxVoices] {};
    int numActiveVoices { 0 };

    std::unique_ptr<juce::XmlElement> buildPatchXml (bool embedSamplesAsBase64,
                                                     juce::ZipFile::Builder* zipBuilder);
    void applyPatchXml (const juce::XmlElement& xml,
                        const std::function<std::shared_ptr<juce::AudioBuffer<float>> (const juce::String& fileRef, double& rate)>& sampleLoader);

    static bool writeSampleAsWav (const juce::AudioBuffer<float>& buffer, double rate, juce::MemoryBlock& dest);
    static std::shared_ptr<juce::AudioBuffer<float>> readWavFromMemory (const void* data, size_t size, double& rate);

    //=== state ================================================================
    std::vector<std::shared_ptr<ModuleInstance>> instances;      // message thread owns
    std::vector<CableInfo> cables;
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
