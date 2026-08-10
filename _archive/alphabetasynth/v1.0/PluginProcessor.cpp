#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================
//  Parameter Layout
// ==============================================================
juce::AudioProcessorValueTreeState::ParameterLayout
AlphaBetaAudioProcessor::createLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    juce::StringArray waves{ "Sine","Tri","Saw","Square","Noise" };
    juce::StringArray octs{ "-2","-1","0","+1","+2" };
    juce::StringArray ftype{ "LP12","LP24","LP24+","BP","HP" };

    // --- OSC 1 ---
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1WA, "OSC1 Wave A", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1OCA, "OSC1 Oct A", octs, 1));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1WB, "OSC1 Wave B", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O1OCB, "OSC1 Oct B", octs, 1));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1MRP, "OSC1 Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O1DET, "OSC1 Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 20.0f));

    // --- OSC 2 ---
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2WA, "OSC2 Wave A", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2OCA, "OSC2 Oct A", octs, 2));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2WB, "OSC2 Wave B", waves, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::O2OCB, "OSC2 Oct B", octs, 2));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2MRP, "OSC2 Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::O2DET, "OSC2 Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    // --- Mix ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::MIX, "OSC Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.375f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::DRV, "Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FM, "FM",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f, 0.4f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::SPR, "Spread",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));

    // --- Filter ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FCUT, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.28f), 250.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FRES, "Filter Res",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::FTYP, "Filter Type", ftype, 2));

    auto envR = juce::NormalisableRange<float>(0.001f, 10.0f, 0.0f, 0.4f);
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FATT, "F Attack", envR, 0.01f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FDEC, "F Decay", envR, 0.4f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FSUS, "F Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.6f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FREL, "F Release", envR, 0.6f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FFAD, "F Fade",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.65f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::FDEP, "F Depth",
        juce::NormalisableRange<float>(-1.0f, 1.0f), 0.55f));

    // --- Amp ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AVOL, "Amp Vol",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AVEL, "Amp Vel",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AATT, "A Attack", envR, 0.005f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::ADEC, "A Decay", envR, 0.25f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::ASUS, "A Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AREL, "A Release", envR, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::AFAD, "A Fade",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // --- Chorus ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::HWET, "Chorus Wet",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::HTIM, "Chorus Time",
        juce::NormalisableRange<float>(1.0f, 50.0f), 35.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::HRAT, "Chorus Rate",
        juce::NormalisableRange<float>(0.1f, 10.0f), 0.2f));

    // --- Glide ---
    p.push_back(std::make_unique<juce::AudioParameterFloat>(PID::GLID, "Glide",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.0f, 0.5f), 0.0f));

    // --- Resonance curve ---
    juce::StringArray rcurve{ "Quadratic", "Cubic" };
    p.push_back(std::make_unique<juce::AudioParameterChoice>(PID::RTYP, "Res Curve", rcurve, 1));

    return { p.begin(), p.end() };
}

// ==============================================================
//  Constructor
// ==============================================================
AlphaBetaAudioProcessor::AlphaBetaAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output",
        juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "AlphaBeta", createLayout())
{
    // Add 16 voices
    for (int i = 0; i < 16; ++i) {
        auto* v = new AlphaVoice();
        v->voiceIdx = i;
        synth.addVoice(v);
    }
    synth.addSound(new AlphaSound());
}

// ==============================================================
//  Prepare
// ==============================================================
void AlphaBetaAudioProcessor::prepareToPlay(double sr, int blockSize) {
    synth.setCurrentPlaybackSampleRate(sr);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<AlphaVoice*>(synth.getVoice(i)))
            v->prepareVoice(sr);

    chorus.prepare(sr, blockSize);
}

// ==============================================================
//  Process
// ==============================================================
static MonoOsc::Wave waveFromInt(int v) {
    switch (v) {
    case 1: return MonoOsc::TRI;
    case 2: return MonoOsc::SAW;
    case 3: return MonoOsc::SQUARE;
    case 4: return MonoOsc::NOISE;
    default: return MonoOsc::SINE;
    }
}

void AlphaBetaAudioProcessor::updateVoiceParams() {
    int   o1wa = (int)*apvts.getRawParameterValue(PID::O1WA);
    int   o1oa = (int)*apvts.getRawParameterValue(PID::O1OCA) - 2;
    int   o1wb = (int)*apvts.getRawParameterValue(PID::O1WB);
    int   o1ob = (int)*apvts.getRawParameterValue(PID::O1OCB) - 2;
    float o1m = *apvts.getRawParameterValue(PID::O1MRP);
    float o1d = *apvts.getRawParameterValue(PID::O1DET);

    int   o2wa = (int)*apvts.getRawParameterValue(PID::O2WA);
    int   o2oa = (int)*apvts.getRawParameterValue(PID::O2OCA) - 2;
    int   o2wb = (int)*apvts.getRawParameterValue(PID::O2WB);
    int   o2ob = (int)*apvts.getRawParameterValue(PID::O2OCB) - 2;
    float o2m = *apvts.getRawParameterValue(PID::O2MRP);
    float o2d = *apvts.getRawParameterValue(PID::O2DET);

    float mix = *apvts.getRawParameterValue(PID::MIX);
    float drv = *apvts.getRawParameterValue(PID::DRV);
    float fm = *apvts.getRawParameterValue(PID::FM);
    float spr = *apvts.getRawParameterValue(PID::SPR);
    float glide = *apvts.getRawParameterValue(PID::GLID);

    float fcut = *apvts.getRawParameterValue(PID::FCUT);
    float fres = *apvts.getRawParameterValue(PID::FRES);
    int   ftyp = (int)*apvts.getRawParameterValue(PID::FTYP);
    int   rtyp = (int)*apvts.getRawParameterValue(PID::RTYP);
    float fdep = *apvts.getRawParameterValue(PID::FDEP);

    ADSRFade::Params fep, aep;
    fep.attack = *apvts.getRawParameterValue(PID::FATT);
    fep.decay = *apvts.getRawParameterValue(PID::FDEC);
    fep.sustain = *apvts.getRawParameterValue(PID::FSUS);
    fep.release = *apvts.getRawParameterValue(PID::FREL);
    fep.fade = *apvts.getRawParameterValue(PID::FFAD);

    aep.attack = *apvts.getRawParameterValue(PID::AATT);
    aep.decay = *apvts.getRawParameterValue(PID::ADEC);
    aep.sustain = *apvts.getRawParameterValue(PID::ASUS);
    aep.release = *apvts.getRawParameterValue(PID::AREL);
    aep.fade = *apvts.getRawParameterValue(PID::AFAD);

    float avol = *apvts.getRawParameterValue(PID::AVOL);
    float avel = *apvts.getRawParameterValue(PID::AVEL);

    for (int i = 0; i < synth.getNumVoices(); ++i) {
        if (auto* v = dynamic_cast<AlphaVoice*>(synth.getVoice(i))) {
            v->o1wA = waveFromInt(o1wa); v->o1octA = o1oa;
            v->o1wB = waveFromInt(o1wb); v->o1octB = o1ob;
            v->o1morph = o1m;  v->o1detune = o1d;

            v->o2wA = waveFromInt(o2wa); v->o2octA = o2oa;
            v->o2wB = waveFromInt(o2wb); v->o2octB = o2ob;
            v->o2morph = o2m;  v->o2detune = o2d;

            v->oscMix = mix; v->drive = drv; v->fm = fm;
            v->stereoSpread = spr; v->glideTime = glide;

            v->filterCutoff = fcut; v->filterRes = fres;
            v->filterType = ftyp; v->resCurve = rtyp; v->filterDepth = fdep;
            v->fEnvP = fep;

            v->aEnvP = aep; v->ampVol = avol; v->ampVelSens = avel;
        }
    }
}

void AlphaBetaAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    updateVoiceParams();
    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());

    // Chorus
    float wet = *apvts.getRawParameterValue(PID::HWET);
    float time = *apvts.getRawParameterValue(PID::HTIM);
    float rate = *apvts.getRawParameterValue(PID::HRAT);
    chorus.setWet(wet);
    chorus.setTime(time);
    chorus.setRate(rate);
    chorus.process(buffer);

    // Stereo spread: M/S widening driven by spread param
    float spread = *apvts.getRawParameterValue(PID::SPR);
    if (spread > 0.001f && buffer.getNumChannels() >= 2) {
        float width = 1.0f + spread * 1.5f;
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            float m = (L[i] + R[i]) * 0.5f;
            float s = (L[i] - R[i]) * 0.5f * width;
            L[i] = m + s;
            R[i] = m - s;
        }
    }
}

// ==============================================================
//  State save/restore
// ==============================================================
void AlphaBetaAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}

void AlphaBetaAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// ==============================================================
//  Plugin entry point
// ==============================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AlphaBetaAudioProcessor();
}