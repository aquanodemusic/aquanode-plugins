/*
  ==============================================================================
    PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
static juce::AudioParameterFloat* pf (const juce::String& id, const juce::String& name,
                                      float lo, float hi, float def, float skew = 1.0f)
{
    return new juce::AudioParameterFloat (
        { id, 1 }, name,
        juce::NormalisableRange<float> (lo, hi, 0.0001f, skew), def);
}

APVTS::ParameterLayout TablaAudioProcessor::createLayout()
{
    using namespace tabla::pid;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.emplace_back (pf (tuning,       "Tuning",        -12.0f, 12.0f, 0.0f));
    p.emplace_back (pf (tension,      "Tension",         0.0f,  1.0f, 0.25f));
    p.emplace_back (pf (syahi,        "Syahi",           0.0f,  1.0f, 0.75f));
    p.emplace_back (pf (size,         "Size",            0.6f,  1.6f, 1.0f));
    p.emplace_back (pf (damping,      "Damping",         0.0f,  1.0f, 0.4f));
    p.emplace_back (pf (brightness,   "Brightness",      0.0f,  1.0f, 0.5f));
    p.emplace_back (pf (strikePos,    "Strike Pos",      0.0f,  1.0f, 0.6f));
    p.emplace_back (pf (hardness,     "Hardness",        0.0f,  1.0f, 0.5f));
    p.emplace_back (pf (pressure,     "Bayan Pressure",  0.0f,  1.0f, 0.0f));
    p.emplace_back (pf (bodyRes,      "Body Res",        0.0f,  1.0f, 0.5f));
    p.emplace_back (pf (airRes,       "Air Res",         0.0f,  1.0f, 0.4f));
    p.emplace_back (pf (sympathetic,  "Sympathetic",     0.0f,  1.0f, 0.3f));
    p.emplace_back (pf (bassCoupling, "Bass Coupling",   0.0f,  1.0f, 0.25f));
    p.emplace_back (pf (randomness,   "Randomness",      0.0f,  1.0f, 0.35f));
    p.emplace_back (pf (articulation, "Articulation",    0.3f,  2.0f, 1.0f));
    p.emplace_back (pf (sustain,      "Sustain",         0.3f,  2.0f, 1.0f));
    p.emplace_back (pf (decay,        "Decay",           0.3f,  2.0f, 1.0f));
    p.emplace_back (pf (velResponse,  "Vel Response",    0.2f,  3.0f, 1.0f));
    p.emplace_back (pf (output,       "Output",          0.0f,  1.5f, 0.9f));
    p.emplace_back (pf (width,        "Width",           0.0f,  1.0f, 0.35f));

    return { p.begin(), p.end() };
}

//==============================================================================
TablaAudioProcessor::TablaAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    schedule.reserve (64);
}

//==============================================================================
void TablaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    bayan.prepare (sampleRate, tabla::TablaDrum::Type::Bayan);
    dayan.prepare (sampleRate, tabla::TablaDrum::Type::Dayan);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) oversampler->getLatencyInSamples());

    outGain.reset (sampleRate, 0.02);
    widener.prepare (sampleRate);
    samplePos = 0;
    schedule.clear();
}

bool TablaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
float TablaAudioProcessor::applyVelCurve (float v) const
{
    const float k = apvts.getRawParameterValue (tabla::pid::velResponse)->load();
    return std::pow (juce::jlimit (0.0f, 1.0f, v), k);
}

void TablaAudioProcessor::triggerBol (tabla::Bol b, float velocity, int sampleOffset)
{
    const float vel = applyVelCurve (velocity);
    for (auto& s : tabla::bolToStrokes (b, vel))
    {
        Scheduled sc;
        sc.drum = s.drum;
        sc.p    = s.p;
        sc.when = samplePos + sampleOffset + (juce::int64) (s.timeMs * 0.001 * sr);
        schedule.push_back (sc);
    }
}

void TablaAudioProcessor::setSlideBend (int drum, float semitones)
{
    (drum == 0 ? slideBendBayan : slideBendDayan).store (semitones);
}

void TablaAudioProcessor::triggerUIStrike (int drum, float radius, float velocity)
{
    int s1, b1, s2, b2;
    uiFifo.prepareToWrite (1, s1, b1, s2, b2);
    if (b1 > 0)
    {
        uiBuf[(size_t) s1] = { drum, juce::jlimit (0.0f, 1.0f, radius),
                               juce::jlimit (0.05f, 1.0f, velocity) };
        uiFifo.finishedWrite (1);
    }
}

//==============================================================================
void TablaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // --- read parameters (block rate) ---
    auto get = [this] (const juce::String& id)
    { return apvts.getRawParameterValue (id)->load(); };

    const float pTuning  = get (tabla::pid::tuning);
    const float pTension = get (tabla::pid::tension);
    const float pSyahi   = get (tabla::pid::syahi);
    const float pSize    = get (tabla::pid::size);
    const float pDamp    = get (tabla::pid::damping);
    const float pBright  = get (tabla::pid::brightness);
    const float pStrikeP = get (tabla::pid::strikePos);
    const float pHard    = get (tabla::pid::hardness);
    const float pPress   = get (tabla::pid::pressure);
    const float pBody    = get (tabla::pid::bodyRes);
    const float pAir     = get (tabla::pid::airRes);
    const float pSymp    = get (tabla::pid::sympathetic);
    const float pBass    = get (tabla::pid::bassCoupling);
    const float pRand    = get (tabla::pid::randomness);
    const float pArtic   = get (tabla::pid::articulation);
    const float pSustain = get (tabla::pid::sustain);
    const float pDecay   = get (tabla::pid::decay);

    outGain.setTargetValue (get (tabla::pid::output));
    const float pWidth = get (tabla::pid::width);

    // Rebuild modal tables from current controls (cheap at block rate).
    bayan.rebuild (pTuning, pSyahi, pSize, pDamp, pBright, pSustain, pDecay);
    dayan.rebuild (pTuning, pSyahi, pSize, pDamp, pBright, pSustain, pDecay);

    // Bayan heel pressure -> continuous upward bend (the "ge -> ghe" slide).
    float bayanBend = pPress * 5.0f;   // up to ~a fourth

    // --- pull UI clicks into the schedule ---
    {
        int s1, b1, s2, b2;
        const int ready = uiFifo.getNumReady();
        uiFifo.prepareToRead (ready, s1, b1, s2, b2);

        auto consume = [&] (int idx)
        {
            auto& u = uiBuf[(size_t) idx];
            tabla::StrikeParams p;
            p.position   = u.radius;
            p.level      = applyVelCurve (u.vel);
            p.hardness   = pHard;
            p.durationMs = 5.0f;
            p.brightness = pBright;
            p.muteAmount = (u.radius > 0.85f && u.drum == 1) ? 0.3f : 0.0f;
            p.decayScale = 1.0f;
            schedule.push_back ({ u.drum, p, samplePos });
        };

        for (int i = 0; i < b1; ++i) consume (s1 + i);
        for (int i = 0; i < b2; ++i) consume (s2 + i);
        uiFifo.finishedRead (b1 + b2);
    }

    // --- MIDI: bols, velocity, pitch-bend/CC pressure ---
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int  off = meta.samplePosition;

        if (m.isNoteOn())
        {
            const int bolIdx = m.getNoteNumber() - kFirstBolNote;
            if (bolIdx >= 0 && bolIdx < (int) tabla::Bol::NumBols)
                triggerBol ((tabla::Bol) bolIdx, m.getFloatVelocity(), off);
        }
        else if (m.isPitchWheel())
        {
            const float norm = (m.getPitchWheelValue() - 8192) / 8192.0f; // -1..1
            bayanBend = pPress * 5.0f + norm * 4.0f;
        }
        else if (m.isController() && m.getControllerNumber() == 1) // mod wheel
        {
            bayanBend = pPress * 5.0f + (m.getControllerValue() / 127.0f) * 5.0f;
        }
    }

    // Slide mode: UI drag on a drum adds a continuous pitch bend, just like
    // sliding the palm heel across the head.
    bayan.setPressureBend (bayanBend + slideBendBayan.load());
    dayan.setPressureBend (slideBendDayan.load());

    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    const int controlInterval = 16;   // coeff refresh rate
    int untilControl = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const juce::int64 now = samplePos + i;

        // fire any scheduled strokes due this sample
        for (int s = (int) schedule.size() - 1; s >= 0; --s)
        {
            if (schedule[(size_t) s].when <= now)
            {
                auto& sc = schedule[(size_t) s];
                auto& drum = (sc.drum == 0) ? bayan : dayan;
                // Global Strike-Pos knob biases every stroke's contact point.
                sc.p.position = juce::jlimit (0.0f, 1.0f,
                                              sc.p.position * 0.7f + pStrikeP * 0.3f);
                drum.strike (sc.p, pBright - 0.5f, pArtic, rng, pRand);
                if (sc.drum == 0) bayanFlash.store (1.0f); else dayanFlash.store (1.0f);
                schedule.erase (schedule.begin() + s);
            }
        }

        if (untilControl-- <= 0)
        {
            bayan.updateControl();
            dayan.updateControl();
            untilControl = controlInterval;
        }

        // sympathetic / bass coupling: feed a little of each drum into the other
        const float symp = pSymp * 0.03f;
        const float toDayan = sympSmoothed * (symp + pBass * 0.04f); // bayan -> dayan (bass coupling)
        const float toBayan = sympSmoothed * symp;

        const float by = bayan.process (toBayan, pTension, pBody, pAir, rng);
        const float dy = dayan.process (toDayan, pTension, pBody, pAir, rng);

        sympSmoothed += 0.05f * (0.5f * (by + dy) - sympSmoothed);

        // gentle stereo placement: bayan left, dayan right
        L[i] = by * 0.9f + dy * 0.35f;
        R[i] = by * 0.35f + dy * 0.9f;
    }

    // --- nonlinear stage: 2x-oversampled soft saturation ---
    const float drive = 1.0f + 2.5f * pHard;
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto up = oversampler->processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer (ch);
            for (size_t n = 0; n < up.getNumSamples(); ++n)
                d[n] = std::tanh (d[n] * drive) / drive;
        }
        oversampler->processSamplesDown (block);
    }

    // master gain + DC-safe trim + stereo widening
    for (int i = 0; i < numSamples; ++i)
    {
        widener.process (L[i], R[i], pWidth);
        const float g = outGain.getNextValue();
        L[i] *= g; R[i] *= g;
    }

    samplePos += numSamples;

    // decay the UI flashes
    bayanFlash.store (bayanFlash.load() * 0.85f);
    dayanFlash.store (dayanFlash.load() * 0.85f);
}

//==============================================================================
juce::AudioProcessorEditor* TablaAudioProcessor::createEditor()
{
    return new TablaAudioProcessorEditor (*this);
}

void TablaAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TablaAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TablaAudioProcessor();
}
