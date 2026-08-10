#include "PluginProcessor.h"
#include "PluginEditor.h"

#define _USE_MATH_DEFINES
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//==============================================================================
// Internal helpers
//==============================================================================
static Biquad makePeakingEQInternal(double fc, double sampleRate,
                                     double gainDB, double bwSemitones) noexcept
{
    double nyquist = sampleRate * 0.5;
    fc = juce::jlimit(1.0, nyquist - 1.0, fc);

    const double A      = std::pow(10.0, gainDB / 40.0);
    const double w0     = 2.0 * M_PI * fc / sampleRate;
    const double sinw0  = std::sin(w0);
    const double cosw0  = std::cos(w0);
    const double bwOct  = bwSemitones / 12.0;
    const double alpha  = sinw0 * std::sinh((std::log(2.0) / 2.0) * bwOct * w0 / sinw0);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cosw0;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cosw0;
    const double a2 = 1.0 - alpha / A;

    Biquad bq;
    bq.b0 = b0/a0;  bq.b1 = b1/a0;  bq.b2 = b2/a0;
    bq.a1 = a1/a0;  bq.a2 = a2/a0;
    return bq;
}

// Public wrappers
Biquad makePeakingEQ(double fc, double sampleRate,
                      double gainDB, double bwSemitones) noexcept
{
    return makePeakingEQInternal(fc, sampleRate, gainDB, bwSemitones);
}

Biquad makeLowPassQ(double fc, double sampleRate, double Q) noexcept
{
    double nyquist = sampleRate * 0.5;
    fc = juce::jlimit(20.0, nyquist - 1.0, fc);
    Q  = juce::jlimit(0.1, 40.0, Q);

    const double w0     = 2.0 * M_PI * fc / sampleRate;
    const double cosw0  = std::cos(w0);
    const double sinw0  = std::sin(w0);
    const double alpha  = sinw0 / (2.0 * Q);

    const double b0 = (1.0 - cosw0) * 0.5;
    const double b1 =  1.0 - cosw0;
    const double b2 = (1.0 - cosw0) * 0.5;
    const double a0 =  1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 =  1.0 - alpha;

    Biquad bq;
    bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
    bq.a1 = a1/a0; bq.a2 = a2/a0;
    return bq;
}

Biquad makeLowPass(double fc, double sampleRate) noexcept
{
    return makeLowPassQ(fc, sampleRate, 1.0 / std::sqrt(2.0));
}
Biquad makeHighShelf(double fc, double sampleRate, double gainDB) noexcept
{
    double nyquist = sampleRate * 0.5;
    fc = juce::jlimit(20.0, nyquist - 1.0, fc);

    const double A     = std::pow(10.0, gainDB / 40.0);
    const double w0    = 2.0 * M_PI * fc / sampleRate;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double S     = 1.0; // shelf slope = 1 (maximally smooth)
    const double alpha = sinw0 / 2.0 * std::sqrt((A + 1.0/A) * (1.0/S - 1.0) + 2.0);

    const double b0 =        A * ((A+1) + (A-1)*cosw0 + 2*std::sqrt(A)*alpha);
    const double b1 = -2.0 * A * ((A-1) + (A+1)*cosw0);
    const double b2 =        A * ((A+1) + (A-1)*cosw0 - 2*std::sqrt(A)*alpha);
    const double a0 =             (A+1) - (A-1)*cosw0 + 2*std::sqrt(A)*alpha;
    const double a1 =  2.0 *     ((A-1) - (A+1)*cosw0);
    const double a2 =             (A+1) - (A-1)*cosw0 - 2*std::sqrt(A)*alpha;

    Biquad bq;
    bq.b0 = b0/a0; bq.b1 = b1/a0; bq.b2 = b2/a0;
    bq.a1 = a1/a0; bq.a2 = a2/a0;
    return bq;
}


static inline double midiNoteToHz(double note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

//==============================================================================
juce::String PitchControlEQAudioProcessor::noteActiveParamID(int i)
{
    return "note_active_" + juce::String(i);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
PitchControlEQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < kNumNotes; ++i)
    {
        bool defaultActive = (i == 0 || i == 3 || i == 5); // C, D#, F
        params.push_back(std::make_unique<juce::AudioParameterBool>(
            noteActiveParamID(i), kNoteNames[i] + " protected", defaultActive));
    }

    // ---- Per-note dampen ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        dampenDBParamID(), "Dampen (dB)",
        juce::NormalisableRange<float>(-128.0f, 0.0f, 0.1f), -42.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        dampenQParamID(), "Dampen Q",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.2007f));

    // ---- Per-note boost ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        boostDBParamID(), "Boost (dB)",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 12.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        boostQParamID(), "Boost Q",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.4993f));

    // ---- Output gain ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        outputGainParamID(), "Output Gain",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // ---- Range ----
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        rangeFromParamID(), "Range From", 0, kTotalNotes - 1, 31));   // G2

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        rangeToParamID(), "Range To", 0, kTotalNotes - 1, 120));      // C10

    // ---- Chorus (on boost delta only) ----
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        chorusRateParamID(),  "Chorus Rate",
        juce::NormalisableRange<float>(0.05f, 100.0f, 0.01f), 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        chorusDepthParamID(), "Chorus Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        chorusMixParamID(),   "Chorus Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.25f));

    // ---- Global bell (independent path) ----
    // 0 dB = bypassed; negative = cut; positive = boost
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        globalBellDBParamID(), "Global Bell (dB)",
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), -15.0f));

    // BW in semitones: wide range for a broad global colouring bell
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        globalBellBWParamID(), "Global Bell BW (semitones)",
        juce::NormalisableRange<float>(1.0f, 144.0f, 0.5f), 70.0f));  // stored 70 → audible 75 st

    // Bell centre frequency as a MIDI note (float for smooth control)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        globalBellFreqParamID(), "Global Bell Freq (MIDI note)",
        juce::NormalisableRange<float>(0.0f, (float)(kTotalNotes - 1), 0.1f),
        84.0f));  // C7

    // ---- MIDI Mode ----
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        midiModeParamID(), "MIDI Mode", false));

    return { params.begin(), params.end() };
}

//==============================================================================
PitchControlEQAudioProcessor::PitchControlEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#else
    :
#endif
apvts(*this, nullptr, "PitchControlEQ", createParameterLayout()),
m_paramListener(*this)
{
    for (int i = 0; i < kNumNotes; ++i)
        apvts.addParameterListener(noteActiveParamID(i), &m_paramListener);
    apvts.addParameterListener(dampenDBParamID(),     &m_paramListener);
    apvts.addParameterListener(dampenQParamID(),      &m_paramListener);
    apvts.addParameterListener(boostDBParamID(),      &m_paramListener);
    apvts.addParameterListener(boostQParamID(),       &m_paramListener);
    apvts.addParameterListener(outputGainParamID(),   &m_paramListener);
    apvts.addParameterListener(rangeFromParamID(),    &m_paramListener);
    apvts.addParameterListener(rangeToParamID(),      &m_paramListener);
    apvts.addParameterListener(chorusRateParamID(),      &m_paramListener);
    apvts.addParameterListener(chorusDepthParamID(),     &m_paramListener);
    apvts.addParameterListener(chorusMixParamID(),       &m_paramListener);
    apvts.addParameterListener(globalBellDBParamID(),   &m_paramListener);
    apvts.addParameterListener(globalBellBWParamID(),   &m_paramListener);
    apvts.addParameterListener(globalBellFreqParamID(), &m_paramListener);
    apvts.addParameterListener(midiModeParamID(),        &m_paramListener);

    // Init chorus delay buffers
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        m_chorusBuf[ch].assign(kChorusMaxDelaySamples, 0.0);
        m_chorusWritePos[ch] = 0;
        // Offset R channel LFO by 90° for stereo width
        m_chorusLfoPhase[ch] = (ch == 0) ? 0.0 : M_PI * 0.5;
    }
    m_chorusLfoPhaseInc = 0.0;

    // Zero all filter states
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        for (int n = 0; n < kTotalNotes; ++n)
        {
            for (int st = 0; st < kMaxDampenStages; ++st)
                m_states[ch][n][st] = Biquad::State{};
            m_boostStates[ch][n] = Biquad::State{};
        }
        m_globalBellState[ch] = Biquad::State{};
    }
}

PitchControlEQAudioProcessor::~PitchControlEQAudioProcessor()
{
    for (int i = 0; i < kNumNotes; ++i)
        apvts.removeParameterListener(noteActiveParamID(i), &m_paramListener);
    apvts.removeParameterListener(dampenDBParamID(),     &m_paramListener);
    apvts.removeParameterListener(dampenQParamID(),      &m_paramListener);
    apvts.removeParameterListener(boostDBParamID(),      &m_paramListener);
    apvts.removeParameterListener(boostQParamID(),       &m_paramListener);
    apvts.removeParameterListener(outputGainParamID(),   &m_paramListener);
    apvts.removeParameterListener(rangeFromParamID(),    &m_paramListener);
    apvts.removeParameterListener(rangeToParamID(),      &m_paramListener);
    apvts.removeParameterListener(chorusRateParamID(),      &m_paramListener);
    apvts.removeParameterListener(chorusDepthParamID(),     &m_paramListener);
    apvts.removeParameterListener(chorusMixParamID(),       &m_paramListener);
    apvts.removeParameterListener(globalBellDBParamID(),   &m_paramListener);
    apvts.removeParameterListener(globalBellBWParamID(),   &m_paramListener);
    apvts.removeParameterListener(globalBellFreqParamID(), &m_paramListener);
    apvts.removeParameterListener(midiModeParamID(),        &m_paramListener);
}

//==============================================================================
const juce::String PitchControlEQAudioProcessor::getName()  const { return JucePlugin_Name; }
bool PitchControlEQAudioProcessor::acceptsMidi()             const { return true; }
bool PitchControlEQAudioProcessor::producesMidi()            const { return false; }
bool PitchControlEQAudioProcessor::isMidiEffect()            const { return false; }
double PitchControlEQAudioProcessor::getTailLengthSeconds()  const { return 0.0; }

int  PitchControlEQAudioProcessor::getNumPrograms()                { return 1; }
int  PitchControlEQAudioProcessor::getCurrentProgram()             { return 0; }
void PitchControlEQAudioProcessor::setCurrentProgram(int)          {}
const juce::String PitchControlEQAudioProcessor::getProgramName(int) { return {}; }
void PitchControlEQAudioProcessor::changeProgramName(int, const juce::String&) {}
bool PitchControlEQAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* PitchControlEQAudioProcessor::createEditor()
{
    return new PitchControlEQAudioProcessorEditor(*this);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool PitchControlEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts); return true;
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
void PitchControlEQAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        for (int n = 0; n < kTotalNotes; ++n)
        {
            for (int st = 0; st < kMaxDampenStages; ++st)
                m_states[ch][n][st] = Biquad::State{};
            m_boostStates[ch][n] = Biquad::State{};
        }
        m_globalBellState[ch] = Biquad::State{};
    }

    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        m_chorusBuf[ch].assign(kChorusMaxDelaySamples, 0.0);
        m_chorusWritePos[ch] = 0;
        m_chorusLfoPhase[ch] = (ch == 0) ? 0.0 : M_PI * 0.5;
    }
    m_chorusLfoPhaseInc = 0.0;

    m_dirty.store(true);
}

void PitchControlEQAudioProcessor::releaseResources() {}

//==============================================================================
void PitchControlEQAudioProcessor::rebuildFilters()
{
    m_dirty.store(false, std::memory_order_relaxed);

    const float dampenDB  = apvts.getRawParameterValue(dampenDBParamID())->load();
    // Q knob is 0=wide, 1=narrow. Map exponentially: BW = 4.0 * exp(-6.0 * t)
    // t=0 -> ~4.0 semitones (wide), t=1 -> ~0.01 semitones (very narrow)
    auto qToBW = [](float t) -> float {
        return 4.0f * std::exp(-6.0f * t);
    };
    const float dampenBW = qToBW(apvts.getRawParameterValue(dampenQParamID())->load());
    const float boostDB  = apvts.getRawParameterValue(boostDBParamID())->load();
    const float boostBW  = qToBW(apvts.getRawParameterValue(boostQParamID())->load());
    const float bellDB    = apvts.getRawParameterValue(globalBellDBParamID())->load();
    // Bell Width param: stored 1→144. Invert so knob left=wide, right=narrow.
    const float bellBW    = 145.0f - apvts.getRawParameterValue(globalBellBWParamID())->load();
    const float bellFreqNote = apvts.getRawParameterValue(globalBellFreqParamID())->load();

    int rangeFrom = (int)apvts.getRawParameterValue(rangeFromParamID())->load();
    int rangeTo   = (int)apvts.getRawParameterValue(rangeToParamID())->load();
    if (rangeFrom > rangeTo) std::swap(rangeFrom, rangeTo);

    bool noteProtected[kNumNotes]{};
    int  numProtected = 0;
    for (int i = 0; i < kNumNotes; ++i)
    {
        noteProtected[i] = apvts.getRawParameterValue(noteActiveParamID(i))->load() > 0.5f;
        if (noteProtected[i]) ++numProtected;
    }

    const bool anyProtected = (numProtected > 0);
    m_anyActive = false;

    // ------------------------------------------------------------------
    // Per-note dampen & boost
    // ------------------------------------------------------------------
    for (int n = 0; n < kTotalNotes; ++n)
    {
        const int  nc          = n % kNumNotes;
        const bool isProtected = noteProtected[nc];
        const bool inRange     = (n >= rangeFrom && n <= rangeTo);
        const double fc        = midiNoteToHz((double)n);

        // DAMPEN
        const bool dampenNeeded = anyProtected && inRange && !isProtected && (dampenDB < -0.01f);
        if (dampenNeeded)
        {
            m_anyActive = true;
            double totalCut = (double)dampenDB;
            totalCut = std::max(totalCut, kMaxDampenPerStage * (double)kMaxDampenStages);
            int stages = (int)std::ceil(std::abs(totalCut) / std::abs(kMaxDampenPerStage));
            stages = juce::jlimit(1, kMaxDampenStages, stages);
            m_filterStages[n] = stages;
            double perStage = totalCut / (double)stages;
            for (int st = 0; st < stages; ++st)
                m_filters[n][st] = makePeakingEQInternal(fc, currentSampleRate,
                                                          perStage, (double)dampenBW);
            for (int st = stages; st < kMaxDampenStages; ++st)
                m_filters[n][st] = Biquad{};
        }
        else
        {
            m_filterStages[n] = 0;
            for (int st = 0; st < kMaxDampenStages; ++st)
                m_filters[n][st] = Biquad{};
        }

        // BOOST
        const bool boostNeeded = anyProtected && inRange && isProtected && (boostDB > 0.01f);
        m_boostActive[n] = boostNeeded;
        if (boostNeeded)
        {
            m_anyActive = true;
            m_boostFilters[n] = makePeakingEQInternal(fc, currentSampleRate,
                                                       (double)boostDB, (double)boostBW);
        }
        else
            m_boostFilters[n] = Biquad{};
    }

    // ------------------------------------------------------------------
    // Global bell — independent path, user-controlled depth and BW.
    // Active whenever |bellDB| > threshold.
    // ------------------------------------------------------------------
    const bool bellActive = std::abs(bellDB) > 0.05f;
    m_globalBellOn = bellActive;
    if (bellActive)
    {
        m_anyActive = true;
        double fcBell = midiNoteToHz((double)bellFreqNote);
        m_globalBell = makePeakingEQInternal(fcBell, currentSampleRate,
                                              (double)bellDB, (double)bellBW);
    }
    else
    {
        m_globalBell = Biquad{};
    }

    // Reset biquad states for active filters
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        for (int n = 0; n < kTotalNotes; ++n)
        {
            for (int st = 0; st < m_filterStages[n]; ++st)
                m_states[ch][n][st] = Biquad::State{};
            if (m_boostActive[n])
                m_boostStates[ch][n] = Biquad::State{};
        }
        if (m_globalBellOn) m_globalBellState[ch] = Biquad::State{};
    }
}

//==============================================================================
void PitchControlEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // ------------------------------------------------------------------
    // MIDI Mode: parse incoming MIDI, update note_active params from
    // held notes (any octave of a note class activates that class).
    // ------------------------------------------------------------------
    if (apvts.getRawParameterValue(midiModeParamID())->load(std::memory_order_relaxed) > 0.5f)
    {
        bool anyChange = false;
        for (const auto meta : midiMessages)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                const int nc = msg.getNoteNumber() % kNumNotes;
                if (m_midiHeldCount[nc].fetch_add(1) == 0)
                {
                    // First held note of this class — activate
                    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(
                            apvts.getParameter(noteActiveParamID(nc))))
                    {
                        if (!p->get()) { p->setValueNotifyingHost(1.0f); anyChange = true; }
                    }
                }
            }
            else if (msg.isNoteOff())
            {
                const int nc = msg.getNoteNumber() % kNumNotes;
                const int prev = m_midiHeldCount[nc].load();
                if (prev > 0)
                {
                    if (m_midiHeldCount[nc].fetch_sub(1) == 1)
                    {
                        // Last note of this class released — deactivate
                        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(
                                apvts.getParameter(noteActiveParamID(nc))))
                        {
                            if (p->get()) { p->setValueNotifyingHost(0.0f); anyChange = true; }
                        }
                    }
                }
            }
        }
        (void)anyChange; // rebuildFilters triggered via paramListener
    }

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int N      = buffer.getNumSamples();

    for (int i = numIn; i < numOut; ++i) buffer.clear(i, 0, N);

    if (m_dirty.load(std::memory_order_relaxed))
        rebuildFilters();

    if (!m_anyActive)
        return;

    const float outputGainDB  = apvts.getRawParameterValue(outputGainParamID())->load();
    const float outputGainLin = std::pow(10.0f, outputGainDB / 20.0f);
    const int   numCh         = std::min(numIn, kMaxChannels);

    const float chorusRate  = apvts.getRawParameterValue(chorusRateParamID())->load();
    const float chorusDepth = apvts.getRawParameterValue(chorusDepthParamID())->load();
    const float chorusMix   = apvts.getRawParameterValue(chorusMixParamID())->load();
    // LFO phase increment per sample
    m_chorusLfoPhaseInc = 2.0 * M_PI * (double)chorusRate / currentSampleRate;
    // Max modulation delay in samples: depth 0→1 maps to 0→20ms
    const double chorusMaxDelaySamples = 0.02 * currentSampleRate;

    for (int ch = 0; ch < numCh; ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int s = 0; s < N; ++s)
        {
            const double dry = (double)data[s];
            double out = dry;

            // ----------------------------------------------------------------
            // PATH 1 – Per-note DAMPEN
            //   Process dry through dampen filters → accumulate delta into out.
            //   postDampen is captured so the bell can operate on it.
            // ----------------------------------------------------------------
            double postDampen = dry;
            {
                double sig = dry;
                bool anyDampen = false;
                for (int n = 0; n < kTotalNotes; ++n)
                {
                    const int stages = m_filterStages[n];
                    if (stages > 0)
                    {
                        anyDampen = true;
                        for (int st = 0; st < stages; ++st)
                            sig = m_filters[n][st].process(sig, m_states[ch][n][st]);
                    }
                }
                if (anyDampen)
                {
                    out += (sig - dry);
                    postDampen = sig;
                }
            }

            // ----------------------------------------------------------------
            // PATH 2 – Per-note BOOST
            //   Boost bell runs from dry. The boost delta is then optionally
            //   processed by the chorus (modulated delay) before being added
            //   back to the output. Equal-power crossfade keeps volume constant
            //   regardless of mix amount.
            // ----------------------------------------------------------------
            {
                double sig = dry;
                bool anyBoost = false;
                for (int n = 0; n < kTotalNotes; ++n)
                {
                    if (m_boostActive[n])
                    {
                        anyBoost = true;
                        sig = m_boostFilters[n].process(sig, m_boostStates[ch][n]);
                    }
                }
                if (anyBoost)
                {
                    double delta = sig - dry;

                    // --------------------------------------------------------
                    // CHORUS on boost delta — equal-power crossfade so that
                    // increasing mix does NOT reduce perceived volume.
                    // --------------------------------------------------------
                    if (chorusMix > 0.001f && chorusDepth > 0.001f)
                    {
                        // Write to delay buffer
                        m_chorusBuf[ch][m_chorusWritePos[ch]] = delta;

                        // Modulated read position (sinusoidal LFO)
                        const double lfoVal    = std::sin(m_chorusLfoPhase[ch]);
                        const double delaySamp = chorusMaxDelaySamples * 0.5
                                               * (1.0 + (double)chorusDepth * lfoVal);
                        const double readPos   = (double)m_chorusWritePos[ch] - delaySamp;
                        const int    r0        = (int)std::floor(readPos);
                        const double frac      = readPos - (double)r0;

                        // Linear interpolation between adjacent samples
                        auto bufRead = [&](int idx) -> double {
                            int i = ((idx % kChorusMaxDelaySamples) + kChorusMaxDelaySamples)
                                    % kChorusMaxDelaySamples;
                            return m_chorusBuf[ch][i];
                        };
                        const double wet = bufRead(r0) * (1.0 - frac) + bufRead(r0 + 1) * frac;

                        // Advance write head and LFO (each channel independently)
                        m_chorusWritePos[ch] = (m_chorusWritePos[ch] + 1) % kChorusMaxDelaySamples;
                        m_chorusLfoPhase[ch] += m_chorusLfoPhaseInc;
                        if (m_chorusLfoPhase[ch] >= 2.0 * M_PI)
                            m_chorusLfoPhase[ch] -= 2.0 * M_PI;

                        // Equal-power crossfade: cos/sin ensures constant RMS power
                        const double mixAngle = (double)chorusMix * M_PI * 0.5;
                        delta = delta * std::cos(mixAngle) + wet * std::sin(mixAngle);
                    }

                    out += delta;
                }
            }

            // ----------------------------------------------------------------
            // PATH 3 – GLOBAL BELL
            //   Runs on postDampen (not dry) so it only further shapes the
            //   already-attenuated frequencies. Boosted notes are unaffected
            //   because their contribution entered via the dry-based PATH 2 delta.
            // ----------------------------------------------------------------
            if (m_globalBellOn)
            {
                double sig = m_globalBell.process(postDampen, m_globalBellState[ch]);
                out += (sig - postDampen);
            }

            data[s] = (float)out * outputGainLin;
        }
    }
}

//==============================================================================
void PitchControlEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PitchControlEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PitchControlEQAudioProcessor();
}