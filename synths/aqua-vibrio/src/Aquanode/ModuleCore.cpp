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
        manager.registerBasicFormats();   // WAV, AIFF, FLAC, Ogg Vorbis - but not MP3
       #if JUCE_USE_MP3AUDIOFORMAT
        manager.registerFormat (new juce::MP3AudioFormat(), false);
       #endif
        initialised = true;
    }
    return manager;
}

//==============================================================================
// Reads whatever the picker handed back into the module's sample buffer.
// Split out from loadSampleFromFile so both the desktop File path and the
// Android document-stream path end up in the same place. Takes ownership of
// the stream (AudioFormatManager's stream overload does too).
void SynthModule::loadSampleFromStream (std::unique_ptr<juce::InputStream> stream)
{
    if (stream == nullptr)
        return;

    std::unique_ptr<juce::AudioFormatReader> reader (
        sharedFormatManager().createReaderFor (std::move (stream)));
    if (reader == nullptr)
        return;

    const int numSamples = (int) std::min<juce::int64> (reader->lengthInSamples, 60ll * 4 * (juce::int64) reader->sampleRate);
    const int numChannels = juce::jmax (1, juce::jmin (2, (int) reader->numChannels));

    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, juce::jmax (1, numSamples));
    buffer->clear();
    reader->read (buffer.get(), 0, numSamples, 0, true, numChannels > 1);

    setLoadedSample (std::move (buffer), reader->sampleRate);
}

void SynthModule::loadSampleFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    loadSampleFromStream (std::make_unique<juce::FileInputStream> (file));
}

#if JUCE_ANDROID
void SynthModule::loadSampleFromURL (const juce::URL& url)
{
    if (url.isEmpty())
        return;

    // Same route the patch import/export in PluginEditor already takes: the
    // picker hands back a content:// document URI, which has no filesystem
    // path, so it has to be read as a stream.
    auto doc = juce::AndroidDocument::fromDocument (url);
    if (! doc.hasValue())
        return;

    auto in = doc.createInputStream();
    if (in == nullptr)
        return;

    // Buffer the whole document before decoding. A content:// stream is a pipe
    // from the ContentResolver and does not reliably support setPosition(),
    // but every AudioFormatReader needs to seek: MP3 has no header giving the
    // length, so MP3AudioFormat scans the frames and then rewinds, and it
    // simply fails on a forward-only stream. Reading into memory first makes
    // the stream seekable and costs nothing we were not already paying, since
    // the loader caps the sample at four minutes anyway.
    juce::MemoryBlock mb;
    in->readIntoMemoryBlock (mb);
    if (mb.getSize() == 0)
        return;

    loadSampleFromStream (std::make_unique<juce::MemoryInputStream> (std::move (mb)));
}
#endif

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
               #if JUCE_ANDROID
                // getResult() cannot represent a content:// URI: it comes back
                // as a File that fails existsAsFile(), so the old code returned
                // here silently and no sample was ever loaded.
                self->loadSampleFromURL (fc.getURLResult());
               #else
                const auto file = fc.getResult();
                if (file.existsAsFile())
                    self->loadSampleFromFile (file);
               #endif
            }
        });
}

} // namespace aquanode
