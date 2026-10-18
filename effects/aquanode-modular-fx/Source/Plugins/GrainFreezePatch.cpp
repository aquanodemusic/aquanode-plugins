//==============================================================================
// GrainFreeze - Patch version
//
// GrainFreeze is one instrument - a loaded sound, played through a phase
// vocoder that stretches it in time or freezes it on one spot - so its patch
// version is one module, "GrainFreeze - Player": GrainFreeze's own engine
// (the FFT sizes and hops, the Spectral and Cepstral freezes with Character,
// phase lock, HF boost, micro movement, the glide between frozen spots),
// with its waveform in the card to click or drag the playhead, and every
// knob taking cables - an LFO on Position scans a frozen sound.
//
// Keys play it as GrainFreeze's own MIDI does: a held note runs the playback,
// pitched from C4, the last note held wins. Play / Stop runs it without keys.
// The sound is saved with the patch, so it comes back on any machine.
//==============================================================================
#include "PluginGrainFreezeConfig.h"

namespace aqp_grainfreeze
{
    #include AQP_GRAINFREEZE_SRC(PluginProcessor.h)
    #include AQP_GRAINFREEZE_SRC(PluginEditor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "grainfreeze" };
    using GF = aqp_grainfreeze::GrainfreezeAudioProcessor;

    std::shared_ptr<const juce::AudioBuffer<float>> soundOf (juce::AudioProcessor& p, double& rate)
    {
        auto* g = dynamic_cast<GF*> (&p);
        if (g == nullptr || ! g->isAudioLoaded())
            return nullptr;
        rate = g->getLoadedSampleRate();
        // a view of GrainFreeze's own buffer (saving happens there and then)
        return std::shared_ptr<const juce::AudioBuffer<float>> (&g->getLoadedAudio(), [] (const juce::AudioBuffer<float>*) {});
    }

    void setSound (juce::AudioProcessor& p, std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
    {
        if (auto* g = dynamic_cast<GF*> (&p); g != nullptr && audio != nullptr)
            g->setAudio (std::move (*audio), rate);
    }

    // the GUI card keeps its sound in the patch too
    const bool audioRegistered = registerAudioAccess (kPlugin, { soundOf, setSound });

    // GrainFreeze's waveform with its playhead, repainted while it plays
    class GrainFreezeView final : public juce::Component, private juce::Timer
    {
    public:
        explicit GrainFreezeView (GF& p) : display (p) { addAndMakeVisible (display); startTimerHz (30); }
        void resized() override { display.setBounds (getLocalBounds()); }
    private:
        void timerCallback() override { display.repaint(); }
        aqp_grainfreeze::WaveformDisplay display;
    };
}

//==============================================================================
class GrainFreezePlayerModule final : public kit::InstanceModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.grainfreeze.player";
        d.displayName = kit::moduleName (kPlugin, "Player");
        d.description = "GrainFreeze whole: Load a sound and it plays through the phase vocoder - Stretch "
                        "slows it down without changing the pitch, Freeze holds it on Position (gliding there "
                        "over Glide). A held key plays it, pitched from C4; Play / Stop runs it without keys. "
                        "Click or drag the waveform to move the playhead. The sound is saved with the patch.";
        d.section = ModuleSection::Oscillator;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeButton ("load", "Load Sound", 0, 2),
            makeButton ("play", "Play / Stop", 0, 2),
            makeRotary ("playhead", "Position", 0.0f, 1.0f, 0.0f, 0),

            makeRotary ("timeStretch", "Stretch", 0.1f, 4.0f, 1.0f, 1, "x", true),
            makeCombo ("freezeMode", "Freeze", { "Off", "On" }, 0, 1, 2),
            makeRotary ("glide", "Glide", 0.0f, 1000.0f, 100.0f, 1, "ms"),
            makeCombo ("freezeType", "Freeze Type", { "Spectral", "Cepstral" }, 1, 1, 2),
            makeRotary ("character", "Character", 0.0f, 100.0f, 50.0f, 1, "%").visibleWhen ("freezeType", 1.0f),
            makeRotary ("hfBoost", "HF Boost", 0.0f, 100.0f, 10.0f, 1, "%").visibleWhen ("freezeType", 0.0f),
            makeRotary ("microMovement", "Movement", 0.0f, 100.0f, 20.0f, 1, "%"),

            makeCombo ("fftSize", "FFT Size", { "512", "1024", "2048", "4096", "8192", "16384", "32768", "65536" }, 3, 2, 2).noMod(),
            makeRotary ("hopSize", "Overlap", 2.0f, 16.0f, 4.0f, 2, {}, false, 0.5f).noMod(),
            makeCombo ("windowType", "Window", { "Hann", "Blackman-Harris" }, 1, 2, 2).noMod(),
            makeRotary ("crossfadeLength", "Crossfade", 1.0f, 8.0f, 2.0f, 2, "hops", false, 0.5f),
            makeCombo ("stereoMode", "Stereo", { "Off", "On" }, 1, 2, 2),
            makeCombo ("phaseLock", "Phase Lock", { "Off", "On" }, 1, 2, 2)
        };
        return d;
    }

    GrainFreezePlayerModule() : InstanceModule (kPlugin, true) {}

    GF* grain() const { return dynamic_cast<GF*> (plugin.get()); }

    void uiButtonClicked (const juce::String& id) override
    {
        auto* g = grain();
        if (g == nullptr)
            return;
        if (id == "play")
        {
            g->setPlaying (! g->isPlaying());
        }
        else if (id == "load")
        {
            chooser = std::make_unique<juce::FileChooser> ("Load a sound to play", juce::File(),
                                                           "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            juce::WeakReference<SynthModule> weak (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [weak] (const juce::FileChooser& fc)
                {
                    if (auto* self = dynamic_cast<GrainFreezePlayerModule*> (weak.get()))
                        if (auto* g2 = self->grain(); g2 != nullptr && fc.getResult().existsAsFile())
                            g2->loadAudioFile (fc.getResult());
                });
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        return grain() != nullptr ? std::make_unique<GrainFreezeView> (*grain()) : nullptr;
    }
    int extraContentHeight() const override { return 150; }
    int preferredModuleWidth() const override { return 560; }

    bool hasAudioToSave() const override { return grain() != nullptr && grain()->isAudioLoaded(); }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        return plugin != nullptr ? soundOf (*plugin, rate) : nullptr;
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        if (plugin != nullptr)
            setSound (*plugin, std::move (audio), rate);
    }

private:
    std::unique_ptr<juce::FileChooser> chooser;
};

AQUANODE_REGISTER_MODULE (GrainFreezePlayerModule, GrainFreezePlayerModule::descriptor)

//==============================================================================
namespace aquanode::plugins::grainfreeze
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int player = b.add ("plugin.grainfreeze.player", 0, 0);
        b.copy (player, original);

        if (auto* m = dynamic_cast<GrainFreezePlayerModule*> (b.dsp (player)))
        {
            m->copyStateFrom (original);
            double rate = 44100.0;
            if (auto sound = soundOf (original, rate))
                m->restoreSavedAudio (std::make_shared<juce::AudioBuffer<float>> (*sound), rate);
        }

        PatchPorts ports;
        ports.name = "GrainFreeze";
        ports.audioOut = { { player, "audioOut" } };
        ports.midiIn = { { player, "midiIn" } };
        return ports;
    }

    // a "preset" is a sound to play
    static bool loadPreset (juce::AudioProcessor& original, const juce::File& file)
    {
        auto* g = dynamic_cast<GF*> (&original);
        if (g == nullptr)
            return false;
        g->loadAudioFile (file);
        return g->isAudioLoaded();
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, loadPreset);
}
