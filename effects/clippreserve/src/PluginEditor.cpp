#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
static const juce::Colour BG_TOP{ 0xFFE6ECF2 };
static const juce::Colour BG_BOT{ 0xFFBDC9D6 };
static const juce::Colour PANEL_COL{ 0xFFF0F4F8 };
static const juce::Colour PANEL_EDGE{ 0xFF8CA0B3 };
static const juce::Colour GLOW_COL{ 0xFF00D2FF };
static const juce::Colour GLOW2{ 0xFFE0FAFF };
static const juce::Colour KNOB_TRACK{ 0xFFD2DCE5 };
static const juce::Colour KNOB_FILL{ 0xFF00A8E8 };
static const juce::Colour TEXT_MAIN{ 0xFF1C2D3D };
static const juce::Colour TEXT_DIM{ 0xFF5C7287 };
static const juce::Colour LED_ON{ 0xFF00D2FF };
static const juce::Colour LED_OFF{ 0xFF4A6070 };

//==============================================================================
struct CPLookAndFeel : public juce::LookAndFeel_V4
{
    CPLookAndFeel()
    {
        setColour(juce::Slider::rotarySliderFillColourId, KNOB_FILL);
        setColour(juce::Slider::rotarySliderOutlineColourId, KNOB_TRACK);
        setColour(juce::Slider::thumbColourId, TEXT_MAIN);
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::black);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, KNOB_FILL.withAlpha(0.3f));
        setColour(juce::Label::textColourId, TEXT_MAIN);
        setColour(juce::ToggleButton::textColourId, TEXT_MAIN);
        setColour(juce::ToggleButton::tickColourId, LED_ON);
        setColour(juce::ToggleButton::tickDisabledColourId, LED_OFF);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
        float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider&) override
    {
        const float radius = (float)juce::jmin(width, height) * 0.38f - 4.f;
        const float cx = (float)x + (float)width * 0.5f;
        const float cy = (float)y + (float)height * 0.38f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        for (int i = 2; i >= 1; --i)
        {
            float r = radius + 2.f + (float)i * 2.f;
            g.setColour(GLOW_COL.withAlpha(0.06f * (float)(3 - i)));
            g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.5f);
        }

        juce::Path track;
        track.addCentredArc(cx, cy, radius, radius, 0.f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(KNOB_TRACK);
        g.strokePath(track, juce::PathStrokeType(5.f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        if (sliderPos > 0.f)
        {
            juce::Path fill;
            fill.addCentredArc(cx, cy, radius, radius, 0.f, rotaryStartAngle, angle, true);
            g.setColour(KNOB_FILL);
            g.strokePath(fill, juce::PathStrokeType(5.f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
            g.setColour(GLOW2.withAlpha(0.5f));
            g.strokePath(fill, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        const float bodyR = radius - 6.f;
        juce::ColourGradient bodyGrad(juce::Colours::white, cx, cy - bodyR,
            BG_BOT, cx, cy + bodyR, false);
        g.setGradientFill(bodyGrad);
        g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.f, bodyR * 2.f);
        g.setColour(PANEL_EDGE);
        g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2.f, bodyR * 2.f, 1.5f);

        const float pLen = bodyR * 0.6f;
        const float ptX = cx + std::sin(angle) * pLen;
        const float ptY = cy - std::cos(angle) * pLen;
        g.setColour(TEXT_MAIN);
        g.drawLine(cx, cy, ptX, ptY, 3.0f);
        g.setColour(GLOW_COL);
        g.fillEllipse(ptX - 3.f, ptY - 3.f, 6.f, 6.f);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if (!label.isBeingEdited())
        {
            g.setColour(juce::Colours::black);
            g.setFont(getLabelFont(label));
            g.drawFittedText(label.getText(),
                label.getLocalBounds(),
                label.getJustificationType(),
                1, 1.0f);
        }
        else
        {
            LookAndFeel_V4::drawLabel(g, label);
        }
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
        bool isHighlighted, bool /*isDown*/) override
    {
        const auto  bounds = btn.getLocalBounds().toFloat();
        const float ledR = 7.f;
        const float ledCX = bounds.getX() + ledR + 2.f;
        const float ledCY = bounds.getCentreY();
        const bool  on = btn.getToggleState();

        if (on)
        {
            g.setColour(LED_ON.withAlpha(0.25f));
            g.fillEllipse(ledCX - ledR - 3.f, ledCY - ledR - 3.f,
                (ledR + 3.f) * 2.f, (ledR + 3.f) * 2.f);
        }

        juce::ColourGradient lg(on ? LED_ON.brighter(0.4f) : LED_OFF.brighter(0.1f),
            ledCX - ledR * 0.3f, ledCY - ledR * 0.3f,
            on ? LED_ON.darker(0.3f) : LED_OFF.darker(0.3f),
            ledCX + ledR * 0.5f, ledCY + ledR * 0.5f, false);
        g.setGradientFill(lg);
        g.fillEllipse(ledCX - ledR, ledCY - ledR, ledR * 2.f, ledR * 2.f);
        g.setColour(on ? LED_ON : PANEL_EDGE);
        g.drawEllipse(ledCX - ledR, ledCY - ledR, ledR * 2.f, ledR * 2.f, 1.2f);

        g.setColour(isHighlighted ? TEXT_MAIN : TEXT_DIM);
        g.setFont(juce::Font(juce::FontOptions("Helvetica", 13.f, juce::Font::bold)));
        juce::Rectangle<float> ta(ledCX + ledR + 6.f, bounds.getY(),
            bounds.getWidth() - (ledCX + ledR + 8.f), bounds.getHeight());
        g.drawText(btn.getButtonText(), ta, juce::Justification::centredLeft, true);
    }
};

static CPLookAndFeel cpLAF;

//==============================================================================
// Layout — all in one place
namespace Layout
{
    static constexpr int W = 1040;
    static constexpr int H = 520;
    static constexpr int PANEL_X = 30;
    static constexpr int PANEL_Y = 110;
    // Toggles sit in the header bar, 3 side-by-side, right-aligned
    static constexpr int TOG_W = 155;
    static constexpr int TOG_H = 28;
    static constexpr int TOG_GAP = 10;
    static constexpr int TOG_Y = 38;   // y in header (centred between top and panel)
    // 7 knob columns across the full panel
    static constexpr int NUM_KNOBS = 7;
}

//==============================================================================
ClipPreserveAudioProcessorEditor::ClipPreserveAudioProcessorEditor(ClipPreserveAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    driveAtt(p.apvts, "drive", driveKnob.slider),
    threshAtt(p.apvts, "threshold", threshKnob.slider),
    preserveAtt(p.apvts, "preserve", preserveKnob.slider),
    hpFreqAtt(p.apvts, "hpfreq", hpFreqKnob.slider),
    lpFreqAtt(p.apvts, "lpfreq", lpFreqKnob.slider),
    scGainAtt(p.apvts, "scgain", scGainKnob.slider),
    outputAtt(p.apvts, "output", outputKnob.slider),
    hpOnAtt(p.apvts, "hpon", hpToggle.button),
    lpOnAtt(p.apvts, "lpon", lpToggle.button),
    oversampleAtt(p.apvts, "oversample", osToggle.button)
{
    setLookAndFeel(&cpLAF);

    for (auto* k : { &driveKnob, &threshKnob, &preserveKnob,
                     &hpFreqKnob, &lpFreqKnob, &scGainKnob, &outputKnob })
    {
        addAndMakeVisible(k);
        k->slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::black);
        for (auto* child : k->slider.getChildren())
            if (auto* lbl = dynamic_cast<juce::Label*> (child))
                lbl->setColour(juce::Label::textColourId, juce::Colours::black);
    }

    hpToggle.button.setButtonText("HP FILTER");
    lpToggle.button.setButtonText("LP FILTER");
    osToggle.button.setButtonText("2x OVERSAMPLE");

    for (auto* t : { &hpToggle, &lpToggle, &osToggle })
        addAndMakeVisible(t);

    setSize(Layout::W, Layout::H);
}

ClipPreserveAudioProcessorEditor::~ClipPreserveAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

//==============================================================================
void ClipPreserveAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace Layout;
    const float W = (float)getWidth();
    const float H = (float)getHeight();

    // Background
    juce::ColourGradient bgGrad(BG_TOP, 0.f, 0.f, BG_BOT, 0.f, H, false);
    g.setGradientFill(bgGrad);
    g.fillRect(getLocalBounds());

    const float px = (float)PANEL_X;
    const float py = (float)PANEL_Y;
    const float pw = W - px * 2.f;
    const float ph = H - py - 40.f;

    // Panel shadow + face
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.fillRoundedRectangle(px + 2.f, py + 4.f, pw, ph, 8.f);
    g.setColour(PANEL_COL);
    g.fillRoundedRectangle(px, py, pw, ph, 8.f);
    g.setColour(PANEL_EDGE.withAlpha(0.6f));
    g.drawRoundedRectangle(px, py, pw, ph, 8.f, 1.5f);

    // Title
    g.setFont(juce::Font(juce::FontOptions("Helvetica", 36.f, juce::Font::bold)));
    g.setColour(TEXT_MAIN);
    g.drawText("CLIPPRESERVE", 30, 20, 500, 45, juce::Justification::centredLeft);

    g.setFont(juce::Font(juce::FontOptions("Helvetica", 16.f, juce::Font::plain)));
    g.setColour(TEXT_DIM);
    g.drawText("Detail-Preserving Clipper", 32, 65, 400, 25, juce::Justification::centredLeft);

    juce::ColourGradient accent(GLOW_COL, 30.f, 95.f,
        juce::Colours::transparentBlack, 350.f, 95.f, false);
    g.setGradientFill(accent);
    g.fillRect(30.f, 95.f, 320.f, 3.f);

    // Section headers
    const float sw = pw / (float)NUM_KNOBS;

    g.setFont(juce::Font(juce::FontOptions("Helvetica", 18.f, juce::Font::bold)));
    g.setColour(TEXT_MAIN);
    g.drawText("CLIPPER", px, py + 15.f, sw * 2.f, 25.f, juce::Justification::centred);
    g.drawText("PRESERVE", px + sw * 2.f, py + 15.f, sw * 3.f, 25.f, juce::Justification::centred);
    g.drawText("SIDECHAIN", px + sw * 5.f, py + 15.f, sw, 25.f, juce::Justification::centred);
    g.drawText("GAIN", px + sw * 6.f, py + 15.f, sw, 25.f, juce::Justification::centred);

    // Dividers
    g.setColour(PANEL_EDGE.withAlpha(0.4f));
    g.fillRect(px + sw * 2.f, py + 15.f, 1.f, ph - 30.f);
    g.fillRect(px + sw * 5.f, py + 15.f, 1.f, ph - 30.f);
    g.fillRect(px + sw * 6.f, py + 15.f, 1.f, ph - 30.f);

    // Knob labels
    g.setFont(juce::Font(juce::FontOptions("Helvetica", 17.f, juce::Font::bold)));
    g.setColour(TEXT_DIM);
    const float ly = py + ph - 104.f;
    const float lh = 24.f;
    const auto  jc = juce::Justification::centred;

    g.drawText("DRIVE", px + sw * 0.f, ly, sw, lh, jc);
    g.drawText("THRESHOLD", px + sw * 1.f, ly, sw, lh, jc);
    g.drawText("PRESERVE", px + sw * 2.f, ly, sw, lh, jc);
    g.drawText("HP FREQ", px + sw * 3.f, ly, sw, lh, jc);
    g.drawText("LP FREQ", px + sw * 4.f, ly, sw, lh, jc);
    g.drawText("SC GAIN", px + sw * 5.f, ly, sw, lh, jc);
    g.drawText("OUTPUT", px + sw * 6.f, ly, sw, lh, jc);
}

void ClipPreserveAudioProcessorEditor::resized()
{
    using namespace Layout;

    const int px = PANEL_X;
    const int py = PANEL_Y;
    const int pw = getWidth() - px * 2;
    const int ph = getHeight() - py - 40;

    // Three toggles, side-by-side in the header, right-aligned to panel edge
    const int togRowW = TOG_W * 3 + TOG_GAP * 2;
    const int togX = px + pw - togRowW;   // flush with panel right
    const int togY = TOG_Y;

    hpToggle.setBounds(togX, togY, TOG_W, TOG_H);
    lpToggle.setBounds(togX + TOG_W + TOG_GAP, togY, TOG_W, TOG_H);
    osToggle.setBounds(togX + (TOG_W + TOG_GAP) * 2, togY, TOG_W, TOG_H);

    // 7 knobs across full panel width
    const int sw = pw / NUM_KNOBS;
    const int kY = py + 45;
    const int kH = ph - 65;
    const int pad = 10;

    driveKnob.setBounds(px + sw * 0 + pad, kY, sw - pad * 2, kH);
    threshKnob.setBounds(px + sw * 1 + pad, kY, sw - pad * 2, kH);
    preserveKnob.setBounds(px + sw * 2 + pad, kY, sw - pad * 2, kH);
    hpFreqKnob.setBounds(px + sw * 3 + pad, kY, sw - pad * 2, kH);
    lpFreqKnob.setBounds(px + sw * 4 + pad, kY, sw - pad * 2, kH);
    scGainKnob.setBounds(px + sw * 5 + pad, kY, sw - pad * 2, kH);
    outputKnob.setBounds(px + sw * 6 + pad, kY, sw - pad * 2, kH);
}