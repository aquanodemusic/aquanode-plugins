#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Konstanten
static constexpr int nOps = 12;
static constexpr int nParams = 7;
static constexpr int rowH = 52;
static constexpr int knobW = 55;
static constexpr int mSize = 22;
static constexpr int fbKnobSize = 30;  // Dedicated size for feedback knobs
static constexpr int leftOff = 45;

FM12SynthAudioProcessorEditor::FM12SynthAudioProcessorEditor(FM12SynthAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    // Pre-allocate vectors to avoid reallocations
    opKnobs.reserve(nOps * nParams);
    opKnobAttachments.reserve(nOps * nParams);
    matrixButtons.reserve(nOps * (nOps - 1)); // Excluding diagonal
    matrixAttachments.reserve(nOps * (nOps - 1));
    feedbackKnobs.reserve(nOps);
    feedbackAttachments.reserve(nOps);

    static constexpr const char* pNames[] =
    {
        "attack", "decay", "sustain", "release",
        "level", "ratio", "phase"
    };

    // Knobs erstellen
    for (int op = 0; op < nOps; ++op) {
        for (int i = 0; i < nParams; ++i) {
            auto s = std::make_unique<juce::Slider>();
            s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 14);
            addAndMakeVisible(s.get());

            const juce::String paramID = "op" + juce::String(op) + "_" + juce::String(pNames[i]);

            if (processor.apvts.getParameter(paramID) != nullptr) {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramID, *s);
                opKnobAttachments.push_back(std::move(att));
            }
            opKnobs.push_back(std::move(s));
        }
    }

    // Matrix erstellen first (so they're behind feedback knobs)
    for (int f = 0; f < nOps; ++f) {
        for (int t = 0; t < nOps; ++t) {
            // Skip diagonal - that's where feedback knobs go
            if (f == t)
                continue;

            auto b = std::make_unique<juce::ToggleButton>();
            addAndMakeVisible(b.get());

            const juce::String rID = "route_" + juce::String(f) + "_" + juce::String(t);

            if (processor.apvts.getParameter(rID) != nullptr) {
                auto att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.apvts, rID, *b);
                matrixAttachments.push_back(std::move(att));
            }
            matrixButtons.push_back(std::move(b));
        }
    }

    // Feedback knobs erstellen AFTER matrix (so they appear in front)
    for (int op = 0; op < nOps; ++op)
    {
        auto fb = std::make_unique<juce::Slider>();
        fb->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        fb->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(fb.get());

        const juce::String fbID = "feedback_" + juce::String(op);

        if (processor.apvts.getParameter(fbID) != nullptr) {
            auto att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, fbID, *fb);
            feedbackAttachments.push_back(std::move(att));
        }
        feedbackKnobs.push_back(std::move(fb));
    }

    adsrFMToggle = std::make_unique<juce::ToggleButton>("ADSR FM");
    adsrFMToggle->setButtonText("ADSR FM");
    addAndMakeVisible(adsrFMToggle.get());
    adsrFMAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "adsrAffectsFM", *adsrFMToggle);

    randomizeButton = std::make_unique<juce::TextButton>("Randomize");
    randomizeButton->onClick = [this] { randomizeMatrix(); };
    addAndMakeVisible(randomizeButton.get());

    // Create Save and Load buttons
    saveButton = std::make_unique<juce::TextButton>("Save");
    saveButton->onClick = [this] { savePreset(); };
    addAndMakeVisible(saveButton.get());

    loadButton = std::make_unique<juce::TextButton>("Load");
    loadButton->onClick = [this] { loadPreset(); };
    addAndMakeVisible(loadButton.get());

    chorusAmountKnob = std::make_unique<juce::Slider>();
    chorusAmountKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chorusAmountKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(chorusAmountKnob.get());
    chorusAmountAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "chorusAmount", *chorusAmountKnob);

    chorusWidthKnob = std::make_unique<juce::Slider>();
    chorusWidthKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    chorusWidthKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(chorusWidthKnob.get());
    chorusWidthAttachment =
        std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor.apvts, "chorusWidth", *chorusWidthKnob);

    setSize(800, 700);

    // Disable automatic background painting for better performance
    setOpaque(true);
}

FM12SynthAudioProcessorEditor::~FM12SynthAudioProcessorEditor() {}

void FM12SynthAudioProcessorEditor::resized()
{
    constexpr int startY = 60;
    constexpr int knobStartX = leftOff;
    constexpr int knobSpacing = knobW + 4;
    constexpr int matrixStartX = knobStartX + (nParams * (knobW + 5)) + 40;

    // --- Operator knobs ---
    const int totalKnobs = static_cast<int>(opKnobs.size());
    for (int i = 0; i < totalKnobs; ++i)
    {
        const int row = i / nParams;
        const int col = i % nParams;
        const int xPos = knobStartX + col * knobSpacing;
        const int yPos = startY + row * rowH;
        opKnobs[i]->setBounds(xPos, yPos, knobW, rowH - 2);
    }

    // --- Routing matrix (excluding diagonal) ---
    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
    {
        for (int col = 0; col < nOps; ++col)
        {
            // Skip diagonal
            if (row == col)
                continue;

            const int xPos = matrixStartX + col * mSize;
            const int yPos = startY + row * rowH + (rowH - mSize) / 2;
            matrixButtons[buttonIdx]->setBounds(xPos, yPos, mSize, mSize);
            buttonIdx++;
        }
    }

    // --- Feedback knobs (diagonal) - positioned AFTER matrix buttons for z-order ---
    for (int op = 0; op < nOps; ++op)
    {
        const int xPos = matrixStartX + op * mSize + (mSize - fbKnobSize) / 2;
        const int yPos = startY + op * rowH + (rowH - fbKnobSize) / 2;
        feedbackKnobs[op]->setBounds(xPos, yPos, fbKnobSize, fbKnobSize);
    }

    // --- Chorus + controls ---
    constexpr int toggleX = matrixStartX;
    constexpr int toggleY = 12;

    constexpr int knobSize = 40;
    constexpr int buttonWidth = 90;
    constexpr int buttonHeight = 24;

    // Chorus knobs
    chorusAmountKnob->setBounds(toggleX + 20, 4, knobSize, knobSize);
    chorusWidthKnob->setBounds(toggleX + knobSize + 8, 4, knobSize, knobSize);

    // Buttons positioned between explanation texts
    const int buttonStartX = toggleX + 2 * knobSize + 36;

    // Save and Load buttons (smaller width for better fit)
    constexpr int smallButtonWidth = 55;
    saveButton->setBounds(buttonStartX - 283, toggleY, smallButtonWidth, buttonHeight);
    loadButton->setBounds(buttonStartX - 283 + smallButtonWidth + 5, toggleY, smallButtonWidth, buttonHeight);

    // Randomize and ADSR FM buttons
    randomizeButton->setBounds(buttonStartX - 30, toggleY, buttonWidth, buttonHeight);
    adsrFMToggle->setBounds(buttonStartX + buttonWidth + 8 - 30,
        toggleY,
        buttonWidth,
        buttonHeight);
}

void FM12SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Hintergrund
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(205, 190, 160), 0, 0,
        juce::Colour(170, 150, 115), 0, (float)getHeight(), false));
    g.fillAll();

    // --- BRANDING (UNVERÄNDERT) ---
    g.setFont(juce::Font(20.0f, juce::Font::bold));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("FM12 by aquanode", 20, 8, 200, 30,
        juce::Justification::left, true);

    // --- EXPLANATION TEXT (weiter rechts) ---
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colours::black.withAlpha(0.85f));

    const juce::String explanation =
        "Turn up volume knobs for sound!";

    g.drawText(explanation,
        197,
        5,
        200,
        40,
        juce::Justification::left, true);

    const juce::String explanation2 =
        "Chorus";

    g.drawText(explanation2,
        500,
        5,
        100,
        40,
        juce::Justification::left, true);

    // --- KNOB COLUMN LABELS ---
    static constexpr const char* knobLabels[] =
    { "ATT", "DEC", "SUS", "REL", "VOL", "RATIO", "PHASE" };

    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colours::black.withAlpha(0.8f));

    constexpr int knobStartX = leftOff;
    constexpr int labelY = 45;
    constexpr int knobSpacing = knobW + 4;

    for (int i = 0; i < nParams; ++i)
    {
        const int xPos = knobStartX + i * knobSpacing;
        g.drawText(knobLabels[i], xPos, labelY, knobW, 14,
            juce::Justification::centred);
    }

    // --- OPERATOR ROW LABELS ---
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.setColour(juce::Colours::black);

    constexpr int startY = 60;
    constexpr int opYOffset = (rowH / 2) - 7;

    for (int i = 0; i < nOps; ++i)
    {
        const int yPos = startY + i * rowH + opYOffset;
        g.drawText("OP" + juce::String(i + 1),
            5, yPos, 38, 14,
            juce::Justification::centredRight);
    }

    // --- MATRIX SECTION ---
    constexpr int matrixStartX = knobStartX + (nParams * (knobW + 5)) + 40;

    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("ROUTING MATRIX (Empty Row = Carrier)",
        matrixStartX, 38, nOps * mSize, 18,
        juce::Justification::centred);

    g.setFont(juce::Font(10.0f));
    for (int i = 0; i < nOps; ++i)
    {
        const int xPos = matrixStartX + i * mSize;
        g.drawText(juce::String(i + 1),
            xPos, 56, mSize, 14,
            juce::Justification::centred);
    }

    g.setColour(juce::Colours::black.withAlpha(0.2f));
    g.drawVerticalLine(matrixStartX - 20, 40.0f,
        (float)getHeight() - 10.0f);
}


void FM12SynthAudioProcessorEditor::randomizeMatrix()
{
    juce::Random rng(juce::Time::currentTimeMillis());

    // Iterate through matrix buttons (which excludes diagonal)
    int buttonIdx = 0;
    for (int row = 0; row < nOps; ++row)
    {
        // Skip first row (operator 0)
        if (row == 0)
        {
            buttonIdx += (nOps - 1); // Skip all buttons in row 0
            continue;
        }

        for (int col = 0; col < nOps; ++col)
        {
            // Skip diagonal (shouldn't exist in matrixButtons anyway)
            if (row == col)
                continue;

            // 30% chance to enable each connection for sparse routing
            const bool shouldEnable = rng.nextFloat() < 0.3f;
            matrixButtons[buttonIdx]->setToggleState(shouldEnable, juce::sendNotificationSync);
            buttonIdx++;
        }
    }
}

void FM12SynthAudioProcessorEditor::savePreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save FM12 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.fm12preset");

    auto flags = juce::FileBrowserComponent::saveMode |
        juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
            {
                // Add extension if not present
                if (!file.hasFileExtension(".fm12preset"))
                    file = file.withFileExtension(".fm12preset");

                // Get the current state from the processor
                juce::MemoryBlock memoryBlock;
                processor.getStateInformation(memoryBlock);

                // Write to file
                if (file.replaceWithData(memoryBlock.getData(), memoryBlock.getSize()))
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::InfoIcon,
                        "Success",
                        "Preset saved successfully!",
                        "OK");
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to save preset file.",
                        "OK");
                }
            }
        });
}

void FM12SynthAudioProcessorEditor::loadPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load FM12 Preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.fm12preset");

    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File() && file.existsAsFile())
            {
                // Read the file
                juce::MemoryBlock memoryBlock;

                if (file.loadFileAsData(memoryBlock))
                {
                    // Set the state in the processor
                    processor.setStateInformation(memoryBlock.getData(),
                        static_cast<int>(memoryBlock.getSize()));
                }
                else
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::AlertWindow::WarningIcon,
                        "Error",
                        "Failed to read preset file.",
                        "OK");
                }
            }
        });
}