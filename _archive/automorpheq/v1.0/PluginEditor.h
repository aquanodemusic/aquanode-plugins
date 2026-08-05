#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ResponseCurveComponent : public juce::Component, public juce::Timer
{
public:
    ResponseCurveComponent(AutoMorphEQAudioProcessor& p) : audioProcessor(p)
    {
        startTimerHz(60);
    }

    ~ResponseCurveComponent() override
    {
        stopTimer();
    }

    void timerCallback() override
    {
        repaint();
    }

    void paint(juce::Graphics& g) override;

private:
    AutoMorphEQAudioProcessor& audioProcessor;
    float getMagnitudeForBand(int bandIndex, float frequency);
};

class FilterRowComponent : public juce::Component
{
public:
    FilterRowComponent(int rowIndex) : index(rowIndex)
    {
        auto setupSlider = [this](juce::Slider& slider, const juce::String& labelText, const juce::String& suffix)
            {
                addAndMakeVisible(slider);
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
                slider.setTextValueSuffix(suffix);

                auto* lbl = labels.add(new juce::Label());
                addAndMakeVisible(lbl);
                lbl->setText(labelText, juce::dontSendNotification);
                lbl->setFont(12.0f);
                lbl->setJustificationType(juce::Justification::centredLeft);
            };

        setupSlider(startFreqSlider, "Start Freq", " Hz");
        setupSlider(endFreqSlider, "End Freq", " Hz");
        setupSlider(startVolSlider, "Start Vol", "");
        setupSlider(endVolSlider, "End Vol", "");
        setupSlider(qSlider, "Q", ""); // Added Q Slider
        setupSlider(moveSpeedSlider, "Speed", " s");
        setupSlider(movePosSlider, "Pos", " %");

        movePosSlider.setEnabled(false);

        moveSpeedSlider.onValueChange = [this]()
            {
                if (moveSpeedSlider.getValue() <= 0.001)
                {
                    movePosSlider.setEnabled(true);
                    movePosSlider.setAlpha(1.0f);
                }
                else
                {
                    movePosSlider.setEnabled(false);
                    movePosSlider.setAlpha(0.5f);
                }
            };
    }

    void resized() override
    {
        auto area = getLocalBounds();
        int itemWidth = area.getWidth() / 7;

        juce::Slider* sliders[] = {
            &startFreqSlider, &endFreqSlider, &startVolSlider,
            &endVolSlider, &qSlider, &moveSpeedSlider, &movePosSlider
        };

        for (int i = 0; i < 7; ++i)
        {
            auto slot = area.removeFromLeft(itemWidth).reduced(2);
            if (i < labels.size())
            {
                labels[i]->setBounds(slot.removeFromTop(15));
            }
            sliders[i]->setBounds(slot);
        }
    }

    juce::Slider startFreqSlider, endFreqSlider;
    juce::Slider startVolSlider, endVolSlider;
    juce::Slider qSlider;
    juce::Slider moveSpeedSlider, movePosSlider;

private:
    int index;
    juce::OwnedArray<juce::Label> labels;
};

class AutoMorphEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    AutoMorphEQAudioProcessorEditor(AutoMorphEQAudioProcessor&);
    ~AutoMorphEQAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AutoMorphEQAudioProcessor& audioProcessor;
    ResponseCurveComponent visualizer;
    juce::OwnedArray<FilterRowComponent> filterRows;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;

    juce::ToggleButton wetOnlyButton{ "WET ONLY" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoMorphEQAudioProcessorEditor)
};