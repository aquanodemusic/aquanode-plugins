#pragma once

#include <JuceHeader.h>

class CyanLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CyanLookAndFeel()
    {
        // Set default colors
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff00ffff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff008b8b));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a2e));
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff0f3460));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff00ffff));
        
        setColour(juce::Label::textColourId, juce::Colours::white);
        
        setColour(juce::ToggleButton::textColourId, juce::Colours::white);
        setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00ffff));
        setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff008b8b));
    }
    
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                         float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                         juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto lineW = juce::jmin(8.0f, radius * 0.5f);
        auto arcRadius = radius - lineW * 0.5f;
        
        // Background arc
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(bounds.getCentreX(),
                                   bounds.getCentreY(),
                                   arcRadius,
                                   arcRadius,
                                   0.0f,
                                   rotaryStartAngle,
                                   rotaryEndAngle,
                                   true);
        
        g.setColour(juce::Colour(0xff0f3460));
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        
        // Value arc with gradient
        if (toAngle > rotaryStartAngle)
        {
            juce::Path valueArc;
            valueArc.addCentredArc(bounds.getCentreX(),
                                  bounds.getCentreY(),
                                  arcRadius,
                                  arcRadius,
                                  0.0f,
                                  rotaryStartAngle,
                                  toAngle,
                                  true);
            
            auto fillColour = slider.findColour(juce::Slider::rotarySliderFillColourId);
            
            juce::ColourGradient gradient(fillColour.brighter(0.3f),
                                         bounds.getCentreX() - arcRadius,
                                         bounds.getCentreY(),
                                         fillColour.darker(0.3f),
                                         bounds.getCentreX() + arcRadius,
                                         bounds.getCentreY(),
                                         false);
            
            g.setGradientFill(gradient);
            g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        
        // Thumb (pointer)
        juce::Path thumb;
        auto thumbWidth = lineW * 1.0f;
        radius = thumbWidth * 0.5f;
        thumb.addEllipse(-radius,
            -arcRadius - radius,
            radius * 2.0f,
            radius * 2.0f);
        
        g.setColour(juce::Colour(0xff00ffff));
        g.fillPath(thumb, juce::AffineTransform::rotation(toAngle).translated(bounds.getCentreX(), bounds.getCentreY()));
        
        // Center circle
        g.setColour(juce::Colour(0xff16213e));
        g.fillEllipse(bounds.getCentreX() - 6, bounds.getCentreY() - 6, 12, 12);
        g.setColour(juce::Colour(0xff00ffff));
        g.drawEllipse(bounds.getCentreX() - 6, bounds.getCentreY() - 6, 12, 12, 1.5f);
    }
    
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(2);
        auto cornerSize = 5.0f;
        
        // Background
        g.setColour(button.getToggleState() ? juce::Colour(0xff00ffff).withAlpha(0.3f) : juce::Colour(0xff0f3460));
        g.fillRoundedRectangle(bounds, cornerSize);
        
        // Border
        g.setColour(button.getToggleState() ? juce::Colour(0xff00ffff) : juce::Colour(0xff008b8b));
        g.drawRoundedRectangle(bounds, cornerSize, 2.0f);
        
        // Text
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(button.getButtonText(), bounds, juce::Justification::centred);
    }
    
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* l = new juce::Label();
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, juce::Colours::white);
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::TextEditor::textColourId, juce::Colours::white);
        l->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0f3460).withAlpha(0.6f));
        l->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff00ffff).withAlpha(0.5f));
        l->setFont(juce::Font(12.0f, juce::Font::bold));
        return l;
    }
    
    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        // Only fill background if being edited
        if (label.isBeingEdited())
        {
            g.setColour(juce::Colour(0xff0f3460).withAlpha(0.6f));
            auto bounds = label.getLocalBounds().toFloat().reduced(2);
            g.fillEllipse(bounds);
            
            g.setColour(juce::Colour(0xff00ffff).withAlpha(0.5f));
            g.drawEllipse(bounds, 1.5f);
        }
        
        if (!label.isBeingEdited())
        {
            auto alpha = label.isEnabled() ? 1.0f : 0.5f;
            
            g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
            g.setFont(juce::Font(12.0f, juce::Font::bold));
            
            auto textArea = label.getBorderSize().subtractedFrom(label.getLocalBounds());
            g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
                           juce::jmax(1, (int)((float)textArea.getHeight() / 12.0f)),
                           label.getMinimumHorizontalScale());
        }
    }
};
