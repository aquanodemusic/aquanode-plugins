#include "PluginProcessor.h"
#include "PluginEditor.h"

EQResonatorAudioProcessorEditor::EQResonatorAudioProcessorEditor(EQResonatorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    auto sandColor = juce::Colour(0xffe8d3a3);

    // ================= WET ONLY BUTTON =================
    wetOnlyButton.setButtonText("WET ONLY");
    wetOnlyButton.setColour(juce::ToggleButton::tickColourId, sandColor);
    addAndMakeVisible(wetOnlyButton);
    wetOnlyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "wetOnly", wetOnlyButton);

    // ================= SUBTRACT MODE BUTTON =================
    subtractModeButton.setButtonText("SUBTRACT MODE");
    subtractModeButton.setColour(juce::ToggleButton::tickColourId, sandColor);
    addAndMakeVisible(subtractModeButton);

    subtractModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "subtractMode", subtractModeButton);


    // ================= OCTAVE CONTROLS =================
    for (int i = 0; i < 10; ++i)
    {
        // 1. Octave Toggle Button
        octaveButtons[i].setButtonText(juce::String(i + 1));
        octaveButtons[i].setColour(juce::ToggleButton::tickColourId, sandColor);
        addAndMakeVisible(octaveButtons[i]);

        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, "octave" + juce::String(i), octaveButtons[i]
        ));

        // 2. Q Knob (Rotary)
        octaveQSliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        // Enable the text box below the knob
        octaveQSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 18);
        octaveQSliders[i].setColour(juce::Slider::rotarySliderFillColourId, sandColor);
        octaveQSliders[i].setColour(juce::Slider::thumbColourId, sandColor);
        octaveQSliders[i].setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(octaveQSliders[i]);

        qAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.apvts, "qOctave" + juce::String(i), octaveQSliders[i]
        ));

        octaveQLabels[i].setText("Q", juce::dontSendNotification);
        octaveQLabels[i].setFont(12.0f); // Made slightly larger for readability
        octaveQLabels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(octaveQLabels[i]);

        // 3. Gain Knob (Rotary)
        octaveGainSliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        // Enable the text box below the knob
        octaveGainSliders[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 45, 18);
        octaveGainSliders[i].setColour(juce::Slider::rotarySliderFillColourId, sandColor);
        octaveGainSliders[i].setColour(juce::Slider::thumbColourId, sandColor);
        octaveGainSliders[i].setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(octaveGainSliders[i]);

        gainAttachments.push_back(
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                audioProcessor.apvts, "gainOctave" + juce::String(i), octaveGainSliders[i]
            ));

        octaveGainLabels[i].setText("GAIN", juce::dontSendNotification);
        octaveGainLabels[i].setFont(12.0f);
        octaveGainLabels[i].setJustificationType(juce::Justification::centred);
        addAndMakeVisible(octaveGainLabels[i]);
    }

    // ================= KEYBOARD BUTTONS =================
    int natIndices[] = { 0, 2, 4, 5, 7, 9, 11 };
    juce::String natNames[] = { "C", "D", "E", "F", "G", "A", "B" };

    for (int i = 0; i < 7; ++i) {
        naturalButtons[i].setButtonText(natNames[i]);
        naturalButtons[i].setClickingTogglesState(true);
        naturalButtons[i].setColour(juce::TextButton::buttonOnColourId, sandColor);
        naturalButtons[i].setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible(naturalButtons[i]);

        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, juce::String(natIndices[i]), naturalButtons[i]
        ));
    }

    int shrpIndices[] = { 1, 3, 6, 8, 10 };
    juce::String shrpNames[] = { "C#", "D#", "F#", "G#", "A#" };

    for (int i = 0; i < 5; ++i) {
        sharpButtons[i].setButtonText(shrpNames[i]);
        sharpButtons[i].setClickingTogglesState(true);
        sharpButtons[i].setColour(juce::TextButton::buttonOnColourId, sandColor);
        sharpButtons[i].setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible(sharpButtons[i]);

        buttonAttachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.apvts, juce::String(shrpIndices[i]), sharpButtons[i]
        ));
    }

    setSize(750, 600); // Increased width slightly to fit 10 columns of knobs + text boxes
    startTimerHz(30);
}

EQResonatorAudioProcessorEditor::~EQResonatorAudioProcessorEditor() {}

void EQResonatorAudioProcessorEditor::drawFrequencyVisualizer(juce::Graphics& g)
{
    auto area = getLocalBounds().removeFromTop(120).reduced(15, 10);

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(area.toFloat(), 10.0f);

    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(area);

    const float w = (float)area.getWidth();
    const float h = (float)area.getHeight();
    const float x_off = (float)area.getX();
    const float y_off = (float)area.getY();
    const float y_bottom = y_off + h; // bottom of the area

    const float minFreq = 15.0f;
    const float maxFreq = 20000.0f;
    const float logMin = std::log10(minFreq);
    const float logScale = std::log10(maxFreq) - logMin;

    const float gainMin = -24.0f;    // 0 dB at bottom
    const float gainMax = 24.0f;   // full height at 32 dB

    const float qMin = 1.0f;
    const float qMax = 500.0f;

    const bool subtractMode = audioProcessor.subtractModeParam->load() > 0.5f;

    for (int oct = 0; oct < 10; ++oct) // 10 octaves
    {
        if (audioProcessor.octaveActiveParams[oct]->load() > 0.5f)
        {
            float currentQ = audioProcessor.octaveQParams[oct]->load();
            float currentGain = audioProcessor.octaveGainParams[oct]->load(); // 0..32 dB

            // Clamp gain to 0..32 dB
            currentGain = juce::jlimit(gainMin, gainMax, currentGain);

            // Map gain to line height
            float gainNorm = (currentGain - gainMin) / (gainMax - gainMin); // 0..1
            float lineHeight = gainNorm * h; // line grows upward from bottom

            // Map Q inversely to line thickness: small Q = wide, high Q = narrow
            float qNorm = juce::jlimit(0.0f, 1.0f, (currentQ - qMin) / (qMax - qMin));
            float lineThickness = 4.0f * (1.0f - qNorm) + 0.5f; // 0.5 → 4 px

            // Line color depends on Q for subtle cue
            juce::Colour lineColour = juce::Colours::cyan.interpolatedWith(juce::Colours::white, qNorm);

            for (int n = 0; n < 12; ++n)
            {
                if (audioProcessor.noteParams[n]->load() > 0.5f)
                {
                    int idx = (n * 10) + oct;
                    float freq = audioProcessor.noteFrequencies[idx];

                    if (freq >= minFreq && freq <= maxFreq)
                    {
                        float x = x_off + w * ((std::log10(freq) - logMin) / logScale);
                        g.setColour(lineColour.withAlpha(0.6f));

                        if (subtractMode)
                            g.drawLine(x, y_off, x, y_off + lineHeight, lineThickness); // flipped
                        else
                            g.drawLine(x, y_bottom, x, y_bottom - lineHeight, lineThickness); // normal
                    }
                }
            }
        }
    }
}


void EQResonatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient gradient(
        juce::Colour::fromRGB(100, 200, 220), // light sky blue
        0.0f, 0.0f,
        juce::Colour::fromRGB(0, 180, 180),   // midnight blue
        (float)getWidth(), (float)getHeight(),
        false);

    g.setGradientFill(gradient);
    g.fillAll();

    // ===== VISUALIZER =====
    drawFrequencyVisualizer(g);

    // ===== TITLE ROW =====
    // Positioned at 120 (a happy medium between the original 130 and the aggressive 110)
    const int titleY = 120;

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(22.0f, juce::Font::bold));
    g.drawText("EQ / OCTAVE RESONATOR by aquanode",
        25, titleY, 420, 30,
        juce::Justification::left);

    // ===== SECTION LABELS =====
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));

    // Label positioned nicely below the top separator
    g.drawText("OCTAVE SELECTOR (Recommended for nice tones: Octaves 7+ deactive, Gain around +7 dB, Q 200 in regular or 5 in subtractive mode)",
        25, 175, 700, 20,
        juce::Justification::left);

    g.drawText("NOTE SELECTOR",
        25, 505, 200, 20,
        juce::Justification::left);

    // ===== SEPARATORS =====
    g.setColour(juce::Colours::white.withAlpha(0.2f));

    // Separator line at 170
    g.drawHorizontalLine(170, 20.0f, (float)getWidth() - 20.0f);
    g.drawHorizontalLine(495, 20.0f, (float)getWidth() - 20.0f);
}

void EQResonatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // 1. VISUALIZER: Set to 110 (Original was 120)
    area.removeFromTop(110);

    // 2. TITLE ROW
    auto titleRow = area.removeFromTop(40).reduced(20, 0);

    // Wet Only button
    wetOnlyButton.setBounds(
        titleRow.removeFromRight(130)
        .withSizeKeepingCentre(130, 26).translated(0, 5));

    // After Wet Only button in the title row
    subtractModeButton.setBounds(
        titleRow.removeFromRight(150)
        .withSizeKeepingCentre(150, 26).translated(-5, 5));

    // 3. GAP: Reduced from 40 to 20 (The original "dead air" was here)
    area.removeFromTop(20);

    auto mainArea = area.reduced(15, 10);

    // ===== OCTAVE SECTION =====
    // Space for the "OCTAVE SELECTOR" label text
    mainArea.removeFromTop(25);

    auto octSection = mainArea.removeFromTop(260);
    int colWidth = octSection.getWidth() / 10;

    for (int i = 0; i < 10; ++i)
    {
        auto column = octSection.removeFromLeft(colWidth).reduced(2);

        octaveButtons[i].setBounds(
            column.removeFromTop(28)
            .withSizeKeepingCentre(40, 20));

        octaveGainLabels[i].setBounds(column.removeFromTop(18));
        octaveGainSliders[i].setBounds(column.removeFromTop(90));

        column.removeFromTop(6); // Restored standard spacing

        octaveQLabels[i].setBounds(column.removeFromTop(18));
        octaveQSliders[i].setBounds(column.removeFromTop(90));
    }

    // ===== NOTE SECTION =====
    // This section remains unchanged to preserve your keyboard layout
    mainArea.removeFromTop(30);

    int whiteKeyW = 65;
    int rowH = 35;
    int startX = (getWidth() - (7 * whiteKeyW)) / 2;
    int keyboardTop = getHeight() - 95;

    // Naturals
    for (int i = 0; i < 7; ++i)
    {
        naturalButtons[i].setBounds(
            startX + (i * whiteKeyW),
            keyboardTop + rowH + 5,
            whiteKeyW - 10,
            rowH);
    }

    // Sharps
    int sharpXOffsets[] = { 0, 1, 3, 4, 5 };
    for (int i = 0; i < 5; ++i)
    {
        int xPos = startX + (sharpXOffsets[i] * whiteKeyW)
            + (whiteKeyW / 2);

        sharpButtons[i].setBounds(
            xPos,
            keyboardTop,
            whiteKeyW - 10,
            rowH);
    }
}

void EQResonatorAudioProcessorEditor::timerCallback() { repaint(); }