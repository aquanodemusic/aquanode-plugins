#include "PluginProcessor.h"
#include "PluginEditor.h"

float ResponseCurveComponent::getMagnitudeForBand(int i, float frequency)
{
    juce::String suffix = "_" + juce::String(i);

    float startFreq = audioProcessor.apvts.getRawParameterValue("start_freq" + suffix)->load();
    float endFreq = audioProcessor.apvts.getRawParameterValue("end_freq" + suffix)->load();
    float startVol = audioProcessor.apvts.getRawParameterValue("start_vol" + suffix)->load();
    float endVol = audioProcessor.apvts.getRawParameterValue("end_vol" + suffix)->load();
    float currentQ = audioProcessor.apvts.getRawParameterValue("q" + suffix)->load();

    float rawPhase = audioProcessor.getMorphPosition(i);
    float morph = (float)(0.5 * (1.0 - std::cos(juce::MathConstants<double>::twoPi * rawPhase)));

    float speed = audioProcessor.apvts.getRawParameterValue("speed_" + juce::String(i))->load();
    if (speed <= 0.001f) {
        float manualPos = audioProcessor.apvts.getRawParameterValue("pos_" + juce::String(i))->load();
        morph = manualPos / 100.0f;
    }

    float curFreq = startFreq + (endFreq - startFreq) * morph;
    float curVol = juce::jmax(0.0001f, startVol + (endVol - startVol) * morph);

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(44100.0, curFreq, currentQ, curVol);
    return coeffs->getMagnitudeForFrequency(frequency, 44100.0);
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.1f));

    g.setColour(juce::Colours::darkgrey.withAlpha(0.5f));
    for (float f = 20.0f; f < 20000.0f; f *= 2.0f)
    {
        float x = (std::log(f / 20.0f) / std::log(20000.0f / 20.0f)) * getWidth();
        g.drawVerticalLine((int)x, 0.0f, (float)getHeight());
    }

    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawHorizontalLine(getHeight() / 2, 0.0f, (float)getWidth());
    g.setColour(juce::Colours::cyan);

    juce::Path curve;
    const int pixels = getWidth();

    for (int p = 0; p < pixels; ++p)
    {
        float normX = (float)p / (float)pixels;
        float freq = 20.0f * std::pow(20000.0f / 20.0f, normX);
        float totalGainLinear = 1.0f;

        for (int i = 0; i < 7; ++i)
        {
            totalGainLinear *= getMagnitudeForBand(i, freq);
        }
        float db = juce::Decibels::gainToDecibels(totalGainLinear);
        float normY = 0.5f - (db / 48.0f);
        float y = normY * getHeight();

        if (p == 0) curve.startNewSubPath((float)p, y);
        else        curve.lineTo((float)p, y);
    }

    g.strokePath(curve, juce::PathStrokeType(2.0f));
    g.drawRect(getLocalBounds(), 1);
}

AutoMorphEQAudioProcessorEditor::AutoMorphEQAudioProcessorEditor(AutoMorphEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), visualizer(p)
{
    addAndMakeVisible(visualizer);

    // Setup Wet Only Button
    addAndMakeVisible(wetOnlyButton);
    wetOnlyButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    wetOnlyButton.setColour(juce::ToggleButton::tickColourId, juce::Colours::cyan);
    wetOnlyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "wet_only", wetOnlyButton);

    for (int i = 0; i < 7; ++i)
    {
        auto* row = filterRows.add(new FilterRowComponent(i));
        addAndMakeVisible(row);

        juce::String s = "_" + juce::String(i);
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "start_freq" + s, row->startFreqSlider));
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "end_freq" + s, row->endFreqSlider));
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "start_vol" + s, row->startVolSlider));
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "end_vol" + s, row->endVolSlider));
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "q" + s, row->qSlider));
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "speed" + s, row->moveSpeedSlider));
        attachments.push_back(std::make_unique<SliderAttachment>(p.apvts, "pos" + s, row->movePosSlider));
    }

    setSize(1000, 700);
}

AutoMorphEQAudioProcessorEditor::~AutoMorphEQAudioProcessorEditor()
{
}

void AutoMorphEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient backgroundGradient(
        juce::Colour(0xff00eedd), 0.0f, 0.0f,
        juce::Colour(0xff009999), 0.0f, (float)getHeight(),
        false);
    g.setGradientFill(backgroundGradient);
    g.fillAll();
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("AutoMorphEQ by aquanode | For Manual Position Mode set Speed = 0", 0, 5, getWidth(), 30, juce::Justification::centred, true);

    g.setColour(juce::Colours::cyan.withAlpha(0.3f));
    g.drawHorizontalLine(38, 0.0f, (float)getWidth());
}

void AutoMorphEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Position the Wet Only button in the top right corner (header area)
    auto headerArea = area.removeFromTop(40);
    wetOnlyButton.setBounds(headerArea.removeFromRight(120).reduced(5));

    visualizer.setBounds(area.removeFromTop(200).reduced(10));

    int rowHeight = area.getHeight() / 7;
    for (auto* row : filterRows)
    {
        row->setBounds(area.removeFromTop(rowHeight).reduced(5, 2));
    }
}