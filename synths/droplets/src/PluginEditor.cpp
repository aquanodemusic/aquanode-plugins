#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
DropletsAudioProcessorEditor::DropletsAudioProcessorEditor(DropletsAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // --- Setup Lambda for Knobs ---
    auto setupKnob = [this](juce::Slider& s, juce::String paramID, std::unique_ptr<SliderAttachment>& attach)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF60A5FA));
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible(s);
        attach = std::make_unique<SliderAttachment>(audioProcessor.apvts, paramID, s);
    };

    // --- Setup Lambda for Faders ---
    auto setupFader = [this](juce::Slider& s, juce::String paramID, std::unique_ptr<SliderAttachment>& attach)
    {
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
        s.setColour(juce::Slider::trackColourId, juce::Colour(0xFF1E77AA));
        s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF1A3040));
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF60A5FA).withAlpha(0.3f));
        addAndMakeVisible(s);
        attach = std::make_unique<SliderAttachment>(audioProcessor.apvts, paramID, s);
    };

    // Top Row - Faders
    setupFader(radiusSlider, "radius", radiusAttach);
    setupFader(radiusBWSlider, "radiusBW", radiusBWAttach);
    setupFader(depthSlider, "depth", depthAttach);
    setupFader(depthBWSlider, "depthBW", depthBWAttach);
    setupFader(pitchRiseSlider, "pitchRise", pitchRiseAttach);
    setupFader(rateSlider, "rate", rateAttach);
    setupFader(rateBWSlider, "rateBW", rateBWAttach);
    setupFader(brightnessSlider, "brightness", brightnessAttach);
    setupFader(widthSlider, "width", widthAttach);
    setupFader(fieldSlider, "field", fieldAttach);

    // Second Row - Knobs for Modulation
    setupKnob(modAmountSlider, "modAmount", modAmountAttach);
    setupKnob(modAttackSlider, "modAttack", modAttackAttach);
    setupKnob(modDecaySlider, "modDecay", modDecayAttach);
    setupKnob(modSustainSlider, "modSustain", modSustainAttach);
    setupKnob(modReleaseSlider, "modRelease", modReleaseAttach);
    
    // Control knobs
    setupKnob(maxDropletsSlider, "maxDroplets", maxDropletsAttach);
    setupKnob(volumeSlider, "masterVol", volumeAttach);
    
    // Third Row - Global Volume ADSR
    setupKnob(volAttackSlider, "volAttack", volAttackAttach);
    setupKnob(volDecaySlider, "volDecay", volDecayAttach);
    setupKnob(volSustainSlider, "volSustain", volSustainAttach);
    setupKnob(volReleaseSlider, "volRelease", volReleaseAttach);
    
    // Advanced parameters
    setupKnob(secondaryProbSlider, "secondaryProb", secondaryProbAttach);
    setupKnob(secondaryDelaySlider, "secondaryDelay", secondaryDelayAttach);
    setupKnob(ampScaleSlider, "ampScale", ampScaleAttach);
    setupKnob(phaseOffsetSlider, "phaseOffset", phaseOffsetAttach);
    
    // Fourth Row - Per-Droplet ADSR (NEW!)
    setupKnob(dropletAttackSlider, "dropletAttack", dropletAttackAttach);
    setupKnob(dropletDecaySlider, "dropletDecay", dropletDecayAttach);
    setupKnob(dropletSustainSlider, "dropletSustain", dropletSustainAttach);
    setupKnob(dropletReleaseSlider, "dropletRelease", dropletReleaseAttach);

    // Toggle
    secondaryEventToggle.setButtonText("");
    secondaryEventToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xFF60A5FA));
    addAndMakeVisible(secondaryEventToggle);
    secondaryEventAttach = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "secondaryEvent", secondaryEventToggle);

    setSize(550, 520); // Increased height for fourth row
}

DropletsAudioProcessorEditor::~DropletsAudioProcessorEditor()
{
}

//==============================================================================
void DropletsAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background - dark blue-grey like original
    juce::Colour bgColour = juce::Colour(0xFF2A4858);
    g.fillAll(bgColour);

    // Outer border/frame
    g.setColour(juce::Colour(0xFF1A7080));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
    
    g.setColour(bgColour);
    g.fillRoundedRectangle(getLocalBounds().reduced(8).toFloat(), 8.0f);

    // Water droplet decorations in background
    juce::Colour dropletColour = juce::Colour(0xFF60A5FA).withAlpha(0.15f);
    
    // Draw some water droplet shapes
    auto drawDroplet = [&](float x, float y, float size)
    {
        juce::Path droplet;
        droplet.addEllipse(x, y + size * 0.2f, size * 0.8f, size * 0.8f);
        g.setColour(dropletColour);
        g.fillPath(droplet);
    };
    
    drawDroplet(450, 80, 60);
    drawDroplet(520, 140, 45);
    drawDroplet(480, 200, 50);
    drawDroplet(180, 200, 150);
    drawDroplet(80, 170, 200);
    drawDroplet(0, 0, 200);
    drawDroplet(270, -50, 70);
    drawDroplet(320, 150, 110);
    drawDroplet(320, 280, 270);

    // Labels - Top Row
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font("Arial", 9.0f, juce::Font::plain));

    auto drawTopLabel = [&](juce::String text, juce::Component& c)
    {
        g.drawText(text, c.getX(), c.getY() - 15, c.getWidth(), 12,
                   juce::Justification::centred);
    };

    drawTopLabel("SIZE", radiusSlider);
    drawTopLabel("BW", radiusBWSlider);
    drawTopLabel("DEPTH", depthSlider);
    drawTopLabel("BW", depthBWSlider);
    drawTopLabel("PITCH", pitchRiseSlider);
    drawTopLabel("RATE", rateSlider);
    drawTopLabel("BW", rateBWSlider);
    drawTopLabel("BRIGHT", brightnessSlider);
    drawTopLabel("WIDE", widthSlider);
    drawTopLabel("FIELD", fieldSlider);

    // Labels - Bottom Rows
    auto drawBottomLabel = [&](juce::String text, float x, float y, float w)
    {
        g.drawText(text, x, y, w, 12, juce::Justification::centred);
    };

    // Second row labels
    drawBottomLabel("MOD", modAmountSlider.getX(), modAmountSlider.getY() + 65, 50);
    drawBottomLabel("A", modAttackSlider.getX(), modAttackSlider.getY() + 65, 50);
    drawBottomLabel("D", modDecaySlider.getX(), modDecaySlider.getY() + 65, 50);
    drawBottomLabel("S", modSustainSlider.getX(), modSustainSlider.getY() + 65, 50);
    drawBottomLabel("R", modReleaseSlider.getX(), modReleaseSlider.getY() + 65, 50);
    
    // Right side labels
    drawBottomLabel("CPU", maxDropletsSlider.getX(), maxDropletsSlider.getY() + 65, 50);
    drawBottomLabel("AMP", volumeSlider.getX(), volumeSlider.getY() + 65, 50);
    
    // Secondary event toggle label
    g.drawText("SYNC", secondaryEventToggle.getX() - 35, secondaryEventToggle.getY() + 2, 30, 12,
               juce::Justification::right);
    
    // Third row labels - Volume ADSR
    drawBottomLabel("A", volAttackSlider.getX(), volAttackSlider.getY() + 65, 50);
    drawBottomLabel("D", volDecaySlider.getX(), volDecaySlider.getY() + 65, 50);
    drawBottomLabel("S", volSustainSlider.getX(), volSustainSlider.getY() + 65, 50);
    drawBottomLabel("R", volReleaseSlider.getX(), volReleaseSlider.getY() + 65, 50);
    
    // Advanced params labels
    g.setFont(juce::Font("Arial", 8.0f, juce::Font::plain));
    drawBottomLabel("PROB", secondaryProbSlider.getX(), secondaryProbSlider.getY() + 65, 50);
    drawBottomLabel("DELAY", secondaryDelaySlider.getX(), secondaryDelaySlider.getY() + 65, 50);
    drawBottomLabel("GAIN", ampScaleSlider.getX(), ampScaleSlider.getY() + 65, 50);
    drawBottomLabel("PHASE", phaseOffsetSlider.getX(), phaseOffsetSlider.getY() + 65, 50);
    
    // Fourth row labels - Per-Droplet Fade (SIMPLIFIED!)
    g.setFont(juce::Font("Arial", 8.5f, juce::Font::plain));
    drawBottomLabel("FADE", dropletAttackSlider.getX(), dropletAttackSlider.getY() + 65, 50);
    drawBottomLabel("IN", dropletAttackSlider.getX(), dropletAttackSlider.getY() + 75, 50);
    
    drawBottomLabel("FADE", dropletReleaseSlider.getX(), dropletReleaseSlider.getY() + 65, 50);
    drawBottomLabel("OUT", dropletReleaseSlider.getX(), dropletReleaseSlider.getY() + 75, 50);
    
    // Section headers
    g.setFont(juce::Font("Arial", 9.0f, juce::Font::bold));
    g.drawText("MOD ADSR", volAttackSlider.getX() - 10, modAttackSlider.getY() - 20, 
               modReleaseSlider.getRight() - modAttackSlider.getX() + 20, 12,
               juce::Justification::centred);
    
    g.drawText("NOTE ADSR", volAttackSlider.getX() - 10, volAttackSlider.getY() - 20, 
               volReleaseSlider.getRight() - volAttackSlider.getX() + 20, 12,
               juce::Justification::centred);
    
    g.drawText("BUBBLE FADE", dropletAttackSlider.getX() - 10, dropletAttackSlider.getY() - 20, 
               dropletReleaseSlider.getRight() - dropletAttackSlider.getX() + 20, 12,
               juce::Justification::centred);
    
    // Bottom text
    g.setFont(juce::Font("Arial", 10.0f, juce::Font::plain));
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.drawText("Droplets Stochastic Water Bubble Sound Generator by aquanode (inspired by xoxos Water, an old 32 bit plugin)", 
               20, getHeight() - 20, getWidth() - 40, 12,
               juce::Justification::centred);
}

void DropletsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(20); // Top margin

    // ===== TOP ROW: Faders with number boxes =====
    auto topRow = area.removeFromTop(155); // Increased to accommodate number boxes
    int faderWidth = 30;
    int faderHeight = 120;
    int faderSpacing = 51; // Slightly reduced to fit number boxes
    int startX = 18;

    radiusSlider.setBounds(startX, topRow.getY(), faderWidth, faderHeight);
    radiusBWSlider.setBounds(startX + faderSpacing, topRow.getY(), faderWidth, faderHeight);
    depthSlider.setBounds(startX + faderSpacing * 2, topRow.getY(), faderWidth, faderHeight);
    depthBWSlider.setBounds(startX + faderSpacing * 3, topRow.getY(), faderWidth, faderHeight);
    pitchRiseSlider.setBounds(startX + faderSpacing * 4, topRow.getY(), faderWidth, faderHeight);
    rateSlider.setBounds(startX + faderSpacing * 5, topRow.getY(), faderWidth, faderHeight);
    rateBWSlider.setBounds(startX + faderSpacing * 6, topRow.getY(), faderWidth, faderHeight);
    brightnessSlider.setBounds(startX + faderSpacing * 7, topRow.getY(), faderWidth, faderHeight);
    widthSlider.setBounds(startX + faderSpacing * 8, topRow.getY(), faderWidth, faderHeight);
    fieldSlider.setBounds(startX + faderSpacing * 9, topRow.getY(), faderWidth, faderHeight);

    area.removeFromTop(-5); // Negative spacer to bring rows closer

    // ===== SECOND ROW: 8 Items, Perfectly Equidistant =====
    auto secondRow = area.removeFromTop(100);
    int knobSize = 50;
    int toggleSize = 20;
    int knobY = secondRow.getY();

    // Calculate 8 equal slots across the usable width
    float usableWidth = (float)getWidth() - 40.0f;
    float slotWidth = usableWidth / 8.0f;

    // Helper to center a component within its assigned slot index
    auto getSlotX = [&](int index, int componentWidth) {
        float slotCenter = 20.0f + (index * slotWidth) + (slotWidth / 2.0f);
        return (int)(slotCenter - (componentWidth / 2.0f));
    };

    // Slot 0: MOD Amount
    modAmountSlider.setBounds(getSlotX(0, knobSize), knobY, knobSize, knobSize);

    // Slots 1-4: ADSR (A, D, S, R)
    modAttackSlider.setBounds(getSlotX(1, knobSize), knobY, knobSize, knobSize);
    modDecaySlider.setBounds(getSlotX(2, knobSize), knobY, knobSize, knobSize);
    modSustainSlider.setBounds(getSlotX(3, knobSize), knobY, knobSize, knobSize);
    modReleaseSlider.setBounds(getSlotX(4, knobSize), knobY, knobSize, knobSize);

    // Slot 5: SYNC Toggle (Centered vertically in knob row)
    secondaryEventToggle.setBounds(getSlotX(5, toggleSize), knobY + 15, toggleSize, toggleSize);

    // Slots 6-7: CPU and AMP
    maxDropletsSlider.setBounds(getSlotX(6, knobSize), knobY, knobSize, knobSize);
    volumeSlider.setBounds(getSlotX(7, knobSize), knobY, knobSize, knobSize);
    
    // ===== THIRD ROW: Volume ADSR + Advanced params =====
    area.removeFromTop(10); // Small spacer
    auto thirdRow = area.removeFromTop(100);
    int knobY3 = thirdRow.getY();
    
    // Reuse the slot system - 8 knobs evenly spaced
    float slotWidth3 = usableWidth / 8.0f;
    auto getSlotX3 = [&](int index, int componentWidth) {
        float slotCenter = 20.0f + (index * slotWidth3) + (slotWidth3 / 2.0f);
        return (int)(slotCenter - (componentWidth / 2.0f));
    };
    
    // Slots 0-3: Volume ADSR (A, D, S, R)
    volAttackSlider.setBounds(getSlotX3(0, knobSize), knobY3, knobSize, knobSize);
    volDecaySlider.setBounds(getSlotX3(1, knobSize), knobY3, knobSize, knobSize);
    volSustainSlider.setBounds(getSlotX3(2, knobSize), knobY3, knobSize, knobSize);
    volReleaseSlider.setBounds(getSlotX3(3, knobSize), knobY3, knobSize, knobSize);
    
    // Slots 4-7: Advanced params (Prob, Delay, Gain, Phase)
    secondaryProbSlider.setBounds(getSlotX3(4, knobSize), knobY3, knobSize, knobSize);
    secondaryDelaySlider.setBounds(getSlotX3(5, knobSize), knobY3, knobSize, knobSize);
    phaseOffsetSlider.setBounds(getSlotX3(6, knobSize), knobY3, knobSize, knobSize);
    ampScaleSlider.setBounds(getSlotX3(7, knobSize), knobY3, knobSize, knobSize);
    
    // ===== FOURTH ROW: Per-Droplet Fade In/Out (SIMPLIFIED - only 2 knobs) =====
    area.removeFromTop(10); // Small spacer
    auto fourthRow = area.removeFromTop(100);
    int knobY4 = fourthRow.getY();
    
    // Use same slot system - just first 2 slots for Fade In / Fade Out
    // Hide the unused Decay and Sustain knobs
    auto getSlotX4 = [&](int index, int componentWidth) {
        float slotCenter = 20.0f + (index * slotWidth) + (slotWidth / 2.0f);
        return (int)(slotCenter - (componentWidth / 2.0f));
    };
    
    // Slot 0: Fade In (Attack)
    dropletAttackSlider.setBounds(getSlotX4(0, knobSize), knobY4, knobSize, knobSize);
    
    // Slot 1: Fade Out (Release) 
    dropletReleaseSlider.setBounds(getSlotX4(1, knobSize), knobY4, knobSize, knobSize);
    
    // Hide unused Decay and Sustain knobs (not needed for simple fade)
    dropletDecaySlider.setBounds(0, 0, 0, 0);
    dropletSustainSlider.setBounds(0, 0, 0, 0);
}
