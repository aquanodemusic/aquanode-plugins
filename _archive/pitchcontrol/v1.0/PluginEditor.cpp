/*
  ==============================================================================
    PitchControl – IIR Bell Filter Plugin
    Editor Implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Colour palette
//==============================================================================
namespace Colors
{
    const juce::Colour background     { 0xff00cccc };
    const juce::Colour panelBg        { 0xff00cc67 };
    const juce::Colour accent         { 0x77eeeeee };
    const juce::Colour accentBright   { 0xff533483 };
    const juce::Colour activeKey      { 0xff00ffff };
    const juce::Colour knobFill       { 0xff00ffff };
    const juce::Colour whiteKey       { 0xfff0f0f0 };
    const juce::Colour blackKey       { 0xff1e1e2e };
    const juce::Colour whiteKeyActive { 0xff00ffff };
    const juce::Colour blackKeyActive { 0xff00dddd };
    const juce::Colour knobTrack      { 0xff000000 };
    const juce::Colour knobPointer    { 0xffffffff };
    const juce::Colour textBright     { 0xfff0f0f0 };
    const juce::Colour textDim        { 0xff888888 };
    const juce::Colour borderColor    { 0xff00ffff };
}

//==============================================================================
// PitchControlLookAndFeel
//==============================================================================
PitchControlLookAndFeel::PitchControlLookAndFeel()
{
    setColour (juce::Slider::rotarySliderFillColourId,    Colors::knobFill);
    setColour (juce::Slider::rotarySliderOutlineColourId, Colors::knobTrack);
    setColour (juce::Slider::thumbColourId,               Colors::knobPointer);
    setColour (juce::Label::textColourId,                 Colors::textBright);
    setColour (juce::ComboBox::backgroundColourId,        Colors::accent);
    setColour (juce::ComboBox::textColourId,              Colors::textBright);
    setColour (juce::ComboBox::outlineColourId,           Colors::borderColor);
    setColour (juce::ComboBox::arrowColourId,             Colors::textBright);
    setColour (juce::PopupMenu::backgroundColourId,       Colors::panelBg);
    setColour (juce::PopupMenu::textColourId,             Colors::textBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colors::accentBright);
    setColour (juce::PopupMenu::highlightedTextColourId,  Colors::textBright);
}

void PitchControlLookAndFeel::drawRotarySlider (
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const float radius = std::min (width, height) * 0.5f - 4.0f;
    const float cx     = x + width  * 0.5f;
    const float cy     = y + height * 0.5f;

    // Track arc
    {
        juce::Path p;
        p.addArc (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f,
                  startAngle, endAngle, true);
        g.setColour (Colors::knobTrack);
        g.strokePath (p, juce::PathStrokeType (3.0f));
    }

    // Value arc
    {
        juce::Path p;
        p.addArc (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f,
                  startAngle, startAngle + (endAngle - startAngle) * sliderPos, true);
        g.setColour (Colors::knobFill);
        g.strokePath (p, juce::PathStrokeType (3.0f));
    }

    // Knob body
    juce::ColourGradient grad (Colors::accent.brighter (0.3f), cx - radius * 0.3f, cy - radius * 0.3f,
                               Colors::accent.darker   (0.5f), cx + radius * 0.3f, cy + radius * 0.3f,
                               false);
    g.setGradientFill (grad);
    g.fillEllipse (cx - radius * 0.72f, cy - radius * 0.72f, radius * 1.44f, radius * 1.44f);

    g.setColour (Colors::borderColor.withAlpha (0.6f));
    g.drawEllipse (cx - radius * 0.72f, cy - radius * 0.72f, radius * 1.44f, radius * 1.44f, 1.5f);

    // Pointer
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Line<float> ptr (
        cx + std::sin (angle) * radius * 0.35f,
        cy - std::cos (angle) * radius * 0.35f,
        cx + std::sin (angle) * radius * 0.55f,
        cy - std::cos (angle) * radius * 0.55f);
    g.setColour (Colors::knobPointer);
    g.drawLine (ptr, 2.0f);
}

void PitchControlLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (!label.isBeingEdited())
    {
        g.setColour (label.findColour (juce::Label::textColourId));
        g.setFont   (label.getFont());
        g.drawFittedText (label.getText(), label.getLocalBounds(),
                          label.getJustificationType(), 1, 1.0f);
    }
}

//==============================================================================
// PianoKeyboard – out-of-line constexpr definitions
//==============================================================================
constexpr int   PianoKeyboard::kWhiteNotes[7];
constexpr int   PianoKeyboard::kBlackNotes[5];
constexpr float PianoKeyboard::kBlackKeyOffsets[5];

PianoKeyboard::PianoKeyboard (PitchControlAudioProcessor& p)
    : m_processor (p)
{
}

void PianoKeyboard::resized()
{
    m_whiteKeyWidth  = (float) getWidth()  / (float) kNumWhite;
    m_whiteKeyHeight = (float) getHeight();
    m_blackKeyWidth  = m_whiteKeyWidth  * 0.62f;
    m_blackKeyHeight = m_whiteKeyHeight * 0.62f;
}

float PianoKeyboard::blackKeyCentreX (int b) const noexcept
{
    return kBlackKeyOffsets[b] * m_whiteKeyWidth;
}

bool PianoKeyboard::hitTestBlack (int b, int x, int y) const noexcept
{
    float cx = blackKeyCentreX (b);
    return x >= (cx - m_blackKeyWidth * 0.5f)
        && x <  (cx + m_blackKeyWidth * 0.5f)
        && y <  m_blackKeyHeight;
}

int PianoKeyboard::getNoteAtPosition (int x, int y) const
{
    for (int b = 0; b < kNumBlack; ++b)
        if (hitTestBlack (b, x, y))
            return kBlackNotes[b];

    int w = (int)((float)x / m_whiteKeyWidth);
    if (w >= 0 && w < kNumWhite)
        return kWhiteNotes[w];

    return -1;
}

void PianoKeyboard::mouseDown (const juce::MouseEvent& e)
{
    int note = getNoteAtPosition (e.x, e.y);
    if (note < 0) return;

    auto* param = dynamic_cast<juce::AudioParameterBool*> (
        m_processor.apvts.getParameter (PitchControlAudioProcessor::noteActiveParamID (note)));
    if (param)
        param->setValueNotifyingHost (param->get() ? 0.0f : 1.0f);

    repaint();
}

void PianoKeyboard::paint (juce::Graphics& g)
{
    // Read active (protected) state for each note class
    bool active[kNumNotes] {};
    for (int i = 0; i < kNumNotes; ++i)
        active[i] = m_processor.apvts.getRawParameterValue (
            PitchControlAudioProcessor::noteActiveParamID (i))->load() > 0.5f;

    const float gap     = 1.5f;
    const float cornerR = 4.0f;

    // ---- White keys ----
    for (int w = 0; w < kNumWhite; ++w)
    {
        int  note     = kWhiteNotes[w];
        bool isActive = active[note];

        float kx = w * m_whiteKeyWidth + gap * 0.5f;
        float kw = m_whiteKeyWidth - gap;
        float kh = m_whiteKeyHeight - gap;

        juce::Colour fillCol = isActive ? Colors::whiteKeyActive : Colors::whiteKey;

        juce::ColourGradient grad (
            fillCol.brighter (0.05f), kx, 0.0f,
            fillCol.darker   (0.12f), kx, kh, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (kx, gap * 0.5f, kw, kh, cornerR);

        g.setColour (isActive ? Colors::activeKey : juce::Colours::grey.withAlpha (0.4f));
        g.drawRoundedRectangle (kx, gap * 0.5f, kw, kh, cornerR, 1.5f);

        if (isActive)
        {
            g.setColour (Colors::activeKey);
            // g.fillEllipse (kx + kw * 0.5f - 5.0f, kh - 18.0f, 10.0f, 10.0f);
        }

        // Note name
        g.setFont  (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (isActive ? Colors::background : Colors::textDim);
        g.drawText  (kNoteNames[note],
                     (int) kx, (int)(kh - 20), (int) kw, 16,
                     juce::Justification::centred, false);
    }

    // ---- Black keys ----
    for (int b = 0; b < kNumBlack; ++b)
    {
        int  note     = kBlackNotes[b];
        bool isActive = active[note];

        float cx = blackKeyCentreX (b);
        float bx = cx - m_blackKeyWidth * 0.5f;
        float bw = m_blackKeyWidth;
        float bh = m_blackKeyHeight;

        juce::Colour fillCol = isActive ? Colors::blackKeyActive : Colors::blackKey;

        juce::ColourGradient grad (
            fillCol.brighter (0.15f), bx, 0.0f,
            fillCol.darker   (0.3f),  bx, bh, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bx, 0.0f, bw, bh, 3.0f);

        g.setColour (isActive ? Colors::activeKey.brighter (0.3f)
                              : Colors::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (bx, 0.0f, bw, bh, 3.0f, 1.5f);

        if (isActive)
        {
            g.setColour (Colors::whiteKey);
            //g.fillEllipse (bx + bw * 0.5f - 4.0f, bh - 14.0f, 8.0f, 8.0f);
        }
    }
}

//==============================================================================
// LabelledKnob
//==============================================================================
LabelledKnob::LabelledKnob (const juce::String& labelText,
                              const juce::String& paramID,
                              PitchControlAudioProcessor& processor,
                              juce::LookAndFeel* laf)
{
    slider.setSliderStyle  (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour (juce::Slider::textBoxTextColourId,       Colors::textBright);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    if (laf) slider.setLookAndFeel (laf);

    label.setText              (labelText, juce::dontSendNotification);
    label.setFont              (juce::FontOptions (12.0f, juce::Font::bold));
    label.setColour            (juce::Label::textColourId, Colors::textBright);
    label.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (slider);
    addAndMakeVisible (label);

    m_attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, paramID, slider);
}

void LabelledKnob::resized()
{
    auto area = getLocalBounds();
    label.setBounds  (area.removeFromTop (18));
    slider.setBounds (area);
}

//==============================================================================
// NoteRangeSelector
//==============================================================================
NoteRangeSelector::NoteRangeSelector (const juce::String& labelText,
                                       const juce::String& paramID,
                                       PitchControlAudioProcessor& processor)
{
    m_label.setText              (labelText, juce::dontSendNotification);
    m_label.setFont              (juce::FontOptions (12.0f, juce::Font::bold));
    m_label.setColour            (juce::Label::textColourId, Colors::textBright);
    m_label.setJustificationType (juce::Justification::centred);

    for (int n = 0; n < kTotalNotes; ++n)
    {
        juce::String name = kNoteNames[n % kNumNotes] + juce::String (n / kNumNotes);
        m_combo.addItem (name, n + 1);   // APVTS ComboBoxAttachment uses 1-based IDs
    }
    m_combo.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (m_label);
    addAndMakeVisible (m_combo);

    m_attach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, paramID, m_combo);
}

void NoteRangeSelector::resized()
{
    auto area = getLocalBounds();
    m_label.setBounds (area.removeFromTop (18));
    m_combo.setBounds (area.reduced (2, 2));
}

//==============================================================================
// PitchControlAudioProcessorEditor
//==============================================================================
PitchControlAudioProcessorEditor::PitchControlAudioProcessorEditor (
    PitchControlAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor       (p),
      m_keyboard           (p),
      m_depthKnob  ("Depth (dB)", PitchControlAudioProcessor::depthParamID(),     p, &m_laf),
      m_qKnob      ("Q",          PitchControlAudioProcessor::qParamID(),         p, &m_laf),
      m_rangeFrom  ("Range From", PitchControlAudioProcessor::rangeFromParamID(), p),
      m_rangeTo    ("Range To",   PitchControlAudioProcessor::rangeToParamID(),   p)
{
    setLookAndFeel (&m_laf);

    m_titleLabel.setText              ("PitchControl by aquanode (If Q is high, lower dB!)", juce::dontSendNotification);
    m_titleLabel.setFont              (juce::FontOptions (22.0f, juce::Font::bold));
    m_titleLabel.setColour            (juce::Label::textColourId, juce::Colours::white);
    m_titleLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (m_titleLabel);
    addAndMakeVisible (m_keyboard);
    addAndMakeVisible (m_depthKnob);
    addAndMakeVisible (m_qKnob);
    addAndMakeVisible (m_rangeFrom);
    addAndMakeVisible (m_rangeTo);

    // Set up the Wet Only button
    addAndMakeVisible(m_wetOnlyButton);
    m_wetOnlyButton.setClickingTogglesState(true);

    // Style the button to match your "Colors" namespace
    m_wetOnlyButton.setColour(juce::TextButton::buttonOnColourId, Colors::activeKey);
    m_wetOnlyButton.setColour(juce::TextButton::textColourOnId, juce::Colours::black);

    m_wetOnlyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, PitchControlAudioProcessor::wetOnlyParamID(), m_wetOnlyButton);

    setSize (620, 400);
    setResizable (false, false);
}

PitchControlAudioProcessorEditor::~PitchControlAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void PitchControlAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    // Background
    juce::ColourGradient bg (Colors::background,              0,   0,
                             Colors::panelBg.darker (0.3f),   0, (float)H, false);
    g.setGradientFill (bg);
    g.fillAll();

    // ===== Floating blurred circles =====
    {
        juce::DropShadow shadow1(juce::Colour(0xff00ffff).withAlpha(0.35f), 80, { 0, 0 });
        juce::DropShadow shadow2(juce::Colour(0xff00ffff).withAlpha(0.25f), 120, { 0, 0 });

        juce::Path circle1;
        circle1.addEllipse(100.0f, 80.0f, 180.0f, 180.0f);

        juce::Path circle2;
        circle2.addEllipse(380.0f, 180.0f, 220.0f, 220.0f);

        shadow1.drawForPath(g, circle1);
        shadow2.drawForPath(g, circle2);
    }


    // Keyboard panel shadow
    juce::Rectangle<float> kbPanel (10, 54, W - 20, 185);
    g.setColour (Colors::accent.withAlpha (0.4f));
    g.fillRoundedRectangle (kbPanel.expanded (4), 10.0f);
    g.setColour (Colors::borderColor.withAlpha (0.5f));
    g.drawRoundedRectangle (kbPanel.expanded (4), 10.0f, 1.5f);

    // Controls panel
    juce::Rectangle<float> ctrlPanel (10, 252, W - 20, 138);
    g.setColour (Colors::panelBg.withAlpha (0.7f));
    g.fillRoundedRectangle (ctrlPanel, 8.0f);
    g.setColour (Colors::borderColor.withAlpha (0.35f));
    g.drawRoundedRectangle (ctrlPanel, 8.0f, 1.0f);
}

void PitchControlAudioProcessorEditor::resized()
{
    const int margin = 14;
    const int W      = getWidth() - 2 * margin;

    m_titleLabel.setBounds (margin, 8, W, 32);
    m_keyboard  .setBounds (margin, 56, W, 172);

    // Controls row
    const int ctrlY  = 258;
    const int ctrlH  = 128;
    const int knobW  = 110;
    const int comboW = 130;
    const int comboH = 58;
    const int gap    = 12;

    const int totalW = knobW * 2 + comboW * 2 + gap * 3;
    const int startX = margin + (W - totalW) / 2;

    m_depthKnob.setBounds (startX,                     ctrlY, knobW,  ctrlH);
    m_qKnob    .setBounds (startX + knobW + gap,       ctrlY, knobW,  ctrlH);

    const int comboX = startX + 2 * (knobW + gap);
    const int comboY = ctrlY + (ctrlH - comboH) / 2;
    m_rangeFrom.setBounds (comboX,             comboY, comboW, comboH);
    m_rangeTo  .setBounds (comboX + comboW + gap, comboY, comboW, comboH);

    m_wetOnlyButton.setBounds(margin + (knobW * 2) + 290, ctrlY + 0, 80, 30);
}
