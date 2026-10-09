#include "AquaVibrioNoteLayerManager.h"
#include "AquaVibrioEngine.h"

namespace aquavibrio
{

const juce::Identifier NoteLayerManager::treeType ("NoteLayers");

namespace
{
    bool isGlobalIndex (int i) { return layers::scopeOf (i) == layers::Scope::Global; }

    bool sameLayers (const std::vector<NoteLayerManager::Layer>& a, const std::vector<NoteLayerManager::Layer>& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].number != b[i].number || a[i].notes != b[i].notes || a[i].full != b[i].full || a[i].values != b[i].values)
                return false;
        return true;
    }
}

//==============================================================================
NoteLayerManager::NoteLayerManager (juce::AudioProcessorValueTreeState& state, Engine& e)
    : apvts (state), engine (e), rng (0x1a7e5)
{
    for (const auto& p : allParameters())
    {
        params.push_back (apvts.getParameter (p.id));
        raws.push_back (apvts.getRawParameterValue (p.id));
    }

    publish();
    startTimerHz (30);
}

NoteLayerManager::~NoteLayerManager()
{
    stopTimer();
}

//==============================================================================
std::vector<float> NoteLayerManager::readLive() const
{
    std::vector<float> v (raws.size(), 0.0f);
    for (size_t i = 0; i < raws.size(); ++i)
        if (raws[i] != nullptr)
            v[i] = raws[i]->load();
    return v;
}

void NoteLayerManager::writeLive (const std::vector<float>& target, bool skipGlobal)
{
    for (size_t i = 0; i < params.size() && i < target.size(); ++i)
    {
        if (params[i] == nullptr || raws[i] == nullptr || (skipGlobal && isGlobalIndex ((int) i)))
            continue;
        if (raws[i]->load() == target[i])
            continue;

        params[i]->beginChangeGesture();
        params[i]->setValueNotifyingHost (params[i]->convertTo0to1 (target[i]));
        params[i]->endChangeGesture();
    }
}

//==============================================================================
// What the panel shows
//==============================================================================
std::vector<NoteLayerManager::Layer> NoteLayerManager::getLayers() const
{
    const juce::ScopedLock sl (lock);
    return layers;
}

int NoteLayerManager::numberForNote (int note) const
{
    const juce::ScopedLock sl (lock);
    if (note < 0 || note > 127)
        return 0;
    for (const auto& l : layers)
        if (l.notes[(size_t) note])
            return l.number;
    return 0;
}

bool NoteLayerManager::isSelected (int note) const
{
    const juce::ScopedLock sl (lock);
    return session.open && note >= 0 && note < 128 && session.notes[(size_t) note];
}

NoteLayerManager::Notes NoteLayerManager::getSelection() const
{
    const juce::ScopedLock sl (lock);
    return session.open ? session.notes : Notes();
}

bool NoteLayerManager::hasSelection() const
{
    const juce::ScopedLock sl (lock);
    return session.open;
}

bool NoteLayerManager::editedLocked() const
{
    if (! session.open)
        return false;
    if (session.wholePatch)
        return true;

    const auto live = readLive();
    for (size_t i = 0; i < live.size() && i < session.start.size(); ++i)
        if (! isGlobalIndex ((int) i) && live[i] != session.start[i])
            return true;
    return false;
}

bool NoteLayerManager::selectionIsEdited() const
{
    const juce::ScopedLock sl (lock);
    return editedLocked();
}

int NoteLayerManager::upcomingNumber() const
{
    const juce::ScopedLock sl (lock);
    auto copy = layers;
    if (session.open)
        removeNotes (copy, session.notes);
    return lowestFreeNumber (copy);
}

bool NoteLayerManager::isChangedInSelection (int index) const
{
    const juce::ScopedLock sl (lock);
    if (! session.open || index < 0 || index >= (int) raws.size() || isGlobalIndex (index))
        return false;
    if (session.full || session.sourceKeys.count (index) > 0)
        return true;
    return raws[(size_t) index] != nullptr && raws[(size_t) index]->load() != session.stash[(size_t) index];
}

juce::String NoteLayerManager::describeSelection() const
{
    const juce::ScopedLock sl (lock);
    if (! session.open)
        return {};

    juce::StringArray names;
    int count = 0;
    for (int n = 0; n < 128; ++n)
        if (session.notes[(size_t) n])
        {
            if (count < 10)
                names.add (layers::noteName (n));
            ++count;
        }

    auto text = names.joinIntoString (" ");
    if (count > 10)
        text << " +" << (count - 10) << " more";
    return text;
}

//==============================================================================
// Layers bookkeeping
//==============================================================================
void NoteLayerManager::removeNotes (std::vector<Layer>& list, const Notes& notes)
{
    for (auto& l : list)
        l.notes &= ~notes;

    list.erase (std::remove_if (list.begin(), list.end(), [] (const Layer& l) { return l.notes.none(); }), list.end());
}

int NoteLayerManager::lowestFreeNumber (const std::vector<Layer>& list)
{
    for (int n = 1;; ++n)
    {
        bool used = false;
        for (const auto& l : list)
            used = used || l.number == n;
        if (! used)
            return n;
    }
}

std::map<int, float> NoteLayerManager::sessionOverrides (const std::vector<float>& live) const
{
    std::map<int, float> values;
    for (int i = 0; i < (int) live.size(); ++i)
    {
        if (isGlobalIndex (i))
            continue;
        if (session.full || session.sourceKeys.count (i) > 0 || live[(size_t) i] != session.stash[(size_t) i])
            values[i] = live[(size_t) i];
    }
    return values;
}

std::vector<NoteLayerManager::Layer> NoteLayerManager::layersWithSessionCommitted (const std::vector<float>& live) const
{
    auto result = layers;
    if (! session.open || ! editedLocked())
        return result;

    removeNotes (result, session.notes);

    auto values = sessionOverrides (live);
    if (! values.empty())
    {
        Layer l;
        l.number = lowestFreeNumber (result);
        l.uid = session.uid;
        l.notes = session.notes;
        l.values = std::move (values);
        l.full = session.full;
        result.push_back (std::move (l));
    }

    std::sort (result.begin(), result.end(), [] (const Layer& a, const Layer& b) { return a.number < b.number; });
    return result;
}

void NoteLayerManager::sortLayers()
{
    std::sort (layers.begin(), layers.end(), [] (const Layer& a, const Layer& b) { return a.number < b.number; });
}

//==============================================================================
// Selecting
//==============================================================================
void NoteLayerManager::openSession (const Notes& notes)
{
    {
        const juce::ScopedLock sl (lock);
        session = Session {};
        session.open = true;
        session.uid = nextUid++;
        session.notes = notes;
        session.stash = readLive();
        session.start = session.stash;
    }
    deriveStart();
}

// The sound a selection starts from: a layer's own when every selected note
// belongs to that one layer (so it can be reworked), the base patch otherwise.
void NoteLayerManager::deriveStart()
{
    std::vector<float> target;
    {
        const juce::ScopedLock sl (lock);
        if (! session.open)
            return;

        const Layer* common = nullptr;
        bool first = true, allSame = true;

        for (int n = 0; n < 128 && allSame; ++n)
        {
            if (! session.notes[(size_t) n])
                continue;

            const Layer* owner = nullptr;
            for (const auto& l : layers)
                if (l.notes[(size_t) n]) { owner = &l; break; }

            if (first)  { common = owner; first = false; }
            else if (owner != common) allSame = false;
        }

        target = session.stash;
        session.sourceKeys.clear();
        session.full = false;

        if (allSame && common != nullptr)
        {
            for (const auto& [idx, value] : common->values)
                if (idx >= 0 && idx < (int) target.size() && ! isGlobalIndex (idx))
                {
                    target[(size_t) idx] = value;
                    session.sourceKeys.insert (idx);
                }
            session.full = common->full;
        }

        session.start = target;
    }

    writeLive (target, true);
}

void NoteLayerManager::closeSession (bool restoreBase)
{
    std::vector<float> stash;
    {
        const juce::ScopedLock sl (lock);
        if (! session.open)
            return;
        stash = session.stash;
    }

    if (restoreBase)
        writeLive (stash);

    {
        const juce::ScopedLock sl (lock);
        session = Session {};
    }

    history.clear();
    historyPos = -1;
    historyOwner = -1;
}

void NoteLayerManager::setSelected (int note, bool selected)
{
    if (note < 0 || note > 127)
        return;

    Notes n = getSelection();
    n.set ((size_t) note, selected);
    setSelection (n);
}

void NoteLayerManager::setSelection (const Notes& notes)
{
    if (notes.none())
    {
        cancelSelection();
        return;
    }

    if (! hasSelection())
    {
        openSession (notes);
        history.clear(); historyPos = -1; historyOwner = -1;
        publish();
        return;
    }

    bool edited;
    {
        const juce::ScopedLock sl (lock);
        edited = editedLocked();
        session.notes = notes;
    }

    // Until a knob has been touched, the selection decides the starting sound.
    if (! edited)
        deriveStart();

    publish();
}

void NoteLayerManager::selectLayer (int number)
{
    Notes notes;
    for (const auto& l : getLayers())
        if (l.number == number)
            notes = l.notes;

    if (notes.none())
        return;

    if (hasSelection() && selectionIsEdited())
        finishSelection();

    setSelection (notes);
}

void NoteLayerManager::finishSelection()
{
    if (! hasSelection())
        return;

    std::vector<Layer> result;
    {
        const juce::ScopedLock sl (lock);
        result = layersWithSessionCommitted (readLive());
    }

    closeSession (true);

    {
        const juce::ScopedLock sl (lock);
        layers = std::move (result);
        sortLayers();
    }

    publish();
}

void NoteLayerManager::cancelSelection()
{
    if (! hasSelection())
        return;

    closeSession (true);
    publish();
}

void NoteLayerManager::resetToBase (const Notes& notes)
{
    {
        const juce::ScopedLock sl (lock);
        removeNotes (layers, notes);
    }

    if (hasSelection())
    {
        const Notes remaining = getSelection() & ~notes;
        if (remaining != getSelection())
        {
            setSelection (remaining);   // publishes, or cancels when empty
            return;
        }
    }

    publish();
}

void NoteLayerManager::resetSelectionToBase()
{
    const Notes notes = getSelection();
    if (notes.none())
        return;

    cancelSelection();
    resetToBase (notes);
}

void NoteLayerManager::clearAll()
{
    if (hasSelection())
        closeSession (true);

    {
        const juce::ScopedLock sl (lock);
        layers.clear();
    }

    history.clear(); historyPos = -1; historyOwner = -1;
    publish();
}

//==============================================================================
// Whole patches
//==============================================================================
void NoteLayerManager::loadWholePatch (const std::function<void()>& load, const juce::var& noteLayers)
{
    if (hasSelection())
    {
        load();

        // The global knobs (arp, key mode, tempo, input) stay the base patch's.
        auto fixed = readLive();
        {
            const juce::ScopedLock sl (lock);
            for (int i : layers::indicesWithScope (layers::Scope::Global))
                fixed[(size_t) i] = session.stash[(size_t) i];
            session.full = true;
            session.wholePatch = true;
            session.sourceKeys.clear();
        }
        writeLive (fixed);

        history.clear(); historyPos = -1; historyOwner = -1;
        publish();
        return;
    }

    load();

    auto incoming = layersFromJson (noteLayers);
    {
        const juce::ScopedLock sl (lock);
        for (auto& l : incoming)
            l.uid = nextUid++;
        layers = std::move (incoming);
        sortLayers();
    }

    history.clear(); historyPos = -1; historyOwner = -1;
    publish();
}

//==============================================================================
// Mutation and its history
//==============================================================================
void NoteLayerManager::pushGeneration (int context, bool withLayers)
{
    if (historyOwner != context)
    {
        history.clear();
        historyPos = -1;
        historyOwner = context;
    }

    Generation g { readLive(), withLayers ? getLayers() : std::vector<Layer>() };

    if (historyPos >= 0 && history[(size_t) historyPos].values == g.values
        && sameLayers (history[(size_t) historyPos].layers, g.layers))
        return;   // already where the history stands

    history.resize ((size_t) (historyPos + 1));   // a new branch drops the old redo steps
    history.push_back (std::move (g));
    historyPos = (int) history.size() - 1;

    constexpr int kMaxGenerations = 64;
    while ((int) history.size() > kMaxGenerations)
    {
        history.erase (history.begin());
        --historyPos;
    }
}

void NoteLayerManager::restoreGeneration (const Generation& g)
{
    writeLive (g.values, historyOwner != 0);

    if (historyOwner == 0)
    {
        const juce::ScopedLock sl (lock);
        layers = g.layers;
    }

    publish();
}

void NoteLayerManager::mutate (Mutator::Strength strength)
{
    const int context = historyContext();
    pushGeneration (context, context == 0);

    auto values = readLive();
    Mutator::mutate (values, strength, rng);
    writeLive (values, true);

    pushGeneration (context, context == 0);
    sendChangeMessage();
}

void NoteLayerManager::randomAssign (Mutator::Strength strength)
{
    if (hasSelection())
    {
        // The selected notes share one number, so they share one mutation.
        Generation before;
        {
            const juce::ScopedLock sl (lock);
            before = { session.stash, layers };
        }

        auto values = readLive();
        Mutator::mutate (values, strength, rng);
        writeLive (values, true);
        finishSelection();

        history = { before };
        historyPos = 0;
        historyOwner = 0;
        pushGeneration (0, true);
        sendChangeMessage();
        return;
    }

    // Nothing selected: every key gets a number, and a mutation, of its own -
    // each one grown from the sound that key had.
    pushGeneration (0, true);

    const auto base = readLive();
    const auto current = getLayers();
    std::vector<Layer> fresh;

    for (int n = 0; n < 128; ++n)
    {
        const Layer* old = nullptr;
        for (const auto& l : current)
            if (l.notes[(size_t) n]) { old = &l; break; }

        auto v = base;
        if (old != nullptr)
            for (const auto& [idx, value] : old->values)
                if (! isGlobalIndex (idx))
                    v[(size_t) idx] = value;

        Mutator::mutate (v, strength, rng);

        Layer l;
        l.number = n + 1;
        l.notes.set ((size_t) n);
        l.full = old != nullptr && old->full;

        for (int i = 0; i < (int) v.size(); ++i)
            if (! isGlobalIndex (i)
                && (l.full || v[(size_t) i] != base[(size_t) i] || (old != nullptr && old->values.count (i) > 0)))
                l.values[i] = v[(size_t) i];

        if (! l.values.empty())
            fresh.push_back (std::move (l));
    }

    {
        const juce::ScopedLock sl (lock);
        for (auto& l : fresh)
            l.uid = nextUid++;
        layers = std::move (fresh);
    }

    pushGeneration (0, true);
    publish();
}

bool NoteLayerManager::canStepBack() const
{
    return historyOwner == historyContext() && historyPos > 0;
}

bool NoteLayerManager::canStepForward() const
{
    return historyOwner == historyContext() && historyPos >= 0 && historyPos + 1 < (int) history.size();
}

void NoteLayerManager::stepBack()
{
    if (historyOwner != historyContext() || historyPos < 0)
        return;

    // Knobs turned since the last mutation become a step of their own, so
    // Forward can bring them back.
    pushGeneration (historyOwner, historyOwner == 0);

    if (historyPos > 0)
        restoreGeneration (history[(size_t) --historyPos]);
}

void NoteLayerManager::stepForward()
{
    if (canStepForward())
        restoreGeneration (history[(size_t) ++historyPos]);
}

//==============================================================================
// Saving
//==============================================================================
std::vector<float> NoteLayerManager::committedBaseValues() const
{
    const juce::ScopedLock sl (lock);
    return session.open ? session.stash : readLive();
}

std::vector<NoteLayerManager::Layer> NoteLayerManager::committedLayers() const
{
    const juce::ScopedLock sl (lock);
    return session.open ? layersWithSessionCommitted (readLive()) : layers;
}

juce::ValueTree NoteLayerManager::toValueTree() const
{
    juce::ValueTree tree (treeType);
    tree.setProperty ("version", 1, nullptr);

    for (const auto& l : committedLayers())
    {
        juce::StringArray notes, values;
        for (int n = 0; n < 128; ++n)
            if (l.notes[(size_t) n])
                notes.add (juce::String (n));
        for (const auto& [idx, value] : l.values)
            values.add (juce::String (allParameters()[(size_t) idx].id) + ":" + juce::String (juce::roundToInt (value)));

        juce::ValueTree child ("Layer");
        child.setProperty ("number", l.number, nullptr);
        child.setProperty ("notes", notes.joinIntoString (" "), nullptr);
        child.setProperty ("full", l.full, nullptr);
        child.setProperty ("values", values.joinIntoString (";"), nullptr);
        tree.appendChild (child, nullptr);
    }

    return tree;
}

juce::var NoteLayerManager::toJson() const
{
    juce::Array<juce::var> list;

    for (const auto& l : committedLayers())
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("number", l.number);

        juce::Array<juce::var> notes;
        for (int n = 0; n < 128; ++n)
            if (l.notes[(size_t) n])
                notes.add (n);
        obj->setProperty ("notes", notes);
        obj->setProperty ("full", l.full);

        auto values = std::make_unique<juce::DynamicObject>();
        for (const auto& [idx, value] : l.values)
            values->setProperty (juce::String (allParameters()[(size_t) idx].id), juce::roundToInt (value));
        obj->setProperty ("values", juce::var (values.release()));

        list.add (juce::var (obj.release()));
    }

    return list;
}

// Shared by the state tree and the preset JSON: tolerant of anything odd, so a
// damaged or hand-edited file still loads what it can.
static std::vector<NoteLayerManager::Layer> sanitise (std::vector<NoteLayerManager::Layer> list)
{
    NoteLayerManager::Notes claimed;
    std::vector<NoteLayerManager::Layer> out;

    for (auto& l : list)
    {
        l.notes &= ~claimed;                         // a key belongs to one layer only
        if (l.notes.none() || l.values.empty())
            continue;
        claimed |= l.notes;
        out.push_back (std::move (l));
    }

    // numbers unique and positive
    std::set<int> used;
    for (auto& l : out)
    {
        if (l.number < 1 || used.count (l.number) > 0)
        {
            int n = 1;
            while (used.count (n) > 0) ++n;
            l.number = n;
        }
        used.insert (l.number);
    }

    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.number < b.number; });
    return out;
}

std::vector<NoteLayerManager::Layer> NoteLayerManager::layersFromJson (const juce::var& json)
{
    std::vector<Layer> list;
    if (! json.isArray())
        return list;

    for (const auto& item : *json.getArray())
    {
        Layer l;
        l.number = (int) item.getProperty ("number", 0);
        l.full = (bool) item.getProperty ("full", false);

        if (const auto* notes = item.getProperty ("notes", {}).getArray())
            for (const auto& n : *notes)
                if ((int) n >= 0 && (int) n < 128)
                    l.notes.set ((size_t) (int) n);

        if (auto* values = item.getProperty ("values", {}).getDynamicObject())
            for (const auto& prop : values->getProperties())
            {
                const int idx = layers::indexOf (prop.name.toString());
                if (idx >= 0 && ! isGlobalIndex (idx))
                    l.values[idx] = (float) (double) prop.value;
            }

        list.push_back (std::move (l));
    }

    return sanitise (std::move (list));
}

void NoteLayerManager::restoreFrom (const juce::ValueTree& tree)
{
    std::vector<Layer> list;

    if (tree.hasType (treeType))
        for (const auto& child : tree)
        {
            Layer l;
            l.number = (int) child.getProperty ("number", 0);
            l.full = (bool) child.getProperty ("full", false);

            for (const auto& n : juce::StringArray::fromTokens (child.getProperty ("notes").toString(), " ", ""))
                if (n.isNotEmpty() && n.getIntValue() >= 0 && n.getIntValue() < 128)
                    l.notes.set ((size_t) n.getIntValue());

            for (const auto& pair : juce::StringArray::fromTokens (child.getProperty ("values").toString(), ";", ""))
            {
                const int idx = layers::indexOf (pair.upToFirstOccurrenceOf (":", false, false));
                if (idx >= 0 && ! isGlobalIndex (idx))
                    l.values[idx] = pair.fromFirstOccurrenceOf (":", false, false).getFloatValue();
            }

            list.push_back (std::move (l));
        }

    list = sanitise (std::move (list));

    {
        const juce::ScopedLock sl (lock);
        for (auto& l : list)
            l.uid = nextUid++;
        layers = std::move (list);
        session = Session {};   // the host replaced the whole state: nothing to restore
    }

    history.clear(); historyPos = -1; historyOwner = -1;
    publish();
}

void NoteLayerManager::writeCommittedState (juce::ValueTree& stateCopy) const
{
    const juce::ScopedLock sl (lock);

    if (session.open)
    {
        // The knobs show the selection; the host must store the base patch.
        for (auto child : stateCopy)
        {
            const int idx = layers::indexOf (child.getProperty ("id").toString());
            if (idx >= 0 && idx < (int) session.stash.size())
                child.setProperty ("value", session.stash[(size_t) idx], nullptr);   // the tree keeps raw values
        }
    }

    stateCopy.removeChild (stateCopy.getChildWithName (treeType), nullptr);
    stateCopy.appendChild (toValueTree(), nullptr);
}

//==============================================================================
// Publishing to the engine
//==============================================================================
void NoteLayerManager::publish()
{
    auto snap = std::make_unique<NoteLayerSnapshot>();
    int fxLayers = 0;

    {
        const juce::ScopedLock sl (lock);

        snap->serial = ++serialCounter;
        snap->slots.clear();
        snap->slots.push_back ({});    // slot 0: the base patch
        snap->slotForNote.fill (0);

        for (const auto& l : layers)
        {
            NoteLayerSnapshot::Slot slot;
            slot.uid = l.uid;
            bool hasFx = false;
            for (const auto& [idx, value] : l.values)
                if (! isGlobalIndex (idx))
                {
                    slot.overrides.emplace_back (idx, value);
                    hasFx = hasFx || layers::scopeOf (idx) == layers::Scope::Chain;
                }
            fxLayers += hasFx ? 1 : 0;

            const auto s = (juce::uint8) snap->slots.size();
            snap->slots.push_back (std::move (slot));
            for (int n = 0; n < 128; ++n)
                if (l.notes[(size_t) n] && ! (session.open && session.notes[(size_t) n]))
                    snap->slotForNote[(size_t) n] = s;
        }

        if (session.open)
        {
            NoteLayerSnapshot::Slot slot;
            slot.uid = session.uid;
            slot.live = true;
            const auto s = (juce::uint8) snap->slots.size();
            snap->slots.push_back (std::move (slot));
            for (int n = 0; n < 128; ++n)
                if (session.notes[(size_t) n])
                    snap->slotForNote[(size_t) n] = s;

            snap->sessionOpen = true;
            snap->stash = session.stash;
        }
    }

    engine.publishLayers (snap.get());
    snapshots.push_back (std::move (snap));

    // Layers with effects of their own will want racks of their own: have
    // some ready before the first note asks.
    if (fxLayers > 0 || hasSelection())
    {
        engine.requestChains (2 + juce::jmin (fxLayers, Engine::kPolyVoices));
        engine.serviceChains();
    }

    // Keep the plugin's state tree current, so the layers are part of the
    // APVTS state the host saves.
    {
        const juce::ScopedLock sl (lock);
        apvts.state.removeChild (apvts.state.getChildWithName (treeType), nullptr);
        apvts.state.appendChild (toValueTree(), nullptr);
    }

    service();
    sendChangeMessage();
}

void NoteLayerManager::service()
{
    engine.serviceChains();

    // Layouts older than the one the audio thread is using can never be
    // picked up again.
    // (The newest is never below the one in use, so it always survives.)
    const auto inUse = engine.getLayersInUse();
    snapshots.erase (std::remove_if (snapshots.begin(), snapshots.end(),
                                     [inUse] (const std::unique_ptr<NoteLayerSnapshot>& s) { return s->serial < inUse; }),
                     snapshots.end());
}

} // namespace aquavibrio
