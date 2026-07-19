#include "PluginProcessor.h"
// Includes the processor implementation so the editor can:
// - Access the AudioProcessor class
// - Read DSP state
// - Access the APVTS (parameter system)

#include "PluginEditor.h"
// Includes the editor header where the class is declared

// CONSTRUCTOR
BandpassModulatorAudioProcessorEditor::BandpassModulatorAudioProcessorEditor(BandpassModulatorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
    // '::' is the scope resolution operator.
    // It means: "BandpassModulatorAudioProcessorEditor that belongs to the global scope".
    // This is how we define member functions outside the class declaration.
    // ':' starts the member initializer list.
    // This runs BEFORE the constructor body.
    // AudioProcessorEditor(&p):
    // - Calls the base class constructor
    // - '&p' passes a POINTER to the processor
    // - The base class needs this to communicate with the host
    // audioProcessor(p):
    // - Initializes our reference member
    // - 'p' is passed by reference, not copied
{
    // Sets the initial window size (Width, Height)
    setSize(600, 500);

    // LAMBDA SLIDER SETUP
    // A 'Lambda' is a temporary function defined on the spot. 
    // This 'setupSlider' saves us from writing the same 10 lines of code for 8 different sliders.
    auto setupSlider = [this](juce::Slider& s, juce::Label& l, juce::String name, juce::DropShadowEffect& shadow, juce::Colour thumbColor) {
    // 'auto' lets the compiler deduce the type.
    // This is a lambda function (anonymous inline function).
    // It avoids repeating the same setup code for every slider.
    // [this] captures the current class instance,
    // allowing access to member functions like addAndMakeVisible().
    // juce::Slider& / juce::Label& are references:
    // - No copies
    // - Directly modifies the original components
        s.setSliderStyle(juce::Slider::LinearHorizontal); // Flat, left-to-right slider
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20); // Value box on the right
        // Customizing the colors using the 'thumbColor' passed into the lambda
        s.setColour(juce::Slider::thumbColourId, thumbColor);
        s.setColour(juce::Slider::trackColourId, juce::Colours::wheat); // Sand-colored track
        // Shadow: black with 50% opacity, 5px blur, offset by 3px right and 3px down
        shadow.setShadowProperties(juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 5, { 3, 3 }));
        // Configures a drop shadow:
        // - Semi-transparent black
        // - 5px blur
        // - Offset 3px right and down
        s.setComponentEffect(&shadow); // Apply the shadow to the component
        addAndMakeVisible(s); // Add slider to the window
        l.setText(name, juce::dontSendNotification); // Set label text
        addAndMakeVisible(l); // Add label to the window
        };

    //==============================================================================
    // IMPORTANT DESIGN NOTE ABOUT PARAMETERS
    //
    // We DO NOT set slider ranges, default values, or initial states here.
    // Even though we technically could.
    //
    // WHY?
    // Because parameters live in the APVTS in the processor.
    //
    // APVTS = AudioProcessorValueTreeState
    //
    // The APVTS:
    // - Owns all parameters
    // - Defines ranges, default values, and automation behavior
    // - Handles host automation, state recall, and preset saving
    //
    // The editor is ONLY a view/controller.
    // Attachments sync the UI to the APVTS automatically.
    //
    // This guarantees:
    // - One single source of truth
    // - Correct DAW recall
    // - Proper automation behavior

    //==============================================================================

    // INITIALIZE SLIDERS
    // Frequency/Timing sliders are Cyan (Ocean theme)
    setupSlider(minFreqSlider, minFreqLabel, "Min Freq", minFreqShadow, juce::Colours::cyan);
    setupSlider(maxFreqSlider, maxFreqLabel, "Max Freq", maxFreqShadow, juce::Colours::cyan);
    setupSlider(glideTimeSlider, glideTimeLabel, "Glide", glideTimeShadow, juce::Colours::cyan);
    setupSlider(stayTimeSlider, stayTimeLabel, "Stay", stayTimeShadow, juce::Colours::cyan);

    // Modulation sliders are White (Sea foam theme)
    setupSlider(panningSlider, panningLabel, "Pan", panningShadow, juce::Colours::white);
    setupSlider(dryWetSlider, dryWetLabel, "Dry/Wet", dryWetShadow, juce::Colours::white);
    setupSlider(widthSlider, widthLabel, "Pinch", widthShadow, juce::Colours::white);
    setupSlider(wetGainSlider, wetGainLabel, "Wet Gain", wetGainShadow, juce::Colours::white);

    // --- SIDEBAR & SWITCHES ---
    addAndMakeVisible(noteLockSwitch);
    noteLockSwitch.setButtonText("Note Lock");
    toggleShadow.setShadowProperties(juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 5, { 2, 2 }));
    noteLockSwitch.setComponentEffect(&toggleShadow);

    // Arrays make it easy to loop through the 7 musical note buttons
    juce::ToggleButton* noteBtns[] = { &btnC, &btnD, &btnE, &btnF, &btnG, &btnA, &btnB };
    juce::String noteNames[] = { "C", "D", "E", "F", "G", "A", "B" };

    for (int i = 0; i < 7; ++i) {
        addAndMakeVisible(noteBtns[i]);
        noteBtns[i]->setButtonText(noteNames[i]);
        // '->' is used because noteBtns[i] is a pointer
    }

    addAndMakeVisible(panningLfoSwitch);
    panningLfoSwitch.setButtonText("Panning LFO");

    // --- MODE SELECTOR (3-STATE DROPDOWN) ---
    addAndMakeVisible(modeSelector);
    modeSelector.addItem("Random", 1);
    modeSelector.addItem("Up", 2);
    modeSelector.addItem("Down", 3);

    // ComboBox Styling
    // ComboBox item IDs must start at 1 (JUCE rule)
    modeSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha(0.1f));
    modeSelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::darkturquoise.withAlpha(0.5f));
    modeSelector.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    modeSelector.setJustificationType(juce::Justification::centred);

    // LookAndFeel: Global UI changes for the popup menu
    getLookAndFeel().setColour(juce::PopupMenu::backgroundColourId, juce::Colours::darkcyan);
    getLookAndFeel().setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::cyan);
    getLookAndFeel().setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);

    // --- ATTACHMENTS (Connecting UI to DSP) ---
    // These link the UI components to the 'apvts' in the Processor using their IDs.
    minFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "minFreq", minFreqSlider);
    maxFreqAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "maxFreq", maxFreqSlider);
    glideTimeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "glideTime", glideTimeSlider);
    stayTimeAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "stayTime", stayTimeSlider);
    panningAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "panning", panningSlider);
    dryWetAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "dryWet", dryWetSlider);
    widthAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "width", widthSlider);
    lfoActiveAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "panningLfoActive", panningLfoSwitch);
    wetGainAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "wetGain", wetGainSlider);
    modeAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.apvts, "mode", modeSelector);
    noteLockAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteLockActive", noteLockSwitch);

    // Manual attachments for note buttons
    attC = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteC", btnC);
    attD = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteD", btnD);
    attE = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteE", btnE);
    attF = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteF", btnF);
    attG = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteG", btnG);
    attA = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteA", btnA);
    attB = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteB", btnB);

    // Branding Setup
    brandingLabel.setText("BandpassModulator by aquanode", juce::dontSendNotification);
    brandingLabel.setJustificationType(juce::Justification::bottomRight);
    brandingLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    brandingLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
    brandingShadow.setShadowProperties(juce::DropShadow(juce::Colours::wheat.withAlpha(0.5f), 10, { 0, 0 }));
    brandingLabel.setComponentEffect(&brandingShadow);
    addAndMakeVisible(brandingLabel);

    // Start UI Refresh at 30 FPS (Frames Per Second)
    startTimerHz(30);
}

BandpassModulatorAudioProcessorEditor::~BandpassModulatorAudioProcessorEditor()
{
    stopTimer(); // Always stop timers when the window closes to prevent crashes
    // Always stop timers when the editor is destroyed
    // This prevents dangling callbacks and crashes
}

// --- PAINTING THE BACKGROUND ---
void BandpassModulatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto oceanDeep = juce::Colours::steelblue;
    auto sandColor = juce::Colour(0xffe8d3a3); // Hex code for light sand

    // Creates a horizontal gradient from SteelBlue to Sand
    juce::ColourGradient gradient(oceanDeep, 0.0f, 0.0f, sandColor, (float)getWidth(), (float)getHeight(), false);
    gradient.addColour(0.6, juce::Colours::cyan.withMultipliedAlpha(0.7f)); // Add a cyan "wave" in the middle

    g.setGradientFill(gradient);
    g.fillAll(); // Paint the entire background with this gradient

    drawFilterCurve(g); // Call the custom function to draw the visualizer
}

// --- DRAWING THE FILTER CURVE (VISUALIZER) ---
void BandpassModulatorAudioProcessorEditor::drawFilterCurve(juce::Graphics& g)
// '::' is the scope resolution operator.
// It means this function belongs to the class BandpassModulatorAudioProcessorEditor.
//
// 'juce::Graphics& g':
// - '&' means reference (no copy is made)
// - We draw directly onto the Graphics object provided by JUCE
{
    // getLocalBounds() returns the full editor rectangle
    // removeFromTop(150) cuts off the top 150 pixels and returns that area
    // reduced(10) shrinks it inward by 10 pixels on all sides
    auto area = getLocalBounds().removeFromTop(150).reduced(10);
    // 'auto' lets the compiler deduce the type (here: juce::Rectangle<int>)

    // Set drawing color to black with 40% opacity
    // withAlpha() returns a modified copy of the colour
    g.setColour(juce::Colours::black.withAlpha(0.4f));

    // Draw a rounded rectangle as background
    // toFloat() converts Rectangle<int> to Rectangle<float>
    g.fillRoundedRectangle(area.toFloat(), 10.0f);

    // --- CLIPPING LOGIC ---
    // Clipping means "do not allow drawing outside this shape"

    juce::Path clipPath;
    // juce::Path is a vector drawing container (lines, curves, shapes)

    clipPath.addRoundedRectangle(area.toFloat(), 10.0f);
    // Defines the clipping shape

    juce::Graphics::ScopedSaveState saveState(g);
    // RAII object:
    // - Saves the current Graphics state (clip, transform, colour)
    // - Automatically restores it when this object goes out of scope

    g.reduceClipRegion(clipPath);
    // Applies the clipping mask so all drawing stays inside the rounded rectangle

    // Convert area dimensions to float for math
    float w = (float)area.getWidth();
    float h = (float)area.getHeight();
    float x_off = (float)area.getX();
    float y_off = (float)area.getY();

    // --- DRAW FREQUENCY GRID (OCTAVES) ---

    g.setFont(10.0f);
    // Sets font size for grid labels

    for (int octave = 1; octave <= 10; ++octave)
        // Classic for-loop:
        // - Starts at octave 1
        // - Ends at octave 10
        // - ++octave increments by 1
    {
        // Convert octave number into frequency (equal temperament)
        // 440 Hz = A4 (MIDI note 69)
        // pow(2, x) means "one octave per doubling"
        float freq = 440.0f * std::pow(2.0f, (octave * 12 - 69) / 12.0f);

        // Logarithmic frequency mapping:
        // - Human hearing is logarithmic
        // - Low frequencies get more screen space
        float x = w * (std::log10(freq / 20.0f) / std::log10(20000.0f / 20.0f));

        // Only draw if inside visible area
        if (x >= 0 && x <= w)
        {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            // Draw a vertical grid line
            g.drawVerticalLine(x_off + x, y_off, y_off + h);

            // Draw octave label text
            g.drawText(
                "C" + juce::String(octave),
                x_off + x + 2,
                y_off + 2,
                30,
                20,
                juce::Justification::topLeft
            );
        }
    }

    // --- ANIMATING THE PATH ---

    // Get high-resolution time in milliseconds
    auto ms = juce::Time::getMillisecondCounterHiRes();
    // 'auto' deduces double

    // Create a slow sine wave oscillating between 0 and 1
    float wave =
        (std::sin(ms / 3000.0 * juce::MathConstants<float>::twoPi) + 1.0f) * 0.5f;
    // sin() outputs [-1, 1]
    // +1 shifts to [0, 2]
    // *0.5 scales to [0, 1]

    // Interpolate between cyan and white based on wave value
    auto pulseColor = juce::Colours::cyan.interpolatedWith(
        juce::Colours::white,
        wave
    );

    // --- FETCH DSP VALUES FOR VISUALIZATION ---

    float cutoff = audioProcessor.getCurrentCutoff();
    // Calls a function on the processor to get live cutoff frequency

    float width = *audioProcessor.apvts.getRawParameterValue("width");
    // getRawParameterValue() returns std::atomic<float>*
    // '*' dereferences the pointer to read the value

    juce::Path curvePath;
    // Path that will hold the filter curve line

    for (float x = 0; x <= w; x += 1.0f)
        // Iterate horizontally across the display, pixel by pixel
    {
        // Convert x position into logarithmic frequency
        float freq = 20.0f * std::pow(1000.0f, x / w);

        // Distance from cutoff in log-frequency space
        float distance =
            std::abs(std::log10(freq) - std::log10(cutoff));

        // Gaussian-style bell curve
        float magnitude = std::exp(-distance * (2.0f * width));

        // Convert magnitude into y screen position
        float y = y_off + h - (magnitude * h * 0.8f);

        // Start path at first point
        if (x == 0)
            curvePath.startNewSubPath(x_off + x, y);
        else
            curvePath.lineTo(x_off + x, y);
    }

    // Draw outer glow (thick, translucent)
    g.setColour(pulseColor.withAlpha(0.3f));
    g.strokePath(curvePath, juce::PathStrokeType(4.0f));

    // Draw main curve line (thin, solid)
    g.setColour(pulseColor);
    g.strokePath(curvePath, juce::PathStrokeType(1.5f));
}

// --- LAYOUT LOGIC (POSITIONING COMPONENTS) ---
// Always use the resized() function for the actual view
void BandpassModulatorAudioProcessorEditor::resized()
// resized() is called automatically whenever the window changes size
{
    auto area = getLocalBounds();
    // Start with the full editor rectangle

    // --- FOOTER AREA ---
    auto footerArea = area.removeFromBottom(40).reduced(20, 0);
    // Removes bottom strip for footer controls

    panningLfoSwitch.setBounds(
        footerArea.removeFromLeft(120).withTrimmedBottom(15)
    );
    // Place LFO switch on the left

    modeSelector.setBounds(
        footerArea.removeFromLeft(120).withTrimmedBottom(15).reduced(0, 5)
    );
    // Place mode selector next to it

    brandingLabel.setBounds(
        footerArea.removeFromRight(320).withTrimmedBottom(20)
    );
    // Place branding on the right

    // --- DISPLAY AREA ---
    auto displayArea = area.removeFromTop(150);
    // Reserves space for the visualizer

    // --- MAIN CONTROL AREA ---
    auto mainArea = area.reduced(20, 10);

    auto rightColumn = mainArea.removeFromRight(110);
    // Right column holds note buttons

    auto leftColumn = mainArea;
    // Left column holds sliders and labels

    int numRows = 8;
    int rowHeight = leftColumn.getHeight() / numRows;
    int sliderHeight = 24;
    int y = leftColumn.getY();
    int yOffset = (rowHeight - sliderHeight) / 2;

    // Lambda to place one row of UI elements
    // for convenient positioning we introduce UI elements row by row in the same fashion
    auto placeRow =
        [&](juce::Label& label, juce::Slider& slider, juce::Component& button, int rowIndex)
        // '&' capture means the lambda uses existing local variables by reference
        {
            int currentY = y + (rowIndex * rowHeight) + yOffset;
            label.setBounds(leftColumn.getX(), currentY, 70, sliderHeight);
            slider.setBounds(
                leftColumn.getX() + 75,
                currentY,
                leftColumn.getWidth() - 100,
                sliderHeight
            );
            button.setBounds(
                rightColumn.getX(),
                currentY,
                rightColumn.getWidth(),
                sliderHeight
            );
        };

    // Place each control row
    placeRow(minFreqLabel, minFreqSlider, noteLockSwitch, 0);
    placeRow(maxFreqLabel, maxFreqSlider, btnC, 1);
    placeRow(glideTimeLabel, glideTimeSlider, btnD, 2);
    placeRow(stayTimeLabel, stayTimeSlider, btnE, 3);
    placeRow(widthLabel, widthSlider, btnF, 4);
    placeRow(dryWetLabel, dryWetSlider, btnG, 5);
    placeRow(wetGainLabel, wetGainSlider, btnA, 6);
    placeRow(panningLabel, panningSlider, btnB, 7);
}

// --- REFRESH LOOP ---
void BandpassModulatorAudioProcessorEditor::timerCallback()
// Called automatically by JUCE at the timer frequency (e.g. 30 Hz)
{
    bool lfoActive =
        *audioProcessor.apvts.getRawParameterValue("panningLfoActive") > 0.5f;
    // Reads the LFO active parameter from the APVTS
    // > 0.5f converts float parameter to bool logic

    if (lfoActive)
    {
        // Push DSP-driven pan value into the UI slider
        // dontSendNotification prevents feedback loops
        panningSlider.setValue(
            audioProcessor.getCurrentPan(),
            juce::dontSendNotification
        );
    }

    repaint();
    // Triggers paint() to redraw the editor
}