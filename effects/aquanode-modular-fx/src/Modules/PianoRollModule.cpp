#include "PianoRollModule.h"

using namespace aquanode;

//==============================================================================
// construction / state
//==============================================================================
PianoRollModule::PianoRollModule()
{
    published = std::make_shared<const NoteList>();
    audioNotes = published;
    for (auto& e : activeEnd)
        e = -1.0;
}

PianoRollModule::~PianoRollModule() = default;

const juce::StringArray& PianoRollModule::gridChoices()
{
    static const juce::StringArray choices { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32",
                                             "1/4T", "1/8T", "1/16T" };
    return choices;
}

double PianoRollModule::gridBeats() const
{
    static const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                    2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
    return beats[juce::jlimit (0, 8, (int) std::lround (getParameter ("grid")))];
}

int PianoRollModule::numBars() const
{
    return juce::jlimit (1, kMaxBars, (int) std::lround (getParameter ("bars")));
}

void PianoRollModule::setNotes (NoteList newNotes)
{
    const double maxBeat = kMaxBars * kBeatsPerBar;

    newNotes.erase (std::remove_if (newNotes.begin(), newNotes.end(),
                        [maxBeat] (const PianoRollNote& n) { return n.start >= maxBeat || n.start < -1.0e-9; }),
                    newNotes.end());

    for (auto& n : newNotes)
    {
        n.start = juce::jmax (0.0, n.start);
        n.length = juce::jlimit (1.0 / 64.0, maxBeat - n.start, n.length);
        n.pitch = juce::jlimit (0, 127, n.pitch);
        n.velocity = juce::jlimit (0.01f, 1.0f, n.velocity);
    }

    std::stable_sort (newNotes.begin(), newNotes.end(),
                      [] (const PianoRollNote& a, const PianoRollNote& b)
                      {
                          return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
                      });

    if ((int) newNotes.size() > kMaxNotes)
        newNotes.resize ((size_t) kMaxNotes);

    notes = std::move (newNotes);
    publish();
    editCounter.fetch_add (1, std::memory_order_relaxed);
}

void PianoRollModule::publish()
{
    auto snapshot = std::make_shared<const NoteList> (notes);
    std::shared_ptr<const NoteList> old;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        old = std::move (published);
        published = std::move (snapshot);
    }

    // Free superseded snapshots here on the message thread, never on the
    // audio thread: one is only dropped once nothing but this list holds it
    // (the audio thread can only ever pick up the CURRENT snapshot).
    retired.push_back (std::move (old));
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                       [] (const std::shared_ptr<const NoteList>& p) { return p == nullptr || p.use_count() == 1; }),
                   retired.end());
}

void PianoRollModule::pushUndo()
{
    undoStack.push_back (notes);
    if (undoStack.size() > 64)
        undoStack.erase (undoStack.begin());
}

bool PianoRollModule::undo()
{
    if (undoStack.empty())
        return false;

    auto previous = std::move (undoStack.back());
    undoStack.pop_back();
    setNotes (std::move (previous));
    return true;
}

void PianoRollModule::previewNote (int pitch, float velocity01)
{
    int s1, n1, s2, n2;
    previewFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        previewData[(size_t) s1] = { juce::jlimit (0, 127, pitch), velocity01 };
    else if (n2 > 0)
        previewData[(size_t) s2] = { juce::jlimit (0, 127, pitch), velocity01 };
    previewFifo.finishedWrite (n1 + n2);
}

// "PR1" then one "start:length:pitch:velocity" group per note
juce::String PianoRollModule::saveCustomState() const
{
    if (notes.empty())
        return "PR1";

    juce::String s;
    s.preallocateBytes (notes.size() * 24 + 8);
    s << "PR1";
    for (const auto& n : notes)
        s << ' ' << juce::String (n.start, 5) << ':' << juce::String (n.length, 5)
          << ':' << n.pitch << ':' << juce::String (n.velocity, 3);
    return s;
}

void PianoRollModule::loadCustomState (const juce::String& state)
{
    auto tokens = juce::StringArray::fromTokens (state, " ", {});
    if (tokens.isEmpty() || tokens[0] != "PR1")
        return;

    NoteList loaded;
    for (int i = 1; i < tokens.size(); ++i)
    {
        auto parts = juce::StringArray::fromTokens (tokens[i], ":", {});
        if (parts.size() != 4)
            continue;

        PianoRollNote n;
        n.start = parts[0].getDoubleValue();
        n.length = parts[1].getDoubleValue();
        n.pitch = parts[2].getIntValue();
        n.velocity = (float) parts[3].getDoubleValue();
        loaded.push_back (n);
    }

    undoStack.clear();
    setNotes (std::move (loaded));
}

//==============================================================================
// audio thread
//==============================================================================
void PianoRollModule::prepare (double sr)
{
    SynthModule::prepare (sr);
    reset();
}

void PianoRollModule::reset()
{
    // The engine wipes every voice whenever it calls this, so the notes this
    // module was holding are already gone: forget them without note-offs. The
    // song position survives (deleting some other module should not throw
    // the sequence back to bar one).
    for (auto& e : activeEnd)
        e = -1.0;
    numActive = 0;
    for (auto& p : previewLeft)
        p = 0;
    numPendingPreviews = 0;
    wasPlaying = false;
}

void PianoRollModule::blockStart()
{
    std::shared_ptr<const NoteList> latest;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        latest = published;
    }

    if (latest != audioNotes)
    {
        audioNotes = std::move (latest);   // the old one is still held by `retired`
        listChanged = true;
    }

    // notes asked for by the editor (placing / dragging / clicking a key).
    // Anything not played during the previous block is dropped rather than
    // saved up to fire all at once later.
    numPendingPreviews = 0;
    const int ready = previewFifo.getNumReady();
    if (ready > 0)
    {
        int s1, n1, s2, n2;
        previewFifo.prepareToRead (ready, s1, n1, s2, n2);
        for (int i = 0; i < n1 && numPendingPreviews < (int) pendingPreviews.size(); ++i)
            pendingPreviews[(size_t) numPendingPreviews++] = previewData[(size_t) (s1 + i)];
        for (int i = 0; i < n2 && numPendingPreviews < (int) pendingPreviews.size(); ++i)
            pendingPreviews[(size_t) numPendingPreviews++] = previewData[(size_t) (s2 + i)];
        previewFifo.finishedRead (n1 + n2);
    }
}

size_t PianoRollModule::firstNoteAtOrAfter (double beat) const
{
    if (audioNotes == nullptr)
        return 0;

    const auto& list = *audioNotes;
    auto it = std::lower_bound (list.begin(), list.end(), beat,
                                [] (const PianoRollNote& n, double b) { return n.start < b; });
    return (size_t) std::distance (list.begin(), it);
}

void PianoRollModule::releaseAllActive (MidiDriverEvent* events, int& n, int maxEvents)
{
    while (numActive > 0 && n < maxEvents)
    {
        const int p = activeList[--numActive];
        activeEnd[p] = -1.0;
        events[n++] = { p, 0.0f, false };
    }
}

int PianoRollModule::midiDriverAdvanceEvents (double sr, MidiDriverEvent* events, int maxEvents)
{
    int n = 0;

    // ---- previews: a short note now, released after a quarter second
    if (numPendingPreviews > 0)
    {
        for (int i = 0; i < numPendingPreviews && n + 2 <= maxEvents; ++i)
        {
            const int p = pendingPreviews[(size_t) i].first;
            if (activeEnd[p] >= 0.0)
                continue;   // the sequence is playing that key right now
            if (previewLeft[p] > 0)
                events[n++] = { p, 0.0f, false };
            events[n++] = { p, pendingPreviews[(size_t) i].second, true };
            previewLeft[p] = juce::jmax (1, (int) (0.25 * sr));
        }
        numPendingPreviews = 0;
    }

    for (int p = 0; p < 128; ++p)
    {
        if (previewLeft[p] > 0 && --previewLeft[p] == 0 && activeEnd[p] < 0.0 && n < maxEvents)
            events[n++] = { p, 0.0f, false };
    }

    // ---- tempo: the host's when asked for AND available, else our own
    const bool hostAvailable = hostTransport.hasBpm && hostTransport.bpm > 0.0;
    const bool useHost = param (pSync) > 0.5f && hostAvailable;
    const double bpm = juce::jlimit (10.0, 999.0, useHost ? hostTransport.bpm : (double) param (pTempo));
    uiBpm.store (bpm, std::memory_order_relaxed);
    uiHostTempo.store (hostAvailable, std::memory_order_relaxed);

    const double loopLen = juce::jlimit (1, kMaxBars, (int) std::lround (param (pBars))) * kBeatsPerBar;

    // ---- seek / rewind requested by the UI
    const double seekTo = seekRequest.exchange (-1.0, std::memory_order_acq_rel);
    if (seekTo >= 0.0)
    {
        releaseAllActive (events, n, maxEvents);
        position = seekTo < loopLen ? seekTo : 0.0;
        nextIndex = firstNoteAtOrAfter (position);
        listChanged = false;
    }

    const bool playing = param (pPlaying) > 0.5f;
    if (! playing)
    {
        if (wasPlaying)
            releaseAllActive (events, n, maxEvents);   // pause: let go, keep the position
        wasPlaying = false;
        uiPlayhead.store (position, std::memory_order_relaxed);
        return n;
    }

    if (! wasPlaying || listChanged)
    {
        nextIndex = firstNoteAtOrAfter (position);
        wasPlaying = true;
        listChanged = false;
    }

    // the loop got shorter than where we are: start over
    if (position >= loopLen)
    {
        releaseAllActive (events, n, maxEvents);
        position = 0.0;
        nextIndex = 0;
    }

    const double windowEnd = position + bpm / 60.0 / sr;

    // ---- note-offs that are due
    for (int i = 0; i < numActive && n < maxEvents;)
    {
        const int p = activeList[i];
        if (activeEnd[p] <= position)
        {
            events[n++] = { p, 0.0f, false };
            activeEnd[p] = -1.0;
            activeList[i] = activeList[--numActive];
        }
        else
        {
            ++i;
        }
    }

    // ---- note-ons starting inside this sample
    if (audioNotes != nullptr)
    {
        const auto& list = *audioNotes;
        while (nextIndex < list.size() && list[nextIndex].start < windowEnd)
        {
            const auto& note = list[nextIndex];
            if (note.start >= loopLen)
            {
                nextIndex = list.size();   // everything after lies past the loop end
                break;
            }

            if (n + 2 > maxEvents)
                break;                     // finish on the next sample

            const int p = note.pitch;
            if (activeEnd[p] >= 0.0)
            {
                // same key still down (overlapping notes): re-strike it
                events[n++] = { p, 0.0f, false };
                for (int i = 0; i < numActive; ++i)
                    if (activeList[i] == p)
                    {
                        activeList[i] = activeList[--numActive];
                        break;
                    }
            }
            else if (previewLeft[p] > 0)
            {
                events[n++] = { p, 0.0f, false };
            }
            previewLeft[p] = 0;

            events[n++] = { p, note.velocity, true };
            activeEnd[p] = juce::jmin (note.start + note.length, loopLen);
            activeList[numActive++] = p;
            ++nextIndex;
        }
    }

    position = windowEnd;

    // ---- end of the loop
    if (position >= loopLen)
    {
        releaseAllActive (events, n, maxEvents);   // notes are cut at the loop end
        nextIndex = 0;

        if (param (pLoop) < 0.5f)
        {
            position -= loopLen;                   // keep the fractional overshoot
            if (position >= loopLen)
                position = 0.0;
        }
        else
        {
            position = 0.0;                        // "Once": stop and rewind
            wasPlaying = false;
            setParameterByIndex (pPlaying, 0.0f);   // no string lookup on the audio thread
        }
    }

    uiPlayhead.store (position, std::memory_order_relaxed);
    return n;
}

//==============================================================================
// MIDI files
//==============================================================================
bool PianoRollModule::importMidi (juce::InputStream& in)
{
    juce::MidiFile file;
    if (! file.readFrom (in, true))
        return false;

    double fileBpm = -1.0;
    {
        juce::MidiMessageSequence tempos;
        file.findAllTempoEvents (tempos);
        if (tempos.getNumEvents() > 0)
        {
            const double spq = tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote();
            if (spq > 0.0)
                fileBpm = 60.0 / spq;
        }
    }

    const short timeFormat = file.getTimeFormat();
    const bool smpte = timeFormat <= 0;
    const double bpmForSeconds = fileBpm > 0.0 ? fileBpm : 120.0;
    if (smpte)
        file.convertTimestampTicksToSeconds();

    auto toBeats = [&] (double t)
    {
        return smpte ? t * bpmForSeconds / 60.0 : t / (double) timeFormat;
    };

    NoteList imported;
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*file.getTrack (t));
        seq.updateMatchedPairs();

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            if (! ev->message.isNoteOn())
                continue;

            PianoRollNote note;
            note.start = toBeats (ev->message.getTimeStamp());
            const double end = ev->noteOffObject != nullptr
                                 ? toBeats (ev->noteOffObject->message.getTimeStamp())
                                 : note.start + 1.0;
            note.length = juce::jmax (1.0 / 64.0, end - note.start);
            note.pitch = ev->message.getNoteNumber();
            note.velocity = ev->message.getFloatVelocity();
            imported.push_back (note);
        }
    }

    if (imported.empty())
        return false;

    double lastEnd = 0.0;
    for (const auto& n : imported)
        lastEnd = juce::jmax (lastEnd, n.start + n.length);

    pushUndo();
    setParameter ("bars", (float) juce::jlimit (1, kMaxBars, (int) std::ceil (lastEnd / kBeatsPerBar - 1.0e-6)));
    if (fileBpm > 0.0)
        setParameter ("tempo", (float) juce::jlimit (20.0, 300.0, fileBpm));
    setNotes (std::move (imported));
    return true;
}

bool PianoRollModule::exportMidi (juce::OutputStream& out) const
{
    constexpr int tpq = 960;
    const double bpm = wantsHostTempo() && isHostTempoAvailable() ? getEffectiveBpm()
                                                                   : (double) getParameter ("tempo");

    // note-offs must come before a note-on on the same tick, so sort first
    struct Ev { double tick; bool on; juce::MidiMessage msg; };
    std::vector<Ev> evs;
    double lastTick = 0.0;
    for (const auto& n : notes)
    {
        const double on = std::round (n.start * tpq);
        const double off = std::round ((n.start + n.length) * tpq);
        evs.push_back ({ on, true, juce::MidiMessage::noteOn (1, n.pitch, n.velocity) });
        evs.push_back ({ off, false, juce::MidiMessage::noteOff (1, n.pitch) });
        lastTick = juce::jmax (lastTick, off);
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b)
                      { return a.tick != b.tick ? a.tick < b.tick : (! a.on && b.on); });

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Aquanode Piano Roll"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (10.0, 999.0, bpm))), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    for (auto& e : evs)
        seq.addEvent (e.msg, e.tick);

    lastTick = juce::jmax (lastTick, loopLengthBeats() * tpq);
    seq.addEvent (juce::MidiMessage::endOfTrack(), lastTick);
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (tpq);
    file.addTrack (seq);
    return file.writeTo (out, 1);
}

namespace
{
    void showPianoRollProblem (const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Piano Roll", message);
    }
}

void PianoRollModule::chooseAndImportMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Import MIDI file", juce::File(), "*.mid;*.midi;*.smf");

    juce::WeakReference<SynthModule> weakThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [weakThis] (const juce::FileChooser& fc)
        {
            auto* self = dynamic_cast<PianoRollModule*> (weakThis.get());
            if (self == nullptr)
                return;

            // Read the whole document first: on Android it is a content://
            // stream from the ContentResolver (no filesystem path, not
            // reliably seekable), exactly like the sample loader handles it.
            juce::MemoryBlock data;
           #if JUCE_ANDROID
            const auto url = fc.getURLResult();
            if (url.isEmpty())
                return;
            auto doc = juce::AndroidDocument::fromDocument (url);
            std::unique_ptr<juce::InputStream> in;
            if (doc.hasValue())
                in = doc.createInputStream();
            if (in == nullptr)
                in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
            if (in != nullptr)
                in->readIntoMemoryBlock (data);
           #else
            const auto file = fc.getResult();
            if (! file.existsAsFile())
                return;
            file.loadFileAsData (data);
           #endif

            juce::MemoryInputStream stream (data, false);
            if (data.getSize() == 0 || ! self->importMidi (stream))
                showPianoRollProblem ("That file has no notes this Piano Roll can read.");
        });
}

void PianoRollModule::chooseAndExportMidi()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Export MIDI file",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Piano Roll.mid"),
        "*.mid");

    juce::WeakReference<SynthModule> weakThis (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
        [weakThis] (const juce::FileChooser& fc)
        {
            auto* self = dynamic_cast<PianoRollModule*> (weakThis.get());
            if (self == nullptr)
                return;

            // getURLResult rather than getResult: on Android the SAF "create
            // document" picker hands back a content:// URI that getResult
            // cannot represent (same trap Record works around).
            const auto destination = fc.getURLResult();
            if (destination.isEmpty())
                return;

            juce::MemoryOutputStream encoded;
            if (! self->exportMidi (encoded) || encoded.getDataSize() == 0)
            {
                showPianoRollProblem ("Could not encode the MIDI file.");
                return;
            }

            // Same two-step write as Record on Android: a plain file if the
            // destination is one, otherwise an AndroidDocument stream for the
            // content:// URI the picker gave us.
            std::unique_ptr<juce::OutputStream> out;
            if (destination.isLocalFile())
            {
                auto file = destination.getLocalFile();
                file.deleteFile();
                std::unique_ptr<juce::FileOutputStream> fileStream (file.createOutputStream());
                if (fileStream != nullptr && fileStream->openedOk())
                    out = std::move (fileStream);
            }

            if (out == nullptr)
            {
               #if JUCE_ANDROID
                auto doc = juce::AndroidDocument::fromDocument (destination);
                if (doc.hasValue())
                    out = doc.createOutputStream();
               #else
                out = destination.createOutputStream();
               #endif
            }

            if (out == nullptr)
            {
                showPianoRollProblem ("Could not write to that location.");
                return;
            }

            out->write (encoded.getData(), encoded.getDataSize());
            out->flush();
        });
}

void PianoRollModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "import")      chooseAndImportMidi();
    else if (paramId == "export") chooseAndExportMidi();
}

//==============================================================================
// UI
//==============================================================================
namespace
{
    namespace prc
    {
        const juce::Colour background   (0xff141414);
        const juce::Colour whiteKeyRow  (0xff1f1f1f);
        const juce::Colour blackKeyRow  (0xff181818);
        const juce::Colour note         (0xffd970b0);   // utility pink
        const juce::Colour playhead     (0xff00ffff);   // the MIDI cyan
        const juce::Colour pastEnd      (0xff000000);
    }

    bool isBlackKey (int pitch)
    {
        const int pc = ((pitch % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    juce::String formatBeatPosition (double beats)
    {
        const int bar = (int) (beats / PianoRollModule::kBeatsPerBar) + 1;
        const int beat = (int) std::fmod (beats, PianoRollModule::kBeatsPerBar) + 1;
        return juce::String (bar) + "." + juce::String (beat);
    }

    juce::String tempoStatus (const PianoRollModule& m)
    {
        const auto bpm = juce::String (m.getEffectiveBpm(), 1) + " bpm";
        if (! m.wantsHostTempo())
            return bpm;
        return m.isHostTempoAvailable() ? bpm + " (host)"
                                        : bpm + " (no host tempo)";
    }

    void styleButton (juce::TextButton& b)
    {
        b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff262626));
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd970b0));
        b.setColour (juce::TextButton::textColourOffId, juce::Colours::white.withAlpha (0.85f));
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }
}

//==============================================================================
// The full-size editing grid
//==============================================================================
class PianoRollGrid : public juce::Component
{
public:
    std::function<void (float velocity)> onSelectionChanged;

    explicit PianoRollGrid (PianoRollModule& m) : module (&m)
    {
        setWantsKeyboardFocus (true);
        lastLength = m.gridBeats() < 0.25 ? 0.25 : m.gridBeats();
    }

    PianoRollModule* roll() const { return dynamic_cast<PianoRollModule*> (module.get()); }

    //=== view ===============================================================
    void zoomHorizontally (double factor, float anchorX)
    {
        const double anchorBeat = beatAtX (anchorX);
        pxPerBeat = juce::jlimit (4.0, 480.0, pxPerBeat * factor);
        scrollBeats = anchorBeat - (anchorX - keyW) / pxPerBeat;
        clampView();
        repaint();
    }

    void zoomVertically (double factor)
    {
        const double centrePitch = topPitch - visibleRows() * 0.5;
        rowH = juce::jlimit (6.0, 40.0, rowH * factor);
        topPitch = centrePitch + visibleRows() * 0.5;
        clampView();
        repaint();
    }

    void fitLoop()
    {
        if (auto* m = roll())
        {
            pxPerBeat = juce::jlimit (4.0, 480.0, (getWidth() - keyW - 8) / m->loopLengthBeats());
            scrollBeats = 0.0;

            // and centre on the notes there are (or around middle C)
            int lo = 128, hi = -1;
            for (const auto& n : m->getNotes())
            {
                lo = juce::jmin (lo, n.pitch);
                hi = juce::jmax (hi, n.pitch);
            }
            const double centre = hi >= 0 ? (lo + hi) * 0.5 : 60.0;
            topPitch = centre + visibleRows() * 0.5;
            clampView();
            repaint();
        }
    }

    void setFollowPlayhead (bool shouldFollow) { follow = shouldFollow; }

    // called by the overlay's timer
    void tick()
    {
        auto* m = roll();
        if (m == nullptr)
            return;

        if (follow && m->isPlaying() && dragMode == DragMode::none)
        {
            const double ph = m->getPlayheadBeats();
            const double visible = (getWidth() - keyW) / pxPerBeat;
            if (ph < scrollBeats || ph > scrollBeats + visible * 0.9)
            {
                scrollBeats = ph - visible * 0.05;
                clampView();
            }
        }

        if (m->getEditCounter() != seenEditCounter)
        {
            seenEditCounter = m->getEditCounter();
            if (dragMode == DragMode::none && selected >= (int) m->getNotes().size())
                selected = -1;
        }
        repaint();
    }

    void setSelectedVelocity (float v)
    {
        auto* m = roll();
        if (m == nullptr || selected < 0 || selected >= (int) m->getNotes().size())
            return;

        auto list = m->getNotes();
        list[(size_t) selected].velocity = v;
        m->setNotes (std::move (list));
    }

    void deleteSelected()
    {
        auto* m = roll();
        if (m == nullptr || selected < 0 || selected >= (int) m->getNotes().size())
            return;

        m->pushUndo();
        auto list = m->getNotes();
        list.erase (list.begin() + selected);
        selected = -1;
        m->setNotes (std::move (list));
    }

    //=== painting ============================================================
    void paint (juce::Graphics& g) override
    {
        auto* m = roll();
        g.fillAll (prc::background);
        if (m == nullptr)
            return;

        const auto area = getLocalBounds();
        const int gridTop = rulerH;
        const double loopLen = m->loopLengthBeats();
        const double grid = m->gridBeats();

        // ---- rows
        const int firstPitch = juce::jmax (0, (int) std::floor (topPitch - visibleRows()) - 1);
        const int lastPitch = juce::jmin (127, (int) std::ceil (topPitch));
        for (int p = firstPitch; p <= lastPitch; ++p)
        {
            const float y = (float) yForPitch (p);
            g.setColour (isBlackKey (p) ? prc::blackKeyRow : prc::whiteKeyRow);
            g.fillRect (juce::Rectangle<float> ((float) keyW, y, (float) (area.getWidth() - keyW), (float) rowH));
            g.setColour (juce::Colours::white.withAlpha (p % 12 == 0 ? 0.14f : 0.04f));
            g.drawHorizontalLine ((int) (y + rowH), (float) keyW, (float) area.getWidth());
        }

        // ---- vertical lines: grid steps, beats, bars
        const double viewEnd = beatAtX ((float) area.getWidth());
        auto drawLines = [&] (double step, juce::Colour c)
        {
            if (step * pxPerBeat < 5.0)
                return;
            g.setColour (c);
            for (double b = std::floor (scrollBeats / step) * step; b <= viewEnd; b += step)
            {
                const float x = (float) xForBeat (b);
                if (x >= keyW)
                    g.drawVerticalLine ((int) x, (float) gridTop, (float) area.getHeight());
            }
        };
        drawLines (grid, juce::Colours::white.withAlpha (0.035f));
        drawLines (1.0, juce::Colours::white.withAlpha (0.08f));
        drawLines (PianoRollModule::kBeatsPerBar, juce::Colours::white.withAlpha (0.22f));

        // ---- everything past the loop end is shaded: it does not play
        {
            const float endX = (float) xForBeat (loopLen);
            if (endX < area.getWidth())
            {
                g.setColour (prc::pastEnd.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (juce::jmax ((float) keyW, endX), (float) gridTop,
                                                    (float) area.getWidth(), (float) area.getHeight()));
                g.setColour (prc::playhead.withAlpha (0.5f));
                if (endX >= keyW)
                    g.drawVerticalLine ((int) endX, 0.0f, (float) area.getHeight());
            }
        }

        // ---- notes
        const auto& list = m->getNotes();
        g.setFont (juce::Font (juce::FontOptions (juce::jmin (11.0f, (float) rowH - 2.0f))));
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto r = noteRect (list[(size_t) i]);
            if (r.getRight() < keyW || r.getX() > area.getWidth() || r.getBottom() < gridTop || r.getY() > area.getHeight())
                continue;

            const auto& n = list[(size_t) i];
            const float a = 0.35f + 0.65f * n.velocity;
            g.setColour (prc::note.withMultipliedBrightness (0.55f + 0.45f * a));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (i == selected ? juce::Colours::white : juce::Colours::black.withAlpha (0.6f));
            g.drawRoundedRectangle (r, 2.0f, i == selected ? 1.6f : 1.0f);

            if (r.getWidth() > 28.0f && rowH >= 10.0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.8f));
                g.drawText (midiNoteName (n.pitch), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
            }
        }

        // ---- ruler
        g.setColour (juce::Colour (0xff0e0e0e));
        g.fillRect (0, 0, area.getWidth(), rulerH);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const int barStep = juce::jmax (1, (int) std::ceil (36.0 / (pxPerBeat * PianoRollModule::kBeatsPerBar)));
        for (int bar = (int) (scrollBeats / PianoRollModule::kBeatsPerBar); ; bar += 1)
        {
            const double b = bar * PianoRollModule::kBeatsPerBar;
            const float x = (float) xForBeat (b);
            if (x > area.getWidth())
                break;
            if (x < keyW || bar % barStep != 0)
                continue;
            g.setColour (juce::Colours::white.withAlpha (b < loopLen ? 0.75f : 0.3f));
            g.drawVerticalLine ((int) x, (float) rulerH - 7.0f, (float) rulerH);
            g.drawText (juce::String (bar + 1), (int) x + 3, 0, 40, rulerH, juce::Justification::centredLeft, false);
        }

        // ---- playhead
        {
            const float x = (float) xForBeat (m->getPlayheadBeats());
            if (x >= keyW && x <= area.getWidth())
            {
                g.setColour (prc::playhead.withAlpha (0.9f));
                g.drawLine (x, 0.0f, x, (float) area.getHeight(), 1.5f);
                juce::Path tri;
                tri.addTriangle (x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
                g.fillPath (tri);
            }
        }

        // ---- keyboard strip
        for (int p = firstPitch; p <= lastPitch; ++p)
        {
            const auto key = juce::Rectangle<float> (0.0f, (float) yForPitch (p), (float) keyW, (float) rowH);
            if (key.getBottom() < gridTop)
                continue;
            g.setColour (isBlackKey (p) ? juce::Colour (0xff2a2a2a) : juce::Colour (0xffdedad2));
            g.fillRect (key.reduced (0.0f, 0.5f));
            if (p % 12 == 0 && rowH >= 9.0)
            {
                g.setColour (juce::Colours::black);
                g.setFont (juce::Font (juce::FontOptions (juce::jmin (11.0f, (float) rowH))));
                g.drawText (midiNoteName (p), key.reduced (3.0f, 0.0f), juce::Justification::centredRight, false);
            }
        }
        g.setColour (juce::Colour (0xff0e0e0e));
        g.fillRect (0, 0, keyW, rulerH);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (formatBeatPosition (m->getPlayheadBeats()), 0, 0, keyW, rulerH, juce::Justification::centred, false);

        // ---- first-time hint
        if (list.empty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.drawFittedText ("Tap to add a note  -  drag empty space to slide around\n"
                              "drag a note to move it, its right edge to stretch it\n"
                              "tap twice (or right-click) a note to delete it  -  click the ruler to jump",
                              area.withTrimmedLeft (keyW).withTrimmedTop (rulerH).reduced (20),
                              juce::Justification::centred, 4);
        }
    }

    //=== mouse ===============================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* m = roll();
        if (m == nullptr)
            return;

        grabKeyboardFocus();
        downPos = e.position;
        downScroll = scrollBeats;
        downTop = topPitch;
        moved = false;
        undoPushed = false;

        if (e.position.y < rulerH && e.position.x >= keyW)
        {
            dragMode = DragMode::seek;
            m->seek (snapToGrid (juce::jmax (0.0, beatAtX (e.position.x))));
            return;
        }

        if (e.position.x < keyW)
        {
            dragMode = DragMode::keys;
            lastPreviewPitch = pitchAtY (e.position.y);
            m->previewNote (lastPreviewPitch, newVelocity());
            return;
        }

        const int hit = noteAt (e.position);
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu() || e.getNumberOfClicks() >= 2)
            {
                selected = hit;
                deleteSelected();
                dragMode = DragMode::none;
                return;
            }

            setSelected (hit);
            dragOriginal = m->getNotes()[(size_t) hit];
            const auto r = noteRect (dragOriginal);
            const float edge = juce::jmin (10.0f, r.getWidth() * 0.35f);
            dragMode = e.position.x >= r.getRight() - edge ? DragMode::resize : DragMode::move;
            downBeat = beatAtX (e.position.x);
            downPitch = pitchAtY (e.position.y);
            if (dragMode == DragMode::move)
                m->previewNote (dragOriginal.pitch, dragOriginal.velocity);
            return;
        }

        if (e.mods.isPopupMenu())
        {
            dragMode = DragMode::none;
            return;
        }

        dragMode = DragMode::panOrTap;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* m = roll();
        if (m == nullptr)
            return;

        if (e.position.getDistanceFrom (downPos) > 4.0f)
            moved = true;

        switch (dragMode)
        {
            case DragMode::seek:
                m->seek (snapToGrid (juce::jmax (0.0, beatAtX (e.position.x))));
                break;

            case DragMode::keys:
            {
                const int p = pitchAtY (e.position.y);
                if (p != lastPreviewPitch)
                {
                    lastPreviewPitch = p;
                    m->previewNote (p, newVelocity());
                }
                break;
            }

            case DragMode::panOrTap:
                if (moved)
                {
                    scrollBeats = downScroll - (e.position.x - downPos.x) / pxPerBeat;
                    topPitch = downTop + (e.position.y - downPos.y) / rowH;
                    clampView();
                    repaint();
                }
                break;

            case DragMode::move:
            {
                if (! moved)
                    break;
                auto changed = dragOriginal;
                const double delta = beatAtX (e.position.x) - downBeat;
                changed.start = e.mods.isAltDown() ? juce::jmax (0.0, dragOriginal.start + delta)
                                                   : juce::jmax (0.0, snapToGrid (dragOriginal.start + delta));
                changed.pitch = juce::jlimit (0, 127, dragOriginal.pitch + pitchAtY (e.position.y) - downPitch);
                const bool pitchChanged = changed.pitch != currentDragged().pitch;
                replaceSelected (changed);
                if (pitchChanged)
                    m->previewNote (changed.pitch, changed.velocity);
                break;
            }

            case DragMode::resize:
            {
                auto changed = dragOriginal;
                const double minLen = juce::jmin (m->gridBeats(), 0.25);
                const double end = e.mods.isAltDown() ? beatAtX (e.position.x)
                                                      : snapToGrid (beatAtX (e.position.x) + m->gridBeats() * 0.5);
                changed.length = juce::jmax (minLen, end - changed.start);
                if (std::abs (changed.length - currentDragged().length) > 1.0e-9)
                {
                    replaceSelected (changed);
                    lastLength = changed.length;
                }
                break;
            }

            case DragMode::none:
                break;
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        auto* m = roll();
        if (m != nullptr && dragMode == DragMode::panOrTap && ! moved)
        {
            // a tap on empty grid: new note, snapped, at the last used length
            PianoRollNote n;
            n.start = juce::jmax (0.0, std::floor (beatAtX (e.position.x) / m->gridBeats() + 1.0e-9) * m->gridBeats());
            n.pitch = pitchAtY (e.position.y);
            n.length = lastLength;
            n.velocity = newVelocity();

            if (n.start < PianoRollModule::kMaxBars * PianoRollModule::kBeatsPerBar)
            {
                m->pushUndo();
                auto list = m->getNotes();
                list.push_back (n);
                m->setNotes (std::move (list));
                selectMatching (n);
                m->previewNote (n.pitch, n.velocity);
            }
        }

        dragMode = DragMode::none;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            zoomHorizontally (w.deltaY > 0 ? 1.15 : 1.0 / 1.15, e.position.x);
            return;
        }

        const bool horizontal = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
        if (horizontal)
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            scrollBeats -= d * 240.0 / pxPerBeat;
        }
        else
        {
            topPitch += w.deltaY * 24.0;
        }
        clampView();
        repaint();
    }

    void mouseMagnify (const juce::MouseEvent& e, float scale) override
    {
        zoomHorizontally (scale, e.position.x);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto* m = roll();
        if (m == nullptr)
            return false;

        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            deleteSelected();
            return true;
        }
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        {
            m->undo();
            selected = -1;
            return true;
        }
        if (k == juce::KeyPress::spaceKey)
        {
            m->setPlaying (! m->isPlaying());
            return true;
        }
        return false;
    }

    void resized() override { clampView(); }

private:
    enum class DragMode { none, panOrTap, move, resize, seek, keys };

    double xForBeat (double beat) const { return keyW + (beat - scrollBeats) * pxPerBeat; }
    double beatAtX (float x) const { return scrollBeats + (x - keyW) / pxPerBeat; }
    double yForPitch (int pitch) const { return rulerH + (topPitch - pitch) * rowH; }
    int pitchAtY (float y) const
    {
        return juce::jlimit (0, 127, (int) std::ceil (topPitch - (y - rulerH) / rowH));
    }
    double visibleRows() const { return (getHeight() - rulerH) / rowH; }

    juce::Rectangle<float> noteRect (const PianoRollNote& n) const
    {
        const float x = (float) xForBeat (n.start);
        const float w = juce::jmax (3.0f, (float) (n.length * pxPerBeat) - 1.0f);
        return { x, (float) yForPitch (n.pitch) + 1.0f, w, (float) rowH - 2.0f };
    }

    int noteAt (juce::Point<float> pos) const
    {
        auto* m = roll();
        if (m == nullptr)
            return -1;
        const auto& list = m->getNotes();
        for (int i = (int) list.size() - 1; i >= 0; --i)
            if (noteRect (list[(size_t) i]).expanded (0.0f, 1.0f).contains (pos))
                return i;
        return -1;
    }

    double snapToGrid (double beat) const
    {
        auto* m = roll();
        const double g = m != nullptr ? m->gridBeats() : 0.25;
        return std::round (beat / g) * g;
    }

    float newVelocity() const
    {
        auto* m = roll();
        return m != nullptr ? juce::jlimit (1.0f, 127.0f, m->getParameter ("newVel")) / 127.0f : 0.8f;
    }

    void clampView()
    {
        auto* m = roll();
        const double loopLen = m != nullptr ? m->loopLengthBeats() : 16.0;
        const double visible = juce::jmax (1.0, (getWidth() - keyW) / pxPerBeat);
        scrollBeats = juce::jlimit (0.0, juce::jmax (0.0, loopLen + 4.0 - visible * 0.5), scrollBeats);
        const double rows = visibleRows();
        topPitch = juce::jlimit (juce::jmin (127.0, rows - 1.0), 127.0, topPitch);
    }

    const PianoRollNote& currentDragged() const
    {
        static const PianoRollNote none;
        auto* m = roll();
        if (m == nullptr || selected < 0 || selected >= (int) m->getNotes().size())
            return none;
        return m->getNotes()[(size_t) selected];
    }

    void replaceSelected (const PianoRollNote& changed)
    {
        auto* m = roll();
        if (m == nullptr || selected < 0 || selected >= (int) m->getNotes().size())
            return;

        if (! undoPushed)
        {
            m->pushUndo();
            undoPushed = true;
        }

        auto list = m->getNotes();
        list[(size_t) selected] = changed;
        m->setNotes (std::move (list));
        selectMatching (changed);   // the sort may have moved it
        repaint();
    }

    void selectMatching (const PianoRollNote& n)
    {
        auto* m = roll();
        if (m == nullptr)
            return;
        const auto& list = m->getNotes();
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].pitch == n.pitch && std::abs (list[(size_t) i].start - n.start) < 1.0e-6
                && std::abs (list[(size_t) i].length - n.length) < 1.0e-6)
            {
                setSelected (i);
                return;
            }
    }

    void setSelected (int index)
    {
        selected = index;
        auto* m = roll();
        if (m != nullptr && index >= 0 && index < (int) m->getNotes().size() && onSelectionChanged)
            onSelectionChanged (m->getNotes()[(size_t) index].velocity);
    }

    juce::WeakReference<SynthModule> module;

    static constexpr int keyW = 46;
    static constexpr int rulerH = 22;
    double pxPerBeat { 48.0 };
    double rowH { 14.0 };
    double scrollBeats { 0.0 };
    double topPitch { 84.0 };
    bool follow { false };

    DragMode dragMode { DragMode::none };
    juce::Point<float> downPos;
    double downScroll { 0.0 }, downTop { 0.0 }, downBeat { 0.0 };
    int downPitch { 60 };
    bool moved { false }, undoPushed { false };
    PianoRollNote dragOriginal;
    int selected { -1 };
    int lastPreviewPitch { -1 };
    double lastLength { 0.5 };
    int seenEditCounter { -1 };
};

//==============================================================================
// The panel living inside the module card: toolbar + the grid + a resize grip
//==============================================================================
class PianoRollPanel : public juce::Component,
                       public aquanode::ExtraContentParamListener,
                       private juce::Timer
{
public:
    explicit PianoRollPanel (PianoRollModule& m) : module (&m), grid (m)
    {
        for (auto* b : { &playButton, &rewindButton, &hostButton, &loopButton, &undoButton, &fitButton,
                         &zoomOutButton, &zoomInButton, &rowsOutButton, &rowsInButton, &followButton,
                         &importButton, &exportButton })
        {
            styleButton (*b);
            addAndMakeVisible (*b);
        }

        hostButton.setClickingTogglesState (true);
        loopButton.setClickingTogglesState (true);
        followButton.setClickingTogglesState (true);

        playButton.setTooltip ("Play / Pause (Space)");
        hostButton.setTooltip ("Lock to the DAW's tempo (keeps its own tempo when there is no host)");
        loopButton.setTooltip ("Loop the bars, or play them once and stop");
        fitButton.setTooltip ("Fit the whole loop into view");
        followButton.setTooltip ("Scroll along with the playhead");
        zoomOutButton.setTooltip ("Zoom out in time");
        zoomInButton.setTooltip ("Zoom in in time");
        rowsOutButton.setTooltip ("Smaller note rows");
        rowsInButton.setTooltip ("Taller note rows");

        playButton.onClick = [this] { if (auto* r = roll()) r->setPlaying (! r->isPlaying()); refreshButtons(); };
        rewindButton.onClick = [this] { if (auto* r = roll()) r->rewind(); };
        hostButton.onClick = [this]
        {
            if (auto* r = roll())
                r->setParameter ("sync", hostButton.getToggleState() ? 1.0f : 0.0f);
        };
        loopButton.onClick = [this]
        {
            if (auto* r = roll())
                r->setParameter ("loop", loopButton.getToggleState() ? 1.0f : 0.0f);
            refreshButtons();
        };
        undoButton.onClick = [this] { if (auto* r = roll()) r->undo(); };
        fitButton.onClick = [this] { grid.fitLoop(); };
        zoomOutButton.onClick = [this] { grid.zoomHorizontally (1.0 / 1.4, (float) grid.getWidth() * 0.5f); };
        zoomInButton.onClick = [this] { grid.zoomHorizontally (1.4, (float) grid.getWidth() * 0.5f); };
        rowsOutButton.onClick = [this] { grid.zoomVertically (1.0 / 1.25); };
        rowsInButton.onClick = [this] { grid.zoomVertically (1.25); };
        followButton.onClick = [this] { grid.setFollowPlayhead (followButton.getToggleState()); };
        importButton.onClick = [this] { if (auto* r = roll()) r->chooseAndImportMidi(); };
        exportButton.onClick = [this] { if (auto* r = roll()) r->chooseAndExportMidi(); };

        auto setupBar = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
        {
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 22);
            s.setRange (lo, hi, step);
            s.setTextValueSuffix (suffix);
            s.setColour (juce::Slider::trackColourId, juce::Colour (0xffd970b0).withAlpha (0.55f));
            s.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff262626));
            s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (s);
        };
        setupBar (tempoSlider, 20.0, 300.0, 1.0, " bpm");
        setupBar (barsSlider, 1.0, PianoRollModule::kMaxBars, 1.0, " bars");
        setupBar (velSlider, 1.0, 127.0, 1.0, " vel");
        barsSlider.setSkewFactorFromMidPoint (16.0);
        tempoSlider.setDoubleClickReturnValue (true, 120.0);

        tempoSlider.onValueChange = [this]
        {
            if (auto* r = roll())
                r->setParameter ("tempo", (float) tempoSlider.getValue());
        };
        barsSlider.onValueChange = [this]
        {
            if (auto* r = roll())
                r->setParameter ("bars", (float) barsSlider.getValue());
        };
        velSlider.onValueChange = [this]
        {
            if (auto* r = roll())
                r->setParameter ("newVel", (float) velSlider.getValue());
            if (! settingVelFromSelection)
                grid.setSelectedVelocity ((float) velSlider.getValue() / 127.0f);
        };
        grid.onSelectionChanged = [this] (float v)
        {
            const juce::ScopedValueSetter<bool> svs (settingVelFromSelection, true);
            velSlider.setValue (juce::jlimit (1.0, 127.0, (double) std::round (v * 127.0f)), juce::sendNotificationSync);
        };

        gridCombo.addItemList (PianoRollModule::gridChoices(), 1);
        gridCombo.onChange = [this]
        {
            if (auto* r = roll())
                r->setParameter ("grid", (float) gridCombo.getSelectedItemIndex());
        };
        addAndMakeVisible (gridCombo);

        statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        statusLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (statusLabel);

        addAndMakeVisible (grid);
        addAndMakeVisible (grip);
        grip.onResize = [this] (juce::Point<int> panelPos) { resizeTo (panelPos); };

        timerCallback();   // show the real state straight away, not after the first tick
        startTimerHz (30);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // the toolbar wraps onto as many rows as the card's width needs
        struct Item { juce::Component* c; int w; };
        const Item items[] = {
            { &playButton, 54 }, { &rewindButton, 28 }, { &tempoSlider, 84 }, { &hostButton, 44 },
            { &barsSlider, 70 }, { &loopButton, 48 }, { &gridCombo, 64 }, { &velSlider, 66 },
            { &undoButton, 44 }, { &fitButton, 32 }, { &zoomOutButton, 30 }, { &zoomInButton, 30 },
            { &rowsOutButton, 30 }, { &rowsInButton, 30 }, { &followButton, 50 },
            { &importButton, 50 }, { &exportButton, 50 }, { &statusLabel, 170 }
        };

        constexpr int rowH = 24, gap = 3;
        area = area.reduced (4);   // inset inside the dark toolbar strip
        int x = area.getX(), y = area.getY();
        const int right = area.getRight();
        for (const auto& it : items)
        {
            const bool flexible = it.c == &statusLabel;   // shrinks into what is left
            const int w = flexible ? juce::jmax (80, juce::jmin (it.w, right - x)) : it.w;
            if (x + w > right && x > area.getX())
            {
                x = area.getX();
                y += rowH + gap;
            }
            it.c->setBounds (x, y, flexible ? juce::jmin (w, right - x) : w, rowH);
            x += w + gap;
        }

        toolbarBottom = y + rowH + 4;
        grid.setBounds (getLocalBounds().withTop (toolbarBottom + 3));
        grip.setBounds (getWidth() - 16, getHeight() - 16, 16, 16);
        grip.toFront (false);

        if (! fittedOnce && grid.getWidth() > 100)
        {
            fittedOnce = true;
            grid.fitLoop();
        }
    }

    void paint (juce::Graphics& g) override
    {
        // the toolbar sits on a dark strip, like the grid below it
        g.setColour (prc::background);
        g.fillRoundedRectangle (getLocalBounds().withHeight (toolbarBottom).toFloat(), 4.0f);
    }

private:
    // drag handle in the bottom-right corner: resizes the whole module card
    struct ResizeGrip : public juce::Component
    {
        std::function<void (juce::Point<int>)> onResize;

        ResizeGrip() { setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor); }

        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            for (int i = 0; i < 3; ++i)
            {
                const float o = 4.0f + 4.0f * (float) i;
                g.drawLine ((float) getWidth() - o, (float) getHeight() - 2.0f,
                            (float) getWidth() - 2.0f, (float) getHeight() - o, 1.3f);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            // keep the grab point under the finger rather than snapping the corner to it
            if (auto* p = getParentComponent())
                grabOffset = getBounds().getBottomRight() - e.getEventRelativeTo (p).getPosition();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // measured in the panel's coordinates: the panel's top-left never
            // moves while the card grows, so this stays stable under drag and
            // under the canvas zoom
            if (auto* p = getParentComponent())
                if (onResize)
                    onResize (e.getEventRelativeTo (p).getPosition() + grabOffset);
        }

        juce::Point<int> grabOffset;
    };

    PianoRollModule* roll() const { return dynamic_cast<PianoRollModule*> (module.get()); }

    void resizeTo (juce::Point<int> bottomRight)
    {
        auto* r = roll();
        if (r == nullptr)
            return;

        // the card is the panel plus the module's side padding (10 px each side)
        const int newW = juce::jlimit (PianoRollModule::kMinWidth, PianoRollModule::kMaxWidth, bottomRight.x + 20);
        const int newH = juce::jlimit (PianoRollModule::kMinHeight, PianoRollModule::kMaxHeight, bottomRight.y);
        if (newW == r->preferredModuleWidth() && newH == r->extraContentHeight())
            return;

        r->setParameter ("viewW", (float) newW);
        r->setParameter ("viewH", (float) newH);
        if (paramsChangedByContent)
            paramsChangedByContent();   // the editor re-measures the card
    }

    void syncControlsFromModule()
    {
        auto* r = roll();
        if (r == nullptr)
            return;

        if (! tempoSlider.isMouseButtonDown())
            tempoSlider.setValue (r->getParameter ("tempo"), juce::dontSendNotification);
        if (! barsSlider.isMouseButtonDown())
            barsSlider.setValue (r->numBars(), juce::dontSendNotification);
        if (! velSlider.isMouseButtonDown() && ! settingVelFromSelection)
            velSlider.setValue (r->getParameter ("newVel"), juce::dontSendNotification);
        gridCombo.setSelectedItemIndex ((int) std::lround (r->getParameter ("grid")), juce::dontSendNotification);
        hostButton.setToggleState (r->wantsHostTempo(), juce::dontSendNotification);
        loopButton.setToggleState (r->getParameter ("loop") > 0.5f, juce::dontSendNotification);
        refreshButtons();
    }

    void refreshButtons()
    {
        auto* r = roll();
        if (r == nullptr)
            return;
        playButton.setButtonText (r->isPlaying() ? "Pause" : "Play");
        playButton.setToggleState (r->isPlaying(), juce::dontSendNotification);
        loopButton.setButtonText (r->getParameter ("loop") > 0.5f ? "Once" : "Loop");
        undoButton.setEnabled (r->canUndo());
        tempoSlider.setEnabled (! (r->wantsHostTempo() && r->isHostTempoAvailable()));
    }

    void timerCallback() override
    {
        auto* r = roll();
        if (r == nullptr)
            return;

        syncControlsFromModule();
        statusLabel.setText (formatBeatPosition (r->getPlayheadBeats()) + "   " + tempoStatus (*r),
                             juce::dontSendNotification);
        grid.tick();
    }

    juce::WeakReference<SynthModule> module;
    PianoRollGrid grid;
    ResizeGrip grip;

    juce::TextButton playButton { "Play" }, rewindButton { "|<" }, hostButton { "Host" }, loopButton { "Loop" },
                     undoButton { "Undo" }, fitButton { "Fit" }, zoomOutButton { "H-" }, zoomInButton { "H+" },
                     rowsOutButton { "V-" }, rowsInButton { "V+" }, followButton { "Follow" },
                     importButton { "Import" }, exportButton { "Export" };
    juce::Slider tempoSlider, barsSlider, velSlider;
    juce::ComboBox gridCombo;
    juce::Label statusLabel;
    bool settingVelFromSelection { false };
    bool fittedOnce { false };
    int toolbarBottom { 0 };
};

std::unique_ptr<juce::Component> PianoRollModule::createExtraContentComponent()
{
    return std::make_unique<PianoRollPanel> (*this);
}

//==============================================================================
static ModuleDescriptor pianoRollDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "util.pianoroll";
    d.displayName = "Piano Roll";
    d.description =
        "A looping note sequencer you draw into, right in the card: time on x axis, pitch on y; drag the "
        "corner grip to make it bigger. Import/Export to read and write .mid files. Its Midi Out feeds any "
        "Midi In - generators, the drums, a Midi Add - and like the Arp it replaces the played "
        "keys there. Host locks to the DAW tempo, and keeps its own tempo when there is none.";
    d.section = ModuleSection::Utility;
    d.sidebarOrder = 25;
    d.sockets = {
        midiOut ("midiOut", "Midi Out")
    };
    d.params = {
        // all owned by the panel inside the card, saved with the patch
        makeRotary ("tempo",   "Tempo",    20.0f, 300.0f, 120.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("bars",    "Bars",     1.0f, (float) PianoRollModule::kMaxBars, 4.0f, 9, {}, true, 1.0f).hide().noMod(),
        makeCombo  ("sync",    "Clock",    { "Own Tempo", "Host Tempo" }, 0, 9, 3).hide().noMod(),
        makeCombo  ("loop",    "Mode",     { "Loop", "Once" }, 0, 9, 2).hide().noMod(),
        makeRotary ("playing", "Playing",  0.0f, 1.0f, 0.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("grid",    "Grid",     0.0f, 8.0f, 3.0f, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("newVel",  "Velocity", 1.0f, 127.0f, 100.0f, 9, {}, false, 1.0f).hide().noMod(),

        // the card's size (drag the grip in its bottom-right corner)
        makeRotary ("viewW", "Width",  (float) PianoRollModule::kMinWidth,  (float) PianoRollModule::kMaxWidth,
                    (float) PianoRollModule::kDefaultWidth, 9, {}, false, 1.0f).hide().noMod(),
        makeRotary ("viewH", "Height", (float) PianoRollModule::kMinHeight, (float) PianoRollModule::kMaxHeight,
                    (float) PianoRollModule::kDefaultHeight, 9, {}, false, 1.0f).hide().noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (PianoRollModule, pianoRollDescriptor)
