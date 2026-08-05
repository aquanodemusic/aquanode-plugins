#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Constructor for the plugin editor.
// Sets up all GUI components (sliders, buttons, labels) and attaches them
// to the processor parameters using the AudioProcessorValueTreeState.
//==============================================================================
EQResonatorAudioProcessorEditor::EQResonatorAudioProcessorEditor(EQResonatorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Custom color used for sliders, buttons, and labels
    auto sandColor = juce::Colour(0xffe8d3a3);

    // --- WET GAIN SLIDER ---
    // Rotary knob to control the wet/dry mix of the resonators
    wetGainSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wetGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    wetGainSlider.setColour(juce::Slider::rotarySliderFillColourId, sandColor);
    wetGainSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(wetGainSlider);

    // Attach the slider to the processor parameter "wetGain"
    wetGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "wetGain", wetGainSlider
    );

    // Label for the wet gain slider
    wetGainLabel.setText("WET\nGAIN", juce::dontSendNotification);
    wetGainLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(wetGainLabel);

    // Wet only button
    wetOnlyButton.setButtonText("WET ONLY");
    wetOnlyButton.setColour(juce::ToggleButton::tickColourId, sandColor);
    addAndMakeVisible(wetOnlyButton);
    wetOnlyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "wetOnly", wetOnlyButton);


    // --- NOTCH MODE BUTTON ---
    // Toggle button to enable/disable the "attenuate" (notch) mode
    attenuateButton.setButtonText("NOTCH MODE");
    attenuateButton.setColour(juce::ToggleButton::tickColourId, sandColor);
    addAndMakeVisible(attenuateButton);

    // Attach button to processor parameter "attenuate"
    attenuateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "attenuate", attenuateButton
    );

    // --- OCTAVE SELECTOR SLIDERS AND BUTTONS ---
    // Loop through 10 octaves to create toggle buttons and Q sliders
    for (int i = 0; i < 10; ++i)
    {
        // Octave enable button (1-10)
        octaveButtons[i].setButtonText(juce::String(i + 1));
        octaveButtons[i].setColour(juce::ToggleButton::tickColourId, sandColor);
        addAndMakeVisible(octaveButtons[i]);

        // Attach each button to the corresponding processor parameter (octave0 - octave9)
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, "octave" + juce::String(i), octaveButtons[i]
        ));

        // Q slider for the octave
        octaveQSliders[i].setSliderStyle(juce::Slider::LinearVertical);
        octaveQSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        octaveQSliders[i].setColour(juce::Slider::trackColourId, juce::Colours::black.withAlpha(0.3f));
        octaveQSliders[i].setColour(juce::Slider::thumbColourId, sandColor);
        addAndMakeVisible(octaveQSliders[i]);

        // Attach slider to the corresponding Q parameter in the processor
        qAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, "qOctave" + juce::String(i), octaveQSliders[i]
        ));

        // Label for the Q slider
        octaveQLabels[i].setText("Q", juce::dontSendNotification);
        octaveQLabels[i].setFont(10.0f);
        octaveQLabels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(octaveQLabels[i]);
    }

    // --- NATURAL NOTE BUTTONS (C, D, E, F, G, A, B) ---
    int natIndices[] = { 0, 2, 4, 5, 7, 9, 11 }; // Note IDs matching processor params
    juce::String natNames[] = { "C", "D", "E", "F", "G", "A", "B" };

    for (int i = 0; i < 7; ++i) {
        naturalButtons[i].setButtonText(natNames[i]);
        naturalButtons[i].setClickingTogglesState(true); // Acts as a toggle button
        naturalButtons[i].setColour(juce::ToggleButton::tickColourId, sandColor);
        addAndMakeVisible(naturalButtons[i]);

        // Attach button to processor parameter (note ID)
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, juce::String(natIndices[i]), naturalButtons[i]
        ));
    }

    // --- SHARP NOTE BUTTONS (C#, D#, F#, G#, A#) ---
    int shrpIndices[] = { 1, 3, 6, 8, 10 }; // Note IDs for sharps
    juce::String shrpNames[] = { "C#", "D#", "F#", "G#", "A#" };

    for (int i = 0; i < 5; ++i) {
        sharpButtons[i].setButtonText(shrpNames[i]);
        sharpButtons[i].setClickingTogglesState(true);
        sharpButtons[i].setColour(juce::ToggleButton::tickColourId, sandColor);
        addAndMakeVisible(sharpButtons[i]);

        // Attach button to processor parameter (note ID)
        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, juce::String(shrpIndices[i]), sharpButtons[i]
        ));
    }

    // --- FINAL EDITOR SETTINGS ---
    setSize(600, 540); // Set editor window size (wider to fit 10 octaves)
    startTimerHz(30);  // Start timer for GUI updates, 30 frames per second
}

//==============================================================================
// Destructor for the editor. Nothing special needed since smart pointers
// automatically clean up the attachments and controls.
//==============================================================================
EQResonatorAudioProcessorEditor::~EQResonatorAudioProcessorEditor() {}

//==============================================================================
// Draws the frequency visualizer at the top of the editor.
// Shows vertical lines for each active note in each active octave.
// Frequency is mapped logarithmically to fit the display.
//==============================================================================
void EQResonatorAudioProcessorEditor::drawFrequencyVisualizer(juce::Graphics& g)
{
    // Define the area at the top of the editor (120px height, padding applied)
    auto area = getLocalBounds().removeFromTop(120).reduced(15, 10);

    // Draw a semi-transparent black background rectangle for the visualizer
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);

    // Save graphics state and clip to the visualizer area
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(area);

    // Extract dimensions and offsets for easier calculations
    float w = (float)area.getWidth();
    float x_off = (float)area.getX();
    float h = (float)area.getHeight();
    float y_off = (float)area.getY();

    // Frequency mapping for 10 octaves (from ~15Hz to 20kHz)
    const float minFreq = 15.0f;
    const float maxFreq = 20000.0f;
    const float logMin = std::log10(minFreq);
    const float logScale = std::log10(maxFreq) - logMin;

    // Loop over all octaves
    for (int oct = 0; oct < 10; ++oct) // 10 octaves!
    {
        // Only draw if the octave is active
        if (audioProcessor.octaveActiveParams[oct]->load() > 0.5f)
        {
            // Get the Q (resonance) value for the octave and map it to 0-1 for color interpolation
            float currentQ = audioProcessor.octaveQParams[oct]->load();
            float qRatio = juce::jlimit(0.0f, 1.0f, (currentQ - 1.0f) / 99.0f);

            // Line color interpolates between cyan and white depending on Q
            auto lineColour = juce::Colours::cyan.interpolatedWith(juce::Colours::white, qRatio);

            // Loop over the 12 notes
            for (int n = 0; n < 12; ++n)
            {
                // Only draw if the note is active
                if (audioProcessor.noteParams[n]->load() > 0.5f)
                {
                    // Calculate the index in the noteFrequencies array
                    // Important: multiplier must match numOctaves in the processor
                    int idx = (n * 10) + oct;
                    float freq = audioProcessor.noteFrequencies[idx];

                    // Only draw if frequency is within display range
                    if (freq >= minFreq && freq <= maxFreq)
                    {
                        // Map frequency to x-position logarithmically
                        float x = x_off + w * ((std::log10(freq) - logMin) / logScale);

                        // Draw thicker semi-transparent line behind for glow effect
                        g.setColour(lineColour.withAlpha(0.5f));
                        g.drawLine(x, y_off, x, y_off + h, 3.0f);

                        // Draw main line
                        g.setColour(lineColour);
                        g.drawLine(x, y_off, x, y_off + h, 1.0f);
                    }
                }
            }
        }
    }
}

//==============================================================================
// Paints the overall background and static UI elements
//==============================================================================
void EQResonatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Create a gradient background from steelblue to darkcyan
    auto oceanDeep = juce::Colours::steelblue;
    juce::ColourGradient gradient(
        oceanDeep, 0.0f, 0.0f,
        juce::Colours::darkcyan, (float)getWidth(), (float)getHeight(),
        false
    );
    g.setGradientFill(gradient);
    g.fillAll();

    // Draw the frequency visualizer on top
    drawFrequencyVisualizer(g);

    // Top offset for UI labels
    int uiTopOffset = 160;

    // Plugin title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText("EQ / OCTAVE RESONATOR", 25, uiTopOffset, 400, 30, juce::Justification::left);

    // Author credit
    g.setFont(juce::Font(12.0f, juce::Font::italic));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawText("by aquanode", 28, uiTopOffset + 28, 200, 20, juce::Justification::left);

    // Labels for octave and note sections
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("OCTAVE SELECTOR", 25, uiTopOffset + 75, 200, 20, juce::Justification::left);
    g.drawText("NOTE SELECTOR", 25, uiTopOffset + 255, 200, 20, juce::Justification::left);

    // Horizontal divider lines for section separation
    g.setColour(juce::Colours::white.withAlpha(0.2f));
    g.drawHorizontalLine(uiTopOffset + 68, 20.0f, (float)getWidth() - 20.0f);
    g.drawHorizontalLine(uiTopOffset + 245, 20.0f, (float)getWidth() - 20.0f);
}

//==============================================================================
// Called when the editor is resized.
// Sets bounds for every slider, button, and label based on window size.
//==============================================================================
void EQResonatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Remove space used by the top UI offset (matches paint() top labels)
    area.removeFromTop(140);

    auto mainArea = area.reduced(20, 10);

    // --- HEADER SECTION: WET & NOTCH ---
    // Header area
    auto headerArea = mainArea.removeFromTop(70);

    // Wet Gain Slider stays on the right
    auto wetGainArea = headerArea.removeFromRight(140);
    wetGainSlider.setBounds(wetGainArea.removeFromLeft(65).withSizeKeepingCentre(65, 65));
    wetGainLabel.setBounds(wetGainSlider.getRight() + 5, wetGainSlider.getY(), 60, 65);

    // --- NOTCH MODE ---
    int notchHeight = 20; // height of the checkbox
    int notchY = headerArea.getY() + 5; // move a bit down from top of header area
    attenuateButton.setBounds(headerArea.removeFromRight(110).withSizeKeepingCentre(100, notchHeight));
    attenuateButton.setTopLeftPosition(attenuateButton.getX(), notchY); // shift up

    // --- WET ONLY ---
    int wetOnlyHeight = 20;
    int wetOnlyY = notchY + notchHeight + 5; // directly under NOTCH MODE
    wetOnlyButton.setBounds(attenuateButton.getX(), wetOnlyY, 100, wetOnlyHeight);

    mainArea.removeFromTop(40); // Space for "OCTAVE SELECTOR" text

    // --- OCTAVE CONTROLS (10 columns) ---
    auto octSection = mainArea.removeFromTop(125);
    int colWidth = octSection.getWidth() / 10;
    for (int i = 0; i < 10; ++i) {
        auto column = octSection.removeFromLeft(colWidth);
        octaveButtons[i].setBounds(column.removeFromTop(25).withSizeKeepingCentre(42, 20));
        auto sliderArea = column.removeFromTop(75);
        octaveQSliders[i].setBounds(sliderArea.withSizeKeepingCentre(30, 75));
        octaveQLabels[i].setBounds(column.removeFromTop(15));
    }

    mainArea.removeFromTop(30); // Space for "NOTE SELECTOR" text

    // --- NOTE BUTTONS (keyboard layout) ---
    int whiteKeyW = 65; // Width of white keys
    int rowH = 35;      // Height of keys
    int startX = (getWidth() - (7 * whiteKeyW)) / 2; // Center keyboard horizontally
    int keyboardTop = getHeight() - 100;             // Vertical position of keyboard

    // Natural notes (white keys)
    for (int i = 0; i < 7; ++i) {
        naturalButtons[i].setBounds(startX + (i * whiteKeyW), keyboardTop + rowH + 5, whiteKeyW - 10, rowH);
    }

    // Sharp notes (black keys)
    int sharpXOffsets[] = { 0, 1, 3, 4, 5 }; // Position relative to white keys
    for (int i = 0; i < 5; ++i) {
        int xPos = startX + (sharpXOffsets[i] * whiteKeyW) + (whiteKeyW / 2);
        sharpButtons[i].setBounds(xPos, keyboardTop, whiteKeyW - 10, rowH);
    }
}

//==============================================================================
// Timer callback, called 30 times per second.
// Forces a repaint to update the frequency visualizer.
//==============================================================================
void EQResonatorAudioProcessorEditor::timerCallback() { repaint(); }