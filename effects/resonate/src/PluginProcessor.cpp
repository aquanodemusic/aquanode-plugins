/*
  ==============================================================================
    Ableton-Style Resonator Plugin -- v4 implementation (four engines)
    See PluginProcessor.h for the overview.
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>
#include <cstring>

//==============================================================================
// Knob curves
//
// Modelled / Native / Dark: decay skewed so 75 sits at half travel,
// color skewed so 40 sits at half travel (as in v2/v3).
static juce::NormalisableRange<float> makeDecayRange()
{
    juce::NormalisableRange<float> r(0.0f, 100.0f, 0.1f);
    r.setSkewForCentre(75.0f);
    return r;
}
static juce::NormalisableRange<float> makeColorRange()
{
    juce::NormalisableRange<float> r(0.0f, 100.0f, 0.1f);
    r.setSkewForCentre(40.0f);
    return r;
}

// Crisp (beta): piecewise decay curve -- 90..100 gets ~45% of the knob travel.
//     travel 0.00 - 0.30  ->  value   0 -  60
//     travel 0.30 - 0.55  ->  value  60 -  90
//     travel 0.55 - 0.85  ->  value  90 -  98
//     travel 0.85 - 1.00  ->  value  98 - 100
static const float kDecayNorms[5]  = { 0.0f, 0.30f, 0.55f, 0.85f, 1.0f };
static const float kDecayValues[5] = { 0.0f, 60.0f, 90.0f, 98.0f, 100.0f };

static float decayNormToValue(float norm)
{
    norm = juce::jlimit(0.0f, 1.0f, norm);
    for (int i = 0; i < 4; ++i)
        if (norm <= kDecayNorms[i + 1])
            return juce::jmap(norm, kDecayNorms[i], kDecayNorms[i + 1], kDecayValues[i], kDecayValues[i + 1]);
    return 100.0f;
}
static float decayValueToNorm(float value)
{
    value = juce::jlimit(0.0f, 100.0f, value);
    for (int i = 0; i < 4; ++i)
        if (value <= kDecayValues[i + 1])
            return juce::jmap(value, kDecayValues[i], kDecayValues[i + 1], kDecayNorms[i], kDecayNorms[i + 1]);
    return 1.0f;
}
static juce::NormalisableRange<float> makeCrispDecayRange()
{
    return juce::NormalisableRange<float>(0.0f, 100.0f,
        [](float, float, float norm)  -> float { return decayNormToValue(norm); },
        [](float, float, float value) -> float { return decayValueToNorm(value); },
        [](float, float, float value) -> float { return juce::jlimit(0.0f, 100.0f, value); });
}

//==============================================================================
// Parameter layout

static void addEngineParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                const juce::String& p, const juce::String& tag, bool crispStyle)
{
    auto name = [&](const juce::String& n) { return tag.isEmpty() ? n : (tag + " " + n); };

    const auto decayRange = crispStyle ? makeCrispDecayRange() : makeDecayRange();
    const auto colorRange = crispStyle ? juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f) : makeColorRange();
    const float decayDefault    = crispStyle ? 80.0f : 75.0f;
    const float colorDefault    = crispStyle ? 52.0f : 45.0f;
    const float resDecayDefault = crispStyle ? 95.0f : 50.0f;
    const float resColorDefault = crispStyle ? 74.2f : 50.0f;
    const float noteDefault     = crispStyle ? 60.0f : 48.0f;

    layout.add(std::make_unique<juce::AudioParameterBool>(p + "filter_enabled", name("Filter On"), false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(p + "filter_freq", name("Filter Frequency"),
        juce::NormalisableRange<float>(20.0f, 12000.0f, 1.0f, 0.3f), 1000.0f));
    layout.add(std::make_unique<juce::AudioParameterChoice>(p + "filter_type", name("Filter Type"),
        juce::StringArray{ "Lowpass", "Highpass", "Bandpass", "Notch" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(p + "mode", name("Shape Mode"),
        juce::StringArray{ "Full", "Odd" }, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(p + "decay", name("Decay"), decayRange, decayDefault));
    layout.add(std::make_unique<juce::AudioParameterBool>(p + "const_mode", name("Const"), false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(p + "color", name("Color"), colorRange, colorDefault));
    layout.add(std::make_unique<juce::AudioParameterBool>(p + "per_res_mode", name("Res Mode"), false));
    layout.add(std::make_unique<juce::AudioParameterFloat>(p + "res1_note", name("Resonator 1 Note"),
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), noteDefault));
    layout.add(std::make_unique<juce::AudioParameterFloat>(p + "width", name("Width"),
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f));

    if (crispStyle)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(p + "center_mode", name("Center"), true));
        layout.add(std::make_unique<juce::AudioParameterBool>(p + "exp_decay", name("Exp Decay"), false));
        layout.add(std::make_unique<juce::AudioParameterBool>(p + "oversample_2x", name("2x Oversample"), false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p + "smooth", name("Smooth"),
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p + "chorus", name("Chorus"),
            juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p + "lfo_rate", name("LFO Rate"),
            juce::NormalisableRange<float>(0.1f, 5.0f, 0.01f), 0.5f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(p + "lfo_depth", name("LFO Depth"),
            juce::NormalisableRange<float>(0.0f, 20.0f, 0.1f), 5.0f));
    }

    juce::NormalisableRange<float> panRange(-100.0f, 100.0f, 0.1f);
    const float defaultPan[ResonateAudioProcessor::MAX_RESONATORS] =
        { 0.0f, -100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f };

    for (int i = 0; i < ResonateAudioProcessor::MAX_RESONATORS; ++i)
    {
        const juce::String id = p + "res" + juce::String(i + 1);
        const juce::String rn = name("Resonator " + juce::String(i + 1));

        layout.add(std::make_unique<juce::AudioParameterBool>(id + "_enabled", rn + " On", i == 0));
        if (i > 0)
            layout.add(std::make_unique<juce::AudioParameterInt>(id + "_pitch", rn + " Pitch", -24, 24, 0));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id + "_fine", rn + " Fine",
            juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id + "_gain", rn + " Gain",
            juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id + "_decay", rn + " Decay", decayRange, resDecayDefault));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id + "_color", rn + " Color", colorRange, resColorDefault));
        layout.add(std::make_unique<juce::AudioParameterBool>(id + "_const", rn + " Const", false));
        layout.add(std::make_unique<juce::AudioParameterFloat>(id + "_pan", rn + " Pan", panRange, defaultPan[i]));
    }
}

static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ── Global ───────────────────────────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "engine", "Engine", juce::StringArray{ "Modelled", "Native", "Crisp", "Dark" }, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>("midi_enabled", "MIDI In", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>("gain", "Gain",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("drywet", "Dry/Wet",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>("wet_only", "Wet Only", false));

    // ── One full parameter set per engine ────────────────────────────────────
    addEngineParameters(layout, "",     "",        false);   // Modelled (legacy IDs)
    addEngineParameters(layout, "nat_", "Native",  false);
    addEngineParameters(layout, "crs_", "Crisp",   true);
    addEngineParameters(layout, "drk_", "Dark",    false);
    return layout;
}

//==============================================================================
ResonateAudioProcessor::ResonateAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < MAX_RESONATORS; ++i)
        midiDisplayNote[i].store(-1);
    cacheRefs();
}

ResonateAudioProcessor::~ResonateAudioProcessor() {}

void ResonateAudioProcessor::cacheRefs()
{
    for (int e = 0; e < NUM_ENGINES; ++e)
    {
        const juce::String p = enginePrefix(e);
        auto get = [&](const juce::String& id) { return parameters.getRawParameterValue(p + id); };
        auto& r = refs[e];
        r.note = get("res1_note");      r.decay = get("decay");          r.color = get("color");
        r.constMode = get("const_mode"); r.mode = get("mode");           r.perRes = get("per_res_mode");
        r.filterOn = get("filter_enabled"); r.filterFreq = get("filter_freq");
        r.filterType = get("filter_type"); r.width = get("width");
        for (int i = 0; i < MAX_RESONATORS; ++i)
        {
            const juce::String id = "res" + juce::String(i + 1);
            auto& q = r.res[i];
            q.enabled = get(id + "_enabled");
            q.pitch   = (i > 0) ? get(id + "_pitch") : nullptr;
            q.fine = get(id + "_fine"); q.gain = get(id + "_gain");
            q.decay = get(id + "_decay"); q.color = get(id + "_color");
            q.cnst = get(id + "_const"); q.pan = get(id + "_pan");
        }
        if (e == EngineCrisp)
        {
            r.center = get("center_mode"); r.expDecay = get("exp_decay"); r.os2x = get("oversample_2x");
            r.smooth = get("smooth"); r.chorus = get("chorus");
            r.lfoRate = get("lfo_rate"); r.lfoDepth = get("lfo_depth");
        }
    }
}

ResonateAudioProcessor::Snapshot ResonateAudioProcessor::snapshot(int e) const
{
    const auto& r = refs[e];
    Snapshot s;
    s.note = r.note->load(); s.decay = r.decay->load(); s.color = r.color->load();
    s.constMode = r.constMode->load() > 0.5f; s.perRes = r.perRes->load() > 0.5f;
    s.filterOn = r.filterOn->load() > 0.5f; s.filterFreq = r.filterFreq->load();
    s.filterType = static_cast<int>(r.filterType->load()); s.mode = static_cast<int>(r.mode->load());
    s.width = r.width->load() / 100.0f;
    for (int i = 0; i < MAX_RESONATORS; ++i)
    {
        const auto& q = r.res[i];
        auto& o = s.res[i];
        o.enabled = q.enabled->load() > 0.5f;
        o.pitch = q.pitch ? static_cast<int>(q.pitch->load()) : 0;
        o.fine = q.fine->load(); o.gain = q.gain->load();
        o.decay = q.decay->load(); o.color = q.color->load();
        o.cnst = q.cnst->load() > 0.5f; o.pan = q.pan->load();
    }
    return s;
}

double ResonateAudioProcessor::midiNoteFor(int idx) const
{
    if (!midiEnabled || heldNote[idx] < 0) return -1.0;
    return static_cast<double>(heldNote[idx]);
}

//==============================================================================
const juce::String ResonateAudioProcessor::getName() const { return JucePlugin_Name; }

bool ResonateAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}
bool ResonateAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}
bool ResonateAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}
double ResonateAudioProcessor::getTailLengthSeconds() const { return 10.0; }
int    ResonateAudioProcessor::getNumPrograms() { return 1; }
int    ResonateAudioProcessor::getCurrentProgram() { return 0; }
void   ResonateAudioProcessor::setCurrentProgram(int) {}
const  juce::String ResonateAudioProcessor::getProgramName(int) { return {}; }
void   ResonateAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
template <class Bank>
void ResonateAudioProcessor::prepareModelBank(Bank& b, double sr)
{
    for (auto& v : b.voice) { v.prepare(sr); v.reset(); }
    for (int ch = 0; ch < 2; ++ch) { b.stage[ch].prepare(sr); b.filt[ch].reset(); }
}

void ResonateAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int blockSize = juce::jmax(1, samplesPerBlock);

    prepareModelBank(modelled, sampleRate);
    prepareModelBank(dark, sampleRate);
    for (auto& r : native.res) { r.prepare(sampleRate); r.reset(); }
    native.filter.reset();
    crisp.prepare(sampleRate, blockSize);

    const int sel = getSelectedEngine();
    engineIn.setSize(2, blockSize, false, true, true);
    for (int e = 0; e < NUM_ENGINES; ++e)
    {
        engineOut[e].setSize(2, blockSize, false, true, true);
        engineInGain[e].reset(sampleRate, INPUT_HANDOVER_SECONDS);
        engineInGain[e].setCurrentAndTargetValue(e == sel ? 1.0f : 0.0f);
        engineOutFade[e].reset(sampleRate, SAFETY_FADE_SECONDS);
        engineOutFade[e].setCurrentAndTargetValue(1.0f);
        engineActive[e] = (e == sel);
        silentSamples[e] = 0;
        ringOutSamples[e] = 0;
    }
    setLatencySamples(0);
}

void ResonateAudioProcessor::releaseResources()
{
    for (auto& v : modelled.voice) v.reset();
    for (auto& v : dark.voice) v.reset();
    for (auto& r : native.res) r.reset();
    crisp.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ResonateAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    return true;
#endif
}
#endif

//==============================================================================
// MIDI voice allocation (round robin over the resonators that are switched on
// in the currently selected engine; held pitches are shared by all engines)

void ResonateAudioProcessor::handleMidiMessages(const juce::MidiBuffer& midiMessages)
{
    const int n = MAX_RESONATORS;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isNoteOn())
        {
            // Only resonators that are switched on can receive a note. If
            // nothing is enabled there's nowhere for it to go -- drop it.
            bool anyEnabled = false;
            for (int i = 0; i < n; ++i)
                if (allocEnabled[i]) { anyEnabled = true; break; }

            if (!anyEnabled)
                continue;

            int slot = -1;

            // Walk from the round-robin pointer looking for a free, enabled slot
            for (int k = 0; k < n; ++k)
            {
                const int idx = (rrPointer + k) % n;
                if (allocEnabled[idx] && !voices[idx].active) { slot = idx; break; }
            }

            // All enabled slots held -> steal the oldest enabled one
            if (slot < 0)
            {
                juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();
                for (int i = 0; i < n; ++i)
                    if (allocEnabled[i] && voices[i].order < oldest) { oldest = voices[i].order; slot = i; }
            }

            voices[slot].note = msg.getNoteNumber();
            voices[slot].active = true;
            voices[slot].order = ++voiceCounter;

            heldNote[slot] = msg.getNoteNumber();
            midiDisplayNote[slot].store(msg.getNoteNumber());

            rrPointer = (slot + 1) % n;
        }
        else if (msg.isNoteOff())
        {
            for (int i = 0; i < n; ++i)
                if (voices[i].active && voices[i].note == msg.getNoteNumber())
                {
                    voices[i].active = false;
                    break;
                }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int i = 0; i < n; ++i)
                voices[i].active = false;
        }
    }
}


//==============================================================================
// Pan law (as in v2): Resonator I = balance; II-VII = constant power,
// snapped to exactly 0/1 at the extremes.
void ResonateAudioProcessor::computePans(const Snapshot& s, float* panL, float* panR)
{
    for (int i = 0; i < MAX_RESONATORS; ++i)
    {
        const double p = juce::jlimit(-1.0, 1.0, s.res[i].pan / 100.0);
        if (i == 0)
        {
            panL[0] = static_cast<float>(p <= 0.0 ? 1.0 : 1.0 - p);
            panR[0] = static_cast<float>(p >= 0.0 ? 1.0 : 1.0 + p);
        }
        else if (p <= -0.9995) { panL[i] = 1.0f; panR[i] = 0.0f; }
        else if (p >= 0.9995)  { panL[i] = 0.0f; panR[i] = 1.0f; }
        else
        {
            const double theta = (p + 1.0) * juce::MathConstants<double>::pi * 0.25;
            panL[i] = static_cast<float>(std::cos(theta));
            panR[i] = static_cast<float>(std::sin(theta));
        }
    }
}

static const int kSourceChannel[ResonateAudioProcessor::MAX_RESONATORS] = { -1, 0, 1, 0, 1, 0, 1 };

//==============================================================================
// Modelled / Dark engines (measured model)

template <class Bank>
void ResonateAudioProcessor::updateModelBank(Bank& b, const Snapshot& s, double sr)
{
    b.filterOn = s.filterOn;
    if (s.filterOn)
        for (int ch = 0; ch < 2; ++ch)
            b.filt[ch].set(s.filterType, s.filterFreq, sr);
    b.width = s.width;
    computePans(s, b.panL, b.panR);

    for (int i = 0; i < MAX_RESONATORS; ++i)
    {
        const auto& q = s.res[i];
        b.resOn[i] = q.enabled;
        b.resGain[i] = juce::Decibels::decibelsToGain(static_cast<float>(q.gain), -100.0f);
        if (!q.enabled) continue;

        const double effDecay = s.perRes ? q.decay : s.decay;
        const double effColor = s.perRes ? q.color : s.color;
        const bool   effConst = s.perRes ? q.cnst  : s.constMode;
        const double mn = midiNoteFor(i);
        const double base = (mn >= 0.0) ? mn : (s.note + (i > 0 ? q.pitch : 0));
        const double note = juce::jlimit(0.0, 135.0, base + q.fine / 100.0);
        b.voice[i].setParams(note, effDecay, effColor, effConst, s.mode == 1);
    }
}

template <class Bank>
void ResonateAudioProcessor::renderModelBank(Bank& b, const float* const in[2], int n, int nIn, int nOut,
                                             float* outL, float* outR)
{
    const bool stereoWidth = (nIn >= 2 && nOut >= 2);
    for (int t = 0; t < n; ++t)
    {
        float sIn[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < nIn; ++ch)
        {
            float x = in[ch][t];
            if (b.filterOn) x = b.filt[ch].process(x);
            sIn[ch] = b.stage[ch].process(x);       // x0.25 + 327 Hz high-pass
        }
        // Resonator I: mono sum in, same to both sides (then balance)
        const float mono = (nIn > 1) ? 0.5f * (sIn[0] + sIn[1]) : sIn[0];
        const float o1 = b.resOn[0] ? b.voice[0].process(mono) * b.resGain[0] : 0.0f;
        float L = o1 * b.panL[0], R = o1 * b.panR[0];

        float busL = 0.0f, busR = 0.0f;
        for (int i = 1; i < MAX_RESONATORS; ++i)
        {
            const int ch = kSourceChannel[i];
            if (ch >= nIn || !b.resOn[i]) continue;
            const float o = b.voice[i].process(sIn[ch]) * b.resGain[i];
            busL += o * b.panL[i]; busR += o * b.panR[i];
        }
        if (stereoWidth)
        {
            const float mid = (busL + busR) * 0.5f, side = (busL - busR) * 0.5f * b.width;
            busL = mid + side; busR = mid - side;
        }
        outL[t] = L + busL;
        if (outR != nullptr) outR[t] = R + busR;
    }
}

//==============================================================================
// Native engine (original v2 flow)

void ResonateAudioProcessor::updateNative(const Snapshot& s, double sr)
{
    native.filterOn = s.filterOn; native.filterType = s.filterType; native.mode = s.mode;
    native.width = s.width;
    computePans(s, native.panL, native.panR);
    if (s.filterOn && sr > 0.0)
    {
        const float normFreq = juce::jlimit(0.0f, 0.99f, static_cast<float>(s.filterFreq / (sr * 0.5)));
        native.filter.setFrequency(normFreq, 0.7f);
    }
    const auto nm = (s.mode == 0) ? NativeEngine::ModeA : NativeEngine::ModeB;
    for (int i = 0; i < MAX_RESONATORS; ++i)
    {
        auto& r = native.res[i];
        const auto& q = s.res[i];
        r.enabled = q.enabled; r.pitchSemitones = (i > 0) ? q.pitch : 0;
        r.fineDetune = q.fine; r.gain = q.gain;
        r.updateParameters(sr, s.decay, s.note, s.color, nm, s.constMode, i,
                           s.perRes, q.decay, q.color, q.cnst, midiNoteFor(i));
    }
}

void ResonateAudioProcessor::renderNative(const float* const in[2], int n, int nIn, int nOut,
                                          float* outL, float* outR)
{
    const bool stereoWidth = (nIn >= 2 && nOut >= 2);
    const auto ft = static_cast<NativeEngine::FilterType>(native.filterType);
    const bool modeA = (native.mode == 0);
    for (int t = 0; t < n; ++t)
    {
        float sIn[2] = { 0.0f, 0.0f };
        for (int ch = 0; ch < nIn; ++ch)
        {
            float x = in[ch][t];
            if (native.filterOn) x = native.filter.process(x, ch, ft);
            sIn[ch] = x;
        }
        float L = 0.0f, R = 0.0f;
        for (int ch = 0; ch < nIn; ++ch)
        {
            const float o = modeA ? native.res[0].processA(sIn[ch], ch) : native.res[0].processB(sIn[ch], ch);
            if (ch == 0) L = o * native.panL[0]; else R = o * native.panR[0];
        }
        float busL = 0.0f, busR = 0.0f;
        for (int i = 1; i < MAX_RESONATORS; ++i)
        {
            const int ch = kSourceChannel[i];
            if (ch >= nIn) continue;
            const float o = modeA ? native.res[i].processA(sIn[ch], ch) : native.res[i].processB(sIn[ch], ch);
            busL += o * native.panL[i]; busR += o * native.panR[i];
        }
        if (stereoWidth)
        {
            const float mid = (busL + busR) * 0.5f, side = (busL - busR) * 0.5f * native.width;
            busL = mid + side; busR = mid - side;
        }
        outL[t] = L + busL;
        if (outR != nullptr) outR[t] = R + busR;
    }
}

//==============================================================================
// Crisp engine parameters

CrispEngine::Params ResonateAudioProcessor::crispParams(const Snapshot& s) const
{
    const auto& r = refs[EngineCrisp];
    CrispEngine::Params p;
    p.note = s.note; p.decay = s.decay; p.color = s.color;
    p.smooth = r.smooth->load(); p.chorus = r.chorus->load();
    p.lfoRate = r.lfoRate->load(); p.lfoDepth = r.lfoDepth->load();
    p.filterOn = s.filterOn; p.filterFreq = s.filterFreq; p.filterType = s.filterType;
    p.constMode = s.constMode; p.center = r.center->load() > 0.5f;
    p.expDecay = r.expDecay->load() > 0.5f; p.perRes = s.perRes; p.os2x = r.os2x->load() > 0.5f;
    p.mode = s.mode; p.width = s.width;
    for (int i = 0; i < MAX_RESONATORS; ++i)
    {
        const auto& q = s.res[i];
        p.pan[i] = q.pan;
        p.res[i].enabled = q.enabled; p.res[i].pitch = q.pitch; p.res[i].fine = q.fine;
        p.res[i].gain = q.gain; p.res[i].decay = q.decay; p.res[i].color = q.color;
        p.res[i].cnst = q.cnst; p.res[i].midiNote = midiNoteFor(i);
    }
    return p;
}

//==============================================================================
void ResonateAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalIn = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();
    for (auto i = totalIn; i < totalOut; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int nIn = juce::jmin(totalIn, 2);
    const int nOut = juce::jmin(totalOut, 2);
    if (numSamples <= 0 || nIn <= 0 || nOut <= 0)
        return;

    // ── Engine selection: input goes to the selected engine only ────────────
    const int sel = getSelectedEngine();
    for (int e = 0; e < NUM_ENGINES; ++e)
    {
        engineInGain[e].setTargetValue(e == sel ? 1.0f : 0.0f);
        if (e == sel)
        {
            if (!engineActive[e]) { resetEngine(e); engineActive[e] = true; }  // fresh start
            ringOutSamples[e] = 0;
            silentSamples[e] = 0;
            engineOutFade[e].setTargetValue(1.0f);      // in case a safety fade was running
        }
    }

    // ── MIDI (allocation follows the selected engine's On switches) ─────────
    midiEnabled = parameters.getRawParameterValue("midi_enabled")->load() > 0.5f;
    for (int i = 0; i < MAX_RESONATORS; ++i)
        allocEnabled[i] = refs[sel].res[i].enabled->load() > 0.5f;
    handleMidiMessages(midiMessages);
    for (int i = 0; i < MAX_RESONATORS; ++i)
        midiDisplayNote[i].store(midiEnabled ? heldNote[i] : -1);

    if (engineIn.getNumSamples() < numSamples)
        engineIn.setSize(2, numSamples, false, true, true);
    for (int e = 0; e < NUM_ENGINES; ++e)
        if (engineOut[e].getNumSamples() < numSamples)
            engineOut[e].setSize(2, numSamples, false, true, true);

    // ── Render every engine that is selected or still ringing out ───────────
    const float* dryIn[2] = { buffer.getReadPointer(0), nIn > 1 ? buffer.getReadPointer(1) : nullptr };
    for (int e = 0; e < NUM_ENGINES; ++e)
    {
        if (!engineActive[e])
            continue;

        // Input for this engine: the dry signal with the 5 ms hand-over ramp
        // (a ringing-out engine gets silence).
        const bool feeding = engineInGain[e].isSmoothing() || engineInGain[e].getTargetValue() > 0.0f;
        for (int ch = 0; ch < nIn; ++ch)
        {
            if (!feeding) { engineIn.clear(ch, 0, numSamples); continue; }
            engineIn.copyFrom(ch, 0, dryIn[ch], numSamples);
        }
        if (feeding && engineInGain[e].isSmoothing())
        {
            for (int t = 0; t < numSamples; ++t)
            {
                const float gIn = engineInGain[e].getNextValue();
                for (int ch = 0; ch < nIn; ++ch)
                    engineIn.setSample(ch, t, engineIn.getSample(ch, t) * gIn);
            }
        }
        else if (!feeding)
            engineInGain[e].skip(numSamples);

        const float* in[2] = { engineIn.getReadPointer(0), nIn > 1 ? engineIn.getReadPointer(1) : nullptr };
        renderEngine(e, in, numSamples, nIn, nOut);

        // Output fade before switch-off (30 ms below -70 dB, or 300 ms safety fade)
        if (engineOutFade[e].isSmoothing() || engineOutFade[e].getTargetValue() < 1.0f)
            for (int t = 0; t < numSamples; ++t)
            {
                const float g = engineOutFade[e].getNextValue();
                for (int ch = 0; ch < nOut; ++ch)
                    engineOut[e].setSample(ch, t, engineOut[e].getSample(ch, t) * g);
            }

        // Ring-out bookkeeping for engines that are no longer selected
        if (e != sel)
        {
            float peak = 0.0f;
            for (int ch = 0; ch < nOut; ++ch)
                peak = juce::jmax(peak, engineOut[e].getMagnitude(ch, 0, numSamples));

            silentSamples[e] = (peak < SILENCE_THRESHOLD) ? silentSamples[e] + numSamples : 0;
            ringOutSamples[e] += numSamples;

            // Start a fade-out (never a hard cut) when the tail has been below
            // -70 dB for 100 ms (short fade), or is still ringing after 10 s
            // (longer safety fade). Ramp length is set when the fade starts.
            const bool fading = engineOutFade[e].getTargetValue() == 0.0f;
            if (!fading)
            {
                if (silentSamples[e] >= static_cast<int>(SILENCE_HOLD_SECONDS * currentSampleRate))
                {
                    engineOutFade[e].reset(currentSampleRate, SILENCE_FADE_SECONDS);
                    engineOutFade[e].setTargetValue(0.0f);
                }
                else if (ringOutSamples[e] > static_cast<int>(MAX_RINGOUT_SECONDS * currentSampleRate))
                {
                    engineOutFade[e].reset(currentSampleRate, SAFETY_FADE_SECONDS);
                    engineOutFade[e].setTargetValue(0.0f);
                }
            }

            const bool faded = engineOutFade[e].getTargetValue() == 0.0f && !engineOutFade[e].isSmoothing();
            if (faded)
            {
                // Fully faded to silence -> reset and switch the engine off.
                // (This block's output, which contains the end of the fade, is
                // still summed below and cleared afterwards.)
                engineActive[e] = false;
                resetEngine(e, false);
                engineOutFade[e].setCurrentAndTargetValue(1.0f);
                silentSamples[e] = 0;
                ringOutSamples[e] = 0;
            }
        }
    }

    // ── Sum + master gain + dry/wet ──────────────────────────────────────────
    const float masterGain = juce::Decibels::decibelsToGain(parameters.getRawParameterValue("gain")->load());
    const float dryWet = parameters.getRawParameterValue("drywet")->load() / 100.0f;
    const bool  wetOnly = parameters.getRawParameterValue("wet_only")->load() > 0.5f;

    float* out[2] = { buffer.getWritePointer(0), nOut > 1 ? buffer.getWritePointer(1) : nullptr };
    for (int ch = 0; ch < nOut; ++ch)
    {
        for (int t = 0; t < numSamples; ++t)
        {
            float wet = 0.0f;
            for (int e = 0; e < NUM_ENGINES; ++e)
                wet += engineOut[e].getSample(ch, t);
            wet *= masterGain;
            const float dry = out[ch][t];
            out[ch][t] = wetOnly ? wet * dryWet : dry * (1.0f - dryWet) + wet * dryWet;
        }
    }
    // Engines that are off contribute nothing (their buffers are not rendered)
    for (int e = 0; e < NUM_ENGINES; ++e)
        if (!engineActive[e])
            engineOut[e].clear();
}

//==============================================================================
// Render one engine's wet signal into engineOut[e]
void ResonateAudioProcessor::renderEngine(int e, const float* const in[2], int n, int nIn, int nOut)
{
    float* oL = engineOut[e].getWritePointer(0);
    float* oR = nOut > 1 ? engineOut[e].getWritePointer(1) : nullptr;
    switch (e)
    {
        case EngineModelled: updateModelBank(modelled, snapshot(e), currentSampleRate);
                             renderModelBank(modelled, in, n, nIn, nOut, oL, oR); break;
        case EngineDark:     updateModelBank(dark, snapshot(e), currentSampleRate);
                             renderModelBank(dark, in, n, nIn, nOut, oL, oR); break;
        case EngineNative:   updateNative(snapshot(e), currentSampleRate);
                             renderNative(in, n, nIn, nOut, oL, oR); break;
        case EngineCrisp:
        default:
            crisp.process(in, n, nIn, nOut, crispParams(snapshot(e)));
            for (int ch = 0; ch < nOut; ++ch)
                engineOut[e].copyFrom(ch, 0, crisp.getOutput(ch), n);
            break;
    }
}

// Clear an engine's state so it starts silent next time it is selected
void ResonateAudioProcessor::resetEngine(int e, bool clearOutput)
{
    switch (e)
    {
        case EngineModelled:
            for (auto& v : modelled.voice) v.reset();
            for (int ch = 0; ch < 2; ++ch) { modelled.stage[ch].reset(); modelled.filt[ch].reset(); }
            break;
        case EngineDark:
            for (auto& v : dark.voice) v.reset();
            for (int ch = 0; ch < 2; ++ch) { dark.stage[ch].reset(); dark.filt[ch].reset(); }
            break;
        case EngineNative:
            for (auto& r : native.res) r.reset();
            native.filter.reset();
            break;
        case EngineCrisp:
        default:
            crisp.reset();
            break;
    }
    if (clearOutput)
        engineOut[e].clear();
}

//==============================================================================
bool                        ResonateAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* ResonateAudioProcessor::createEditor() { return new ResonateAudioProcessorEditor(*this); }

//==============================================================================
// State / presets
//
// upgradeStateXml() lets older files load sensibly:
//   - beta (has "center_mode", no "engine"): its unprefixed values are the
//     Crisp settings -> renamed to "crs_*", engine set to Crisp
//   - v2/v3 (unprefixed, no "engine"): already the Modelled IDs -> unchanged
//   Global IDs (gain, drywet, wet_only, midi_enabled) are never renamed.
void ResonateAudioProcessor::upgradeStateXml(juce::XmlElement& xml) const
{
    bool hasEngine = false, isBeta = false;
    for (auto* e : xml.getChildIterator())
    {
        const auto id = e->getStringAttribute("id");
        if (id == "engine") hasEngine = true;
        if (id == "center_mode" || id == "lfo_rate" || id == "oversample_2x") isBeta = true;
    }
    const juce::StringArray globals{ "gain", "drywet", "wet_only", "midi_enabled", "engine" };

    if (hasEngine)
    {
        // v3.2: Modelled and Native shared the unprefixed knobs. If there is no
        // separate Native set yet, give Native a copy so it sounds as it did.
        bool hasNative = false;
        for (auto* e : xml.getChildIterator())
            if (e->getStringAttribute("id").startsWith("nat_")) { hasNative = true; break; }
        if (hasNative)
            return;

        juce::Array<juce::XmlElement*> toCopy;
        for (auto* e : xml.getChildIterator())
        {
            const auto id = e->getStringAttribute("id");
            if (id.isNotEmpty() && !globals.contains(id)
                && !id.startsWith("crs_") && !id.startsWith("drk_"))
                toCopy.add(e);
        }
        for (auto* e : toCopy)
        {
            auto* c = xml.createNewChildElement("PARAM");
            c->setAttribute("id", "nat_" + e->getStringAttribute("id"));
            c->setAttribute("value", e->getStringAttribute("value"));
        }
        return;
    }

    if (!isBeta)
        return;

    for (auto* e : xml.getChildIterator())
    {
        const auto id = e->getStringAttribute("id");
        if (id.isNotEmpty() && !globals.contains(id))
            e->setAttribute("id", "crs_" + id);
    }
    auto* eng = xml.createNewChildElement("PARAM");
    eng->setAttribute("id", "engine");
    eng->setAttribute("value", static_cast<double>(EngineCrisp));
}

void ResonateAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void ResonateAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(parameters.state.getType()))
    {
        upgradeStateXml(*xmlState);
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::File ResonateAudioProcessor::getDefaultPresetDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("AquaNode")
        .getChildFile("Resonate Presets");
    if (!dir.exists())
        dir.createDirectory();
    return dir;
}

// A preset stores the complete APVTS state: all four engines' settings plus
// the globals and the selected engine.
bool ResonateAudioProcessor::savePresetToFile(const juce::File& file)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml == nullptr)
        return false;

    xml->setAttribute("presetName", file.getFileNameWithoutExtension());
    xml->setAttribute("pluginVersion", JucePlugin_VersionString);
    return xml->writeTo(file, {});
}

bool ResonateAudioProcessor::loadPresetFromFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (xml == nullptr || !xml->hasTagName(parameters.state.getType()))
        return false;

    upgradeStateXml(*xml);
    parameters.replaceState(juce::ValueTree::fromXml(*xml));
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ResonateAudioProcessor();
}
