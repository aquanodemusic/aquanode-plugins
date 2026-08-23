#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <vector>

// Include Rubber Band Library
// You'll need to add RubberBandSingle.cpp to your project
// Download from: https://github.com/breakfastquay/rubberband
#ifdef USE_RUBBERBAND
#include "Rubberband/rubberband/RubberBandStretcher.h"
using namespace RubberBand;
#endif

AudioStretcherAudioProcessor::AudioStretcherAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    addParameter(pitchShiftParam = new juce::AudioParameterFloat(
        "pitchShift",
        "Pitch Shift",
        juce::NormalisableRange<float>(-36.0f, 36.0f, 0.01f),
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + " st"; }
    ));

    addParameter(fineTuneParam = new juce::AudioParameterFloat(
        "fineTune",
        "Fine Tune",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 1) + " ct"; }
    ));

    // 0.2x .. 4x, skewed so 1x (unity) sits at the centre of the range -
    // matches the skew applied to the GUI knob in PluginEditor.cpp.
    juce::NormalisableRange<float> speedRange(0.2f, 4.0f, 0.01f);
    speedRange.setSkewForCentre(1.0f);

    addParameter(timeStretchParam = new juce::AudioParameterFloat(
        "timeStretch",
        "Time Stretch",
        speedRange,
        1.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 2) + "x"; }
    ));

    // 0 .. 5 seconds, skewed so the low end of the range - up to roughly
    // 200ms, where short fades live - gets noticeably more slider/knob
    // travel than the rest of the range. setSkewForCentre(0.2) puts the
    // 0.2s point at the midpoint of the control.
    juce::NormalisableRange<float> fadeRange(0.0f, 5.0f, 0.001f);
    fadeRange.setSkewForCentre(0.2f);

    addParameter(fadeInParam = new juce::AudioParameterFloat(
        "fadeIn",
        "Fade In",
        fadeRange,
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 3) + " s"; }
    ));

    addParameter(fadeOutParam = new juce::AudioParameterFloat(
        "fadeOut",
        "Fade Out",
        fadeRange,
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(value, 3) + " s"; }
    ));
}

AudioStretcherAudioProcessor::~AudioStretcherAudioProcessor()
{
}

const juce::String AudioStretcherAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioStretcherAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool AudioStretcherAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool AudioStretcherAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double AudioStretcherAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioStretcherAudioProcessor::getNumPrograms()
{
    return 1;
}

int AudioStretcherAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioStretcherAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String AudioStretcherAudioProcessor::getProgramName(int index)
{
    return {};
}

void AudioStretcherAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

void AudioStretcherAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
}

void AudioStretcherAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AudioStretcherAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void AudioStretcherAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Handle preview playback. previewBuffer can be resized out from under
    // us at any moment by clearPreview() (message thread, fired on every
    // knob tweak) or by renderPreview() finishing (background thread), and
    // AudioBuffer::setSize() is not safe to race against a reader. A
    // try-lock means the audio thread is never blocked by either of them:
    // if we lose the race we just leave this block silent instead of
    // touching a buffer that's mid-resize.
    const juce::SpinLock::ScopedTryLockType previewTryLock(previewLock);

    if (previewTryLock.isLocked() && previewPlaying && previewBuffer.getNumSamples() > 0)
    {
        int numSamples = buffer.getNumSamples();
        int numChannels = juce::jmin(buffer.getNumChannels(), previewBuffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            if (previewPlayPosition >= previewBuffer.getNumSamples())
            {
                if (previewLooping)
                {
                    // Wrap back to the start and keep filling this block.
                    previewPlayPosition = 0;
                }
                else
                {
                    // Reached end of preview
                    previewPlaying = false;
                    previewPlayPosition = 0;

                    // Clear remaining samples
                    for (int ch = 0; ch < numChannels; ++ch)
                        for (int j = i; j < numSamples; ++j)
                            buffer.setSample(ch, j, 0.0f);
                    break;
                }
            }

            // Copy to all channels
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, previewBuffer.getSample(ch, previewPlayPosition));
            previewPlayPosition++; // Increment ONCE per sample, not per channel
        }
    }
}

bool AudioStretcherAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AudioStretcherAudioProcessor::createEditor()
{
    return new AudioStretcherAudioProcessorEditor(*this);
}

void AudioStretcherAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
#if JucePlugin_Build_Standalone
    // The standalone app (Windows .exe / Android .apk) has no host to
    // recall a session for, and JUCE's standalone wrapper otherwise
    // persists this into its own properties file and restores it on next
    // launch. That silently desynced knob display from engine state
    // before (see the earlier speed-doubling bug) and, more generally,
    // just isn't wanted here: every standalone launch should behave like
    // the very first ever run. Writing nothing means there is nothing for
    // the wrapper to restore later.
    juce::ignoreUnused(destData);
#else
    juce::MemoryOutputStream stream(destData, true);

    stream.writeFloat(*pitchShiftParam);
    stream.writeFloat(*timeStretchParam);
    stream.writeFloat(*fineTuneParam);
    stream.writeFloat(*fadeInParam);
    stream.writeFloat(*fadeOutParam);
#endif
}

void AudioStretcherAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
#if JucePlugin_Build_Standalone
    // See getStateInformation() - standalone never recalls old state, so
    // there is nothing to read back here either. Parameters simply keep
    // whatever they were constructed with (1.0x / 0st / 0ct).
    juce::ignoreUnused(data, sizeInBytes);
#else
    juce::MemoryInputStream stream(data, static_cast<size_t> (sizeInBytes), false);

    pitchShiftParam->setValueNotifyingHost(stream.readFloat());
    timeStretchParam->setValueNotifyingHost(stream.readFloat());

    // Older saves included a "use naive method" bool at this point that no
    // longer exists as a parameter (RubberBand is now the only engine) -
    // consume the byte anyway so fineTune still lines up for old presets.
    if (!stream.isExhausted())
        stream.readBool();

    if (!stream.isExhausted())
        fineTuneParam->setValueNotifyingHost(stream.readFloat());

    // Fades weren't part of older saves either - guard the same way so
    // those presets still load cleanly, just with fades defaulting to off.
    if (!stream.isExhausted())
        fadeInParam->setValueNotifyingHost(stream.readFloat());

    if (!stream.isExhausted())
        fadeOutParam->setValueNotifyingHost(stream.readFloat());
#endif
}

void AudioStretcherAudioProcessor::loadAudioFile(const juce::URL& audioFileURL)
{
    juce::Logger::writeToLog("loadAudioFile: url=\"" + audioFileURL.toString(true)
        + "\" isLocalFile=" + (audioFileURL.isLocalFile() ? "yes" : "no"));

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg Vorbis - but NOT mp3
#if JUCE_USE_MP3AUDIOFORMAT
    formatManager.registerFormat(new juce::MP3AudioFormat(), false);
    juce::Logger::writeToLog("loadAudioFile: JUCE_USE_MP3AUDIOFORMAT is ON - MP3 reader registered");
#else
    juce::Logger::writeToLog("loadAudioFile: JUCE_USE_MP3AUDIOFORMAT is OFF - MP3 files will not load");
#endif

    std::unique_ptr<juce::AudioFormatReader> reader;

    if (audioFileURL.isLocalFile())
    {
        // Real filesystem path - desktop (Windows/Mac/Linux), and the
        // occasional Android provider that hands back a URL that LOOKS like
        // a real path (isLocalFile() == true) because the document ID
        // literally embeds one - e.g. the Downloads provider returning
        // ".../document/raw:/storage/emulated/0/Download/foo.wav". Try the
        // direct read; it's the fast path when it's genuinely available.
        reader.reset(formatManager.createReaderFor(audioFileURL.getLocalFile()));
        if (reader == nullptr)
            juce::Logger::writeToLog("loadAudioFile: createReaderFor(local file) returned null "
                "- either the format isn't recognised, or (very likely on Android) this is a "
                "content provider whose URL merely LOOKS like a raw path - scoped storage still "
                "blocks direct access to it even though isLocalFile() said yes. Falling back to "
                "the content:// stream route below.");
    }

    // Falls through here whenever the direct-file attempt above didn't
    // happen (URL genuinely isn't local) OR it did happen but produced no
    // reader (Android's scoped storage silently refused a path that merely
    // looked local - see comment above). Either way, the only remaining
    // route is to open it as a document stream instead.
    //
    // juce::URL::createInputStream() is NOT reliable for content:// URIs -
    // its generic implementation isn't guaranteed to negotiate the SAF
    // permission grant correctly and can silently hand back a null stream.
    // juce::AndroidDocument is the class JUCE's own FileChooser uses
    // internally to wrap a content:// result, and its createInputStream()
    // is the correct, supported way to read one.
    if (reader == nullptr)
    {
#if JUCE_ANDROID
        auto androidDoc = juce::AndroidDocument::fromDocument(audioFileURL);
        if (! androidDoc.hasValue())
            juce::Logger::writeToLog("loadAudioFile: AndroidDocument::fromDocument() could not "
                "resolve this URI - the content:// URI itself may be invalid or the app never "
                "got a valid permission grant for it");

        auto stream = androidDoc.createInputStream();
#else
        auto stream = audioFileURL.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
#endif

        if (stream == nullptr)
        {
            juce::Logger::writeToLog("loadAudioFile: createInputStream() returned null - "
                "could not open a stream for this document at all");
        }
        else
        {
            // createReaderFor(stream) probes each registered format in turn
            // and seeks back to the start between attempts. The stream
            // ContentResolver hands back for a content:// URI isn't
            // reliably seekable, so that probing silently fails and reader
            // is left null with no indication why. Read the whole thing
            // into memory first - a MemoryInputStream is always seekable -
            // then probe that instead. Files here are short audio clips, so
            // buffering the whole thing is fine.
            juce::MemoryBlock fileData;
            stream->readIntoMemoryBlock(fileData);

            juce::Logger::writeToLog("loadAudioFile: read " + juce::String((int)fileData.getSize())
                + " bytes from the document stream");

            if (fileData.getSize() > 0)
            {
                reader.reset(formatManager.createReaderFor(
                    std::make_unique<juce::MemoryInputStream>(fileData, true)));
                if (reader == nullptr)
                    juce::Logger::writeToLog("loadAudioFile: createReaderFor(memory data) returned "
                        "null - the bytes came through, but no registered format could parse them "
                        "(check the file really is a valid WAV, and consider whether the content "
                        "provider handed back something other than raw audio - e.g. a redirect "
                        "or a thumbnail stub)");
            }
        }
    }

    if (reader != nullptr)
    {
        loadedBuffer.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
        reader->read(&loadedBuffer, 0, (int)reader->lengthInSamples, 0, true, true);
        loadedSampleRate = reader->sampleRate;

        juce::Logger::writeToLog("loadAudioFile: success - " + juce::String(reader->numChannels)
            + "ch, " + juce::String(reader->lengthInSamples) + " samples @ "
            + juce::String(reader->sampleRate) + "Hz");

        // getFileName() works for both a real path and a content:// URI -
        // for the latter JUCE resolves it to the document's display name
        // (e.g. via the ContentResolver), which is also where the
        // extension below actually comes from, since content:// URIs have
        // no path/extension of their own to inspect.
        loadedFileName = audioFileURL.getFileName();
        loadedFileExtension = loadedFileName.contains(".")
            ? loadedFileName.fromLastOccurrenceOf(".", true, false).toLowerCase()
            : juce::String();
        loadedURL = audioFileURL;

        // Preserve source bit depth where it means something (WAV/AIFF/FLAC
        // report a real fixed PCM depth). Compressed/lossy sources like MP3
        // or OGG don't have a native "bit depth" in this sense, so fall back
        // to a sensible default for whatever we end up writing them as.
        int bits = (int)reader->bitsPerSample;
        loadedBitsPerSample = (bits == 16 || bits == 24 || bits == 32) ? bits : 24;

        // Initialize region to full file
        regionStart = 0;
        regionEnd = loadedBuffer.getNumSamples();

        // Clear any previous preview
        previewBuffer.setSize(0, 0);
        stopPreview();
    }
}

bool AudioStretcherAudioProcessor::canExportInSourceFormat() const
{
    return loadedFileExtension == ".wav" || loadedFileExtension == ".aiff" ||
        loadedFileExtension == ".aif" || loadedFileExtension == ".flac";
}

juce::String AudioStretcherAudioProcessor::getRecommendedExportExtension() const
{
    if (loadedFileExtension == ".wav")
        return ".wav";
    if (loadedFileExtension == ".aiff" || loadedFileExtension == ".aif")
        return ".aiff";
    if (loadedFileExtension == ".flac")
        return ".flac";

    // MP3, OGG, etc: JUCE doesn't ship an encoder for these, so fall back
    // to a lossless format instead of silently mislabeling the output.
    return ".flac";
}

void AudioStretcherAudioProcessor::processAndExport(const juce::URL& outputURL)
{
    if (loadedBuffer.getNumSamples() == 0)
        return;

    // Fine tune is in cents; fold it into the semitone pitch value the
    // stretcher engines expect (100 cents == 1 semitone).
    float pitch = *pitchShiftParam + (*fineTuneParam / 100.0f);
    // The Speed knob is a playback-speed multiplier (0.7x = 70% speed =
    // slower, takes longer to play), but the stretch engines below all
    // expect a duration ratio - output length / input length - which is
    // the reciprocal of speed (half speed = twice the duration).
    float stretch = 1.0f / *timeStretchParam;

#ifndef USE_RUBBERBAND
    // RubberBand is the only engine that actually produces sound. Without
    // it there's nothing correct to fall back to, so do nothing rather
    // than exporting audio from a different, unrequested engine.
    return;
#else
    if (progressCallback)
        progressCallback(0.0f);
    if (stageCallback)
        stageCallback("Starting...");

    // Extract selected region with bounds checking
    int regionLength = regionEnd - regionStart;
    if (regionLength <= 0 || regionStart < 0 || regionEnd > loadedBuffer.getNumSamples())
        return;

    juce::AudioBuffer<float> regionBuffer(loadedBuffer.getNumChannels(), regionLength);

    for (int ch = 0; ch < loadedBuffer.getNumChannels(); ++ch)
    {
        regionBuffer.copyFrom(ch, 0, loadedBuffer, ch, regionStart, regionLength);
    }

    // Process the region directly - loadedBuffer itself is never touched,
    // so the UI thread can keep safely reading it to draw the waveform
    // while this (potentially slow) processing runs on a background thread.
    juce::AudioBuffer<float> processedBuffer = processWithRubberBand(regionBuffer, pitch, stretch);

    // Fade in/out are specified in seconds of output audio, at the rate
    // processedBuffer will actually be written out at.
    applyFades(processedBuffer, loadedSampleRate, *fadeInParam, *fadeOutParam);

    if (progressCallback)
        progressCallback(0.9f);
    if (stageCallback)
        stageCallback("Writing file...");

    // Extension the caller wants (mirrors getRecommendedExportExtension(),
    // which is what the editor used to pick this outputURL in the first
    // place). A content:// URI has no path to call getFileExtension() on,
    // so pull the extension out of its resolved display name instead.
    auto outName = outputURL.getFileName();
    auto outExt = outName.contains(".") ? outName.fromLastOccurrenceOf(".", true, false).toLowerCase() : juce::String();

    juce::Logger::writeToLog("processAndExport: url=\"" + outputURL.toString(true)
        + "\" isLocalFile=" + (outputURL.isLocalFile() ? "yes" : "no")
        + " processedBuffer=" + juce::String(processedBuffer.getNumChannels()) + "ch/"
        + juce::String(processedBuffer.getNumSamples()) + " samples");

    // Encode to a private scratch file first, rather than writing straight
    // into the SAF/content:// destination. This isn't just about the
    // isLocalFile() trap we already worked around elsewhere - some device
    // storage stacks (Samsung's One UI DocumentsProvider in particular)
    // have been unreliable about actually committing bytes written directly
    // through a content:// OutputStream, even when the write calls all
    // report success. The app's own temp directory is always a plain,
    // ordinary filesystem location with no SAF/ContentResolver involved at
    // all, so encoding here is unaffected by any of that, and gives us a
    // known-good file to verify before we even attempt the copy to wherever
    // the user actually asked to save.
    auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("AudioStretcherExport", outExt, false);

    {
        auto tempStream = std::make_unique<juce::FileOutputStream>(tempFile);
        if (!tempStream->openedOk())
        {
            juce::Logger::writeToLog("processAndExport: could not open scratch file at \""
                + tempFile.getFullPathName() + "\"");
            if (stageCallback)
                stageCallback("Failed to write file");
            if (progressCallback)
                progressCallback(1.0f);
            return;
        }

        bool writeOk;
        if (outExt == ".wav")
            writeOk = writeToWav(processedBuffer, loadedSampleRate, loadedBitsPerSample, std::move(tempStream));
        else if (outExt == ".aiff" || outExt == ".aif")
            writeOk = writeToAiff(processedBuffer, loadedSampleRate, loadedBitsPerSample, std::move(tempStream));
        else
            // FLAC (native match, or the fallback for MP3/OGG sources): FLAC
            // supports up to 32-bit, but 32-bit FLAC is poorly supported in
            // the wild, so cap it at 24 which covers every practical case.
            writeOk = writeToFlac(processedBuffer, loadedSampleRate, juce::jmin(loadedBitsPerSample, 24), std::move(tempStream));

        juce::Logger::writeToLog("processAndExport: scratch file encode "
            + juce::String(writeOk ? "reported success" : "reported failure") + ", size on disk = "
            + juce::String((juce::int64)tempFile.getSize()) + " bytes");

        if (!writeOk || tempFile.getSize() == 0)
        {
            tempFile.deleteFile();
            if (stageCallback)
                stageCallback("Failed to write file");
            if (progressCallback)
                progressCallback(1.0f);
            return;
        }
    }

    // Now copy the known-good scratch file to the real destination. This is
    // a single, simple, well-understood operation - much easier for a flaky
    // storage provider to get right than juggling an AudioFormatWriter
    // directly, and much easier for us to verify afterwards.
    std::unique_ptr<juce::OutputStream> outputStream;

    if (outputURL.isLocalFile())
    {
        auto outputFile = outputURL.getLocalFile();
        outputFile.deleteFile();

        auto fileStream = std::make_unique<juce::FileOutputStream>(outputFile);
        if (fileStream->openedOk())
            outputStream = std::move(fileStream);
        else
            juce::Logger::writeToLog("processAndExport: FileOutputStream failed to open at \""
                + outputFile.getFullPathName() + "\" - likely the same scoped-storage trap as on "
                "load: the URL LOOKS like a raw path (isLocalFile() said yes) but this app can't "
                "actually write there directly. Falling back to the content:// stream route.");
    }

    // Same fallback logic as loadAudioFile(): whether the URL wasn't local
    // to begin with, or claimed to be but the direct write failed, the
    // content:// document route is the one that's actually reliable on
    // Android. The destination almost certainly already exists as a
    // zero-byte placeholder at this point - Android's "create document"
    // SAF flow creates the file the instant the user confirms the save
    // dialog, before this function is even called - so if we silently
    // give up here that placeholder is exactly the empty file the user
    // sees afterwards.
    if (outputStream == nullptr)
    {
#if JUCE_ANDROID
        auto androidDoc = juce::AndroidDocument::fromDocument(outputURL);
        if (! androidDoc.hasValue())
            juce::Logger::writeToLog("processAndExport: AndroidDocument::fromDocument() could not "
                "resolve this output URI");

        outputStream = androidDoc.createOutputStream();
#else
        outputStream = outputURL.createOutputStream();
#endif
        if (outputStream == nullptr)
            juce::Logger::writeToLog("processAndExport: content:// createOutputStream() also "
                "returned null - could not open a writable stream to this document at all");
    }

    if (outputStream == nullptr)
    {
        tempFile.deleteFile();
        if (stageCallback)
            stageCallback("Couldn't open destination for writing");
        if (progressCallback)
            progressCallback(1.0f);
        return;
    }

    bool copyOk = false;
    {
        juce::FileInputStream tempIn(tempFile);
        if (tempIn.openedOk())
        {
            auto bytesCopied = outputStream->writeFromInputStream(tempIn, -1);
            outputStream->flush();
            copyOk = (bytesCopied == tempFile.getSize());
            juce::Logger::writeToLog("processAndExport: copied " + juce::String(bytesCopied)
                + " of " + juce::String((juce::int64)tempFile.getSize())
                + " scratch bytes to the destination");
        }
        else
        {
            juce::Logger::writeToLog("processAndExport: could not re-open scratch file for the copy");
        }
    }
    // Explicitly drop the stream here (rather than letting it fall out of
    // scope naturally further down) so the destination is closed - and,
    // hopefully, actually committed by whatever storage provider is behind
    // it - before we report success or failure back to the UI.
    outputStream.reset();

    tempFile.deleteFile();

    if (progressCallback)
        progressCallback(1.0f);
    if (stageCallback)
        stageCallback(copyOk ? "Complete!" : "Failed to write file");
#endif
}

#ifdef USE_RUBBERBAND
juce::AudioBuffer<float> AudioStretcherAudioProcessor::processWithRubberBand(
    const juce::AudioBuffer<float>& input,
    float pitchSemitones,
    float timeStretch)
{
    if (progressCallback)
        progressCallback(0.05f);
    if (stageCallback)
        stageCallback("Setup");

    using namespace RubberBand;

    int channels = input.getNumChannels();
    int inputSamples = input.getNumSamples();

    // Create Rubber Band stretcher with high quality settings
    // R3 ("Finer") engine: markedly less ringing/phasiness than the R2
    // default, especially on complex mixes, vocals, and bass-heavy
    // material - exactly the artifacts that show up as "ringing" after a
    // stretch/pitch-shift. It costs more CPU than R2, but we're already
    // offline and on a background thread, so that's essentially free here.
    // (OptionStretchPrecise and OptionPitchHighConsistency, which used to
    // be here, are both no-ops: OptionStretch* flags are obsolete and
    // ignored by the library, and OptionPitch* flags only have an effect
    // in real-time mode - we run OptionProcessOffline.)
    RubberBandStretcher stretcher(
        loadedSampleRate,
        channels,
        RubberBandStretcher::OptionProcessOffline |
        RubberBandStretcher::OptionEngineFiner |
        RubberBandStretcher::OptionThreadingNever
    );

    // Set time and pitch ratios
    stretcher.setTimeRatio(timeStretch);
    stretcher.setPitchScale(std::pow(2.0, pitchSemitones / 12.0));

    if (progressCallback)
        progressCallback(0.15f);

    // Prepare input pointers
    const float** inputBuffers = new const float* [channels];

    // Study phase. RubberBand's internal buffers have a fixed capacity, so
    // feeding the *entire* selection to study()/process() in a single call
    // (the old approach) only fills that buffer up to its capacity - any
    // input beyond it is silently dropped. That's what was capping every
    // export/preview at a few seconds regardless of how long the actual
    // selection was. Feeding it in chunks sized to what the stretcher
    // itself asks for via getSamplesRequired() is the pattern RubberBand's
    // own examples use, and avoids ever handing it more than it can hold.
    if (progressCallback)
        progressCallback(0.25f);
    if (stageCallback)
        stageCallback("Study");
    {
        int pos = 0;
        while (pos < inputSamples)
        {
            int chunk = juce::jmax(1, juce::jmin((int)stretcher.getSamplesRequired(), inputSamples - pos));
            for (int ch = 0; ch < channels; ++ch)
                inputBuffers[ch] = input.getReadPointer(ch) + pos;

            pos += chunk;
            stretcher.study(inputBuffers, (size_t)chunk, pos >= inputSamples);
        }
    }

    // Process phase - same chunking, and output is drained during the loop
    // (not just after it) so nothing backs up past the stretcher's own
    // buffer capacity here either.
    if (progressCallback)
        progressCallback(0.40f);
    if (stageCallback)
        stageCallback("Process");

    std::vector<std::vector<float>> outputChunks((size_t)channels);
    int totalOutputSamples = 0;

    auto drainAvailable = [&]()
        {
            int avail;
            while ((avail = stretcher.available()) > 0)
            {
                std::vector<std::vector<float>> chunk((size_t)channels, std::vector<float>((size_t)avail));
                std::vector<float*> chunkPtrs((size_t)channels);
                for (int ch = 0; ch < channels; ++ch)
                    chunkPtrs[(size_t)ch] = chunk[(size_t)ch].data();

                size_t got = stretcher.retrieve(chunkPtrs.data(), (size_t)avail);

                for (int ch = 0; ch < channels; ++ch)
                    outputChunks[(size_t)ch].insert(outputChunks[(size_t)ch].end(),
                        chunk[(size_t)ch].begin(), chunk[(size_t)ch].begin() + (long)got);

                totalOutputSamples += (int)got;
            }
        };

    {
        int pos = 0;
        while (pos < inputSamples)
        {
            int chunk = juce::jmax(1, juce::jmin((int)stretcher.getSamplesRequired(), inputSamples - pos));
            for (int ch = 0; ch < channels; ++ch)
                inputBuffers[ch] = input.getReadPointer(ch) + pos;

            pos += chunk;
            stretcher.process(inputBuffers, (size_t)chunk, pos >= inputSamples);

            drainAvailable();

            if (progressCallback)
                progressCallback(0.40f + 0.25f * ((float)pos / (float)inputSamples));
        }
    }

    if (progressCallback)
        progressCallback(0.65f);
    if (stageCallback)
        stageCallback("Retrieve");

    // Final drain for whatever the stretcher was still holding onto
    // internally after the last (final=true) process() call.
    drainAvailable();

    juce::AudioBuffer<float> outputBuffer(channels, totalOutputSamples);
    for (int ch = 0; ch < channels; ++ch)
        outputBuffer.copyFrom(ch, 0, outputChunks[(size_t)ch].data(), totalOutputSamples);

    delete[] inputBuffers;

    if (progressCallback)
        progressCallback(0.85f);

    return outputBuffer;
}
#endif

void AudioStretcherAudioProcessor::applyFades(juce::AudioBuffer<float>& buffer, double bufferSampleRate, float fadeInSeconds, float fadeOutSeconds) const
{
    int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || bufferSampleRate <= 0.0)
        return;

    int fadeInSamples = juce::jlimit(0, numSamples, juce::roundToInt(fadeInSeconds * bufferSampleRate));
    int fadeOutSamples = juce::jlimit(0, numSamples, juce::roundToInt(fadeOutSeconds * bufferSampleRate));

    // On a short selection, a long fade-in and fade-out can overlap. Scale
    // both down proportionally so they just meet in the middle instead of
    // double-attenuating the overlapping samples.
    if (fadeInSamples + fadeOutSamples > numSamples)
    {
        double scale = (double)numSamples / (double)(fadeInSamples + fadeOutSamples);
        fadeInSamples = (int)(fadeInSamples * scale);
        fadeOutSamples = numSamples - fadeInSamples;
    }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < fadeInSamples; ++i)
            data[i] *= (float)i / (float)fadeInSamples;

        for (int i = 0; i < fadeOutSamples; ++i)
            data[numSamples - 1 - i] *= (float)i / (float)fadeOutSamples;
    }
}

bool AudioStretcherAudioProcessor::writeToFlac(const juce::AudioBuffer<float>& buffer,
    double outputSampleRate,
    int bitsPerSample,
    std::unique_ptr<juce::OutputStream> outputStream)
{
    if (outputStream == nullptr)
        return false;

    juce::FlacAudioFormat flacFormat;

    // Highest lossless compression level; bit depth matches the source
    // (capped at 24 by the caller, since 32-bit FLAC is poorly supported)
    juce::StringPairArray metadataValues;
    metadataValues.set("quality", "8"); // FLAC compression level 0-8

    std::unique_ptr<juce::AudioFormatWriter> writer(flacFormat.createWriterFor(
        outputStream.get(),
        outputSampleRate,
        (unsigned int)buffer.getNumChannels(),
        bitsPerSample,
        metadataValues, // Use metadata for quality setting
        0
    ));

    if (writer == nullptr)
    {
        juce::Logger::writeToLog("writeToFlac: createWriterFor() returned null");
        return false;
    }

    // On success createWriterFor() takes ownership of the raw stream
    // pointer (the writer deletes it), so release our unique_ptr's claim
    // on it - otherwise it would be double-deleted.
    outputStream.release();

    bool wroteOk = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer->flush();
    juce::Logger::writeToLog("writeToFlac: writeFromAudioSampleBuffer returned "
        + juce::String(wroteOk ? "true" : "false") + " for " + juce::String(buffer.getNumSamples())
        + " samples");
    return true;
}

bool AudioStretcherAudioProcessor::writeToWav(const juce::AudioBuffer<float>& buffer,
    double outputSampleRate,
    int bitsPerSample,
    std::unique_ptr<juce::OutputStream> outputStream)
{
    if (outputStream == nullptr)
        return false;

    juce::WavAudioFormat wavFormat;

    // JUCE's WAV writer treats a 32-bit request as IEEE float, and
    // 16/24-bit as integer PCM, so this naturally matches the source's
    // original sample format as well as its bit depth.
    std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(
        outputStream.get(),
        outputSampleRate,
        (unsigned int)buffer.getNumChannels(),
        bitsPerSample,
        {},
        0
    ));

    if (writer == nullptr)
    {
        juce::Logger::writeToLog("writeToWav: createWriterFor() returned null");
        return false;
    }

    // On success createWriterFor() takes ownership of the raw stream
    // pointer, so release our unique_ptr's claim on it.
    outputStream.release();

    bool wroteOk = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer->flush();
    juce::Logger::writeToLog("writeToWav: writeFromAudioSampleBuffer returned "
        + juce::String(wroteOk ? "true" : "false") + " for " + juce::String(buffer.getNumSamples())
        + " samples");
    return true;
}

bool AudioStretcherAudioProcessor::writeToAiff(const juce::AudioBuffer<float>& buffer,
    double outputSampleRate,
    int bitsPerSample,
    std::unique_ptr<juce::OutputStream> outputStream)
{
    if (outputStream == nullptr)
        return false;

    juce::AiffAudioFormat aiffFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(aiffFormat.createWriterFor(
        outputStream.get(),
        outputSampleRate,
        (unsigned int)buffer.getNumChannels(),
        bitsPerSample,
        {},
        0
    ));

    if (writer == nullptr)
    {
        juce::Logger::writeToLog("writeToAiff: createWriterFor() returned null");
        return false;
    }

    outputStream.release();
    bool wroteOk = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer->flush();
    juce::Logger::writeToLog("writeToAiff: writeFromAudioSampleBuffer returned "
        + juce::String(wroteOk ? "true" : "false") + " for " + juce::String(buffer.getNumSamples())
        + " samples");
    return true;
}

void AudioStretcherAudioProcessor::setRegion(int startSample, int endSample)
{
    regionStart = juce::jlimit(0, loadedBuffer.getNumSamples(), startSample);
    regionEnd = juce::jlimit(regionStart, loadedBuffer.getNumSamples(), endSample);

    // Ensure we have at least 1 sample
    if (regionEnd <= regionStart)
        regionEnd = juce::jmin(regionStart + 1, loadedBuffer.getNumSamples());

    // Clear preview when region changes
    previewBuffer.setSize(0, 0);
    stopPreview();
}

void AudioStretcherAudioProcessor::renderPreview()
{
    if (loadedBuffer.getNumSamples() == 0 || regionStart >= regionEnd)
        return;

    // Note: Preview is rendered in memory at full quality (32-bit float)

    if (progressCallback)
        progressCallback(0.0f);
    if (stageCallback)
        stageCallback("PRERENDER");

    stopPreview();

    float pitch = *pitchShiftParam + (*fineTuneParam / 100.0f);
    // The Speed knob is a playback-speed multiplier (0.7x = 70% speed =
    // slower, takes longer to play), but the stretch engines below all
    // expect a duration ratio - output length / input length - which is
    // the reciprocal of speed (half speed = twice the duration).
    float stretch = 1.0f / *timeStretchParam;

#ifndef USE_RUBBERBAND
    // RubberBand is the only engine that actually produces sound. Without
    // it there's nothing correct to preview, so do nothing rather than
    // rendering with a different, unrequested engine.
    return;
#else
    // Extract region with bounds checking
    int regionLength = regionEnd - regionStart;
    if (regionLength <= 0 || regionStart < 0 || regionEnd > loadedBuffer.getNumSamples())
        return;

    juce::AudioBuffer<float> regionBuffer(loadedBuffer.getNumChannels(), regionLength);

    for (int ch = 0; ch < loadedBuffer.getNumChannels(); ++ch)
    {
        regionBuffer.copyFrom(ch, 0, loadedBuffer, ch, regionStart, regionLength);
    }

    // Process the region directly - loadedBuffer itself is never touched,
    // so the UI thread can keep safely reading it to draw the waveform
    // while this runs on a background thread.
    juce::AudioBuffer<float> rendered = processWithRubberBand(regionBuffer, pitch, stretch);

    // Fade in/out are specified in seconds of output audio, so apply them
    // here at loadedSampleRate - before the resample below, which would
    // otherwise change how many samples a given fade duration covers.
    applyFades(rendered, loadedSampleRate, *fadeInParam, *fadeOutParam);

    // `rendered` is at loadedSampleRate (the source file's rate), but
    // processBlock() streams preview samples straight out with no further
    // conversion - one previewBuffer sample per output sample. If the
    // host's actual output rate (DAW project rate, standalone device rate,
    // APK device rate...) differs from loadedSampleRate, that mismatch by
    // itself pitches and speeds the preview up or down, even though the
    // stretch/pitch parameters themselves were applied correctly above.
    // Resample here so the preview always plays back at the correct pitch
    // and speed regardless of what rate the host happens to run at - i.e.
    // the way the file would sound in a normal audio player.
    if (hostSampleRate > 0.0 && std::abs(hostSampleRate - loadedSampleRate) > 0.5)
    {
        double ratio = loadedSampleRate / hostSampleRate; // input samples consumed per output sample
        int outLength = juce::jmax(1, juce::roundToInt(rendered.getNumSamples() / ratio));

        juce::AudioBuffer<float> resampled(rendered.getNumChannels(), outLength);

        for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.reset();
            interpolator.process(ratio, rendered.getReadPointer(ch), resampled.getWritePointer(ch), outLength);
        }

        rendered = std::move(resampled);
    }

    // Hand the finished render to the audio thread under the lock. This is
    // the only moment previewBuffer actually changes, and it's now the
    // only moment the audio thread's try-lock can ever contend with.
    {
        const juce::SpinLock::ScopedLockType lock(previewLock);
        previewBuffer = std::move(rendered);
        previewPlayPosition = 0;
    }

    if (progressCallback)
        progressCallback(1.0f);
    if (stageCallback)
        stageCallback("Ready to play");
#endif
}

void AudioStretcherAudioProcessor::startPreview()
{
    const juce::SpinLock::ScopedLockType lock(previewLock);

    if (previewBuffer.getNumSamples() == 0)
        return;

    // If playback had reached the end, start over from the beginning;
    // otherwise resume from wherever pausePreview() left off.
    if (previewPlayPosition >= previewBuffer.getNumSamples())
        previewPlayPosition = 0;

    previewPlaying = true;
}

void AudioStretcherAudioProcessor::pausePreview()
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewPlaying = false;
}

void AudioStretcherAudioProcessor::stopPreview()
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewPlaying = false;
    previewPlayPosition = 0;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioStretcherAudioProcessor();
}