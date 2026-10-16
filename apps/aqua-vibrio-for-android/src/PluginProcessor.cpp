#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace aquavibrio
{

// The hardware table, plus the two controls that are not hardware parameters
// (neither is stored in presets): the engine switch and the master volume.
static juce::AudioProcessorValueTreeState::ParameterLayout createLayoutWithEngineMode()
{
    auto layout = createParameterLayout();
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "EngineMode", 1 }, "Engine",
                                                               juce::StringArray { "Native", "Original" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "MasterVolume", 1 }, "Master Volume",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int)
            {
                return (v > 0.04f ? "+" : "") + juce::String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";
            })));
    return layout;
}

//==============================================================================
// The app's own, always-writable folders. On Android this is the private data
// directory (the only place we can use plain paths); elsewhere it is the normal
// per-user application data folder, and nothing in it is used unless the
// platform's picker could not give us a real path.
static juce::File appFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Aqua Vibrio");
}

static juce::File presetLibraryFolder()  { return appFolder().getChildFile ("Presets"); }

#if JUCE_ANDROID
static juce::File importedWavesFolder()  { return appFolder().getChildFile ("Waves"); }

// Copies every .wav under a picked tree into `dest`, keeping the sub-folder
// layout: setSampledWaveFolder tells the classic waves from the wavetables by
// which folder they sit in, so flattening them would lose the wavetables.
static int copyWavesFromTree (const juce::AndroidDocument& dir, const juce::File& dest, int depth)
{
    int copied = 0;

    for (const auto& child : juce::AndroidDocumentIterator::makeNonRecursive (dir))
    {
        const auto info = child.getInfo();
        const auto name = juce::File::createLegalFileName (info.getName());
        if (name.isEmpty())
            continue;

        if (info.isDirectory())
        {
            if (depth < 4)   // the engine only looks a few levels down anyway
                copied += copyWavesFromTree (child, dest.getChildFile (name), depth + 1);
        }
        else if (name.endsWithIgnoreCase (".wav"))
        {
            if (auto in = child.createInputStream())
            {
                // whole document first: a content:// stream is not seekable
                juce::MemoryBlock bytes;
                in->readIntoMemoryBlock (bytes);

                if (bytes.getSize() > 0 && dest.createDirectory().wasOk()
                    && dest.getChildFile (name).replaceWithData (bytes.getData(), bytes.getSize()))
                    ++copied;
            }
        }
    }

    return copied;
}
#endif

AquaVibrioProcessor::AquaVibrioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",   juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)
          // The hardware has a second pair of outputs, and Second Output Balance
          // decides how much of the patch goes there. The host may leave this
          // bus disabled, which is fine - the engine checks.
          .withOutput ("Output 2", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "AquaVibrio", createLayoutWithEngineMode())
{
    engineMode = apvts.getRawParameterValue ("EngineMode");
    masterVolume = apvts.getRawParameterValue ("MasterVolume");
    engineSeen.store (isOriginalEngine() ? 1 : 0);
    noteLayers.setMirror (&originalEngine);
    startTimer (50);   // builds TI wavetables the Original engine asks for

    // Sampled waveforms, if the user keeps them in Documents/Aqua Vibrio/Waves.
    {
        const auto waves = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("Aqua Vibrio").getChildFile ("Waves");
        if (waves.isDirectory())
            setSampledWaveFolder (waves);
    }

   #if JUCE_ANDROID
    // Android cannot read Documents/ by path; waves picked in the app live in
    // its own folder (see importSampledWavesFromTree).
    if (getNumSampledWaves() == 0 && getNumTIWavetables() == 0 && importedWavesFolder().isDirectory())
        setSampledWaveFolder (importedWavesFolder());
   #endif
}

// Finds the two sets of user-supplied samples at or below the chosen folder
// (or a level or two above it): the classic single-cycle waves (a folder
// holding "Wave 03.wav" .. "Wave 64.wav") and the wavetables (a folder of
// "NN Name.wav" files).
int AquaVibrioProcessor::setSampledWaveFolder (const juce::File& folder)
{
    const auto isClassic = [] (const juce::File& d) { return d.getChildFile ("Wave 03.wav").existsAsFile(); };
    const auto isTables = [] (const juce::File& d)
    {
        int n = 0;
        for (const auto& f : d.findChildFiles (juce::File::findFiles, false, "*.wav"))
        {
            const auto name = f.getFileName();
            if (name.length() > 4 && juce::CharacterFunctions::isDigit (name[0])
                && juce::CharacterFunctions::isDigit (name[1]) && name[2] == ' ')
                ++n;
        }
        return n >= 8;
    };

    juce::File classic, tables;
    const auto search = [&] (const juce::File& root)
    {
        if (! root.isDirectory()) return;
        juce::Array<juce::File> dirs { root };
        dirs.addArray (root.findChildFiles (juce::File::findDirectories, true));
        for (const auto& d : dirs)
        {
            if (d.getRelativePathFrom (root).length() > 0
                && d.getRelativePathFrom (root).retainCharacters ("/\\").length() > 3)
                continue;   // at most a few levels down
            if (classic == juce::File{} && isClassic (d)) classic = d;
            if (tables == juce::File{} && ! isClassic (d) && isTables (d)) tables = d;
        }
    };

    search (folder);
    for (auto up = folder.getParentDirectory(), k = juce::File{}; (classic == juce::File{} || tables == juce::File{})
             && up != folder && up.getFullPathName().length() > 3; up = up.getParentDirectory())
    {
        search (up);
        if (up.getParentDirectory() == up || up == folder.getParentDirectory().getParentDirectory()) break;
    }

    const int n = originalEngine.loadSampledWaves (classic);
    const int t = originalEngine.setTIWavetableFolder (tables);
    if (n > 0 || t > 0)
    {
        sampledWaveFolder = folder;
    }
    return n + t;
}

void AquaVibrioProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    engine.bindTo (apvts);
    engine.prepare (sampleRate, blockSize);
    originalEngine.bindTo (apvts);
    originalEngine.prepare (sampleRate, blockSize);

    masterGain.reset (sampleRate, 0.03);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterVolume->load()));
}

//==============================================================================
// The master volume follows the engine: switching to ORIGINAL sets +12 dB
// (that engine plays at the reference's own, quieter level), switching back
// to NATIVE sets 0 dB. From the timer, so on the message thread whoever
// flipped the switch (the button, the host, automation).
void AquaVibrioProcessor::timerCallback()
{
    originalEngine.serviceWavetables();

    const int now = isOriginalEngine() ? 1 : 0;
    const int was = engineSeen.exchange (now);
    if (was != now)
        setMasterVolumeDb (now == 1 ? kOriginalMasterDb : 0.0f);
}

void AquaVibrioProcessor::setMasterVolumeDb (float db)
{
    if (auto* p = apvts.getParameter ("MasterVolume"))
        p->setValueNotifyingHost (p->convertTo0to1 (db));
}

bool AquaVibrioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto second = layouts.getChannelSet (false, 1);
    return second.isDisabled() || second == juce::AudioChannelSet::stereo();
}

void AquaVibrioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Merges notes played on the on-screen keyboard into the same buffer the
    // host's MIDI arrives in, and - just by scanning that buffer - lets the
    // keyboard light up for notes played from the host or a real controller
    // too, so the widget is a two-way window onto whatever the engine hears.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
            {
                engine.setTempo (*bpm);
                originalEngine.setTempo (*bpm);
            }

    // Switching engines: the one being left lets go of its notes, so they
    // don't hang there waiting for a note-off it will never see.
    const int wanted = isOriginalEngine() ? 1 : 0;
    display::originalMode().store (wanted == 1, std::memory_order_relaxed);
    if (wanted != activeEngine)
    {
        if (activeEngine == 0) engine.allNotesOff();
        if (activeEngine == 1) originalEngine.allNotesOff();
        activeEngine = wanted;
    }

    // Deliberately NOT cleared: the engine reads each input sample for the
    // vocoder and the input follower before writing its own output over it.
    auto* const second = (getBusCount (false) > 1 && getBus (false, 1) != nullptr
                              && getBus (false, 1)->isEnabled()
                              ? getBusBuffer (buffer, false, 1).getArrayOfWritePointers()
                              : nullptr);

    if (activeEngine == 1)
    {
        originalEngine.setSecondOutput (second);
        originalEngine.renderBlock (buffer, midi);
    }
    else
    {
        engine.setSecondOutput (second);
        engine.renderBlock (buffer, midi);
    }

    // Master volume, over every output channel (both output pairs).
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterVolume->load()));
    const int outs = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();
    if (masterGain.isSmoothing())
    {
        const float from = masterGain.getCurrentValue();
        masterGain.skip (n);
        for (int c = 0; c < outs; ++c)
            buffer.applyGainRamp (c, 0, n, from, masterGain.getCurrentValue());
    }
    else if (masterGain.getTargetValue() != 1.0f)
    {
        for (int c = 0; c < outs; ++c)
            buffer.applyGain (c, 0, n, masterGain.getTargetValue());
    }
}

juce::AudioProcessorEditor* AquaVibrioProcessor::createEditor()
{
    return new AquaVibrioEditor (*this);
}

//==============================================================================
void AquaVibrioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The layers travel inside the same state tree as the parameters. With
    // notes selected the knobs show the selection, so the committed view -
    // base patch plus layers, selection included - is what gets stored.
    const juce::ScopedLock sl (noteLayers.getLock());
    auto state = apvts.copyState();
    noteLayers.writeCommittedState (state);
    state.setProperty ("sampledWaveFolder", sampledWaveFolder.getFullPathName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AquaVibrioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            const bool hasMaster = tree.getChildWithProperty ("id", "MasterVolume").isValid();
            {
                const juce::ScopedLock sl (noteLayers.getLock());
                apvts.replaceState (tree);
            }

            // A restored session keeps its own master volume: this is not an
            // engine switch. (Sessions from before the knob existed get the
            // level that goes with their engine.)
            engineSeen.store (isOriginalEngine() ? 1 : 0);
            if (! hasMaster)
                setMasterVolumeDb (isOriginalEngine() ? kOriginalMasterDb : 0.0f);
            noteLayers.restoreFrom (tree.getChildWithName (NoteLayerManager::treeType));

            const juce::String waves = tree.getProperty ("sampledWaveFolder", {}).toString();
            if (waves.isNotEmpty() && juce::File::isAbsolutePath (waves) && juce::File (waves) != sampledWaveFolder)
                setSampledWaveFolder (juce::File (waves));
        }
}

//==============================================================================
int AquaVibrioProcessor::loadBank (const juce::File& file)
{
    presetFiles.clear();
    presetNames.clear();
    bankPatches.clear();
    currentPatch = -1;

    // A hardware bank: every single dump in a .mid / .syx / .bin file becomes
    // one entry of the list, the way the hardware shows a ROM bank.
    if (file.existsAsFile() && (file.hasFileExtension ("mid;midi;syx;bin")))
    {
        juce::MemoryBlock data;
        if (file.loadFileAsData (data))
            bankPatches = parseBankFile (data);

        for (const auto& p : bankPatches)
            presetNames.add (p.getName());

        if (! bankPatches.empty())
            selectPatch (0);

        return (int) bankPatches.size();
    }

    const auto dir = file.isDirectory() ? file : file.getParentDirectory();
    if (! dir.isDirectory())
        return 0;

    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
    files.sort();

    int selectIndex = -1;

    for (auto& f : files)
    {
        const auto json = f.loadFileAsString();
        const auto parsed = juce::JSON::parse (json);

        // Only files that actually look like our presets go in the list -
        // an unrelated .json file living in the same folder is silently
        // skipped rather than corrupting the bank.
        if (! parsed.isObject() || parsed.getProperty ("format", {}).toString() != "AquaVibrioPreset")
            continue;

        if (! file.isDirectory() && f == file)
            selectIndex = (int) presetFiles.size();

        presetNames.add (presetNameFromJson (json, f));
        presetFiles.push_back (f);
    }

    if (! presetFiles.empty())
        selectPatch (selectIndex >= 0 ? selectIndex : 0);

    return (int) presetFiles.size();
}

void AquaVibrioProcessor::selectPatch (int index)
{
    if (! bankPatches.empty())
    {
        if (index < 0 || index >= (int) bankPatches.size())
            return;

        const auto& patch = bankPatches[(size_t) index];
        noteLayers.loadWholePatch ([&] { applyPatch (patch, apvts); }, juce::var());
        currentPatch = index;
        engine.allNotesOff();
        originalEngine.allNotesOff();
        return;
    }

    if (index < 0 || index >= (int) presetFiles.size())
        return;

    // With notes selected the preset becomes their patch; otherwise it is the
    // new patch, and brings its own note layers (or none).
    const auto json = presetFiles[(size_t) index].loadFileAsString();
    const auto parsed = juce::JSON::parse (json);
    bool ok = false;

    noteLayers.loadWholePatch ([&] { ok = applyJsonPreset (json, apvts); },
                               parsed.isObject() ? parsed.getProperty ("noteLayers", {}) : juce::var());

    if (ok)
    {
        currentPatch = index;
        engine.allNotesOff();
        originalEngine.allNotesOff();
    }
}

juce::String AquaVibrioProcessor::currentPatchToJson (const juce::String& name)
{
    const auto base = noteLayers.committedBaseValues();
    return presetToJson (apvts, name, noteLayers.toJson(), &base);
}

void AquaVibrioProcessor::saveCurrentPatch (const juce::File& file, const juce::String& name)
{
    file.replaceWithText (currentPatchToJson (name));
}

juce::File AquaVibrioProcessor::storeInLibrary (const void* data, size_t numBytes, const juce::String& fileName)
{
    if (data == nullptr || numBytes == 0)
        return {};

    const auto dir = presetLibraryFolder();
    if (! dir.createDirectory().wasOk())
        return {};

    auto name = juce::File::createLegalFileName (fileName.trim());
    if (name.isEmpty())
        name = "Preset";

    // loadBank decides what a file is by its extension, and some document
    // providers hand back names without a usable one: fall back on what the
    // bytes are. Anything that is neither a preset nor a bank is not stored.
    const auto known = juce::StringArray { "json", "mid", "midi", "syx", "bin" };
    if (! known.contains (name.fromLastOccurrenceOf (".", false, false).toLowerCase()))
    {
        const auto* b = static_cast<const juce::uint8*> (data);

        if (b[0] == '{')                                                                    name << ".json";
        else if (numBytes >= 4 && b[0] == 'M' && b[1] == 'T' && b[2] == 'h' && b[3] == 'd') name << ".mid";
        else if (b[0] == 0xF0)                                                              name << ".syx";
        else                                                                                return {};
    }

    const auto file = dir.getChildFile (name);
    return file.replaceWithData (data, numBytes) ? file : juce::File{};
}

#if JUCE_ANDROID
void AquaVibrioProcessor::importSampledWavesFromTree (const juce::URL& treeUrl, std::function<void (int)> done)
{
    const auto dest = importedWavesFolder();
    juce::WeakReference<AquaVibrioProcessor> weakThis (this);

    // Off the UI thread: a full set of waves is dozens of files. Nothing in the
    // copy touches the processor; only the hand-over below does, on the message
    // thread, and only if the processor is still alive.
    juce::Thread::launch ([treeUrl, dest, weakThis, done = std::move (done)]
    {
        int copied = 0;

        const auto root = juce::AndroidDocument::fromTree (treeUrl);
        if (root.hasValue())
            copied = copyWavesFromTree (root, dest, 0);

        juce::MessageManager::callAsync ([weakThis, dest, copied, done]
        {
            int found = 0;

            if (copied > 0)
                if (auto* self = weakThis.get())
                    found = self->setSampledWaveFolder (dest);

            if (done != nullptr)
                done (found);
        });
    });
}
#endif

juce::String AquaVibrioProcessor::getPatchName (int index) const
{
    return index >= 0 && index < presetNames.size() ? presetNames[index] : juce::String();
}

juce::String AquaVibrioProcessor::getCurrentPatchName() const
{
    return getPatchName (currentPatch);
}

} // namespace aquavibrio

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aquavibrio::AquaVibrioProcessor();
}
