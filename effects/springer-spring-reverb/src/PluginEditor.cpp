#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SpringerAudioProcessorEditor::SpringerAudioProcessorEditor(SpringerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    auto addAttach = [&](juce::Slider& s, juce::String id) {
        attachments.push_back(std::make_unique<SliderAttachment>(audioProcessor.apvts, id, s));
        };

    // --- 1. Haupt-Knobs (Reihe 1 & 2) ---
    setupKnob(widthSlider);       addAttach(widthSlider, "width");
    setupKnob(resonanceSlider);   addAttach(resonanceSlider, "resonance");
    setupKnob(couplingSlider);    addAttach(couplingSlider, "coupling");
    setupKnob(dampingSlider);     addAttach(dampingSlider, "damping");
    setupKnob(spreadSlider);      addAttach(spreadSlider, "spread");

    setupKnob(numCoilsSlider);    addAttach(numCoilsSlider, "numCoils");
    setupKnob(chirpSlider);       addAttach(chirpSlider, "chirp");
    setupKnob(lfoRateSlider);     addAttach(lfoRateSlider, "lfoRate");
    setupKnob(lfoDepthSlider);    addAttach(lfoDepthSlider, "lfoDepth");
    setupKnob(densityASlider);    addAttach(densityASlider, "densityA"); // Pre-Diffuse
    setupKnob(densityBSlider);    addAttach(densityBSlider, "densityB"); // Tank-Diffuse

    // --- 2. Oben: Wet-Regler & Horizontale Slider ---
    setupKnob(wetSlider);         addAttach(wetSlider, "wet");

    auto setupHorizontal = [this](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 15);
        addAndMakeVisible(s);
        };
    setupHorizontal(numStagesSlider); addAttach(numStagesSlider, "numStages");
    setupHorizontal(pitchSlider);     addAttach(pitchSlider, "pitch");
    pitchSlider.setSkewFactorFromMidPoint(-12.0);
    pitchSlider.setColour(juce::Slider::thumbColourId, juce::Colours::aqua);

    // Alle Sichtbar machen
    addAndMakeVisible(widthSlider); addAndMakeVisible(resonanceSlider);
    addAndMakeVisible(couplingSlider); addAndMakeVisible(dampingSlider);
    addAndMakeVisible(spreadSlider); addAndMakeVisible(numCoilsSlider);
    addAndMakeVisible(chirpSlider); addAndMakeVisible(lfoRateSlider);
    addAndMakeVisible(lfoDepthSlider); addAndMakeVisible(densityASlider);
    addAndMakeVisible(densityBSlider); addAndMakeVisible(wetSlider);

    // --- 3. Reihe 3: Die 7 Coil-Delays ---
    for (int i = 0; i < 7; ++i)
    {
        setupKnob(coilSliders[i]);
        coilSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 15);
        addAttach(coilSliders[i], "coilLen" + juce::String(i + 1));
        addAndMakeVisible(coilSliders[i]);
    }

    // Buttons
    randomizeButton.setButtonText("Random\n Allpass");
    addAndMakeVisible(randomizeButton);
    randomizeButton.onClick = [this]() { audioProcessor.randomizeSprings(); };

    muteDryButton.setButtonText("Wet Only");
    muteDryButton.setClickingTogglesState(true);
    muteDryButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    muteDryAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "muteDry", muteDryButton);
    addAndMakeVisible(muteDryButton);

    setSize(650, 480);
}

SpringerAudioProcessorEditor::~SpringerAudioProcessorEditor() {}

void SpringerAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Hintergrund Gradienten
    juce::Colour topColor = juce::Colour(0xff00ffff);
    juce::Colour bottomColor = juce::Colour(0xff00c3cd);
    juce::ColourGradient gradient(topColor, 0.0f, 0.0f,
        bottomColor, 0.0f, (float)getHeight(),
        false);
    g.setFillType(juce::FillType(gradient));
    g.fillAll();

    // Branding
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(36.0f).boldened());
    g.drawFittedText("Springer", { 30, 25, 200, 40 }, juce::Justification::left, 1);
    g.setFont(14.0f);
    g.setColour(juce::Colours::darkcyan);
    g.drawFittedText("by aquanode", { 32, 60, 200, 20 }, juce::Justification::left, 1);
    g.setColour(juce::Colours::white);

    // Labels
    g.setFont(juce::Font(13.0f).boldened());
    auto drawLabel = [&](const juce::String& text, juce::Slider& s) {
        g.drawFittedText(text, s.getBounds().withY(s.getY() - 12).withHeight(15), juce::Justification::centredBottom, 1);
        };

    drawLabel("Size", widthSlider);
    drawLabel("Feedback", resonanceSlider);
    drawLabel("X-Couple", couplingSlider);
    drawLabel("Damping", dampingSlider);
    drawLabel("Spread", spreadSlider);

    drawLabel("Coils", numCoilsSlider);
    drawLabel("Chirp", chirpSlider);
    drawLabel("Mod Rate", lfoRateSlider);
    drawLabel("Mod Depth", lfoDepthSlider);
    drawLabel("Pre-Diffuse", densityASlider);
    drawLabel("Tank-Diffuse", densityBSlider);

    drawLabel("Mix", wetSlider); // Wet Knob oben

    // Coil Sektion
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawLine(20, 350, getWidth() - 20, 350, 1.0f);
    g.setColour(juce::Colours::white);
    g.drawFittedText("Individual Coil Delays (Density 1-7)", { 0, 355, getWidth(), 20 }, juce::Justification::centred, 1);

    g.drawFittedText("Pitch", { 340, 30, 60, 20 }, juce::Justification::right, 1);
    g.drawFittedText("Dispersion", { 340, 55, 60, 20 }, juce::Justification::right, 1);
}

void SpringerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // Top Header Bereich
    auto topRow = area.removeFromTop(85);
    auto slidersArea = topRow.removeFromRight(220).reduced(0, 10);
    pitchSlider.setBounds(slidersArea.removeFromTop(25));
    numStagesSlider.setBounds(slidersArea.removeFromTop(25));

    // Wet Knob links neben die horizontalen Slider schieben
    wetSlider.setBounds(topRow.removeFromRight(200).reduced(10));

    const int rowHeight = 120;
    auto row1 = area.removeFromTop(rowHeight);
    area.removeFromTop(5);
    auto row2 = area.removeFromTop(rowHeight);
    area.removeFromTop(35);
    auto row3 = area.removeFromTop(90);

    int colWidth = row1.getWidth() / 6;

    // Row 1 Layout
    widthSlider.setBounds(row1.removeFromLeft(colWidth).reduced(2));
    resonanceSlider.setBounds(row1.removeFromLeft(colWidth).reduced(2));
    couplingSlider.setBounds(row1.removeFromLeft(colWidth).reduced(2));
    dampingSlider.setBounds(row1.removeFromLeft(colWidth).reduced(2));
    spreadSlider.setBounds(row1.removeFromLeft(colWidth).reduced(2));
    auto btnArea = row1.removeFromLeft(colWidth).reduced(5, 15);
    muteDryButton.setBounds(btnArea.removeFromTop(30));
    randomizeButton.setBounds(btnArea.removeFromBottom(40));

    // Row 2 Layout
    numCoilsSlider.setBounds(row2.removeFromLeft(colWidth).reduced(2));
    chirpSlider.setBounds(row2.removeFromLeft(colWidth).reduced(2));
    lfoRateSlider.setBounds(row2.removeFromLeft(colWidth).reduced(2));
    lfoDepthSlider.setBounds(row2.removeFromLeft(colWidth).reduced(2));
    densityASlider.setBounds(row2.removeFromLeft(colWidth).reduced(2));
    densityBSlider.setBounds(row2.removeFromLeft(colWidth).reduced(2));

    // Row 3 Layout
    int coilWidth = row3.getWidth() / 7;
    for (int i = 0; i < 7; ++i)
        coilSliders[i].setBounds(row3.removeFromLeft(coilWidth).reduced(4));
}

void SpringerAudioProcessorEditor::setupKnob(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    slider.setMouseDragSensitivity(250);
}