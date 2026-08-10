#include "PluginProcessor.h"

#include "PluginEditor.h"

BandpassModulatorAudioProcessorEditor::BandpassModulatorAudioProcessorEditor(BandpassModulatorAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(600, 500);

    auto setupSlider = [this](juce::Slider& s, juce::Label& l, juce::String name, juce::DropShadowEffect& shadow, juce::Colour thumbColor) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        s.setColour(juce::Slider::thumbColourId, thumbColor);
        s.setColour(juce::Slider::trackColourId, juce::Colours::wheat);
        shadow.setShadowProperties(juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 5, { 3, 3 }));
        s.setComponentEffect(&shadow);
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        addAndMakeVisible(l);
        };

    setupSlider(minFreqSlider, minFreqLabel, "Min Freq", minFreqShadow, juce::Colours::cyan);
    setupSlider(maxFreqSlider, maxFreqLabel, "Max Freq", maxFreqShadow, juce::Colours::cyan);
    setupSlider(glideTimeSlider, glideTimeLabel, "Glide", glideTimeShadow, juce::Colours::cyan);
    setupSlider(stayTimeSlider, stayTimeLabel, "Stay", stayTimeShadow, juce::Colours::cyan);

    setupSlider(panningSlider, panningLabel, "Pan", panningShadow, juce::Colours::white);
    setupSlider(dryWetSlider, dryWetLabel, "Dry/Wet", dryWetShadow, juce::Colours::white);
    setupSlider(widthSlider, widthLabel, "Pinch", widthShadow, juce::Colours::white);
    setupSlider(wetGainSlider, wetGainLabel, "Wet Gain", wetGainShadow, juce::Colours::white);

    addAndMakeVisible(noteLockSwitch);
    noteLockSwitch.setButtonText("Note Lock");
    toggleShadow.setShadowProperties(juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 5, { 2, 2 }));
    noteLockSwitch.setComponentEffect(&toggleShadow);

    juce::ToggleButton* noteBtns[] = { &btnC, &btnD, &btnE, &btnF, &btnG, &btnA, &btnB };
    juce::String noteNames[] = { "C", "D", "E", "F", "G", "A", "B" };

    for (int i = 0; i < 7; ++i) {
        addAndMakeVisible(noteBtns[i]);
        noteBtns[i]->setButtonText(noteNames[i]);
    }

    juce::ToggleButton* sharpBtns[] = { &btnCsharp, &btnDsharp, &btnFsharp, &btnGsharp, &btnAsharp };
    juce::String sharpNames[] = { "C#", "D#", "F#", "G#", "A#" };

    for (int i = 0; i < 5; ++i) {
        addAndMakeVisible(sharpBtns[i]);
        sharpBtns[i]->setButtonText(sharpNames[i]);
    }

    addAndMakeVisible(panningLfoSwitch);
    panningLfoSwitch.setButtonText("Panning LFO");

    addAndMakeVisible(modeSelector);
    modeSelector.addItem("Random", 1);
    modeSelector.addItem("Up", 2);
    modeSelector.addItem("Down", 3);

    modeSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colours::white.withAlpha(0.1f));
    modeSelector.setColour(juce::ComboBox::outlineColourId, juce::Colours::darkturquoise.withAlpha(0.5f));
    modeSelector.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    modeSelector.setJustificationType(juce::Justification::centred);

    getLookAndFeel().setColour(juce::PopupMenu::backgroundColourId, juce::Colours::darkcyan);
    getLookAndFeel().setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colours::cyan);
    getLookAndFeel().setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);

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

    attC = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteC", btnC);
    attD = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteD", btnD);
    attE = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteE", btnE);
    attF = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteF", btnF);
    attG = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteG", btnG);
    attA = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteA", btnA);
    attB = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteB", btnB);

    attCsharp = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteCsharp", btnCsharp);
    attDsharp = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteDsharp", btnDsharp);
    attFsharp = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteFsharp", btnFsharp);
    attGsharp = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteGsharp", btnGsharp);
    attAsharp = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "noteAsharp", btnAsharp);

    brandingLabel.setText("BandpassModulator by aquanode", juce::dontSendNotification);
    brandingLabel.setJustificationType(juce::Justification::bottomRight);
    brandingLabel.setFont(juce::Font(16.0f, juce::Font::bold));
    brandingLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
    brandingShadow.setShadowProperties(juce::DropShadow(juce::Colours::wheat.withAlpha(0.5f), 10, { 0, 0 }));
    brandingLabel.setComponentEffect(&brandingShadow);
    addAndMakeVisible(brandingLabel);

    startTimerHz(30);
}

BandpassModulatorAudioProcessorEditor::~BandpassModulatorAudioProcessorEditor()
{
    stopTimer();
}

void BandpassModulatorAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto oceanDeep = juce::Colours::steelblue;
    auto sandColor = juce::Colour(0xffe8d3a3);

    juce::ColourGradient gradient(oceanDeep, 0.0f, 0.0f, sandColor, (float)getWidth(), (float)getHeight(), false);
    gradient.addColour(0.6, juce::Colours::cyan.withMultipliedAlpha(0.7f));

    g.setGradientFill(gradient);
    g.fillAll();

    drawFilterCurve(g);
}

void BandpassModulatorAudioProcessorEditor::drawFilterCurve(juce::Graphics& g)
{
    auto area = getLocalBounds().removeFromTop(150).reduced(10);

    g.setColour(juce::Colours::black.withAlpha(0.4f));

    g.fillRoundedRectangle(area.toFloat(), 10.0f);


    juce::Path clipPath;

    clipPath.addRoundedRectangle(area.toFloat(), 10.0f);

    juce::Graphics::ScopedSaveState saveState(g);

    g.reduceClipRegion(clipPath);

    float w = (float)area.getWidth();
    float h = (float)area.getHeight();
    float x_off = (float)area.getX();
    float y_off = (float)area.getY();


    g.setFont(10.0f);

    for (int octave = 1; octave <= 10; ++octave)
    {
        float freq = 440.0f * std::pow(2.0f, (octave * 12 - 69) / 12.0f);

        float x = w * (std::log10(freq / 20.0f) / std::log10(20000.0f / 20.0f));

        if (x >= 0 && x <= w)
        {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawVerticalLine(x_off + x, y_off, y_off + h);

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


    auto ms = juce::Time::getMillisecondCounterHiRes();

    float wave =
        (std::sin(ms / 3000.0 * juce::MathConstants<float>::twoPi) + 1.0f) * 0.5f;

    auto pulseColor = juce::Colours::cyan.interpolatedWith(
        juce::Colours::white,
        wave
    );


    float cutoff = audioProcessor.getCurrentCutoff();

    float width = *audioProcessor.apvts.getRawParameterValue("width");

    juce::Path curvePath;

    for (float x = 0; x <= w; x += 1.0f)
    {
        float freq = 20.0f * std::pow(1000.0f, x / w);

        float distance =
            std::abs(std::log10(freq) - std::log10(cutoff));

        float magnitude = std::exp(-distance * (2.0f * width));

        float y = y_off + h - (magnitude * h * 0.8f);

        if (x == 0)
            curvePath.startNewSubPath(x_off + x, y);
        else
            curvePath.lineTo(x_off + x, y);
    }

    g.setColour(pulseColor.withAlpha(0.3f));
    g.strokePath(curvePath, juce::PathStrokeType(4.0f));

    g.setColour(pulseColor);
    g.strokePath(curvePath, juce::PathStrokeType(1.5f));
}

void BandpassModulatorAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto footerArea = area.removeFromBottom(40).reduced(20, 0);

    panningLfoSwitch.setBounds(
        footerArea.removeFromLeft(120).withTrimmedBottom(15)
    );

    modeSelector.setBounds(
        footerArea.removeFromLeft(120).withTrimmedBottom(15).reduced(0, 5)
    );

    brandingLabel.setBounds(
        footerArea.removeFromRight(320).withTrimmedBottom(20)
    );

    auto displayArea = area.removeFromTop(150);

    auto mainArea = area.reduced(20, 10);

    auto rightColumn = mainArea.removeFromRight(110);

    auto leftColumn = mainArea;

    int numRows = 8;
    int rowHeight = leftColumn.getHeight() / numRows;
    int sliderHeight = 24;
    int y = leftColumn.getY();
    int yOffset = (rowHeight - sliderHeight) / 2;

    auto placeRow =
        [&](juce::Label& label, juce::Slider& slider, juce::Component& mainBtn, juce::Component* sharpBtn, int rowIndex)
        {
            int currentY = y + (rowIndex * rowHeight) + yOffset;

            label.setBounds(leftColumn.getX(), currentY, 70, sliderHeight);
            slider.setBounds(
                leftColumn.getX() + 75,
                currentY,
                leftColumn.getWidth() - 100,
                sliderHeight
            );

            auto buttonArea = rightColumn.reduced(0, 0);
            buttonArea.setY(currentY);
            buttonArea.setHeight(sliderHeight);

            if (sharpBtn != nullptr)
            {
                int halfWidth = buttonArea.getWidth() / 2;
                mainBtn.setBounds(buttonArea.removeFromLeft(halfWidth));
                sharpBtn->setBounds(buttonArea);
            }
            else
            {
                mainBtn.setBounds(buttonArea);
            }
        };

    placeRow(minFreqLabel, minFreqSlider, noteLockSwitch, nullptr, 0);

    placeRow(maxFreqLabel, maxFreqSlider, btnC, &btnCsharp, 1);

    placeRow(glideTimeLabel, glideTimeSlider, btnD, &btnDsharp, 2);

    placeRow(stayTimeLabel, stayTimeSlider, btnE, nullptr, 3);

    placeRow(widthLabel, widthSlider, btnF, &btnFsharp, 4);

    placeRow(dryWetLabel, dryWetSlider, btnG, &btnGsharp, 5);

    placeRow(wetGainLabel, wetGainSlider, btnA, &btnAsharp, 6);

    placeRow(panningLabel, panningSlider, btnB, nullptr, 7);
}

void BandpassModulatorAudioProcessorEditor::timerCallback()
{
    bool lfoActive =
        *audioProcessor.apvts.getRawParameterValue("panningLfoActive") > 0.5f;

    if (lfoActive)
    {
        panningSlider.setValue(
            audioProcessor.getCurrentPan(),
            juce::dontSendNotification
        );
    }

    repaint();
}