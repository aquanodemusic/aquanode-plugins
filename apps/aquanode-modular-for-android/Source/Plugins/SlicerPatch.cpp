//==============================================================================
// Slicer - Patch version
//
// Slicer is one instrument - a sample, the slices found in it, and the voices
// that play them - so its patch version is one module, "Slicer - Slice
// Player": Slicer's own engine (transient detection, the three slice-end
// modes, reverse / loop / mono, the B0 random stream, the fadeout), with
// Slicer's own waveform editor in the card (drag, add and delete slices, or
// click to play them) and every playback knob taking cables.
//
// Keys: C1 and up play slice 1, 2, 3 ... ; B0 starts the random stream.
// Region, Strength and End Mode decide the slicing: change them, then press
// Re-Analyze (as in the plugin). The sample and the edited slices are saved
// with the patch, so they come back on any machine.
//==============================================================================
#include "PluginSlicerConfig.h"

namespace aqp_slicer
{
    #include AQP_SLICER_SRC(PluginProcessor.h)
    #include AQP_SLICER_SRC(PluginEditor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "slicer" };
    using SP = aqp_slicer::SlicerAudioProcessor;

    std::shared_ptr<const juce::AudioBuffer<float>> sliceAudioOf (juce::AudioProcessor& p, double& rate)
    {
        auto* s = dynamic_cast<SP*> (&p);
        if (s == nullptr || ! s->hasSample())
            return nullptr;
        rate = s->getLoadedSampleRate();
        // a view of Slicer's own buffer (saving happens there and then)
        return std::shared_ptr<const juce::AudioBuffer<float>> (&s->getSampleBuffer(), [] (const juce::AudioBuffer<float>*) {});
    }

    void setSliceAudio (juce::AudioProcessor& p, std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
    {
        if (auto* s = dynamic_cast<SP*> (&p); s != nullptr && audio != nullptr)
            s->setSample (std::move (*audio), rate, s->getLoadedSamplePath());
    }

    // the GUI card keeps its sample in the patch too
    const bool audioRegistered = registerAudioAccess (kPlugin, { sliceAudioOf, setSliceAudio });

    // Slicer's waveform editor, repainted while it plays
    class SlicerView final : public juce::Component, private juce::Timer
    {
    public:
        explicit SlicerView (SP& p) : display (p) { addAndMakeVisible (display); startTimerHz (30); }
        void resized() override { display.setBounds (getLocalBounds()); }
    private:
        void timerCallback() override { display.repaint(); }
        aqp_slicer::WaveformDisplay display;
    };
}

//==============================================================================
class SlicerPlayerModule final : public SynthModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.slicer.player";
        d.displayName = kit::moduleName (kPlugin, "Slice Player");
        d.description = "Slicer whole: Load a loop, it slices it at the hits, one slice per key from C1 up "
                        "(B0 runs the random stream). Edit the slices in the waveform - drag them, right-click "
                        "to delete, click to add or play (Click). Region, Strength and End Mode take effect "
                        "with Re-Analyze. The sample and its slices are saved with the patch.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeButton ("load", "Load Sample", 0, 2),
            makeButton ("analyze", "Re-Analyze", 0, 2),
            makeButton ("export", "Export FLAC", 0, 1),

            makeRotary ("volume", "Volume", 0.0f, 1.0f, 0.8f, 1),
            makeRotary ("playbackSpeed", "Speed", 0.25f, 4.0f, 1.0f, 1, "x", true),
            makeRotary ("fadeoutMs", "Fadeout", 0.0f, 200.0f, 5.0f, 1, "ms"),
            makeCombo ("reverseMode", "Reverse", { "Off", "On" }, 0, 1, 1),
            makeCombo ("loopMode", "Loop", { "Off", "On" }, 0, 1, 1),

            makeCombo ("monoMode", "Mono", { "Off", "On" }, 0, 2, 1),
            makeRotary ("sliceStrength", "Strength", 0.01f, 1.0f, 0.3f, 2, {}, true).noMod(),
            makeRotary ("regionStart", "Region In", 0.0f, 1.0f, 0.0f, 2).noMod(),
            makeRotary ("regionEnd", "Region Out", 0.0f, 1.0f, 1.0f, 2).noMod(),
            makeCombo ("sliceEndMode", "End Mode", { "Full Length", "Trim -40 dB", "Smart Trim" }, 2, 3, 2).noMod(),
            makeCombo ("interactionMode", "Click", { "Adds Slices", "Plays Slices" }, 0, 3, 2).noMod()
        };
        return d;
    }

    SlicerPlayerModule()
    {
        auto p = createOriginal (kPlugin, 48000.0);
        slicer.reset (dynamic_cast<SP*> (p.get()));
        if (slicer != nullptr)
            p.release();
    }

    VoiceMode voiceMode() const override { return VoiceMode::Global; }
    bool wantsRawMidi() const override { return true; }
    void rawMidiMessage (const juce::MidiMessage& m) override { midiQueue.addEvent (m, pos); }
    void midiAllNotesOff() override { midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos); }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        if (slicer == nullptr)
            return;
        links.clear();
        const auto& d = getDescriptor();
        for (int i = 0; i < (int) d.params.size(); ++i)
            if (d.params[(size_t) i].type != ParamType::Button)
                if (auto* prm = kit::findParam (*slicer, d.params[(size_t) i].id))
                    links.push_back ({ prm, i, -1.0f });
        {
            const juce::ScopedLock sl (slicer->getCallbackLock());
            slicer->releaseResources();
            slicer->setRateAndBufferSizeDetails (sr, kBlock);
            slicer->prepareToPlay (sr, kBlock);
        }
        io.setSize (2, kBlock);
        io.clear();
        midiQueue.ensureSize (1024);
        for (auto& c : out) c.fill (0.0f);
        pos = 0;
    }

    void reset() override
    {
        midiQueue.clear();
        midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    }

    void processSample (const StereoFrame*, StereoFrame* outputs) override
    {
        outputs[0] = { out[0][(size_t) pos], out[1][(size_t) pos] };
        if (++pos >= kBlock)
        {
            pos = 0;
            runBlock();
        }
    }

    void uiButtonClicked (const juce::String& id) override
    {
        if (slicer == nullptr)
            return;
        if (id == "analyze")
        {
            pushKnobs (true);
            slicer->analyzeAndSlice();
        }
        else if (id == "load")
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sample to slice", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            juce::WeakReference<SynthModule> weak (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [weak] (const juce::FileChooser& fc)
                {
                    auto* self = dynamic_cast<SlicerPlayerModule*> (weak.get());
                    if (self == nullptr) return;
                    self->pushKnobs (true);
                   #if JUCE_ANDROID
                    if (auto doc = juce::AndroidDocument::fromDocument (fc.getURLResult()); doc.hasValue())
                        if (auto in = doc.createInputStream())
                        {
                            juce::MemoryBlock mb;
                            in->readIntoMemoryBlock (mb);
                            self->slicer->loadSample (mb.getData(), mb.getSize());
                        }
                   #else
                    if (fc.getResult().existsAsFile())
                        self->slicer->loadSample (fc.getResult());
                   #endif
                });
        }
        else if (id == "export")
        {
            chooser = std::make_unique<juce::FileChooser> ("Export the slices into a folder", juce::File());
            juce::WeakReference<SynthModule> weak (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [weak] (const juce::FileChooser& fc)
                {
                    if (auto* self = dynamic_cast<SlicerPlayerModule*> (weak.get()))
                        if (fc.getResult().isDirectory())
                            self->slicer->exportSlicesToDisc (fc.getResult());
                });
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        return slicer != nullptr ? std::make_unique<SlicerView> (*slicer) : nullptr;
    }
    int extraContentHeight() const override { return 190; }
    int preferredModuleWidth() const override { return 620; }

    // the plugin's state (slices, sample path) - the knobs save themselves
    juce::String saveCustomState() const override
    {
        if (slicer == nullptr) return {};
        juce::MemoryBlock mb;
        slicer->getStateInformation (mb);
        return mb.toBase64Encoding();
    }
    void loadCustomState (const juce::String& s) override
    {
        juce::MemoryBlock mb;
        if (slicer != nullptr && mb.fromBase64Encoding (s) && mb.getSize() > 0)
            slicer->setStateInformation (mb.getData(), (int) mb.getSize());
    }

    bool hasAudioToSave() const override { return slicer != nullptr && slicer->hasSample(); }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        return slicer != nullptr ? sliceAudioOf (*slicer, rate) : nullptr;
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        if (slicer != nullptr)
        {
            pushKnobs (true);   // slice with this module's own settings
            setSliceAudio (*slicer, std::move (audio), rate);
        }
    }

    SP* plugin() const { return slicer.get(); }

private:
    static constexpr int kBlock = 32;

    // knobs (with their cables) into Slicer's parameters
    void pushKnobs (bool all)
    {
        for (auto& l : links)
        {
            const float v = l.param->convertTo0to1 (all ? getParameterBase (l.knob) : param (l.knob));
            if (v != l.last)
            {
                l.param->setValueNotifyingHost (v);
                l.last = v;
            }
        }
    }

    void runBlock()
    {
        if (slicer == nullptr)
            return;
        pushKnobs (false);
        {
            const juce::ScopedTryLock sl (slicer->getCallbackLock());
            if (sl.isLocked())
                slicer->processBlock (io, midiQueue);
            else
                io.clear();
        }
        midiQueue.clear();
        for (int i = 0; i < kBlock; ++i)
        {
            float l = io.getSample (0, i), r = io.getSample (1, i);
            out[0][(size_t) i] = std::isfinite (l) ? l : 0.0f;
            out[1][(size_t) i] = std::isfinite (r) ? r : 0.0f;
        }
    }

    struct Link { juce::RangedAudioParameter* param; int knob; float last; };

    std::unique_ptr<SP> slicer;
    std::vector<Link> links;
    juce::AudioBuffer<float> io;
    juce::MidiBuffer midiQueue;
    std::array<std::array<float, kBlock>, 2> out {};
    int pos { 0 };
    std::unique_ptr<juce::FileChooser> chooser;
};

AQUANODE_REGISTER_MODULE (SlicerPlayerModule, SlicerPlayerModule::descriptor)

//==============================================================================
namespace aquanode::plugins::slicer
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int player = b.add ("plugin.slicer.player", 0, 0);
        b.copy (player, original);

        if (auto* m = dynamic_cast<SlicerPlayerModule*> (b.dsp (player)))
            if (auto* src = dynamic_cast<SP*> (&original); src != nullptr && m->plugin() != nullptr)
            {
                // the sample and the slices exactly as the plugin has them
                juce::MemoryBlock state;
                src->getStateInformation (state);
                m->plugin()->setStateInformation (state.getData(), (int) state.getSize());
                if (src->hasSample())
                {
                    const auto slices = src->slicesToString();
                    juce::AudioBuffer<float> copy (src->getSampleBuffer());
                    m->plugin()->setSample (std::move (copy), src->getLoadedSampleRate(), src->getLoadedSamplePath());
                    m->plugin()->restoreSlices (slices);
                }
            }

        PatchPorts ports;
        ports.name = "Slicer";
        ports.audioOut = { { player, "audioOut" } };
        ports.midiIn = { { player, "midiIn" } };
        return ports;
    }

    // a "preset" is an audio file to slice
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* s = dynamic_cast<SP*> (&original);
        if (s == nullptr)
            return false;
        s->loadSample (file);
        return s->hasSample();
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
