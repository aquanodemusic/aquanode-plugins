#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PitchColorAudioProcessorEditor::PitchColorAudioProcessorEditor (PitchColorAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set the custom look and feel
    setLookAndFeel(&cyanLookAndFeel);
    
    // Set up title
    titleLabel.setText("Pitch Color - Bell Filter Bank", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(28.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);
    
    // Colors for the 12 notes (vibrant and cyan-friendly)
    std::array<juce::Colour, 12> noteColors = {
        juce::Colour(0xffff5555),  // C - Red
        juce::Colour(0xffff9955),  // C# - Orange
        juce::Colour(0xffffdd55),  // D - Yellow
        juce::Colour(0xffaaff55),  // D# - Yellow-Green
        juce::Colour(0xff55ff55),  // E - Green
        juce::Colour(0xff55ffaa),  // F - Cyan-Green
        juce::Colour(0xff55ffff),  // F# - Cyan
        juce::Colour(0xff55aaff),  // G - Sky Blue
        juce::Colour(0xff5555ff),  // G# - Blue
        juce::Colour(0xffaa55ff),  // A - Purple
        juce::Colour(0xffff55ff),  // A# - Magenta
        juce::Colour(0xffff55aa)   // B - Pink
    };
    
    // Set up gain sliders and labels
    for (int i = 0; i < 12; ++i)
    {
        auto& slider = gainSliders[i];
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
        slider.setColour(juce::Slider::rotarySliderFillColourId, noteColors[i]);
        slider.setColour(juce::Slider::thumbColourId, noteColors[i].brighter(0.5f));
        addAndMakeVisible(slider);
        
        gainAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameters(),
            juce::String("gain") + juce::String(i),
            slider);
        
        auto& label = gainLabels[i];
        label.setText(juce::String(audioProcessor.noteNames[i]) + " dB", juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(12.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, noteColors[i].brighter(0.3f));
        addAndMakeVisible(label);
    }
    
    // Set up Q sliders and labels
    for (int i = 0; i < 12; ++i)
    {
        auto& slider = qSliders[i];
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
        slider.setColour(juce::Slider::rotarySliderFillColourId, noteColors[i].darker(0.2f));
        slider.setColour(juce::Slider::thumbColourId, noteColors[i]);
        addAndMakeVisible(slider);
        
        qAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameters(),
            juce::String("q") + juce::String(i),
            slider);
        
        auto& label = qLabels[i];
        label.setText(juce::String(audioProcessor.noteNames[i]) + " Q", juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::Font(12.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, noteColors[i]);
        addAndMakeVisible(label);
    }
    
    // Set up start note control
    startNoteLabel.setText("Start Note", juce::dontSendNotification);
    startNoteLabel.setJustificationType(juce::Justification::centred);
    startNoteLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    startNoteLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(startNoteLabel);
    
    startNoteSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    startNoteSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    startNoteSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(startNoteSlider);
    
    startNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getParameters(), "startNote", startNoteSlider);
    
    // Set up end note control
    endNoteLabel.setText("End Note", juce::dontSendNotification);
    endNoteLabel.setJustificationType(juce::Justification::centred);
    endNoteLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    endNoteLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(endNoteLabel);
    
    endNoteSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    endNoteSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    endNoteSlider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00ffff));
    addAndMakeVisible(endNoteSlider);
    
    endNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getParameters(), "endNote", endNoteSlider);
    
    // Set up wet only button
    wetOnlyButton.setButtonText("Wet Only");
    addAndMakeVisible(wetOnlyButton);
    
    wetOnlyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getParameters(), "wetOnly", wetOnlyButton);
    
    setSize (1000, 450);
}

PitchColorAudioProcessorEditor::~PitchColorAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void PitchColorAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Cyan gradient background
    juce::ColourGradient gradient(juce::Colour(0xff0a4d68), 0, 0,
                                  juce::Colour(0xff05161a), 0, static_cast<float>(getHeight()),
                                  false);
    g.setGradientFill(gradient);
    g.fillAll();
    
    // Draw main panel with glass morphism effect
    auto drawPanel = [&](int x, int y, int width, int height)
    {
        juce::Rectangle<float> area(x, y, width, height);
        
        // Dark background with transparency
        g.setColour(juce::Colour(0xff0f3460).withAlpha(0.4f));
        g.fillRoundedRectangle(area, 10.0f);
        
        // Bright border
        g.setColour(juce::Colour(0xff00ffff).withAlpha(0.3f));
        g.drawRoundedRectangle(area, 10.0f, 2.0f);
        
        // Top highlight
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff00ffff).withAlpha(0.1f), area.getX(), area.getY(),
            juce::Colour(0xff00ffff).withAlpha(0.0f), area.getX(), area.getY() + 30,
            false));
        g.fillRoundedRectangle(area.getX(), area.getY(), area.getWidth(), 30, 10.0f);
    };
    
    drawPanel(20, 50, 960, 300);  // Main knobs panel
    drawPanel(20, 350, 960, 90); // Controls panel
}

void PitchColorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    
    // Title
    titleLabel.setBounds(area.removeFromTop(50).reduced(10));
    
    // Piano layout configuration
    // Notes: C=0, C#=1, D=2, D#=3, E=4, F=5, F#=6, G=7, G#=8, A=9, A#=10, B=11
    // Sharp notes (black keys): C#, D#, F#, G#, A# = 1, 3, 6, 8, 10
    // Natural notes (white keys): C, D, E, F, G, A, B = 0, 2, 4, 5, 7, 9, 11
    
    std::array<bool, 12> isSharp = { false, true, false, true, false, false, true, false, true, false, true, false };
    
    // Main knobs area
    auto knobsArea = area.removeFromTop(300).reduced(30, 15);
    
    int knobSize = 85;
    int labelHeight = 20;
    int verticalSpacing = 12;
    int knobColumnWidth = 80;
    
    // Calculate total width needed and center it
    int totalWidth = 12 * knobColumnWidth;
    int startX = knobsArea.getX() + (knobsArea.getWidth() - totalWidth) / 2;
    
    for (int i = 0; i < 12; ++i)
    {
        int x = startX + i * knobColumnWidth;
        
        // Sharp notes (black keys) are offset upward
        int yOffset = isSharp[i] ? 0 : 45;
        
        // Gain (dB) on top
        int gainLabelY = knobsArea.getY() + yOffset;
        int gainKnobY = gainLabelY + labelHeight;
        
        gainLabels[i].setBounds(x, gainLabelY, knobColumnWidth, labelHeight);
        gainSliders[i].setBounds(x + (knobColumnWidth - knobSize) / 2, gainKnobY, knobSize, knobSize);
        
        // Q below gain
        int qKnobY = gainKnobY + knobSize + verticalSpacing;
        int qLabelY = qKnobY + knobSize;
        
        qSliders[i].setBounds(x + (knobColumnWidth - knobSize) / 2, qKnobY, knobSize, knobSize);
        qLabels[i].setBounds(x, qLabelY, knobColumnWidth, labelHeight);
    }
    
    // Controls section
    auto controlArea = area.removeFromTop(80).reduced(30, 10);
    
    int controlWidth = controlArea.getWidth() / 3;
    
    // Start note
    auto startArea = controlArea.removeFromLeft(controlWidth);
    startNoteLabel.setBounds(startArea.removeFromTop(22));
    startArea.removeFromTop(-5);
    startNoteSlider.setBounds(startArea.reduced(30, 5));
    
    // End note
    auto endArea = controlArea.removeFromLeft(controlWidth);
    endNoteLabel.setBounds(endArea.removeFromTop(22));
    endArea.removeFromTop(-5);
    endNoteSlider.setBounds(endArea.reduced(30, 5));
    
    // Wet only button
    auto wetArea = controlArea;
    wetOnlyButton.setBounds(wetArea.getCentreX() - 60, wetArea.getCentreY() - 18, 120, 36);
}
