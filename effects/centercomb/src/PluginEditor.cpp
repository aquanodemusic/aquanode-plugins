#include "PluginProcessor.h"
#include "PluginEditor.h"

static float wrapFrequency(float freq, float minFreq, float maxFreq)
{
    float range = maxFreq - minFreq;
    float x = std::fmod(freq - minFreq, range);
    if (x < 0.0f)
        x += range;
    return minFreq + x;
}

static float mirrorFrequency(float freq, float minFreq, float maxFreq)
{
    float range = maxFreq - minFreq;
    float period = 2.0f * range;

    float x = std::fmod(freq - minFreq, period);
    if (x < 0.0f)
        x += period;

    if (x > range)
        x = period - x;

    return minFreq + x;
}

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
    linearButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible(linearButton);

    // --- Frequency Mode ComboBox ---
    freqModeBox.addItem("Regular Mode", 1);
    freqModeBox.addItem("Wrap Mode", 2);
    freqModeBox.addItem("Mirror Mode", 3);
    freqModeBox.setSelectedId(1); // default

    freqModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colours::cyan);
    freqModeBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    freqModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    freqModeBox.setColour(juce::ComboBox::arrowColourId, juce::Colours::white);

    addAndMakeVisible(freqModeBox);

    

    // APVTS attachment (parameter comes later in processor)
    freqModeAttachment = std::make_unique<ChoiceAttachment>(
        audioProcessor.apvts, "freqMode", freqModeBox);


    // Headlines
    brandingLabel.setText("CenterComb by aquanode", juce::dontSendNotification);
    brandingLabel.setFont(juce::Font(28.0f, juce::Font::bold));
    brandingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(brandingLabel);

    subtitleLabel.setText("Using Wrap/Mirror Mode while Multiplicative is active is an experimental feature!", juce::dontSendNotification);
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

    // --- Smooth Slider Setup ---
    setupSlider(smoothSlider, smoothLabel, "Transition Smoothing Time");

    // Customize for the tighter space in the title area
    smoothSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    smoothLabel.setFont(juce::Font(12.0f, juce::Font::plain));
    smoothLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    // Attachment (Make sure the string "smoothTime" matches your APVTS layout)
    smoothAttachment = std::make_unique<Attachment>(audioProcessor.apvts, "smoothTime", smoothSlider);

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
    // If you want the 10Hz line to be visible, minFreq must be 10.0f
    const float minFreq = 10.0f;
    const float maxFreq = 20000.0f;
    const float visualizerSR = 44100.0f;

    // --- 1. GATHER VALUES ---
    // Pulling from smoothedParams (requires smoothedParams to be public in Processor)
    bool isLinearView = audioProcessor.apvts.getRawParameterValue("linear")->load() > 0.5f;
    bool isMulti = audioProcessor.apvts.getRawParameterValue("spreadMode")->load() > 0.5f;
    int freqMode = static_cast<int>(audioProcessor.apvts.getRawParameterValue("freqMode")->load());

    float mainGain = audioProcessor.smoothedParams.gain.getCurrentValue();
    float centerF = audioProcessor.smoothedParams.freq.getCurrentValue();
    float q = audioProcessor.smoothedParams.q.getCurrentValue();
    float damp = audioProcessor.smoothedParams.damp.getCurrentValue();

    float spread = isMulti ? audioProcessor.smoothedParams.spreadRatio.getCurrentValue()
        : audioProcessor.smoothedParams.spreadHz.getCurrentValue();
    spread = juce::jmax(spread, 0.0001f);

    int pairs = static_cast<int>(audioProcessor.apvts.getRawParameterValue("amount")->load());

    const float lowLimit = 20.0f;
    const float highLimit = visualizerSR * 0.475f;

    // Helper lambda for frequency mapping
    auto freqToX = [&](float f) {
        float normX = isLinearView ? (f - minFreq) / (maxFreq - minFreq)
            : std::log10(f / minFreq) / std::log10(maxFreq / minFreq);
        return juce::jmap(normX, 0.0f, 1.0f, (float)area.getX(), (float)area.getRight());
        };

    // --- 2. GRID DRAWING (Decade Lines) ---
    g.setColour(juce::Colours::white.withAlpha(0.2f));

    std::vector<float> decades = { 10.0f, 100.0f, 1000.0f, 10000.0f };

    for (auto f : decades)
    {
        float x = freqToX(f);

        if (x >= area.getX() && x <= area.getRight())
        {
            // Draw the vertical grid line
            g.drawVerticalLine(juce::roundToInt(x), (float)area.getY(), (float)area.getBottom());

            // Draw the text labels
            g.setColour(juce::Colours::white.withAlpha(0.4f));
            g.setFont(11.0f);
            juce::String label = (f >= 1000.0f) ? juce::String(f / 1000.0f, 0) + "k" : juce::String(f, 0);
            g.drawText(label, juce::roundToInt(x) + 3, area.getBottom() - 15, 40, 15, juce::Justification::left);

            g.setColour(juce::Colours::white.withAlpha(0.2f)); // Reset for next line
        }
    }

    // Optional: Draw the "Hard Bounds" (20Hz and 20kHz) in a subtle cyan
    g.setColour(juce::Colours::cyan.withAlpha(0.2f));
    g.drawVerticalLine(juce::roundToInt(freqToX(20.0f)), (float)area.getY(), (float)area.getBottom());
    g.drawVerticalLine(juce::roundToInt(freqToX(20000.0f)), (float)area.getY(), (float)area.getBottom());

    // --- 3. PRE-CALCULATE ACTIVE FILTERS ---
    struct FilterData { juce::dsp::IIR::Coefficients<float>::Ptr coeffs; };
    std::vector<FilterData> activeFilters;
    activeFilters.reserve(1 + (pairs * 2));

    for (int i = 0; i < (1 + (pairs * 2)); ++i)
    {
        int offset = i - pairs;
        double rawTineF = centerF;

        if (isMulti)
            rawTineF = (double)centerF * std::pow((double)spread, (double)offset);
        else
            rawTineF = (double)centerF + ((double)offset * (double)spread);

        float fTine = static_cast<float>(rawTineF);
        if (freqMode == 1)      fTine = wrapFrequency(fTine, lowLimit, highLimit);
        else if (freqMode == 2) fTine = mirrorFrequency(fTine, lowLimit, highLimit);

        if (fTine >= lowLimit && fTine <= highLimit && std::isfinite(fTine))
        {
            float weight = 1.0f - (damp * (std::abs(offset) / static_cast<float>(pairs + 1)));
            activeFilters.push_back({ juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                visualizerSR, fTine, q, juce::Decibels::decibelsToGain(mainGain * weight)) });
        }
    }

    // --- 4. DRAW RESPONSE CURVE ---
    juce::Path curve;
    bool firstPoint = true;

    for (int x = area.getX(); x <= area.getRight(); ++x)
    {
        float normX = (x - area.getX()) / (float)area.getWidth();
        float currentFreq = isLinearView ? juce::jmap(normX, 0.0f, 1.0f, minFreq, maxFreq)
            : minFreq * std::pow(maxFreq / minFreq, normX);
        float totalGainDB = 0.0f;

        for (auto& f : activeFilters)
        {
            float mag = std::abs(f.coeffs->getMagnitudeForFrequency(currentFreq, visualizerSR));
            totalGainDB += juce::Decibels::gainToDecibels(mag);
        }

        float y = juce::jmap(totalGainDB, -40.0f, 40.0f, (float)area.getBottom(), (float)area.getY());

        if (firstPoint) {
            curve.startNewSubPath((float)x, y);
            firstPoint = false;
        }
        else {
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

    // --- 1) Visualizer Area (Completely Free at the Top) ---
    auto visualizerArea = area.removeFromTop(180);

    // --- 2) Title Section (Middle) ---
    auto titleArea = area.removeFromTop(60);
    brandingLabel.setBounds(titleArea.removeFromTop(35));
    subtitleLabel.setBounds(titleArea);

    area.removeFromTop(15); // Spacer before controls

    // --- 3) Sidebar Area (Bottom Right) ---
    // We increase this slightly to 150 to give the knob more horizontal room
    auto sidebarArea = area.removeFromRight(150);
    area.removeFromRight(20); // Spacer between the 6 knobs and the sidebar

    // --- 4) Main Controls (The Six Knobs) ---
    auto mainControlsArea = area;
    auto row1 = mainControlsArea.removeFromTop(mainControlsArea.getHeight() / 2);
    auto row2 = mainControlsArea;

    int columnWidth = mainControlsArea.getWidth() / 3;

    auto setupCell = [](juce::Rectangle<int> r, juce::Slider& s, juce::Label& l) {
        l.setBounds(r.removeFromTop(20));
        s.setBounds(r);
        };

    setupCell(row1.removeFromLeft(columnWidth), freqSlider, freqLabel);
    setupCell(row1.removeFromLeft(columnWidth), gainSlider, gainLabel);
    setupCell(row1, qSlider, qLabel);

    setupCell(row2.removeFromLeft(columnWidth), amountSlider, amountLabel);
    setupCell(row2.removeFromLeft(columnWidth), spreadSlider, spreadLabel);
    setupCell(row2, dampSlider, dampLabel);

    // --- 5) Sidebar Column Stack ---
    int buttonHeight = 25;
    int spacing = 5;

    // Place the 4 top buttons
    linearButton.setBounds(sidebarArea.removeFromTop(buttonHeight));
    sidebarArea.removeFromTop(spacing);

    hardLimiterButton.setBounds(sidebarArea.removeFromTop(buttonHeight));
    sidebarArea.removeFromTop(spacing);

    spreadModeButton.setBounds(sidebarArea.removeFromTop(buttonHeight));
    sidebarArea.removeFromTop(spacing);

    wetOnlyButton.setBounds(sidebarArea.removeFromTop(buttonHeight));
    sidebarArea.removeFromTop(spacing);

    // Mode Selector
    freqModeBox.setBounds(sidebarArea.removeFromTop(buttonHeight));
    sidebarArea.removeFromTop(spacing * 2);

    // Smoothing Section (Larger Knob)
    // We use the rest of the sidebarArea for the knob
    smoothLabel.setBounds(sidebarArea.removeFromTop(20));

    // This will now fill the remaining width and a good portion of the height
    // Reducing the sidebarArea further ensures the knob has a square-ish aspect ratio
    smoothSlider.setBounds(sidebarArea.reduced(10, 0).removeFromTop(sidebarArea.getWidth() - 20));
}

void CenterCombAudioProcessorEditor::timerCallback()
{
    bool isMulti = audioProcessor.apvts.getRawParameterValue("spreadMode")->load() > 0.5f;

    spreadSlider.setTextValueSuffix(isMulti ? " x" : " Hz");

    repaint();
}
