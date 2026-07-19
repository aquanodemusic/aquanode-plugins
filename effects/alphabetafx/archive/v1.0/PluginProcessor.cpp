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
}

// ==============================================================
//  Process
// ==============================================================
void AlphaBetaFXAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    // If input is mono, copy L→R so both channels have signal
    if (buffer.getNumChannels() == 1) {
        // Handled below; just treat R as a copy
    }

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    float drive = *apvts.getRawParameterValue(FXPID::DRV);
    float fcut  = *apvts.getRawParameterValue(FXPID::FCUT);
    float fres  = *apvts.getRawParameterValue(FXPID::FRES);
    int   ftyp  = (int)*apvts.getRawParameterValue(FXPID::FTYP);
    int   rtyp  = (int)*apvts.getRawParameterValue(FXPID::RTYP);

    float wet  = *apvts.getRawParameterValue(FXPID::HWET);
    float time = *apvts.getRawParameterValue(FXPID::HTIM);
    float rate = *apvts.getRawParameterValue(FXPID::HRAT);

    // Sync filter parameters
    ladderL.resCurve = rtyp;  ladderR.resCurve = rtyp;
    svfL.resCurve    = rtyp;  svfR.resCurve    = rtyp;

    // Drive gain: 1x at 0, 7x at 1.0; compensated so unity stays close to 0dB
    float driveGain = 1.0f + drive * 6.0f;
    float driveComp = 1.0f / (0.4f + drive * 0.6f + 0.6f);

    float* L = buffer.getWritePointer(0);
    float* R = (numChannels >= 2) ? buffer.getWritePointer(1) : buffer.getWritePointer(0);

    // Set filter params once per block (parameters don't change sample-by-sample)
    auto applyFilter = [&](MoogLadder& ladder, SVFilter& svf, float x) -> float {
        switch (ftyp) {
        case 0: ladder.setParams(fcut, fres); return ladder.process12(x);
        case 2: ladder.setParams(fcut, fres); return ladder.process24plus(x);
        case 3: svf.setParams(fcut, fres);    return svf.process(x).bp;
        case 4: svf.setParams(fcut, fres);    return svf.process(x).hp;
        default: ladder.setParams(fcut, fres); return ladder.process24(x);
        }
    };

    for (int i = 0; i < numSamples; ++i) {
        // Pre-filter soft saturation (drive)
        float l = tanhA(L[i] * driveGain) * driveComp;
        float r = tanhA(R[i] * driveGain) * driveComp;

        // Per-channel filtering
        L[i] = applyFilter(ladderL, svfL, l);
        R[i] = applyFilter(ladderR, svfR, r);
    }

    // Stereo chorus
    chorus.setWet(wet);
    chorus.setTime(time);
    chorus.setRate(rate);
    chorus.process(buffer);
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
