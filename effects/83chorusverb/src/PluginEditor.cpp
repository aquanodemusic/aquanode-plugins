/*
    PluginEditor.cpp  -  83ChorusVerb

    GPLv3.
*/
#include "PluginEditor.h"

ChorusVerbAudioProcessorEditor::ChorusVerbAudioProcessorEditor (ChorusVerbAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lnf);

    fxPanel = std::make_unique<cv83ui::FxPanel> (processor.apvts);
    addAndMakeVisible (*fxPanel);

    setResizable (true, true);
    setResizeLimits (760, 360, 1900, 900);
    getConstrainer()->setFixedAspectRatio (1195.0 / 505.0);

    setSize (1195, 505);
}

ChorusVerbAudioProcessorEditor::~ChorusVerbAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ChorusVerbAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (cv83ui::col::bg);
}

void ChorusVerbAudioProcessorEditor::resized()
{
    fxPanel->setBounds (getLocalBounds().reduced (10));
}
