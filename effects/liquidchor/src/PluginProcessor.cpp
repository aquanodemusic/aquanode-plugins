/*
  ==============================================================================
    LiquidChor - Roland Juno BBD Chorus
    PluginProcessor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
    LiquidChorAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ── DELAY ──────────────────────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"time1", 1}, "Time 1",
        juce::NormalisableRange<float> (0.5f, 25.0f, 0.01f, 0.5f), 7.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"time2", 1}, "Time 2",
        juce::NormalisableRange<float> (0.5f, 25.0f, 0.01f, 0.5f), 14.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"feedback", 1}, "Feedback",
        juce::NormalisableRange<float> (0.0f, 0.95f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"hpass", 1}, "H Pass",
        juce::NormalisableRange<float> (20.0f, 3000.0f, 1.0f, 0.3f), 20.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"lpass", 1}, "L Pass",
        juce::NormalisableRange<float> (500.0f, 20000.0f, 1.0f, 0.4f), 20000.0f));

    // ── MODULATION ─────────────────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"startPhase", 1}, "Start Phase",
        juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"lrPhase", 1}, "LR Phase",
        juce::NormalisableRange<float> (0.0f, 360.0f, 1.0f), 90.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID {"lfoType", 1}, "LFO Type",
        juce::StringArray {"Sine", "Saw"}, 0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID {"tempoSync", 1}, "Tempo Sync", false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID {"syncDiv", 1}, "Sync Division",
        juce::StringArray {"1/16", "1/8", "1/4", "1/2", "1/1"}, 2));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"speed", 1}, "Speed",
        juce::NormalisableRange<float> (0.05f, 10.0f, 0.001f, 0.35f), 0.5f));

    // ── LEVELS ─────────────────────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID {"invertWet", 1}, "Invert Wet", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID {"noiseGate", 1}, "Noise Gate", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"noise", 1}, "Noise",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    // Noise sweep mode:
    //   0 = R→L  (original: LFO envelopes as-is)
    //   1 = L→R  (swap L/R envelopes)
    //   2 = Back & Forth  (L and R are opposite phase — alternating)
    //   3 = L Alt. / R Alt.  (L loud for first half-cycle, R for second)
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID {"noiseMode", 1}, "Noise Mode",
        juce::StringArray {"R \xe2\x86\x92 L", "L \xe2\x86\x92 R",
                           "Back & Forth", "L / R Alt."}, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"gain", 1}, "Gain",
        juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID {"mix", 1}, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    return { params.begin(), params.end() };
}

//==============================================================================
LiquidChorAudioProcessor::LiquidChorAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
#else
    :
#endif
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

LiquidChorAudioProcessor::~LiquidChorAudioProcessor() {}

//==============================================================================
void LiquidChorAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;

    delayBufSize = static_cast<int> (sampleRate * 0.15) + 32;
    delayBufL.assign (delayBufSize, 0.0f);
    delayBufR.assign (delayBufSize, 0.0f);
    writePos = 0;

    float startNorm = *apvts.getRawParameterValue ("startPhase") / 360.0f;
    float lrNorm    = *apvts.getRawParameterValue ("lrPhase")    / 360.0f;
    lfoPhaseL = startNorm;
    lfoPhaseR = startNorm + lrNorm;
    while (lfoPhaseR >= 1.0f) lfoPhaseR -= 1.0f;

    fbL = fbR = 0.0f;
    hpPrevInL = hpPrevInR = 0.0f;
    hpStateL  = hpStateR  = 0.0f;
    lpStateL  = lpStateR  = 0.0f;
    noiseLpL  = noiseLpR  = 0.0f;
    gateEnv   = 0.0f;
    wasPlaying = false;
}

void LiquidChorAudioProcessor::releaseResources()
{
    delayBufL.clear();
    delayBufR.clear();
    delayBufSize = 0;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool LiquidChorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}
#endif

//==============================================================================
float LiquidChorAudioProcessor::evaluateLfo (float phase, int type) noexcept
{
    if (type == 1) // Ascending sawtooth: -1 → +1
        return phase * 2.0f - 1.0f;
    return std::sin (phase * juce::MathConstants<float>::twoPi);
}

float LiquidChorAudioProcessor::readDelayInterp (const std::vector<float>& buf,
                                                  float delaySamples) const noexcept
{
    float readF = static_cast<float> (writePos) - delaySamples;
    float sizeF = static_cast<float> (delayBufSize);
    while (readF < 0.0f) readF += sizeF;

    int   i0   = static_cast<int> (readF) % delayBufSize;
    float frac = readF - std::floor (readF);
    int   i1   = (i0 + 1) % delayBufSize;

    return buf[i0] + frac * (buf[i1] - buf[i0]);
}

//==============================================================================
void LiquidChorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn  = getTotalNumInputChannels();
    const int totalOut = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (totalIn < 1 || delayBufSize == 0) return;

    // ── Read parameters (once per block) ───────────────────────────────────
    const float time1Ms     = *apvts.getRawParameterValue ("time1");
    const float time2Ms     = *apvts.getRawParameterValue ("time2");
    const float feedbackAmt = *apvts.getRawParameterValue ("feedback");
    const float hpassFreq   = *apvts.getRawParameterValue ("hpass");
    const float lpassFreq   = *apvts.getRawParameterValue ("lpass");
    const float startPhNorm = *apvts.getRawParameterValue ("startPhase") / 360.0f;
    const float lrPhNorm    = *apvts.getRawParameterValue ("lrPhase")    / 360.0f;
    const int   lfoType     = static_cast<int> (*apvts.getRawParameterValue ("lfoType"));
    const bool  tempoSync   = *apvts.getRawParameterValue ("tempoSync") > 0.5f;
    const int   syncDivIdx  = static_cast<int> (*apvts.getRawParameterValue ("syncDiv"));
    const float speed       = *apvts.getRawParameterValue ("speed");
    const bool  invertWet   = *apvts.getRawParameterValue ("invertWet")  > 0.5f;
    const bool  noiseGateOn = *apvts.getRawParameterValue ("noiseGate") > 0.5f;
    const float noiseAmt    = *apvts.getRawParameterValue ("noise");
    const int   noiseMode   = static_cast<int> (*apvts.getRawParameterValue ("noiseMode"));
    const float gainLin     = juce::Decibels::decibelsToGain (
                               static_cast<float> (*apvts.getRawParameterValue ("gain")));
    const float mix         = *apvts.getRawParameterValue ("mix");

    // ── LFO frequency ─────────────────────────────────────────────────────
    float lfoHz = speed;

    if (tempoSync)
    {
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                const float beatDivs[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                if (auto bpm = pos->getBpm())
                    lfoHz = static_cast<float> (*bpm) / 60.0f
                            / beatDivs[juce::jlimit (0, 4, syncDivIdx)];

                if (auto playing = pos->getIsPlaying())
                {
                    if (playing && !wasPlaying)
                    {
                        lfoPhaseL = startPhNorm;
                        lfoPhaseR = startPhNorm + lrPhNorm;
                        while (lfoPhaseR >= 1.0f) lfoPhaseR -= 1.0f;
                    }
                    wasPlaying = playing;
                }
            }
        }
    }

    lfoHz = juce::jlimit (0.001f, 30.0f, lfoHz);

    // ── Pre-compute per-block constants ───────────────────────────────────
    const float fs      = static_cast<float> (currentSampleRate);
    const float lfoInc  = lfoHz / fs;
    const float ms2smp  = fs / 1000.0f;

    // High-pass coefficients
    const float hpTau   = 1.0f / (juce::MathConstants<float>::twoPi
                                   * juce::jmax (hpassFreq, 1.0f));
    const float hpAlpha = hpTau / (hpTau + 1.0f / fs);

    // Low-pass coefficient (on wet signal)
    const float lpAlpha = std::exp (-juce::MathConstants<float>::twoPi
                                    * juce::jmax (lpassFreq, 20.0f) / fs);

    // Noise LP (warm hiss ~2.5 kHz)
    const float noiseLpAlpha = std::exp (-juce::MathConstants<float>::twoPi
                                         * 2500.0f / fs);
    const float noiseScale = 0.018f;

    // Noise gate coefficients
    const float gateAttack  = std::exp (-1.0f / (0.005f * fs));
    const float gateRelease = std::exp (-1.0f / (0.25f  * fs));
    const float gateThresh  = 0.0003f;

    // ── Stereo pointers ───────────────────────────────────────────────────
    const bool  stereoIn  = (totalIn  >= 2);
    const bool  stereoOut = (totalOut >= 2);
    float* dataL = buffer.getWritePointer (0);
    float* dataR = stereoOut ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    if (!stereoIn && stereoOut)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    // ── Sample loop ───────────────────────────────────────────────────────
    const int numSamples = buffer.getNumSamples();

    for (int s = 0; s < numSamples; ++s)
    {
        const float dryL = dataL[s];
        const float dryR = stereoIn ? dataR[s] : dryL;

        // LFO values in [-1, +1]
        const float lL = evaluateLfo (lfoPhaseL, lfoType);
        const float lR = evaluateLfo (lfoPhaseR, lfoType);

        // Modulated delay times
        float dtL = (time1Ms + (time2Ms - time1Ms) * (0.5f + 0.5f * lL)) * ms2smp;
        float dtR = (time1Ms + (time2Ms - time1Ms) * (0.5f + 0.5f * lR)) * ms2smp;
        dtL = juce::jlimit (1.0f, static_cast<float> (delayBufSize - 2), dtL);
        dtR = juce::jlimit (1.0f, static_cast<float> (delayBufSize - 2), dtR);

        delayBufL[writePos] = dryL + fbL * feedbackAmt;
        delayBufR[writePos] = dryR + fbR * feedbackAmt;

        float wetL = readDelayInterp (delayBufL, dtL);
        float wetR = readDelayInterp (delayBufR, dtR);

        fbL = wetL;
        fbR = wetR;

        // ── High-pass filter on wet signal ──────────────────────────────
        float newHpL = hpAlpha * (hpStateL + wetL - hpPrevInL);
        hpPrevInL = wetL;
        hpStateL  = newHpL;
        wetL      = newHpL;

        float newHpR = hpAlpha * (hpStateR + wetR - hpPrevInR);
        hpPrevInR = wetR;
        hpStateR  = newHpR;
        wetR      = newHpR;

        // ── Low-pass filter on wet signal ───────────────────────────────
        // 1-pole IIR: y[n] = alpha * y[n-1] + (1 - alpha) * x[n]
        lpStateL = lpAlpha * lpStateL + (1.0f - lpAlpha) * wetL;
        wetL     = lpStateL;
        lpStateR = lpAlpha * lpStateR + (1.0f - lpAlpha) * wetR;
        wetR     = lpStateR;

        // ── Invert wet ──────────────────────────────────────────────────
        if (invertWet) { wetL = -wetL; wetR = -wetR; }

        // ── LFO-swept noise with selectable sweep mode ──────────────────
        const float rawNL = rng.nextFloat() * 2.0f - 1.0f;
        const float rawNR = rng.nextFloat() * 2.0f - 1.0f;
        noiseLpL = noiseLpAlpha * noiseLpL + (1.0f - noiseLpAlpha) * rawNL;
        noiseLpR = noiseLpAlpha * noiseLpR + (1.0f - noiseLpAlpha) * rawNR;

        float noiseEnvL, noiseEnvR;

        switch (noiseMode)
        {
            case 1: // L → R  (swap envelopes)
                noiseEnvL = 0.5f + 0.5f * lR;
                noiseEnvR = 0.5f + 0.5f * lL;
                break;

            case 2: // Back & Forth  (channels are opposite phase)
                noiseEnvL = 0.5f + 0.5f * lL;
                noiseEnvR = 0.5f - 0.5f * lL;   // anti-phase: one loud, other quiet
                break;

            case 3: // L / R Alternating  (L fills first half-cycle, R second)
            {
                const float ph = lfoPhaseL;   // 0..1
                if (ph < 0.5f)
                {
                    // L half: rises then falls (half-sine)
                    noiseEnvL = std::sin (ph * juce::MathConstants<float>::twoPi);
                    noiseEnvR = 0.0f;
                }
                else
                {
                    noiseEnvL = 0.0f;
                    noiseEnvR = std::sin ((ph - 0.5f) * juce::MathConstants<float>::twoPi);
                }
                break;
            }

            default: // 0: R → L  (original behaviour)
                noiseEnvL = 0.5f + 0.5f * lL;
                noiseEnvR = 0.5f + 0.5f * lR;
                break;
        }

        const float nL = noiseLpL * noiseEnvL * noiseAmt * noiseScale;
        const float nR = noiseLpR * noiseEnvR * noiseAmt * noiseScale;

        // ── Wet/dry mix ─────────────────────────────────────────────────
        float outL = dryL * (1.0f - mix) + wetL * mix + nL;
        float outR = dryR * (1.0f - mix) + wetR * mix + nR;

        outL *= gainLin;
        outR *= gainLin;

        // ── Noise gate (keyed by dry input level) ───────────────────────
        if (noiseGateOn)
        {
            const float inputLevel = (std::abs (dryL) + std::abs (dryR)) * 0.5f;
            if (inputLevel > gateEnv)
                gateEnv = gateAttack  * gateEnv + (1.0f - gateAttack)  * inputLevel;
            else
                gateEnv = gateRelease * gateEnv + (1.0f - gateRelease) * inputLevel;

            const float gateGain = juce::jmin (gateEnv / gateThresh, 1.0f);
            outL *= gateGain;
            outR *= gateGain;
        }

        writePos = (writePos + 1) % delayBufSize;

        lfoPhaseL += lfoInc;
        lfoPhaseR += lfoInc;
        if (lfoPhaseL >= 1.0f) lfoPhaseL -= 1.0f;
        if (lfoPhaseR >= 1.0f) lfoPhaseR -= 1.0f;

        dataL[s] = outL;
        if (stereoOut) dataR[s] = outR;
    }
}

//==============================================================================
bool LiquidChorAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* LiquidChorAudioProcessor::createEditor()
{
    return new LiquidChorAudioProcessorEditor (*this);
}

//==============================================================================
const juce::String LiquidChorAudioProcessor::getName() const { return JucePlugin_Name; }
bool LiquidChorAudioProcessor::acceptsMidi()  const { return false; }
bool LiquidChorAudioProcessor::producesMidi() const { return false; }
bool LiquidChorAudioProcessor::isMidiEffect() const { return false; }
double LiquidChorAudioProcessor::getTailLengthSeconds() const { return 0.5; }

int  LiquidChorAudioProcessor::getNumPrograms()                          { return 1; }
int  LiquidChorAudioProcessor::getCurrentProgram()                       { return 0; }
void LiquidChorAudioProcessor::setCurrentProgram (int)                   {}
const juce::String LiquidChorAudioProcessor::getProgramName (int)        { return {}; }
void LiquidChorAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void LiquidChorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LiquidChorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LiquidChorAudioProcessor();
}
