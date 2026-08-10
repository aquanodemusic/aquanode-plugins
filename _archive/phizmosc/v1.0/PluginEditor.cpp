#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
// PhismOscLookAndFeel
//==============================================================================
PhismOscLookAndFeel::PhismOscLookAndFeel()
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

// void PhismOscLookAndFeel::drawRotarySlider(juce::Graphics& g,
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

void PhismOscLookAndFeel::drawRotarySlider(juce::Graphics& g,
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
    juce::ColourGradient bg2(knobFace.brighter(0.15f), cx - r * 0.3f, cy - r * 0.4f, knobFace.darker(0.3f), cx + r * 0.3f, cy + r * 0.4f, true);
    g.setGradientFill(bg2); g.fillEllipse(b.reduced(2.f));

    // 4. Pointer Needle Line & Dot
    float ang = startA + sliderPos * (endA - startA);
    float px = cx + (r - 10.f) * std::sin(ang), py = cy - (r - 10.f) * std::cos(ang);
    juce::Path ptr;
    ptr.startNewSubPath(cx + (r * 0.15f) * std::sin(ang), cy - (r * 0.15f) * std::cos(ang));
    ptr.lineTo(px, py);
    g.setColour(rc.brighter(0.5f));
    g.strokePath(ptr, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.9f)); g.fillEllipse(px - 2.5f, py - 2.5f, 5.f, 5.f);
}

void PhismOscLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.fillAll(juce::Colour(0x00000000));
    auto font = getLabelFont(label); g.setFont(font);
    g.setColour(label.findColour(juce::Label::textColourId));
    g.drawFittedText(label.getText(), label.getLocalBounds(), label.getJustificationType(),
        juce::jmax(1, (int)((float)label.getHeight() / font.getHeight())));
}

juce::Font PhismOscLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions().withHeight(11.f));
}

void PhismOscLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
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

void PhismOscLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) {
    g.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, 1);
}

//==============================================================================
// PhismOscKnob
//==============================================================================
PhismOscKnob::PhismOscKnob(const juce::String& lbl,
    juce::AudioProcessorValueTreeState& apvts, const juce::String& id, PhismOscLookAndFeel& laf)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 58, 14);
    slider.setLookAndFeel(&laf); addAndMakeVisible(slider);
    label.setText(lbl, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setLookAndFeel(&laf); addAndMakeVisible(label);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, slider);
}
void PhismOscKnob::resized() { auto b = getLocalBounds(); label.setBounds(b.removeFromBottom(16)); slider.setBounds(b); }

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
    // Buttons get their OWN row, well clear of the title text drawn in paint() above
    // them, so the two no longer overlap/hide each other.
    const int btnY = 24, btnH = 20;
    btnReset.setBounds(4, btnY, 56, btnH);
    btnRamp.setBounds(64, btnY, 56, btnH);
    btnStepped.setBounds(124, btnY, 82, btnH);
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
    g.setColour(juce::Colour(PhismOscLookAndFeel::col_accent1).withAlpha(0.85f));
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
    juce::ColourGradient fillGrad(juce::Colour(PhismOscLookAndFeel::col_accent1).withAlpha(0.18f), 0.f, da.getY(),
        juce::Colour(PhismOscLookAndFeel::col_accent2).withAlpha(0.04f), 0.f, da.getBottom(), false);
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
    juce::ColourGradient lineGrad(juce::Colour(PhismOscLookAndFeel::col_accent1), da.getTopLeft(),
        juce::Colour(PhismOscLookAndFeel::col_accent2), da.getTopRight(), false);
    g.setGradientFill(lineGrad);
    g.strokePath(linePath, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Point handles
    for (int i = 0; i < EVO_POINTS; ++i) {
        float x = pointToPixelX(i), y = pointToPixelY(proc.getCurvePoint(i, osc));
        bool hovered = (i == dragIndex);
        g.setColour(hovered ? juce::Colour(PhismOscLookAndFeel::col_accent1) : juce::Colours::white.withAlpha(0.55f));
        g.fillEllipse(x - 3.5f, y - 3.5f, 7.f, 7.f);
        if (hovered) {
            g.setColour(juce::Colour(PhismOscLookAndFeel::col_accent1).withAlpha(0.6f));
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
    g.setColour(juce::Colour(PhismOscLookAndFeel::col_accent1));
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

    if (!proc.isWavetableLoaded(slotIndex)) {
        g.setColour(juce::Colour(0xff6040a0).withAlpha(0.7f));
        g.setFont(juce::Font(juce::FontOptions().withHeight(12.f)));
        g.drawText("Load a .wav wavetable", b, juce::Justification::centred); return;
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
    // ROW 2
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
    knobFilterEnvAmt("F. Env Amt", p.apvts, "filterEnvAmt", laf),
    knobFilterLFODep("F. LFO", p.apvts, "filterLFODep", laf),
    // ROW 3
    knobAttack2("Osc 2 Att", p.apvts, "attack2", laf),
    knobDecay2("Osc 2 Dec", p.apvts, "decay2", laf),
    knobSustain2("Osc 2 Sus", p.apvts, "sustain2", laf),
    knobRelease2("Osc 2 Rel", p.apvts, "release2", laf),
    knobOctaveA("Oct Osc 1", p.apvts, "octaveA", laf),
    knobOctaveB("Oct Osc 2", p.apvts, "octaveB", laf),
    knobGlide("Glide", p.apvts, "glide", laf),
    knobMono("Mono", p.apvts, "mono", laf),
    knobFilterAtt("F. Att", p.apvts, "filterAtt", laf),
    knobFilterDec("F. Dec", p.apvts, "filterDec", laf),
    knobFilterSus("F. Sus", p.apvts, "filterSus", laf),
    knobFilterRel("F. Rel", p.apvts, "filterRel", laf),
    // ROW 4
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
    // Slightly larger than before to give every label and control room to
    // breathe (capped well under 1200 x 700).
    setSize(1190, 680);

    auto setupSec = [&](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions().withHeight(11.5f)));
        l.setColour(juce::Label::textColourId, juce::Colour(0xffff6ec7));
        l.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(l); };

    setupSec(sectionWT, "WAVETABLE");
    setupSec(sectionEvo, "EVOLUTION");
    setupSec(sectionPitch, "PITCH");
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
        auto* param = audioProcessor.apvts.getRawParameterValue(paramID);
        btn.setToggleState(param && param->load() > 0.5f, juce::dontSendNotification);
        btn.onClick = [this, &btn, paramID]
        {
            if (auto* p = audioProcessor.apvts.getParameter(paramID))
                p->setValueNotifyingHost(btn.getToggleState() ? 1.f : 0.f);
        };
        addAndMakeVisible(btn);
    };
    setupToggle(toggleFrameInterp,    "frameInterp");
    setupToggle(toggleFilterPerVoice, "filterPerVoice");
    setupToggle(toggleVelToFrame,     "velToFrame");
    setupToggle(toggleEvoPhaseCarry,  "evoPhaseCarry");

    // Load A
    loadButtonA.setButtonText("LOAD A"); loadButtonA.setLookAndFeel(&laf);
    loadButtonA.onClick = [this] { loadWavetableClicked(0); }; addAndMakeVisible(loadButtonA);
    cycleSizeLabelA.setText("Cycle:", juce::dontSendNotification);
    cycleSizeLabelA.setColour(juce::Label::textColourId, juce::Colour(0xff6ec6ff));
    cycleSizeLabelA.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    addAndMakeVisible(cycleSizeLabelA);
    cycleSizeEditorA.setText("2048"); cycleSizeEditorA.setInputRestrictions(6, "0123456789");
    cycleSizeEditorA.setLookAndFeel(&laf); addAndMakeVisible(cycleSizeEditorA);
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
    cycleSizeEditorB.setText("2048"); cycleSizeEditorB.setInputRestrictions(6, "0123456789");
    cycleSizeEditorB.setLookAndFeel(&laf); addAndMakeVisible(cycleSizeEditorB);
    filenameLabelB.setText("No file loaded", juce::dontSendNotification);
    filenameLabelB.setColour(juce::Label::textColourId, juce::Colour(0x886ec6ff));
    filenameLabelB.setFont(juce::Font(juce::FontOptions().withHeight(10.5f)));
    filenameLabelB.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(filenameLabelB);

    addAndMakeVisible(evoCurveEditorA);
    addAndMakeVisible(evoCurveEditorB);
    addAndMakeVisible(wtDisplayA);
    addAndMakeVisible(wtDisplayB);

    // All knobs
    addAndMakeVisible(knobGain);
    addAndMakeVisible(knobEvoTime);     addAndMakeVisible(knobEvoLFORate);   addAndMakeVisible(knobEvoLFODepth);
    addAndMakeVisible(knobPosLFORate);  addAndMakeVisible(knobPosLFODepth);
    addAndMakeVisible(knobDetune);      addAndMakeVisible(knobPitchLFO);     addAndMakeVisible(knobPitchLFORate);
    addAndMakeVisible(knobPitchEnvAmt); addAndMakeVisible(knobPitchEnvAtt);  addAndMakeVisible(knobPitchEnvDec);
    // Row 2
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
}

TranswaveAudioProcessorEditor::~TranswaveAudioProcessorEditor()
{
    stopTimer(); setLookAndFeel(nullptr);
}

void TranswaveAudioProcessorEditor::loadWavetableClicked(int slot)
{
    fileChooser = std::make_unique<juce::FileChooser>("Select wavetable .wav",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav");
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
        TranswaveAudioProcessor::getPresetsDirectory().getChildFile("New Preset.phism"), "*.phism");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
        juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& ch) {
            auto dest = ch.getResult(); if (dest == juce::File{}) return;
            if (dest.getFileExtension().toLowerCase() != ".phism") dest = dest.withFileExtension(".phism");
            presetNameLabel.setText(audioProcessor.savePreset(dest) ?
                dest.getFileNameWithoutExtension() : "Save failed!", juce::dontSendNotification); });
}

void TranswaveAudioProcessorEditor::loadPresetClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Open preset...",
        TranswaveAudioProcessor::getPresetsDirectory(), "*.phism");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& ch) {
            auto src = ch.getResult(); if (!src.existsAsFile()) return;
            if (audioProcessor.loadPreset(src)) {
                presetNameLabel.setText(src.getFileNameWithoutExtension(), juce::dontSendNotification);
                filenameLabelA.setText(audioProcessor.isWavetableLoaded(0) ?
                    audioProcessor.getWavetableName(0) : "No file loaded", juce::dontSendNotification);
                filenameLabelB.setText(audioProcessor.isWavetableLoaded(1) ?
                    audioProcessor.getWavetableName(1) : "No file loaded", juce::dontSendNotification);
                if (audioProcessor.isWavetableLoaded(0))
                    cycleSizeEditorA.setText(juce::String(audioProcessor.getCycleSamples(0)), false);
                if (audioProcessor.isWavetableLoaded(1))
                    cycleSizeEditorB.setText(juce::String(audioProcessor.getCycleSamples(1)), false);
            }
            else presetNameLabel.setText("Load failed!", juce::dontSendNotification); });
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

    g.setFont(juce::Font(juce::FontOptions().withHeight(22.f)));
    juce::ColourGradient tg(juce::Colour(0xffff6ec7), 18.f, 14.f, juce::Colour(0xff6ec6ff), 200.f, 14.f, false);
    g.setGradientFill(tg); g.drawText("PhizmOsc", 18, 8, 280, 28, juce::Justification::centredLeft);

    // Panel backgrounds
    auto dp = [&](int px, int py, int pw, int ph) {
        auto r = juce::Rectangle<int>(px, py, pw, ph);
        g.setColour(juce::Colour(0x22ffffff)); g.fillRoundedRectangle(r.toFloat(), 6.f);
        g.setColour(juce::Colour(0x33ff6ec7)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 6.f, 0.8f); };

    const int kw = 70;
    const int panH = 114;
    const int x0 = 12;

    // ROW 1: WT(1) | EVOLUTION(5) | PITCH(6) — cols 1–12
    dp(x0, 52, 1 * kw, panH);
    dp(x0 + 1 * kw, 52, 5 * kw, panH);
    dp(x0 + 6 * kw, 52, 6 * kw, panH);

    // ROW 2: OSC1 ADSR(4) | CHAR(2) | SCAN(2) | FILTER top(4) — cols 1–12
    dp(x0, 176, 4 * kw, panH);
    dp(x0 + 4 * kw, 176, 2 * kw, panH);
    dp(x0 + 6 * kw, 176, 2 * kw, panH);
    dp(x0 + 8 * kw, 176, 4 * kw, panH);

    // ROW 3: OSC2(4) | OCTAVE(2) | GLIDE(2) | FILTER ENVELOPE(4) — cols 1–12
    dp(x0, 300, 4 * kw, panH);
    dp(x0 + 4 * kw, 300, 2 * kw, panH);
    dp(x0 + 6 * kw, 300, 2 * kw, panH);
    dp(x0 + 8 * kw, 300, 4 * kw, panH);

    // ROW 4: STEREO(5) | FX(7) — cols 1–12
    dp(x0, 424, 5 * kw, panH);
    dp(x0 + 5 * kw, 424, 7 * kw, panH);

    // Row dividers
    g.setColour(juce::Colour(0x22ff6ec7));
    g.drawHorizontalLine(295, (float)x0, (float)(x0 + 12 * kw));
    g.drawHorizontalLine(419, (float)x0, (float)(x0 + 12 * kw));
}

//==============================================================================
void TranswaveAudioProcessorEditor::resized()
{
    const int kw = 70, kh = 80;
    const int x0 = 12;   // grid left edge

    // Row knob y-starts (label sits ~16px above, knob fills rest)
    const int ky1 = 70;   // row 1
    const int ky2 = 194;  // row 2
    const int ky3 = 318;  // row 3
    const int ky4 = 442;  // row 4

    // Row label y-starts
    const int ly1 = 54;
    const int ly2 = 178;
    const int ly3 = 302;
    const int ly4 = 426;

    // --- Title bar ---
    // Toggles occupy the left of the title row (after logo); preset name moves right
    const int tbY = 9, tbH = 26;
    // Four engine toggles, left to right after the logo (which ends ~200px)
    toggleFrameInterp.setBounds   (200,  tbY, 72, tbH);
    toggleFilterPerVoice.setBounds(278,  tbY, 72, tbH);
    toggleVelToFrame.setBounds    (356,  tbY, 72, tbH);
    toggleEvoPhaseCarry.setBounds (434,  tbY, 84, tbH);
    // Preset name now sits to the right of the toggles
    presetNameLabel.setBounds(528, tbY, 320, tbH);
    infoLabel.setBounds(858, 13, 168, 18);
    loadPresetButton.setBounds(1034, tbY, 64, tbH);
    savePresetButton.setBounds(1104, tbY, 64, tbH);

    // --- Section labels ---
// Row 1
    sectionWT.setBounds(x0 + 5, ly1, kw - 5, 14);
    sectionEvo.setBounds(x0 + 1 * kw + 5, ly1, 5 * kw - 5, 14);
    sectionPitch.setBounds(x0 + 6 * kw + 5, ly1, 6 * kw - 5, 14);
    // Row 2
    sectionADSR.setBounds(x0 + 5, ly2, 4 * kw - 5, 14);
    sectionGrit.setBounds(x0 + 4 * kw + 5, ly2, 2 * kw - 5, 14);
    sectionScan.setBounds(x0 + 6 * kw + 5, ly2, 2 * kw - 5, 14);
    sectionFilter.setBounds(x0 + 8 * kw + 5, ly2, 4 * kw - 5, 14);
    // Row 3
    sectionOsc2.setBounds(x0 + 5, ly3, 4 * kw - 5, 14);
    sectionOctave.setBounds(x0 + 4 * kw + 5, ly3, 2 * kw - 5, 14);
    sectionGlide.setBounds(x0 + 6 * kw + 5, ly3, 2 * kw - 5, 14);
    sectionFilterEnv.setBounds(x0 + 8 * kw + 5, ly3, 4 * kw - 5, 14);
    // Row 4
    sectionStereo.setBounds(x0 + 5, ly4, 5 * kw - 5, 14);
    sectionFX.setBounds(x0 + 5 * kw + 5, ly4, 7 * kw - 5, 14);

    // ============================================================
    // ROW 1 — 12 knobs: Gain | Evo(5) | Pitch(6)
    knobGain.setBounds(x0 + 0 * kw, ky1, kw, kh);
    knobEvoTime.setBounds(x0 + 1 * kw, ky1, kw, kh);
    knobEvoLFORate.setBounds(x0 + 2 * kw, ky1, kw, kh);
    knobEvoLFODepth.setBounds(x0 + 3 * kw, ky1, kw, kh);
    knobPosLFORate.setBounds(x0 + 4 * kw, ky1, kw, kh);
    knobPosLFODepth.setBounds(x0 + 5 * kw, ky1, kw, kh);
    knobDetune.setBounds(x0 + 6 * kw, ky1, kw, kh);
    knobPitchLFO.setBounds(x0 + 7 * kw, ky1, kw, kh);
    knobPitchLFORate.setBounds(x0 + 8 * kw, ky1, kw, kh);
    knobPitchEnvAmt.setBounds(x0 + 9 * kw, ky1, kw, kh);
    knobPitchEnvAtt.setBounds(x0 + 10 * kw, ky1, kw, kh);
    knobPitchEnvDec.setBounds(x0 + 11 * kw, ky1, kw, kh);

    // ============================================================
    // ROW 2 — 12 knobs: Osc1ADSR(4) | Char(2) | Scan(2) | Filter top(4)
    knobAttack.setBounds(x0 + 0 * kw, ky2, kw, kh);
    knobDecay.setBounds(x0 + 1 * kw, ky2, kw, kh);
    knobSustain.setBounds(x0 + 2 * kw, ky2, kw, kh);
    knobRelease.setBounds(x0 + 3 * kw, ky2, kw, kh);
    knobBitCrush.setBounds(x0 + 4 * kw, ky2, kw, kh);
    knobGrit.setBounds(x0 + 5 * kw, ky2, kw, kh);
    knobScanStyle.setBounds(x0 + 6 * kw, ky2, kw, kh);
    knobScanJump.setBounds(x0 + 7 * kw, ky2, kw, kh);
    knobFilterFreq.setBounds(x0 + 8 * kw, ky2, kw, kh);
    knobFilterRes.setBounds(x0 + 9 * kw, ky2, kw, kh);
    knobFilterEnvAmt.setBounds(x0 + 10 * kw, ky2, kw, kh);
    knobFilterLFODep.setBounds(x0 + 11 * kw, ky2, kw, kh);

    // ============================================================
    // ROW 3 — 12 knobs: Osc2ADSR(4) | Octave(2) | Glide(2) | Filter Envelope(4)
    knobAttack2.setBounds(x0 + 0 * kw, ky3, kw, kh);
    knobDecay2.setBounds(x0 + 1 * kw, ky3, kw, kh);
    knobSustain2.setBounds(x0 + 2 * kw, ky3, kw, kh);
    knobRelease2.setBounds(x0 + 3 * kw, ky3, kw, kh);
    knobOctaveA.setBounds(x0 + 4 * kw, ky3, kw, kh);
    knobOctaveB.setBounds(x0 + 5 * kw, ky3, kw, kh);
    knobGlide.setBounds(x0 + 6 * kw, ky3, kw, kh);
    knobMono.setBounds(x0 + 7 * kw, ky3, kw, kh);
    knobFilterAtt.setBounds(x0 + 8 * kw, ky3, kw, kh);
    knobFilterDec.setBounds(x0 + 9 * kw, ky3, kw, kh);
    knobFilterSus.setBounds(x0 + 10 * kw, ky3, kw, kh);
    knobFilterRel.setBounds(x0 + 11 * kw, ky3, kw, kh);

    // ============================================================
    // ROW 4 — 12 knobs: Stereo(5) | FX(7)
    knobSpread.setBounds(x0 + 0 * kw, ky4, kw, kh);
    knobStereoWidth.setBounds(x0 + 1 * kw, ky4, kw, kh);
    knobUniDetune.setBounds(x0 + 2 * kw, ky4, kw, kh);
    knobStereoPhase.setBounds(x0 + 3 * kw, ky4, kw, kh);
    knobOscMix.setBounds(x0 + 4 * kw, ky4, kw, kh);
    knobChorusRate.setBounds(x0 + 5 * kw, ky4, kw, kh);
    knobChorusDepth.setBounds(x0 + 6 * kw, ky4, kw, kh);
    knobRingMod.setBounds(x0 + 7 * kw, ky4, kw, kh);
    knobReverbSize.setBounds(x0 + 8 * kw, ky4, kw, kh);
    knobReverbDamp.setBounds(x0 + 9 * kw, ky4, kw, kh);
    knobReverbWet.setBounds(x0 + 10 * kw, ky4, kw, kh);
    knobNoise.setBounds(x0 + 11 * kw, ky4, kw, kh);

    // --- EVOLUTION CURVE EDITORS (right column, stacked: Osc A on top, Osc B below) ---
    const int curveX = x0 + 12 * kw + 14;
    const int curveW = getWidth() - 12 - curveX;
    evoCurveEditorA.setBounds(curveX, 52, curveW, 238);
    evoCurveEditorB.setBounds(curveX, 300, curveW, 238);

    // --- BOTTOM: wavetable load controls (own row) + wavetable displays (own row below) ---
    // These two rows no longer overlap, so nothing gets hidden behind anything else.
    const int ctrlY = 550, ctrlH = 28, lblYOff = 554, lblH = 20;
    // Left zone (Slot A)
    loadButtonA.setBounds(x0, ctrlY, 84, ctrlH);
    cycleSizeLabelA.setBounds(104, lblYOff, 40, lblH);
    cycleSizeEditorA.setBounds(148, ctrlY, 56, ctrlH);
    filenameLabelA.setBounds(212, lblYOff, 372, lblH);

    // Right zone (Slot B)
    filenameLabelB.setBounds(596, lblYOff, 380, lblH);
    cycleSizeLabelB.setBounds(980, lblYOff, 40, lblH);
    cycleSizeEditorB.setBounds(1024, ctrlY, 56, ctrlH);
    loadButtonB.setBounds(1084, ctrlY, 84, ctrlH);

    const int dispY = 586;
    wtDisplayA.setBounds(x0, dispY, 572, 82);
    wtDisplayB.setBounds(596, dispY, 572, 82);
}
