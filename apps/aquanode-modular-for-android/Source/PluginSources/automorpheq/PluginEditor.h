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
        setupSlider(qSlider, "Q", "");
        setupSlider(moveSpeedSlider, "Speed", " s");
        setupSlider(movePosSlider, "Pos", " %");

        // Add waveform combo box
        addAndMakeVisible(waveformCombo);
        waveformCombo.addItem("Sine", 1);
        waveformCombo.addItem("Triangle", 2);
        waveformCombo.addItem("Ramp Up", 3);
        waveformCombo.addItem("Ramp Down", 4);
        waveformCombo.addItem("Square", 5);
        waveformCombo.addItem("Tanh", 6);
        waveformCombo.addItem("Random", 7);

        auto* waveformLabel = labels.add(new juce::Label());
        addAndMakeVisible(waveformLabel);
        waveformLabel->setText("Wave", juce::dontSendNotification);
        waveformLabel->setFont(12.0f);
        waveformLabel->setJustificationType(juce::Justification::centredLeft);

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
        int itemWidth = area.getWidth() / 8; // Changed from 7 to 8 to accommodate waveform

        juce::Slider* sliders[] = {
            &startFreqSlider, &endFreqSlider, &startVolSlider,
            &endVolSlider, &qSlider, &moveSpeedSlider, &movePosSlider
        };

        for (int i = 0; i < 7; ++i)
        {
            auto slot = area.removeFromLeft(itemWidth).reduced(2);
            if (i < labels.size() - 1) // -1 because last label is for waveform
            {
                labels[i]->setBounds(slot.removeFromTop(15));
            }
            sliders[i]->setBounds(slot);
        }

        // Position waveform combo
        auto waveformSlot = area.removeFromLeft(itemWidth).reduced(2);
        labels[7]->setBounds(waveformSlot.removeFromTop(15));
        waveformCombo.setBounds(waveformSlot);
    }

    juce::Slider startFreqSlider, endFreqSlider;
    juce::Slider startVolSlider, endVolSlider;
    juce::Slider qSlider;
    juce::Slider moveSpeedSlider, movePosSlider;
    juce::ComboBox waveformCombo;

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
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::vector<std::unique_ptr<SliderAttachment>> attachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;

    juce::ToggleButton wetOnlyButton{ "WET ONLY" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wetOnlyAttachment;

    juce::TextButton randomizeButton{ "RANDOMIZE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoMorphEQAudioProcessorEditor)
};
