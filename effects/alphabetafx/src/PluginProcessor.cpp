#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================
//  Parameter Layout
// ==============================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AlphaBetaFXAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    juce::StringArray ftype { "LP12", "LP24", "LP24+", "BP", "HP" };
    juce::StringArray rcurve{ "Quadratic", "Cubic" };

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::DRV, "Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::FCUT, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.28f), 2000.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::FRES, "Filter Res",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        FXPID::FTYP, "Filter Type", ftype, 1));

    p.push_back(std::make_unique<juce::AudioParameterChoice>(
        FXPID::RTYP, "Res Curve", rcurve, 1));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::HWET, "Chorus Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::HTIM, "Chorus Time",
        juce::NormalisableRange<float>(1.0f, 50.0f), 15.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::HRAT, "Chorus Rate",
        juce::NormalisableRange<float>(0.1f, 10.0f), 0.5f));

    // ---- Filter Envelope ----
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        FXPID::EON, "Env On", false));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::EATK, "Env Attack",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.0f, 0.4f), 0.010f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::EDCY, "Env Decay",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.0f, 0.4f), 0.200f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::ESUS, "Env Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::EREL, "Env Release",
        juce::NormalisableRange<float>(0.001f, 4.0f, 0.0f, 0.4f), 0.300f));

    // Amount: -1..+1 — negative sweeps cutoff DOWN, positive sweeps UP (up to 3 octaves)
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::EAMT, "Env Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.5f));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        FXPID::EMID, "Env MIDI", false));  // false = AUTO, true = MIDI

    // Retrigger time: level-follower release constant for AUTO mode.
    // 5 ms = near-instant re-trigger; 500 ms = waits for near-silence.
    p.push_back(std::make_unique<juce::AudioParameterFloat>(
        FXPID::ERTG, "Env Retrigger",
        juce::NormalisableRange<float>(5.0f, 500.0f, 0.0f, 0.35f), 80.0f));

    return { p.begin(), p.end() };
}

// ==============================================================
//  Constructor
// ==============================================================
AlphaBetaFXAudioProcessor::AlphaBetaFXAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "AlphaBetaFX", createLayout())
{}

// ==============================================================
//  Prepare
// ==============================================================
void AlphaBetaFXAudioProcessor::prepareToPlay(double sr, int blockSize)
{
    float f = (float)sr;
    ladderL.setSampleRate(f);  ladderR.setSampleRate(f);
    svfL.setSampleRate(f);     svfR.setSampleRate(f);
    ladderL.reset(); ladderR.reset();
    svfL.reset();    svfR.reset();
    chorus.prepare(sr, blockSize);
    filterEnv.setSampleRate(f);
    filterEnv.reset();
}

// ==============================================================
//  Process
// ==============================================================
void AlphaBetaFXAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ---- Read parameters ----
    float drive = *apvts.getRawParameterValue(FXPID::DRV);
    float fcut  = *apvts.getRawParameterValue(FXPID::FCUT);
    float fres  = *apvts.getRawParameterValue(FXPID::FRES);
    int   ftyp  = (int)*apvts.getRawParameterValue(FXPID::FTYP);
    int   rtyp  = (int)*apvts.getRawParameterValue(FXPID::RTYP);

    float wet  = *apvts.getRawParameterValue(FXPID::HWET);
    float time = *apvts.getRawParameterValue(FXPID::HTIM);
    float rate = *apvts.getRawParameterValue(FXPID::HRAT);

    bool  envOn   = *apvts.getRawParameterValue(FXPID::EON)  > 0.5f;
    float envAtk  = *apvts.getRawParameterValue(FXPID::EATK);
    float envDcy  = *apvts.getRawParameterValue(FXPID::EDCY);
    float envSus  = *apvts.getRawParameterValue(FXPID::ESUS);
    float envRel  = *apvts.getRawParameterValue(FXPID::EREL);
    float envAmt  = *apvts.getRawParameterValue(FXPID::EAMT);
    bool  envMidi = *apvts.getRawParameterValue(FXPID::EMID) > 0.5f;
    float envRtg  = *apvts.getRawParameterValue(FXPID::ERTG);

    // ---- Sync filter / envelope settings ----
    ladderL.resCurve = rtyp;  ladderR.resCurve = rtyp;
    svfL.resCurve    = rtyp;  svfR.resCurve    = rtyp;
    filterEnv.setADSR(envAtk, envDcy, envSus, envRel);
    filterEnv.setMidiMode(envMidi);
    filterEnv.setRetriggerTime(envRtg);  // AUTO mode only; ignored in MIDI mode

    // ---- Drive gain ----
    float driveGain = 1.0f + drive * 6.0f;
    float driveComp = 1.0f / (0.4f + drive * 0.6f + 0.6f);

    float* L = buffer.getWritePointer(0);
    float* R = (numChannels >= 2) ? buffer.getWritePointer(1) : buffer.getWritePointer(0);

    // ---- Filter helper (per-sample, accepts modulated cutoff) ----
    auto applyFilter = [&](MoogLadder& ladder, SVFilter& svf,
                           float x, float cutoff) -> float
    {
        switch (ftyp) {
        case 0: ladder.setParams(cutoff, fres); return ladder.process12(x);
        case 2: ladder.setParams(cutoff, fres); return ladder.process24plus(x);
        case 3: svf.setParams(cutoff, fres);    return svf.process(x).bp;
        case 4: svf.setParams(cutoff, fres);    return svf.process(x).hp;
        default: ladder.setParams(cutoff, fres); return ladder.process24(x);
        }
    };

    // ---- Pre-collect MIDI events for sample-accurate processing ----
    // NOTE FOR FL STUDIO USERS: for MIDI to reach this buffer in Patcher,
    // your CMakeLists.txt MUST declare:  NEEDS_MIDI_INPUT TRUE
    // in the juce_add_plugin() call. acceptsMidi() alone is not enough for
    // FL Studio to route MIDI into an FX plugin's processBlock.
    //
    // Collect ALL note events regardless of current mode — mode can change
    // mid-session and pre-collecting has zero cost when MIDI is empty.
    // Also handles note-on velocity=0 (which some hosts send instead of note-off).
    struct MidiEvt { int samplePos; bool isNoteOn; float velocity; };
    juce::Array<MidiEvt> midiEvents;
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const bool velZeroNoteOn = msg.isNoteOn() && msg.getVelocity() == 0;
        if (msg.isNoteOn() && !velZeroNoteOn)
            midiEvents.add({ metadata.samplePosition, true,  msg.getFloatVelocity() });
        else if (msg.isNoteOff() || velZeroNoteOn)
            midiEvents.add({ metadata.samplePosition, false, 0.0f });
    }
    int midiIdx = 0;

    // ---- Per-sample loop ----
    for (int i = 0; i < numSamples; ++i)
    {
        // Fire any MIDI events that land on this exact sample
        while (midiIdx < midiEvents.size() && midiEvents[midiIdx].samplePos <= i)
        {
            if (envMidi)
            {
                if (midiEvents[midiIdx].isNoteOn)
                    filterEnv.noteOn(midiEvents[midiIdx].velocity);
                else
                    filterEnv.noteOff();
            }
            ++midiIdx;
        }

        // Level follower input (pre-drive, tracks original dynamics)
        float inputAbs = 0.5f * (std::abs(L[i]) + std::abs(R[i]));

        // Advance envelope state machine; get 0..1 output (velocity-scaled in MIDI mode)
        float envVal = filterEnv.process(inputAbs);

        // Compute modulated cutoff
        float modCut = fcut;
        if (envOn && envVal > 0.0001f) {
            float semitones = envAmt * 3.0f * envVal;  // ±3 octaves max
            modCut = fcut * std::exp2(semitones);
            modCut = juce::jlimit(20.0f, 20000.0f, modCut);
        }

        // Pre-filter saturation (drive)
        float l = tanhA(L[i] * driveGain) * driveComp;
        float r = tanhA(R[i] * driveGain) * driveComp;

        L[i] = applyFilter(ladderL, svfL, l, modCut);
        R[i] = applyFilter(ladderR, svfR, r, modCut);
    }

    // ---- Stereo chorus ----
    chorus.setWet(wet);
    chorus.setTime(time);
    chorus.setRate(rate);
    chorus.process(buffer);

    // Do NOT pass MIDI through (this is an FX plugin, not an instrument)
    midi.clear();
}

// ==============================================================
//  State
// ==============================================================
void AlphaBetaFXAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void AlphaBetaFXAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ==============================================================
//  Editor
// ==============================================================
juce::AudioProcessorEditor* AlphaBetaFXAudioProcessor::createEditor() {
    return new AlphaBetaFXAudioProcessorEditor(*this);
}

// ==============================================================
//  Plugin entry point
// ==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AlphaBetaFXAudioProcessor();
}
