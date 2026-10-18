//==============================================================================
// SampleField - Patch version
//
// SampleField is one instrument - a pool of up to 128 samples, and 24 voices
// that each pick one at random on every pass, with its own random pan, rate
// and level, its time limit and a per-voice echo - so its patch version is one
// module, "SampleField - Field": SampleField's own engine, every knob taking
// cables. A held key keeps its voice drawing samples from the pool (a skipped
// draw rests as long as a sample would have played); letting go lets the
// sample (and its echoes) ring out.
//
// The pool is saved with the patch - all of its samples in one embedded
// sound, the patch state saying where each one starts - so it comes back on
// any machine, whatever became of the files.
//==============================================================================
#include "PluginSampleFieldConfig.h"

namespace aqp_samplefield
{
    #include AQP_SAMPLEFIELD_SRC(PluginProcessor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "samplefield" };
    using SF = aqp_samplefield::SampleFieldAudioProcessor;

    std::shared_ptr<const juce::AudioBuffer<float>> poolOf (juce::AudioProcessor& p, double& rate)
    {
        auto* s = dynamic_cast<SF*> (&p);
        if (s == nullptr || s->getNumLoadedSamples() == 0)
            return nullptr;
        rate = 48000.0;   // only a container: each sample keeps its own rate in the state
        return s->getPackedPool();
    }

    void setPool (juce::AudioProcessor& p, std::shared_ptr<juce::AudioBuffer<float>> audio, double)
    {
        if (auto* s = dynamic_cast<SF*> (&p); s != nullptr && audio != nullptr)
            s->setPackedPool (*audio);
    }

    // the GUI card keeps its pool in the patch too
    const bool audioRegistered = registerAudioAccess (kPlugin, { poolOf, setPool });

    juce::StringArray lockChoices()
    {
        juce::StringArray a { "Off" };
        for (int i = 1; i <= 8; ++i)
            a.add (juce::String (i) + "/8");
        return a;
    }

    // how many samples the pool holds, under the buttons
    class PoolView final : public juce::Component, private juce::Timer
    {
    public:
        explicit PoolView (SF& p) : sf (p) { startTimerHz (4); }
        void paint (juce::Graphics& g) override
        {
            const int n = sf.getNumLoadedSamples();
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.setFont (13.0f);
            g.drawText (n == 0 ? juce::String ("No samples - Load Samples to fill the field")
                               : juce::String (n) + (n == 1 ? " sample" : " samples") + " in the field",
                        getLocalBounds(), juce::Justification::centred);
        }
    private:
        void timerCallback() override
        {
            if (const int n = sf.getNumLoadedSamples(); n != shown) { shown = n; repaint(); }
        }
        SF& sf;
        int shown { -1 };
    };
}

//==============================================================================
class SampleFieldModule final : public kit::InstanceModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.samplefield.field";
        d.displayName = kit::moduleName (kPlugin, "Field");
        d.description = "SampleField whole: Load Samples (as many as you like, up to 128) and every key "
                        "you hold plays one picked at random, then the next, with random pan, rate (up to "
                        "+-2 octaves) and level on each. Skip rests instead of playing; Time cuts each sample "
                        "short (or locks it to eighths of the tempo); the echo catches a share of them (Echo "
                        "Chance). The pool is saved with the patch.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeButton ("load", "Load Samples", 0, 2),
            makeButton ("clear", "Clear", 0, 1),

            makeRotary ("pan", "Pan", -1.0f, 1.0f, 0.0f, 1),
            makeRotary ("rate", "Rate", 0.25f, 4.0f, 1.0f, 1, "x", true),
            makeRotary ("vol", "Volume", 0.0f, 2.0f, 1.0f, 1),
            makeRotary ("skip", "Skip", 0.0f, 1.0f, 0.0f, 1),
            makeRotary ("time", "Time", 0.1f, 10.0f, 10.0f, 1, "s", true),
            makeCombo ("timeLock", "Time Lock", lockChoices(), 0, 1, 2).noMod(),

            makeRotary ("panRnd", "Pan Rnd", 0.0f, 1.0f, 0.0f, 2),
            makeRotary ("rateRnd", "Rate Rnd", 0.0f, 1.0f, 0.0f, 2),
            makeRotary ("volRnd", "Vol Rnd", 0.0f, 1.0f, 0.0f, 2),

            makeRotary ("delayTime", "Echo Time", 0.1f, 10.0f, 0.5f, 3, "s", true),
            makeCombo ("delayLock", "Echo Lock", lockChoices(), 0, 3, 2).noMod(),
            makeRotary ("delayVol", "Feedback", 0.0f, 1.0f, 0.5f, 3),
            makeRotary ("delayProb", "Echo Chance", 0.0f, 1.0f, 0.0f, 3)
        };
        return d;
    }

    SampleFieldModule() : InstanceModule (kPlugin, true) {}

    SF* field() const { return dynamic_cast<SF*> (plugin.get()); }

    void uiButtonClicked (const juce::String& id) override
    {
        auto* s = field();
        if (s == nullptr)
            return;
        if (id == "clear")
        {
            s->unloadAllSamples();
        }
        else if (id == "load")
        {
            chooser = std::make_unique<juce::FileChooser> ("Load samples into the field", juce::File(),
                                                           "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");
            juce::WeakReference<SynthModule> weak (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectMultipleItems,
                [weak] (const juce::FileChooser& fc)
                {
                    auto* self = dynamic_cast<SampleFieldModule*> (weak.get());
                    if (self == nullptr || self->field() == nullptr)
                        return;
                    juce::Array<juce::File> files;
                    for (const auto& f : fc.getResults())
                        if (f.existsAsFile())
                            files.add (f);
                    if (! files.isEmpty())
                        self->field()->loadSamples (files);
                });
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        return field() != nullptr ? std::make_unique<PoolView> (*field()) : nullptr;
    }
    int extraContentHeight() const override { return 26; }

    bool hasAudioToSave() const override { return field() != nullptr && field()->getNumLoadedSamples() > 0; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        return plugin != nullptr ? poolOf (*plugin, rate) : nullptr;
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        if (plugin != nullptr)
            setPool (*plugin, std::move (audio), rate);
    }

protected:
    // the two tempo locks are not parameters of SampleField's: they go in here
    juce::String pluginParamFor (const juce::String& id) const override
    {
        return id == "timeLock" || id == "delayLock" ? juce::String() : id;
    }
    void beforeBlock() override
    {
        if (auto* s = field(); s != nullptr && timeLock >= 0 && delayLock >= 0)
        {
            s->setTempoLock ((int) std::lround (param (timeLock)));
            s->setDelayTempoLock ((int) std::lround (param (delayLock)));
        }
    }

public:
    void prepare (double sr) override
    {
        timeLock = paramIndex ("timeLock");
        delayLock = paramIndex ("delayLock");
        InstanceModule::prepare (sr);
    }

private:
    int timeLock { -1 }, delayLock { -1 };

    std::unique_ptr<juce::FileChooser> chooser;
};

AQUANODE_REGISTER_MODULE (SampleFieldModule, SampleFieldModule::descriptor)

//==============================================================================
namespace aquanode::plugins::samplefield
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int field = b.add ("plugin.samplefield.field", 0, 0);
        b.copy (field, original);

        if (auto* src = dynamic_cast<SF*> (&original))
        {
            b.set (field, "timeLock", (float) src->getTempoLock());
            b.set (field, "delayLock", (float) src->getDelayTempoLock());
        }
        if (auto* m = dynamic_cast<SampleFieldModule*> (b.dsp (field)))
        {
            m->copyStateFrom (original);
            if (auto* src = dynamic_cast<SF*> (&original))
                if (auto pool = src->getPackedPool())
                    m->restoreSavedAudio (pool, 48000.0);
        }

        PatchPorts ports;
        ports.name = "SampleField";
        ports.audioOut = { { field, "audioOut" } };
        ports.midiIn = { { field, "midiIn" } };
        return ports;
    }

    // a "preset" is a sample to start the field with
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* s = dynamic_cast<SF*> (&original);
        if (s == nullptr)
            return false;
        s->loadSamples ({ file });
        return s->getNumLoadedSamples() > 0;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
