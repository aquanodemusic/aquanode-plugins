#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
EQCurveComponent::EQCurveComponent(PhorestAudioProcessor& p)
    : processor(p)
{
    startTimerHz(30); // 60 FPS refresh rate for smoother animation
}

void EQCurveComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Ink-wash bamboo background — rich natural green, no neon
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(18, 52, 26), bounds.getTopLeft(),
        juce::Colour(12, 40, 20), bounds.getBottomRight(),
        false));
    g.fillRoundedRectangle(bounds, 8.0f);

    // Border — crisp when natural, blooming when glow is on
    if (glowEnabled)
    {
        for (int i = 2; i >= 0; --i)
        {
            float alpha = 0.4f - (i * 0.1f);
            g.setColour(juce::Colour(0, 201, 87).withAlpha(alpha));
            g.drawRoundedRectangle(bounds.reduced(i * -1.5f), 8.0f, 2.0f);
        }
    }
    else
    {
        g.setColour(juce::Colour(60, 130, 70).withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);
    }

    // Left margin for dB labels, small right margin, top/bottom for freq labels and title
    auto graphBounds = bounds.withTrimmedLeft(40).withTrimmedRight(8).withTrimmedTop(30).withTrimmedBottom(30);

    // Draw frequency and gain grids
    drawFrequencyGrid(g, graphBounds);
    drawGainGrid(g, graphBounds);

    // Get frequency response data
    auto response = processor.getFrequencyResponse();

    // Get current parameters for display
    auto& apvts = processor.getValueTreeState();
    int lfoShape = static_cast<int>(apvts.getRawParameterValue("lfoShape")->load());
    int stages = static_cast<int>(apvts.getRawParameterValue("stages")->load());
    float feedback = apvts.getRawParameterValue("feedback")->load();
    float detune = apvts.getRawParameterValue("detune")->load();
    float stereoPhase = apvts.getRawParameterValue("stereo")->load();

    // Draw 0dB reference line more prominently
    float zeroDbY = graphBounds.getBottom() - 0.75f * graphBounds.getHeight();
    g.setColour(juce::Colour(152, 255, 152).withAlpha(0.3f));
    g.drawHorizontalLine(static_cast<int>(zeroDbY), graphBounds.getX(), graphBounds.getRight());

    // Draw the frequency response curve
    juce::Path responseCurve;
    bool firstPoint = true;

    for (int i = 0; i < response.numPoints; ++i)
    {
        float freq = response.frequencies[i];
        float magnitude = response.magnitudes[i];

        // Convert to screen coordinates
        float x = graphBounds.getX() + frequencyToX(freq, graphBounds.getWidth());
        float y = graphBounds.getBottom() - magnitude * graphBounds.getHeight();

        if (firstPoint)
        {
            responseCurve.startNewSubPath(x, y);
            firstPoint = false;
        }
        else
        {
            responseCurve.lineTo(x, y);
        }
    }

    // Draw glowing response curve — multi-layer bloom only when glow is on
    if (glowEnabled)
    {
        for (int glow = 4; glow >= 0; --glow)
        {
            float alpha = 0.25f - (glow * 0.04f);
            float thickness = 2.0f + (glow * 3.0f);
            g.setColour(juce::Colour(50, 255, 50).withAlpha(alpha));
            g.strokePath(responseCurve, juce::PathStrokeType(thickness,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Main curve — bright lime when glowing, soft sage when natural
    g.setColour(glowEnabled ? juce::Colour(50, 255, 50) : juce::Colour(100, 200, 110));
    g.strokePath(responseCurve, juce::PathStrokeType(glowEnabled ? 3.5f : 2.5f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Soft fill under curve
    juce::Path fillPath = responseCurve;
    fillPath.lineTo(graphBounds.getRight(), graphBounds.getBottom());
    fillPath.lineTo(graphBounds.getX(), graphBounds.getBottom());
    fillPath.closeSubPath();

    if (glowEnabled)
    {
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(50, 255, 50).withAlpha(0.2f), graphBounds.getCentreX(), graphBounds.getY(),
            juce::Colour(0, 201, 87).withAlpha(0.05f), graphBounds.getCentreX(), graphBounds.getBottom(),
            false));
    }
    else
    {
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(80, 170, 90).withAlpha(0.12f), graphBounds.getCentreX(), graphBounds.getY(),
            juce::Colour(40, 110, 50).withAlpha(0.03f), graphBounds.getCentreX(), graphBounds.getBottom(),
            false));
    }
    g.fillPath(fillPath);

    // Title
    g.setColour(glowEnabled ? juce::Colour(152, 255, 152) : juce::Colour(120, 185, 128));
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("FREQUENCY RESPONSE", bounds.getX() + 10, bounds.getY() + 5,
        bounds.getWidth() - 20, 20, juce::Justification::centred);
}

void EQCurveComponent::drawFrequencyGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Draw vertical frequency grid lines
    std::vector<float> frequencies = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };

    g.setFont(juce::Font(10.0f, juce::Font::bold));

    for (float freq : frequencies)
    {
        float x = bounds.getX() + frequencyToX(freq, bounds.getWidth());

        // Make major lines (100, 1k, 10k) more visible
        if (freq == 100.0f || freq == 1000.0f || freq == 10000.0f)
        {
            g.setColour(juce::Colour(34, 139, 34).withAlpha(0.4f));
            g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
        }
        else
        {
            g.setColour(juce::Colour(34, 139, 34).withAlpha(0.2f));
            g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
        }

        // Draw frequency labels with glow
        juce::String label;
        if (freq >= 1000)
            label = juce::String(freq / 1000.0f, 1) + "k";
        else
            label = juce::String(static_cast<int>(freq));

        // Text glow
        g.setColour(juce::Colour(0, 201, 87).withAlpha(0.3f));
        g.drawText(label, x - 21, bounds.getBottom() + 1, 40, 12, juce::Justification::centred);
        g.drawText(label, x - 19, bounds.getBottom() + 1, 40, 12, juce::Justification::centred);
        g.drawText(label, x - 20, bounds.getBottom(), 40, 12, juce::Justification::centred);
        g.drawText(label, x - 20, bounds.getBottom() + 2, 40, 12, juce::Justification::centred);

        // Main text
        g.setColour(juce::Colour(152, 255, 152));
        g.drawText(label, x - 20, bounds.getBottom() + 1, 40, 12, juce::Justification::centred);
    }
}

void EQCurveComponent::drawGainGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Draw horizontal gain grid lines
    std::vector<float> gains = { -18, -12, -6, 0, 6 }; // dB - extended range to show notches

    g.setFont(juce::Font(10.0f, juce::Font::bold));

    for (float gainDb : gains)
    {
        // Map from dB to 0-1 range (now -18 to +6 dB range)
        float normalized = (gainDb + 18.0f) / 24.0f;
        float y = bounds.getBottom() - normalized * bounds.getHeight();

        // Highlight 0 dB line more prominently
        if (gainDb == 0.0f)
        {
            g.setColour(juce::Colour(152, 255, 152).withAlpha(0.5f));
            g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());

            // Draw 0dB marker with extra emphasis
            g.setColour(juce::Colour(152, 255, 152).withAlpha(0.8f));
            juce::String label = "0dB";

            // Text glow
            g.setColour(juce::Colour(0, 201, 87).withAlpha(0.4f));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx != 0 || dy != 0)
                        g.drawText(label, bounds.getX() - 34 + dx, y - 6 + dy, 30, 12, juce::Justification::right);

            // Main text
            g.setColour(juce::Colour(152, 255, 152));
            g.drawText(label, bounds.getX() - 35, y - 6, 30, 12, juce::Justification::right);
        }
        else
        {
            g.setColour(juce::Colour(34, 139, 34).withAlpha(0.25f));
            g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());

            // Draw gain labels
            g.setColour(juce::Colour(152, 255, 152).withAlpha(0.7f));
            juce::String label = juce::String(static_cast<int>(gainDb)) + "dB";
            g.drawText(label, bounds.getX() - 35, y - 6, 30, 12, juce::Justification::right);
        }
    }
}

float EQCurveComponent::frequencyToX(float freq, float width)
{
    // Logarithmic mapping from 20Hz to 20kHz
    float minFreq = 20.0f;
    float maxFreq = 20000.0f;

    float normalized = std::log(freq / minFreq) / std::log(maxFreq / minFreq);
    return normalized * width;
}

void EQCurveComponent::timerCallback()
{
    repaint();
}

void EQCurveComponent::resized()
{
}

//==============================================================================
VibrantLookAndFeel::VibrantLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, aquaGreen);
    setColour(juce::Slider::rotarySliderFillColourId, limeGreen);
    setColour(juce::Slider::rotarySliderOutlineColourId, forestGreen);
    setColour(juce::Slider::trackColourId, bgMid);
    setColour(juce::Label::textColourId, mintGreen);
}

void VibrantLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
    juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = juce::jmin(8.0f, radius * 0.5f);
    auto arcRadius = radius - lineW * 0.5f;

    // Dark forest center
    g.setColour(bgDark);
    g.fillEllipse(bounds);

    // Outer ring — neon bloom when glow on, subtle moss ring when off
    if (glowEnabled)
    {
        for (int i = 3; i >= 0; --i)
        {
            float alpha = 0.2f - (i * 0.04f);
            g.setColour(emeraldGreen.withAlpha(alpha));
            g.drawEllipse(bounds.expanded(i * 2), i + 1);
        }
    }
    else
    {
        g.setColour(juce::Colour(50, 100, 60).withAlpha(0.35f));
        g.drawEllipse(bounds.expanded(1), 1.0f);
    }

    // Draw track background
    juce::Path backgroundArc;
    backgroundArc.addCentredArc(bounds.getCentreX(),
        bounds.getCentreY(),
        arcRadius,
        arcRadius,
        0.0f,
        rotaryStartAngle,
        rotaryEndAngle,
        true);

    g.setColour(bgMid);
    g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc(bounds.getCentreX(),
            bounds.getCentreY(),
            arcRadius,
            arcRadius,
            0.0f,
            rotaryStartAngle,
            toAngle,
            true);

        if (glowEnabled)
        {
            // Neon gradient arc + luminous bloom
            juce::ColourGradient gradient(emeraldGreen, bounds.getX(), bounds.getCentreY(),
                limeGreen, bounds.getRight(), bounds.getCentreY(), false);
            g.setGradientFill(gradient);
            g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(limeGreen.withAlpha(0.5f));
            g.strokePath(valueArc, juce::PathStrokeType(lineW + 2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            // Muted celadon arc — readable but not harsh
            juce::ColourGradient gradient(juce::Colour(45, 130, 65), bounds.getX(), bounds.getCentreY(),
                juce::Colour(70, 160, 80), bounds.getRight(), bounds.getCentreY(), false);
            g.setGradientFill(gradient);
            g.strokePath(valueArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Pointer
    juce::Path pointer;
    auto pointerLength = radius * 0.5f;
    auto pointerThickness = 3.5f;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -radius + lineW * 2,
        pointerThickness, pointerLength, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(toAngle).translated(bounds.getCentreX(), bounds.getCentreY()));

    if (glowEnabled)
    {
        g.setColour(aquaGreen.withAlpha(0.7f));
        g.fillPath(pointer);
        g.setColour(mintGreen);
        g.fillPath(pointer);
    }
    else
    {
        // Soft sage pointer — no bloom
        g.setColour(juce::Colour(100, 175, 115).withAlpha(0.75f));
        g.fillPath(pointer);
    }
}

juce::Label* VibrantLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = new juce::Label();
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::textColourId, aquaGreen);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setFont(juce::Font(12.0f, juce::Font::bold));
    return l;
}

void VibrantLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (!label.isBeingEdited())
    {
        auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const juce::Font font(getLabelFont(label));

        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(font);

        auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), textArea, label.getJustificationType(),
            juce::jmax(1, (int)(textArea.getHeight() / font.getHeight())),
            label.getMinimumHorizontalScale());
    }
}

void VibrantLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
    const juce::Colour&,
    bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    bool isOn = button.getToggleState();

    // Lantern circle background
    if (isOn)
    {
        // Lit lantern — warm glowing core
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0, 60, 20), bounds.getCentreX(), bounds.getY(),
            juce::Colour(0, 30, 12), bounds.getCentreX(), bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 6.0f);

        // Neon bloom border
        for (int i = 2; i >= 0; --i)
        {
            float alpha = isHighlighted ? 0.7f - i * 0.15f : 0.5f - i * 0.12f;
            g.setColour(juce::Colour(0, 220, 90).withAlpha(alpha));
            g.drawRoundedRectangle(bounds.reduced(i * -0.8f), 6.0f, 1.5f);
        }
    }
    else
    {
        // Unlit — natural bamboo dark
        g.setColour(juce::Colour(18, 48, 24).withAlpha(isHighlighted ? 0.9f : 0.7f));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colour(55, 110, 65).withAlpha(0.5f));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
}

void VibrantLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
    bool isHighlighted, bool)
{
    bool isOn = button.getToggleState();

    if (isOn && isHighlighted)
        g.setColour(juce::Colour(200, 255, 200));
    else if (isOn)
        g.setColour(juce::Colour(50, 255, 50));
    else
        g.setColour(juce::Colour(90, 155, 100));

    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.drawText(button.getButtonText(), button.getLocalBounds(),
        juce::Justification::centred, false);
}

//==============================================================================
CustomRotarySlider::CustomRotarySlider(const juce::String& labelText, const juce::String& paramID,
    juce::AudioProcessorValueTreeState& apvts, bool isToggle)
{
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    // --- ENABLE DOUBLE-CLICK TO RESET ---
    // 1. Find the parameter in the APVTS to get its actual default value
    if (auto* param = apvts.getParameter(paramID))
    {
        float defaultValue = param->getDefaultValue();
        // Convert normalized default (0-1) to the slider's range values
        float rangeDefault = param->getNormalisableRange().convertFrom0to1(defaultValue);
        slider.setDoubleClickReturnValue(true, rangeDefault);
    }

    addAndMakeVisible(label);
    label.setText(labelText, juce::dontSendNotification);

    addAndMakeVisible(valueLabel);
    valueLabel.setEditable(true);
    valueLabel.setJustificationType(juce::Justification::centred);

    // Text entry: strips any unit suffix, parses full double precision
    valueLabel.onTextChange = [this, paramID]() {
        auto text = valueLabel.getText().trim().toLowerCase();
        if (paramID == "lfoShape") {
            if (text.contains("man")) slider.setValue(0);
            else if (text.contains("sin")) slider.setValue(1);
            else if (text.contains("tri")) slider.setValue(2);
            else if (text.contains("up"))  slider.setValue(3);
            else if (text.contains("down")) slider.setValue(4);
            else if (text.contains("sq"))   slider.setValue(5);
            else if (text.contains("tan") || text.contains("tanh")) slider.setValue(6);
            else slider.setValue(text.getDoubleValue());
        }
        else if (paramID == "minDepth" || paramID == "maxDepth") {
            // Accept Hz value (e.g. "500 Hz" or "500") or raw 0-1 value
            double val = text.retainCharacters("0123456789.-").getDoubleValue();
            // If the value is > 1, treat as Hz and convert to 0-1
            if (val > 1.0) val = (val - 20.0) / (4000.0 - 20.0);
            val = juce::jlimit(0.0, 1.0, val);
            slider.setValue(val, juce::sendNotificationSync);
        }
        else if (paramID == "dryWet" || paramID == "feedback" || paramID == "detune") {
            // Accept "0.85" or "85%" - normalise % to 0-1
            double val = text.retainCharacters("0123456789.-").getDoubleValue();
            if (text.containsChar('%') || val > 1.0) val /= 100.0;
            slider.setValue(val, juce::sendNotificationSync);
        }
        else if (paramID == "stereo") {
            // Accept "0.25" or "90deg" - normalise degrees to 0-1
            double val = text.retainCharacters("0123456789.-").getDoubleValue();
            if (text.contains("deg") || val > 1.0) val /= 360.0;
            slider.setValue(val, juce::sendNotificationSync);
        }
        else {
            // General: strip any trailing unit letters, parse the number
            double val = text.retainCharacters("0123456789.-").getDoubleValue();
            slider.setValue(val, juce::sendNotificationSync);
        }
        };

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, slider);

    // Force fully smooth continuous dragging — the attachment may have synced a non-zero
    // interval from the parameter range. Override it to 0 for all non-integer params.
    if (paramID != "stages" && paramID != "lfoShape")
        slider.setRange(slider.getMinimum(), slider.getMaximum(), 0.0);

    // Helper: format a double with up to 6 significant decimals, stripping trailing zeros
    auto formatPrecise = [](double v, int maxDecimals = 6) -> juce::String {
        juce::String s = juce::String(v, maxDecimals);
        // Strip trailing zeros after decimal point
        if (s.containsChar('.'))
        {
            while (s.getLastCharacter() == '0') s = s.dropLastCharacters(1);
            if (s.getLastCharacter() == '.')   s = s.dropLastCharacters(1);
        }
        return s;
        };

    // Display logic
    slider.onValueChange = [this, paramID, formatPrecise]() {
        if (paramID == "lfoShape") {
            int val = (int)slider.getValue();
            juce::String names[] = { "Manual", "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Tanh" };
            valueLabel.setText(names[juce::jlimit(0, 6, val)], juce::dontSendNotification);
        }
        else if (paramID == "sweepFreq")
            valueLabel.setText(formatPrecise(slider.getValue(), 6) + " Hz", juce::dontSendNotification);
        else if (paramID == "minDepth" || paramID == "maxDepth") {
            // Depth 0-1 maps to allpass sweep range 20Hz–4000Hz
            double hz = 20.0 + slider.getValue() * (4000.0 - 20.0);
            valueLabel.setText(formatPrecise(hz, 4) + " Hz", juce::dontSendNotification);
        }
        else if (paramID == "freqRange")
            valueLabel.setText(formatPrecise(slider.getValue(), 6) + "x", juce::dontSendNotification);
        else if (paramID == "stereo")
            valueLabel.setText(formatPrecise(slider.getValue() * 360.0, 4) + "deg", juce::dontSendNotification);
        else if (paramID == "dryWet" || paramID == "feedback" || paramID == "detune")
            valueLabel.setText(formatPrecise(slider.getValue() * 100.0, 4) + "%", juce::dontSendNotification);
        else
            valueLabel.setText(formatPrecise(slider.getValue(), 6), juce::dontSendNotification);
        };
    slider.onValueChange();
}

void CustomRotarySlider::paint(juce::Graphics& g)
{
    // Background with forest green tint
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(10, 25, 15).withAlpha(0.7f));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Subtle emerald border
    g.setColour(juce::Colour(0, 201, 87).withAlpha(0.4f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.5f);
}

void CustomRotarySlider::resized()
{
    auto bounds = getLocalBounds();

    label.setBounds(bounds.removeFromTop(16));
    valueLabel.setBounds(bounds.removeFromBottom(18));
    slider.setBounds(bounds.reduced(2));
}

//==============================================================================
PhorestAudioProcessorEditor::PhorestAudioProcessorEditor(PhorestAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    sweepFreqSlider("SPEED", "sweepFreq", p.getValueTreeState()),
    minDepthSlider("MIN", "minDepth", p.getValueTreeState()),
    maxDepthSlider("MAX", "maxDepth", p.getValueTreeState()),
    freqRangeSlider("RANGE", "freqRange", p.getValueTreeState()),
    stereoSlider("STEREO", "stereo", p.getValueTreeState()),
    stagesSlider("STAGES", "stages", p.getValueTreeState()),
    feedbackSlider("FDBK", "feedback", p.getValueTreeState()),
    dryWetSlider("MIX", "dryWet", p.getValueTreeState()),
    lfoShapeSlider("SHAPE", "lfoShape", p.getValueTreeState()),
    manualPosSlider("POS", "manualPos", p.getValueTreeState()),
    detuneSlider("DETUNE", "detune", p.getValueTreeState()),
    dryCancelSlider("CANCEL", "dryCancel", p.getValueTreeState()),
    eqCurveDisplay(p)
{
    setLookAndFeel(&vibrantLookAndFeel);

    // Title
    addAndMakeVisible(titleLabel);
    titleLabel.setText("PHOREST", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(32.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);

    // Add sliders
    addAndMakeVisible(sweepFreqSlider);
    addAndMakeVisible(minDepthSlider);
    addAndMakeVisible(maxDepthSlider);
    addAndMakeVisible(freqRangeSlider);
    addAndMakeVisible(stereoSlider);
    addAndMakeVisible(stagesSlider);
    addAndMakeVisible(feedbackSlider);
    addAndMakeVisible(dryWetSlider);
    addAndMakeVisible(lfoShapeSlider);
    addAndMakeVisible(manualPosSlider);
    addAndMakeVisible(detuneSlider);
    addAndMakeVisible(dryCancelSlider);

    // Update manual position slider visibility when shape changes
    p.getValueTreeState().addParameterListener("lfoShape", this);

    // Add EQ curve display
    addAndMakeVisible(eqCurveDisplay);

    // Glow button — top right corner, lantern style
    addAndMakeVisible(glowButton);
    glowButton.setButtonText("GLOW");
    glowButton.setClickingTogglesState(true);
    glowButton.setToggleState(true, juce::dontSendNotification);
    glowButton.onClick = [this]() {
        glowEnabled = glowButton.getToggleState();
        vibrantLookAndFeel.setGlowEnabled(glowEnabled);
        eqCurveDisplay.setGlowEnabled(glowEnabled);
        if (glowEnabled)
            startTimerHz(30);
        else
            stopTimer();
        repaint();
        // Force all child knobs to repaint with updated look and feel
        for (int i = 0; i < getNumChildComponents(); ++i)
            getChildComponent(i)->repaint();
        };

    startTimerHz(30); // start animating immediately (glow is on by default)

    setSize(550, 550);
}

PhorestAudioProcessorEditor::~PhorestAudioProcessorEditor()
{
    audioProcessor.getValueTreeState().removeParameterListener("lfoShape", this);
    setLookAndFeel(nullptr);
}

void PhorestAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "lfoShape")
    {
        // Update manual position slider visibility when shape changes
        bool isManualMode = static_cast<int>(newValue) == 0;
        manualPosSlider.setVisible(isManualMode);
        manualPosSlider.setEnabled(isManualMode);
        manualPosSlider.setAlpha(isManualMode ? 1.0f : 0.3f);
    }
}

void PhorestAudioProcessorEditor::timerCallback()
{
    // Clearly visible drift — different speeds so circles never sync up
    circlePhase0 += 0.025f;   // ~7 sec full cycle
    circlePhase1 += 0.017f;   // ~10 sec full cycle
    circlePhase2 += 0.033f;   // ~5 sec full cycle
    repaint();
}

//==============================================================================
void PhorestAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Japanese ink-wash bamboo grove — rich layered greens
    juce::ColourGradient gradient(
        juce::Colour(22, 62, 30), 0, 0,
        juce::Colour(16, 50, 24), 0, static_cast<float>(getHeight()),
        false);
    gradient.addColour(0.35, juce::Colour(30, 78, 38));
    gradient.addColour(0.65, juce::Colour(24, 68, 32));
    g.setGradientFill(gradient);
    g.fillAll();

    auto bounds = getLocalBounds().toFloat();

    if (glowEnabled)
    {
        // Animated ambient glow circles — drift gently like light through leaves
        float cx0 = -40.0f + std::sin(circlePhase0) * 70.0f;
        float cy0 = -30.0f + std::cos(circlePhase0 * 0.7f) * 55.0f;

        float cx1 = getWidth() - 220.0f + std::sin(circlePhase1 + 0.8f) * 80.0f;
        float cy1 = getHeight() - 200.0f + std::cos(circlePhase1) * 65.0f;

        float cx2 = getWidth() * 0.3f + std::sin(circlePhase2) * 55.0f;
        float cy2 = getHeight() * 0.4f + std::cos(circlePhase2 * 0.9f) * 50.0f;

        // Soft pulsing alpha — breathes gently
        float pulse0 = 0.16f + std::sin(circlePhase0 * 1.3f) * 0.05f;
        float pulse1 = 0.22f + std::sin(circlePhase1 * 1.1f) * 0.06f;
        float pulse2 = 0.09f + std::sin(circlePhase2 * 1.5f) * 0.03f;

        g.setColour(juce::Colour(45, 105, 55).withAlpha(pulse0));
        g.fillEllipse(cx0, cy0, 280, 220);

        g.setColour(juce::Colour(15, 48, 22).withAlpha(pulse1));
        g.fillEllipse(cx1, cy1, 260, 230);

        g.setColour(juce::Colour(55, 120, 62).withAlpha(pulse2));
        g.fillEllipse(cx2, cy2, 200, 180);

        // Electric neon border bloom
        for (int i = 3; i >= 0; --i)
        {
            float alpha = 0.3f - (i * 0.06f);
            g.setColour(juce::Colour(0, 201, 87).withAlpha(alpha));
            g.drawRect(bounds.reduced(i * -1.5f), 2.0f + i);
        }
        // Neon ambient spots
        g.setColour(juce::Colour(0, 201, 87).withAlpha(0.10f));
        g.fillEllipse(80, 80, 260, 260);
        g.setColour(juce::Colour(50, 255, 50).withAlpha(0.08f));
        g.fillEllipse(getWidth() - 280, getHeight() - 280, 290, 290);
    }
    else
    {
        // Static ink-wash depth patches — natural mode
        g.setColour(juce::Colour(45, 105, 55).withAlpha(0.18f));
        g.fillEllipse(-40, -30, 280, 220);
        g.setColour(juce::Colour(15, 48, 22).withAlpha(0.22f));
        g.fillEllipse(getWidth() - 220, getHeight() - 200, 260, 230);
        g.setColour(juce::Colour(55, 120, 62).withAlpha(0.10f));
        g.fillEllipse(getWidth() * 0.3f, getHeight() * 0.4f, 200, 180);

        // Subtle natural border — like a lacquer frame edge
        g.setColour(juce::Colour(65, 140, 75).withAlpha(0.45f));
        g.drawRect(bounds, 1.5f);
        g.setColour(juce::Colour(40, 100, 50).withAlpha(0.25f));
        g.drawRect(bounds.expanded(1.5f), 1.0f);
    }

    // Title
    g.setFont(juce::Font(32.0f, juce::Font::bold));

    if (glowEnabled)
    {
        // Neon glow layers behind title
        for (int glow = 4; glow > 0; --glow)
        {
            g.setColour(juce::Colour(50, 255, 50).withAlpha(0.14f));
            g.drawText("PHOREST", 0, 10 - glow, getWidth(), 40, juce::Justification::centred);
            g.drawText("PHOREST", 0, 10 + glow, getWidth(), 40, juce::Justification::centred);
        }
        juce::ColourGradient titleGradient(
            juce::Colour(0, 201, 87), getWidth() * 0.25f, 30,
            juce::Colour(152, 255, 152), getWidth() * 0.75f, 30,
            false);
        titleGradient.addColour(0.5, juce::Colour(50, 255, 50));
        g.setGradientFill(titleGradient);
    }
    else
    {
        // Warm celadon gold — like ink brushwork on washi paper
        juce::ColourGradient titleGradient(
            juce::Colour(90, 185, 100), getWidth() * 0.25f, 30,
            juce::Colour(140, 210, 120), getWidth() * 0.75f, 30,
            false);
        titleGradient.addColour(0.5, juce::Colour(115, 200, 115));
        g.setGradientFill(titleGradient);
    }
    g.drawText("PHOREST", 0, 10, getWidth(), 40, juce::Justification::centred);
}

void PhorestAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // Title area — glow button sits in the top-right of this strip
    auto titleArea = bounds.removeFromTop(55);
    glowButton.setBounds(titleArea.removeFromRight(62).reduced(8, 12));

    bounds.reduce(15, 10);

    // EQ Curve display at the top
    auto eqBounds = bounds.removeFromTop(200);
    eqCurveDisplay.setBounds(eqBounds);

    bounds.removeFromTop(15); // Spacing

    // Layout controls in 2 rows
    int knobWidth = 70;
    int knobHeight = 110;
    int spacing = 10;

    auto row1 = bounds.removeFromTop(knobHeight);
    bounds.removeFromTop(spacing);
    auto row2 = bounds.removeFromTop(knobHeight);

    // Calculate total width needed and center the rows
    int totalWidth1 = (knobWidth + spacing) * 6 - spacing;
    int totalWidth2 = (knobWidth + spacing) * 6 - spacing;  // 6 knobs in row 2 now

    int leftMargin1 = (getWidth() - totalWidth1) / 2;
    int leftMargin2 = (getWidth() - totalWidth2) / 2;

    row1.removeFromLeft(leftMargin1 - 15);
    row2.removeFromLeft(leftMargin2 - 15);

    // Check if manual mode is active
    bool isManualMode = audioProcessor.getValueTreeState().getRawParameterValue("lfoShape")->load() == 0;

    // Row 1: sweep, min, max, range, shape, position (when manual mode)
    sweepFreqSlider.setBounds(row1.removeFromLeft(knobWidth));
    row1.removeFromLeft(spacing);
    freqRangeSlider.setBounds(row1.removeFromLeft(knobWidth));
    row1.removeFromLeft(spacing);
    minDepthSlider.setBounds(row1.removeFromLeft(knobWidth));
    row1.removeFromLeft(spacing);
    maxDepthSlider.setBounds(row1.removeFromLeft(knobWidth));
    row1.removeFromLeft(spacing);
    lfoShapeSlider.setBounds(row1.removeFromLeft(knobWidth));
    row1.removeFromLeft(spacing);

    // Manual position slider - only visible and enabled when manual mode is active
    manualPosSlider.setBounds(row1.removeFromLeft(knobWidth));
    manualPosSlider.setVisible(isManualMode);
    manualPosSlider.setEnabled(isManualMode);
    manualPosSlider.setAlpha(isManualMode ? 1.0f : 0.3f);

    // Row 2: stages, feedback, detune, cancel, stereo, mix
    stagesSlider.setBounds(row2.removeFromLeft(knobWidth));
    row2.removeFromLeft(spacing);
    feedbackSlider.setBounds(row2.removeFromLeft(knobWidth));
    row2.removeFromLeft(spacing);
    detuneSlider.setBounds(row2.removeFromLeft(knobWidth));
    row2.removeFromLeft(spacing);
    dryCancelSlider.setBounds(row2.removeFromLeft(knobWidth));
    row2.removeFromLeft(spacing);
    stereoSlider.setBounds(row2.removeFromLeft(knobWidth));
    row2.removeFromLeft(spacing);
    dryWetSlider.setBounds(row2.removeFromLeft(knobWidth));
}