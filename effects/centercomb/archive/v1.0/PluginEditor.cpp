#include "PluginProcessor.h"
#include "PluginEditor.h"

CenterCombAudioProcessorEditor::CenterCombAudioProcessorEditor(CenterCombAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(600, 550);

    setupSlider(gainSlider, gainLabel, "Main Gain (dB)");
    setupSlider(freqSlider, freqLabel, "Center Freq");
    setupSlider(amountSlider, amountLabel, "Tines");
    setupSlider(spreadSlider, spreadLabel, "Spread (Hz)");
    setupSlider(qSlider, qLabel, "Resonance");
    setupSlider(dampSlider, dampLabel, "Dampening");

    // Attachments
    gainAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "gain", gainSlider);
    freqAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "freq", freqSlider);
    amountAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "amount", amountSlider);
    spreadHzAttachment = std::make_unique<Attachment>(
        audioProcessor.apvts, "spreadHz", spreadSlider);
    qAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "q", qSlider);
    dampAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "damp", dampSlider);
    linearAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "linear", linearButton);

    // Style the Toggle Button
    linearButton.setClickingTogglesState(true);
    linearButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    linearButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible(linearButton);

    // Headlines
    brandingLabel.setText("CenterComb by aquanode", juce::dontSendNotification);
    brandingLabel.setFont(juce::Font(28.0f, juce::Font::bold));
    brandingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(brandingLabel);

    subtitleLabel.setText("Mind loud peaks extending above the visualizer boundaries if the Limiter is off!", juce::dontSendNotification);
    subtitleLabel.setFont(juce::Font(12.0f, juce::Font::italic));
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
    subtitleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(subtitleLabel);

    spreadModeButton.setClickingTogglesState(true);
    spreadModeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    addAndMakeVisible(spreadModeButton);
    spreadModeAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, "spreadMode", spreadModeButton);

    wetOnlyButton.setClickingTogglesState(true);
    wetOnlyButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    addAndMakeVisible(wetOnlyButton);

    wetOnlyAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "wetOnly", wetOnlyButton);

    addAndMakeVisible(hardLimiterButton);
    hardLimiterButton.setClickingTogglesState(true);
    hardLimiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::cyan);
    hardLimiterButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);

    // Attach to the parameter
    hardLimiterAttachment = std::make_unique<ButtonAttachment>(
        audioProcessor.apvts, "hardLimiter", hardLimiterButton);


    // Change spread mode
    spreadModeButton.onClick = [this]()
        {
            bool isMulti = spreadModeButton.getToggleState();

            spreadHzAttachment.reset();
            spreadRatioAttachment.reset();

            if (isMulti)
            {
                spreadRatioAttachment = std::make_unique<Attachment>(
                    audioProcessor.apvts, "spreadRatio", spreadSlider);
            }
            else
            {
                spreadHzAttachment = std::make_unique<Attachment>(
                    audioProcessor.apvts, "spreadHz", spreadSlider);
            }
        };


    startTimerHz(30);
}

CenterCombAudioProcessorEditor::~CenterCombAudioProcessorEditor() { stopTimer(); }

void CenterCombAudioProcessorEditor::setupSlider(juce::Slider& s, juce::Label& l, juce::String name)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colours::cyan);
    s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible(s);

    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setFont(juce::Font(14.0f, juce::Font::plain));
    addAndMakeVisible(l);
}

void CenterCombAudioProcessorEditor::drawResponseCurve(juce::Graphics& g, juce::Rectangle<int> area)
{
    const float minFreq = 20.0f;
    const float maxFreq = 20000.0f;
    const float sampleRate = 44100.0f;

    // --- Fetch toggles ---
    bool isLinearView = audioProcessor.apvts.getRawParameterValue("linear")->load() > 0.5f;
    bool isMultiplicativeSpread = audioProcessor.apvts.getRawParameterValue("spreadMode")->load() > 0.5f;

    // --- Grid ---
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    auto drawGridLine = [&](float f)
        {
            float normX = isLinearView ? (f - minFreq) / (maxFreq - minFreq)
                : std::log10(f / minFreq) / std::log10(maxFreq / minFreq);
            auto x = juce::jmap(normX, 0.0f, 1.0f, (float)area.getX(), (float)area.getRight());
            if (x >= area.getX() && x <= area.getRight())
                g.drawVerticalLine((int)x, (float)area.getY(), (float)area.getBottom());
        };

    if (isLinearView)
    {
        for (float f = 2000.0f; f <= 20000.0f; f += 2000.0f)
            drawGridLine(f);
    }
    else
    {
        for (float f = 100.0f; f < 20000.0f; f *= 2.0f)
            drawGridLine(f);
    }

    // --- Fetch parameters ---
    float mainGain = audioProcessor.apvts.getRawParameterValue("gain")->load();
    float centerF = audioProcessor.apvts.getRawParameterValue("freq")->load();
    int pairs = static_cast<int>(audioProcessor.apvts.getRawParameterValue("amount")->load());
    float q = audioProcessor.apvts.getRawParameterValue("q")->load();
    float damp = audioProcessor.apvts.getRawParameterValue("damp")->load();

    float spread = isMultiplicativeSpread
        ? audioProcessor.apvts.getRawParameterValue("spreadRatio")->load()
        : audioProcessor.apvts.getRawParameterValue("spreadHz")->load();
    spread = juce::jmax(spread, 0.0001f);

    // --- Frequency to X mapping ---
    auto freqToX = [&](float f)
        {
            float normX = isLinearView ? (f - minFreq) / (maxFreq - minFreq)
                : std::log10(f / minFreq) / std::log10(maxFreq / minFreq);
            return juce::jmap(normX, 0.0f, 1.0f, (float)area.getX(), (float)area.getRight());
        };

    // --- Draw center frequency line ---
    if (centerF >= minFreq && centerF <= maxFreq)
    {
        float x = freqToX(centerF);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawLine(x, (float)area.getY(), x, (float)area.getBottom(), 1.5f);
    }

    // --- Generate response curve ---
    juce::Path curve;
    bool firstPoint = true;
    int activeCount = 1 + (pairs * 2);

    for (int x = area.getX(); x <= area.getRight(); ++x)
    {
        float normX = (x - area.getX()) / (float)area.getWidth();
        float freq = isLinearView ? juce::jmap(normX, 0.0f, 1.0f, minFreq, maxFreq)
            : minFreq * std::pow(maxFreq / minFreq, normX);

        float totalGainDB = 0.0f;

        for (int i = 0; i < activeCount; ++i)
        {
            int offset = i - pairs;
            float fTine;

            if (isMultiplicativeSpread)
                fTine = centerF * std::pow(spread, static_cast<float>(offset));
            else
                fTine = centerF + static_cast<float>(offset) * spread;

            // --- Skip tines outside safe range ---
            if (fTine <= 20.0f || fTine >= sampleRate * 0.5f)
                continue;

            float weight = 1.0f - (damp * (std::abs(offset) / static_cast<float>(pairs + 1)));
            float tineGainDB = mainGain * weight;

            auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate, fTine, q, juce::Decibels::decibelsToGain(tineGainDB));

            float mag = std::abs(coeffs->getMagnitudeForFrequency(freq, sampleRate));
            totalGainDB += juce::Decibels::gainToDecibels(mag);
        }

        float y = juce::jmap(totalGainDB, -40.0f, 40.0f, (float)area.getBottom(), (float)area.getY());

        if (firstPoint)
        {
            curve.startNewSubPath((float)x, y);
            firstPoint = false;
        }
        else
        {
            curve.lineTo((float)x, y);
        }
    }

    g.setColour(juce::Colours::cyan);
    g.strokePath(curve, juce::PathStrokeType(2.5f));
}


void CenterCombAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bg(juce::Colour(0xff00f5d4), 0, 0, juce::Colour(0xff0077b6), 0, (float)getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    auto area = getLocalBounds().reduced(20);
    auto visualizerArea = area.removeFromTop(180);

    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(visualizerArea.toFloat(), 6.0f);

    drawResponseCurve(g, visualizerArea);

    brandingLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    g.setColour(juce::Colours::white.withAlpha(0.3f));
}

void CenterCombAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);

    // --- 1) Visualizer Area ---
    auto visualizerArea = area.removeFromTop(180);

    int topButtonWidth = 80;
    int topButtonHeight = 22;
    int topButtonSpacing = 5;

    // Top-right buttons aligned with visualizer top
    linearButton.setBounds(
        visualizerArea.getRight() - topButtonWidth,
        visualizerArea.getY(),
        topButtonWidth,
        topButtonHeight
    );

    hardLimiterButton.setBounds(
        visualizerArea.getRight() - topButtonWidth,
        visualizerArea.getY() + topButtonHeight + topButtonSpacing,
        topButtonWidth,
        topButtonHeight
    );

    // --- 2) Title Section (Middle-left) with buttons (Middle-right) ---
    auto titleArea = area.removeFromTop(60);
    int padding = 5;
    int buttonWidth = 120;
    int buttonHeight = 22;
    int buttonSpacing = 5;

    // Title & subtitle (left side)
    int labelWidth = titleArea.getWidth() - buttonWidth - padding * 3;
    int titleHeight = 35;

    brandingLabel.setBounds(
        titleArea.getX() + padding,
        titleArea.getY(),
        labelWidth,
        titleHeight
    );
    brandingLabel.setJustificationType(juce::Justification::centredLeft);

    subtitleLabel.setBounds(
        titleArea.getX() + padding,
        titleArea.getY() + titleHeight,
        labelWidth,
        titleArea.getHeight() - titleHeight
    );
    subtitleLabel.setJustificationType(juce::Justification::centredLeft);

    // Spread & Wet Only buttons (right side, vertically centered within titleArea)
    int buttonsX = titleArea.getX() + titleArea.getWidth() - buttonWidth - padding;
    int buttonsY = titleArea.getY() + (titleArea.getHeight() - (buttonHeight * 2 + buttonSpacing)) / 2;

    spreadModeButton.setBounds(buttonsX, buttonsY, buttonWidth, buttonHeight);
    wetOnlyButton.setBounds(buttonsX, buttonsY + buttonHeight + buttonSpacing, buttonWidth, buttonHeight);

    // --- 3) Sliders Section ---
    area.removeFromTop(20); // spacing

    auto row1 = area.removeFromTop(area.getHeight() / 2);
    auto row2 = area;
    int w = row1.getWidth() / 3;

    auto setupCell = [](juce::Rectangle<int> r, juce::Slider& s, juce::Label& l) {
        l.setBounds(r.removeFromTop(20));
        s.setBounds(r);
        };

    // Row 1: Freq, Gain, Q
    setupCell(row1.removeFromLeft(w), freqSlider, freqLabel);
    setupCell(row1.removeFromLeft(w), gainSlider, gainLabel);
    setupCell(row1, qSlider, qLabel);

    // Row 2: Amount, Spread, Damp
    setupCell(row2.removeFromLeft(w), amountSlider, amountLabel);
    setupCell(row2.removeFromLeft(w), spreadSlider, spreadLabel);
    setupCell(row2, dampSlider, dampLabel);
}



void CenterCombAudioProcessorEditor::timerCallback()
{
    bool isMulti = audioProcessor.apvts.getRawParameterValue("spreadMode")->load() > 0.5f;

    spreadSlider.setTextValueSuffix(isMulti ? " x" : " Hz");

    repaint();
}
