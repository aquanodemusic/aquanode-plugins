#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Modules/ModuleRegistry.h"
#include "Modules/MidiAddModule.h"
#include "Modules/DiscardMidiModule.h"
#include "Modules/DawModModule.h"

#include <algorithm>

using namespace aquanode;

//==============================================================================
AquanodeModularAudioProcessor::AquanodeModularAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    aquanode::forceLinkAllModules();   // guarantees every module is linked + registered

    // Fixed, always-present automation targets. They exist whether or not any
    // DAW Mod module is patched, so a host's automation lanes never shift
    // around as the patch changes.
    for (int i = 0; i < (int) dawModParams.size(); ++i)
    {
        auto* p = new juce::AudioParameterFloat (
            juce::ParameterID { "dawmod" + juce::String (i + 1), 1 },
            "DAW Mod " + juce::String (i + 1),
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f);
        dawModParams[(size_t) i] = p;
        addParameter (p);
    }

    rebuildGraph();   // publish an (empty) graph so processBlock is always valid
}

AquanodeModularAudioProcessor::~AquanodeModularAudioProcessor() = default;

//==============================================================================
void AquanodeModularAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    isPrepared = true;
    inputScratch.setSize (2, juce::jmax (1, samplesPerBlock));

    for (auto& inst : instances)
        inst->dsp->prepare (sampleRate);

    for (auto& vs : voiceStates)
        vs = VoiceState();
    refreshActiveVoices();

    purgeRetired();
}

void AquanodeModularAudioProcessor::releaseResources()
{
}

bool AquanodeModularAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono()
        || in == juce::AudioChannelSet::disabled();
}

//==============================================================================
void AquanodeModularAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    std::shared_ptr<const ProcessGraph> graph;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        graph = renderGraph;
    }

    if (graph != nullptr)
    {
        audioAdoptedSerial.store (graph->serial, std::memory_order_release);

        // a fresh graph may have dropped param cables -> wipe stale offsets
        if (graph->serial != audioLastGraphSerial)
        {
            audioLastGraphSerial = graph->serial;
            for (const auto& node : graph->nodes)
                node->dsp->clearParamMods();
        }
    }

    const int numSamples = buffer.getNumSamples();

    // copy the host input before clearing (in and out may share the buffer)
    if (inputScratch.getNumSamples() < numSamples)
        inputScratch.setSize (2, numSamples, false, false, true);

    const int numHostIns = getTotalNumInputChannels();
    for (int ch = 0; ch < 2; ++ch)
    {
        if (ch < numHostIns)
            inputScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);
        else if (numHostIns == 1)
            inputScratch.copyFrom (ch, 0, buffer, 0, 0, numSamples);
        else
            inputScratch.clear (ch, 0, numSamples);
    }

    buffer.clear();

    if (graph == nullptr || graph->nodes.empty())
        return;

    // host tempo (120 BPM fallback)
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm())
                bpm = *hostBpm;

    const auto& nodes = graph->nodes;

    if (resetAllPending.exchange (false, std::memory_order_acquire))
    {
        for (auto& n : nodes)
        {
            n->dsp->reset();
            std::fill (n->outBuf.begin(), n->outBuf.end(), StereoFrame { 0.0f, 0.0f });
            std::fill (n->prevOutBuf.begin(), n->prevOutBuf.end(), StereoFrame { 0.0f, 0.0f });
            std::fill (n->outV.begin(), n->outV.end(), StereoFrame { 0.0f, 0.0f });
            std::fill (n->prevOutV.begin(), n->prevOutV.end(), StereoFrame { 0.0f, 0.0f });
        }
        for (auto& vs : voiceStates)
            vs = VoiceState();
        refreshActiveVoices();
    }

    // one read of the host's automation values per block; every module gets a
    // pointer to this snapshot so the values are stable across the block
    for (size_t i = 0; i < dawModParams.size(); ++i)
        dawModSnapshot[i] = dawModParams[i] != nullptr ? dawModParams[i]->get() : 0.0f;

    for (auto& n : nodes)
    {
        n->dsp->setTempo (bpm);
        n->dsp->setHostModValues (dawModSnapshot.data());
        n->dsp->blockStart();
    }

    auto midiIt = midiMessages.begin();
    const auto midiEnd = midiMessages.end();

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto* hostInL = inputScratch.getReadPointer (0);
    const auto* hostInR = inputScratch.getReadPointer (1);

    const double invSr = 1.0 / currentSampleRate;

    // reused per-contribution accumulator (all edge type conversions)
    auto accumulate = [] (StereoFrame& dst, const StereoFrame& frame, EdgeConversion conv)
    {
        switch (conv)
        {
            case EdgeConversion::AudioToMod:
                dst[0] += (frame[0] + frame[1]) * 0.5f;   // stereo -> mono
                break;
            case EdgeConversion::ModToAudio:
                dst[0] += frame[0];                        // mono -> both channels
                dst[1] += frame[0];
                break;
            case EdgeConversion::ModToMod:
                dst[0] += frame[0];
                break;
            case EdgeConversion::Direct:
                dst[0] += frame[0];
                dst[1] += frame[1];
                break;
        }
    };

    for (int s = 0; s < numSamples; ++s)
    {
        // sample-accurate MIDI -> global voice events (omni)
        while (midiIt != midiEnd && (*midiIt).samplePosition <= s)
        {
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn())
            {
                for (const auto& md : graph->midiDrivers)
                    md.dsp->midiDriverHeldNoteOn (msg.getNoteNumber());
                engineNoteOn (*graph, msg.getNoteNumber(), msg.getFloatVelocity());
            }
            else if (msg.isNoteOff())
            {
                for (const auto& md : graph->midiDrivers)
                    md.dsp->midiDriverHeldNoteOff (msg.getNoteNumber());
                engineNoteOff (nodes, msg.getNoteNumber());
            }
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            {
                for (const auto& md : graph->midiDrivers)
                    md.dsp->midiDriverAllHeldOff();
                engineAllNotesOff (nodes);
            }
            ++midiIt;
        }

        // note drivers run on their own clock, emitting their own notes
        for (const auto& md : graph->midiDrivers)
        {
            int onNote = -1, offNote = -1;
            md.dsp->midiDriverAdvance (currentSampleRate, onNote, offNote);

            if (offNote >= 0)
                midiDriverNoteOff (nodes, offNote, md.instanceId);
            if (onNote >= 0)
            {
                startVoice (nodes, onNote, onNote, md.instanceId, 0.85f, true, true,
                            md.instanceId);
                // a Midi Add with this driver in its Always Midi In turns the
                // driver's single note into a chord
                spawnMidiAdditions (*graph, onNote, 0.85f, md.instanceId, true);
            }
        }

        // released voices die once their patch-wide tail has elapsed
        for (int v = 0; v < kMaxVoices; ++v)
        {
            if (voiceStates[v].active && ! voiceStates[v].held)
            {
                voiceStates[v].countdown -= invSr;
                if (voiceStates[v].countdown <= 0.0)
                    deactivateVoice (nodes, v);
            }
        }

        float sumL = 0.0f, sumR = 0.0f;

        for (size_t ni = 0; ni < nodes.size(); ++ni)
        {
            auto& node = *nodes[ni];

            // ---- knob modulation: sum every param cable into its offset
            // (per-voice sources are voice-summed, like any global consumer)
            const auto& pedges = graph->incomingParams[ni];
            if (! pedges.empty())
            {
                for (const auto& pe : pedges)
                    node.dsp->setParamMod (pe.paramIdx, 0.0f);

                for (const auto& pe : pedges)
                {
                    auto& psrc = *nodes[(size_t) pe.srcNode];
                    float value = 0.0f;

                    if (psrc.perVoice)
                    {
                        const auto& lanes = pe.feedback ? psrc.prevOutV : psrc.outV;
                        for (int k = 0; k < numActiveVoices; ++k)
                        {
                            const int v = activeVoiceList[k];
                            const auto& f = lanes[(size_t) (v * psrc.numOut + pe.srcSock)];
                            value += 0.5f * (f[0] + f[1]);
                        }
                    }
                    else
                    {
                        const auto& f = (pe.feedback ? psrc.prevOutBuf : psrc.outBuf)[(size_t) pe.srcSock];
                        value = 0.5f * (f[0] + f[1]);
                    }

                    node.dsp->addParamMod (pe.paramIdx, value * pe.scale);
                }
            }

            if (node.perVoice)
            {
                // ---- per-voice lane: run this module once for every active
                // voice, with that voice's own inputs
                for (int k = 0; k < numActiveVoices; ++k)
                {
                    const int v = activeVoiceList[k];

                    for (int i = 0; i < node.numIn; ++i)
                        node.inBuf[(size_t) i] = { 0.0f, 0.0f };

                    for (const auto& e : graph->incoming[ni])
                    {
                        auto& src = *nodes[(size_t) e.srcNode];
                        const StereoFrame* frame;
                        if (src.perVoice)
                            frame = &(e.feedback ? src.prevOutV : src.outV)
                                        [(size_t) (v * src.numOut + e.srcSock)];
                        else   // global source broadcasts to every voice
                            frame = &(e.feedback ? src.prevOutBuf : src.outBuf)[(size_t) e.srcSock];

                        accumulate (node.inBuf[(size_t) e.dstSock], *frame, e.conversion);
                    }

                    node.dsp->processVoiceSample (v, node.inBuf.data(),
                                                  node.outV.data() + (size_t) (v * node.numOut));
                }
            }
            else
            {
                // ---- global lane: per-voice sources are summed here - this
                // is THE polyphonic voice sum (at Audio Out, effects, etc.)
                if (node.dsp->wantsHostInput())
                    node.dsp->setHostInput (hostInL[s], hostInR[s]);

                for (int i = 0; i < node.numIn; ++i)
                    node.inBuf[(size_t) i] = { 0.0f, 0.0f };

                for (const auto& e : graph->incoming[ni])
                {
                    auto& src = *nodes[(size_t) e.srcNode];
                    auto& dst = node.inBuf[(size_t) e.dstSock];

                    if (src.perVoice)
                    {
                        const auto& lanes = e.feedback ? src.prevOutV : src.outV;
                        for (int k = 0; k < numActiveVoices; ++k)
                        {
                            const int v = activeVoiceList[k];
                            accumulate (dst, lanes[(size_t) (v * src.numOut + e.srcSock)], e.conversion);
                        }
                    }
                    else
                    {
                        accumulate (dst, (e.feedback ? src.prevOutBuf : src.outBuf)[(size_t) e.srcSock],
                                    e.conversion);
                    }
                }

                node.dsp->processSample (node.inBuf.data(), node.outBuf.data());

                if (node.dsp->providesHostOutput())
                {
                    float l = 0.0f, r = 0.0f;
                    node.dsp->getHostOutput (l, r);
                    sumL += l;
                    sumR += r;
                }
            }
        }

        // cache this sample's outputs for next sample's feedback edges
        for (auto& n : nodes)
        {
            if (n->perVoice)
            {
                for (int k = 0; k < numActiveVoices; ++k)
                {
                    const int v = activeVoiceList[k];
                    for (int i = 0; i < n->numOut; ++i)
                        n->prevOutV[(size_t) (v * n->numOut + i)] = n->outV[(size_t) (v * n->numOut + i)];
                }
            }
            else
            {
                n->prevOutBuf = n->outBuf;
            }
        }

        outL[s] = sumL;
        if (outR != nullptr)
            outR[s] = sumR;
    }
}

//==============================================================================
// global voice manager (audio thread)
//==============================================================================
void AquanodeModularAudioProcessor::refreshActiveVoices()
{
    numActiveVoices = 0;
    for (int v = 0; v < kMaxVoices; ++v)
        if (voiceStates[v].active)
            activeVoiceList[numActiveVoices++] = v;
}

double AquanodeModularAudioProcessor::computeVoiceTail (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes) const
{
    double tail = 0.02;   // minimum: covers unpatched-env gate ramps
    for (const auto& n : nodes)
        if (n->perVoice)
            tail = juce::jmax (tail, n->dsp->voiceTailSeconds());
    return tail + 0.05;   // small safety margin for filter ringing etc.
}

void AquanodeModularAudioProcessor::deactivateVoice (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int v)
{
    voiceStates[v] = VoiceState();

    for (const auto& n : nodes)
    {
        if (! n->perVoice)
            continue;

        n->dsp->voiceReset (v);
        for (int i = 0; i < n->numOut; ++i)
        {
            n->outV[(size_t) (v * n->numOut + i)] = { 0.0f, 0.0f };
            n->prevOutV[(size_t) (v * n->numOut + i)] = { 0.0f, 0.0f };
        }
    }

    refreshActiveVoices();
}

void AquanodeModularAudioProcessor::startVoice (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes,
    int note, int sourceNote, int midiSource, float velocity01,
    bool listenersOnly, bool selfReleased, int originDriver)
{
    // 1) the same pitch from the same origin is still sounding -> retrigger it
    int v = -1;
    bool retrigger = false;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voiceStates[i].active && voiceStates[i].note == note
            && voiceStates[i].midiSource == midiSource)
        {
            v = i;
            retrigger = true;
            break;
        }

    // 2) otherwise a free voice
    if (v < 0)
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voiceStates[i].active)
            {
                v = i;
                break;
            }

    // 3) otherwise steal the oldest sounding voice
    if (v < 0)
    {
        v = 0;
        for (int i = 1; i < kMaxVoices; ++i)
            if (voiceStates[i].order < voiceStates[v].order)
                v = i;
        deactivateVoice (nodes, v);   // hard reset the stolen voice everywhere
    }

    voiceStates[v].note = note;
    voiceStates[v].sourceNote = sourceNote;
    voiceStates[v].midiSource = midiSource;
    voiceStates[v].originDriver = originDriver;
    voiceStates[v].selfReleased = selfReleased;
    voiceStates[v].active = true;
    voiceStates[v].held = true;
    voiceStates[v].countdown = 0.0;
    voiceStates[v].order = ++voiceOrderCounter;
    refreshActiveVoices();

    // Notes from a Midi Add / Arp only reach the generators patched to that
    // specific instance. Notes the player pressed reach everything EXCEPT
    // generators following an Arp, since an Arp takes those notes over rather
    // than adding to them. Generators that get no note-on simply stay silent
    // on this voice.
    //
    // Modules with no "Add Midi In" socket at all (ADSR, LFO, envelope
    // followers, etc.) aren't generators a Midi Add can be patched into -
    // they just shadow whichever voice slot is active, exactly like they do
    // for a normally-played note. Without this, a voice spawned by a Midi
    // Add would never reach that ADSR's voiceNoteOn, so its envelope would
    // stay at Idle/0 forever and silently mute the added notes even though
    // the generator itself was triggered correctly.
    for (const auto& n : nodes)
    {
        if (! n->perVoice)
            continue;

        const bool hasAddMidiIn = n->descriptor().inputIndexOf ("addMidiIn") >= 0;

        const bool shouldHear = listenersOnly
            ? (n->listensTo (midiSource) || ! hasAddMidiIn)
            : (! n->midiSourceReplaces);

        if (shouldHear)
        {
            n->dsp->voiceNoteOn (v, note, retrigger);
            n->dsp->voiceVelocity (v, velocity01);
        }
    }
}

// Every Midi Add fed by this note source spawns its extra notes around the
// given note. driverId < 0 = the host keyboard (Midi Adds with nothing in
// their Always Midi In); otherwise only the Midi Adds that driver feeds.
void AquanodeModularAudioProcessor::spawnMidiAdditions (const ProcessGraph& graph, int note,
                                                        float velocity01, int driverId,
                                                        bool selfReleased)
{
    for (const auto& ma : graph.midiAdds)
    {
        if (ma.drivenBy != driverId)
            continue;

        auto* adder = dynamic_cast<MidiAddModule*> (ma.dsp);
        if (adder == nullptr)
            continue;

        // A driven Midi Add hands the driver's own note on to its listeners
        // as well, so the chord has its root: nothing else would deliver it,
        // since those generators follow the Midi Add and not the driver.
        // Keyboard-fed Midi Adds need no such thing - the played key already
        // reaches every generator by itself.
        if (driverId >= 0)
            startVoice (graph.nodes, note, note, ma.instanceId, velocity01, true,
                        selfReleased, driverId);

        for (int slot = 0; slot < MidiAddModule::kNumSlots; ++slot)
        {
            const int offset = adder->offsetSemitones (slot);
            if (offset == 0)
                continue;   // unison copy: the generator already plays this pitch

            const int added = note + offset;
            if (added < 0 || added > 127)
                continue;   // transposed off the end of the keyboard

            startVoice (graph.nodes, added, note, ma.instanceId, velocity01, true,
                        selfReleased, driverId);
        }
    }
}

void AquanodeModularAudioProcessor::engineNoteOn (const ProcessGraph& graph, int note, float velocity01)
{
    const auto& nodes = graph.nodes;

    // the key itself: heard by every generator, exactly as before
    startVoice (nodes, note, note, -1, velocity01, false);

    // Discard Midi hands the key on to its listeners - unless the note class
    // is switched on in its grid, in which case those generators stay silent
    // and the "wrong" note simply never sounds
    for (const auto& mf : graph.midiFilters)
    {
        auto* filter = dynamic_cast<DiscardMidiModule*> (mf.dsp);
        if (filter == nullptr || filter->isNoteDiscarded (note))
            continue;

        startVoice (nodes, note, note, mf.instanceId, velocity01, true);
    }

    // plus whatever each keyboard-fed Midi Add adds around it
    spawnMidiAdditions (graph, note, velocity01, -1, false);
}

void AquanodeModularAudioProcessor::midiDriverNoteOff (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int note, int driverInstanceId)
{
    // releases the driver's own note AND every note a Midi Add chorded onto
    // it (those carry the driver in originDriver and the driver's pitch in
    // sourceNote, so a whole drone chord lets go together)
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (! voiceStates[v].active || ! voiceStates[v].held)
            continue;

        const bool isDriverNote = voiceStates[v].midiSource == driverInstanceId
                                    && voiceStates[v].note == note;
        const bool isChordedNote = voiceStates[v].originDriver == driverInstanceId
                                    && voiceStates[v].sourceNote == note;

        if (! isDriverNote && ! isChordedNote)
            continue;

        voiceStates[v].held = false;
        voiceStates[v].countdown = computeVoiceTail (nodes);

        for (const auto& n : nodes)
            if (n->perVoice)
                n->dsp->voiceNoteOff (v);
    }
}

void AquanodeModularAudioProcessor::engineNoteOff (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes, int note)
{
    // release every voice this key spawned: the note itself and any additions
    // a Midi Add made from it (matched on sourceNote, not the sounding pitch)
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (voiceStates[v].active && voiceStates[v].held
            && ! voiceStates[v].selfReleased && voiceStates[v].sourceNote == note)
        {
            voiceStates[v].held = false;
            voiceStates[v].countdown = computeVoiceTail (nodes);

            for (const auto& n : nodes)
                if (n->perVoice)
                    n->dsp->voiceNoteOff (v);
        }
    }
}

void AquanodeModularAudioProcessor::engineAllNotesOff (
    const std::vector<std::shared_ptr<ModuleInstance>>& nodes)
{
    const double tail = computeVoiceTail (nodes);
    for (int v = 0; v < kMaxVoices; ++v)
    {
        if (voiceStates[v].active && voiceStates[v].held)
        {
            voiceStates[v].held = false;
            voiceStates[v].countdown = tail;

            for (const auto& n : nodes)
                if (n->perVoice)
                    n->dsp->voiceNoteOff (v);
        }
    }
}

//==============================================================================
// patch model
//==============================================================================
void AquanodeModularAudioProcessor::initInstanceBuffers (ModuleInstance& inst)
{
    inst.numIn = juce::jmax (1, inst.descriptor().numInputs());
    inst.numOut = juce::jmax (1, inst.descriptor().numOutputs());
    inst.inBuf.assign ((size_t) inst.numIn, { 0.0f, 0.0f });
    inst.outBuf.assign ((size_t) inst.numOut, { 0.0f, 0.0f });
    inst.prevOutBuf.assign ((size_t) inst.numOut, { 0.0f, 0.0f });

    // per-voice lanes for anything that can run per-voice (PerVoice or Flexible)
    if (inst.dsp->voiceMode() != aquanode::VoiceMode::Global)
    {
        inst.outV.assign ((size_t) (inst.numOut * aquanode::kMaxVoices), { 0.0f, 0.0f });
        inst.prevOutV.assign ((size_t) (inst.numOut * aquanode::kMaxVoices), { 0.0f, 0.0f });
    }
}

int AquanodeModularAudioProcessor::addModule (const juce::String& typeId, juce::Point<int> position)
{
    auto* reg = ModuleFactory::instance().find (typeId);
    if (reg == nullptr)
        return -1;

    auto inst = std::make_shared<ModuleInstance>();
    inst->id = nextInstanceId++;
    inst->reg = reg;
    inst->dsp = ModuleFactory::instance().createInstance (typeId);
    inst->position = position;
    initInstanceBuffers (*inst);

    if (isPrepared)
        inst->dsp->prepare (currentSampleRate);

    instances.push_back (inst);
    rebuildGraph();
    return inst->id;
}

void AquanodeModularAudioProcessor::removeModule (int instanceId)
{
    cables.erase (std::remove_if (cables.begin(), cables.end(),
        [instanceId] (const CableInfo& c) { return c.fromModule == instanceId || c.toModule == instanceId; }),
        cables.end());
    paramCables.erase (std::remove_if (paramCables.begin(), paramCables.end(),
        [instanceId] (const ParamCableInfo& c) { return c.fromModule == instanceId || c.toModule == instanceId; }),
        paramCables.end());

    for (auto it = instances.begin(); it != instances.end(); ++it)
    {
        if ((*it)->id == instanceId)
        {
            graveyard.emplace_back (graphSerial + 1, std::static_pointer_cast<void> (*it));
            instances.erase (it);
            break;
        }
    }
    rebuildGraph();

    // full refresh: wipe every remaining module's transient state so nothing
    // from the deleted patch (held voices, feedback tails, delay lines) lingers
    resetAllPending.store (true, std::memory_order_release);
}

int AquanodeModularAudioProcessor::cloneModule (int instanceId)
{
    auto* src = getInstance (instanceId);
    if (src == nullptr)
        return -1;

    const int newId = addModule (src->descriptor().typeId, src->position + juce::Point<int> (24, 24));
    if (auto* dst = getInstance (newId))
    {
        for (const auto& p : src->descriptor().params)
            dst->dsp->setParameter (p.id, src->dsp->getParameter (p.id));

        const auto custom = src->dsp->saveCustomState();
        if (custom.isNotEmpty())
            dst->dsp->loadCustomState (custom);

        if (src->dsp->usesLoadedSample())
        {
            double rate = 44100.0;
            if (auto sample = src->dsp->getLoadedSample (&rate))
                dst->dsp->setLoadedSample (sample, rate);
        }
    }
    return newId;
}

void AquanodeModularAudioProcessor::setModulePosition (int instanceId, juce::Point<int> position)
{
    if (auto* inst = getInstance (instanceId))
        inst->position = position;
}

bool AquanodeModularAudioProcessor::addCable (int fromModule, const juce::String& fromSocket,
                                              int toModule, const juce::String& toSocket)
{
    auto* src = getInstance (fromModule);
    auto* dst = getInstance (toModule);
    if (src == nullptr || dst == nullptr)
        return false;

    // only output -> input is legal
    const int srcIdx = src->descriptor().outputIndexOf (fromSocket);
    const int dstIdx = dst->descriptor().inputIndexOf (toSocket);
    if (srcIdx < 0 || dstIdx < 0)
        return false;

    // MIDI is its own domain: a Midi Out only ever feeds an Add Midi In, and
    // an Add Midi In never accepts audio or modulation
    const bool srcMidi = src->descriptor().outputs()[(size_t) srcIdx]->kind == aquanode::SocketKind::Midi;
    const bool dstMidi = dst->descriptor().inputs()[(size_t) dstIdx]->kind == aquanode::SocketKind::Midi;
    if (srcMidi != dstMidi)
        return false;

    // Several MIDI sources may feed one socket: a generator can follow an
    // Arp AND a Midi Add, or drone on three Always Midis at once. They stack
    // rather than replace, so nothing is removed here.

    // no exact duplicates (fan-out and fan-in are otherwise unrestricted)
    for (const auto& c : cables)
        if (c.fromModule == fromModule && c.fromSocket == fromSocket
         && c.toModule == toModule && c.toSocket == toSocket)
            return false;

    cables.push_back ({ fromModule, fromSocket, toModule, toSocket });
    rebuildGraph();
    return true;
}

void AquanodeModularAudioProcessor::removeCable (int cableIndex)
{
    if (cableIndex >= 0 && cableIndex < (int) cables.size())
    {
        cables.erase (cables.begin() + cableIndex);
        rebuildGraph();
    }
}

bool AquanodeModularAudioProcessor::addParamCable (int fromModule, const juce::String& fromSocket,
                                                   int toModule, const juce::String& paramId, float depth)
{
    auto* src = getInstance (fromModule);
    auto* dst = getInstance (toModule);
    if (src == nullptr || dst == nullptr)
        return false;
    if (src->descriptor().outputIndexOf (fromSocket) < 0)
        return false;

    // target must be a modulatable rotary parameter that is either visible or
    // explicitly exposed as a cable target by the module's custom UI (e.g.
    // the Pitch Lock Filter's note keys)
    const aquanode::ParamSpec* spec = nullptr;
    for (const auto& ps : dst->descriptor().params)
        if (ps.id == paramId) { spec = &ps; break; }
    if (spec == nullptr || spec->type != aquanode::ParamType::Rotary
        || (spec->hidden && ! spec->hiddenCableTarget) || ! spec->modulatable)
        return false;

    // duplicate (same source socket -> same knob): just update the depth
    for (auto& pc : paramCables)
        if (pc.fromModule == fromModule && pc.fromSocket == fromSocket
            && pc.toModule == toModule && pc.paramId == paramId)
        {
            pc.depth = juce::jlimit (-1.0f, 1.0f, depth);
            rebuildGraph();
            return true;
        }

    ParamCableInfo pc;
    pc.fromModule = fromModule;
    pc.fromSocket = fromSocket;
    pc.toModule = toModule;
    pc.paramId = paramId;
    pc.depth = juce::jlimit (-1.0f, 1.0f, depth);
    paramCables.push_back (pc);
    rebuildGraph();
    return true;
}

void AquanodeModularAudioProcessor::removeParamCable (int index)
{
    if (index >= 0 && index < (int) paramCables.size())
    {
        paramCables.erase (paramCables.begin() + index);
        rebuildGraph();
    }
}

void AquanodeModularAudioProcessor::setParamCableDepth (int index, float depth)
{
    if (index >= 0 && index < (int) paramCables.size())
    {
        paramCables[(size_t) index].depth = juce::jlimit (-1.0f, 1.0f, depth);
        rebuildGraph();
    }
}

std::vector<int> AquanodeModularAudioProcessor::getModuleIds() const
{
    std::vector<int> ids;
    for (const auto& i : instances)
        ids.push_back (i->id);
    return ids;
}

ModuleInstance* AquanodeModularAudioProcessor::getInstance (int instanceId) const
{
    for (const auto& i : instances)
        if (i->id == instanceId)
            return i.get();
    return nullptr;
}

void AquanodeModularAudioProcessor::initializePatch()
{
    clearPatch();

    const int osc = addModule ("osc.basic",  { 60, 60 });
    const int out = addModule ("io.audioOut", { 460, 60 });
    addCable (osc, "audioOut", out, "audioIn");

    resetAllPending.store (true, std::memory_order_release);   // wipe voices + DSP state
    rebuildGraph();
}

void AquanodeModularAudioProcessor::clearPatch()
{
    cables.clear();
    paramCables.clear();
    for (auto& inst : instances)
        graveyard.emplace_back (graphSerial + 1, std::static_pointer_cast<void> (inst));
    instances.clear();
    nextInstanceId = 1;
}

//==============================================================================
// graph compilation: DFS topological order + back-edge (feedback) detection
//==============================================================================
void AquanodeModularAudioProcessor::rebuildGraph()
{
    purgeRetired();

    auto graph = std::make_shared<ProcessGraph>();
    graph->serial = ++graphSerial;

    const int n = (int) instances.size();

    // resolve cables to (node, socket) index edges; drop any stale ones
    struct RawEdge { int from, fromSock, to, toSock; SocketKind fromKind, toKind; bool feedback = false;
                     bool isParam = false; int paramIdx = -1; float scale = 0.0f; };
    std::vector<RawEdge> rawEdges;
    std::vector<std::vector<int>> adjacency ((size_t) n);   // outgoing raw-edge indices

    auto nodeIndexOf = [this, n] (int instanceId) -> int
    {
        for (int i = 0; i < n; ++i)
            if (instances[(size_t) i]->id == instanceId)
                return i;
        return -1;
    };

    // MIDI routing is re-derived from the cables below
    for (auto& inst : instances)
    {
        inst->midiSourceIds.clear();
        inst->midiSourceReplaces = false;
    }

    for (const auto& c : cables)
    {
        const int from = nodeIndexOf (c.fromModule);
        const int to = nodeIndexOf (c.toModule);
        if (from < 0 || to < 0)
            continue;

        const auto& fromDesc = instances[(size_t) from]->descriptor();
        const auto& toDesc = instances[(size_t) to]->descriptor();
        const int fromSock = fromDesc.outputIndexOf (c.fromSocket);
        const int toSock = toDesc.inputIndexOf (c.toSocket);
        if (fromSock < 0 || toSock < 0)
            continue;

        // MIDI cables are not signal flow: they only tell a generator which
        // Midi Add to listen to, so they never become audio/mod graph edges
        if (fromDesc.outputs()[(size_t) fromSock]->kind == SocketKind::Midi
            || toDesc.inputs()[(size_t) toSock]->kind == SocketKind::Midi)
        {
            auto& dstInst = *instances[(size_t) to];
            if (! dstInst.listensTo (c.fromModule))
                dstInst.midiSourceIds.push_back (c.fromModule);

            // one replacing source (Arp, Always Midi, Discard Midi) is enough
            // to take the played keys away from this generator
            if (instances[(size_t) from]->dsp->midiSourceReplacesInput())
                dstInst.midiSourceReplaces = true;
            continue;
        }

        RawEdge e;
        e.from = from;  e.fromSock = fromSock;
        e.to = to;      e.toSock = toSock;
        e.fromKind = fromDesc.outputs()[(size_t) fromSock]->kind;
        e.toKind = toDesc.inputs()[(size_t) toSock]->kind;

        adjacency[(size_t) from].push_back ((int) rawEdges.size());
        rawEdges.push_back (e);
    }

    // A Midi Add fed by a replacing driver hands that takeover on to its own
    // listeners: Always Midi -> Midi Add -> Oscillator drones a chord and the
    // played keys stay out of it, exactly as a direct Always Midi cable would.
    // One level deep only - deeper MIDI chains are not a supported topology.
    for (auto& inst : instances)
    {
        if (inst->midiSourceReplaces)
            continue;

        for (int srcId : inst->midiSourceIds)
        {
            auto* srcInst = getInstance (srcId);
            if (srcInst == nullptr)
                continue;

            for (int upId : srcInst->midiSourceIds)
            {
                auto* up = getInstance (upId);
                if (up != nullptr && up->dsp->isMidiNoteDriver()
                    && up->dsp->midiSourceReplacesInput())
                {
                    inst->midiSourceReplaces = true;
                    break;
                }
            }
        }
    }

    // parameter-modulation cables join the same dependency graph so sources
    // are always computed before the knobs they drive (cycles -> feedback)
    for (const auto& pc : paramCables)
    {
        const int from = nodeIndexOf (pc.fromModule);
        const int to = nodeIndexOf (pc.toModule);
        if (from < 0 || to < 0)
            continue;

        const auto& fromDesc = instances[(size_t) from]->descriptor();
        const auto& toDesc = instances[(size_t) to]->descriptor();
        const int fromSock = fromDesc.outputIndexOf (pc.fromSocket);
        if (fromSock < 0)
            continue;

        int paramIdx = -1;
        float span = 0.0f;
        for (size_t i = 0; i < toDesc.params.size(); ++i)
            if (toDesc.params[i].id == pc.paramId)
            {
                paramIdx = (int) i;
                span = toDesc.params[i].maxValue - toDesc.params[i].minValue;
                break;
            }
        if (paramIdx < 0)
            continue;

        RawEdge e;
        e.from = from;  e.fromSock = fromSock;
        e.to = to;      e.toSock = 0;
        e.fromKind = fromDesc.outputs()[(size_t) fromSock]->kind;
        e.toKind = SocketKind::Modulation;
        e.isParam = true;
        e.paramIdx = paramIdx;
        e.scale = pc.depth * span;

        adjacency[(size_t) from].push_back ((int) rawEdges.size());
        rawEdges.push_back (e);
    }

    // iterative DFS: reverse postorder = topological order for forward edges;
    // any edge into a "gray" (on-stack) node closes a cycle -> feedback edge
    enum : char { White = 0, Gray, Black };
    std::vector<char> colour ((size_t) n, White);
    std::vector<int> postorder;
    postorder.reserve ((size_t) n);

    for (int start = 0; start < n; ++start)
    {
        if (colour[(size_t) start] != White)
            continue;

        std::vector<std::pair<int, size_t>> stack;   // (node, next child index)
        stack.emplace_back (start, 0);
        colour[(size_t) start] = Gray;

        while (! stack.empty())
        {
            auto& [node, child] = stack.back();
            if (child < adjacency[(size_t) node].size())
            {
                const int edgeIdx = adjacency[(size_t) node][child++];
                const int target = rawEdges[(size_t) edgeIdx].to;

                if (colour[(size_t) target] == White)
                {
                    colour[(size_t) target] = Gray;
                    stack.emplace_back (target, 0);
                }
                else if (colour[(size_t) target] == Gray)
                {
                    rawEdges[(size_t) edgeIdx].feedback = true;   // back edge
                }
                // Black: forward/cross edge - fine
            }
            else
            {
                colour[(size_t) node] = Black;
                postorder.push_back (node);
                stack.pop_back();
            }
        }
    }

    // topological order = reverse postorder
    std::vector<int> orderPosition ((size_t) n, 0);
    graph->nodes.reserve ((size_t) n);
    for (auto it = postorder.rbegin(); it != postorder.rend(); ++it)
    {
        orderPosition[(size_t) *it] = (int) graph->nodes.size();
        graph->nodes.push_back (instances[(size_t) *it]);
    }

    graph->incoming.assign ((size_t) n, {});
    graph->incomingParams.assign ((size_t) n, {});

    // ---- resolve voice lanes -------------------------------------------------
    // PerVoice modules are always per-voice; Flexible modules become per-voice
    // when ANY source feeding them is per-voice (an osc->filter chain runs one
    // filter per note; a reverb->volume chain stays global). Iterate to a
    // fixpoint since flags only ever flip global -> per-voice.
    for (auto& inst : instances)
        inst->perVoice = (inst->dsp->voiceMode() == VoiceMode::PerVoice);

    for (bool changed = true; changed;)
    {
        changed = false;
        for (const auto& e : rawEdges)
        {
            if (e.isParam)
                continue;   // knob modulation is global; it never changes lanes
            auto& src = instances[(size_t) e.from];
            auto& dst = instances[(size_t) e.to];
            if (src->perVoice && ! dst->perVoice
                && dst->dsp->voiceMode() == VoiceMode::Flexible)
            {
                dst->perVoice = true;
                changed = true;
            }
        }
    }

    // a lane flip leaves stale state in the other lane's buffers - full refresh
    bool anyLaneChanged = false;
    for (const auto& inst : instances)
    {
        auto it = lastResolvedLanes.find (inst->id);
        if (it != lastResolvedLanes.end() && it->second != inst->perVoice)
            anyLaneChanged = true;
    }
    lastResolvedLanes.clear();
    for (const auto& inst : instances)
        lastResolvedLanes[inst->id] = inst->perVoice;
    if (anyLaneChanged)
        resetAllPending.store (true, std::memory_order_release);

    // reset connection flags, then set from live edges
    for (auto& inst : instances)
        for (int i = 0; i < 8; ++i)
            inst->dsp->setInputConnected (i, false);

    for (const auto& e : rawEdges)
    {
        if (e.isParam)
        {
            CompiledParamEdge pe;
            pe.srcNode = orderPosition[(size_t) e.from];
            pe.srcSock = e.fromSock;
            pe.paramIdx = e.paramIdx;
            pe.scale = e.scale;
            pe.feedback = e.feedback;
            graph->incomingParams[(size_t) orderPosition[(size_t) e.to]].push_back (pe);
            continue;
        }

        CompiledEdge ce;
        ce.srcNode = orderPosition[(size_t) e.from];
        ce.srcSock = e.fromSock;
        ce.dstSock = e.toSock;
        ce.feedback = e.feedback;

        if (e.fromKind == SocketKind::Audio && e.toKind == SocketKind::Modulation)
            ce.conversion = EdgeConversion::AudioToMod;
        else if (e.fromKind == SocketKind::Modulation && e.toKind == SocketKind::Audio)
            ce.conversion = EdgeConversion::ModToAudio;
        else if (e.fromKind == SocketKind::Modulation)
            ce.conversion = EdgeConversion::ModToMod;
        else
            ce.conversion = EdgeConversion::Direct;

        graph->incoming[(size_t) orderPosition[(size_t) e.to]].push_back (ce);
        instances[(size_t) e.to]->dsp->setInputConnected (e.toSock, true);
    }

    // DAW Mod slot assignment. A module keeps the slot it already claimed
    // (saved with the patch), so deleting one DAW Mod never re-points another
    // one's automation lane. Only genuinely unclaimed modules take free slots,
    // in creation order; anything past the twelfth gets none and stays silent.
    {
        bool taken[DawModModule::kMaxSlots] = {};
        std::vector<DawModModule*> unclaimed;

        for (const auto& inst : instances)
        {
            auto* dm = dynamic_cast<DawModModule*> (inst->dsp.get());
            if (dm == nullptr)
                continue;

            const int want = (int) dm->getParameter ("slot");
            if (want >= 0 && want < DawModModule::kMaxSlots && ! taken[want])
            {
                taken[want] = true;
                dm->setSlot (want);
            }
            else
            {
                unclaimed.push_back (dm);   // new, or its slot was taken by a clone
            }
        }

        for (auto* dm : unclaimed)
        {
            int slot = -1;
            for (int i = 0; i < DawModModule::kMaxSlots; ++i)
                if (! taken[i])
                {
                    slot = i;
                    taken[i] = true;
                    break;
                }

            dm->setSlot (slot);
            dm->setParameter ("slot", (float) slot);   // remember the claim
        }
    }

    // MIDI note drivers (Arp, Always Midi) with at least one listener get
    // ticked every sample; an unpatched one costs nothing
    for (const auto& inst : instances)
    {
        if (! inst->dsp->isMidiNoteDriver())
            continue;

        for (const auto& other : instances)
            if (other->listensTo (inst->id))
            {
                graph->midiDrivers.push_back ({ inst->id, inst->dsp.get() });
                break;
            }
    }

    // Midi Add instances worth evaluating on each note: only those a
    // generator is actually listening to, so an unpatched one costs nothing
    for (const auto& inst : instances)
    {
        if (inst->descriptor().typeId != "util.midiadd")
            continue;

        bool hasListener = false;
        for (const auto& other : instances)
            if (other->listensTo (inst->id))
            {
                hasListener = true;
                break;
            }

        if (! hasListener)
            continue;

        // a note driver patched into this Midi Add's own "Always Midi In"
        // chords THAT driver's notes instead of the played keys - an Always
        // Midi through a Midi Add drones a whole chord. Only the first
        // driver counts: two drivers into one Midi Add would interleave
        // note-offs unpredictably.
        int drivenBy = -1;
        for (int srcId : inst->midiSourceIds)
        {
            auto* srcInst = getInstance (srcId);
            if (srcInst != nullptr && srcInst->dsp->isMidiNoteDriver())
            {
                drivenBy = srcId;
                break;
            }
        }

        graph->midiAdds.push_back ({ inst->id, inst->dsp.get(), drivenBy });
    }

    // Discard Midi instances with listeners: they hand the keyboard to those
    // generators minus the note classes switched on in the grid
    for (const auto& inst : instances)
    {
        if (inst->descriptor().typeId != "util.discardmidi")
            continue;

        for (const auto& other : instances)
            if (other->listensTo (inst->id))
            {
                graph->midiFilters.push_back ({ inst->id, inst->dsp.get() });
                break;
            }
    }

    // publish: keep the old graph alive in the graveyard until the audio
    // thread has adopted the new one, so nothing is freed under its feet
    std::shared_ptr<const ProcessGraph> old;
    {
        const juce::SpinLock::ScopedLockType sl (graphLock);
        old = renderGraph;
        renderGraph = graph;
    }
    if (old != nullptr)
        graveyard.emplace_back (graph->serial, std::const_pointer_cast<ProcessGraph> (old));
}

void AquanodeModularAudioProcessor::purgeRetired()
{
    const auto adopted = audioAdoptedSerial.load (std::memory_order_acquire);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
        [adopted] (const auto& entry) { return adopted >= entry.first; }),
        graveyard.end());
}

//==============================================================================
// serialization
//==============================================================================
bool AquanodeModularAudioProcessor::writeSampleAsWav (const juce::AudioBuffer<float>& buffer,
                                                      double rate, juce::MemoryBlock& dest)
{
    juce::WavAudioFormat wav;
    auto* stream = new juce::MemoryOutputStream (dest, false);
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream, rate, (unsigned int) buffer.getNumChannels(), 16, {}, 0));

    if (writer == nullptr)
    {
        delete stream;   // writer would have taken ownership on success
        return false;
    }

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();      // flushes into dest
    return dest.getSize() > 0;
}

std::shared_ptr<juce::AudioBuffer<float>> AquanodeModularAudioProcessor::readWavFromMemory (
    const void* data, size_t size, double& rate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::MemoryInputStream (data, size, true), true));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return nullptr;

    const int numChannels = juce::jmax (1, juce::jmin (2, (int) reader->numChannels));
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (numChannels, (int) reader->lengthInSamples);
    reader->read (buffer.get(), 0, (int) reader->lengthInSamples, 0, true, numChannels > 1);
    rate = reader->sampleRate;
    return buffer;
}

std::unique_ptr<juce::XmlElement> AquanodeModularAudioProcessor::buildPatchXml (
    bool embedSamplesAsBase64, juce::ZipFile::Builder* zipBuilder)
{
    auto root = std::make_unique<juce::XmlElement> ("AquanodePatch");
    root->setAttribute ("version", 1);

    auto* modulesEl = root->createNewChildElement ("Modules");
    for (const auto& inst : instances)
    {
        auto* m = modulesEl->createNewChildElement ("Module");
        m->setAttribute ("id", inst->id);
        m->setAttribute ("type", inst->descriptor().typeId);
        m->setAttribute ("x", inst->position.x);
        m->setAttribute ("y", inst->position.y);

        for (const auto& p : inst->descriptor().params)
        {
            if (p.type == ParamType::Button)
                continue;

            auto* pe = m->createNewChildElement ("Param");
            pe->setAttribute ("id", p.id);

            const float v = inst->dsp->getParameter (p.id);
            if ((p.type == ParamType::Combo || p.type == ParamType::RotarySteppedList)
                && p.choices.size() > 0)
                pe->setAttribute ("value", p.choices[juce::jlimit (0, p.choices.size() - 1, (int) v)]);
            else
                pe->setAttribute ("value", v);
        }

        // opaque per-module blob (Draw LFO's curve, etc.)
        {
            const auto custom = inst->dsp->saveCustomState();
            if (custom.isNotEmpty())
                m->createNewChildElement ("CustomState")->addTextElement (custom);
        }

        if (inst->dsp->usesLoadedSample())
        {
            double rate = 44100.0;
            if (auto sample = inst->dsp->getLoadedSample (&rate))
            {
                juce::MemoryBlock wavData;
                if (writeSampleAsWav (*sample, rate, wavData))
                {
                    auto* se = m->createNewChildElement ("EmbeddedSample");
                    se->setAttribute ("format", "wav");

                    if (embedSamplesAsBase64)
                    {
                        se->setAttribute ("base64",
                            juce::Base64::toBase64 (wavData.getData(), wavData.getSize()));
                    }
                    else if (zipBuilder != nullptr)
                    {
                        const auto fileName = "samples/" + juce::String (inst->id) + "_"
                            + inst->descriptor().typeId.replaceCharacter ('.', '_') + ".wav";
                        se->setAttribute ("file", fileName);
                        zipBuilder->addEntry (new juce::MemoryInputStream (wavData, true),
                                              5, fileName, juce::Time::getCurrentTime());
                    }
                }
            }
        }
    }

    // The host's automation values belong to the DAW session, not to a patch
    // that gets shared around, so they only ride along in the plugin's own
    // state blob (base64 mode) and never in an exported patch zip.
    if (embedSamplesAsBase64)
    {
        auto* hm = root->createNewChildElement ("HostMods");
        for (size_t i = 0; i < dawModParams.size(); ++i)
            if (dawModParams[i] != nullptr)
                hm->setAttribute ("m" + juce::String ((int) i + 1),
                                  (double) dawModParams[i]->get());
    }

    auto* cablesEl = root->createNewChildElement ("Cables");
    for (const auto& c : cables)
    {
        auto* ce = cablesEl->createNewChildElement ("Cable");
        ce->setAttribute ("fromModule", c.fromModule);
        ce->setAttribute ("fromSocket", c.fromSocket);
        ce->setAttribute ("toModule", c.toModule);
        ce->setAttribute ("toSocket", c.toSocket);
    }

    auto* paramCablesEl = root->createNewChildElement ("ParamCables");
    for (const auto& pc : paramCables)
    {
        auto* pe = paramCablesEl->createNewChildElement ("ParamCable");
        pe->setAttribute ("fromModule", pc.fromModule);
        pe->setAttribute ("fromSocket", pc.fromSocket);
        pe->setAttribute ("toModule", pc.toModule);
        pe->setAttribute ("param", pc.paramId);
        pe->setAttribute ("depth", pc.depth);
    }

    return root;
}

void AquanodeModularAudioProcessor::applyPatchXml (const juce::XmlElement& xml,
    const std::function<std::shared_ptr<juce::AudioBuffer<float>> (const juce::String&, double&)>& sampleLoader)
{
    if (! xml.hasTagName ("AquanodePatch"))
        return;

    clearPatch();

    int maxId = 0;

    if (auto* modulesEl = xml.getChildByName ("Modules"))
    {
        for (auto* m : modulesEl->getChildWithTagNameIterator ("Module"))
        {
            const auto typeId = m->getStringAttribute ("type");
            auto* reg = ModuleFactory::instance().find (typeId);
            if (reg == nullptr)
                continue;   // module type no longer registered - skip gracefully

            auto inst = std::make_shared<ModuleInstance>();
            inst->id = m->getIntAttribute ("id");
            inst->reg = reg;
            inst->dsp = ModuleFactory::instance().createInstance (typeId);
            inst->position = { m->getIntAttribute ("x"), m->getIntAttribute ("y") };
            maxId = juce::jmax (maxId, inst->id);
            initInstanceBuffers (*inst);

            for (auto* pe : m->getChildWithTagNameIterator ("Param"))
            {
                const auto paramId = pe->getStringAttribute ("id");
                const auto* spec = reg->descriptor.findParam (paramId);
                if (spec == nullptr)
                    continue;

                const auto text = pe->getStringAttribute ("value");
                float value = (float) text.getDoubleValue();

                if ((spec->type == ParamType::Combo || spec->type == ParamType::RotarySteppedList))
                {
                    const int idx = spec->choices.indexOf (text);
                    if (idx >= 0)
                        value = (float) idx;
                }

                inst->dsp->setParameter (paramId, value);
            }

            if (auto* cs = m->getChildByName ("CustomState"))
                inst->dsp->loadCustomState (cs->getAllSubText());

            if (auto* se = m->getChildByName ("EmbeddedSample"))
            {
                double rate = 44100.0;
                std::shared_ptr<juce::AudioBuffer<float>> sample;

                const auto base64 = se->getStringAttribute ("base64");
                if (base64.isNotEmpty())
                {
                    juce::MemoryOutputStream decoded;
                    if (juce::Base64::convertFromBase64 (decoded, base64))
                        sample = readWavFromMemory (decoded.getData(), decoded.getDataSize(), rate);
                }
                else if (sampleLoader != nullptr)
                {
                    const auto fileRef = se->getStringAttribute ("file");
                    if (fileRef.isNotEmpty())
                        sample = sampleLoader (fileRef, rate);
                }

                if (sample != nullptr)
                    inst->dsp->setLoadedSample (sample, rate);
            }

            if (isPrepared)
                inst->dsp->prepare (currentSampleRate);

            instances.push_back (inst);
        }
    }

    nextInstanceId = maxId + 1;

    if (auto* cablesEl = xml.getChildByName ("Cables"))
    {
        for (auto* ce : cablesEl->getChildWithTagNameIterator ("Cable"))
        {
            cables.push_back ({ ce->getIntAttribute ("fromModule"),
                                ce->getStringAttribute ("fromSocket"),
                                ce->getIntAttribute ("toModule"),
                                ce->getStringAttribute ("toSocket") });
        }
    }

    if (auto* hm = xml.getChildByName ("HostMods"))
    {
        for (size_t i = 0; i < dawModParams.size(); ++i)
        {
            const auto attr = "m" + juce::String ((int) i + 1);
            if (dawModParams[i] != nullptr && hm->hasAttribute (attr))
                *dawModParams[i] = (float) hm->getDoubleAttribute (attr);
        }
    }

    if (auto* paramCablesEl = xml.getChildByName ("ParamCables"))
    {
        for (auto* pe : paramCablesEl->getChildWithTagNameIterator ("ParamCable"))
        {
            ParamCableInfo pc;
            pc.fromModule = pe->getIntAttribute ("fromModule");
            pc.fromSocket = pe->getStringAttribute ("fromSocket");
            pc.toModule = pe->getIntAttribute ("toModule");
            pc.paramId = pe->getStringAttribute ("param");
            pc.depth = juce::jlimit (-1.0f, 1.0f, (float) pe->getDoubleAttribute ("depth", 0.3));
            paramCables.push_back (pc);
        }
    }

    rebuildGraph();
    resetAllPending.store (true, std::memory_order_release);   // fresh patch: wipe voices + DSP state
    sendChangeMessage();
}

//==============================================================================
void AquanodeModularAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // host state: one opaque blob, samples base64-embedded inline (no zip)
    if (auto xml = buildPatchXml (true, nullptr))
        copyXmlToBinary (*xml, destData);
}

void AquanodeModularAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyPatchXml (*xml, nullptr);
}

//==============================================================================
bool AquanodeModularAudioProcessor::exportPatchToZip (const juce::File& zipFile)
{
    juce::ZipFile::Builder builder;

    auto xml = buildPatchXml (false, &builder);   // samples as real files in the zip
    if (xml == nullptr)
        return false;

    const auto xmlText = xml->toString();
    juce::MemoryBlock xmlBlock (xmlText.toRawUTF8(), xmlText.getNumBytesAsUTF8());
    builder.addEntry (new juce::MemoryInputStream (xmlBlock, true),
                      5, "patch.xml", juce::Time::getCurrentTime());

    zipFile.deleteFile();
    juce::FileOutputStream out (zipFile);
    if (! out.openedOk())
        return false;

    double progress = 0.0;
    return builder.writeToStream (out, &progress);
}

bool AquanodeModularAudioProcessor::importPatchFromZip (const juce::File& zipFile)
{
    juce::ZipFile zip (zipFile);

    const int xmlIndex = zip.getIndexOfFileName ("patch.xml");
    if (xmlIndex < 0)
        return false;

    juce::String xmlText;
    {
        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (xmlIndex));
        if (stream == nullptr)
            return false;
        xmlText = stream->readEntireStreamAsString();
    }

    auto xml = juce::parseXML (xmlText);
    if (xml == nullptr)
        return false;

    applyPatchXml (*xml, [&zip] (const juce::String& fileRef, double& rate)
        -> std::shared_ptr<juce::AudioBuffer<float>>
    {
        const int idx = zip.getIndexOfFileName (fileRef);
        if (idx < 0)
            return nullptr;

        std::unique_ptr<juce::InputStream> stream (zip.createStreamForEntry (idx));
        if (stream == nullptr)
            return nullptr;

        juce::MemoryBlock data;
        stream->readIntoMemoryBlock (data);
        return readWavFromMemory (data.getData(), data.getSize(), rate);
    });

    return true;
}

//==============================================================================
juce::AudioProcessorEditor* AquanodeModularAudioProcessor::createEditor()
{
    return new AquanodeModularAudioProcessorEditor (*this);
}

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AquanodeModularAudioProcessor();
}
