#include "PatchKit.h"

namespace aquanode::plugins::kit
{

juce::RangedAudioParameter* findParam (juce::AudioProcessor& original, const juce::String& id)
{
    for (auto* p : original.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() == id)
                return ranged;
    return nullptr;
}

float param (juce::AudioProcessor& original, const juce::String& id, float fallback)
{
    auto* p = findParam (original, id);
    if (p == nullptr)
        return fallback;

    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
        return (float) c->getIndex();
    if (auto* b = dynamic_cast<juce::AudioParameterBool*> (p))
        return b->get() ? 1.0f : 0.0f;
    if (auto* i = dynamic_cast<juce::AudioParameterInt*> (p))
        return (float) i->get();
    return p->convertFrom0to1 (p->getValue());
}

juce::Colour accentOf (const juce::String& pluginId)
{
    return baseSpec (pluginId).accent;
}

juce::String moduleName (const juce::String& pluginId, const juce::String& kind)
{
    return baseSpec (pluginId).displayName + " - " + kind;
}

ParamSpec makeOnOff (juce::String id, juce::String label, bool defaultOn, int row)
{
    return makeCombo (std::move (id), std::move (label), { "Off", "On" }, defaultOn ? 1 : 0, row, 1);
}

//==============================================================================
int Builder::add (const juce::String& typeId, int dx, int dy)
{
    return patch.addModule (typeId, origin + juce::Point<int> (dx, dy));
}

SynthModule* Builder::dsp (int module) const
{
    auto* inst = patch.getInstance (module);
    return inst != nullptr ? inst->dsp.get() : nullptr;
}

void Builder::set (int module, const juce::String& paramId, float value)
{
    if (auto* d = dsp (module))
    {
        if (const auto* spec = d->getDescriptor().findParam (paramId))
            value = juce::jlimit (spec->minValue, spec->maxValue, value);
        d->setParameter (paramId, value);
    }
}

int Builder::copy (int module, juce::AudioProcessor& original,
                   const std::function<juce::String (const juce::String&)>& map)
{
    auto* d = dsp (module);
    if (d == nullptr)
        return 0;

    int copied = 0;
    for (const auto& spec : d->getDescriptor().params)
    {
        const auto pluginId = map != nullptr ? map (spec.id) : spec.id;
        if (pluginId.isEmpty() || findParam (original, pluginId) == nullptr)
            continue;
        set (module, spec.id, param (original, pluginId));
        ++copied;
    }
    return copied;
}

bool Builder::cable (int from, const juce::String& outSocket, int to, const juce::String& inSocket)
{
    return patch.addCable (from, outSocket, to, inSocket);
}

bool Builder::mod (int from, const juce::String& outSocket, int to, const juce::String& paramId, float depth)
{
    return patch.addParamCable (from, outSocket, to, paramId, juce::jlimit (-1.0f, 1.0f, depth));
}

void Builder::setCustomState (int module, const juce::String& state)
{
    if (auto* d = dsp (module))
        d->loadCustomState (state);
}

//==============================================================================
InstanceModule::InstanceModule (const juce::String& pluginId, bool takesMidi)
    : plugin (createOriginal (pluginId, 48000.0)), midi (takesMidi)
{
    midiQueue.ensureSize (1024);
}

InstanceModule::~InstanceModule()
{
    if (plugin != nullptr)
        plugin->setPlayHead (nullptr);
}

void InstanceModule::rawMidiMessage (const juce::MidiMessage& m)
{
    if (midi)
        midiQueue.addEvent (m, pos);
}

void InstanceModule::midiAllNotesOff()
{
    if (midi)
        midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), pos);
}

void InstanceModule::relink()
{
    links.clear();
    if (plugin == nullptr)
        return;
    const auto& d = getDescriptor();
    for (int i = 0; i < (int) d.params.size(); ++i)
    {
        const auto& spec = d.params[(size_t) i];
        if (spec.type == ParamType::Button)
            continue;
        const auto id = pluginParamFor (spec.id);
        if (id.isNotEmpty())
            if (auto* p = findParam (*plugin, id))
                links.push_back ({ p, i, -1.0f });
    }
}

void InstanceModule::pushKnobs (bool all)
{
    for (auto& l : links)
    {
        const float v = l.param->convertTo0to1 (all ? getParameterBase (l.knob) : param (l.knob));
        if (all || v != l.last)
        {
            l.param->setValueNotifyingHost (v);
            l.last = v;
        }
    }
}

void InstanceModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    if (plugin == nullptr)
        return;

    const auto& d = getDescriptor();
    inAudio = d.inputIndexOf ("audioIn");
    outAudio = d.outputIndexOf ("audioOut");
    relink();

    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        plugin->releaseResources();
        plugin->setRateAndBufferSizeDetails (sr, kBlock);
        plugin->prepareToPlay (sr, kBlock);
    }
    plugin->setPlayHead (&playHead);

    io.setSize (juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), kBlock);
    io.clear();
    for (auto& f : inFifo) f.fill (0.0f);
    for (auto& f : outFifo) f.fill (0.0f);
    midiQueue.clear();
    pos = 0;
    sampleCounter = 0;
}

void InstanceModule::reset()
{
    midiQueue.clear();
    if (midi)
        midiQueue.addEvent (juce::MidiMessage::allNotesOff (1), 0);
}

void InstanceModule::processSample (const StereoFrame* in, StereoFrame* out)
{
    if (inAudio >= 0)
    {
        inFifo[0][(size_t) pos] = in[inAudio][0];
        inFifo[1][(size_t) pos] = in[inAudio][1];
    }
    if (outAudio >= 0)
        out[outAudio] = { outFifo[0][(size_t) pos], outFifo[1][(size_t) pos] };
    extraOutputs (out, pos);

    if (++pos >= kBlock)
    {
        pos = 0;
        runBlock();
    }
}

void InstanceModule::render (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiIn)
{
    plugin->processBlock (buffer, midiIn);
}

void InstanceModule::runBlock()
{
    if (plugin == nullptr)
        return;

    // everything the plugin is handed happens under its callback lock, as in
    // a host: the message thread swapping its sound or state never meets a
    // block (or a parameter listener) half way
    const juce::ScopedTryLock sl (plugin->getCallbackLock());
    if (! sl.isLocked() || plugin->isSuspended())
    {
        midiQueue.clear();
        for (auto& f : outFifo) f.fill (0.0f);
        sampleCounter += kBlock;
        return;
    }

    pushKnobs (false);
    beforeBlock();

    // the patch's clock as the plugin's play head: the host's, else a
    // free-running one at the patch tempo
    {
        juce::AudioPlayHead::PositionInfo info;
        bool fromHost = false;
        if (hostPlayHead != nullptr)
            if (auto host = hostPlayHead->getPosition())
            {
                info = *host;
                fromHost = true;
            }
        if (! fromHost)
        {
            const double secs = (double) sampleCounter / sampleRate;
            info.setBpm (tempoBpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            info.setTimeInSamples (sampleCounter);
            info.setTimeInSeconds (secs);
            info.setPpqPosition (secs * tempoBpm / 60.0);
            info.setIsPlaying (hostTransport.isPlaying);
        }
        playHead.info = info;
    }

    io.clear();
    if (plugin->getTotalNumInputChannels() > 0)
        for (int ch = 0; ch < juce::jmin (2, io.getNumChannels()); ++ch)
            io.copyFrom (ch, 0, inFifo[(size_t) ch].data(), kBlock);

    render (io, midiQueue);
    midiQueue.clear();

    const bool mono = plugin->getTotalNumOutputChannels() == 1;
    for (int i = 0; i < kBlock; ++i)
    {
        const float l = io.getSample (0, i), r = io.getSample (mono ? 0 : 1, i);
        outFifo[0][(size_t) i] = std::isfinite (l) ? l : 0.0f;
        outFifo[1][(size_t) i] = std::isfinite (r) ? r : 0.0f;
    }
    sampleCounter += kBlock;
}

juce::String InstanceModule::saveCustomState() const
{
    if (plugin == nullptr)
        return {};
    juce::MemoryBlock mb;
    plugin->getStateInformation (mb);
    return mb.toBase64Encoding();
}

void InstanceModule::loadCustomState (const juce::String& s)
{
    juce::MemoryBlock mb;
    if (plugin == nullptr || ! mb.fromBase64Encoding (s) || mb.getSize() == 0)
        return;
    {
        const juce::ScopedLock sl (plugin->getCallbackLock());
        plugin->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    for (auto& l : links) l.last = -1.0f;   // the knobs have the last word
    stateRestored();
}

void InstanceModule::copyStateFrom (juce::AudioProcessor& source)
{
    juce::MemoryBlock mb;
    source.getStateInformation (mb);
    loadCustomState (mb.toBase64Encoding());
}

} // namespace aquanode::plugins::kit
