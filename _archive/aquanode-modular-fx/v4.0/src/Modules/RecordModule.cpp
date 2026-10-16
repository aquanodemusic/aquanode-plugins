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
    recording.store (false, std::memory_order_relaxed);

    // Keep the existing take (one restored from a preset, or recorded before
    // the host re-prepared us at another rate) instead of wiping it.
    const int keep = recordedCount.load (std::memory_order_relaxed);
    const int capacity = juce::jmax (1, (int) (sr * maxRecordSeconds), keep);
    bufL.resize ((size_t) capacity, 0.0f);
    bufR.resize ((size_t) capacity, 0.0f);
}

bool RecordModule::hasAudioToSave() const
{
    return ! recording.load (std::memory_order_acquire) && recordedCount.load (std::memory_order_acquire) > 0;
}

std::shared_ptr<const juce::AudioBuffer<float>> RecordModule::getAudioToSave (double& rate) const
{
    if (! hasAudioToSave())
        return nullptr;

    const int frames = recordedCount.load (std::memory_order_acquire);
    auto take = std::make_shared<juce::AudioBuffer<float>> (2, frames);
    take->copyFrom (0, 0, bufL.data(), frames);
    take->copyFrom (1, 0, bufR.data(), frames);
    rate = takeRate > 0.0 ? takeRate : sampleRate;
    return take;
}

void RecordModule::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
{
    if (audio == nullptr || audio->getNumSamples() <= 0)
        return;

    const int frames = audio->getNumSamples();
    if ((int) bufL.size() < frames)
    {
        bufL.resize ((size_t) frames, 0.0f);
        bufR.resize ((size_t) frames, 0.0f);
    }
    std::copy (audio->getReadPointer (0), audio->getReadPointer (0) + frames, bufL.begin());
    const int rch = audio->getNumChannels() > 1 ? 1 : 0;
    std::copy (audio->getReadPointer (rch), audio->getReadPointer (rch) + frames, bufR.begin());
    takeRate = rate;
    recordedCount.store (frames, std::memory_order_release);
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
        takeRate = sampleRate;
        recording.store (true, std::memory_order_release);
    }
    else if (paramId == "clear")
    {
        // throw the take away (it is no longer saved with the patch either)
        recording.store (false, std::memory_order_release);
        recordedCount.store (0, std::memory_order_release);
        takeRate = 0.0;
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
    const double sr = takeRate > 0.0 ? takeRate : sampleRate;   // a restored take keeps its own rate

    // native OS "Save As" dialog (Explorer on Windows, Finder on macOS)
    activeSaveChooser->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [weakThis, framesToWrite, sr, useFlac, bitDepth] (const juce::FileChooser& fc)
        {
            auto* self = static_cast<RecordModule*> (weakThis.get());
            if (self == nullptr)
                return;

            auto file = fc.getResult();
            if (file == juce::File{})
                return;   // dialog was cancelled

            self->writeToFile (file, framesToWrite, sr, useFlac, bitDepth);
        });
}

void RecordModule::writeToFile (const juce::File& file, int numFrames, double sr, bool useFlac, int bitDepth)
{
    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return;

    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (useFlac)
    {
        juce::FlacAudioFormat flacFormat;
        // JUCE's FLAC writer doesn't expose libFLAC's numeric 0-8 compression
        // levels directly - quality option 0 is its highest/only available
        // setting, which is the closest thing to "compression 8" it offers.
        writer.reset (flacFormat.createWriterFor (stream.get(), sr, 2, bitDepth, {}, 0));
    }
    else
    {
        juce::WavAudioFormat wavFormat;
        writer.reset (wavFormat.createWriterFor (stream.get(), sr, 2, bitDepth, {}, 0));
    }

    if (writer == nullptr)
        return;

    stream.release();   // the writer now owns the stream

    const float* channels[2] = { bufL.data(), bufR.data() };
    writer->writeFromFloatArrays (channels, 2, numFrames);
    writer->flush();
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
        "FLAC file at 16 or 24 bit. Auto-stops after 2 minutes. The take is kept with the patch (presets "
        "and DAW sessions) until Delete throws it away.";
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
        makeButton ("save",     "Save",       1, 1),
        makeButton ("clear",    "Delete",     1, 2)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (RecordModule, recordDescriptor)
