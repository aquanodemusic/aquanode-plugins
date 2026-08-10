/*
  ==============================================================================
    PluginEditor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Palette — warm "Claude" clay/brown.
namespace col
{
    const juce::Colour bg{ 0xff2a211c }; // dark warm brown background
    const juce::Colour panel{ 0xff3a2c24 };
    const juce::Colour clay{ 0xffcc785c }; // Claude clay accent
    const juce::Colour skin{ 0xffe8d5b0 }; // drum head
    const juce::Colour skinRim{ 0xff8a6f4a };
    const juce::Colour syahi{ 0xff211a15 }; // central black paste
    const juce::Colour text{ 0xffe9ddca };
    const juce::Colour knobBody{ 0xff4a382c };
}

//==============================================================================
juce::Rectangle<float> TablaPad::bayanCircle() const
{
    auto r = getLocalBounds().toFloat().reduced(14.0f);
    const float d = juce::jmin(r.getWidth() * 0.46f, r.getHeight() * 0.9f);
    return { r.getX() + r.getWidth() * 0.02f,
             r.getCentreY() - d * 0.5f, d, d };            // bayan = bigger, left
}

juce::Rectangle<float> TablaPad::dayanCircle() const
{
    auto r = getLocalBounds().toFloat().reduced(14.0f);
    const float d = juce::jmin(r.getWidth() * 0.36f, r.getHeight() * 0.72f);
    return { r.getRight() - d - r.getWidth() * 0.02f,
             r.getCentreY() - d * 0.5f, d, d };            // dayan = smaller, right
}

static void drawDrum(juce::Graphics& g, juce::Rectangle<float> c,
    float syahiFrac, const juce::String& name, float flash)
{
    const auto centre = c.getCentre();

    // wooden shell ring
    g.setColour(col::skinRim);
    g.fillEllipse(c.expanded(7.0f));

    // head
    juce::ColourGradient grad(col::skin.brighter(0.1f), centre.x, centre.y,
        col::skin.darker(0.25f), c.getX(), c.getBottom(), true);
    g.setGradientFill(grad);
    g.fillEllipse(c);

    // strike flash (energy ripple)
    if (flash > 0.01f)
    {
        g.setColour(col::clay.withAlpha(flash * 0.5f));
        const float e = c.getWidth() * (0.2f + 0.4f * (1.0f - flash));
        g.drawEllipse(c.reduced(c.getWidth() * 0.5f - e), 3.0f);
    }

    // syahi (central loaded paste) — what makes the tabla pitched
    const float sd = c.getWidth() * syahiFrac;
    juce::Rectangle<float> sy(0, 0, sd, sd);
    sy.setCentre(centre);
    juce::ColourGradient sg(col::syahi.brighter(0.15f), centre.x, centre.y,
        col::syahi, sy.getX(), sy.getBottom(), true);
    g.setGradientFill(sg);
    g.fillEllipse(sy);

    g.setColour(col::skinRim.withAlpha(0.6f));
    g.drawEllipse(c, 1.5f);

    g.setColour(col::text.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    g.drawText(name, c.withY(c.getBottom() + 2.0f).withHeight(18.0f),
        juce::Justification::centred);
}

void TablaPad::paint(juce::Graphics& g)
{
    drawDrum(g, bayanCircle(), 0.5f, "BAYAN (bass)", proc.bayanFlash.load());
    drawDrum(g, dayanCircle(), 0.42f, "DAYAN (treble)", proc.dayanFlash.load());
}

void TablaPad::hit(const juce::MouseEvent& e)
{
    const auto p = e.position;
    const float vel = juce::jlimit(0.15f, 1.0f,
        1.0f - (p.y / (float)juce::jmax(1, getHeight())));

    auto tryDrum = [&](juce::Rectangle<float> c, int drumIndex) -> bool
        {
            const auto ctr = c.getCentre();
            const float radius = ctr.getDistanceFrom(p) / (c.getWidth() * 0.5f);
            if (radius <= 1.02f)
            {
                proc.triggerUIStrike(drumIndex, juce::jlimit(0.0f, 1.0f, radius), vel);
                dragDrum = drumIndex;
                return true;
            }
            return false;
        };

    dragDrum = -1;
    if (!tryDrum(dayanCircle(), 1))
        tryDrum(bayanCircle(), 0);
}

void TablaPad::mouseDown(const juce::MouseEvent& e)
{
    hit(e);
    dragging = (dragDrum >= 0);
    dragStart = e.position;
}

void TablaPad::mouseDrag(const juce::MouseEvent& e)
{
    if (!slideMode)
    {
        // Original behaviour: dragging just keeps striking wherever the
        // mouse currently is.
        hit(e);
        return;
    }

    if (!dragging || dragDrum < 0)
        return;

    // Sliding the palm heel across the head raises pitch continuously —
    // map drag distance from the initial contact point to an upward bend.
    const float dist = e.position.getDistanceFrom(dragStart);
    const float semis = juce::jlimit(0.0f, 6.0f, dist * 0.06f);
    proc.setSlideBend(dragDrum, semis);
}

void TablaPad::mouseUp(const juce::MouseEvent&)
{
    if (dragging && dragDrum >= 0)
        proc.setSlideBend(dragDrum, 0.0f);

    dragging = false;
    dragDrum = -1;
}

//==============================================================================
void TablaAudioProcessorEditor::addKnob(const juce::String& paramID,
    const juce::String& textLabel)
{
    auto* k = new KnobBlock();
    k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    k->slider.setColour(juce::Slider::rotarySliderFillColourId, col::clay);
    k->slider.setColour(juce::Slider::rotarySliderOutlineColourId, col::knobBody);
    k->slider.setColour(juce::Slider::thumbColourId, col::skin);
    addAndMakeVisible(k->slider);

    k->label.setText(textLabel, juce::dontSendNotification);
    k->label.setJustificationType(juce::Justification::centredLeft);
    k->label.setColour(juce::Label::textColourId, col::text.withAlpha(0.9f));
    k->label.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    addAndMakeVisible(k->label);

    k->valueLabel.setJustificationType(juce::Justification::centredLeft);
    k->valueLabel.setColour(juce::Label::textColourId, col::clay);
    k->valueLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    addAndMakeVisible(k->valueLabel);

    auto updateValueLabel = [k] { k->valueLabel.setText(juce::String(k->slider.getValue(), 3),
        juce::dontSendNotification); };
    k->slider.onValueChange = updateValueLabel;

    k->attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, k->slider);
    updateValueLabel();

    knobs.add(k);
}

TablaAudioProcessorEditor::TablaAudioProcessorEditor(TablaAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), pad(p)
{
    title.setText("T A B L A", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, col::clay);
    title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title);

    slideModeToggle.setColour(juce::ToggleButton::textColourId, col::text);
    slideModeToggle.setColour(juce::ToggleButton::tickColourId, col::clay);
    slideModeToggle.onClick = [this] { pad.setSlideMode(slideModeToggle.getToggleState()); };
    addAndMakeVisible(slideModeToggle);

    using namespace tabla::pid;
    // Row order matches the layout grid below.
    addKnob(tuning, "Tuning");
    addKnob(tension, "Tension");
    addKnob(syahi, "Syahi");
    addKnob(size, "Size");
    addKnob(damping, "Damping");
    addKnob(brightness, "Bright");
    addKnob(strikePos, "Strike");
    addKnob(hardness, "Hardness");
    addKnob(pressure, "Pressure");
    addKnob(bodyRes, "Body");
    addKnob(airRes, "Air");
    addKnob(sympathetic, "Sympath");
    addKnob(bassCoupling, "Bass Cpl");
    addKnob(randomness, "Random");
    addKnob(articulation, "Artic");
    addKnob(sustain, "Sustain");
    addKnob(decay, "Decay");
    addKnob(velResponse, "Vel");
    addKnob(output, "Output");
    addKnob(width, "Width");

    addAndMakeVisible(pad);
    setSize(560, 620);
}

void TablaAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(col::bg);

    // knob panel backdrop
    auto top = getLocalBounds().removeFromTop(334).reduced(8).toFloat();
    g.setColour(col::panel);
    g.fillRoundedRectangle(top, 10.0f);
}

void TablaAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto titleRow = area.removeFromTop(34);
    slideModeToggle.setBounds(titleRow.removeFromRight(140).reduced(4));
    title.setBounds(titleRow.reduced(16, 4));

    // ---- knob grid: big dial on the left, name + value stacked to the right ----
    auto knobArea = area.removeFromTop(300).reduced(14, 6);
    const int perRow = 4;
    const int rows = (knobs.size() + perRow - 1) / perRow;
    const int kw = knobArea.getWidth() / perRow;
    const int kh = knobArea.getHeight() / juce::jmax(1, rows);

    for (int i = 0; i < knobs.size(); ++i)
    {
        const int r = i / perRow, c = i % perRow;
        juce::Rectangle<int> cell(knobArea.getX() + c * kw,
            knobArea.getY() + r * kh, kw, kh);
        cell.reduce(6, 4);

        const int knobSize = juce::jmin(cell.getHeight(), cell.getWidth() / 2);
        auto knobCell = cell.removeFromLeft(knobSize);
        knobs[i]->slider.setBounds(knobCell);

        auto textCell = cell.reduced(6, 0);
        knobs[i]->label.setBounds(textCell.removeFromTop(textCell.getHeight() / 2));
        knobs[i]->valueLabel.setBounds(textCell);
    }

    // ---- drums fill the rest ----
    pad.setBounds(area.reduced(10));
}