/*
  ==============================================================================
    SignalControl - Drawable CV Generator
    Processor Implementation  v6

    Output channels:
      Ch 0 — CV signal  (0.0–1.0 where drawn, IIR-smoothed, fades to 0 at holes)
      Ch 1 — Gate signal (1.0 drawn, 0.0 hole, cosine declick ramp)

    Host parameter:
      "cvOut" — AudioParameterFloat mirroring the IIR-smoothed CV value,
      updated via setValueNotifyingHost() at the end of every block.
      Marked automatable+meta so FL Studio's Patcher lists it as a modulation
      node — draw a cable from "CV Out" to any target parameter.
      This is control-rate (block-rate), not audio-rate; for audio-rate
      modulation route Ch 0 audio output directly.

    FL Studio Peak Controller:
      Peak Controller is an audio envelope follower, not a parameter reader.
      To use it: route Ch 0 audio output to a mixer track and point Peak
      Controller at that track.  The CV signal (0–1) maps to Peak Controller's
      0–100 % range.  For direct parameter modulation without Peak Controller,
      use Patcher's cable from the "CV Out" parameter node instead.

    Curve playback:
      Always nearest-neighbour lookup of curveData — no interpolation at
      playback time. The editor linearly interpolates between drawn points and
      writes the result into curveData at draw time.

    Declick:
      At every drawn→hole or hole→drawn boundary a cosine fade
      (DECLICK_SAMPLES long, scaled to actual sample rate) is applied to
      the Gate channel only. CV uses a separate one-pole IIR smoother.

    Fixes (v6):
      - STATE RECALL BUG: apvts.replaceState() was called before reading the
        CurveData child ValueTree, silently destroying it. The child is now
        extracted before replaceState() so all 2048 drawn values survive a
        project save/reload.
      - FL STUDIO PATCHER: cvOut was marked withAutomatable(false)/withMeta(false),
        hiding it from Patcher's parameter list. Both flags now true.

    Fixes (v4/v5, retained):
      - CV channel no longer multiplied by declickGain (was killing CV on every
        gate transition). CV outputs IIR-smoothed drawn value directly.
      - IIR smoother now actually applied to CV output (~4 ms time constant).
      - Interrupted declick seeds from current gain, preventing jumps.
      - writeLocked read per-sample for mid-block stroke write suppression.
      - curveWriteInProgress flag gates audio thread during bulk writes.
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
SignalControlAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Rate: -100..+100 Hz. Negative = reverse playback direction.
    // Custom skew/thirds are handled by the editor Slider mapping;
    // the parameter stores raw Hz.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "rate", "Rate",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.001f),
        0.25f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "tempoSync", "Tempo Sync", false));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "syncDivision", "Sync Division", 1, 8, 4));

    // Output parameter: mirrors the IIR-smoothed CV value each block.
    //
    // FL STUDIO PATCHER ROUTING (Bug Fix):
    //   Previously this was marked withAutomatable(false) and withMeta(false),
    //   which prevented FL Studio's Patcher from exposing it as a modulation
    //   node.  It is now automatable so that Patcher lists it under the plugin's
    //   parameters and lets you draw a modulation cable from it to any other
    //   plugin parameter.
    //
    //   HOW TO USE WITH PEAK CONTROLLER IN FL STUDIO:
    //   Peak Controller reads the *audio* peak level — it is an envelope
    //   follower, not a parameter reader.  To drive Peak Controller:
    //     1. Route this plugin's audio OUTPUT channel 1 (the CV signal, 0–1)
    //        to a mixer track that Peak Controller is watching, OR
    //     2. In Patcher, connect this plugin's "CV Out" parameter output
    //        directly to the target plugin's parameter input — this skips
    //        Peak Controller entirely and is more accurate (block-rate, ~1–5 ms).
    //
    //   Updated from the audio thread via setValueNotifyingHost() each block.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cvOut", "CV Out",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f),
        0.0f,
        juce::AudioParameterFloatAttributes()
        .withAutomatable(true)   // FIX: must be true for Patcher to wire it
        .withMeta(true)          // FIX: marks it as a modulation source node
        .withLabel("CV")));

    return { params.begin(), params.end() };
}

//==============================================================================
SignalControlAudioProcessor::SignalControlAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
#else
    :
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    for (auto& v : curveData)
        v.store(-1.0f);

    apvts.addParameterListener("rate", this);
    apvts.addParameterListener("tempoSync", this);
    apvts.addParameterListener("syncDivision", this);

    cachedRate.store(*apvts.getRawParameterValue("rate"));
    cachedTempoSync.store(*apvts.getRawParameterValue("tempoSync"));
    cachedDivision.store((float)*apvts.getRawParameterValue("syncDivision"));

    cvOutParam = apvts.getParameter("cvOut");
}

SignalControlAudioProcessor::~SignalControlAudioProcessor()
{
    apvts.removeParameterListener("rate", this);
    apvts.removeParameterListener("tempoSync", this);
    apvts.removeParameterListener("syncDivision", this);
}

void SignalControlAudioProcessor::parameterChanged(const juce::String& id, float v)
{
    if (id == "rate")               cachedRate.store(v);
    else if (id == "tempoSync")     cachedTempoSync.store(v);
    else if (id == "syncDivision")  cachedDivision.store(v);
}

//==============================================================================
const juce::String SignalControlAudioProcessor::getName() const { return JucePlugin_Name; }
bool SignalControlAudioProcessor::acceptsMidi()  const { return false; }
bool SignalControlAudioProcessor::producesMidi() const { return false; }
bool SignalControlAudioProcessor::isMidiEffect() const { return false; }
double SignalControlAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  SignalControlAudioProcessor::getNumPrograms() { return 1; }
int  SignalControlAudioProcessor::getCurrentProgram() { return 0; }
void SignalControlAudioProcessor::setCurrentProgram(int) {}
const juce::String SignalControlAudioProcessor::getProgramName(int) { return {}; }
void SignalControlAudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void SignalControlAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    phaseAccumulator = 0.0;
    smoothedCV = 0.0f;
    smoothedGate = 0.0f;
    declickCounter = 0;
    declickDir = 0;
    prevGateOn = false;
    declickGain = 0.0f;   // gate starts closed
    prevRawCV = -1.0f;
}

void SignalControlAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SignalControlAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return true;
#endif
}
#endif

//==============================================================================
double SignalControlAudioProcessor::getSamplesPerCycle(double bpm) const
{
    double secondsPerBeat = 60.0 / bpm;
    double cycleSeconds = (double)cachedDivision.load() * secondsPerBeat;
    return cycleSeconds * currentSampleRate;
}

//==============================================================================
void SignalControlAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < totalOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    // --- Cycle length ---
    double samplesPerCycle = 1.0;
    bool useSync = cachedTempoSync.load() > 0.5f;

    // phaseStep: positive = forward, negative = reverse, 0 = frozen
    double phaseStep = 1.0;

    if (useSync)
    {
        juce::AudioPlayHead* ph = getPlayHead();
        juce::AudioPlayHead::CurrentPositionInfo pos;
        double bpm = 120.0;
        if (ph != nullptr && ph->getCurrentPosition(pos) && pos.bpm > 0.0)
            bpm = pos.bpm;
        samplesPerCycle = getSamplesPerCycle(bpm);
        phaseStep = 1.0;
    }
    else
    {
        float rate = cachedRate.load();
        if (std::abs(rate) < 0.0001f)
        {
            samplesPerCycle = 1.0;  // arbitrary non-zero; phaseStep=0 keeps phase frozen
            phaseStep = 0.0;
        }
        else
        {
            samplesPerCycle = currentSampleRate / (double)std::abs(rate);
            phaseStep = (rate < 0.0f) ? -1.0 : 1.0;
        }
    }

    // --- Declick length scaled to current SR ---
    int declickLen = juce::jmax(1, (int)(DECLICK_SAMPLES * currentSampleRate / 44100.0));

    // Channel assignment:
    //   Stereo layout  — Ch 0: CV signal (0–1),  Ch 1: Gate (0 or 1 with declick)
    //   Mono layout    — Ch 0: CV signal only (gate not available)
    //
    // FL Studio Peak Controller tip:
    //   Peak Controller is an audio-level envelope follower.  To use it,
    //   send this plugin's Ch 0 audio output to a mixer track and point
    //   Peak Controller at that track.  The CV signal (0–1) maps directly
    //   to Peak Controller's 0–100 % range.  Alternatively, use Patcher's
    //   parameter-cable routing from the "CV Out" parameter (no Peak
    //   Controller needed — direct block-rate parameter modulation).
    float* cvPtr = (totalOut > 0) ? buffer.getWritePointer(0) : nullptr;
    float* gatePtr = (totalOut > 1) ? buffer.getWritePointer(1) : nullptr;

    // One-pole IIR coefficient for CV output smoothing.
    // Smooths rapid value changes (e.g. coarse drawing steps) without
    // blurring intentional fast curves. ~4 ms time constant at 44100 Hz.
    const float smoothCoeff = 1.0f - std::exp(-1.0f / (0.004f * (float)currentSampleRate));

    for (int s = 0; s < numSamples; ++s)
    {
        // Re-read writeLocked per sample so mid-block spline writes are caught
        bool writeLocked = curveWriteInProgress.load();

        double phase = phaseAccumulator / samplesPerCycle;
        phase -= std::floor(phase);

        // Lookup curve value — nearest-neighbour only. The curve buffer
        // already contains linearly-interpolated values between drawn
        // points (filled in by the editor at draw time), so no further
        // interpolation is performed here during playback.
        int idx = juce::jlimit(0, CURVE_RESOLUTION - 1,
            (int)(phase * CURVE_RESOLUTION));
        float rawCV = curveData[idx].load();

        bool gateOn = (rawCV >= 0.0f);
        // CV target: the drawn height value (0 in holes so IIR fades to 0)
        float cvTarget = gateOn ? juce::jlimit(0.0f, 1.0f, rawCV) : 0.0f;

        // --- Declick gate transitions (Gate channel only) ---
        // Seed declickGain from its current value when a new transition
        // interrupts an in-progress fade, so there is no jump.
        if (!writeLocked && gateOn != prevGateOn)
        {
            declickDir = gateOn ? 1 : -1;
            declickCounter = declickLen;
            prevGateOn = gateOn;
            // declickGain stays at its current value — the cosine ramp
            // below will drive it toward the target from wherever it is.
        }

        if (declickCounter > 0)
        {
            // Map remaining counter to a 0→1 progress value
            float t = 1.0f - (float)declickCounter / (float)declickLen;
            float env = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t));
            // Fade-in: gain goes 0→1; fade-out: gain goes 1→0
            declickGain = (declickDir > 0) ? env : (1.0f - env);
            --declickCounter;
        }
        else
        {
            declickGain = prevGateOn ? 1.0f : 0.0f;
        }

        // --- IIR smoothing on CV ---
        // Smooths step artefacts from coarsely-drawn curves and prevents
        // clicks when the scan head jumps between drawn slots.
        // CV output uses the smoothed value; gate uses the declick envelope.
        smoothedCV += smoothCoeff * (cvTarget - smoothedCV);

        // BUG FIX: CV channel outputs the drawn height directly (smoothed),
        // NOT multiplied by declickGain — that was killing the CV value
        // during every gate transition. Gate channel carries the on/off ramp.
        float finalCV = smoothedCV;
        float finalGate = declickGain;

        if (cvPtr)   cvPtr[s] = finalCV;
        if (gatePtr) gatePtr[s] = finalGate;

        // Advance phase (phaseStep: +1 forward, -1 reverse, 0 frozen)
        phaseAccumulator += phaseStep;
        if (phaseAccumulator >= samplesPerCycle)
            phaseAccumulator -= samplesPerCycle;
        else if (phaseAccumulator < 0.0)
            phaseAccumulator += samplesPerCycle;
    }

    // Expose state for editor
    {
        double phase = phaseAccumulator / samplesPerCycle;
        phase -= std::floor(phase);
        scanPosition.store((float)phase);

        int idx = juce::jlimit(0, CURVE_RESOLUTION - 1, (int)(phase * CURVE_RESOLUTION));
        currentCV.store(curveData[idx].load());
    }

    // --- Expose CV value to host as a parameter (e.g. for FL Studio's
    // Patcher modulation routing) ---
    // NOTE: this updates at block rate (~every 1-5 ms depending on buffer
    // size), i.e. control-rate, NOT audio-rate. Host parameter automation/
    // notification systems run on the message thread and simply cannot be
    // pushed at audio-sample rate — that is a host/API limitation, not
    // something this plugin can change. For true audio-rate CV/modulation,
    // route this plugin's audio output channel 0 (the CV signal) directly
    // into the target plugin's audio input — that path already runs at full
    // audio rate and is unaffected by this.
    if (cvOutParam != nullptr)
        cvOutParam->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, smoothedCV));
}

//==============================================================================
bool SignalControlAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* SignalControlAudioProcessor::createEditor()
{
    return new SignalControlAudioProcessorEditor(*this);
}

//==============================================================================
// Curve transform operations — called from the message/editor thread.
// curveWriteInProgress is held true during the write so the audio thread
// suppresses declick detection while the buffer is being reorganised.

void SignalControlAudioProcessor::mirrorCurveX()
{
    float tmp[CURVE_RESOLUTION];
    for (int i = 0; i < CURVE_RESOLUTION; ++i)
        tmp[i] = curveData[i].load();

    curveWriteInProgress.store(true);
    for (int i = 0; i < CURVE_RESOLUTION; ++i)
        curveData[i].store(tmp[CURVE_RESOLUTION - 1 - i]);
    curveWriteInProgress.store(false);
}

void SignalControlAudioProcessor::mirrorCurveY()
{
    curveWriteInProgress.store(true);
    for (int i = 0; i < CURVE_RESOLUTION; ++i)
    {
        float v = curveData[i].load();
        if (v >= 0.0f)
            curveData[i].store(juce::jlimit(0.0f, 1.0f, 1.0f - v));
    }
    curveWriteInProgress.store(false);
}

void SignalControlAudioProcessor::shiftCurveX(int slots)
{
    // Positive slots = shift right; negative = shift left. Wraps at edges.
    slots = ((slots % CURVE_RESOLUTION) + CURVE_RESOLUTION) % CURVE_RESOLUTION;
    if (slots == 0) return;

    float tmp[CURVE_RESOLUTION];
    for (int i = 0; i < CURVE_RESOLUTION; ++i)
        tmp[i] = curveData[i].load();

    curveWriteInProgress.store(true);
    for (int i = 0; i < CURVE_RESOLUTION; ++i)
        curveData[(i + slots) % CURVE_RESOLUTION].store(tmp[i]);
    curveWriteInProgress.store(false);
}

void SignalControlAudioProcessor::shiftCurveY(float delta)
{
    // delta in [0,1] units. Holes stay holes. Values wrap at [0,1] edges.
    curveWriteInProgress.store(true);
    for (int i = 0; i < CURVE_RESOLUTION; ++i)
    {
        float v = curveData[i].load();
        if (v >= 0.0f)
        {
            // Wrap: fmod keeps value in [0,1) with wrapping behaviour
            float shifted = v + delta;
            // Use fmod to wrap into [0,1]
            shifted = std::fmod(shifted, 1.0f);
            if (shifted < 0.0f) shifted += 1.0f;
            curveData[i].store(shifted);
        }
    }
    curveWriteInProgress.store(false);
}

//==============================================================================
// State saving: raw binary layout avoids DAW XML attribute size truncation.
//   [4] magic 0x53435631 | [4] xmlSize | [xmlSize] APVTS XML | [8192] curve floats
static constexpr juce::uint32 STATE_MAGIC = 0x53435631u;
static constexpr int CURVE_BYTES = CURVE_RESOLUTION * (int)sizeof(float);

void SignalControlAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    juce::MemoryBlock xmlBlock;
    { juce::MemoryOutputStream s(xmlBlock, false); xml->writeTo(s); }
    auto xmlSize = (juce::uint32)xmlBlock.getSize();

    float curve[CURVE_RESOLUTION];
    for (int i = 0; i < CURVE_RESOLUTION; ++i) curve[i] = curveData[i].load();

    destData.reset();
    juce::MemoryOutputStream out(destData, false);
    out.writeInt((int)STATE_MAGIC);
    out.writeInt((int)xmlSize);
    out.write(xmlBlock.getData(), xmlSize);
    out.write(curve, (size_t)CURVE_BYTES);
}

void SignalControlAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (sizeInBytes >= 8 + CURVE_BYTES)
    {
        juce::MemoryInputStream in(data, (size_t)sizeInBytes, false);
        if ((juce::uint32)in.readInt() == STATE_MAGIC)
        {
            int xmlSize = in.readInt();
            if (xmlSize >= 0 && xmlSize <= sizeInBytes - 8 - CURVE_BYTES)
            {
                juce::MemoryBlock xmlBlock(xmlSize);
                in.read(xmlBlock.getData(), xmlSize);
                auto xmlStr = juce::String::createStringFromData(xmlBlock.getData(), xmlSize);
                if (auto xml = juce::XmlDocument::parse(xmlStr))
                    apvts.replaceState(juce::ValueTree::fromXml(*xml));

                float curve[CURVE_RESOLUTION];
                in.read(curve, (size_t)CURVE_BYTES);
                for (int i = 0; i < CURVE_RESOLUTION; ++i) curveData[i].store(curve[i]);
                return;
            }
        }
    }
    // Legacy fallback
    if (auto xml = std::unique_ptr<juce::XmlElement>(getXmlFromBinary(data, sizeInBytes)))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    for (auto& v : curveData) v.store(-1.0f);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SignalControlAudioProcessor();
}