#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Colour palette ────────────────────────────────────────────────────────────
static const juce::Colour kBg{ 0xff1e1e2e };
static const juce::Colour kRow{ 0xff28283d };
static const juce::Colour kLabel{ 0xffaaaabc };
static const juce::Colour kFg{ 0xffcdd6f4 };
static const juce::Colour kAccent{ 0xff89b4fa };
static const juce::Colour kBoxBg{ 0xff313244 };

// ── Constructor ───────────────────────────────────────────────────────────────
AudioRerouterPluginAudioProcessorEditor::AudioRerouterPluginAudioProcessorEditor(
    AudioRerouterPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&laf);

    // Plugin window: 5 rows (logo / mode / channel / limiter+thresh / dry+wet)
    // Row heights: 36 (logo) + 4 rows × 34 + 4 gaps × 10 = 36+136+40 = 212
    // Adding top/bottom margins: 212 + 20 + 16 = 248. Round up to 252.
    setSize(360, 270);

    // ── Helpers ───────────────────────────────────────────────────────────────
    auto styleLabel = [&](juce::Label& l, const juce::String& text,
        juce::Justification just = juce::Justification::centredRight)
        {
            l.setText(text, juce::dontSendNotification);
            l.setJustificationType(just);
            l.setColour(juce::Label::textColourId, kLabel);
            addAndMakeVisible(l);
        };

    auto styleToggle = [&](juce::ToggleButton& b, const juce::String& text)
        {
            b.setButtonText(text);
            b.setColour(juce::ToggleButton::textColourId, kFg);
            // tick colours are handled by our custom LookAndFeel
            addAndMakeVisible(b);
        };

    auto styleHSlider = [&](juce::Slider& s)
        {
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 38, 22);
            s.setColour(juce::Slider::textBoxTextColourId, kFg);
            s.setColour(juce::Slider::textBoxBackgroundColourId, kBoxBg);
            s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setColour(juce::Slider::thumbColourId, kAccent);
            s.setColour(juce::Slider::backgroundColourId, kBoxBg);
            s.setColour(juce::Slider::trackColourId, kAccent);
            addAndMakeVisible(s);
        };

    auto styleKnob = [&](juce::Slider& s, const juce::String& tooltip)
        {
            s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
            s.setColour(juce::Slider::textBoxTextColourId, kLabel);
            s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setTooltip(tooltip);
            addAndMakeVisible(s);
        };

    // ── Mode row: Send / Receive ──────────────────────────────────────────────
    styleLabel(modeLabel, "Mode:");
    styleToggle(sendToggle, "Send");
    styleToggle(receiveToggle, "Receive");

    // Initialise visual state from parameter
    const bool modeIsReceive = *audioProcessor.apvts.getRawParameterValue("mode") > 0.5f;
    sendToggle.setToggleState(!modeIsReceive, juce::dontSendNotification);
    receiveToggle.setToggleState(modeIsReceive, juce::dontSendNotification);

    sendToggle.addListener(this);
    receiveToggle.addListener(this);

    // ── Channel row ───────────────────────────────────────────────────────────
    styleLabel(channelLabel, "Channel:");
    styleHSlider(channelSlider);
    channelSlider.setRange(1.0, 8.0, 1.0);
    channelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (audioProcessor.apvts, "channel", channelSlider);

    // ── Limiter + Threshold row ───────────────────────────────────────────────
    styleLabel(limiterLabel, "Limiter:");
    styleToggle(limiterToggle, "On");
    limiterToggle.addListener(this);

    const bool limOn = *audioProcessor.apvts.getRawParameterValue("limiter") > 0.5f;
    limiterToggle.setToggleState(limOn, juce::dontSendNotification);

    styleLabel(thresholdLabel, "Threshold:", juce::Justification::centredLeft);
    styleHSlider(thresholdSlider);
    thresholdSlider.setRange(0.0, 1.0, 0.01);
    thresholdSlider.setNumDecimalPlacesToDisplay(2);
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (audioProcessor.apvts, "threshold", thresholdSlider);

    // ── Dry / Wet row: rotary knobs ───────────────────────────────────────────
    styleLabel(dryLabel, "Dry", juce::Justification::centred);
    styleLabel(wetLabel, "Wet", juce::Justification::centred);
    styleKnob(drySlider, "Dry mix (receive mode only)");
    styleKnob(wetSlider, "Wet level (receive mode only)");
    drySlider.setRange(0.0, 1.0, 0.01);
    drySlider.setNumDecimalPlacesToDisplay(2);
    wetSlider.setRange(0.0, 2.0, 0.01);
    wetSlider.setNumDecimalPlacesToDisplay(2);
    dryAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (audioProcessor.apvts, "dry", drySlider);
    wetAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (audioProcessor.apvts, "wet", wetSlider);
}

AudioRerouterPluginAudioProcessorEditor::~AudioRerouterPluginAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// ── paint ─────────────────────────────────────────────────────────────────────
void AudioRerouterPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    const int W = getWidth();

    // ── Logo bar ──────────────────────────────────────────────────────────────
    // Draw a wide badge containing the full plugin name
    const juce::Rectangle<float> logoBadge{ 10.0f, 8.0f, 150.0f, 22.0f };

    // Badge background
    g.setColour(kAccent);
    g.fillRoundedRectangle(logoBadge, 4.0f);

    // Title inside badge
    g.setColour(juce::Colour(0xff1e1e2e));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.5f).withStyle("Bold")));
    g.drawText("Audio Rerouter", logoBadge.toNearestInt(), juce::Justification::centred, false);

    // Subtle separator line below header
    g.setColour(kAccent.withAlpha(0.25f));
    g.drawLine(10.0f, 34.0f, (float)W - 10.0f, 34.0f, 1.0f);

    // ── Alternating row tints ─────────────────────────────────────────────────
    // Rows start at y=40.  Four content rows, each 34 px tall + 10 px gap.
    const int rowH = 34;
    const int rowGap = 10;
    const int startY = 40;

    for (int i = 0; i < 4; ++i)
    {
        if (i % 2 == 0)
        {
            g.setColour(kRow);
            g.fillRoundedRectangle(6.0f,
                (float)(startY + i * (rowH + rowGap)) - 3.0f,
                (float)(W - 12),
                (float)(rowH + 6),
                4.0f);
        }
    }
}

// ── resized ───────────────────────────────────────────────────────────────────
void AudioRerouterPluginAudioProcessorEditor::resized()
{
    const int marginX = 10;
    const int labelW = 72;
    const int gutter = 6;
    const int rowH = 34;
    const int rowGap = 10;
    const int startY = 40;          // below logo/header strip
    const int W = getWidth();  // 360

    // Current y-cursor
    int y = startY;

    // ── Row 0: Mode ───────────────────────────────────────────────────────────
    // [ Mode: ]  [x] Send   [x] Receive
    {
        modeLabel.setBounds(marginX, y, labelW, rowH);

        const int toggleW = 90;
        int tx = marginX + labelW + gutter;
        sendToggle.setBounds(tx, y, toggleW, rowH);
        receiveToggle.setBounds(tx + toggleW, y, toggleW, rowH);
        y += rowH + rowGap;
    }

    // ── Row 1: Channel ────────────────────────────────────────────────────────
    {
        channelLabel.setBounds(marginX, y, labelW, rowH);
        channelSlider.setBounds(marginX + labelW + gutter, y,
            W - marginX - labelW - gutter - marginX, rowH);
        y += rowH + rowGap;
    }

    // ── Row 2: Limiter  On/Off  │  Threshold: <val> <slider> ─────────────────
    // Layout: [Limiter:][box On][  Threshold:][val][────slider────]
    {
        const int limLabelW = labelW;          // "Limiter:"
        const int checkW = 56;              // toggle with "On" text
        const int thLabelW = 74;              // "Threshold:"
        const int thSliderW = W - marginX - limLabelW - checkW - thLabelW - marginX - 4;

        int x = marginX;
        limiterLabel.setBounds(x, y, limLabelW, rowH);  x += limLabelW;
        limiterToggle.setBounds(x, y, checkW, rowH);  x += checkW + 4;
        thresholdLabel.setBounds(x, y, thLabelW, rowH);  x += thLabelW;
        thresholdSlider.setBounds(x, y, thSliderW, rowH);
        y += rowH + rowGap;
    }

    // ── Row 3: Dry / Wet knobs ────────────────────────────────────────────────
    // Two round knobs side by side, centred in the row.
    {
        const int knobSize = rowH * 2 + rowGap - 4; // ~74 px — tall enough to look nice
        const int knobAreaH = knobSize + 18;           // knob + label below

        // Vertically centre the knob area within a slightly taller strip
        const int rowTop = y;
        const int centreX = W / 2;
        const int spacing = 60;  // distance between knob centres

        // Dry
        const int dryKnobX = centreX - spacing - knobSize / 2;
        drySlider.setBounds(dryKnobX, rowTop, knobSize, knobSize);
        dryLabel.setBounds(dryKnobX, rowTop + knobSize - 2, knobSize, 16);

        // Wet
        const int wetKnobX = centreX + spacing - knobSize / 2;
        wetSlider.setBounds(wetKnobX, rowTop, knobSize, knobSize);
        wetLabel.setBounds(wetKnobX, rowTop + knobSize - 2, knobSize, 16);

        // Grow window height to accommodate if needed (already sized for this in ctor)
        (void)knobAreaH;
    }
}

// ── buttonClicked ─────────────────────────────────────────────────────────────
void AudioRerouterPluginAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    // ── Mode: Send / Receive exclusive toggles ────────────────────────────────
    if (button == &sendToggle)
    {
        // Always force Send ON, Receive OFF
        sendToggle.setToggleState(true, juce::dontSendNotification);
        receiveToggle.setToggleState(false, juce::dontSendNotification);
        audioProcessor.apvts.getParameter("mode")->setValueNotifyingHost(0.0f);
        return;
    }

    if (button == &receiveToggle)
    {
        sendToggle.setToggleState(false, juce::dontSendNotification);
        receiveToggle.setToggleState(true, juce::dontSendNotification);
        audioProcessor.apvts.getParameter("mode")->setValueNotifyingHost(1.0f);
        return;
    }

    // ── Limiter toggle with warning on disable ────────────────────────────────
    if (button == &limiterToggle)
    {
        const bool paramIsOn =
            *audioProcessor.apvts.getRawParameterValue("limiter") > 0.5f;

        if (paramIsOn)
        {
            // Snap back to ON visually while the async dialog is open
            limiterToggle.setToggleState(true, juce::dontSendNotification);

            juce::Component::SafePointer<AudioRerouterPluginAudioProcessorEditor> safeThis(this);

            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Disable Limiter?")
                .withMessage("Are you sure you want to disable the limiter? "
                    "This could lead to infinite feedback loops.")
                .withButton("Yes, Disable")
                .withButton("No, Keep On")
                .withAssociatedComponent(this),
                [safeThis](int result)
                {
                    if (!safeThis) return;
                    if (result == 1) // "Yes, Disable"
                    {
                        safeThis->audioProcessor.apvts.getParameter("limiter")
                            ->setValueNotifyingHost(0.0f);
                        safeThis->limiterToggle.setToggleState(false,
                            juce::dontSendNotification);
                    }
                    // result == 0: keep ON — toggle is already visually ON
                });
        }
        else
        {
            // Turning ON — no warning needed
            audioProcessor.apvts.getParameter("limiter")->setValueNotifyingHost(1.0f);
            limiterToggle.setToggleState(true, juce::dontSendNotification);
        }
    }
}

void AudioRerouterPluginAudioProcessorEditor::sliderValueChanged(juce::Slider*)
{
    // All sliders use APVTS attachments; nothing to do manually here.
}