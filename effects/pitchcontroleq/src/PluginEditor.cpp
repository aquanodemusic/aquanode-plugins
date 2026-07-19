/*
  ==============================================================================
    PitchControlEQ – IIR Bell Filter Plugin
    Editor Implementation
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//==============================================================================
namespace Colors
{
    const juce::Colour background  { 0xff00cccc };
    const juce::Colour panelBg     { 0xff00cc67 };
    const juce::Colour accent      { 0x77eeeeee };
    const juce::Colour accentBright{ 0xff533483 };
    const juce::Colour knobFill    { 0xff00ffff };
    const juce::Colour whiteKey    { 0xfff0f0f0 };
    const juce::Colour blackKey    { 0xff1e1e2e };
    const juce::Colour whiteKeyActive{ 0xff00ffff };
    const juce::Colour blackKeyActive{ 0xff00dddd };
    const juce::Colour knobTrack   { 0xff000000 };
    const juce::Colour knobPointer { 0xffffffff };
    const juce::Colour textBright  { 0xfff0f0f0 };
    const juce::Colour textDim     { 0xff888888 };
    const juce::Colour borderColor { 0xff00ffff };
}

//==============================================================================
// PitchControlEQLookAndFeel
//==============================================================================
PitchControlEQLookAndFeel::PitchControlEQLookAndFeel()
{
    setColour(juce::Slider::rotarySliderFillColourId,    Colors::knobFill);
    setColour(juce::Slider::rotarySliderOutlineColourId, Colors::knobTrack);
    setColour(juce::Slider::thumbColourId,               Colors::knobPointer);
    setColour(juce::Slider::trackColourId,               Colors::knobFill);
    setColour(juce::Label::textColourId,                 Colors::textBright);
    setColour(juce::PopupMenu::backgroundColourId,           Colors::panelBg);
    setColour(juce::PopupMenu::textColourId,                 Colors::textBright);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Colors::accentBright);
    setColour(juce::PopupMenu::highlightedTextColourId,      Colors::textBright);
}

void PitchControlEQLookAndFeel::drawRotarySlider(
    juce::Graphics& g, int x, int y, int width, int height,
    float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const float r  = std::min(width, height) * 0.5f - 4.0f;
    const float cx = x + width  * 0.5f;
    const float cy = y + height * 0.5f;

    { juce::Path p; p.addArc(cx-r, cy-r, r*2, r*2, startAngle, endAngle, true);
      g.setColour(Colors::knobTrack); g.strokePath(p, juce::PathStrokeType(3.0f)); }

    { juce::Path p; p.addArc(cx-r, cy-r, r*2, r*2, startAngle,
          startAngle + (endAngle - startAngle) * sliderPos, true);
      g.setColour(Colors::knobFill); g.strokePath(p, juce::PathStrokeType(3.0f)); }

    juce::ColourGradient grad(Colors::accent.brighter(0.3f), cx - r*0.3f, cy - r*0.3f,
                               Colors::accent.darker(0.5f),  cx + r*0.3f, cy + r*0.3f, false);
    g.setGradientFill(grad);
    g.fillEllipse(cx - r*0.72f, cy - r*0.72f, r*1.44f, r*1.44f);
    g.setColour(Colors::borderColor.withAlpha(0.6f));
    g.drawEllipse(cx - r*0.72f, cy - r*0.72f, r*1.44f, r*1.44f, 1.5f);

    float angle = startAngle + sliderPos * (endAngle - startAngle);
    g.setColour(Colors::knobPointer);
    g.drawLine(cx + std::sin(angle)*r*0.35f, cy - std::cos(angle)*r*0.35f,
               cx + std::sin(angle)*r*0.55f, cy - std::cos(angle)*r*0.55f, 2.0f);
}

void PitchControlEQLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (!label.isBeingEdited())
    {
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawFittedText(label.getText(), label.getLocalBounds(),
                         label.getJustificationType(), 1, 1.0f);
    }
}

//==============================================================================
// PianoKeyboard
//==============================================================================
constexpr int   PianoKeyboard::kWhiteNotes[7];
constexpr int   PianoKeyboard::kBlackNotes[5];
constexpr float PianoKeyboard::kBlackKeyOffsets[5];

PianoKeyboard::PianoKeyboard(PitchControlEQAudioProcessor& p) : m_processor(p) {}

void PianoKeyboard::resized()
{
    m_whiteKeyWidth  = (float)getWidth()  / (float)kNumWhite;
    m_whiteKeyHeight = (float)getHeight();
    m_blackKeyWidth  = m_whiteKeyWidth  * 0.62f;
    m_blackKeyHeight = m_whiteKeyHeight * 0.62f;
}

float PianoKeyboard::blackKeyCentreX(int b) const noexcept
{
    return kBlackKeyOffsets[b] * m_whiteKeyWidth;
}

bool PianoKeyboard::hitTestBlack(int b, int x, int y) const noexcept
{
    float cx = blackKeyCentreX(b);
    return x >= (cx - m_blackKeyWidth*0.5f) && x < (cx + m_blackKeyWidth*0.5f)
           && y < m_blackKeyHeight;
}

int PianoKeyboard::getNoteAtPosition(int x, int y) const
{
    for (int b = 0; b < kNumBlack; ++b)
        if (hitTestBlack(b, x, y)) return kBlackNotes[b];
    int w = (int)((float)x / m_whiteKeyWidth);
    if (w >= 0 && w < kNumWhite) return kWhiteNotes[w];
    return -1;
}

void PianoKeyboard::mouseDown(const juce::MouseEvent& e)
{
    int note = getNoteAtPosition(e.x, e.y);
    if (note < 0) return;
    auto* param = dynamic_cast<juce::AudioParameterBool*>(
        m_processor.apvts.getParameter(PitchControlEQAudioProcessor::noteActiveParamID(note)));
    if (param) param->setValueNotifyingHost(param->get() ? 0.0f : 1.0f);
    repaint();
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    bool active[kNumNotes]{};
    for (int i = 0; i < kNumNotes; ++i)
        active[i] = m_processor.apvts.getRawParameterValue(
            PitchControlEQAudioProcessor::noteActiveParamID(i))->load() > 0.5f;

    const float gap = 1.5f, cornerR = 4.0f;

    for (int w = 0; w < kNumWhite; ++w)
    {
        int note = kWhiteNotes[w]; bool on = active[note];
        float kx = w * m_whiteKeyWidth + gap*0.5f;
        float kw = m_whiteKeyWidth - gap;
        float kh = m_whiteKeyHeight - gap;
        juce::ColourGradient grad((on ? Colors::whiteKeyActive : Colors::whiteKey).brighter(0.05f), kx, 0,
                                  (on ? Colors::whiteKeyActive : Colors::whiteKey).darker(0.12f),  kx, kh, false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(kx, gap*0.5f, kw, kh, cornerR);
        g.setColour(on ? Colors::whiteKeyActive : juce::Colours::grey.withAlpha(0.4f));
        g.drawRoundedRectangle(kx, gap*0.5f, kw, kh, cornerR, 1.5f);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setColour(on ? Colors::background : Colors::textDim);
        g.drawText(kNoteNames[note], (int)kx, (int)(kh - 20), (int)kw, 16,
                   juce::Justification::centred, false);
    }

    for (int b = 0; b < kNumBlack; ++b)
    {
        int note = kBlackNotes[b]; bool on = active[note];
        float bx = blackKeyCentreX(b) - m_blackKeyWidth*0.5f;
        float bw = m_blackKeyWidth, bh = m_blackKeyHeight;
        juce::ColourGradient grad((on ? Colors::blackKeyActive : Colors::blackKey).brighter(0.15f), bx, 0,
                                  (on ? Colors::blackKeyActive : Colors::blackKey).darker(0.3f),   bx, bh, false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(bx, 0, bw, bh, 3.0f);
        g.setColour(on ? Colors::blackKeyActive.brighter(0.3f) : Colors::accent.withAlpha(0.6f));
        g.drawRoundedRectangle(bx, 0, bw, bh, 3.0f, 1.5f);
    }
}

//==============================================================================
// LabelledKnob
//==============================================================================
LabelledKnob::LabelledKnob(const juce::String& labelText, const juce::String& paramID,
    PitchControlEQAudioProcessor& processor, juce::LookAndFeel* laf)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour(juce::Slider::textBoxTextColourId,       Colors::textBright);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    if (laf) slider.setLookAndFeel(laf);

    label.setText(labelText, juce::dontSendNotification);
    label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    label.setColour(juce::Label::textColourId, Colors::textBright);
    label.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(slider);
    addAndMakeVisible(label);

    m_attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, slider);
}

void LabelledKnob::resized()
{
    auto a = getLocalBounds();
    label.setBounds(a.removeFromTop(18));
    slider.setBounds(a);
}

//==============================================================================
// RangeSlider  –  horizontal slider with label above
//==============================================================================
RangeSlider::RangeSlider(const juce::String& labelText, const juce::String& paramID,
                         PitchControlEQAudioProcessor& processor)
{
    m_label.setText(labelText, juce::dontSendNotification);
    m_label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    m_label.setColour(juce::Label::textColourId, Colors::textBright);
    m_label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_label);

    m_slider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 18);
    m_slider.setColour(juce::Slider::textBoxTextColourId,       Colors::textBright);
    m_slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    m_slider.setColour(juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    m_slider.setColour(juce::Slider::thumbColourId,             Colors::knobFill);
    m_slider.setColour(juce::Slider::trackColourId,             Colors::knobFill.withAlpha(0.5f));
    m_slider.setColour(juce::Slider::backgroundColourId,        Colors::knobTrack);
    addAndMakeVisible(m_slider);

    // Use a lambda text function to show note name + octave instead of raw index
    m_slider.textFromValueFunction = [](double v) -> juce::String {
        int n = juce::roundToInt(v);
        n = juce::jlimit(0, kTotalNotes - 1, n);
        return kNoteNames[n % kNumNotes] + juce::String(n / kNumNotes);
    };
    m_slider.valueFromTextFunction = [](const juce::String& t) -> double {
        // Try to parse e.g. "C#4" → note index
        if (t.isEmpty()) return 0.0;
        int noteClass = -1;
        int charsUsed = 0;
        // Try two-char note names first (C#, D#, F#, G#, A#)
        if (t.length() >= 2)
        {
            juce::String two = t.substring(0, 2).toUpperCase();
            for (int i = 0; i < kNumNotes; ++i)
                if (kNoteNames[i].toUpperCase() == two) { noteClass = i; charsUsed = 2; break; }
        }
        // Then single-char
        if (noteClass < 0)
        {
            juce::String one = t.substring(0, 1).toUpperCase();
            for (int i = 0; i < kNumNotes; ++i)
                if (kNoteNames[i].toUpperCase() == one) { noteClass = i; charsUsed = 1; break; }
        }
        if (noteClass < 0) return t.getDoubleValue();
        int oct = t.substring(charsUsed).getIntValue();
        return juce::jlimit(0, kTotalNotes - 1, noteClass + oct * kNumNotes);
    };

    m_attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, m_slider);
    // Re-apply after attachment (attachment resets these)
    m_slider.textFromValueFunction = [](double v) -> juce::String {
        int n = juce::roundToInt(v);
        n = juce::jlimit(0, kTotalNotes - 1, n);
        return kNoteNames[n % kNumNotes] + juce::String(n / kNumNotes);
        };
    m_slider.valueFromTextFunction = [](const juce::String& t) -> double {
        if (t.isEmpty()) return 0.0;
        int noteClass = -1;
        int charsUsed = 0;
        if (t.length() >= 2)
        {
            juce::String two = t.substring(0, 2).toUpperCase();
            for (int i = 0; i < kNumNotes; ++i)
                if (kNoteNames[i].toUpperCase() == two) { noteClass = i; charsUsed = 2; break; }
        }
        if (noteClass < 0)
        {
            juce::String one = t.substring(0, 1).toUpperCase();
            for (int i = 0; i < kNumNotes; ++i)
                if (kNoteNames[i].toUpperCase() == one) { noteClass = i; charsUsed = 1; break; }
        }
        if (noteClass < 0) return t.getDoubleValue();
        int oct = t.substring(charsUsed).getIntValue();
        return juce::jlimit(0, kTotalNotes - 1, noteClass + oct * kNumNotes);
        };

    m_slider.updateText();
}

void RangeSlider::resized()
{
    auto a = getLocalBounds();
    m_label.setBounds(a.removeFromTop(18));
    m_slider.setBounds(a);
}

//==============================================================================
// PitchControlEQAudioProcessorEditor
//==============================================================================
PitchControlEQAudioProcessorEditor::PitchControlEQAudioProcessorEditor(PitchControlEQAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      m_keyboard(p),
      m_depthKnob    ("Cut dB",      PitchControlEQAudioProcessor::dampenDBParamID(),    p, &m_laf),
      m_qKnob        ("Cut Width",   PitchControlEQAudioProcessor::dampenQParamID(),     p, &m_laf),
      m_boostKnob    ("Boost dB",    PitchControlEQAudioProcessor::boostDBParamID(),     p, &m_laf),
      m_boostQKnob   ("Boost Width", PitchControlEQAudioProcessor::boostQParamID(),      p, &m_laf),
      m_outputGainKnob("Output",     PitchControlEQAudioProcessor::outputGainParamID(),  p, &m_laf),
      m_rangeFrom    ("Range From",  PitchControlEQAudioProcessor::rangeFromParamID(),   p),
      m_rangeTo      ("Range To",    PitchControlEQAudioProcessor::rangeToParamID(),     p),
      m_bellDBKnob   ("Bell Cut",    PitchControlEQAudioProcessor::globalBellDBParamID(),    p, &m_laf),
      m_bellBWKnob   ("Bell Width",  PitchControlEQAudioProcessor::globalBellBWParamID(),    p, &m_laf),
      m_bellFreqKnob  ("Bell Center",PitchControlEQAudioProcessor::globalBellFreqParamID(),  p, &m_laf),
      m_chorusRateKnob ("Chorus Rate",  PitchControlEQAudioProcessor::chorusRateParamID(),   p, &m_laf),
      m_chorusDepthKnob("Chorus Depth", PitchControlEQAudioProcessor::chorusDepthParamID(),  p, &m_laf),
      m_chorusMixKnob  ("Chorus Mix",   PitchControlEQAudioProcessor::chorusMixParamID(),    p, &m_laf)
{
    setLookAndFeel(&m_laf);

    // MIDI Mode button — top right
    m_midiModeButton.setButtonText("MIDI MODE");
    m_midiModeButton.setClickingTogglesState(true);
    m_midiModeButton.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff1a1a2e));
    m_midiModeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00ffcc));
    m_midiModeButton.setColour(juce::TextButton::textColourOffId,  juce::Colour(0xff888888));
    m_midiModeButton.setColour(juce::TextButton::textColourOnId,   juce::Colour(0xff001a14));
    // Attach to APVTS — state now persists with preset/session
    m_midiModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        p.apvts, PitchControlEQAudioProcessor::midiModeParamID(), m_midiModeButton);
    // Also reset held counts when user turns the mode off via the button
    m_midiModeButton.onClick = [this]()
    {
        if (!m_midiModeButton.getToggleState())
            for (auto& c : audioProcessor.m_midiHeldCount)
                c.store(0);
    };
    addAndMakeVisible(m_midiModeButton);

    m_titleLabel.setText("PitchControlEQ VST by aquanode", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    m_titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_titleLabel);

    addAndMakeVisible(m_keyboard);
    addAndMakeVisible(m_depthKnob);
    addAndMakeVisible(m_qKnob);
    addAndMakeVisible(m_boostKnob);
    addAndMakeVisible(m_boostQKnob);
    addAndMakeVisible(m_outputGainKnob);
    addAndMakeVisible(m_rangeFrom);
    addAndMakeVisible(m_rangeTo);

    // Row 2 controls
    addAndMakeVisible(m_bellDBKnob);
    addAndMakeVisible(m_bellBWKnob);
    addAndMakeVisible(m_bellFreqKnob);
    addAndMakeVisible(m_chorusRateKnob);
    addAndMakeVisible(m_chorusDepthKnob);
    addAndMakeVisible(m_chorusMixKnob);

    // Width knobs: param 0=wide(4st) → 1=narrow(0.01st). Display actual BW.
    auto widthToDisplayBW = [](double t) -> juce::String {
        float bw = 4.0f * std::exp(-6.0f * (float)t);
        if (bw >= 1.0f)  return juce::String(bw, 1) + " st";
        if (bw >= 0.1f)  return juce::String(bw, 2) + " st";
        return juce::String(bw, 3) + " st";
    };
    m_qKnob.slider.textFromValueFunction      = widthToDisplayBW;
    m_boostQKnob.slider.textFromValueFunction = widthToDisplayBW;
    m_qKnob.slider.updateText();
    m_boostQKnob.slider.updateText();

    // Bell Width knob: param 1→144st stored, but we invert so left=wide, right=narrow.
    // Display shows the actual audible width (inverted from stored value).
    m_bellBWKnob.slider.textFromValueFunction = [](double v) -> juce::String {
        float audibleBW = 145.0f - (float)v;   // invert: stored 1→144 becomes audible 144→1
        return juce::String(audibleBW, 0) + " st";
    };
    m_bellBWKnob.slider.updateText();

    // Bell Freq knob: display as note name
    m_bellFreqKnob.slider.textFromValueFunction = [](double v) -> juce::String {
        int n = juce::jlimit(0, kTotalNotes - 1, juce::roundToInt(v));
        return kNoteNames[n % kNumNotes] + juce::String(n / kNumNotes);
    };
    m_bellFreqKnob.slider.valueFromTextFunction = [](const juce::String& t) -> double {
        if (t.isEmpty()) return 0.0;
        int noteClass = -1; int charsUsed = 0;
        if (t.length() >= 2) {
            juce::String two = t.substring(0, 2).toUpperCase();
            for (int i = 0; i < kNumNotes; ++i)
                if (kNoteNames[i].toUpperCase() == two) { noteClass = i; charsUsed = 2; break; }
        }
        if (noteClass < 0) {
            juce::String one = t.substring(0, 1).toUpperCase();
            for (int i = 0; i < kNumNotes; ++i)
                if (kNoteNames[i].toUpperCase() == one) { noteClass = i; charsUsed = 1; break; }
        }
        if (noteClass < 0) return t.getDoubleValue();
        int oct = t.substring(charsUsed).getIntValue();
        return juce::jlimit(0, kTotalNotes - 1, noteClass + oct * kNumNotes);
    };
    m_bellFreqKnob.slider.updateText();

    setSize(660, 460); // Increased height to accommodate the second line
    setResizable(false, false);
    startTimerHz(30);
}

PitchControlEQAudioProcessorEditor::~PitchControlEQAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void PitchControlEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int H = getHeight();

    juce::ColourGradient bg(Colors::background, 0, 0,
                             Colors::panelBg.darker(0.3f), 0, (float)H, false);
    g.setGradientFill(bg);
    g.fillAll();

    // Subtle ambient glows
    {
        juce::DropShadow s1(juce::Colour(0xff00ffff).withAlpha(0.3f), 80, { 0,0 });
        juce::DropShadow s2(juce::Colour(0xff00ffff).withAlpha(0.2f), 120, { 0,0 });
        juce::Path c1, c2;
        c1.addEllipse(80,  110, 160, 160);
        c2.addEllipse(420, 170, 200, 200);
        s1.drawForPath(g, c1);
        s2.drawForPath(g, c2);
    }

    // Keyboard panel surround
    juce::Rectangle<float> kb(10, 34, (float)(getWidth() - 20), 158.0f);
    g.setColour(Colors::accent.withAlpha(0.4f));
    g.fillRoundedRectangle(kb.expanded(4), 10);
    g.setColour(Colors::borderColor.withAlpha(0.5f));
    g.drawRoundedRectangle(kb.expanded(4), 10, 1.5f);

    // Controls panel surround
    juce::Rectangle<float> ctrl(10, 204, (float)(getWidth() - 20), 240.0f); // Expanded height for second line
    g.setColour(Colors::panelBg.withAlpha(0.7f));
    g.fillRoundedRectangle(ctrl, 8);
    g.setColour(Colors::borderColor.withAlpha(0.35f));
    g.drawRoundedRectangle(ctrl, 8, 1.0f);
}

//==============================================================================
void PitchControlEQAudioProcessorEditor::resized()
{
    const int margin = 14;
    const int W = getWidth() - 2 * margin;

    // MIDI Mode button — top right, vertically centred in title bar
    const int btnW = 90, btnH = 20;
    m_midiModeButton.setBounds(getWidth() - margin - btnW, 4, btnW, btnH);

    // Title (leave room for button on the right)
    m_titleLabel.setBounds(margin, 4, W - btnW - 8, 24);

    // Piano keyboard
    m_keyboard.setBounds(margin, 36, W, 152);

    // -----------------------------------------------------------------------
    // Controls: Row 1 — 7 equal slots (5 knobs + 2 range sliders)
    //           Row 2 — 6 equal slots (3 bell + 3 chorus), no gaps
    // -----------------------------------------------------------------------
    const int ctrlAreaY = 212;
    const int knobH = 105;

    // --- Row 1: Cut dB | Cut Width | Boost dB | Boost Width | Output | Range From/To ---
    // Use same 6-slot grid as Row 2 so the range column aligns with Chorus Mix
    const int numSlots1 = 6;
    const int slotW1 = W / numSlots1;
    auto slotX1 = [&](int i) { return margin + i * slotW1 + (slotW1 - 100) / 2; };

    m_depthKnob.setBounds     (slotX1(0), ctrlAreaY, 100, knobH);
    m_qKnob.setBounds         (slotX1(1), ctrlAreaY, 100, knobH);
    m_boostKnob.setBounds     (slotX1(2), ctrlAreaY, 100, knobH);
    m_boostQKnob.setBounds    (slotX1(3), ctrlAreaY, 100, knobH);
    m_outputGainKnob.setBounds(slotX1(4), ctrlAreaY, 100, knobH);

    const int sliderH = (knobH - 8) / 2;
    m_rangeFrom.setBounds(slotX1(5), ctrlAreaY,                 100, sliderH);
    m_rangeTo.setBounds  (slotX1(5), ctrlAreaY + sliderH + 8,   100, sliderH);

    // --- Row 2: Bell Cut | Bell Width | Bell Center | Chorus Rate | Chorus Depth | Chorus Mix ---
    const int ctrlAreaY2 = ctrlAreaY + knobH + 15;
    const int numSlots2 = 6;
    const int slotW2 = W / numSlots2;
    auto slotX2 = [&](int i) { return margin + i * slotW2 + (slotW2 - 100) / 2; };

    m_bellDBKnob.setBounds    (slotX2(0), ctrlAreaY2, 100, knobH);
    m_bellBWKnob.setBounds    (slotX2(1), ctrlAreaY2, 100, knobH);
    m_bellFreqKnob.setBounds  (slotX2(2), ctrlAreaY2, 100, knobH);
    m_chorusRateKnob.setBounds (slotX2(3), ctrlAreaY2, 100, knobH);
    m_chorusDepthKnob.setBounds(slotX2(4), ctrlAreaY2, 100, knobH);
    m_chorusMixKnob.setBounds  (slotX2(5), ctrlAreaY2, 100, knobH);
}