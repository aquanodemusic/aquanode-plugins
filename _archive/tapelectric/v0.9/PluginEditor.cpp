#include "PluginProcessor.h"
#include "PluginEditor.h"

TapElectricAudioProcessorEditor::TapElectricAudioProcessorEditor(TapElectricAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(580, 500); // Increased height for larger harmonic knobs

    auto setupSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& txt, const juce::String& id, std::unique_ptr<SliderAttachment>& att) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16); // Width for 4 decimals
        s.setNumDecimalPlacesToDisplay(4); // 4 decimal places for ALL sliders
        addAndMakeVisible(s);
        att = std::make_unique<SliderAttachment>(audioProcessor.apvts, id, s);
        l.setText(txt, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
        };

    // Initialize all 18+ attachments
    setupSlider(mixSlider, mixLabel, "H/N Mult", "mix", mixAttachment);
    setupSlider(humVolumeSlider, humVolumeLabel, "Hum Vol", "humVol", humVolumeAttachment);
    setupSlider(humPanSlider, humPanLabel, "Hum Pan", "humPan", humPanAttachment);
    setupSlider(humStereoSlider, humStereoLabel, "Hum Stereo", "humStereo", humStereoAttachment);
    setupSlider(humDriftSlider, humDriftLabel, "Hum Drift", "humDrift", humDriftAttachment);

    setupSlider(harmonic1Slider, harmonic1Label, "1x", "h1", h1Att);
    setupSlider(harmonic1RandAmtSlider, harmonic1RandAmtLabel, "Flc", "h1RandAmt", h1RandAmtAtt);
    setupSlider(harmonic1RandSpdSlider, harmonic1RandSpdLabel, "Spd", "h1RandSpd", h1RandSpdAtt);
    
    setupSlider(harmonic2Slider, harmonic2Label, "2x", "h2", h2Att);
    setupSlider(harmonic2RandAmtSlider, harmonic2RandAmtLabel, "Flc", "h2RandAmt", h2RandAmtAtt);
    setupSlider(harmonic2RandSpdSlider, harmonic2RandSpdLabel, "Spd", "h2RandSpd", h2RandSpdAtt);
    
    setupSlider(harmonic3Slider, harmonic3Label, "3x", "h3", h3Att);
    setupSlider(harmonic3RandAmtSlider, harmonic3RandAmtLabel, "Flc", "h3RandAmt", h3RandAmtAtt);
    setupSlider(harmonic3RandSpdSlider, harmonic3RandSpdLabel, "Spd", "h3RandSpd", h3RandSpdAtt);
    
    setupSlider(harmonic4Slider, harmonic4Label, "4x", "h4", h4Att);
    setupSlider(harmonic4RandAmtSlider, harmonic4RandAmtLabel, "Flc", "h4RandAmt", h4RandAmtAtt);
    setupSlider(harmonic4RandSpdSlider, harmonic4RandSpdLabel, "Spd", "h4RandSpd", h4RandSpdAtt);
    
    setupSlider(harmonic5Slider, harmonic5Label, "5x", "h5", h5Att);
    setupSlider(harmonic5RandAmtSlider, harmonic5RandAmtLabel, "Flc", "h5RandAmt", h5RandAmtAtt);
    setupSlider(harmonic5RandSpdSlider, harmonic5RandSpdLabel, "Spd", "h5RandSpd", h5RandSpdAtt);
    
    setupSlider(harmonic6Slider, harmonic6Label, "6x", "h6", h6Att);
    setupSlider(harmonic6RandAmtSlider, harmonic6RandAmtLabel, "Flc", "h6RandAmt", h6RandAmtAtt);
    setupSlider(harmonic6RandSpdSlider, harmonic6RandSpdLabel, "Spd", "h6RandSpd", h6RandSpdAtt);

    setupSlider(noiseVolumeSlider, noiseVolumeLabel, "Noise Vol", "noiseVol", noiseVolAtt);
    setupSlider(noisePanSlider, noisePanLabel, "Noise Pan", "noisePan", noisePanAtt);
    setupSlider(noiseStereoSlider, noiseStereoLabel, "Noise Stereo", "noiseStereo", noiseStereoAtt);

    setupSlider(inputVolumeSlider, inputVolumeLabel, "Input Vol", "inputVol", inputVolAtt);
    setupSlider(inputDriftSlider, inputDriftLabel, "Input Drift", "inputDrift", inputDriftAtt);
    setupSlider(inputWobbleSlider, inputWobbleLabel, "Wobble", "inputWobble", inputWobbleAtt);

    // EQ setup
    auto setupEQ = [this](EQBandControls& eq, const juce::String& qID, const juce::String& lbl) {
        eq.freqLabel.setText(lbl, juce::dontSendNotification);
        eq.freqLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(eq.freqLabel);
        eq.qSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        addAndMakeVisible(eq.qSlider);
        eq.qAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, qID, eq.qSlider);
        addAndMakeVisible(eq.qLabel);
        };

    setupEQ(eq1, "eqQ1", "LOW");
    setupEQ(eq2, "eqQ2", "MID");
    setupEQ(eq3, "eqQ3", "HIGH");
    // Inside TapElectricAudioProcessorEditor constructor:

// === 50/60 Hz Toggle (Base Freq) ===
    freqModeLabel.setText("Base Freq", juce::dontSendNotification);
    freqModeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(freqModeLabel);

    // Add choices to the box
    freqModeBox.addItem("50 Hz", 1);
    freqModeBox.addItem("60 Hz", 2);
    addAndMakeVisible(freqModeBox);

    // Attach it to APVTS (No more manual onChange listeners needed!)
    freqModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "freqMode", freqModeBox);


    // === Randomize Button ===
    randomizeButton.setButtonText("Rand");
    addAndMakeVisible(randomizeButton);

    // This is the one place we still use a lambda because it's a "trigger," not a state
    randomizeButton.onClick = [this] {
        audioProcessor.randomizeHarmonics();
        };
    startTimerHz(30); // Now works because we added public juce::Timer
}

TapElectricAudioProcessorEditor::~TapElectricAudioProcessorEditor() { stopTimer(); }

void TapElectricAudioProcessorEditor::handleEQPadMouse(const juce::MouseEvent& e, EQBandControls& eq, const juce::String& fID, const juce::String& gID)
{
    if (auto* fP = audioProcessor.apvts.getParameter(fID)) {
        if (auto* gP = audioProcessor.apvts.getParameter(gID)) {
            if (e.mods.isLeftButtonDown() && eq.padBounds.contains(e.position)) {
                float nX = juce::jlimit(0.0f, 1.0f, (e.position.x - eq.padBounds.getX()) / eq.padBounds.getWidth());
                float nY = juce::jlimit(0.0f, 1.0f, (e.position.y - eq.padBounds.getY()) / eq.padBounds.getHeight());
                fP->setValueNotifyingHost(nX);
                gP->setValueNotifyingHost(1.0f - nY);
            }
        }
    }
}

//==============================================================================
void TapElectricAudioProcessorEditor::timerCallback()
{
    // 1. Button Text aktualisieren
    if (auto* h1 = audioProcessor.apvts.getParameter("h1"))
    {
        bool isChanged = std::abs(h1->getValue() - h1->getDefaultValue()) > 0.001f;
        randomizeButton.setButtonText(isChanged ? "Restore" : "Rand Tone");
    }

    // 2. EQ Area repainten
    repaint(eqAreaBounds);
}

//==============================================================================
void TapElectricAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colour(0xff1a1a1a));

    // Header Strip
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(0, 0, getWidth(), 90);

    // Title
    g.setColour(juce::Colour(0xffcc9966));
    g.setFont(juce::Font(26.0f, juce::Font::bold));
    g.drawText("TapElectric", 20, 15, 200, 30, juce::Justification::left);

    g.setFont(juce::Font(12.0f, juce::Font::plain));
    g.setColour(juce::Colour(0xff888888));
    g.drawText("Tape & Power Hum", 20, 42, 200, 20, juce::Justification::left);

    // Row Headings
    g.setColour(juce::Colour(0xffcc9966).withAlpha(0.7f));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText("HUM & MAIN", 15, 85, 150, 20, juce::Justification::left);
    g.drawText("RESONANCES", 15, 195, 150, 20, juce::Justification::left);
    g.drawText("NOISE & INPUT", 15, 375, 150, 20, juce::Justification::left);

    // Draw the 3 EQ Pads
    paintEQPad(g, eq1, "eqFreq1", "eqGain1", juce::Colour(0xff6699ff), "LOW");
    paintEQPad(g, eq2, "eqFreq2", "eqGain2", juce::Colour(0xff99ff66), "MID");
    paintEQPad(g, eq3, "eqFreq3", "eqGain3", juce::Colour(0xffff9966), "HIGH");
}

void TapElectricAudioProcessorEditor::paintEQPad(juce::Graphics& g, EQBandControls& eq,
                                                   const juce::String& paramFreqId, const juce::String& paramGainId,
                                                   const juce::Colour& color, const juce::String& label)
{
    // Get parameter values from APVTS
    auto* freqParam = dynamic_cast<juce::RangedAudioParameter*>(audioProcessor.apvts.getParameter(paramFreqId));
    auto* gainParam = dynamic_cast<juce::RangedAudioParameter*>(audioProcessor.apvts.getParameter(paramGainId));
    if (!freqParam || !gainParam) return;
    
    // Draw pad background
    g.setColour(juce::Colour(0xff1a1a1a));
    g.fillRect(eq.padBounds);
    
    // Draw grid
    g.setColour(juce::Colour(0xff333333));
    for (int i = 1; i < 4; ++i)
    {
        float x = eq.padBounds.getX() + (eq.padBounds.getWidth() * i / 4.0f);
        g.drawLine(x, eq.padBounds.getY(), x, eq.padBounds.getBottom(), 1.0f);
        
        float y = eq.padBounds.getY() + (eq.padBounds.getHeight() * i / 4.0f);
        g.drawLine(eq.padBounds.getX(), y, eq.padBounds.getRight(), y, 1.0f);
    }
    
    // Draw center line (0dB)
    g.setColour(juce::Colour(0xff555555));
    float centerY = eq.padBounds.getCentreY();
    g.drawLine(eq.padBounds.getX(), centerY, eq.padBounds.getRight(), centerY, 1.5f);
    
    // Draw border
    g.setColour(juce::Colour(0xff444444));
    g.drawRect(eq.padBounds, 2.0f);
    
    // Get denormalized values
    float freq = freqParam->convertFrom0to1(freqParam->getValue());
    float gain = gainParam->convertFrom0to1(gainParam->getValue());
    
    // Calculate position
    // X position (log scale, normalized)
    float normX = freqParam->getValue();
    float x = eq.padBounds.getX() + (normX * eq.padBounds.getWidth());
    
    // Y position (inverted for gain - high gain at top)
    float normY = 1.0f - gainParam->getValue();
    float y = eq.padBounds.getY() + (normY * eq.padBounds.getHeight());
    
    // Draw crosshair
    g.setColour(color.withAlpha(0.3f));
    g.drawLine(x, eq.padBounds.getY(), x, eq.padBounds.getBottom(), 1.0f);
    g.drawLine(eq.padBounds.getX(), y, eq.padBounds.getRight(), y, 1.0f);
    
    // Draw handle
    g.setColour(color);
    g.fillEllipse(x - 6, y - 6, 12, 12);
    g.setColour(color.brighter(0.5f));
    g.drawEllipse(x - 6, y - 6, 12, 12, 2.0f);
    
    // Draw frequency and gain text
    g.setFont(juce::Font(10.0f));
    g.setColour(juce::Colours::white);
    juce::String freqText = freq < 1000.0f ? 
        juce::String(freq, 0) + "Hz" : 
        juce::String(freq / 1000.0f, 1) + "kHz";
    juce::String gainText = juce::String(gain, 1) + "dB";
    
    g.drawText(freqText + " | " + gainText, 
               eq.padBounds.getX(), eq.padBounds.getBottom() + 2,
               eq.padBounds.getWidth(), 15, juce::Justification::centred);
}

void TapElectricAudioProcessorEditor::resized()
{
    const int knobSize = 65;
    const int colWidth = 92;
    const int leftMargin = 15;
    const int labelHeight = 18;

    // === HEADER EQ AREA (Top Right) ===
    const int eqY = 25;
    const int padW = 100;
    const int padH = 35;
    const int eqStartX = 200;
    const int spacing = 20;     // Etwas mehr Abstand, da die Q-Slider nach links ragen
    const int qSliderSize = 30;

    auto layoutEQ = [&](EQBandControls& eq, int x) {
        // Das Grafik-Pad
        eq.padBounds = juce::Rectangle<float>(x, eqY, padW, padH);

        // Label über dem Pad
        eq.freqLabel.setBounds(x, eqY - 16, padW, 14);

        // Q-Slider DIAGONAL LINKS UNTERHALB:
        // x - (Hälfte der Slidergröße) schiebt ihn nach links
        // getBottom() + 2 schiebt ihn direkt unter das Pad
        eq.qSlider.setBounds(x - (qSliderSize / 2),
            eq.padBounds.getBottom() + 2,
            qSliderSize,
            qSliderSize);
        };

    layoutEQ(eq1, eqStartX);
    layoutEQ(eq2, eqStartX + padW + spacing);
    layoutEQ(eq3, eqStartX + (padW + spacing) * 2);

    // Repaint Bereich für den Timer
    eqAreaBounds = juce::Rectangle<int>(eqStartX - 50, 0, 450, 100);

    // === ROW LAYOUT HELPER ===
    auto layoutControl = [&](int col, int rowY, juce::Slider& s, juce::Label& l) {
        int x = leftMargin + (col * colWidth);
        l.setBounds(x, rowY, colWidth, labelHeight);
        s.setBounds(x + (colWidth - knobSize) / 2, rowY + labelHeight, knobSize, knobSize);
        };

    // ROW 1: HUM & MIX
    int r1 = 100;
    layoutControl(0, r1, humVolumeSlider, humVolumeLabel);
    layoutControl(1, r1, humPanSlider, humPanLabel);
    layoutControl(2, r1, humStereoSlider, humStereoLabel);
    layoutControl(3, r1, humDriftSlider, humDriftLabel);

    freqModeLabel.setBounds(leftMargin + 4 * colWidth, r1, colWidth, labelHeight);
    freqModeBox.setBounds(leftMargin + 4 * colWidth, r1 + 22, colWidth - 10, 24);
    randomizeButton.setBounds(leftMargin + 4 * colWidth, r1 + 52, colWidth - 10, 24);

    layoutControl(5, r1, mixSlider, mixLabel);

    // ROW 2: RESONANCES (6 columns with 3 knobs each) - LARGER KNOBS
    int r2 = 210;
    const int mainHarmonicSize = 70;     // Larger main harmonic knob
    const int miniKnobSize = 50;         // Larger randomization knobs
    const int miniLabelHeight = 14;
    
    auto layoutHarmonicColumn = [&](int col, 
                                     juce::Slider& main, juce::Label& mainLbl,
                                     juce::Slider& amt, juce::Label& amtLbl,
                                     juce::Slider& spd, juce::Label& spdLbl) {
        int x = leftMargin + (col * colWidth);
        
        // Main harmonic slider at top - LARGER
        mainLbl.setBounds(x, r2, colWidth, labelHeight);
        main.setBounds(x + (colWidth - mainHarmonicSize) / 2, r2 + labelHeight, mainHarmonicSize, mainHarmonicSize);
        
        // Two larger knobs below (Rand Amt and Rand Spd)
        int subRowY = r2 + labelHeight + mainHarmonicSize + 6;
        
        // Rand Amt (left side)
        amtLbl.setBounds(x, subRowY, colWidth/2, miniLabelHeight);
        amt.setBounds(x + (colWidth/2 - miniKnobSize)/2, subRowY + miniLabelHeight, miniKnobSize, miniKnobSize);
        amt.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        
        // Rand Spd (right side)
        spdLbl.setBounds(x + colWidth/2, subRowY, colWidth/2, miniLabelHeight);
        spd.setBounds(x + colWidth/2 + (colWidth/2 - miniKnobSize)/2, subRowY + miniLabelHeight, miniKnobSize, miniKnobSize);
        spd.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
    };
    
    layoutHarmonicColumn(0, harmonic1Slider, harmonic1Label, 
                         harmonic1RandAmtSlider, harmonic1RandAmtLabel,
                         harmonic1RandSpdSlider, harmonic1RandSpdLabel);
    layoutHarmonicColumn(1, harmonic2Slider, harmonic2Label,
                         harmonic2RandAmtSlider, harmonic2RandAmtLabel,
                         harmonic2RandSpdSlider, harmonic2RandSpdLabel);
    layoutHarmonicColumn(2, harmonic3Slider, harmonic3Label,
                         harmonic3RandAmtSlider, harmonic3RandAmtLabel,
                         harmonic3RandSpdSlider, harmonic3RandSpdLabel);
    layoutHarmonicColumn(3, harmonic4Slider, harmonic4Label,
                         harmonic4RandAmtSlider, harmonic4RandAmtLabel,
                         harmonic4RandSpdSlider, harmonic4RandSpdLabel);
    layoutHarmonicColumn(4, harmonic5Slider, harmonic5Label,
                         harmonic5RandAmtSlider, harmonic5RandAmtLabel,
                         harmonic5RandSpdSlider, harmonic5RandSpdLabel);
    layoutHarmonicColumn(5, harmonic6Slider, harmonic6Label,
                         harmonic6RandAmtSlider, harmonic6RandAmtLabel,
                         harmonic6RandSpdSlider, harmonic6RandSpdLabel);

    // ROW 3: NOISE & INPUT - Moved down to accommodate larger harmonics section
    int r3 = 400;
    layoutControl(0, r3, noiseVolumeSlider, noiseVolumeLabel);
    layoutControl(1, r3, noisePanSlider, noisePanLabel);
    layoutControl(2, r3, noiseStereoSlider, noiseStereoLabel);
    layoutControl(3, r3, inputVolumeSlider, inputVolumeLabel);
    layoutControl(4, r3, inputDriftSlider, inputDriftLabel);
    layoutControl(5, r3, inputWobbleSlider, inputWobbleLabel);
}

void TapElectricAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    handleEQPadMouse(e, eq1, TapElectricAudioProcessor::PARAM_EQ_FREQ_1, 
                     TapElectricAudioProcessor::PARAM_EQ_GAIN_1);
    handleEQPadMouse(e, eq2, TapElectricAudioProcessor::PARAM_EQ_FREQ_2, 
                     TapElectricAudioProcessor::PARAM_EQ_GAIN_2);
    handleEQPadMouse(e, eq3, TapElectricAudioProcessor::PARAM_EQ_FREQ_3, 
                     TapElectricAudioProcessor::PARAM_EQ_GAIN_3);
}

void TapElectricAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    handleEQPadMouse(e, eq1, TapElectricAudioProcessor::PARAM_EQ_FREQ_1, 
                     TapElectricAudioProcessor::PARAM_EQ_GAIN_1);
    handleEQPadMouse(e, eq2, TapElectricAudioProcessor::PARAM_EQ_FREQ_2, 
                     TapElectricAudioProcessor::PARAM_EQ_GAIN_2);
    handleEQPadMouse(e, eq3, TapElectricAudioProcessor::PARAM_EQ_FREQ_3, 
                     TapElectricAudioProcessor::PARAM_EQ_GAIN_3);
}

void TapElectricAudioProcessorEditor::mouseUp(const juce::MouseEvent& e)
{
    eq1.isDragging = false;
    eq2.isDragging = false;
    eq3.isDragging = false;
}
