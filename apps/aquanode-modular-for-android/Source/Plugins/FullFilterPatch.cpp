//==============================================================================
// FullFilter - Patch version
//
// FullFilter is one filter - a bank of up to 128 bells stacked on the harmonic
// series of a note (an EQ in the shape of a sawtooth), played by MIDI with up
// to twelve stacks at once - so its patch version is one module, "FullFilter
// - Bell Bank": FullFilter's own engine (the base stack that holds its last
// note and glides, the extra stacks with their envelopes, the 1/sqrt(N) level
// compensation), with FullFilter's own bell display and its two bar editors in
// the card - drag the bars to set each bell's level and tuning. Every knob
// takes cables. The acoustic shapes, the square wave (even bells muted) and
// Reset Freq are buttons, as in the plugin, and the imported wavetable is
// saved with the patch.
//==============================================================================
#include "PluginFullFilterConfig.h"

namespace aqp_fullfilter
{
    #include AQP_FULLFILTER_SRC(PluginProcessor.h)
    #include AQP_FULLFILTER_SRC(PluginEditor.h)
}

#include "PatchKit.h"

using namespace aquanode;
using namespace aquanode::plugins;

namespace
{
    const juce::String kPlugin { "fullfilter" };
    using FF = aqp_fullfilter::FullFilterAudioProcessor;

    std::shared_ptr<const juce::AudioBuffer<float>> wavetableOf (juce::AudioProcessor& p, double& rate)
    {
        auto* f = dynamic_cast<FF*> (&p);
        if (f == nullptr || ! f->hasWavetableLoaded())
            return nullptr;
        const auto& w = f->getWavetableSamples();
        auto buffer = std::make_shared<juce::AudioBuffer<float>> (1, (int) w.size());
        buffer->copyFrom (0, 0, w.data(), (int) w.size());
        rate = 44100.0;   // a wavetable has no rate of its own
        return buffer;
    }

    void setWavetable (juce::AudioProcessor& p, std::shared_ptr<juce::AudioBuffer<float>> audio, double)
    {
        auto* f = dynamic_cast<FF*> (&p);
        if (f == nullptr || audio == nullptr || audio->getNumSamples() <= 0)
            return;
        const auto path = f->getWavetableFilePath();
        // the bells' levels came back with the state already: only the frames to scan
        f->loadWavetableSamples (audio->getReadPointer (0), audio->getNumSamples(),
                                 path.isNotEmpty() ? juce::File (path).getFileName() : juce::String ("Wavetable"),
                                 path, false);
    }

    // the GUI card keeps its wavetable in the patch too
    const bool audioRegistered = registerAudioAccess (kPlugin, { wavetableOf, setWavetable });

    // FullFilter's bell display over its volume and frequency bar editors
    class BellBankView final : public juce::Component
    {
    public:
        explicit BellBankView (FF& p)
            : visualizer (p),
              volumeBars (p, aqp_fullfilter::BellBarEditor::Mode::Volume),
              freqBars (p, aqp_fullfilter::BellBarEditor::Mode::Frequency)
        {
            addAndMakeVisible (visualizer);
            addAndMakeVisible (volumeBars);
            addAndMakeVisible (freqBars);
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (12.0f);
            g.drawText ("Bell Level", volumeLabel, juce::Justification::centredLeft);
            g.drawText ("Bell Tuning", freqLabel, juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            visualizer.setBounds (r.removeFromTop (r.getHeight() * 2 / 5).reduced (0, 2));
            auto labels = r.removeFromTop (16);
            const int half = r.getWidth() / 2;
            volumeLabel = labels.removeFromLeft (half).withTrimmedLeft (4);
            freqLabel = labels.withTrimmedLeft (8);
            volumeBars.setBounds (r.removeFromLeft (half).reduced (0, 2).withTrimmedRight (4));
            freqBars.setBounds (r.reduced (0, 2).withTrimmedLeft (4));
        }
    private:
        aqp_fullfilter::BellVisualizer visualizer;
        aqp_fullfilter::BellBarEditor volumeBars, freqBars;
        juce::Rectangle<int> volumeLabel, freqLabel;
    };
}

//==============================================================================
class FullFilterBankModule final : public kit::InstanceModule
{
public:
    static ModuleDescriptor descriptor()
    {
        ModuleDescriptor d;
        d.typeId = "plugin.fullfilter.bank";
        d.displayName = kit::moduleName (kPlugin, "Bell Bank");
        d.description = "FullFilter whole: bell filters on every harmonic of the played note (Root follows the "
                        "keys and holds the last one; Glide slides between them with one voice). Polyphony "
                        "lets extra notes add stacks of their own, shaped by the envelope when ADSR is on. "
                        "Low Pass tilts the bells down the series, Amount sets how many there are. Drag the bars "
                        "to set each bell's level and tuning, or load a wavetable and scan it with Position.";
        d.section = ModuleSection::Filter;
        d.accent = kit::accentOf (kPlugin);
        d.sidebarOrder = 400;
        d.sockets = { audioIn ("audioIn", "Audio In"), midiIn ("midiIn", "Midi In"), audioOut ("audioOut", "Audio Out") };
        d.params = {
            makeRotary ("root", "Root", 20.0f, 2000.0f, 110.0f, 0, "Hz", true),
            makeRotary ("level", "Level", -24.0f, 48.0f, 12.0f, 0, "dB"),
            makeRotary ("lowpass", "Low Pass", 0.2f, 1.0f, 0.2f, 0),
            makeRotary ("q", "Q", 10.0f, 50.0f, 20.0f, 0, {}, true),
            makeRotary ("amount", "Amount", 1.0f, 128.0f, 64.0f, 0, {}, false, 1.0f),
            makeRotary ("glide", "Glide", 0.0f, 2.0f, 0.0f, 0, "s"),
            makeRotary ("polyphony", "Polyphony", 1.0f, 12.0f, 12.0f, 0, {}, false, 1.0f),

            makeCombo ("adsrEnabled", "ADSR", { "Off", "On" }, 0, 1, 2),
            makeRotary ("attack", "Attack", 0.0f, 2.0f, 0.03f, 1, "s"),
            makeRotary ("decay", "Decay", 0.0f, 2.0f, 0.1f, 1, "s").visibleWhen ("adsrEnabled", 1.0f),
            makeRotary ("sustain", "Sustain", 0.0f, 1.0f, 1.0f, 1).visibleWhen ("adsrEnabled", 1.0f),
            makeRotary ("release", "Release", 0.0f, 3.0f, 0.2f, 1, "s").visibleWhen ("adsrEnabled", 1.0f),
            makeRotary ("wavetablePosition", "Position", 0.0f, 1.0f, 0.0f, 1),
            makeCombo ("wavetableMode", "Wavetable To", { "Volume", "Filter Position", "Both" }, 0, 1, 2),

            makeButton ("loadWavetable", "Load Wavetable", 2, 2),
            makeButton ("square", "Square Wave", 2, 2),
            makeButton ("resetFreq", "Reset Freq", 2, 2),

            makeButton ("beam", "Beam", 3, 1),
            makeButton ("bell", "Bell", 3, 1),
            makeButton ("plate", "Plate", 3, 1),
            makeButton ("vibraphone", "Vibes", 3, 1),
            makeButton ("marimba", "Marimba", 3, 1)
        };
        return d;
    }

    FullFilterBankModule() : InstanceModule (kPlugin, true) {}

    FF* bank() const { return dynamic_cast<FF*> (plugin.get()); }

    void uiButtonClicked (const juce::String& id) override
    {
        auto* f = bank();
        if (f == nullptr)
            return;
        using Preset = FF::AcousticPreset;
        if (id == "square")          f->setEvenHarmonicsMuted (! f->getEvenHarmonicsMuted());
        else if (id == "resetFreq")  f->resetAllBellFrequencies();
        else if (id == "beam")       f->applyAcousticPreset (Preset::Beam);
        else if (id == "bell")       f->applyAcousticPreset (Preset::Bell);
        else if (id == "plate")      f->applyAcousticPreset (Preset::Plate);
        else if (id == "vibraphone") f->applyAcousticPreset (Preset::Vibraphone);
        else if (id == "marimba")    f->applyAcousticPreset (Preset::Marimba);
        else if (id == "loadWavetable")
        {
            chooser = std::make_unique<juce::FileChooser> ("Import a wavetable (2048-sample frames)", juce::File(), "*.wav");
            juce::WeakReference<SynthModule> weak (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [weak] (const juce::FileChooser& fc)
                {
                    if (auto* self = dynamic_cast<FullFilterBankModule*> (weak.get()))
                        if (auto* f2 = self->bank(); f2 != nullptr && fc.getResult().existsAsFile())
                            f2->loadWavetableFile (fc.getResult());
                });
        }
    }

    std::unique_ptr<juce::Component> createExtraContentComponent() override
    {
        return bank() != nullptr ? std::make_unique<BellBankView> (*bank()) : nullptr;
    }
    int extraContentHeight() const override { return 250; }
    int preferredModuleWidth() const override { return 760; }

    bool hasAudioToSave() const override { return bank() != nullptr && bank()->hasWavetableLoaded(); }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioToSave (double& rate) const override
    {
        return plugin != nullptr ? wavetableOf (*plugin, rate) : nullptr;
    }
    void restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate) override
    {
        if (plugin != nullptr)
            setWavetable (*plugin, std::move (audio), rate);
    }

private:
    std::unique_ptr<juce::FileChooser> chooser;
};

AQUANODE_REGISTER_MODULE (FullFilterBankModule, FullFilterBankModule::descriptor)

//==============================================================================
namespace aquanode::plugins::fullfilter
{
    PatchPorts buildPatch (AquanodeModularAudioProcessor& patch, juce::AudioProcessor& original,
                           juce::Point<int> origin)
    {
        kit::Builder b { patch, origin };
        const int bank = b.add ("plugin.fullfilter.bank", 0, 0);
        b.copy (bank, original);

        if (auto* m = dynamic_cast<FullFilterBankModule*> (b.dsp (bank)))
        {
            // the bells' edits and the wavetable exactly as the plugin has them
            m->copyStateFrom (original);
            double rate = 44100.0;
            if (auto w = wavetableOf (original, rate))
                m->restoreSavedAudio (std::make_shared<juce::AudioBuffer<float>> (*w), rate);
        }

        PatchPorts ports;
        ports.name = "FullFilter";
        ports.audioIn = { { bank, "audioIn" } };
        ports.midiIn = { { bank, "midiIn" } };
        ports.audioOut = { { bank, "audioOut" } };
        return ports;
    }

    static const bool registered = registerPatchVersion (kPlugin, buildPatch, nullptr);
}
