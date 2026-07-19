#include "PluginProcessor.h"
#include "PluginEditor.h"

const int NoteAdderProcessor::SCALE_INTERVALS[NUM_SCALES][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // Major
    { 0, 2, 3, 5, 7, 8, 10 },   // Natural Minor
    { 0, 2, 4, 7, 9, 0,  0  },  // Pentatonic Major
    { 0, 3, 5, 7, 10, 0, 0  },  // Pentatonic Minor
    { 0, 2, 4, 6, 7, 9, 11 },   // Lydian
    { 0, 2, 4, 5, 7, 9, 10 },   // Mixolydian
    { 0, 2, 3, 5, 7, 9, 10 },   // Dorian
    { 0, 2, 3, 5, 7, 8, 11 },   // Harmonic Minor
    { 0, 2, 3, 5, 7, 9, 11 },   // Melodic Minor
};
const int NoteAdderProcessor::SCALE_SIZES[NUM_SCALES] = { 7, 7, 5, 5, 7, 7, 7, 7, 7 };

//==============================================================================
NoteAdderProcessor::NoteAdderProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter(modeParam = new juce::AudioParameterInt(
        "mode", "Mode", 0, 1, 0));

    for (int i = 0; i < NUM_ROWS; ++i)
    {
        juce::String s(i);
        addParameter(enabledParams[i] = new juce::AudioParameterBool(
            "enabled" + s, "Slot " + juce::String(i + 1) + " On", false));
        addParameter(semiParams[i] = new juce::AudioParameterInt(
            "semi" + s, "Slot " + juce::String(i + 1) + " Semi", -12, 12, 0));
        addParameter(octaveParams[i] = new juce::AudioParameterInt(
            "oct" + s, "Slot " + juce::String(i + 1) + " Oct", -7, 7, 0));
    }

    addParameter(rootNoteParam = new juce::AudioParameterInt(
        "root", "Root Note", 0, 11, 0));
    addParameter(scaleTypeParam = new juce::AudioParameterInt(
        "scale", "Scale Type", 0, NUM_SCALES - 1, 0));
    addParameter(noteCountParam = new juce::AudioParameterInt(
        "count", "Note Count", 0, 7, 3));
    addParameter(discardParam = new juce::AudioParameterBool(
        "discard", "Discard Non-Scale", false));
    addParameter(discardInputParam = new juce::AudioParameterBool(
        "discardInput", "Discard Input Note", false));
    addParameter(randomSkipParam = new juce::AudioParameterBool(
        "randomskip", "Random Skip Notes", false));
    addParameter(lockBassParam = new juce::AudioParameterBool(
        "lockbass", "Lock Bass", false));
    addParameter(inversionModeParam = new juce::AudioParameterInt(
        "invmode", "Inversion Mode", 0, 4, 0));
    addParameter(inversionPickerParam = new juce::AudioParameterInt(
        "invpicker", "Inversion Picker", 0, 7, 0));
}

NoteAdderProcessor::~NoteAdderProcessor() {}

//==============================================================================
void NoteAdderProcessor::prepareToPlay(double, int)
{
    for (auto& ch : activeAddedNotes)
        for (auto& v : ch)
            v.clear();
    for (auto& ch : discardedNotes)
        ch.fill(false);
    lastChordNotes.clear();
    cycleIndex = 0;
}

void NoteAdderProcessor::releaseResources() {}

bool NoteAdderProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::disabled();
}

//==============================================================================
bool NoteAdderProcessor::isNoteInScale(int note) const
{
    const int type = scaleTypeParam->get();
    const int root = rootNoteParam->get();
    const int sz = SCALE_SIZES[type];
    const int* ivs = SCALE_INTERVALS[type];
    const int pc = ((note % 12) - root + 12) % 12;
    for (int i = 0; i < sz; ++i)
        if (ivs[i] == pc) return true;
    return false;
}

void NoteAdderProcessor::rotateInversion(std::vector<int>& notes, int inv)
{
    // Rotate `inv` times: lowest note jumps up an octave
    for (int r = 0; r < inv; ++r)
    {
        if (notes.empty()) break;
        std::sort(notes.begin(), notes.end());
        int shifted = notes[0] + 12;
        notes.erase(notes.begin());
        if (shifted <= 127)
            notes.push_back(shifted);
    }
    std::sort(notes.begin(), notes.end());
}

//==============================================================================
std::vector<int> NoteAdderProcessor::computeAddedNotes(int note)
{
    std::vector<int> result;

    // ── MANUAL ──────────────────────────────────────────────────────────────
    if (modeParam->get() == 0)
    {
        for (int i = 0; i < NUM_ROWS; ++i)
        {
            if (!enabledParams[i]->get()) continue;
            int added = note + semiParams[i]->get() + octaveParams[i]->get() * 12;
            if (added >= 0 && added <= 127)
                result.push_back(added);
        }
        return result;
    }

    // ── SCALE ────────────────────────────────────────────────────────────────
    const int type = scaleTypeParam->get();
    const int root = rootNoteParam->get();
    const int count = noteCountParam->get();
    const int sz = SCALE_SIZES[type];
    const int* ivs = SCALE_INTERVALS[type];

    const int pc = ((note % 12) - root + 12) % 12;
    int degree = -1;
    for (int i = 0; i < sz; ++i)
        if (ivs[i] == pc) { degree = i; break; }

    if (degree < 0) return result;   // not in scale

    const int noteBase = note - pc;

    for (int k = 1; k <= count; ++k)
    {
        int targetDeg = degree + 2 * k;
        int octaveAdd = targetDeg / sz;
        int degInScale = targetDeg % sz;
        int added = noteBase + ivs[degInScale] + octaveAdd * 12;
        if (added >= 0 && added <= 127)
            result.push_back(added);
    }

    // ── Random skip: always keep first, 65% chance on rest ──────────────────
    if (randomSkipParam->get() && result.size() > 1)
    {
        std::vector<int> kept;
        kept.push_back(result[0]);
        for (int i = 1; i < (int)result.size(); ++i)
            if (rng.nextFloat() > 0.35f)
                kept.push_back(result[i]);
        result = kept;
    }

    // ── Inversion mode ───────────────────────────────────────────────────────
    if (!result.empty())
    {
        std::sort(result.begin(), result.end());
        const int n = (int)result.size();
        const int invMode = inversionModeParam->get();

        switch (invMode)
        {
        case 0:   // Normal — nothing to do
            break;

        case 1:   // Picker — fixed inversion index chosen by user
        {
            int inv = juce::jmin(inversionPickerParam->get(), n);
            rotateInversion(result, inv);
            break;
        }

        case 2:   // Voice Leader — pick inversion closest to last chord
        {
            if (!lastChordNotes.empty())
            {
                int bestInv = 0;
                int bestDist = INT_MAX;

                for (int inv = 0; inv <= n; ++inv)
                {
                    auto trial = result;
                    rotateInversion(trial, inv);

                    int dist = 0;
                    int minSz = juce::jmin((int)trial.size(),
                        (int)lastChordNotes.size());
                    for (int j = 0; j < minSz; ++j)
                        dist += std::abs(trial[j] - lastChordNotes[j]);

                    if (dist < bestDist) { bestDist = dist; bestInv = inv; }
                }

                rotateInversion(result, bestInv);
            }
            break;
        }

        case 3:   // Drop 2 — second-highest note drops an octave
        {
            if (n >= 2)
            {
                std::sort(result.begin(), result.end());
                int idx = n - 2;
                int dropped = result[idx] - 12;
                if (dropped >= 0 && dropped != note)
                    result[idx] = dropped;
                std::sort(result.begin(), result.end());
            }
            break;
        }

        case 4:   // Cycle — steps through inversions on each new note
        {
            int inv = cycleIndex % (n + 1);
            ++cycleIndex;
            rotateInversion(result, inv);
            break;
        }

        default: break;
        }

        // Store for next voice-leading comparison
        lastChordNotes = result;
    }

    // ── Lock bass: push all added notes strictly above the played note ───────
    if (lockBassParam->get())
    {
        for (auto& n : result)
            while (n <= note && n + 12 <= 127)
                n += 12;

        result.erase(
            std::remove_if(result.begin(), result.end(),
                [note](int n) { return n <= note || n > 127; }),
            result.end());
    }

    return result;
}

//==============================================================================
void NoteAdderProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();
    juce::MidiBuffer processed;

    const bool scaleMode = (modeParam->get() == 1);
    const bool discardMode = scaleMode && discardParam->get();
    const bool dropInput = discardInputParam->get();

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int  pos = meta.samplePosition;

        if (msg.isNoteOn())
        {
            const int ch = msg.getChannel() - 1;
            const int note = msg.getNoteNumber();
            const int vel = msg.getVelocity();

            if (discardMode && !isNoteInScale(note))
            {
                discardedNotes[ch][note] = true;
                activeAddedNotes[ch][note].clear();
                continue;
            }

            discardedNotes[ch][note] = false;
            activeAddedNotes[ch][note].clear();

            if (!dropInput)
                processed.addEvent(msg, pos);

            for (int added : computeAddedNotes(note))
            {
                processed.addEvent(
                    juce::MidiMessage::noteOn(msg.getChannel(), added,
                        (juce::uint8)vel), pos);
                activeAddedNotes[ch][note].push_back(added);
            }
        }
        else if (msg.isNoteOff())
        {
            const int ch = msg.getChannel() - 1;
            const int note = msg.getNoteNumber();

            if (discardedNotes[ch][note])
            {
                discardedNotes[ch][note] = false;
                continue;
            }

            if (!dropInput)
                processed.addEvent(msg, pos);

            for (int added : activeAddedNotes[ch][note])
                processed.addEvent(
                    juce::MidiMessage::noteOff(msg.getChannel(), added,
                        (juce::uint8)0), pos);
            activeAddedNotes[ch][note].clear();
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& ch : activeAddedNotes) for (auto& v : ch) v.clear();
            for (auto& ch : discardedNotes)   ch.fill(false);
            processed.addEvent(msg, pos);
        }
        else
        {
            processed.addEvent(msg, pos);
        }
    }

    midi.swapWith(processed);
}

//==============================================================================
const juce::String NoteAdderProcessor::getName() const { return JucePlugin_Name; }

juce::AudioProcessorEditor* NoteAdderProcessor::createEditor()
{
    return new NoteAdderEditor(*this);
}

//==============================================================================
void NoteAdderProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream s(dest, true);
    s.writeInt(modeParam->get());
    for (int i = 0; i < NUM_ROWS; ++i)
    {
        s.writeBool(enabledParams[i]->get());
        s.writeInt(semiParams[i]->get());
        s.writeInt(octaveParams[i]->get());
    }
    s.writeInt(rootNoteParam->get());
    s.writeInt(scaleTypeParam->get());
    s.writeInt(noteCountParam->get());
    s.writeBool(discardParam->get());
    s.writeBool(discardInputParam->get());
    s.writeBool(randomSkipParam->get());
    s.writeBool(lockBassParam->get());
    s.writeInt(inversionModeParam->get());
    s.writeInt(inversionPickerParam->get());
}

void NoteAdderProcessor::setStateInformation(const void* data, int size)
{
    juce::MemoryInputStream s(data, (size_t)size, false);
    if (!s.isExhausted()) *modeParam = s.readInt();
    for (int i = 0; i < NUM_ROWS; ++i)
    {
        if (s.isExhausted()) break;
        *enabledParams[i] = s.readBool();
        *semiParams[i] = s.readInt();
        *octaveParams[i] = s.readInt();
    }
    if (!s.isExhausted()) *rootNoteParam = s.readInt();
    if (!s.isExhausted()) *scaleTypeParam = s.readInt();
    if (!s.isExhausted()) *noteCountParam = s.readInt();
    if (!s.isExhausted()) *discardParam = s.readBool();
    if (!s.isExhausted()) *discardInputParam = s.readBool();
    if (!s.isExhausted()) *randomSkipParam = s.readBool();
    if (!s.isExhausted()) *lockBassParam = s.readBool();
    if (!s.isExhausted()) *inversionModeParam = s.readInt();
    if (!s.isExhausted()) *inversionPickerParam = s.readInt();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoteAdderProcessor();
}