#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
// PhizmOscLookAndFeel
//==============================================================================
PhizmOscLookAndFeel::PhizmOscLookAndFeel()
{
    setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.7f));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0x33000000));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a1a6e));
    setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff6ec7));
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1a0a2e));
    setColour(juce::TextEditor::textColourId, juce::Colour(0xff6ec6ff));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff6040a0));
}

// void PhizmOscLookAndFeel::drawRotarySlider(juce::Graphics& g,
//     int x, int y, int w, int h, float sliderPos, float startA, float endA, juce::Slider&)
// {
//     auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4.f);
//     float cx = b.getCentreX(), cy = b.getCentreY(), r = b.getWidth() * 0.5f;
//     juce::ColourGradient gl(accent1.withAlpha(0.35f), cx, cy, accent2.withAlpha(0.f), cx + r, cy, true);
//     g.setGradientFill(gl); g.fillEllipse(b.expanded(4.f));
//     juce::Colour rc = accent1.interpolatedWith(accent2, sliderPos);
//     g.setColour(rc); g.drawEllipse(b, 2.f);
//     juce::ColourGradient bg2(knobFace.brighter(0.15f), cx - r * 0.3f, cy - r * 0.4f, knobFace.darker(0.3f), cx + r * 0.3f, cy + r * 0.4f, true);
//     g.setGradientFill(bg2); g.fillEllipse(b.reduced(2.f));
//     float ang = startA + sliderPos * (endA - startA);
//     float px = cx + (r - 6.f) * std::sin(ang), py = cy - (r - 6.f) * std::cos(ang);
//     juce::Path ptr;
//     ptr.startNewSubPath(cx + (r * 0.15f) * std::sin(ang), cy - (r * 0.15f) * std::cos(ang));
//     ptr.lineTo(px, py);
//     g.setColour(rc.brighter(0.5f));
//     g.strokePath(ptr, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
//     g.setColour(juce::Colours::white.withAlpha(0.9f)); g.fillEllipse(px - 2.5f, py - 2.5f, 5.f, 5.f);
//     juce::Path arc; arc.addCentredArc(cx, cy, r + 3.f, r + 3.f, 0.f, startA, endA, true);
//     g.setColour(juce::Colours::white.withAlpha(0.12f)); g.strokePath(arc, juce::PathStrokeType(1.5f));
//     juce::Path af; af.addCentredArc(cx, cy, r + 3.f, r + 3.f, 0.f, startA, startA + sliderPos * (endA - startA), true);
//     g.setColour(rc.withAlpha(0.6f)); g.strokePath(af, juce::PathStrokeType(2.5f));
// }

void PhizmOscLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int w, int h, float sliderPos, float startA, float endA, juce::Slider&)
{
    auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4.f);
    float cx = b.getCentreX(), cy = b.getCentreY(), r = b.getWidth() * 0.5f;

    // 1. Outer Glow background
    juce::ColourGradient gl(accent1.withAlpha(0.35f), cx, cy, accent2.withAlpha(0.f), cx + r, cy, true);
    g.setGradientFill(gl); g.fillEllipse(b.expanded(4.f));

    // 2. The main Ellipse border (Keeping this)
    juce::Colour rc = accent1.interpolatedWith(accent2, sliderPos);
    g.setColour(rc); g.drawEllipse(b, 2.f);

    // 3. Knob Face Body
    juce::ColourGradient knobBodyGrad(knobFace.brighter(0.15f), cx - r * 0.3f, cy - r * 0.4f, knobFace.darker(0.3f), cx + r * 0.3f, cy + r * 0.4f, true);
    g.setGradientFill(knobBodyGrad); g.fillEllipse(b.reduced(2.f));

    // 4. Pointer Needle Line & Dot
    float ang = startA + sliderPos * (endA - startA);
    float px = cx + (r - 4.f) * std::sin(ang), py = cy - (r - 4.f) * std::cos(ang);
    juce::Path ptr;
    ptr.startNewSubPath(cx, cy);
    ptr.lineTo(px, py);
    g.setColour(rc.brighter(0.5f));
    g.strokePath(ptr, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.9f)); g.fillEllipse(px - 2.5f, py - 2.5f, 5.f, 5.f);
}

void PhizmOscLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(juce::Colour(0x00000000));
    auto font = getLabelFont(label); g.setFont(font);
    g.setColour(label.findColour(juce::Label::textColourId));
    g.drawFittedText(label.getText(), label.getLocalBounds(), label.getJustificationType(),
        juce::jmax(1, (int)((float)label.getHeight() / font.getHeight())));
}

juce::Font PhizmOscLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions().withHeight(11.f));
}

void PhizmOscLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
    const juce::Colour&, bool hi, bool dn) {
    auto bounds = b.getLocalBounds().toFloat().reduced(1.f);
    bool tog = b.getToggleState();
    float a = dn ? 0.95f : (hi ? 0.75f : (tog ? 0.85f : 0.5f));
    juce::Colour c1 = tog ? juce::Colour(col_accent2) : juce::Colour(col_accent1);
    juce::Colour c2 = tog ? juce::Colour(col_accent1) : juce::Colour(col_accent2);
    juce::ColourGradient gr(c1.withAlpha(a), bounds.getTopLeft(), c2.withAlpha(a * 0.7f), bounds.getBottomRight(), false);
    g.setGradientFill(gr); g.fillRoundedRectangle(bounds, 4.f);
    g.setColour((tog ? c2 : c1).withAlpha(0.8f)); g.drawRoundedRectangle(bounds, 4.f, tog ? 1.5f : 1.f);
}

void PhizmOscLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

//==============================================================================
// PhizmOscKnob
//==============================================================================
PhizmOscKnob::PhizmOscKnob(const juce::String& lbl,
    juce::AudioProcessorValueTreeState& apvts, const juce::String& id, PhizmOscLookAndFeel& laf)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
    slider.setLookAndFeel(&laf); addAndMakeVisible(slider);
    label.setText(lbl, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setLookAndFeel(&laf); addAndMakeVisible(label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, slider);
}
void PhizmOscKnob::resized() { auto b = getLocalBounds(); label.setBounds(b.removeFromBottom(16)); slider.setBounds(b); }

//==============================================================================
// EvoCurveEditor
//==============================================================================
EvoCurveEditor::EvoCurveEditor(TranswaveAudioProcessor& p, int oscIndex, const juce::String& titleText)
    : proc(p), osc(oscIndex), title(titleText)
{
    startTimerHz(30);

    btnReset.onClick = [this] {
        for (int i = 0; i < EVO_POINTS; ++i) proc.setCurvePoint(i, 0.5f, osc);
        repaint(); };
    btnRamp.onClick = [this] {
        for (int i = 0; i < EVO_POINTS; ++i) proc.setCurvePoint(i, (float)i / (float)(EVO_POINTS - 1), osc);
        repaint(); };
    btnStepped.onClick = [this] {
        if (auto* p2 = proc.apvts.getParameter(steppedParamID()))
            p2->setValueNotifyingHost(p2->getValue() > 0.5f ? 0.f : 1.f);
        repaint(); };

    addAndMakeVisible(btnReset);
    addAndMakeVisible(btnRamp);
    addAndMakeVisible(btnStepped);
}

EvoCurveEditor::~EvoCurveEditor() { stopTimer(); }

juce::String EvoCurveEditor::steppedParamID() const { return osc == 0 ? "evoStepped" : "evoSteppedB"; }

void EvoCurveEditor::resized()
{
    // Buttons scale with the component width; they sit in their own row below the title.
    const int w = getWidth();
    const int btnY = 24, btnH = juce::jmax(16, getHeight() / 15);
    const int btnW = juce::jmax(40, w / 4 - 8);
    const int gap = juce::jmax(2, w / 50);
    btnReset.setBounds(gap, btnY, btnW, btnH);
    btnRamp.setBounds(gap + btnW + gap, btnY, btnW, btnH);
    btnStepped.setBounds(gap + 2 * (btnW + gap), btnY, btnW + gap, btnH);
}

juce::Rectangle<float> EvoCurveEditor::drawArea() const
{
    auto b = getLocalBounds().toFloat();
    // Top margin clears: title row (~18px) + button row (~24px starting at 24, height 20) + gap
    return b.reduced(4.f, 0.f).withTrimmedTop(50.f).withTrimmedBottom(4.f);
}

float EvoCurveEditor::pointToPixelX(int idx) const {
    auto da = drawArea();
    return da.getX() + (float)idx / (float)(EVO_POINTS - 1) * da.getWidth();
}

float EvoCurveEditor::pointToPixelY(float val) const {
    auto da = drawArea();
    return da.getBottom() - val * da.getHeight();
}

int EvoCurveEditor::xToPointIndex(float px) const {
    auto da = drawArea();
    float t = (px - da.getX()) / da.getWidth();
    return juce::jlimit(0, EVO_POINTS - 1, (int)std::round(t * (float)(EVO_POINTS - 1)));
}

float EvoCurveEditor::yToVal(float py) const {
    auto da = drawArea();
    float v = (da.getBottom() - py) / da.getHeight();
    return juce::jlimit(0.f, 1.f, v);
}

void EvoCurveEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto da = drawArea();

    juce::ColourGradient bg(juce::Colour(0xff0d0520), b.getTopLeft(),
        juce::Colour(0xff051030), b.getBottomRight(), false);
    g.setGradientFill(bg); g.fillRoundedRectangle(b, 6.f);
    g.setColour(juce::Colour(0xff6040a0).withAlpha(0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.f, 1.f);

    // Title — its own row at the very top, well clear of the FLAT/RAMP/STEP
    // buttons (which sit in their own row below it). No more hidden text.
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
    g.setColour(juce::Colour(PhizmOscLookAndFeel::col_accent1).withAlpha(0.85f));
    g.drawText(title, b.reduced(6.f, 2.f).withHeight(16.f).toNearestInt(),
        juce::Justification::centredLeft);

    // Grid
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    for (int i = 0; i <= 4; ++i) {
        float y = pointToPixelY((float)i / 4.f);
        g.drawHorizontalLine((int)y, da.getX(), da.getRight());
    }
    for (int i = 0; i < EVO_POINTS; i += 8) {
        float x = pointToPixelX(i);
        g.drawVerticalLine((int)x, da.getY(), da.getBottom());
    }

    bool stepped = (proc.apvts.getRawParameterValue(steppedParamID()) &&
        proc.apvts.getRawParameterValue(steppedParamID())->load() > 0.5f);

    // Fill
    juce::Path fillPath;
    fillPath.startNewSubPath(pointToPixelX(0), da.getBottom());
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        if (stepped && i > 0) {
            float py = pointToPixelY(proc.getCurvePoint(i - 1, osc));
            fillPath.lineTo(x, py);
        }
        fillPath.lineTo(x, y);
    }
    fillPath.lineTo(pointToPixelX(EVO_POINTS - 1), da.getBottom());
    fillPath.closeSubPath();
    juce::ColourGradient fillGrad(juce::Colour(PhizmOscLookAndFeel::col_accent1).withAlpha(0.18f), 0.f, da.getY(),
        juce::Colour(PhizmOscLookAndFeel::col_accent2).withAlpha(0.04f), 0.f, da.getBottom(), false);
    g.setGradientFill(fillGrad); g.fillPath(fillPath);

    // Line
    juce::Path linePath;
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        if (i == 0) linePath.startNewSubPath(x, y);
        else {
            if (stepped) { float py = pointToPixelY(proc.getCurvePoint(i - 1, osc)); linePath.lineTo(x, py); }
            linePath.lineTo(x, y);
        }
    }
    juce::ColourGradient lineGrad(juce::Colour(PhizmOscLookAndFeel::col_accent1), da.getTopLeft(),
        juce::Colour(PhizmOscLookAndFeel::col_accent2), da.getTopRight(), false);
    g.setGradientFill(lineGrad);
    g.strokePath(linePath, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Point handles
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        bool hovered = (i == dragIndex);
        g.setColour(hovered ? juce::Colour(PhizmOscLookAndFeel::col_accent1) : juce::Colours::white.withAlpha(0.55f));
        g.fillEllipse(x - 3.5f, y - 3.5f, 7.f, 7.f);
        if (hovered) {
            g.setColour(juce::Colour(PhizmOscLookAndFeel::col_accent1).withAlpha(0.6f));
            g.drawEllipse(x - 5.f, y - 5.f, 10.f, 10.f, 1.5f);
        }
    }

    // Playhead — same shared scan position for both oscillators, evaluated
    // through THIS curve's own shape.
    float ph = proc.evoPlayhead.load();
    float phX = da.getX() + ph * da.getWidth();
    float phVal = proc.evalCurve(ph, osc);
    float phY = pointToPixelY(phVal);
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.drawVerticalLine((int)phX, da.getY(), da.getBottom());
    g.setColour(juce::Colour(PhizmOscLookAndFeel::col_accent1));
    g.fillEllipse(phX - 4.f, phY - 4.f, 8.f, 8.f);

    // Min/max labels
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.f)));
    g.setColour(juce::Colours::white.withAlpha(0.3f));
    g.drawText("1.0", juce::Rectangle<int>((int)da.getX() - 2, (int)da.getY(), 26, 12), juce::Justification::centredRight);
    g.drawText("0.0", juce::Rectangle<int>((int)da.getX() - 2, (int)da.getBottom() - 12, 26, 12), juce::Justification::centredRight);
}

void EvoCurveEditor::handleDrag(const juce::MouseEvent& e) {
    int idx = xToPointIndex((float)e.x);
    float val = yToVal((float)e.y);
    dragIndex = idx;
    proc.setCurvePoint(idx, val, osc);
    repaint();
}

void EvoCurveEditor::mouseDown(const juce::MouseEvent& e) { handleDrag(e); }
void EvoCurveEditor::mouseDrag(const juce::MouseEvent& e) { handleDrag(e); }
void EvoCurveEditor::mouseUp(const juce::MouseEvent&) { dragIndex = -1; repaint(); }

//==============================================================================
// WavetableDisplay
//==============================================================================
WavetableDisplay::WavetableDisplay(TranswaveAudioProcessor& p, int slot, const juce::String& lbl)
    : proc(p), slotIndex(slot), slotLabel(lbl) {
    startTimerHz(30);
}
WavetableDisplay::~WavetableDisplay() { stopTimer(); }

bool WavetableDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".wav")) return true;
    return false;
}

void WavetableDisplay::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void WavetableDisplay::fileDragExit(const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void WavetableDisplay::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.hasFileExtension("wav") && file.existsAsFile())
        {
            int cs = getCycleSize ? getCycleSize() : 2048;
            if (cs < 16) cs = 2048;
            proc.loadWavetable(file, cs, slotIndex);
            break;
        }
    }
}

void WavetableDisplay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient bg(juce::Colour(0xff0d0520), b.getTopLeft(), juce::Colour(0xff051030), b.getBottomRight(), false);
    g.setGradientFill(bg); g.fillRoundedRectangle(b, 6.f);

    // Both oscillators are always active; highlight by mix amount
    float mix = proc.apvts.getRawParameterValue("oscMix") ?
        proc.apvts.getRawParameterValue("oscMix")->load() : 0.5f;
    bool isActive = (slotIndex == 0) ? (mix <= 0.5f) : (mix > 0.5f);
    juce::Colour bc = isActive ? juce::Colour(0xffff6ec7) : juce::Colour(0xff6040a0);
    g.setColour(bc.withAlpha(isActive ? 0.9f : 0.5f));
    g.drawRoundedRectangle(b.reduced(0.5f), 6.f, isActive ? 1.5f : 1.f);

    juce::Rectangle<float> badge(b.getX() + 6.f, b.getY() + 5.f, 42.f, 15.f);
    juce::ColourGradient bg2(isActive ? juce::Colour(0xffff6ec7) : juce::Colour(0xff6040a0), badge.getTopLeft(),
        isActive ? juce::Colour(0xff6ec6ff) : juce::Colour(0xff2a1060), badge.getBottomRight(), false);
    g.setGradientFill(bg2); g.fillRoundedRectangle(badge, 3.f);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    g.drawText(slotLabel, badge.toNearestInt(), juce::Justification::centred);

    // Drag-and-drop hover highlight
    if (dragOver)
    {
        g.setColour(juce::Colour(0x44ffffff));
        g.fillRoundedRectangle(b.reduced(2.f), 6.f);
        g.setColour(juce::Colour(PhizmOscLookAndFeel::col_accent1));
        g.drawRoundedRectangle(b.reduced(1.5f), 6.f, 2.f);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.f)));
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawText("Drop .wav here", b, juce::Justification::centred);
        return;
    }

    if (!proc.isWavetableLoaded(slotIndex)) {
        g.setColour(juce::Colour(0xff6040a0).withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.f)));
        g.drawText("Drop .wav  or  LOAD", b, juce::Justification::centred); return;
    }

    g.setColour(juce::Colour(0xaaff6ec7));
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.f)));
    g.drawText(proc.getWavetableName(slotIndex),
        b.reduced(54.f, 4.f).withTop(b.getY() + 5.f).withHeight(15.f).toNearestInt(),
        juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xaa6ec6ff));
    g.setFont(juce::Font(juce::FontOptions().withHeight(10.f)));
    juce::String info = juce::String(proc.getNumFrames(slotIndex)) + " fr  " +
        juce::String(proc.getCycleSamples(slotIndex)) + " smp";
    g.drawText(info, b.reduced(4.f, 5.f).withHeight(14.f).toNearestInt(), juce::Justification::topRight);

    int nf = proc.getNumFrames(slotIndex);
    int w = (int)b.getWidth();
    if (nf > 0 && w > 0) {
        juce::Path wp;
        float midY = b.getCentreY(), halfH = b.getHeight() * 0.38f;
        float fp = proc.getCurrentEvoFramePos(slotIndex);   // each osc reads its OWN curve
        float fi = fp * (float)(nf - 1);
        for (int px = 0; px < w; ++px) {
            double ph = (double)px / (double)(w - 1);
            float s = proc.sampleFrameNearest(slotIndex, fi, ph);
            float y = midY - s * halfH;
            if (px == 0) wp.startNewSubPath((float)px + b.getX(), y);
            else         wp.lineTo((float)px + b.getX(), y);
        }
        if (isActive) {
            juce::ColourGradient wg(juce::Colour(0xffff6ec7), b.getTopLeft(), juce::Colour(0xff6ec6ff), b.getBottomRight(), false);
            g.setGradientFill(wg);
        }
        else g.setColour(juce::Colour(0xff6040a0).withAlpha(0.6f));
        g.strokePath(wp, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

//==============================================================================
// TranswaveAudioProcessorEditor
//==============================================================================
TranswaveAudioProcessorEditor::TranswaveAudioProcessorEditor(TranswaveAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    // ROW 1
    knobGain("Gain", p.apvts, "gain", laf),
    knobEvoTime("Evo Time", p.apvts, "evoTime", laf),
    knobEvoLFORate("Evo LFO Hz", p.apvts, "evoLFORate", laf),
    knobEvoLFODepth("Evo LFO Dpt", p.apvts, "evoLFODepth", laf),
    knobPosLFORate("Pos LFO Hz", p.apvts, "posLFORate", laf),
    knobPosLFODepth("Pos LFO Dpt", p.apvts, "posLFODepth", laf),
    knobDetune("Detune ct", p.apvts, "detune", laf),
    knobPitchLFO("Pitch LFO", p.apvts, "pitchLFO", laf),
    knobPitchLFORate("Pitch LFO Hz", p.apvts, "pitchLFORate", laf),
    knobPitchEnvAmt("P. Env Amt", p.apvts, "pitchEnvAmt", laf),
    knobPitchEnvAtt("P. Env Att", p.apvts, "pitchEnvAtt", laf),
    knobPitchEnvDec("P. Env Dec", p.apvts, "pitchEnvDec", laf),
    // ROW 2 (new): Transwave envelope
    knobTwAtt("TW Att", p.apvts, "twAtt", laf),
    knobTwDec("TW Dec", p.apvts, "twDec", laf),
    knobTwSus("TW Sus", p.apvts, "twSus", laf),
    knobTwRel("TW Rel", p.apvts, "twRel", laf),
    knobTwAmt("TW Amt", p.apvts, "twAmt", laf),
    knobTwVelAmt("TW Vel", p.apvts, "twVelAmt", laf),
    // ROW 2: Misc
    knobFrameSnap("Frm Snap", p.apvts, "frameSnap", laf),
    knobTwToFilter("TW>Filt", p.apvts, "twToFilter", laf),
    knobEvoBPhaseOff("B Ph.Off", p.apvts, "evoBPhaseOff", laf),
    knobKeytrack("Keytrack", p.apvts, "keytrack", laf),
    knobEvoTimeB("Evo T. B", p.apvts, "evoTimeB", laf),
    knobEvoRestart("Restart", p.apvts, "evoRestart", laf),
    // ROW 3: ENVELOPE | CHARACTER | SCAN | FILTER
    knobAttack("Osc 1 Att", p.apvts, "attack", laf),
    knobDecay("Osc 1 Dec", p.apvts, "decay", laf),
    knobSustain("Osc 1 Sus", p.apvts, "sustain", laf),
    knobRelease("Osc 1 Rel", p.apvts, "release", laf),
    knobBitCrush("Bit Depth", p.apvts, "bitCrush", laf),
    knobGrit("Grit", p.apvts, "grit", laf),
    knobScanStyle("Scan Dir", p.apvts, "scanStyle", laf),
    knobScanJump("Rnd Jump", p.apvts, "jumpProb", laf),
    knobFilterFreq("Cutoff", p.apvts, "filterFreq", laf),
    knobFilterRes("Resonance", p.apvts, "filterQ", laf),
    knobFilterAtt("F. Att", p.apvts, "filterAtt", laf),
    knobFilterDec("F. Dec", p.apvts, "filterDec", laf),
    knobFilterSus("F. Sus", p.apvts, "filterSus", laf),
    knobFilterRel("F. Rel", p.apvts, "filterRel", laf),
    knobFilterEnvAmt("F. Env Amt", p.apvts, "filterEnvAmt", laf),
    knobFilterLFODep("F. LFO", p.apvts, "filterLFODep", laf),
    // ROW 3: OSC2 | OCTAVES | GLIDE/MONO
    knobAttack2("Osc 2 Att", p.apvts, "attack2", laf),
    knobDecay2("Osc 2 Dec", p.apvts, "decay2", laf),
    knobSustain2("Osc 2 Sus", p.apvts, "sustain2", laf),
    knobRelease2("Osc 2 Rel", p.apvts, "release2", laf),
    knobOctaveA("Oct Osc 1", p.apvts, "octaveA", laf),
    knobOctaveB("Oct Osc 2", p.apvts, "octaveB", laf),
    knobGlide("Glide", p.apvts, "glide", laf),
    knobMono("Mono", p.apvts, "mono", laf),
    // ROW 4: STEREO | FX
    knobSpread("Spread ct", p.apvts, "spread", laf),
    knobStereoWidth("Width", p.apvts, "stereoWidth", laf),
    knobUniDetune("Uni Detune", p.apvts, "uniDetune", laf),
    knobStereoPhase("B Phase", p.apvts, "stereoPhase", laf),
    knobOscMix("A/B Mix", p.apvts, "oscMix", laf),
    knobChorusRate("Chr Rate", p.apvts, "chorusRate", laf),
    knobChorusDepth("Chr Depth", p.apvts, "chorusDepth", laf),
    knobRingMod("Ring Mod", p.apvts, "ringMod", laf),
    knobReverbSize("Rvb Size", p.apvts, "reverbSize", laf),
    knobReverbDamp("Rvb Damp", p.apvts, "reverbDamp", laf),
    knobReverbWet("Rvb Wet", p.apvts, "reverbWet", laf),
    knobNoise("Noise", p.apvts, "noiseLevel", laf),
    evoCurveEditorA(p, 0, "OSC A EVOLUTION CURVE"),
    evoCurveEditorB(p, 1, "OSC B EVOLUTION CURVE"),
    wtDisplayA(p, 0, "OSC A"),
    wtDisplayB(p, 1, "OSC B")
{
    setLookAndFeel(&laf);
    // Base size 1190x804; resizable from 50% to 200%.
    // Restore last saved GUI size if available (persisted via processor state).
    setResizable(true, true);
    setResizeLimits(595, 402, 2380, 1608);
    // Restore saved width from APVTS; height is derived from the fixed aspect ratio.
    setSize(audioProcessor.getGuiWidth(), audioProcessor.getGuiHeight());

    auto setupSec = [&](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
        l.setColour(juce::Label::textColourId, juce::Colour(0xffff6ec7));
        l.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(l); };

    setupSec(sectionWT, "WAVETABLE");
    setupSec(sectionEvo, "EVOLUTION");
    setupSec(sectionPitch, "PITCH");
    setupSec(sectionTWEnv, "TRANSWAVE ENVELOPE");
    setupSec(sectionMisc, "MISC");
    setupSec(sectionADSR, "OSC 1 ENVELOPE");
    setupSec(sectionGrit, "CHARACTER");
    setupSec(sectionScan, "SCAN");
    setupSec(sectionFilter, "FILTER");
    setupSec(sectionOsc2, "OSC 2 ENVELOPE");
    setupSec(sectionOctave, "OCTAVE");
    setupSec(sectionGlide, "GLIDE");
    setupSec(sectionFilterEnv, "FILTER ENVELOPE");
    setupSec(sectionStereo, "STEREO");
    setupSec(sectionFX, "FX");

    infoLabel.setText("2-Oscillator Transwave Synthesizer", juce::dontSendNotification);
    infoLabel.setColour(juce::Label::textColourId, juce::Colour(0xaaff6ec7));
    infoLabel.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    infoLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(infoLabel);

    presetNameLabel.setText("- init -", juce::dontSendNotification);
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0d0ff));
    presetNameLabel.setColour(juce::Label::backgroundColourId, juce::Colour(0x22ffffff));
    presetNameLabel.setColour(juce::Label::outlineColourId, juce::Colour(0x33ff6ec7));
    presetNameLabel.setFont(juce::Font(juce::FontOptions().withHeight(12.f)));
    presetNameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(presetNameLabel);

    savePresetButton.setButtonText("SAVE"); savePresetButton.setLookAndFeel(&laf);
    savePresetButton.onClick = [this] { savePresetClicked(); };
    addAndMakeVisible(savePresetButton);
    loadPresetButton.setButtonText("LOAD"); loadPresetButton.setLookAndFeel(&laf);
    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    addAndMakeVisible(loadPresetButton);

    // --- Engine mode toggles ---
    auto setupToggle = [&](juce::TextButton& btn, const juce::String& paramID)
        {
            btn.setLookAndFeel(&laf);
            btn.setClickingTogglesState(true);
            auto* rawParam = audioProcessor.apvts.getRawParameterValue(paramID);
            btn.setToggleState(rawParam && rawParam->load() > 0.5f, juce::dontSendNotification);
            btn.onClick = [this, &btn, paramID]
                {
                    if (auto* param = audioProcessor.apvts.getParameter(paramID))
                        param->setValueNotifyingHost(btn.getToggleState() ? 1.f : 0.f);
                };
            addAndMakeVisible(btn);
        };
    setupToggle(toggleFrameInterp, "frameInterp");
    setupToggle(toggleFilterPerVoice, "filterPerVoice");
    setupToggle(toggleVelToFrame, "velToFrame");
    setupToggle(toggleEvoPhaseCarry, "evoPhaseCarry");

    // Not a parameter-backed toggle: a momentary action button that opens a
    // folder browser and sets the default starting folder for LOAD A/LOAD B.
    toggleSampleFolder.setLookAndFeel(&laf);
    toggleSampleFolder.onClick = [this] { setSampleFolderClicked(); };
    {
        juce::String tip = audioProcessor.getSampleFolder().isNotEmpty()
            ? audioProcessor.getSampleFolder()
            : audioProcessor.getPresetFolder();
        if (tip.isNotEmpty()) toggleSampleFolder.setTooltip(tip);
    }
    addAndMakeVisible(toggleSampleFolder);

    // Load A
    loadButtonA.setButtonText("LOAD A"); loadButtonA.setLookAndFeel(&laf);
    loadButtonA.onClick = [this] { loadWavetableClicked(0); }; addAndMakeVisible(loadButtonA);
    cycleSizeLabelA.setText("Cycle:", juce::dontSendNotification);
    cycleSizeLabelA.setColour(juce::Label::textColourId, juce::Colour(0xff6ec6ff));
    cycleSizeLabelA.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    addAndMakeVisible(cycleSizeLabelA);
    cycleSizeEditorA.setText(juce::String(audioProcessor.getCycleSizeParam(0)));
    cycleSizeEditorA.setInputRestrictions(6, "0123456789");
    cycleSizeEditorA.setLookAndFeel(&laf);
    // Commit only on Return or focus-lost, never mid-keystroke.
    // This prevents the timer sync from overwriting the field while the user is editing.
    auto commitCycleSizeA = [this] {
        int v = cycleSizeEditorA.getText().getIntValue();
        if (v >= 16)
            audioProcessor.setCycleSizeParam(0, v);
        else
            cycleSizeEditorA.setText(juce::String(audioProcessor.getCycleSizeParam(0)), false);
        };
    cycleSizeEditorA.onReturnKey = commitCycleSizeA;
    cycleSizeEditorA.onFocusLost = commitCycleSizeA;
    // Escape reverts to last valid value without committing.
    cycleSizeEditorA.onEscapeKey = [this] {
        cycleSizeEditorA.setText(juce::String(audioProcessor.getCycleSizeParam(0)), false);
        cycleSizeEditorA.moveCaretToEnd();
        };
    addAndMakeVisible(cycleSizeEditorA);
    filenameLabelA.setText("No file loaded", juce::dontSendNotification);
    filenameLabelA.setColour(juce::Label::textColourId, juce::Colour(0x88ff6ec7));
    filenameLabelA.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    filenameLabelA.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filenameLabelA);

    // Load B
    loadButtonB.setButtonText("LOAD B"); loadButtonB.setLookAndFeel(&laf);
    loadButtonB.onClick = [this] { loadWavetableClicked(1); }; addAndMakeVisible(loadButtonB);
    cycleSizeLabelB.setText("Cycle:", juce::dontSendNotification);
    cycleSizeLabelB.setColour(juce::Label::textColourId, juce::Colour(0xff6ec6ff));
    cycleSizeLabelB.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    addAndMakeVisible(cycleSizeLabelB);
    cycleSizeEditorB.setText(juce::String(audioProcessor.getCycleSizeParam(1)));
    cycleSizeEditorB.setInputRestrictions(6, "0123456789");
    cycleSizeEditorB.setLookAndFeel(&laf);
    auto commitCycleSizeB = [this] {
        int v = cycleSizeEditorB.getText().getIntValue();
        if (v >= 16)
            audioProcessor.setCycleSizeParam(1, v);
        else
            cycleSizeEditorB.setText(juce::String(audioProcessor.getCycleSizeParam(1)), false);
        };
    cycleSizeEditorB.onReturnKey = commitCycleSizeB;
    cycleSizeEditorB.onFocusLost = commitCycleSizeB;
    cycleSizeEditorB.onEscapeKey = [this] {
        cycleSizeEditorB.setText(juce::String(audioProcessor.getCycleSizeParam(1)), false);
        cycleSizeEditorB.moveCaretToEnd();
        };
    addAndMakeVisible(cycleSizeEditorB);
    filenameLabelB.setText("No file loaded", juce::dontSendNotification);
    filenameLabelB.setColour(juce::Label::textColourId, juce::Colour(0x886ec6ff));
    filenameLabelB.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    filenameLabelB.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filenameLabelB);

    // --- Smooth sliders (compact rotary next to Load A / Load B) ---
    auto setupSmoothSlider = [&](juce::Slider& sl, juce::Label& lb, const juce::String& labelText)
        {
            sl.setSliderStyle(juce::Slider::RotaryVerticalDrag);
            sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            sl.setLookAndFeel(&laf);
            sl.setTooltip("Smooth: fade in/out at cycle edges (up to 5% of cycle length) — helps avoid clicks with unusual cycle sizes");
            addAndMakeVisible(sl);
            lb.setText(labelText, juce::dontSendNotification);
            lb.setFont(juce::Font(juce::FontOptions().withHeight(9.5f)));
            lb.setColour(juce::Label::textColourId, juce::Colour(0xff6ec6ff));
            lb.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(lb);
        };
    setupSmoothSlider(smoothSliderA, smoothLabelA, "Smooth:");
    setupSmoothSlider(smoothSliderB, smoothLabelB, "Smooth:");
    smoothAttachA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "wtSmooth", smoothSliderA);
    smoothAttachB = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "wtSmoothB", smoothSliderB);

    addAndMakeVisible(evoCurveEditorA);
    addAndMakeVisible(evoCurveEditorB);
    addAndMakeVisible(wtDisplayA);
    addAndMakeVisible(wtDisplayB);

    // Give each display a way to read the current cycle-size field so that
    // drag-and-drop loads use whatever the user has typed, just like LOAD A/B.
    wtDisplayA.getCycleSize = [this] { return cycleSizeEditorA.getText().getIntValue(); };
    wtDisplayB.getCycleSize = [this] { return cycleSizeEditorB.getText().getIntValue(); };

    // All knobs
    addAndMakeVisible(knobGain);
    addAndMakeVisible(knobEvoTime);     addAndMakeVisible(knobEvoLFORate);   addAndMakeVisible(knobEvoLFODepth);
    addAndMakeVisible(knobPosLFORate);  addAndMakeVisible(knobPosLFODepth);
    addAndMakeVisible(knobDetune);      addAndMakeVisible(knobPitchLFO);     addAndMakeVisible(knobPitchLFORate);
    addAndMakeVisible(knobPitchEnvAmt); addAndMakeVisible(knobPitchEnvAtt);  addAndMakeVisible(knobPitchEnvDec);
    // Row 2 (new): TW envelope (right 6 only; left 6 are intentionally empty)
    addAndMakeVisible(knobTwAtt);  addAndMakeVisible(knobTwDec);  addAndMakeVisible(knobTwSus);
    addAndMakeVisible(knobTwRel);  addAndMakeVisible(knobTwAmt);  addAndMakeVisible(knobTwVelAmt);
    // Row 2: Misc (left 6)
    addAndMakeVisible(knobFrameSnap);    addAndMakeVisible(knobTwToFilter);
    addAndMakeVisible(knobEvoBPhaseOff); addAndMakeVisible(knobKeytrack);
    addAndMakeVisible(knobEvoTimeB);     addAndMakeVisible(knobEvoRestart);
    // Row 3
    addAndMakeVisible(knobAttack);      addAndMakeVisible(knobDecay);        addAndMakeVisible(knobSustain);    addAndMakeVisible(knobRelease);
    addAndMakeVisible(knobBitCrush);    addAndMakeVisible(knobGrit);
    addAndMakeVisible(knobScanStyle);   addAndMakeVisible(knobScanJump);
    addAndMakeVisible(knobFilterFreq);  addAndMakeVisible(knobFilterRes);
    addAndMakeVisible(knobFilterEnvAmt); addAndMakeVisible(knobFilterLFODep);
    // Row 3
    addAndMakeVisible(knobAttack2);     addAndMakeVisible(knobDecay2);       addAndMakeVisible(knobSustain2);   addAndMakeVisible(knobRelease2);
    addAndMakeVisible(knobOctaveA);     addAndMakeVisible(knobOctaveB);
    addAndMakeVisible(knobGlide);       addAndMakeVisible(knobMono);
    addAndMakeVisible(knobFilterAtt);   addAndMakeVisible(knobFilterDec);
    addAndMakeVisible(knobFilterSus);   addAndMakeVisible(knobFilterRel);
    // Row 4
    addAndMakeVisible(knobSpread);      addAndMakeVisible(knobStereoWidth);  addAndMakeVisible(knobUniDetune);  addAndMakeVisible(knobStereoPhase);
    addAndMakeVisible(knobOscMix);
    addAndMakeVisible(knobChorusRate);  addAndMakeVisible(knobChorusDepth);
    addAndMakeVisible(knobRingMod);
    addAndMakeVisible(knobReverbSize);  addAndMakeVisible(knobReverbDamp);   addAndMakeVisible(knobReverbWet);
    addAndMakeVisible(knobNoise);

    startTimerHz(20);
    // Must be last — prevents resized() from writing back to APVTS during construction.
    editorReady = true;
}

TranswaveAudioProcessorEditor::~TranswaveAudioProcessorEditor()
{
    stopTimer(); setLookAndFeel(nullptr);
}

void TranswaveAudioProcessorEditor::timerCallback()
{
    // Sync UI fields that may have been updated by setStateInformation()
    // while the editor was open (e.g. when the DAW restores a project).

    // Bug 1: GUI size
    int targetW = audioProcessor.getGuiWidth();
    if (editorReady && getWidth() != targetW)
        setSize(targetW, audioProcessor.getGuiHeight());

    // Bug 3: Cycle sizes — only sync when the field doesn't have focus,
    // otherwise we'd overwrite the user's in-progress edit every 50 ms.
    int csA = audioProcessor.getCycleSizeParam(0);
    if (!cycleSizeEditorA.hasKeyboardFocus(false) &&
        cycleSizeEditorA.getText().getIntValue() != csA)
        cycleSizeEditorA.setText(juce::String(csA), false);

    int csB = audioProcessor.getCycleSizeParam(1);
    if (!cycleSizeEditorB.hasKeyboardFocus(false) &&
        cycleSizeEditorB.getText().getIntValue() != csB)
        cycleSizeEditorB.setText(juce::String(csB), false);

    // Bug 2: Preset name
    juce::String pname = audioProcessor.getCurrentPresetName();
    if (pname.isNotEmpty() && presetNameLabel.getText() != pname)
        presetNameLabel.setText(pname, juce::dontSendNotification);
}

void TranswaveAudioProcessorEditor::loadWavetableClicked(int slot)
{
    juce::File startDir = audioProcessor.getSampleFolder().isNotEmpty()
        ? juce::File(audioProcessor.getSampleFolder())
        : TranswaveAudioProcessor::getPresetsDirectory();
    fileChooser = std::make_unique<juce::FileChooser>("Select wavetable .wav", startDir, "*.wav");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot](const juce::FileChooser& ch) {
            auto f = ch.getResult(); if (!f.existsAsFile()) return;
            int cs = (slot == 0) ? cycleSizeEditorA.getText().getIntValue()
                : cycleSizeEditorB.getText().getIntValue();
            if (cs < 16) cs = 2048;
            audioProcessor.loadWavetable(f, cs, slot);
            if (slot == 0) filenameLabelA.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification);
            else           filenameLabelB.setText(f.getFileNameWithoutExtension(), juce::dontSendNotification); });
}

void TranswaveAudioProcessorEditor::savePresetClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save preset as...",
        audioProcessor.getEffectivePresetsDirectory().getChildFile("New Preset.phizm"), "*.phizm");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& ch) {
            auto dest = ch.getResult(); if (dest == juce::File{}) return;
            if (dest.getFileExtension().toLowerCase() != ".phizm") dest = dest.withFileExtension(".phizm");
            juce::String name = audioProcessor.savePreset(dest)
                ? dest.getFileNameWithoutExtension() : "Save failed!";
            audioProcessor.setCurrentPresetName(name);
            presetNameLabel.setText(name, juce::dontSendNotification); });
}

void TranswaveAudioProcessorEditor::loadPresetClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Open preset...",
        audioProcessor.getEffectivePresetsDirectory(), "*.phizm");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& ch) {
            auto src = ch.getResult(); if (!src.existsAsFile()) return;
            loadPresetFromFile(src); });
}

void TranswaveAudioProcessorEditor::loadPresetFromFile(const juce::File& src)
{
    if (audioProcessor.loadPreset(src))
    {
        juce::String name = src.getFileNameWithoutExtension();
        audioProcessor.setCurrentPresetName(name);
        presetNameLabel.setText(name, juce::dontSendNotification);
        refreshUIAfterPresetLoad();
    }
    else presetNameLabel.setText("Load failed!", juce::dontSendNotification);
}

void TranswaveAudioProcessorEditor::refreshUIAfterPresetLoad()
{
    filenameLabelA.setText(audioProcessor.isWavetableLoaded(0) ?
        audioProcessor.getWavetableName(0) : "No file loaded", juce::dontSendNotification);
    filenameLabelB.setText(audioProcessor.isWavetableLoaded(1) ?
        audioProcessor.getWavetableName(1) : "No file loaded", juce::dontSendNotification);
    // Always restore cycle size fields from APVTS — they are persisted there
    // and must be shown even when no wavetable has been loaded yet.
    cycleSizeEditorA.setText(juce::String(audioProcessor.getCycleSizeParam(0)), false);
    cycleSizeEditorB.setText(juce::String(audioProcessor.getCycleSizeParam(1)), false);
    // Restore preset name label from processor state.
    juce::String pname = audioProcessor.getCurrentPresetName();
    if (pname.isNotEmpty())
        presetNameLabel.setText(pname, juce::dontSendNotification);
}

void TranswaveAudioProcessorEditor::setSampleFolderClicked()
{
    juce::File startDir = audioProcessor.getSampleFolder().isNotEmpty()
        ? juce::File(audioProcessor.getSampleFolder())
        : TranswaveAudioProcessor::getPresetsDirectory();
    fileChooser = std::make_unique<juce::FileChooser>("Choose sample & preset folder...", startDir);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& ch) {
            auto dir = ch.getResult(); if (!dir.isDirectory()) return;
            // One folder for both .wav samples and .phizm presets
            audioProcessor.setSampleFolder(dir.getFullPathName());
            audioProcessor.setPresetFolder(dir.getFullPathName());
            toggleSampleFolder.setTooltip(dir.getFullPathName());
        });
}

bool TranswaveAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    // The editor only handles .phizm preset drops.
    // .wav drops are handled by the WavetableDisplay components directly,
    // so that slot routing (A vs B) is determined by which display is hovered.
    for (auto& f : files)
        if (f.endsWithIgnoreCase(".phizm")) return true;
    return false;
}

void TranswaveAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    // Only .phizm files reach here (see isInterestedInFileDrag above).
    for (auto& f : files)
    {
        juce::File file(f);
        if (file.hasFileExtension("phizm") && file.existsAsFile())
        {
            loadPresetFromFile(file);
            break;
        }
    }
}

//==============================================================================
void TranswaveAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient bg(juce::Colour(0xff120820), 0.f, 0.f, juce::Colour(0xff051030), b.getWidth(), b.getHeight(), false);
    g.setGradientFill(bg); g.fillAll();
    for (int y = 0; y < (int)b.getHeight(); y += 3) {
        g.setColour(juce::Colours::black.withAlpha(0.07f));
        g.drawHorizontalLine(y, 0.f, b.getWidth());
    }
    juce::ColourGradient strip(juce::Colour(0xffff6ec7), 0.f, 0.f, juce::Colour(0xff6ec6ff), b.getWidth(), 0.f, false);
    g.setGradientFill(strip);
    g.fillRect(0.f, 0.f, b.getWidth(), 3.f);
    g.fillRect(0.f, b.getHeight() - 3.f, b.getWidth(), 3.f);

    // All positions scale with current window size relative to base 1190×804
    const float scaleX = b.getWidth() / 1190.f;
    const float scaleY = b.getHeight() / 804.f;

    g.setFont(juce::Font(juce::FontOptions().withHeight(juce::jmax(12.f, 22.f * scaleY))));
    juce::ColourGradient tg(juce::Colour(0xffff6ec7), 18.f * scaleX, 14.f * scaleY,
        juce::Colour(0xff6ec6ff), 200.f * scaleX, 14.f * scaleY, false);
    g.setGradientFill(tg);
    g.drawText("PhizmOsc", juce::roundToInt(18 * scaleX), juce::roundToInt(8 * scaleY),
        juce::roundToInt(280 * scaleX), juce::roundToInt(28 * scaleY), juce::Justification::centredLeft);

    // Panel backgrounds
    auto dp = [&](int px, int py, int pw, int ph) {
        auto r = juce::Rectangle<int>(juce::roundToInt(px * scaleX),
            juce::roundToInt(py * scaleY),
            juce::roundToInt(pw * scaleX),
            juce::roundToInt(ph * scaleY));
        g.setColour(juce::Colour(0x22ffffff)); g.fillRoundedRectangle(r.toFloat(), 6.f);
        g.setColour(juce::Colour(0x33ff6ec7)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.f, 0.8f); };

    // ROW 1: WT(1) | EVOLUTION(5) | PITCH(6) — cols 1–12
    dp(12, 52, 1 * 70, 114);
    dp(12 + 1 * 70, 52, 5 * 70, 114);
    dp(12 + 6 * 70, 52, 6 * 70, 114);

    // ROW 2 (new): MISC(6) left | TW ENVELOPE(6) right
    dp(12, 176, 6 * 70, 114);
    dp(12 + 6 * 70, 176, 6 * 70, 114);

    // ROW 3: OSC1 ADSR(4) | CHAR(2) | SCAN(2) | FILTER top(4) — cols 1–12
    dp(12, 300, 4 * 70, 114);
    dp(12 + 4 * 70, 300, 2 * 70, 114);
    dp(12 + 6 * 70, 300, 2 * 70, 114);
    dp(12 + 8 * 70, 300, 4 * 70, 114);

    // ROW 4: OSC2(4) | OCTAVE(2) | GLIDE(2) | FILTER ENVELOPE(4) — cols 1–12
    dp(12, 424, 4 * 70, 114);
    dp(12 + 4 * 70, 424, 2 * 70, 114);
    dp(12 + 6 * 70, 424, 2 * 70, 114);
    dp(12 + 8 * 70, 424, 4 * 70, 114);

    // ROW 5: STEREO(5) | FX(7) — cols 1–12
    dp(12, 548, 5 * 70, 114);
    dp(12 + 5 * 70, 548, 7 * 70, 114);

    // Row dividers (base pixel coords, scaled via scaleX/scaleY)
    g.setColour(juce::Colour(0x22ff6ec7));
    const float divX1 = 12.f * scaleX, divX2 = (12.f + 12.f * 70.f) * scaleX;
    g.drawHorizontalLine(juce::roundToInt(295 * scaleY), divX1, divX2);
    g.drawHorizontalLine(juce::roundToInt(419 * scaleY), divX1, divX2);
    g.drawHorizontalLine(juce::roundToInt(543 * scaleY), divX1, divX2);
}

void TranswaveAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    // The "PhizmOsc" title is drawn directly in paint() (not a separate
    // Component), using the same base-pixel rect: (18,8,280,28) scaled by
    // the current window size relative to the 1190x804 design. Recompute
    // that same rect here so a double-click anywhere on the title resets
    // the GUI back to its default size.
    const float scaleX = (float)getWidth() / 1190.f;
    const float scaleY = (float)getHeight() / 804.f;
    juce::Rectangle<int> titleBounds(juce::roundToInt(18 * scaleX), juce::roundToInt(8 * scaleY),
        juce::roundToInt(280 * scaleX), juce::roundToInt(28 * scaleY));

    if (titleBounds.contains(e.getPosition()))
    {
        constexpr int defaultW = 1190;
        setSize(defaultW, juce::roundToInt(defaultW * (804.f / 1190.f)));
        audioProcessor.setGuiWidth(defaultW);
    }
}


//==============================================================================
void TranswaveAudioProcessorEditor::resized()
{
    // Save the new width to APVTS so the DAW persists it automatically.
    // The editorReady guard prevents the construction-time setSize() call
    // from overwriting the value that was just restored from APVTS.
    if (editorReady)
        audioProcessor.setGuiWidth(getWidth());

    // Scale every coordinate from the base 1190×804 design to the current window size.
    const float W = (float)getWidth();
    const float H = (float)getHeight();
    const float sX = W / 1190.f;
    const float sY = H / 804.f;

    // Helper: scale a base-pixel rect and return it as integer bounds.
    auto S = [&](float bx, float by, float bw, float bh) -> juce::Rectangle<int> {
        return { juce::roundToInt(bx * sX), juce::roundToInt(by * sY),
                 juce::roundToInt(bw * sX), juce::roundToInt(bh * sY) };
        };

    // Base-pixel grid constants (matching original design)
    const float kw = 70.f, kh = 80.f;
    const float x0 = 12.f;

    // Row knob y-starts
    const float ky1 = 70.f, ky2 = 194.f, ky3 = 318.f, ky4 = 442.f, ky5 = 566.f;
    // Row label y-starts
    const float ly1 = 54.f, ly2 = 178.f, ly3 = 302.f, ly4 = 426.f, ly5 = 550.f;

    // --- Title bar ---
    const float tbY = 9.f, tbH = 26.f;
    toggleFrameInterp.setBounds(S(200, tbY, 72, tbH));
    toggleFilterPerVoice.setBounds(S(278, tbY, 72, tbH));
    toggleVelToFrame.setBounds(S(356, tbY, 72, tbH));
    toggleEvoPhaseCarry.setBounds(S(434, tbY, 84, tbH));
    toggleSampleFolder.setBounds(S(522, tbY, 90, tbH));
    presetNameLabel.setBounds(S(618, tbY, 230, tbH));
    infoLabel.setBounds(S(858, 13, 168, 18));
    loadPresetButton.setBounds(S(1034, tbY, 64, tbH));
    savePresetButton.setBounds(S(1104, tbY, 64, tbH));

    // --- Section labels ---
    const float lh = 14.f;
    // Row 1
    sectionWT.setBounds(S(x0 + 5, ly1, kw - 5, lh));
    sectionEvo.setBounds(S(x0 + 1 * kw + 5, ly1, 5 * kw - 5, lh));
    sectionPitch.setBounds(S(x0 + 6 * kw + 5, ly1, 6 * kw - 5, lh));
    // Row 2
    sectionMisc.setBounds(S(x0 + 5, ly2, 6 * kw - 5, lh));
    sectionTWEnv.setBounds(S(x0 + 6 * kw + 5, ly2, 6 * kw - 5, lh));
    // Row 3
    sectionADSR.setBounds(S(x0 + 5, ly3, 4 * kw - 5, lh));
    sectionGrit.setBounds(S(x0 + 4 * kw + 5, ly3, 2 * kw - 5, lh));
    sectionScan.setBounds(S(x0 + 6 * kw + 5, ly3, 2 * kw - 5, lh));
    sectionFilter.setBounds(S(x0 + 8 * kw + 5, ly3, 4 * kw - 5, lh));
    // Row 4
    sectionOsc2.setBounds(S(x0 + 5, ly4, 4 * kw - 5, lh));
    sectionOctave.setBounds(S(x0 + 4 * kw + 5, ly4, 2 * kw - 5, lh));
    sectionGlide.setBounds(S(x0 + 6 * kw + 5, ly4, 2 * kw - 5, lh));
    sectionFilterEnv.setBounds(S(x0 + 8 * kw + 5, ly4, 4 * kw - 5, lh));
    // Row 5
    sectionStereo.setBounds(S(x0 + 5, ly5, 5 * kw - 5, lh));
    sectionFX.setBounds(S(x0 + 5 * kw + 5, ly5, 7 * kw - 5, lh));

    // ============================================================
    // ROW 1 — 12 knobs: Gain | Evo(5) | Pitch(6)
    knobGain.setBounds(S(x0 + 0 * kw, ky1, kw, kh));
    knobEvoTime.setBounds(S(x0 + 1 * kw, ky1, kw, kh));
    knobEvoLFORate.setBounds(S(x0 + 2 * kw, ky1, kw, kh));
    knobEvoLFODepth.setBounds(S(x0 + 3 * kw, ky1, kw, kh));
    knobPosLFORate.setBounds(S(x0 + 4 * kw, ky1, kw, kh));
    knobPosLFODepth.setBounds(S(x0 + 5 * kw, ky1, kw, kh));
    knobDetune.setBounds(S(x0 + 6 * kw, ky1, kw, kh));
    knobPitchLFO.setBounds(S(x0 + 7 * kw, ky1, kw, kh));
    knobPitchLFORate.setBounds(S(x0 + 8 * kw, ky1, kw, kh));
    knobPitchEnvAmt.setBounds(S(x0 + 9 * kw, ky1, kw, kh));
    knobPitchEnvAtt.setBounds(S(x0 + 10 * kw, ky1, kw, kh));
    knobPitchEnvDec.setBounds(S(x0 + 11 * kw, ky1, kw, kh));

    // ============================================================
    // ROW 2 — cols 1–6: MISC | cols 7–12: TRANSWAVE ENVELOPE
    knobFrameSnap.setBounds(S(x0 + 0 * kw, ky2, kw, kh));
    knobTwToFilter.setBounds(S(x0 + 1 * kw, ky2, kw, kh));
    knobEvoBPhaseOff.setBounds(S(x0 + 2 * kw, ky2, kw, kh));
    knobKeytrack.setBounds(S(x0 + 3 * kw, ky2, kw, kh));
    knobEvoTimeB.setBounds(S(x0 + 4 * kw, ky2, kw, kh));
    knobEvoRestart.setBounds(S(x0 + 5 * kw, ky2, kw, kh));
    knobTwAtt.setBounds(S(x0 + 6 * kw, ky2, kw, kh));
    knobTwDec.setBounds(S(x0 + 7 * kw, ky2, kw, kh));
    knobTwSus.setBounds(S(x0 + 8 * kw, ky2, kw, kh));
    knobTwRel.setBounds(S(x0 + 9 * kw, ky2, kw, kh));
    knobTwAmt.setBounds(S(x0 + 10 * kw, ky2, kw, kh));
    knobTwVelAmt.setBounds(S(x0 + 11 * kw, ky2, kw, kh));

    // ============================================================
    // ROW 3 — 12 knobs: Osc1ADSR(4) | Char(2) | Scan(2) | Filter top(4)
    knobAttack.setBounds(S(x0 + 0 * kw, ky3, kw, kh));
    knobDecay.setBounds(S(x0 + 1 * kw, ky3, kw, kh));
    knobSustain.setBounds(S(x0 + 2 * kw, ky3, kw, kh));
    knobRelease.setBounds(S(x0 + 3 * kw, ky3, kw, kh));
    knobBitCrush.setBounds(S(x0 + 4 * kw, ky3, kw, kh));
    knobGrit.setBounds(S(x0 + 5 * kw, ky3, kw, kh));
    knobScanStyle.setBounds(S(x0 + 6 * kw, ky3, kw, kh));
    knobScanJump.setBounds(S(x0 + 7 * kw, ky3, kw, kh));
    knobFilterFreq.setBounds(S(x0 + 8 * kw, ky3, kw, kh));
    knobFilterRes.setBounds(S(x0 + 9 * kw, ky3, kw, kh));
    knobFilterEnvAmt.setBounds(S(x0 + 10 * kw, ky3, kw, kh));
    knobFilterLFODep.setBounds(S(x0 + 11 * kw, ky3, kw, kh));

    // ============================================================
    // ROW 4 — 12 knobs: Osc2ADSR(4) | Octave(2) | Glide(2) | Filter Envelope(4)
    knobAttack2.setBounds(S(x0 + 0 * kw, ky4, kw, kh));
    knobDecay2.setBounds(S(x0 + 1 * kw, ky4, kw, kh));
    knobSustain2.setBounds(S(x0 + 2 * kw, ky4, kw, kh));
    knobRelease2.setBounds(S(x0 + 3 * kw, ky4, kw, kh));
    knobOctaveA.setBounds(S(x0 + 4 * kw, ky4, kw, kh));
    knobOctaveB.setBounds(S(x0 + 5 * kw, ky4, kw, kh));
    knobGlide.setBounds(S(x0 + 6 * kw, ky4, kw, kh));
    knobMono.setBounds(S(x0 + 7 * kw, ky4, kw, kh));
    knobFilterAtt.setBounds(S(x0 + 8 * kw, ky4, kw, kh));
    knobFilterDec.setBounds(S(x0 + 9 * kw, ky4, kw, kh));
    knobFilterSus.setBounds(S(x0 + 10 * kw, ky4, kw, kh));
    knobFilterRel.setBounds(S(x0 + 11 * kw, ky4, kw, kh));

    // ============================================================
    // ROW 5 — 12 knobs: Stereo(5) | FX(7)
    knobSpread.setBounds(S(x0 + 0 * kw, ky5, kw, kh));
    knobStereoWidth.setBounds(S(x0 + 1 * kw, ky5, kw, kh));
    knobUniDetune.setBounds(S(x0 + 2 * kw, ky5, kw, kh));
    knobStereoPhase.setBounds(S(x0 + 3 * kw, ky5, kw, kh));
    knobOscMix.setBounds(S(x0 + 4 * kw, ky5, kw, kh));
    knobChorusRate.setBounds(S(x0 + 5 * kw, ky5, kw, kh));
    knobChorusDepth.setBounds(S(x0 + 6 * kw, ky5, kw, kh));
    knobRingMod.setBounds(S(x0 + 7 * kw, ky5, kw, kh));
    knobReverbSize.setBounds(S(x0 + 8 * kw, ky5, kw, kh));
    knobReverbDamp.setBounds(S(x0 + 9 * kw, ky5, kw, kh));
    knobReverbWet.setBounds(S(x0 + 10 * kw, ky5, kw, kh));
    knobNoise.setBounds(S(x0 + 11 * kw, ky5, kw, kh));

    // --- EVOLUTION CURVE EDITORS (right column, stacked: Osc A on top, Osc B below)
    const float curveX = x0 + 12 * kw + 14.f;
    const float curveW = 1190.f - 12.f - curveX;   // base-pixel width
    evoCurveEditorA.setBounds(S(curveX, 52, curveW, 300));
    evoCurveEditorB.setBounds(S(curveX, 362, curveW, 300));

    // --- BOTTOM: wavetable load controls + wavetable displays ---
    // Both zones now use the SAME left-to-right order (no more mirroring):
    //   LOAD  |  Cycle: [editor]  |  Smooth: (knob)  |  filename...
    // Each LOAD button is aligned with the left edge of its own visualizer box.

    // Left zone (Slot A) — visualizer box starts at x0
    loadButtonA.setBounds(S(x0, 674, 84, 28));
    cycleSizeLabelA.setBounds(S(x0 + 90.f, 678, 40, 20));
    cycleSizeEditorA.setBounds(S(x0 + 134.f, 674, 56, 28));
    smoothLabelA.setBounds(S(x0 + 200.f, 678, 50, 20));
    smoothSliderA.setBounds(S(x0 + 254.f, 674, 28, 28));
    filenameLabelA.setBounds(S(x0 + 290.f, 678, 282, 20));

    // Right zone (Slot B) — visualizer box starts at 596, same internal layout as A
    const float zb = 596.f;
    loadButtonB.setBounds(S(zb, 674, 84, 28));
    cycleSizeLabelB.setBounds(S(zb + 90.f, 678, 40, 20));
    cycleSizeEditorB.setBounds(S(zb + 134.f, 674, 56, 28));
    smoothLabelB.setBounds(S(zb + 200.f, 678, 50, 20));
    smoothSliderB.setBounds(S(zb + 254.f, 674, 28, 28));
    filenameLabelB.setBounds(S(zb + 290.f, 678, 282, 20));

    wtDisplayA.setBounds(S(x0, 710, 572, 82));
    wtDisplayB.setBounds(S(596, 710, 572, 82));
}