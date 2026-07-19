#include "ModuleCore.h"

namespace aquanode
{

//==============================================================================
ModuleFactory& ModuleFactory::instance()
{
    // Meyer's singleton: function-local static sidesteps the static-init-order
    // problem across the per-module registration translation units.
    static ModuleFactory factory;
    return factory;
}

//==============================================================================
static juce::AudioFormatManager& sharedFormatManager()
{
    static juce::AudioFormatManager manager;
    static bool initialised = false;
    if (! initialised)
    {
        manager.registerBasicFormats();
        initialised = true;
    }
    return manager;
}

void SynthModule::loadSampleFromFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (sharedFormatManager().createReaderFor (file));
    if (reader == nullptr)
        return;

    const int numSamples = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 60ll * 4 * (juce::int64) reader->sampleRate);
    const int numChannels = juce::jmax (1, juce::jmin (2, (int) reader->numChannels));

    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, juce::jmax (1, numSamples));
    buffer->clear();
    reader->read (buffer.get(), 0, numSamples, 0, true, numChannels > 1);

    setLoadedSample (std::move (buffer), reader->sampleRate);
}

void SynthModule::openSampleChooser()
{
    activeChooser = std::make_unique<juce::FileChooser> (
        "Load Sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");

    juce::WeakReference<SynthModule> weakThis (this);
    activeChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weakThis] (const juce::FileChooser& fc)
        {
            if (auto* self = weakThis.get())
            {
                const auto file = fc.getResult();
                if (file.existsAsFile())
                    self->loadSampleFromFile (file);
            }
        });
}

} // namespace aquanode
