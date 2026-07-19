#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Static table definitions ──────────────────────────────────────────────────

// 17 scales.  Unused slots in 5- and 6-note scales are filled with 0
// but will never be accessed because SCALE_SIZES tells us the true size.
const int NoteAdderProcessor::SCALE_INTERVALS[NUM_SCALES][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   //  0  Major (Ionian)
    { 0, 2, 3, 5, 7, 8, 10 },   //  1  Natural Minor (Aeolian)
    { 0, 2, 4, 7, 9, 0,  0  },  //  2  Pentatonic Major  (5)
    { 0, 3, 5, 7, 10, 0, 0  },  //  3  Pentatonic Minor  (5)
    { 0, 2, 4, 6, 7, 9, 11 },   //  4  Lydian
    { 0, 2, 4, 5, 7, 9, 10 },   //  5  Mixolydian
    { 0, 2, 3, 5, 7, 9, 10 },   //  6  Dorian
    { 0, 1, 3, 5, 7, 8, 10 },   //  7  Phrygian
    { 0, 1, 3, 5, 6, 8, 10 },   //  8  Locrian
    { 0, 2, 3, 5, 7, 8, 11 },   //  9  Harmonic Minor
    { 0, 2, 3, 5, 7, 9, 11 },   // 10  Melodic Minor
    { 0, 2, 4, 6, 7, 9, 10 },   // 11  Lydian Dominant
    { 0, 1, 4, 5, 7, 8, 10 },   // 12  Phrygian Dominant
    { 0, 1, 3, 4, 6, 8, 10 },   // 13  Altered (Super Locrian)
    { 0, 2, 4, 6, 8, 10, 0 },   // 14  Whole Tone          (6)
    { 0, 3, 5, 6, 7, 10, 0 },   // 15  Blues               (6)
};

const int NoteAdderProcessor::SCALE_SIZES[NUM_SCALES] = {
    7, 7, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6
};

const char* NoteAdderProcessor::SCALE_NAMES[NUM_SCALES] = {
    "major",           "minor",          "penta major",
    "penta minor",     "lydian",         "mixolydian",
    "dorian",          "phrygian",       "locrian",
    "harmonic minor",  "melodic minor",  "lydian dom.",
    "phrygian dom.",   "altered",        "whole tone",
    "blues"
};

// Beat multipliers: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T
const double NoteAdderProcessor::SUBDIV_FACTORS[9] = {
    4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
    2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0
};

const int NoteAdderProcessor::FREE_RATE_MS[12] = {
    50, 75, 100, 150, 200, 300, 400, 500, 750, 1000, 1500, 2000
};

const int NoteAdderProcessor::VEL_TABLE[10] = {
    8, 16, 24, 32, 48, 64, 80, 96, 112, 127
};

const int NoteAdderProcessor::CLOUD_DECAY_MS[5] = {
    80, 200, 400, 700, 1400
};

// ── Parameter layout (APVTS) ──────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
NoteAdderProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "mode", "Mode", 0, 1, 0));

    for (int i = 0; i < NUM_ROWS; ++i)
    {
        juce::String s(i);
        layout.add(std::make_unique<juce::AudioParameterBool>(
            "enabled" + s, "Slot " + juce::String(i + 1) + " On", false));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            "semi" + s, "Slot " + juce::String(i + 1) + " Semi", -12, 12, 0));
        layout.add(std::make_unique<juce::AudioParameterInt>(
            "oct" + s, "Slot " + juce::String(i + 1) + " Oct", -7, 7, 0));
    }

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "root", "Root Note", 0, 11, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "scale", "Scale Type", 0, NUM_SCALES - 1, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "count", "Note Count", 0, 7, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "harmonicmode", "Harmonic Mode", 0, 5, 1));   // 0=Secundal 1=Tertian 2=Quartal 3=Quintal 4=Septimal 5=Sextal
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "discard", "Discard Non-Scale", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "discardInput", "Discard Input Note", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "randomskip", "Random Skip Notes", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "lockbass", "Lock Bass", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "invmode", "Inversion Mode", 0, 6, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "invpicker", "Inversion Picker", 0, 7, 0));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "animmode", "Anim Mode", 0, 4, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "animsync", "Anim BPM Sync", true));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "animrate", "Anim Rate (Subdiv)", 0, NUM_SUBDIVS - 1, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "animratefree", "Anim Rate (Free ms)", 0, NUM_FREE_RATES - 1, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "wanderprob", "Wander Probability", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "wandermax", "Wander Max Degrees", 0, 6, 2));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "clouddensity", "Cloud Density", 0, 7, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "cloudspread", "Cloud Spread", 0, 6, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "cloudvelmin", "Cloud Vel Min", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "cloudvelmax", "Cloud Vel Max", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "clouddecay", "Cloud Decay", 0, 4, 1));

    return layout;
}

//==============================================================================
NoteAdderProcessor::NoteAdderProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "NoteAdder", createParameterLayout())
{
    // Fetch raw pointers from apvts for efficient audio-thread access
    auto* vts = &apvts;
    modeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("mode"));

    for (int i = 0; i < NUM_ROWS; ++i)
    {
        juce::String s(i);
        enabledParams[i] = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("enabled" + s));
        semiParams[i] = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("semi" + s));
        octaveParams[i] = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("oct" + s));
    }

    rootNoteParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("root"));
    scaleTypeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("scale"));
    noteCountParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("count"));
    harmonicModeParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("harmonicmode"));
    discardParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("discard"));
    discardInputParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("discardInput"));
    randomSkipParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("randomskip"));
    lockBassParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("lockbass"));
    inversionModeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("invmode"));
    inversionPickerParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("invpicker"));

    animModeParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("animmode"));
    animSyncBPMParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("animsync"));
    animRateParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("animrate"));
    animRateFreeParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("animratefree"));
    wanderProbParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("wanderprob"));
    wanderMaxParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("wandermax"));
    cloudDensityParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("clouddensity"));
    cloudSpreadParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("cloudspread"));
    cloudVelMinParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("cloudvelmin"));
    cloudVelMaxParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("cloudvelmax"));
    cloudDecayParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("clouddecay"));
}

NoteAdderProcessor::~NoteAdderProcessor() {}

//==============================================================================
void NoteAdderProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (auto& ch : activeAddedNotes) for (auto& v : ch) v.clear();
    for (auto& ch : discardedNotes)   ch.fill(false);

    animStates.clear();
    cloudNotes.clear();
    lastChordNotes.clear();
    cycleIndex = 0;
    prevAnimMode = animModeParam->get();
}

void NoteAdderProcessor::releaseResources() {}

bool NoteAdderProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::disabled();
}

//==============================================================================
// ── Scale helpers ─────────────────────────────────────────────────────────────

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

int NoteAdderProcessor::nextScaleNoteAbove(int note) const
{
    for (int n = note + 1; n <= 127; ++n)
        if (isNoteInScale(n)) return n;
    return note;
}

int NoteAdderProcessor::nextScaleNoteBelow(int note) const
{
    for (int n = note - 1; n >= 0; --n)
        if (isNoteInScale(n)) return n;
    return note;
}

int NoteAdderProcessor::noteAtScaleDegreeOffset(int baseNote, int degOffset) const
{
    int note = baseNote;
    if (degOffset > 0)
        for (int i = 0; i < degOffset; ++i)  note = nextScaleNoteAbove(note);
    else if (degOffset < 0)
        for (int i = 0; i < -degOffset; ++i) note = nextScaleNoteBelow(note);
    return note;
}

void NoteAdderProcessor::rotateInversion(std::vector<int>& notes, int inv)
{
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
// ── Tick interval ─────────────────────────────────────────────────────────────

double NoteAdderProcessor::computeTickSamples() const
{
    double result = 0.0;

    if (animSyncBPMParam->get())
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
        {
            juce::AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition(pos))
                bpm = juce::jmax(1.0, pos.bpm);
        }
        double spb = currentSampleRate * 60.0 / bpm;
        int idx = juce::jlimit(0, NUM_SUBDIVS - 1, animRateParam->get());
        result = spb * SUBDIV_FACTORS[idx];
    }
    else
    {
        int idx = juce::jlimit(0, NUM_FREE_RATES - 1, animRateFreeParam->get());
        result = currentSampleRate * FREE_RATE_MS[idx] / 1000.0;
    }

    return juce::jmax(result, (double)64);
}

//==============================================================================
// ── Animation tick implementations ───────────────────────────────────────────

void NoteAdderProcessor::tickReVoice(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    if (state.baseNotes.empty()) return;

    const int n = (int)state.baseNotes.size();
    state.invStep = (state.invStep + 1) % (n + 1);

    auto newNotes = state.baseNotes;
    rotateInversion(newNotes, state.invStep);

    for (int note : state.currentNotes)
        out.addEvent(juce::MidiMessage::noteOff(ch + 1, note, (juce::uint8)0), samplePos);
    for (int note : newNotes)
        out.addEvent(juce::MidiMessage::noteOn(ch + 1, note, (juce::uint8)state.velocity), samplePos);

    state.currentNotes = newNotes;
}

void NoteAdderProcessor::tickWander(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    if (modeParam->get() != 1) return;   // scale knowledge required
    if (state.baseNotes.empty()) return;

    const int maxSteps = wanderMaxParam->get() + 1;
    const int probPct = (wanderProbParam->get() + 1) * 10;

    for (int i = 0; i < (int)state.baseNotes.size(); ++i)
    {
        if (rng.nextInt(100) >= probPct) continue;

        int dir = rng.nextInt(3) - 1;
        if (dir == 0) continue;

        int newSteps = juce::jlimit(-maxSteps, maxSteps, state.wanderSteps[i] + dir);
        int newNote = juce::jlimit(0, 127,
            noteAtScaleDegreeOffset(state.baseNotes[i], newSteps));

        if (newNote != state.currentNotes[i])
        {
            out.addEvent(juce::MidiMessage::noteOff(ch + 1, state.currentNotes[i], (juce::uint8)0), samplePos);
            out.addEvent(juce::MidiMessage::noteOn(ch + 1, newNote, (juce::uint8)state.velocity), samplePos);
            state.currentNotes[i] = newNote;
            state.wanderSteps[i] = newSteps;
        }
    }
}

void NoteAdderProcessor::tickDrift(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    if (modeParam->get() != 1) return;

    const int sz = SCALE_SIZES[scaleTypeParam->get()];
    state.driftDegreeShift = (state.driftDegreeShift + 1) % (sz * 2);

    int driftedNote = noteAtScaleDegreeOffset(state.inputNote, state.driftDegreeShift);
    driftedNote = juce::jlimit(0, 127, driftedNote);

    if (!isNoteInScale(driftedNote)) return;

    auto newNotes = computeAddedNotes(driftedNote);

    for (int note : state.currentNotes)
        out.addEvent(juce::MidiMessage::noteOff(ch + 1, note, (juce::uint8)0), samplePos);
    for (int note : newNotes)
        out.addEvent(juce::MidiMessage::noteOn(ch + 1, note, (juce::uint8)state.velocity), samplePos);

    state.currentNotes = newNotes;
}

void NoteAdderProcessor::tickCloud(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    // Cloud works in both modes; note selection differs per mode.
    if ((int)cloudNotes.size() >= MAX_CLOUD_NOTES) return;

    const int density = cloudDensityParam->get() + 1;  // 1 – 8
    const int spreadParam = cloudSpreadParam->get();        // 0 – 6
    const int velMin = VEL_TABLE[cloudVelMinParam->get()];
    const int velMax = VEL_TABLE[cloudVelMaxParam->get()];
    const int decayMs = CLOUD_DECAY_MS[cloudDecayParam->get()];
    const int decaySamples = (int)(currentSampleRate * decayMs / 1000.0);
    const int heldKey = ch * 128 + state.inputNote;
    const int safeMax = juce::jmax(velMin, velMax);
    const int velRange = safeMax - velMin + 1;

    for (int i = 0; i < density; ++i)
    {
        if ((int)cloudNotes.size() >= MAX_CLOUD_NOTES) break;

        int cloudNote = 0;

        if (modeParam->get() == 0)
        {
            // ── Manual mode ──────────────────────────────────────────────────
            // Pick a random note from the manually-configured interval set,
            // then transpose it by a random octave offset.
            // spreadParam 0-6 → octave range 0-3 (every 2 steps = 1 extra octave).
            const auto& pool = state.baseNotes.empty()
                ? std::vector<int>{ state.inputNote }
            : state.baseNotes;

            int baseNote = pool[rng.nextInt((int)pool.size())];
            int octRange = spreadParam / 2;   // 0 – 3 octaves either side
            int octOffset = (octRange > 0) ? (rng.nextInt(octRange * 2 + 1) - octRange) : 0;
            cloudNote = juce::jlimit(0, 127, baseNote + octOffset * 12);
        }
        else
        {
            // ── Scale mode ───────────────────────────────────────────────────
            // Random scale-degree walk around the held note.
            int spread = spreadParam + 1;   // 1 – 7 degrees
            int degOffset = rng.nextInt(spread * 2 + 1) - spread;
            cloudNote = juce::jlimit(0, 127,
                noteAtScaleDegreeOffset(state.inputNote, degOffset));
        }

        int vel = velMin + (velRange > 1 ? rng.nextInt(velRange) : 0);
        vel = juce::jlimit(1, 127, vel);

        int jitterRange = juce::jmax(1, decaySamples / 4);
        int duration = decaySamples - jitterRange / 2 + rng.nextInt(jitterRange);

        out.addEvent(juce::MidiMessage::noteOn(ch + 1, cloudNote, (juce::uint8)vel), samplePos);
        cloudNotes.push_back({ heldKey, ch, cloudNote, duration });
    }
}

//==============================================================================
void NoteAdderProcessor::processCloudNoteOffs(juce::MidiBuffer& out, int numSamples)
{
    for (auto& cn : cloudNotes)
    {
        cn.samplesRemaining -= numSamples;
        if (cn.samplesRemaining <= 0)
            out.addEvent(juce::MidiMessage::noteOff(cn.channel + 1, cn.midiNote, (juce::uint8)0), 0);
    }
    cloudNotes.erase(
        std::remove_if(cloudNotes.begin(), cloudNotes.end(),
            [](const CloudNote& cn) { return cn.samplesRemaining <= 0; }),
        cloudNotes.end());
}

void NoteAdderProcessor::killAnimForNote(int ch, int note,
    juce::MidiBuffer& out, int samplePos)
{
    const int key = ch * 128 + note;

    auto it = animStates.find(key);
    if (it != animStates.end())
    {
        for (int n : it->second.currentNotes)
            out.addEvent(juce::MidiMessage::noteOff(ch + 1, n, (juce::uint8)0), samplePos);
        animStates.erase(it);
    }

    for (auto& cn : cloudNotes)
        if (cn.heldKey == key)
            out.addEvent(juce::MidiMessage::noteOff(cn.channel + 1, cn.midiNote, (juce::uint8)0), samplePos);

    cloudNotes.erase(
        std::remove_if(cloudNotes.begin(), cloudNotes.end(),
            [key](const CloudNote& cn) { return cn.heldKey == key; }),
        cloudNotes.end());
}

void NoteAdderProcessor::killAllAnimation(juce::MidiBuffer& out, int samplePos)
{
    for (auto& [key, state] : animStates)
    {
        const int ch = key / 128;
        for (int n : state.currentNotes)
            out.addEvent(juce::MidiMessage::noteOff(ch + 1, n, (juce::uint8)0), samplePos);
    }
    animStates.clear();

    for (auto& cn : cloudNotes)
        out.addEvent(juce::MidiMessage::noteOff(cn.channel + 1, cn.midiNote, (juce::uint8)0), samplePos);
    cloudNotes.clear();
}

//==============================================================================
// ── computeAddedNotes ─────────────────────────────────────────────────────────

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

    // Harmonic stacking interval:
    //   0=Secundal (step 1)  1=Tertian (step 2)  2=Quartal (step 3)
    //   3=Quintal (step 4)   4=Sextal (step 5)   5=Septimal (step 6)
    const int stepSizes[6] = { 1, 2, 3, 4, 5, 6 };
    const int step = stepSizes[juce::jlimit(0, 5, harmonicModeParam->get())];

    for (int k = 1; k <= count; ++k)
    {
        int targetDeg = degree + step * k;
        int octaveAdd = targetDeg / sz;
        int degInScale = targetDeg % sz;
        int added = noteBase + ivs[degInScale] + octaveAdd * 12;
        if (added >= 0 && added <= 127)
            result.push_back(added);
    }

    // ── Random skip ──────────────────────────────────────────────────────────
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
        case 0: break;
        case 1:
        {
            int inv = juce::jmin(inversionPickerParam->get(), n);
            rotateInversion(result, inv);
            break;
        }
        case 2:
        {
            if (!lastChordNotes.empty())
            {
                int bestInv = 0, bestDist = INT_MAX;
                for (int inv = 0; inv <= n; ++inv)
                {
                    auto trial = result;
                    rotateInversion(trial, inv);
                    int dist = 0, minSz = juce::jmin((int)trial.size(), (int)lastChordNotes.size());
                    for (int j = 0; j < minSz; ++j) dist += std::abs(trial[j] - lastChordNotes[j]);
                    if (dist < bestDist) { bestDist = dist; bestInv = inv; }
                }
                rotateInversion(result, bestInv);
            }
            break;
        }
        case 3:
        {
            if (n >= 2)
            {
                std::sort(result.begin(), result.end());
                int idx = n - 2, dropped = result[idx] - 12;
                if (dropped >= 0 && dropped != note) result[idx] = dropped;
                std::sort(result.begin(), result.end());
            }
            break;
        }
        case 4:
        {
            int inv = cycleIndex % (n + 1);
            ++cycleIndex;
            rotateInversion(result, inv);
            break;
        }
        case 5:
        {
            int inv = n - (cycleIndex % (n + 1));
            ++cycleIndex;
            rotateInversion(result, inv);
            break;
        }
        case 6:
        {
            int inv = rng.nextInt(n + 1);
            rotateInversion(result, inv);
            break;
        }
        default: break;
        }

        lastChordNotes = result;
    }

    // ── Lock bass ────────────────────────────────────────────────────────────
    if (lockBassParam->get())
    {
        for (auto& n : result)
            while (n <= note && n + 12 <= 127) n += 12;

        result.erase(
            std::remove_if(result.begin(), result.end(),
                [note](int n) { return n <= note || n > 127; }),
            result.end());
    }

    return result;
}

//==============================================================================
// ── processBlock ─────────────────────────────────────────────────────────────

void NoteAdderProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();
    juce::MidiBuffer processed;

    const int  numSamples = buffer.getNumSamples();
    const bool scaleMode = (modeParam->get() == 1);
    const bool discardMode = scaleMode && discardParam->get();
    const bool dropInput = discardInputParam->get();
    const int  curAnim = animModeParam->get();
    const bool animOn = (curAnim != 0);

    // Detect animMode change → kill all existing animation
    if (curAnim != prevAnimMode)
    {
        killAllAnimation(processed, 0);
        prevAnimMode = curAnim;
    }

    // ── 1. Process incoming MIDI events ───────────────────────────────────────
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

            if (!dropInput)
                processed.addEvent(msg, pos);

            auto addedNotes = computeAddedNotes(note);

            for (int added : addedNotes)
                processed.addEvent(
                    juce::MidiMessage::noteOn(msg.getChannel(), added, (juce::uint8)vel), pos);

            if (animOn)
            {
                killAnimForNote(ch, note, processed, pos);

                AnimNoteState state;
                state.channel = ch;
                state.velocity = vel;
                state.inputNote = note;
                state.baseNotes = addedNotes;
                state.currentNotes = addedNotes;
                state.wanderSteps.assign(addedNotes.size(), 0);
                state.invStep = 0;
                state.driftDegreeShift = 0;
                state.samplesUntilTick = computeTickSamples();
                animStates[ch * 128 + note] = std::move(state);
            }
            else
            {
                activeAddedNotes[ch][note] = addedNotes;
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

            if (animOn)
                killAnimForNote(ch, note, processed, pos);
            else
            {
                for (int added : activeAddedNotes[ch][note])
                    processed.addEvent(
                        juce::MidiMessage::noteOff(msg.getChannel(), added, (juce::uint8)0), pos);
                activeAddedNotes[ch][note].clear();
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            killAllAnimation(processed, pos);
            for (auto& ch : activeAddedNotes) for (auto& v : ch) v.clear();
            for (auto& ch : discardedNotes)   ch.fill(false);
            processed.addEvent(msg, pos);
        }
        else
        {
            processed.addEvent(msg, pos);
        }
    }

    // ── 2. Advance animation ticks ────────────────────────────────────────────
    if (animOn && !animStates.empty())
    {
        const double tickSamples = computeTickSamples();
        processCloudNoteOffs(processed, numSamples);

        for (auto& [key, state] : animStates)
        {
            state.samplesUntilTick -= numSamples;

            int maxTicks = 32;
            while (state.samplesUntilTick <= 0.0 && maxTicks-- > 0)
            {
                state.samplesUntilTick += tickSamples;
                const int ch = key / 128;

                switch (curAnim)
                {
                case 1: tickReVoice(state, ch, 0, processed); break;
                case 2: tickWander(state, ch, 0, processed); break;
                case 3: tickDrift(state, ch, 0, processed); break;
                case 4: tickCloud(state, ch, 0, processed); break;
                default: break;
                }
            }
        }
    }
    else if (!animOn)
    {
        processCloudNoteOffs(processed, numSamples);
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
// ── State persistence (APVTS XML) ────────────────────────────────────────────

void NoteAdderProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void NoteAdderProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoteAdderProcessor();
}