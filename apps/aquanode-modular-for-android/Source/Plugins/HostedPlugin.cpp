#include "HostedPlugin.h"
#include "PluginCatalog.h"

namespace aquanode::plugins
{

//==============================================================================
ModuleDescriptor makeHostedDescriptor (const HostedSpec& spec)
{
    ModuleDescriptor d;
    d.typeId = spec.typeId;
    d.displayName = spec.displayName;
    d.description = spec.description;
    d.section = spec.section;
    d.accent = spec.accent;
    d.listedInSidebar = false;   // offered by the Plugins section instead

    if (spec.audioIn)  d.sockets.push_back (audioIn ("audioIn", "Audio In"));
    if (spec.sideIn)   d.sockets.push_back (audioIn ("sideIn", spec.sideInLabel));
    if (spec.midiIn)   d.sockets.push_back (midiIn ("midiIn", "Midi In"));
    d.sockets.push_back (audioOut ("audioOut", "Audio Out"));
    if (spec.midiOut)  d.sockets.push_back (midiOut ("midiOut", "Midi Out"));
    return d;
}

//==============================================================================
// The card's content: the plugin's own editor at its own size. It keeps the
// processor alive for as long as the editor exists, so a module deleted while
// its card is still on screen can never leave the editor dangling.
class HostedEditorHolder : public juce::Component,
                           public ExtraContentParamListener,
                           private juce::ComponentListener
{
public:
    HostedEditorHolder (std::shared_ptr<juce::AudioProcessor> p, HostedPluginModule& owner)
        : processor (std::move (p)), module (&owner)
    {
        setOpaque (false);
       #if JUCE_MAJOR_VERSION >= 9
        editor.reset (processor->createEditorAndMakeActive());
       #else
        editor.reset (processor->createEditorIfNeeded());   // the same, before JUCE 9
       #endif
        if (editor != nullptr)
        {
            addAndMakeVisible (editor.get());
            editor->setTopLeftPosition (0, 0);
            editor->addComponentListener (this);
            setSize (editor->getWidth(), editor->getHeight());
            owner.noteEditorSize (editor->getWidth(), editor->getHeight());
        }
        else
        {
            setSize (300, 60);
        }
    }

    ~HostedEditorHolder() override
    {
        if (editor != nullptr)
        {
            editor->removeComponentListener (this);
            // the host's job (JUCE's editor does not do it itself): without it
            // the plugin still counts this editor as its active one, and the
            // card that replaces this one (every canvas rebuild) gets none
            if (processor != nullptr)
                processor->editorBeingDeleted (editor.get());
        }
        editor = nullptr;     // the editor must go before the processor
        processor = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        if (editor == nullptr)
        {
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.drawText ("This plugin has no window", getLocalBounds(), juce::Justification::centred);
        }
    }

    void resized() override
    {
        if (editor != nullptr && editor->getPosition() != juce::Point<int>())
            editor->setTopLeftPosition (0, 0);
    }

private:
    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (! wasResized || &c != editor.get())
            return;

        if (auto* m = dynamic_cast<HostedPluginModule*> (module.get()))
            m->noteEditorSize (c.getWidth(), c.getHeight());

        // the card re-measures itself - after this callback, never inside it
        juce::Component::SafePointer<HostedEditorHolder> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr && safe->paramsChangedByContent)
                safe->paramsChangedByContent();
        });
    }

    std::shared_ptr<juce::AudioProcessor> processor;
    juce::WeakReference<SynthModule> module;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

//==============================================================================
HostedPluginModule::HostedPluginModule (const HostedSpec& s)
    : spec (s),
      editorW (s.defaultWidth),
      editorH (s.defaultHeight)
{
    plugin.reset (spec.create());
    jassert (plugin != nullptr);

    if (spec.sideIn)
        if (auto* bus = plugin->getBus (true, 1))
        {
            bus->enable (true);
            sideChannels = plugin->getChannelCountOfBus (true, 1);
            bus->enable (false);
        }

    plugin->setPlayHead (&playHead);
    pendingOut.reserve (256);
}

HostedPluginModule::~HostedPluginModule()
{
    if (plugin != nullptr)
    {
        plugin->setPlayHead (nullptr);
        if (prepared)
            plugin->releaseResources();
    }
}

void HostedPluginModule::prepare (double newSampleRate)
{
    SynthModule::prepare (newSampleRate);

    const auto& d = getDescriptor();
    inAudioIdx = d.inputIndexOf ("audioIn");
    inSideIdx = d.inputIndexOf ("sideIn");
    outAudioIdx = d.outputIndexOf ("audioOut");
    numOutputs = juce::jmax (1, d.numOutputs());

    mainIns = plugin->getBus (true, 0) != nullptr ? plugin->getChannelCountOfBus (true, 0) : 0;
    sideIns = spec.sideIn && plugin->getBus (true, 1) != nullptr ? plugin->getChannelCountOfBus (true, 1) : 0;
    outs = plugin->getTotalNumOutputChannels();

    const int channels = juce::jmax (1, mainIns + sideChannels, outs);
    io.setSize (channels, kBlockSize, false, true, false);
    io.clear();

    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        if (prepared)
            plugin->releaseResources();
        plugin->setRateAndBufferSizeDetails (newSampleRate, kBlockSize);
        plugin->prepareToPlay (newSampleRate, kBlockSize);
    }

    midiInBuffer.ensureSize (2048);
    midiOutScratch.ensureSize (2048);
    for (auto& ch : outFifo)
        ch.fill (0.0f);
    pos = 0;
    prepared = true;
}

void HostedPluginModule::reset()
{
    // the engine wipes all state after a NaN or a deleted module: the plugin
    // is told by an all-notes-off, its own state stays as the player set it
    for (auto& ch : outFifo)
        ch.fill (0.0f);
    io.clear();
    midiInBuffer.clear();
    for (int ch = 1; ch <= 16; ++ch)
        midiInBuffer.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    pendingOut.clear();
}

void HostedPluginModule::blockStart()
{
    samplesThisHostBlock = 0;

    if (spec.sideIn && prepared && inSideIdx >= 0)
    {
        const bool want = isInputConnected (inSideIdx);
        if (want != (sideIns.load() > 0) && ! sideRelayoutPending.exchange (true))
        {
            juce::WeakReference<SynthModule> weak (this);
            juce::MessageManager::callAsync ([weak, want]
            {
                if (auto* self = dynamic_cast<HostedPluginModule*> (weak.get()))
                    self->setSideChainEnabled (want);
            });
        }
    }
}

void HostedPluginModule::setSideChainEnabled (bool enabled)
{
    // message thread: the audio thread only try-locks, and plays silence
    // for the few samples this takes
    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        if (auto* bus = plugin->getBus (true, 1))
        {
            plugin->releaseResources();
            bus->enable (enabled);
            sideIns = enabled ? juce::jmin (sideChannels, plugin->getChannelCountOfBus (true, 1)) : 0;
            plugin->prepareToPlay (sampleRate, kBlockSize);
        }
    }
    sideRelayoutPending = false;
}

void HostedPluginModule::rawMidiMessage (const juce::MidiMessage& message)
{
    midiInBuffer.addEvent (message, pos);
}

void HostedPluginModule::midiAllNotesOff()
{
    for (int ch = 1; ch <= 16; ++ch)
        midiInBuffer.addEvent (juce::MidiMessage::allNotesOff (ch), pos);
}

void HostedPluginModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    if (prepared)
    {
        if (mainIns > 0)
        {
            const int i = inAudioIdx;
            const StereoFrame f = i >= 0 ? inputs[i] : StereoFrame { 0.0f, 0.0f };
            if (mainIns == 1)
                io.setSample (0, pos, 0.5f * (f[0] + f[1]));
            else
            {
                io.setSample (0, pos, f[0]);
                io.setSample (1, pos, f[1]);
            }
        }

        if (const int sideIns = this->sideIns.load (std::memory_order_relaxed); sideIns > 0)
        {
            const int i = inSideIdx;
            const StereoFrame f = i >= 0 ? inputs[i] : StereoFrame { 0.0f, 0.0f };
            io.setSample (mainIns, pos, f[0]);
            if (sideIns > 1)
                io.setSample (mainIns + 1, pos, f[1]);
        }
    }

    for (int k = 0; k < numOutputs; ++k)
        outputs[k] = { 0.0f, 0.0f };
    if (outAudioIdx >= 0)
        outputs[outAudioIdx] = { outFifo[0][(size_t) pos], outFifo[1][(size_t) pos] };

    ++samplesThisHostBlock;
    ++sampleCounter;

    if (++pos >= kBlockSize)
    {
        pos = 0;
        runBlock();
    }
}

void HostedPluginModule::updatePlayHead()
{
    juce::AudioPlayHead::PositionInfo info;

    // where this little block started, relative to the host's block
    const int offset = samplesThisHostBlock - kBlockSize;
    bool fromHost = false;

    if (hostPlayHead != nullptr)
        if (auto host = hostPlayHead->getPosition())
        {
            info = *host;
            fromHost = true;
            if (auto t = info.getTimeInSamples())
                info.setTimeInSamples (*t + offset);
            if (auto t = info.getTimeInSeconds())
                info.setTimeInSeconds (*t + offset / sampleRate);
            if (auto ppq = info.getPpqPosition())
                if (auto bpm = info.getBpm())
                    info.setPpqPosition (*ppq + offset / sampleRate * (*bpm / 60.0));
        }

    if (! fromHost)
    {
        // standalone / Android: the patch's own tempo, a free-running clock
        const double secs = (double) (sampleCounter - kBlockSize) / sampleRate;
        info.setBpm (tempoBpm);
        info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        info.setTimeInSamples (sampleCounter - kBlockSize);
        info.setTimeInSeconds (secs);
        info.setPpqPosition (secs * tempoBpm / 60.0);
        info.setIsPlaying (hostTransport.isPlaying);
    }

    playHead.info = info;
}

void HostedPluginModule::runBlock()
{
    if (! prepared)
        return;

    updatePlayHead();

    // anything beyond the plugin's own channels must not leak into its output
    const int usedIns = mainIns + sideIns.load (std::memory_order_relaxed);
    for (int ch = usedIns; ch < io.getNumChannels(); ++ch)
        io.clear (ch, 0, kBlockSize);

    {
        const juce::ScopedTryLock sl (plugin->getCallbackLock());
        if (sl.isLocked() && ! plugin->isSuspended())
            plugin->processBlock (io, midiInBuffer);
        else
            io.clear();
    }

    // MIDI the plugin produced (NoteAdder): emitted one block later, at the
    // sample it belongs to
    if (spec.midiOut)
    {
        for (const auto meta : midiInBuffer)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff())
                continue;
            MidiDriverEvent ev;
            ev.note = m.getNoteNumber();
            ev.isOn = m.isNoteOn();
            ev.velocity = m.getFloatVelocity();
            if (pendingOut.size() < pendingOut.capacity())
                pendingOut.push_back ({ sampleCounter + meta.samplePosition, ev });
        }
    }
    midiInBuffer.clear();

    // the processed block becomes the next block's output
    const int numOut = juce::jmin (outs, io.getNumChannels());
    for (int i = 0; i < kBlockSize; ++i)
    {
        float l = numOut > 0 ? io.getSample (0, i) : 0.0f;
        float r = numOut > 1 ? io.getSample (1, i) : l;
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        outFifo[0][(size_t) i] = l;
        outFifo[1][(size_t) i] = r;
    }
    io.clear();
}

int HostedPluginModule::midiDriverAdvanceEvents (double, MidiDriverEvent* events, int maxEvents)
{
    // called once per sample, before the patch runs that sample
    const auto now = driverCounter++;
    int n = 0;
    for (size_t i = 0; i < pendingOut.size();)
    {
        if (pendingOut[i].due <= now && n < maxEvents)
        {
            events[n++] = pendingOut[i].ev;
            pendingOut.erase (pendingOut.begin() + (long) i);
            continue;
        }
        ++i;
    }
    return n;
}

//==============================================================================
std::unique_ptr<juce::Component> HostedPluginModule::createExtraContentComponent()
{
    if (! plugin->hasEditor())
        return nullptr;
    return std::make_unique<HostedEditorHolder> (plugin, *this);
}

void HostedPluginModule::noteEditorSize (int w, int h)
{
    editorW = juce::jmax (100, w);
    editorH = juce::jmax (40, h);
}

int HostedPluginModule::preferredModuleWidth() const
{
    return editorW.load() + 20;     // the card's 10 px margins either side
}

int HostedPluginModule::extraContentHeight() const
{
    return plugin->hasEditor() ? editorH.load() : 0;
}

juce::String HostedPluginModule::saveCustomState() const
{
    juce::MemoryBlock mb;
    plugin->getStateInformation (mb);
    return "w=" + juce::String (editorW.load()) + ";h=" + juce::String (editorH.load()) + ";" + mb.toBase64Encoding();
}

void HostedPluginModule::loadCustomState (const juce::String& state)
{
    auto rest = state;
    while (rest.startsWith ("w=") || rest.startsWith ("h="))
    {
        const auto item = rest.upToFirstOccurrenceOf (";", false, false);
        (item.startsWith ("w=") ? editorW : editorH) = juce::jmax (40, item.substring (2).getIntValue());
        rest = rest.fromFirstOccurrenceOf (";", false, false);
    }

    juce::MemoryBlock mb;
    if (rest.isNotEmpty() && mb.fromBase64Encoding (rest) && mb.getSize() > 0)
        plugin->setStateInformation (mb.getData(), (int) mb.getSize());
}

//==============================================================================
bool HostedPluginModule::hasAudioToSave() const
{
    double rate = 0.0;
    return getAudioToSave (rate) != nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> HostedPluginModule::getAudioToSave (double& rate) const
{
    if (auto* access = audioAccessFor (spec.pluginId); access != nullptr && access->get)
        return access->get (*plugin, rate);
    return nullptr;
}

void HostedPluginModule::restoreSavedAudio (std::shared_ptr<juce::AudioBuffer<float>> audio, double rate)
{
    if (auto* access = audioAccessFor (spec.pluginId); access != nullptr && access->set && audio != nullptr)
        access->set (*plugin, std::move (audio), rate);
}

} // namespace aquanode::plugins
