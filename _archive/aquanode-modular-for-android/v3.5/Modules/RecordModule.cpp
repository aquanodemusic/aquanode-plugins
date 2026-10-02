#include "RecordModule.h"

using namespace aquanode;

//==============================================================================
// tiny status readout: shows idle/recording/last-take state and a running
// clock, so Start/Stop/Save aren't flying blind
class RecordStatusDisplay : public juce::Component,
                            private juce::Timer
{
public:
    explicit RecordStatusDisplay (SynthModule& moduleToShow)
        : module (&moduleToShow)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

        auto* m = static_cast<RecordModule*> (module.get());
        if (m == nullptr)
            return;

        const bool rec = m->isRecording();
        const double secs = m->recordedSeconds();
        const int mins = (int) secs / 60;
        const int rem = (int) secs % 60;

        juce::String text;
        if (rec)
            text << "REC  " << juce::String (mins) << ":" << juce::String (rem).paddedLeft ('0', 2)
                 << " / 2:00";
        else if (secs > 0.0)
            text << "Stopped - " << juce::String (mins) << ":" << juce::String (rem).paddedLeft ('0', 2)
                 << " recorded";
        else
            text << "Idle - " << juce::String (m->currentSampleRate() / 1000.0, 1) << " kHz";

        g.setColour (rec ? juce::Colours::red.brighter (0.2f) : juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (text, getLocalBounds().reduced (4, 0), juce::Justification::centredLeft);
    }

private:
    void timerCallback() override { repaint(); }

    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> RecordModule::createExtraContentComponent()
{
    return std::make_unique<RecordStatusDisplay> (*this);
}

//==============================================================================
void RecordModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    const int capacity = juce::jmax (1, (int) (sr * maxRecordSeconds));
    bufL.assign ((size_t) capacity, 0.0f);
    bufR.assign ((size_t) capacity, 0.0f);
    recordedCount.store (0, std::memory_order_relaxed);
    recording.store (false, std::memory_order_relaxed);
}

void RecordModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    // pure pass-through - Record is a tap on the signal, not an insert
    outputs[0] = inputs[0];

    if (! recording.load (std::memory_order_acquire))
        return;

    const int idx = recordedCount.load (std::memory_order_relaxed);
    const int capacity = (int) bufL.size();
    if (idx >= capacity)
    {
        recording.store (false, std::memory_order_release);   // auto-stop: 2 minutes reached
        return;
    }

    bufL[(size_t) idx] = inputs[0][0];
    bufR[(size_t) idx] = inputs[0][1];
    recordedCount.store (idx + 1, std::memory_order_release);
}

void RecordModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "start")
    {
        // Start always begins a fresh take; use Save first if the previous
        // one is still needed.
        recordedCount.store (0, std::memory_order_release);
        recording.store (true, std::memory_order_release);
    }
    else if (paramId == "stop")
    {
        recording.store (false, std::memory_order_release);
    }
    else if (paramId == "save")
    {
        saveToFile();
    }
}

void RecordModule::saveToFile()
{
    recording.store (false, std::memory_order_release);   // saving mid-take stops it first

    const int numFrames = recordedCount.load (std::memory_order_acquire);
    if (numFrames <= 0)
        return;

    const bool useFlac = getParameter ("format") > 0.5f;
    const int bitDepth = getParameter ("bitDepth") > 0.5f ? 24 : 16;
    const juce::String ext = useFlac ? ".flac" : ".wav";

    const auto defaultFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                 .getNonexistentChildFile ("Recording", ext, false);

    activeSaveChooser = std::make_unique<juce::FileChooser> (
        "Save Recording", defaultFile, useFlac ? "*.flac" : "*.wav");

    juce::WeakReference<SynthModule> weakThis (this);
    const int framesToWrite = numFrames;
    const double sr = sampleRate;

    // native OS "Save As" dialog (Explorer/Finder on desktop, SAF "create
    // document" picker on Android). getURLResult() rather than getResult():
    // a content:// SAF pick comes back from getResult() as a juce::File that
    // fails existsAsFile(), the same trap PluginEditor::saveSelection()
    // works around in the AudioStretcher app - the URL is the only form of
    // the result that's valid on every platform.
    activeSaveChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [weakThis, framesToWrite, sr, useFlac, bitDepth] (const juce::FileChooser& fc)
        {
            auto* self = static_cast<RecordModule*> (weakThis.get());
            if (self == nullptr)
                return;

            auto url = fc.getURLResult();
            if (url.isEmpty())
                return;   // dialog was cancelled

            self->writeToDestination (url, framesToWrite, sr, useFlac, bitDepth);
        });
}

void RecordModule::writeToDestination (const juce::URL& destination, int numFrames, double sr, bool useFlac, int bitDepth)
{
    // Encode to a private scratch file first rather than writing straight
    // into the SAF/content:// destination - some device storage providers
    // are unreliable about committing bytes written directly through a
    // content:// OutputStream even when every call reports success. The
    // app's own temp directory is always a plain filesystem location, so
    // encoding there is unaffected by any of that and gives us a known-good
    // file to copy from. Same approach as processAndExport() in the
    // AudioStretcher app.
    const juce::String ext = useFlac ? ".flac" : ".wav";
    auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getNonexistentChildFile ("AquanodeRecording", ext, false);

    {
        std::unique_ptr<juce::FileOutputStream> stream (tempFile.createOutputStream());
        if (stream == nullptr)
            return;

        std::unique_ptr<juce::AudioFormatWriter> writer;
        if (useFlac)
        {
            juce::FlacAudioFormat flacFormat;
            // JUCE's FLAC writer doesn't expose libFLAC's numeric 0-8
            // compression levels directly - quality option 0 is its
            // highest/only available setting, which is the closest thing to
            // "compression 8" it offers.
            writer.reset (flacFormat.createWriterFor (stream.get(), sr, 2, bitDepth, {}, 0));
        }
        else
        {
            juce::WavAudioFormat wavFormat;
            writer.reset (wavFormat.createWriterFor (stream.get(), sr, 2, bitDepth, {}, 0));
        }

        if (writer == nullptr)
        {
            tempFile.deleteFile();
            return;
        }

        stream.release();   // the writer now owns the stream

        const float* channels[2] = { bufL.data(), bufR.data() };
        writer->writeFromFloatArrays (channels, 2, numFrames);
        writer->flush();
    }

    if (tempFile.getSize() == 0)
    {
        tempFile.deleteFile();
        return;
    }

    // Now copy the known-good scratch file to the real destination: a plain
    // FileOutputStream if it's a real path, falling back to an
    // AndroidDocument stream for a content:// URI (or if the "local" path
    // turns out to be one of the scoped-storage paths that only looks
    // writable - see loadAudioFile()'s equivalent fallback in the
    // AudioStretcher app).
    std::unique_ptr<juce::OutputStream> outputStream;

    if (destination.isLocalFile())
    {
        auto outputFile = destination.getLocalFile();
        outputFile.deleteFile();

        std::unique_ptr<juce::FileOutputStream> fileStream (outputFile.createOutputStream());
        if (fileStream != nullptr && fileStream->openedOk())
            outputStream = std::move (fileStream);
    }

    if (outputStream == nullptr)
    {
       #if JUCE_ANDROID
        auto androidDoc = juce::AndroidDocument::fromDocument (destination);
        if (androidDoc.hasValue())
            outputStream = androidDoc.createOutputStream();
       #else
        outputStream = destination.createOutputStream();
       #endif
    }

    if (outputStream == nullptr)
    {
        tempFile.deleteFile();
        return;
    }

    {
        juce::FileInputStream tempIn (tempFile);
        if (tempIn.openedOk())
        {
            outputStream->writeFromInputStream (tempIn, -1);
            outputStream->flush();
        }
    }

    outputStream.reset();   // close/commit the destination before cleaning up
    tempFile.deleteFile();
}

//==============================================================================
static ModuleDescriptor recordDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.record";
    d.displayName = "Record";
    d.description =
        "Taps whatever passes through it and records it, at whatever sample rate this instance "
        "is actually running at (the host's rate in a DAW, the audio device's rate standalone, or "
        "44.1 kHz if neither is known yet). Save opens a native file dialog and writes a WAV or "
        "FLAC file at 16 or 24 bit. Auto-stops after 2 minutes.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 26;
    d.sockets = {
        audioIn  ("audioIn",  "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeCombo  ("format",   "Format",     { "WAV", "FLAC" }, 0, 0, 2),
        makeCombo  ("bitDepth", "Bit Depth",  { "16-bit", "24-bit" }, 0, 0, 2),
        makeButton ("start",    "Start",      1, 1),
        makeButton ("stop",     "Stop",       1, 1),
        makeButton ("save",     "Save",       1, 1)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RecordModule, recordDescriptor)
