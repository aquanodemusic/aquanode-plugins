#include "PluginProcessor.h"
#include "PluginEditor.h"

#if defined(_MSC_VER)
#include <intrin.h>   // _BitScanForward64
#pragma intrinsic(_BitScanForward64)
#endif

// ── Static table definitions ──────────────────────────────────────────────────

// 16 scales.  Unused slots in 5- and 6-note scales are filled with 0
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
    { 0, 1, 3, 5, 7, 9, 11 },   // 16  Neapolitan Major
    { 0, 1, 3, 5, 7, 8, 11 },   // 17  Neapolitan Minor
};

const int NoteAdderProcessor::SCALE_SIZES[NUM_SCALES] = {
    7, 7, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 7, 7
};

const char* NoteAdderProcessor::SCALE_NAMES[NUM_SCALES] = {
    "major",           "minor",          "penta major",
    "penta minor",     "lydian",         "mixolydian",
    "dorian",          "phrygian",       "locrian",
    "harmonic minor",  "melodic minor",  "lydian dom.",
    "phrygian dom.",   "altered",        "whole tone",
    "blues",           "neapolitan maj.","neapolitan min."
};

// Beat multipliers: 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T
const double NoteAdderProcessor::SUBDIV_FACTORS[9] = {
    4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
    2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0
};

const int NoteAdderProcessor::VEL_TABLE[10] = {
    8, 16, 24, 32, 48, 64, 80, 96, 112, 127
};

// ── Parameter layout (APVTS) ──────────────────────────────────────────────────

juce::AudioProcessorValueTreeState::ParameterLayout
NoteAdderProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "mode", "Mode", 0, 2, 0));  // 0=Manual 1=Scale 2=PerNote

    // ── Global humanize controls ──────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "humanizetime", "Humanize Time (ms)", 0, 200, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "humanizevel", "Humanize Velocity", 0, 100, 0));

    // ── Global semitone transpose ─────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "globaltranspose", "Global Transpose (st)", -12, 12, 0));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "scaletranspose", "Scale Transpose (steps)", -12, 12, 0));

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
        "harmonicmode", "Harmonic Mode", 0, 5, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "harmonicmode2", "Harmonic Mode 2", 0, 6, 0));  // 0=None, 1-6 = 2nds-7ths
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "discard", "Note Filter", 0, 2, 0));  // 0=Allow all 1=Discard 2=Play nearest
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
        "animmode", "Anim Mode", 0, 5, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "animsync", "Anim BPM Sync", true));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "animrate", "Anim Rate (Subdiv)", 0, NUM_SUBDIVS - 1, 3));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "animratefree", "Anim Rate (Free ms)",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.4f), 200.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "animuptovar", "Anim Up-To Variation", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "wanderprob", "Wander Probability", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "wandermax", "Wander Max Degrees", 0, 6, 2));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "clouddensity", "Cloud Density", 0, 7, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "cloudspread", "Cloud Spread", 0, 13, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "cloudvelmin", "Cloud Vel Min", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "cloudvelmax", "Cloud Vel Max", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "clouddecay", "Cloud Decay (ms)",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f), 200.0f));

    // ── Manual-mode animation (independent from scale-mode animation) ──
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "manimmode", "Manual Anim Mode", 0, 2, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "manimsync", "Manual Anim BPM Sync", true));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "manimrate", "Manual Anim Rate (Subdiv)", 0, NUM_SUBDIVS - 1, 3));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "manimratefree", "Manual Anim Rate (Free ms)",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.4f), 200.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "manimuptovar", "Manual Anim Up-To Variation", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "mclouddensity", "Manual Cloud Density", 0, 7, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "mcloudoctave", "Manual Cloud Octave Range", 0, 3, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "mcloudvelmin", "Manual Cloud Vel Min", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "mcloudvelmax", "Manual Cloud Vel Max", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mclouddecay", "Manual Cloud Decay (ms)",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f), 200.0f));

    // ── PerNote chord-type per pitch class (0=Maj…7=Custom) ───────────────────
    static const char* PC_NAMES[12] = { "C","Cs","D","Ds","E","F","Fs","G","Gs","A","As","B" };
    for (int i = 0; i < 12; ++i)
        layout.add(std::make_unique<juce::AudioParameterInt>(
            "pnchord" + juce::String(i),
            juce::String(PC_NAMES[i]) + " Chord Type", 0, 14, 0));

    // ── PerNote animation (independent from scale / manual) ────────────────────
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "pnanimmode", "PerNote Anim Mode", 0, 3, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "pnanimsync", "PerNote Anim BPM Sync", true));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "pnanimrate", "PerNote Anim Rate (Subdiv)", 0, NUM_SUBDIVS - 1, 3));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pnanimratefree", "PerNote Anim Rate (Free ms)",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.4f), 200.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "pnanimuptovar", "PerNote Anim Up-To Variation", false));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "pnclouddensity", "PerNote Cloud Density", 0, 7, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "pncloudoctave", "PerNote Cloud Octave Range", 0, 3, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "pncloudvelmin", "PerNote Cloud Vel Min", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "pncloudvelmax", "PerNote Cloud Vel Max", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "pnclouddecay", "PerNote Cloud Decay (ms)",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.4f), 200.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "cloudmuteinput", "Cloud Mute Input", false));

    // ── Scale Cluster params ──────────────────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterInt>("clustlownotes", "Cluster Low Notes", 1, 12, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>("clusthighnotes", "Cluster High Notes", 1, 12, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>("clustlowostrt", "Cluster Low Oct Start", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>("clustlowoend", "Cluster Low Oct End", 0, 9, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>("clusthighostrt", "Cluster High Oct Start", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>("clusthighoend", "Cluster High Oct End", 0, 9, 6));
    layout.add(std::make_unique<juce::AudioParameterInt>("clustvelmin", "Cluster Vel Min", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>("clustvelmax", "Cluster Vel Max", 0, 9, 6));
    layout.add(std::make_unique<juce::AudioParameterFloat>("clustdecay", "Cluster Decay (ms)",
        juce::NormalisableRange<float>(20.0f, 5000.0f, 1.0f, 0.35f), 2000.0f));

    // ── PerNote Cluster params ────────────────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclustlownotes", "PN Cluster Low Notes", 1, 12, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclusthighnotes", "PN Cluster High Notes", 1, 12, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclustlowostrt", "PN Cluster Low Oct Start", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclustlowoend", "PN Cluster Low Oct End", 0, 9, 3));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclusthighostrt", "PN Cluster High Oct Start", 0, 9, 4));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclusthighoend", "PN Cluster High Oct End", 0, 9, 6));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclustvelmin", "PN Cluster Vel Min", 0, 9, 1));
    layout.add(std::make_unique<juce::AudioParameterInt>("pnclustvelmax", "PN Cluster Vel Max", 0, 9, 6));
    layout.add(std::make_unique<juce::AudioParameterFloat>("pnclustdecay", "PN Cluster Decay (ms)",
        juce::NormalisableRange<float>(20.0f, 5000.0f, 1.0f, 0.35f), 2000.0f));


    static const int NOTE_COLOR_DEFAULTS[12] = {
        16725815,  // C   (255, 55, 55)  red
        16746255,  // C#  (255,135, 15)  orange
        15127040,  // D   (230,210,  0)  amber
         7921940,  // D#  (120,225, 20)  lime
         2019920,  // E   ( 30,210, 80)  green
           51375,  // F   (  0,200,175)  teal
           42495,  // F#  (  0,165,255)  sky blue
         1660415,  // G   ( 25, 85,255)  blue
         6890495,  // G#  (105, 35,255)  indigo
        11796735,  // A   (180,  0,255)  violet
        15073435,  // A#  (230,  0,155)  magenta
        16711755,  // B   (255,  0, 75)  hot pink
    };
    static const char* NOTE_COLOR_NAMES[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    for (int i = 0; i < 12; ++i)
        layout.add(std::make_unique<juce::AudioParameterInt>(
            "notecolor" + juce::String(i),
            juce::String(NOTE_COLOR_NAMES[i]) + " Colour",
            0, 16777215, NOTE_COLOR_DEFAULTS[i]));

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
    humanizeTimeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("humanizetime"));
    humanizeVelParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("humanizevel"));
    globalTransposeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("globaltranspose"));
    scaleTransposeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("scaletranspose"));

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
    harmonicMode2Param = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("harmonicmode2"));
    discardParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("discard"));
    discardInputParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("discardInput"));
    randomSkipParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("randomskip"));
    lockBassParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("lockbass"));
    inversionModeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("invmode"));
    inversionPickerParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("invpicker"));

    animModeParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("animmode"));
    animSyncBPMParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("animsync"));
    animRateParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("animrate"));
    animRateFreeParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("animratefree"));
    animUpToParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("animuptovar"));
    wanderProbParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("wanderprob"));
    wanderMaxParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("wandermax"));
    cloudDensityParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("clouddensity"));
    cloudSpreadParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("cloudspread"));
    cloudVelMinParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("cloudvelmin"));
    cloudVelMaxParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("cloudvelmax"));
    cloudDecayParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("clouddecay"));

    manualAnimModeParam = dynamic_cast<juce::AudioParameterInt*> (vts->getParameter("manimmode"));
    manualAnimSyncBPMParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("manimsync"));
    manualAnimRateParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("manimrate"));
    manualAnimRateFreeParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("manimratefree"));
    manualAnimUpToParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("manimuptovar"));
    manualCloudDensityParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("mclouddensity"));
    manualCloudOctaveParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("mcloudoctave"));
    manualCloudVelMinParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("mcloudvelmin"));
    manualCloudVelMaxParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("mcloudvelmax"));
    manualCloudDecayParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("mclouddecay"));

    for (int i = 0; i < 12; ++i)
        noteColorParams[i] = dynamic_cast<juce::AudioParameterInt*>(
            vts->getParameter("notecolor" + juce::String(i)));

    // PerNote chord types
    for (int i = 0; i < 12; ++i)
        pnChordTypeParams[i] = dynamic_cast<juce::AudioParameterInt*>(
            vts->getParameter("pnchord" + juce::String(i)));

    // PerNote animation
    pnAnimModeParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("pnanimmode"));
    pnAnimSyncBPMParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("pnanimsync"));
    pnAnimRateParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("pnanimrate"));
    pnAnimRateFreeParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("pnanimratefree"));
    pnAnimUpToParam = dynamic_cast<juce::AudioParameterBool*>(vts->getParameter("pnanimuptovar"));
    pnCloudDensityParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("pnclouddensity"));
    pnCloudOctaveParam = dynamic_cast<juce::AudioParameterInt*>(vts->getParameter("pncloudoctave"));
    pnCloudVelMinParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pncloudvelmin"));
    pnCloudVelMaxParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pncloudvelmax"));
    pnCloudDecayParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("pnclouddecay"));

    cloudMuteInputParam = dynamic_cast<juce::AudioParameterBool*>(
        vts->getParameter("cloudmuteinput"));

    clusterLowNotesParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clustlownotes"));
    clusterHighNotesParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clusthighnotes"));
    clusterLowOctStartParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clustlowostrt"));
    clusterLowOctEndParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clustlowoend"));
    clusterHighOctStartParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clusthighostrt"));
    clusterHighOctEndParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clusthighoend"));
    clusterVelMinParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clustvelmin"));
    clusterVelMaxParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("clustvelmax"));
    clusterDecayParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("clustdecay"));

    pnClusterLowNotesParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclustlownotes"));
    pnClusterHighNotesParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclusthighnotes"));
    pnClusterLowOctStartParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclustlowostrt"));
    pnClusterLowOctEndParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclustlowoend"));
    pnClusterHighOctStartParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclusthighostrt"));
    pnClusterHighOctEndParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclusthighoend"));
    pnClusterVelMinParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclustvelmin"));
    pnClusterVelMaxParam = dynamic_cast<juce::AudioParameterInt*>  (vts->getParameter("pnclustvelmax"));
    pnClusterDecayParam = dynamic_cast<juce::AudioParameterFloat*>(vts->getParameter("pnclustdecay"));

    // Initialise custom-offset strings to empty
    pnCustomStrings.fill({});
}

NoteAdderProcessor::~NoteAdderProcessor() {}

//==============================================================================
void NoteAdderProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (auto& ch : activeAddedNotes)   for (auto& v : ch) v.clear();
    for (auto& ch : discardedNotes)     ch.fill(false);
    for (auto& ch : reroutedNoteMap)    ch.fill(-1);
    for (auto& ch : heldInputVelocity)  ch.fill(-1);
    prevTranspose = globalTransposeParam->get();

    animStates.clear();
    cloudNotes.clear();
    pendingHumanizedNotes.clear();
    lastChordNotes.clear();
    cycleIndex = 0;
    const int m = modeParam->get();
    prevAnimMode = (m == 1 ? animModeParam : (m == 2 ? pnAnimModeParam : manualAnimModeParam))->get();
    pianoRollAllOff();
    prevGuiLow = 0;
    prevGuiHigh = 0;
    std::fill(std::begin(auditPhases), std::end(auditPhases), 0.0);
    std::fill(std::begin(auditRampLevel), std::end(auditRampLevel), 0.0);
    std::fill(std::begin(auditRampingDown), std::end(auditRampingDown), false);
    std::fill(std::begin(auditVelocity), std::end(auditVelocity), 0.0);
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

//==============================================================================
// ── Piano-roll bitmask helpers (audio thread only) ───────────────────────────

void NoteAdderProcessor::pianoRollNoteOn(int note, int velocity) noexcept
{
    if (note < 0 || note > 127) return;
    auditVelocity[note] = velocity / 127.0;
    if (note < 64)
        pianoRollNotesLow.fetch_or(uint64_t(1) << note, std::memory_order_relaxed);
    else
        pianoRollNotesHigh.fetch_or(uint64_t(1) << (note - 64), std::memory_order_relaxed);
}

void NoteAdderProcessor::pianoRollNoteOff(int note) noexcept
{
    if (note < 0 || note > 127) return;
    if (note < 64)
        pianoRollNotesLow.fetch_and(~(uint64_t(1) << note), std::memory_order_relaxed);
    else
        pianoRollNotesHigh.fetch_and(~(uint64_t(1) << (note - 64)), std::memory_order_relaxed);
}

void NoteAdderProcessor::pianoRollAllOff() noexcept
{
    pianoRollNotesLow.store(0, std::memory_order_relaxed);
    pianoRollNotesHigh.store(0, std::memory_order_relaxed);
}

// Portable count-trailing-zeros for uint64 (works on MSVC, GCC, Clang)
static inline int ctz64(uint64_t v) noexcept
{
#if defined(_MSC_VER)
    unsigned long idx = 0;
    _BitScanForward64(&idx, v);
    return static_cast<int>(idx);
#else
    return __builtin_ctzll(v);
#endif
}

// Injects note-on/off events for keys clicked in the piano-roll GUI into the
// *input* MIDI buffer so they flow through computeAddedNotes like real MIDI.
void NoteAdderProcessor::injectGuiNotes(juce::MidiBuffer& input) noexcept
{
    static constexpr int GUI_VELOCITY = 80;
    static constexpr int GUI_CHANNEL = 1;

    const uint64_t curLow = guiNotesLow.load(std::memory_order_acquire);
    const uint64_t curHigh = guiNotesHigh.load(std::memory_order_acquire);

    // Bits newly set → note-on; bits newly cleared → note-off
    uint64_t onLow = curLow & ~prevGuiLow;
    uint64_t offLow = prevGuiLow & ~curLow;
    uint64_t onHigh = curHigh & ~prevGuiHigh;
    uint64_t offHigh = prevGuiHigh & ~curHigh;

    auto emitLow = [&](uint64_t mask, bool noteOn)
        {
            while (mask)
            {
                int bit = ctz64(mask);
                mask &= mask - 1;
                auto msg = noteOn
                    ? juce::MidiMessage::noteOn(GUI_CHANNEL, bit, (juce::uint8)GUI_VELOCITY)
                    : juce::MidiMessage::noteOff(GUI_CHANNEL, bit, (juce::uint8)0);
                input.addEvent(msg, 0);
            }
        };
    auto emitHigh = [&](uint64_t mask, bool noteOn)
        {
            while (mask)
            {
                int bit = ctz64(mask) + 64;
                mask &= mask - 1;
                auto msg = noteOn
                    ? juce::MidiMessage::noteOn(GUI_CHANNEL, bit, (juce::uint8)GUI_VELOCITY)
                    : juce::MidiMessage::noteOff(GUI_CHANNEL, bit, (juce::uint8)0);
                input.addEvent(msg, 0);
            }
        };

    emitLow(onLow, true);
    emitLow(offLow, false);
    emitHigh(onHigh, true);
    emitHigh(offHigh, false);

    prevGuiLow = curLow;
    prevGuiHigh = curHigh;
}

// Called once per processBlock, scanning the outgoing buffer to keep the
// bitmask in sync with what is actually sent to the DAW.
void NoteAdderProcessor::updatePianoRollFromBuffer(const juce::MidiBuffer& buf) noexcept
{
    for (const auto meta : buf)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn())
            pianoRollNoteOn(msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            pianoRollNoteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            pianoRollAllOff();
    }
}

//==============================================================================
// ── Audit tone synthesis (sine waves for all active output notes) ─────────────

void NoteAdderProcessor::renderAuditTones(juce::AudioBuffer<float>& buffer,
    int numSamples) noexcept
{
    if (!auditEnabled.load(std::memory_order_relaxed))
    {
        // Only zero the oscillator state on the falling edge (just turned off).
        // Running three 128-element fills every block while disabled wastes
        // real-time budget for no benefit.
        if (auditWasEnabled)
        {
            std::fill(std::begin(auditRampLevel), std::end(auditRampLevel), 0.0);
            std::fill(std::begin(auditRampingDown), std::end(auditRampingDown), false);
            std::fill(std::begin(auditPhases), std::end(auditPhases), 0.0);
            std::fill(std::begin(auditVelocity), std::end(auditVelocity), 0.0);
            auditWasEnabled = false;
        }
        return;
    }
    auditWasEnabled = true;

    const uint64_t lo = pianoRollNotesLow.load(std::memory_order_relaxed);
    const uint64_t hi = pianoRollNotesHigh.load(std::memory_order_relaxed);

    // Fixed per-note gain — no voice-count normalisation.
    // Normalising by voice count causes gain jumps (= clicks) whenever a note
    // turns on or off, because the count changes discretely between blocks.
    // A fixed level keeps every transition smooth; the ramp envelopes handle
    // everything else.
    static constexpr float gain = 0.14f;
    const int   nCh = buffer.getNumChannels();

    // 5 ms linear ramp-up per note: increment per sample
    const double rampInc = 1.0 / (0.005 * currentSampleRate);

    for (int note = 0; note < 128; ++note)
    {
        const bool active = note < 64 ? ((lo >> note) & 1ULL) != 0
            : ((hi >> (note - 64)) & 1ULL) != 0;
        if (!active)
        {
            if (auditRampLevel[note] <= 0.0)
            {
                // Already silent — nothing to do
                auditRampingDown[note] = false;
                auditPhases[note] = 0.0;
                continue;
            }
            // Note just released — start (or continue) ramp-down
            auditRampingDown[note] = true;
        }
        else
        {
            auditRampingDown[note] = false;
        }

        const double freq = 440.0 * std::pow(2.0, (note - 69) / 12.0);
        const double inc = juce::MathConstants<double>::twoPi * freq / currentSampleRate;

        for (int s = 0; s < numSamples; ++s)
        {
            if (auditRampingDown[note])
            {
                auditRampLevel[note] -= rampInc;
                if (auditRampLevel[note] <= 0.0)
                {
                    auditRampLevel[note] = 0.0;
                    auditRampingDown[note] = false;
                    auditPhases[note] = 0.0;
                    break;   // silent from here — skip remaining samples
                }
            }
            else
            {
                auditRampLevel[note] += rampInc;
                if (auditRampLevel[note] > 1.0) auditRampLevel[note] = 1.0;
            }

            const float samp = gain
                * (float)auditVelocity[note]
                * (float)auditRampLevel[note]
                * (float)std::sin(auditPhases[note]);
            for (int c = 0; c < nCh; ++c)
                buffer.getWritePointer(c)[s] += samp;
            auditPhases[note] += inc;
        }
        // Prevent phase drift / float precision loss
        if (auditPhases[note] != 0.0)
            auditPhases[note] = std::fmod(auditPhases[note],
                juce::MathConstants<double>::twoPi);
    }
}

//==============================================================================

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

// Finds the nearest in-scale note to `note`.
// At equal distance above and below, the lower note wins.
int NoteAdderProcessor::nearestScaleNote(int note) const
{
    if (isNoteInScale(note)) return note;
    for (int dist = 1; dist <= 127; ++dist)
    {
        const int below = note - dist;
        const int above = note + dist;
        const bool hasBelow = (below >= 0) && isNoteInScale(below);
        const bool hasAbove = (above <= 127) && isNoteInScale(above);
        if (hasBelow && hasAbove) return below;   // tie → prefer lower
        if (hasBelow) return below;
        if (hasAbove) return above;
    }
    return note;   // should never reach here with a valid scale
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
// ── PerNote chord helpers ─────────────────────────────────────────────────────

std::vector<int> NoteAdderProcessor::parseCustomOffsets(const juce::String& s)
{
    std::vector<int> result;
    if (s.isEmpty()) return result;
    juce::StringArray tokens;
    tokens.addTokens(s, ",", "");
    for (const auto& t : tokens)
    {
        const juce::String trimmed = t.trim();
        if (trimmed.isNotEmpty())
        {
            const int v = trimmed.getIntValue();
            result.push_back(v);
        }
    }
    return result;
}

// Returns a pointer to a static offset array and the count.
// chordType 0-6; for 7 (Custom) returns nullptr/0 – caller handles it.
const int* NoteAdderProcessor::getChordOffsets(int chordType, int& count) noexcept
{
    // offsets relative to root (the played note); root itself is NOT included
    static const int maj[] = { 4, 7 };
    static const int maj7[] = { 4, 7, 11 };
    static const int maj9[] = { 4, 7, 11, 14 };
    static const int min[] = { 3, 7 };
    static const int min7[] = { 3, 7, 10 };
    static const int min9[] = { 3, 7, 10, 14 };
    static const int s69[] = { 4, 7, 9, 14 };   // 6/9
    static const int dom7[] = { 4, 7, 10 };       // Dominant 7th
    static const int sus2[] = { 2, 7 };           // Suspended 2nd
    static const int sus4[] = { 5, 7 };           // Suspended 4th
    static const int power[] = { 7 };              // Power chord (5th only)
    static const int aug[] = { 4, 8 };           // Augmented
    static const int dim[] = { 3, 6 };           // Diminished

    switch (chordType)
    {
    case 0:  count = 2; return maj;
    case 1:  count = 3; return maj7;
    case 2:  count = 4; return maj9;
    case 3:  count = 2; return min;
    case 4:  count = 3; return min7;
    case 5:  count = 4; return min9;
    case 6:  count = 4; return s69;
    case 7:  count = 3; return dom7;
    case 8:  count = 2; return sus2;
    case 9:  count = 2; return sus4;
    case 10: count = 1; return power;
    case 11: count = 2; return aug;
    case 12: count = 2; return dim;
    default: count = 0; return nullptr;
    }
}



double NoteAdderProcessor::computeTickSamples() const
{
    const int mode = modeParam->get();
    const bool isScale = (mode == 1);
    const bool isPerNote = (mode == 2);

    auto* syncP = isScale ? animSyncBPMParam
        : (isPerNote ? pnAnimSyncBPMParam : manualAnimSyncBPMParam);
    auto* rateP = isScale ? animRateParam
        : (isPerNote ? pnAnimRateParam : manualAnimRateParam);
    auto* rateFreeP = isScale ? animRateFreeParam
        : (isPerNote ? pnAnimRateFreeParam : manualAnimRateFreeParam);

    double result = 0.0;

    if (syncP->get())
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
        {
            juce::AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition(pos))
                bpm = juce::jmax(1.0, pos.bpm);
        }
        double spb = currentSampleRate * 60.0 / bpm;
        int idx = juce::jlimit(0, NUM_SUBDIVS - 1, rateP->get());
        result = spb * SUBDIV_FACTORS[idx];
    }
    else
    {
        const double ms = juce::jmax(1.0f, rateFreeP->get());
        result = currentSampleRate * ms / 1000.0;
    }

    // Determine the effective animation mode (normalised to internal codes)
    int curAnim = 0;
    if (isScale)     curAnim = animModeParam->get();
    else if (isPerNote) { const int m = pnAnimModeParam->get();     curAnim = (m == 2) ? 4 : (m == 3) ? 5 : m; }
    else { const int m = manualAnimModeParam->get();    curAnim = (m == 2) ? 4 : m; }

    const double minSamples = (curAnim >= 1 && curAnim <= 3)
        ? (double)juce::jmax(currentBlockSize + 1, 64)
        : (double)64;

    return juce::jmax(result, minSamples);
}

// ── Randomised tick duration for "up to" mode ──────────────────────────────────
// BPM-sync:  picks uniformly (by weight) from subdivisions >= selected length.
//            Weight = selectedFactor / candidateFactor, so twice-as-long = half probability.
// Free-ms:   duration = base * (1 + 3·u²), u∈[0,1) → ranges base … 4×base, quadratic bias.
double NoteAdderProcessor::computeVarTickSamples() noexcept
{
    const double baseSamples = computeTickSamples();

    const int  mode = modeParam->get();
    const bool isScale = (mode == 1);
    const bool isPerNote = (mode == 2);

    auto* syncP = isScale ? animSyncBPMParam : (isPerNote ? pnAnimSyncBPMParam : manualAnimSyncBPMParam);
    auto* rateP = isScale ? animRateParam : (isPerNote ? pnAnimRateParam : manualAnimRateParam);
    auto* rateFreeP = isScale ? animRateFreeParam : (isPerNote ? pnAnimRateFreeParam : manualAnimRateFreeParam);

    const double minSamples = (double)juce::jmax(currentBlockSize + 1, 64);

    if (syncP->get())   // ── BPM-sync mode ───────────────────────────────────────
    {
        const int    selIdx = juce::jlimit(0, NUM_SUBDIVS - 1, rateP->get());
        const double selFactor = SUBDIV_FACTORS[selIdx];

        // Build weight table: only subdivisions that are at least as long as selected.
        double weights[NUM_SUBDIVS] = {};
        double totalWeight = 0.0;
        for (int i = 0; i < NUM_SUBDIVS; ++i)
        {
            if (SUBDIV_FACTORS[i] >= selFactor - 1e-9)
            {
                weights[i] = selFactor / SUBDIV_FACTORS[i]; // 1.0 for selected, <1.0 for longer
                totalWeight += weights[i];
            }
        }

        if (totalWeight <= 0.0) return baseSamples;

        // Weighted random pick
        double pick = rng.nextDouble() * totalWeight;
        for (int i = 0; i < NUM_SUBDIVS; ++i)
        {
            if (weights[i] > 0.0)
            {
                pick -= weights[i];
                if (pick <= 0.0)
                {
                    // Scale baseSamples by the ratio of the chosen subdivision to selected.
                    // baseSamples already corresponds to selFactor, so multiply by ratio.
                    const double ratio = SUBDIV_FACTORS[i] / selFactor;
                    return juce::jmax(baseSamples * ratio, minSamples);
                }
            }
        }
        return baseSamples; // fallback (floating-point edge case)
    }
    else                // ── Free-ms mode ────────────────────────────────────────
    {
        // Quadratic distribution: shorter intervals much more likely than longer ones.
        // baseSamples already encodes base_ms * sampleRate / 1000.
        const float u = rng.nextFloat();             // [0, 1)
        const double scale = 1.0 + 3.0 * u * u;    // [1, 4), quadratic
        return juce::jmax(baseSamples * scale, minSamples);
    }
}



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

    const int humanTimeMs = humanizeTimeParam->get();
    const int humanVelSpread = humanizeVelParam->get();
    const int maxTimeSamples = humanTimeMs > 0
        ? juce::jmax(1, (int)(currentSampleRate * humanTimeMs / 1000.0)) : 0;

    for (int note : newNotes)
    {
        const int tOff = maxTimeSamples > 0 ? rng.nextInt(maxTimeSamples) : 0;
        const int vOff = humanVelSpread > 0 ? rng.nextInt(humanVelSpread * 2 + 1) - humanVelSpread : 0;
        const int vel = juce::jlimit(1, 127, state.velocity + vOff);
        const int onPos = juce::jmin(samplePos + tOff, currentBlockSize - 1);
        out.addEvent(juce::MidiMessage::noteOn(ch + 1, note, (juce::uint8)vel), onPos);
    }

    state.currentNotes = newNotes;
}

void NoteAdderProcessor::tickWander(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    if (modeParam->get() != 1) return;   // scale knowledge required; guard covers manual & perNote
    if (state.baseNotes.empty()) return;

    const int maxSteps = wanderMaxParam->get() + 1;
    const int probPct = (wanderProbParam->get() + 1) * 10;

    const int humanTimeMs = humanizeTimeParam->get();
    const int humanVelSpread = humanizeVelParam->get();
    const int maxTimeSamples = humanTimeMs > 0
        ? juce::jmax(1, (int)(currentSampleRate * humanTimeMs / 1000.0)) : 0;

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
            const int tOff = maxTimeSamples > 0 ? rng.nextInt(maxTimeSamples) : 0;
            const int vOff = humanVelSpread > 0 ? rng.nextInt(humanVelSpread * 2 + 1) - humanVelSpread : 0;
            const int vel = juce::jlimit(1, 127, state.velocity + vOff);

            out.addEvent(juce::MidiMessage::noteOff(ch + 1, state.currentNotes[i], (juce::uint8)0), samplePos);
            const int onPos = juce::jmin(samplePos + tOff, currentBlockSize - 1);
            out.addEvent(juce::MidiMessage::noteOn(ch + 1, newNote, (juce::uint8)vel), onPos);
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

    const int humanTimeMs = humanizeTimeParam->get();
    const int humanVelSpread = humanizeVelParam->get();
    const int maxTimeSamples = humanTimeMs > 0
        ? juce::jmax(1, (int)(currentSampleRate * humanTimeMs / 1000.0)) : 0;

    for (int note : newNotes)
    {
        const int tOff = maxTimeSamples > 0 ? rng.nextInt(maxTimeSamples) : 0;
        const int vOff = humanVelSpread > 0 ? rng.nextInt(humanVelSpread * 2 + 1) - humanVelSpread : 0;
        const int vel = juce::jlimit(1, 127, state.velocity + vOff);
        const int onPos = juce::jmin(samplePos + tOff, currentBlockSize - 1);
        out.addEvent(juce::MidiMessage::noteOn(ch + 1, note, (juce::uint8)vel), onPos);
    }

    state.currentNotes = newNotes;
}

void NoteAdderProcessor::tickCloud(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    // Cloud works in all modes; note selection and params differ per mode.
    if ((int)cloudNotes.size() >= MAX_CLOUD_NOTES) return;

    const int mode = modeParam->get();
    const bool isScale = (mode == 1);
    const bool isPerNote = (mode == 2);

    const int density = (isScale ? cloudDensityParam
        : (isPerNote ? pnCloudDensityParam : manualCloudDensityParam))->get() + 1;
    const int scaleSpread = cloudSpreadParam->get();
    const int manOctRange = (isPerNote ? pnCloudOctaveParam : manualCloudOctaveParam)->get();
    const int velMin = VEL_TABLE[(isScale ? cloudVelMinParam
        : (isPerNote ? pnCloudVelMinParam : manualCloudVelMinParam))->get()];
    const int velMax = VEL_TABLE[(isScale ? cloudVelMaxParam
        : (isPerNote ? pnCloudVelMaxParam : manualCloudVelMaxParam))->get()];
    const int decayMs = juce::roundToInt((isScale ? cloudDecayParam
        : (isPerNote ? pnCloudDecayParam : manualCloudDecayParam))->get());
    const int decaySamples = (int)(currentSampleRate * decayMs / 1000.0);
    const int heldKey = ch * 128 + state.inputNote;
    const int safeMax = juce::jmax(velMin, velMax);
    const int velRange = safeMax - velMin + 1;

    for (int i = 0; i < density; ++i)
    {
        if ((int)cloudNotes.size() >= MAX_CLOUD_NOTES) break;

        int cloudNote = 0;

        if (!isScale)
        {
            // ── Manual and PerNote modes ──────────────────────────────────────
            // Pick from the per-note chord pool + random octave offset
            const auto& pool = state.baseNotes.empty()
                ? std::vector<int>{ state.inputNote }
            : state.baseNotes;

            int baseNote = pool[rng.nextInt((int)pool.size())];
            int octOffset = (manOctRange > 0) ? (rng.nextInt(manOctRange * 2 + 1) - manOctRange) : 0;
            cloudNote = juce::jlimit(0, 127, baseNote + octOffset * 12);
        }
        else
        {
            // ── Scale mode ───────────────────────────────────────────────────
            // Random scale-degree walk around the held note.
            int spread = scaleSpread + 1;   // 1 – 14 degrees
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
void NoteAdderProcessor::tickCluster(AnimNoteState& state, int ch,
    int samplePos, juce::MidiBuffer& out)
{
    const int  mode = modeParam->get();
    const bool isScale = (mode == 1);

    const int lowNotes = (isScale ? clusterLowNotesParam : pnClusterLowNotesParam)->get();
    const int highNotes = (isScale ? clusterHighNotesParam : pnClusterHighNotesParam)->get();
    const int lowOctStart = (isScale ? clusterLowOctStartParam : pnClusterLowOctStartParam)->get();
    const int lowOctEnd = (isScale ? clusterLowOctEndParam : pnClusterLowOctEndParam)->get();
    const int highOctStart = (isScale ? clusterHighOctStartParam : pnClusterHighOctStartParam)->get();
    const int highOctEnd = (isScale ? clusterHighOctEndParam : pnClusterHighOctEndParam)->get();

    const int velMin = VEL_TABLE[juce::jlimit(0, 9, (isScale ? clusterVelMinParam : pnClusterVelMinParam)->get())];
    const int velMax = VEL_TABLE[juce::jlimit(0, 9, (isScale ? clusterVelMaxParam : pnClusterVelMaxParam)->get())];
    const float decayMs = (isScale ? clusterDecayParam : pnClusterDecayParam)->get();
    const int decaySamples = juce::jmax(1, (int)(currentSampleRate * decayMs / 1000.0f));
    const int safeMax = juce::jmax(velMin, velMax);
    const int velRange = safeMax - velMin + 1;
    const int heldKey = ch * 128 + state.inputNote;

    // Release previous cluster notes for this held key so the chord evolves each tick
    for (auto& cn : cloudNotes)
        if (cn.heldKey == heldKey)
            out.addEvent(juce::MidiMessage::noteOff(cn.channel + 1, cn.midiNote, (juce::uint8)0), samplePos);
    cloudNotes.erase(
        std::remove_if(cloudNotes.begin(), cloudNotes.end(),
            [heldKey](const CloudNote& cn) { return cn.heldKey == heldKey; }),
        cloudNotes.end());

    // Build a pool of candidate notes within an octave range.
    // Scale mode: use in-scale notes.  PerNote mode: use chord pitch-classes.
    auto buildPool = [&](int octStart, int octEnd) -> std::vector<int>
        {
            std::vector<int> pool;
            const int safeS = juce::jmin(octStart, octEnd);
            const int safeE = juce::jmax(octStart, octEnd);
            const int noteStart = juce::jlimit(0, 127, safeS * 12);
            const int noteEnd = juce::jlimit(0, 127, safeE * 12 + 11);

            if (isScale)
            {
                for (int n = noteStart; n <= noteEnd; ++n)
                    if (isNoteInScale(n))
                        pool.push_back(n);
            }
            else
            {
                // Collect unique pitch classes from the held note and its chord tones
                bool pcs[12] = {};
                pcs[state.inputNote % 12] = true;
                for (int n : state.baseNotes)
                    pcs[n % 12] = true;
                for (int n = noteStart; n <= noteEnd; ++n)
                    if (pcs[n % 12])
                        pool.push_back(n);
            }
            return pool;
        };

    // Emit up to 'count' distinct notes chosen randomly from the pool
    auto emitCluster = [&](const std::vector<int>& pool, int count)
        {
            if (pool.empty() || count <= 0) return;
            const int available = (int)pool.size();
            const int actual = juce::jmin(count, available);

            // Partial Fisher-Yates shuffle to pick 'actual' distinct indices
            std::vector<int> indices(available);
            for (int i = 0; i < available; ++i) indices[i] = i;

            for (int i = 0; i < actual; ++i)
            {
                if ((int)cloudNotes.size() >= MAX_CLOUD_NOTES) break;
                const int j = i + rng.nextInt(available - i);
                std::swap(indices[i], indices[j]);
                const int note = pool[indices[i]];
                const int vel = juce::jlimit(1, 127,
                    velMin + (velRange > 1 ? rng.nextInt(velRange) : 0));
                const int jitter = juce::jmax(1, decaySamples / 8);
                const int duration = juce::jmax(1,
                    decaySamples - jitter / 2 + rng.nextInt(jitter));
                out.addEvent(juce::MidiMessage::noteOn(ch + 1, note, (juce::uint8)vel), samplePos);
                cloudNotes.push_back({ heldKey, ch, note, duration });
            }
        };

    emitCluster(buildPool(lowOctStart, lowOctEnd), lowNotes);
    emitCluster(buildPool(highOctStart, highOctEnd), highNotes);
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
    pendingHumanizedNotes.clear();
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

    // ── PERNOTE (CUSTOM) ────────────────────────────────────────────────────
    if (modeParam->get() == 2)
    {
        const int pc = note % 12;
        const int chordType = pnChordTypeParams[pc]->get();

        if (chordType == 0)
        {
            // None: no added notes
            return result;
        }
        else if (chordType == 14)
        {
            // Custom: parse the cached offset string
            juce::String customStr;
            {
                juce::ScopedReadLock lk(pnCustomLock);
                customStr = pnCustomStrings[pc];
            }
            for (int off : parseCustomOffsets(customStr))
            {
                int added = note + off;
                if (added >= 0 && added <= 127)
                    result.push_back(added);
            }
        }
        else
        {
            // Types 1–13: Maj, Maj7, Maj9, Min, Min7, Min9, 6/9, Dom7, Sus2, Sus4, Power, Aug, Dim
            int count = 0;
            const int* offs = getChordOffsets(chordType - 1, count);
            for (int k = 0; k < count; ++k)
            {
                int added = note + offs[k];
                if (added >= 0 && added <= 127)
                    result.push_back(added);
            }
        }
        return result;
    }


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

    // ── Second harmony layer ──────────────────────────────────────────────────
    const int hmode2 = harmonicMode2Param->get();   // 0=None, 1-6 = 2nds-7ths
    if (hmode2 > 0)
    {
        const int step2 = hmode2;   // value maps directly: 1→step1 … 6→step6
        if (step2 != step)          // skip if identical to first harmony
        {
            for (int k = 1; k <= count; ++k)
            {
                int targetDeg = degree + step2 * k;
                int octaveAdd = targetDeg / sz;
                int degInScale = targetDeg % sz;
                int added = noteBase + ivs[degInScale] + octaveAdd * 12;
                if (added >= 0 && added <= 127)
                {
                    // Only add if not already present (avoids doubling with mode1)
                    if (std::find(result.begin(), result.end(), added) == result.end())
                        result.push_back(added);
                }
            }
        }
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

    // ── Scale transpose: shift each added note by N in-scale steps ───────────────
    // Applied after all other scale chord logic, before the global semitone transpose.
    {
        const int scaleSteps = scaleTransposeParam->get();
        if (scaleSteps != 0)
        {
            for (auto& n : result)
                n = juce::jlimit(0, 127, noteAtScaleDegreeOffset(n, scaleSteps));
            // Remove duplicates that may have collapsed at octave boundaries
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
        }
    }

    return result;
}

// ── MIDI Recording helpers ────────────────────────────────────────────────────

void NoteAdderProcessor::recordMidiEvent(const juce::MidiMessage& msg, int samplePos) noexcept
{
    const int numFree = recFifo.getFreeSpace();
    if (numFree < 1) return;   // buffer full – drop

    const double ts = recStartTime + (double)samplePos / currentSampleRate;

    int start1, size1, start2, size2;
    recFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        recBuffer[(size_t)start1] = { ts, msg };
        recFifo.finishedWrite(1);
    }
}

std::vector<NoteAdderProcessor::RecordedEvent> NoteAdderProcessor::drainRecordedEvents()
{
    const int ready = recFifo.getNumReady();
    std::vector<RecordedEvent> out;
    out.reserve((size_t)ready);

    int start1, size1, start2, size2;
    recFifo.prepareToRead(ready, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i) out.push_back(recBuffer[(size_t)(start1 + i)]);
    for (int i = 0; i < size2; ++i) out.push_back(recBuffer[(size_t)(start2 + i)]);
    recFifo.finishedRead(size1 + size2);
    return out;
}

//==============================================================================
// ── processBlock ─────────────────────────────────────────────────────────────

void NoteAdderProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midi)
{
    buffer.clear();
    juce::MidiBuffer processed;

    const int  numSamples = buffer.getNumSamples();
    currentBlockSize = numSamples;
    const int  modeVal = modeParam->get();
    const bool scaleMode = (modeVal == 1);
    const bool perNoteMode = (modeVal == 2);
    // 0=Allow all  1=Discard non-scale  2=Play nearest in-scale
    const int  discardMode = scaleMode ? discardParam->get() : 0;
    const bool dropInput = discardInputParam->get();

    // Normalise animation mode to internal codes
    // Scale:   0=Off 1=ReVoice 2=Wander 3=Drift 4=Cloud
    // Manual:  0=Off 1=ReVoice 2=Cloud  → remap 2→4
    // PerNote: 0=Off 1=ReVoice 2=Cloud  → remap 2→4
    int curAnim;
    if (scaleMode)    curAnim = animModeParam->get();
    else if (perNoteMode) { const int m = pnAnimModeParam->get(); curAnim = (m == 2) ? 4 : (m == 3) ? 5 : m; }
    else { const int m = manualAnimModeParam->get();    curAnim = (m == 2) ? 4 : m; }
    const bool animOn = (curAnim != 0);
    // Cloud mute: suppress input passthrough and initial added-note firings,
    // letting only the cloud-generated notes through.
    const bool cloudMuteOn = (curAnim == 4 || curAnim == 5) && cloudMuteInputParam->get();

    // Detect animMode change → kill all existing animation
    if (curAnim != prevAnimMode)
    {
        killAllAnimation(processed, 0);
        prevAnimMode = curAnim;
    }

    // ── 0. Build combined input: incoming MIDI + GUI-clicked notes ──────────
    // Merging into one buffer ensures GUI keys go through computeAddedNotes.
    juce::MidiBuffer combinedInput;
    for (const auto meta : midi)
        combinedInput.addEvent(meta.getMessage(), meta.samplePosition);
    injectGuiNotes(combinedInput);

    // ── 1. Process combined input ─────────────────────────────────────────────
    // NOTE: we process MIDI *before* firing pending notes so that note-offs
    // can cancel pending notes before they are emitted (avoids stuck notes when
    // the key is released before the deferred note-on would have fired).
    for (const auto meta : combinedInput)
    {
        const auto msg = meta.getMessage();
        const int  pos = meta.samplePosition;

        if (msg.isNoteOn())
        {
            const int ch = msg.getChannel() - 1;
            const int note = msg.getNoteNumber();
            const int inputVel = msg.getVelocity();

            // Track for live retranspose (cleared if discarded below)
            heldInputVelocity[ch][note] = inputVel;

            if (discardMode == 1 && !isNoteInScale(note))
            {
                // Mode 1: Discard – silence the note entirely
                heldInputVelocity[ch][note] = -1;
                discardedNotes[ch][note] = true;
                reroutedNoteMap[ch][note] = -1;
                activeAddedNotes[ch][note].clear();
                continue;
            }

            if (discardMode == 2 && !isNoteInScale(note))
            {
                // Mode 2: Play nearest in-scale note instead
                const int rerouted = nearestScaleNote(note);
                reroutedNoteMap[ch][note] = rerouted;
                discardedNotes[ch][note] = false;

                if (!dropInput && !cloudMuteOn)
                    processed.addEvent(
                        juce::MidiMessage::noteOn(msg.getChannel(), rerouted, (juce::uint8)inputVel), pos);

                auto addedNotes = computeAddedNotes(rerouted);
                const int humanTimeMs = humanizeTimeParam->get();
                const int humanVelSpread = humanizeVelParam->get();
                const int maxTimeSamples = humanTimeMs > 0
                    ? juce::jmax(1, (int)(currentSampleRate * humanTimeMs / 1000.0)) : 0;
                for (int added : addedNotes)
                {
                    const int timeSamples = maxTimeSamples > 0 ? rng.nextInt(maxTimeSamples) : 0;
                    const int velOffset = humanVelSpread > 0
                        ? rng.nextInt(humanVelSpread * 2 + 1) - humanVelSpread : 0;
                    const int addedVel = juce::jlimit(1, 127, inputVel + velOffset);
                    if (!animOn)
                    {
                        if (timeSamples == 0)
                            processed.addEvent(
                                juce::MidiMessage::noteOn(msg.getChannel(), added, (juce::uint8)addedVel), pos);
                        else
                            pendingHumanizedNotes.push_back({ msg.getChannel(), rerouted, added, addedVel, timeSamples });
                    }
                    else
                    {
                        processed.addEvent(
                            juce::MidiMessage::noteOn(msg.getChannel(), added, (juce::uint8)addedVel), pos);
                    }
                }
                if (animOn)
                {
                    killAnimForNote(ch, rerouted, processed, pos);
                    AnimNoteState state;
                    state.channel = ch;
                    state.velocity = inputVel;
                    state.inputNote = rerouted;
                    state.baseNotes = addedNotes;
                    state.currentNotes = addedNotes;
                    state.wanderSteps.assign(addedNotes.size(), 0);
                    state.invStep = 0; state.driftDegreeShift = 0;
                    state.samplesUntilTick = computeTickSamples();
                    animStates[ch * 128 + rerouted] = std::move(state);
                }
                else
                {
                    activeAddedNotes[ch][rerouted] = addedNotes;
                }
                continue;
            }

            reroutedNoteMap[ch][note] = -1;
            discardedNotes[ch][note] = false;

            // Input note passthrough – untouched, no humanization
            if (!dropInput && !cloudMuteOn)
                processed.addEvent(
                    juce::MidiMessage::noteOn(msg.getChannel(), note, (juce::uint8)inputVel), pos);

            auto addedNotes = computeAddedNotes(note);

            // Humanize each added note independently (time + velocity)
            const int humanTimeMs = humanizeTimeParam->get();
            const int humanVelSpread = humanizeVelParam->get();
            const int maxTimeSamples = humanTimeMs > 0
                ? juce::jmax(1, (int)(currentSampleRate * humanTimeMs / 1000.0)) : 0;

            // firedNotes tracks only notes that had an actual note-on emitted this block.
            // Deferred (humanized) notes are tracked in pendingHumanizedNotes; they must
            // NOT be included in activeAddedNotes or a note-off will be sent for a note
            // that was never turned on when the key is released before the deferred fire.
            std::vector<int> firedNotes;
            firedNotes.reserve(addedNotes.size());

            for (int added : addedNotes)
            {
                const int timeSamples = maxTimeSamples > 0 ? rng.nextInt(maxTimeSamples) : 0;
                const int velOffset = humanVelSpread > 0
                    ? rng.nextInt(humanVelSpread * 2 + 1) - humanVelSpread : 0;
                const int addedVel = juce::jlimit(1, 127, inputVel + velOffset);

                if (!animOn)
                {
                    // Non-anim path: full cross-block deferral
                    const int absPos = pos + timeSamples;
                    if (absPos < numSamples)
                    {
                        processed.addEvent(
                            juce::MidiMessage::noteOn(msg.getChannel(), added,
                                (juce::uint8)addedVel), absPos);
                        firedNotes.push_back(added);   // fired now → track for note-off
                    }
                    else
                    {
                        pendingHumanizedNotes.push_back({ msg.getChannel(), note, added,
                                                          addedVel, absPos - numSamples });
                        // NOT added to firedNotes – the pending path handles its own note-off
                    }
                }
                else
                {
                    // Anim path: fire immediately (clamped); tick fns handle subsequent humanize.
                    // When cloud-mute is on, skip the initial chord note-ons entirely —
                    // the cloud tick will generate its own notes from the base pool.
                    if (!cloudMuteOn)
                    {
                        const int addedPos = juce::jmin(pos + timeSamples, numSamples - 1);
                        processed.addEvent(
                            juce::MidiMessage::noteOn(msg.getChannel(), added,
                                (juce::uint8)addedVel), addedPos);
                        firedNotes.push_back(added);
                    }
                }
            }

            if (animOn)
            {
                killAnimForNote(ch, note, processed, pos);

                AnimNoteState state;
                state.channel = ch;
                state.velocity = inputVel;  // base; tick fns apply humanize themselves
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
                // Only store notes that were actually fired; deferred notes will
                // register themselves via pendingHumanizedNotes on arrival.
                activeAddedNotes[ch][note] = std::move(firedNotes);
            }
        }
        else if (msg.isNoteOff())
        {
            const int ch = msg.getChannel() - 1;
            const int note = msg.getNoteNumber();

            heldInputVelocity[ch][note] = -1;  // no longer held

            if (discardedNotes[ch][note])
            {
                discardedNotes[ch][note] = false;
                continue;
            }

            // If this note was rerouted (mode 2), send note-off for the rerouted pitch
            const int rerouted = reroutedNoteMap[ch][note];
            const int noteForOff = (rerouted >= 0) ? rerouted : note;
            reroutedNoteMap[ch][note] = -1;

            if (!dropInput && !cloudMuteOn)
                processed.addEvent(
                    juce::MidiMessage::noteOff(msg.getChannel(), noteForOff, (juce::uint8)0), pos);

            if (animOn)
                killAnimForNote(ch, noteForOff, processed, pos);
            else
            {
                for (int added : activeAddedNotes[ch][noteForOff])
                    processed.addEvent(
                        juce::MidiMessage::noteOff(msg.getChannel(), added, (juce::uint8)0), pos);
                activeAddedNotes[ch][noteForOff].clear();
            }

            // Cancel any still-pending humanized notes for this input note
            pendingHumanizedNotes.erase(
                std::remove_if(pendingHumanizedNotes.begin(), pendingHumanizedNotes.end(),
                    [&](const PendingNote& pn)
                    { return pn.midiChannel == msg.getChannel() && pn.inputNote == noteForOff; }),
                pendingHumanizedNotes.end());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            killAllAnimation(processed, pos);
            for (auto& ch : activeAddedNotes)  for (auto& v : ch) v.clear();
            for (auto& ch : discardedNotes)    ch.fill(false);
            for (auto& ch : reroutedNoteMap)   ch.fill(-1);
            for (auto& ch : heldInputVelocity) ch.fill(-1);
            processed.addEvent(msg, pos);
        }
        else
        {
            processed.addEvent(msg, pos);
        }
    }

    // ── 1b. Fire pending humanized notes that survived note-off cancellation ──
    // Doing this AFTER the MIDI loop means note-offs above already had a chance
    // to erase any pending notes whose key was released before they fired.
    {
        std::vector<PendingNote> carry;
        carry.reserve(pendingHumanizedNotes.size());
        for (auto& pn : pendingHumanizedNotes)
        {
            if (pn.samplesRemaining < numSamples)
            {
                processed.addEvent(
                    juce::MidiMessage::noteOn(pn.midiChannel, pn.addedNote,
                        (juce::uint8)pn.velocity),
                    pn.samplesRemaining);

                // Register the fired note so the eventual note-off can clean it up.
                // Guard: only register if the key is still held (fast re-press safety).
                const int zch = pn.midiChannel - 1;
                if (heldInputVelocity[zch][pn.inputNote] >= 0)
                    activeAddedNotes[zch][pn.inputNote].push_back(pn.addedNote);
            }
            else
            {
                pn.samplesRemaining -= numSamples;
                carry.push_back(pn);
            }
        }
        pendingHumanizedNotes = std::move(carry);
    }

    // ── 2. Advance animation ticks ────────────────────────────────────────────
    if (animOn && !animStates.empty())
    {
        const double tickSamples = computeTickSamples();
        const bool upToActive = (scaleMode ? animUpToParam
            : (perNoteMode ? pnAnimUpToParam
                : manualAnimUpToParam))->get();
        processCloudNoteOffs(processed, numSamples);

        for (auto& [key, state] : animStates)
        {
            state.samplesUntilTick -= numSamples;

            int maxTicks = 32;
            while (state.samplesUntilTick <= 0.0 && maxTicks-- > 0)
            {
                // When "up to" is active each tick gets its own randomised duration.
                const double thisTick = upToActive ? computeVarTickSamples() : tickSamples;
                state.samplesUntilTick += thisTick;
                const int ch = key / 128;

                switch (curAnim)
                {
                case 1: tickReVoice(state, ch, 0, processed); break;
                case 2: tickWander(state, ch, 0, processed); break;
                case 3: tickDrift(state, ch, 0, processed); break;
                case 4: tickCloud(state, ch, 0, processed); break;
                case 5: tickCluster(state, ch, 0, processed); break;
                default: break;
                }
            }
        }
    }
    else if (!animOn)
    {
        processCloudNoteOffs(processed, numSamples);
    }

    // ── Global semitone transpose (applied last, after all MIDI logic) ──────────
    {
        const int transpSemitones = globalTransposeParam->get();
        if (transpSemitones != 0)
        {
            juce::MidiBuffer transposed;
            for (const auto meta : processed)
            {
                auto msg = meta.getMessage();
                if (msg.isNoteOn() || msg.isNoteOff())
                {
                    const int newNote = juce::jlimit(0, 127,
                        msg.getNoteNumber() + transpSemitones);
                    msg = msg.isNoteOn()
                        ? juce::MidiMessage::noteOn(msg.getChannel(), newNote, msg.getVelocity())
                        : juce::MidiMessage::noteOff(msg.getChannel(), newNote, (juce::uint8)0);
                }
                transposed.addEvent(msg, meta.samplePosition);
            }
            processed.swapWith(transposed);
        }
    }

    // ── Live retranspose: revoice all held notes when transpose value changes ──
    // Runs AFTER the per-block transpose so injected events are already at their
    // final pitch and won't be double-shifted.
    {
        const int newT = globalTransposeParam->get();
        const int oldT = prevTranspose;
        if (newT != oldT)
        {
            for (int ch = 0; ch < 16; ++ch)
            {
                // ── Input passthrough and added notes (non-animation) ──────────
                for (int note = 0; note < 128; ++note)
                {
                    const int vel = heldInputVelocity[ch][note];
                    if (vel < 0) continue;

                    // Input passthrough (or rerouted) note
                    if (!discardedNotes[ch][note])
                    {
                        const int playNote = (reroutedNoteMap[ch][note] >= 0)
                            ? reroutedNoteMap[ch][note] : note;
                        const int oldOut = juce::jlimit(0, 127, playNote + oldT);
                        const int newOut = juce::jlimit(0, 127, playNote + newT);
                        if (oldOut != newOut)
                        {
                            processed.addEvent(
                                juce::MidiMessage::noteOff(ch + 1, oldOut, (juce::uint8)0), 0);
                            processed.addEvent(
                                juce::MidiMessage::noteOn(ch + 1, newOut, (juce::uint8)vel), 0);
                        }
                    }

                    // Added notes (non-animation mode)
                    for (int addedNote : activeAddedNotes[ch][note])
                    {
                        const int oldOut = juce::jlimit(0, 127, addedNote + oldT);
                        const int newOut = juce::jlimit(0, 127, addedNote + newT);
                        if (oldOut != newOut)
                        {
                            processed.addEvent(
                                juce::MidiMessage::noteOff(ch + 1, oldOut, (juce::uint8)0), 0);
                            processed.addEvent(
                                juce::MidiMessage::noteOn(ch + 1, newOut, (juce::uint8)vel), 0);
                        }
                    }
                }

                // ── Animation state currentNotes ──────────────────────────────
                for (auto& [key, state] : animStates)
                {
                    if (key / 128 != ch) continue;
                    for (int animNote : state.currentNotes)
                    {
                        const int oldOut = juce::jlimit(0, 127, animNote + oldT);
                        const int newOut = juce::jlimit(0, 127, animNote + newT);
                        if (oldOut != newOut)
                        {
                            processed.addEvent(
                                juce::MidiMessage::noteOff(ch + 1, oldOut, (juce::uint8)0), 0);
                            processed.addEvent(
                                juce::MidiMessage::noteOn(ch + 1, newOut,
                                    (juce::uint8)juce::jlimit(1, 127, state.velocity)), 0);
                        }
                    }
                }

                // ── Cloud notes ───────────────────────────────────────────────
                for (auto& cn : cloudNotes)
                {
                    if (cn.channel != ch) continue;
                    const int oldOut = juce::jlimit(0, 127, cn.midiNote + oldT);
                    const int newOut = juce::jlimit(0, 127, cn.midiNote + newT);
                    if (oldOut != newOut)
                    {
                        processed.addEvent(
                            juce::MidiMessage::noteOff(ch + 1, oldOut, (juce::uint8)0), 0);
                        processed.addEvent(
                            juce::MidiMessage::noteOn(ch + 1, newOut, (juce::uint8)64), 0);
                    }
                }
            }

            // Pending humanized notes: shift target pitch so they arrive correctly.
            for (auto& pn : pendingHumanizedNotes)
                pn.addedNote = juce::jlimit(0, 127, pn.addedNote + (newT - oldT));

            prevTranspose = newT;
        }
    }

    // ── Live scale-transpose: revoice held notes when scale transpose changes ──
    // Only active in scale mode (scale transpose only affects added notes there).
    // Runs after the global transpose block so globalT is already final for this
    // block; the same addedNote + globalT convention as the global retranspose is used.
    if (scaleMode)
    {
        const int newST = scaleTransposeParam->get();
        if (newST != prevScaleTranspose)
        {
            const int globalT = globalTransposeParam->get();

            if (!animOn)
            {
                // Non-animation: recompute and replace activeAddedNotes for every held note
                for (int ch = 0; ch < 16; ++ch)
                {
                    for (int note = 0; note < 128; ++note)
                    {
                        if (heldInputVelocity[ch][note] < 0) continue;
                        if (discardedNotes[ch][note]) continue;

                        const int vel = heldInputVelocity[ch][note];
                        const int playNote = (reroutedNoteMap[ch][note] >= 0)
                            ? reroutedNoteMap[ch][note] : note;

                        // Note-off for all currently sounding added notes
                        for (int an : activeAddedNotes[ch][note])
                            processed.addEvent(
                                juce::MidiMessage::noteOff(ch + 1,
                                    juce::jlimit(0, 127, an + globalT), (juce::uint8)0), 0);

                        // Recompute with the new scale transpose (already in param)
                        auto newNotes = computeAddedNotes(playNote);

                        // Note-on for all newly computed added notes
                        for (int n : newNotes)
                            processed.addEvent(
                                juce::MidiMessage::noteOn(ch + 1,
                                    juce::jlimit(0, 127, n + globalT),
                                    (juce::uint8)juce::jlimit(1, 127, vel)), 0);

                        activeAddedNotes[ch][note] = newNotes;
                    }
                }
            }
            else
            {
                // Animation mode: rebase every animation state to the new scale transpose.
                // This resets wander/drift offsets so the chord is re-voiced from its new
                // root position — analogous to releasing and re-pressing the key.
                for (auto& [key, state] : animStates)
                {
                    const int ch = key / 128;

                    // Note-off for all currently sounding animation notes
                    for (int an : state.currentNotes)
                        processed.addEvent(
                            juce::MidiMessage::noteOff(ch + 1,
                                juce::jlimit(0, 127, an + globalT), (juce::uint8)0), 0);

                    // Recompute base chord with new scale transpose
                    auto newBase = computeAddedNotes(state.inputNote);

                    // Note-on for the new base chord
                    for (int n : newBase)
                        processed.addEvent(
                            juce::MidiMessage::noteOn(ch + 1,
                                juce::jlimit(0, 127, n + globalT),
                                (juce::uint8)juce::jlimit(1, 127, state.velocity)), 0);

                    state.baseNotes = newBase;
                    state.currentNotes = newBase;
                    state.wanderSteps.assign(newBase.size(), 0);
                    state.invStep = 0;
                    state.driftDegreeShift = 0;
                }

                // Kill cloud notes — they were spawned from the old scale transpose chord
                for (auto& cn : cloudNotes)
                    processed.addEvent(
                        juce::MidiMessage::noteOff(cn.channel + 1,
                            juce::jlimit(0, 127, cn.midiNote + globalT), (juce::uint8)0), 0);
                cloudNotes.clear();
            }

            // Cancel pending humanized notes: they haven't fired yet and would arrive
            // at the old scale-transpose pitch. The revoiced activeAddedNotes already
            // cover all currently held keys, so no note-offs are needed for these.
            pendingHumanizedNotes.clear();

            prevScaleTranspose = newST;
        }
    }

    updatePianoRollFromBuffer(processed);
    renderAuditTones(buffer, numSamples);

    // ── MIDI Recording ────────────────────────────────────────
    if (isRecording.load(std::memory_order_relaxed))
    {
        // Grab host BPM from transport info (best-effort)
        if (auto* ph = getPlayHead())
        {
            juce::AudioPlayHead::CurrentPositionInfo pos;
            if (ph->getCurrentPosition(pos) && pos.bpm > 0.0)
                currentBpm.store(pos.bpm, std::memory_order_relaxed);
        }

        for (const auto meta : processed)
            recordMidiEvent(meta.getMessage(), meta.samplePosition);

        recStartTime += (double)numSamples / currentSampleRate;
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
    // Store custom per-note strings into the state tree before serializing
    {
        juce::ScopedReadLock lk(pnCustomLock);
        for (int i = 0; i < 12; ++i)
            apvts.state.setProperty("pnCustom" + juce::String(i), pnCustomStrings[i], nullptr);
    }
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void NoteAdderProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        // Restore custom strings from state
        juce::ScopedWriteLock lk(pnCustomLock);
        for (int i = 0; i < 12; ++i)
            pnCustomStrings[i] = apvts.state.getProperty("pnCustom" + juce::String(i), "").toString();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NoteAdderProcessor();
}