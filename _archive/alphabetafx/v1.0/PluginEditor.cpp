#include "PluginEditor.h"

// ================================================================
//  FXAlphaLAF  – constructor
// ================================================================
FXAlphaLAF::FXAlphaLAF() {
    setColour(juce::ComboBox::backgroundColourId,       FXPal::bg);
    setColour(juce::ComboBox::textColourId,             FXPal::text);
    setColour(juce::ComboBox::arrowColourId,            FXPal::blue);
    setColour(juce::ComboBox::outlineColourId,          FXPal::track);
    setColour(juce::ComboBox::focusedOutlineColourId,   FXPal::blue);
    setColour(juce::PopupMenu::backgroundColourId,      juce::Colour(0xFF1C2030));
    setColour(juce::PopupMenu::textColourId,            juce::Colour(0xFFCDD2DA));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, FXPal::blueDark);
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

// ================================================================
//  Rotary Slider
// ================================================================
void FXAlphaLAF::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPos, float startAngle, float endAngle,
    juce::Slider& /*slider*/)
{
    auto bounds = juce::Rectangle<float>((float)x, (float)y,
        (float)width, (float)(height - 13)).reduced(8.0f);
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Drop shadow
    g.setColour(juce::Colour(0x50000000));
    g.fillEllipse(cx - radius + 1.5f, cy - radius + 2.5f, radius * 2.0f, radius * 2.0f);

    // Body gradient
    juce::ColourGradient body(
        FXPal::knobTop, cx - radius * 0.4f, cy - radius * 0.5f,
        FXPal::knobBot, cx + radius * 0.5f, cy + radius * 0.6f, false);
    body.addColour(0.45, juce::Colour(0xFFB0B8C4));
    body.addColour(0.70, juce::Colour(0xFF747D8A));
    g.setGradientFill(body);
    g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // Specular
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.fillEllipse(cx - radius * 0.7f, cy - radius * 0.75f, radius * 0.9f, radius * 0.65f);

    // Rim
    g.setColour(FXPal::knobRim);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.4f);

    // Track arc
    float arcR = radius + 4.0f;
    {
        juce::Path tr;
        tr.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(FXPal::track);
        g.strokePath(tr, juce::PathStrokeType(3.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Value arc
    if (sliderPos > 0.001f) {
        juce::Path va;
        va.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, angle, true);
        juce::ColourGradient arcG(
            FXPal::blueDark, cx - arcR, cy,
            FXPal::blueBright, cx + arcR, cy, false);
        arcG.addColour(0.4, FXPal::blue);
        arcG.addColour(0.75, FXPal::blueMid);
        g.setGradientFill(arcG);
        g.strokePath(va, juce::PathStrokeType(3.8f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        float dotX = cx + std::sin(angle) * arcR;
        float dotY = cy - std::cos(angle) * arcR;
        g.setColour(FXPal::blueBright);
        g.fillEllipse(dotX - 2.8f, dotY - 2.8f, 5.6f, 5.6f);
        g.setColour(FXPal::blueGlow);
        g.fillEllipse(dotX - 5.5f, dotY - 5.5f, 11.0f, 11.0f);
    }

    // Indicator line
    {
        float sinA = std::sin(angle), cosA = std::cos(angle);
        float li = radius * 0.25f, lo = radius * 0.80f;
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawLine(cx + sinA * li, cy - cosA * li, cx + sinA * lo, cy - cosA * lo, 2.0f);
    }

    // Centre dot
    g.setColour(FXPal::blue);
    g.fillEllipse(cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
}

// ================================================================
//  ComboBox
// ================================================================
void FXAlphaLAF::drawComboBox(juce::Graphics& g, int width, int height,
    bool isDown, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0.5f, 0.5f,
        (float)width - 1.0f, (float)height - 1.0f);
    juce::ColourGradient bg(
        isDown ? juce::Colour(0xFFC8CDDA) : juce::Colour(0xFFD6DAE4), 0.f, 0.f,
        isDown ? juce::Colour(0xFFB8BDC8) : juce::Colour(0xFFC0C5D0), 0.f, (float)height,
        false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, 4.0f);
    bool active = box.isPopupActive();
    g.setColour(active ? FXPal::blue : juce::Colour(0xFF7A8090));
    g.drawRoundedRectangle(r, 4.0f, active ? 1.8f : 1.1f);
    float ax = (float)width - 13.0f;
    float ay = (float)height * 0.5f;
    juce::Path arr;
    arr.addTriangle(ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour(active ? FXPal::blueBright : FXPal::blue);
    g.fillPath(arr);
}

void FXAlphaLAF::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(5, 0, box.getWidth() - 24, box.getHeight());
    label.setJustificationType(juce::Justification::centredLeft);
}

juce::Font FXAlphaLAF::getComboBoxFont(juce::ComboBox&) {
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain);
}
juce::Font FXAlphaLAF::getLabelFont(juce::Label&) {
    return juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain);
}
void FXAlphaLAF::drawLabel(juce::Graphics& g, juce::Label& label) {
    g.setFont(getLabelFont(label));
    g.setColour(FXPal::textDim);
    g.drawText(label.getText(), label.getLocalBounds(),
        label.getJustificationType(), true);
}
void FXAlphaLAF::fillTextEditorBackground(juce::Graphics& g, int w, int h, juce::TextEditor&) {
    g.setColour(FXPal::bg);
    g.fillRect(0, 0, w, h);
}

void FXAlphaLAF::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    g.setColour(juce::Colour(0xFF1C2030));
    g.fillRoundedRectangle(0.f, 0.f, (float)w, (float)h, 6.0f);
    g.setColour(FXPal::blueDark.withAlpha(0.5f));
    g.drawRoundedRectangle(0.5f, 0.5f, (float)w - 1.f, (float)h - 1.f, 6.0f, 1.0f);
}

void FXAlphaLAF::drawPopupMenuItem(juce::Graphics& g,
    const juce::Rectangle<int>& area,
    bool /*isSeparator*/, bool isActive, bool isHighlighted,
    bool /*isTicked*/, bool /*hasSubMenu*/,
    const juce::String& text,
    const juce::String& /*shortcut*/,
    const juce::Drawable* /*icon*/,
    const juce::Colour* /*textColour*/)
{
    if (isHighlighted && isActive) {
        g.setColour(FXPal::blueDark.withAlpha(0.7f));
        g.fillRoundedRectangle(area.toFloat().reduced(2.f, 1.f), 3.f);
    }
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    g.setColour(isActive ? juce::Colour(0xFFCDD2DA) : juce::Colour(0xFF505A6A));
    g.drawText(text, area.reduced(10, 0), juce::Justification::centredLeft, true);
}

void FXAlphaLAF::drawButtonBackground(juce::Graphics& g,
    juce::Button& btn, const juce::Colour&,
    bool isHighlighted, bool isDown)
{
    auto r = btn.getLocalBounds().toFloat().reduced(1.0f);
    juce::ColourGradient body(
        isDown ? FXPal::blueDark : (isHighlighted ? FXPal::blue : juce::Colour(0xFF3A4050)),
        r.getX(), r.getY(),
        isDown ? FXPal::blue.darker(0.3f) : (isHighlighted ? FXPal::blueMid : juce::Colour(0xFF252A38)),
        r.getX(), r.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(isHighlighted || isDown ? FXPal::blue : FXPal::blueDark.withAlpha(0.8f));
    g.drawRoundedRectangle(r, 5.0f, isDown ? 2.0f : 1.3f);
}

void FXAlphaLAF::drawButtonText(juce::Graphics& g, juce::TextButton& btn, bool, bool) {
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
    g.setColour(FXPal::blueBright);
    g.drawText(btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred, true);
}

// ================================================================
//  FXLKnob
// ================================================================
FXLKnob::FXLKnob(const juce::String& lab) : labelText(lab) {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(slider);
}

void FXLKnob::resized() {
    slider.setBounds(getLocalBounds());
}

void FXLKnob::paint(juce::Graphics& g) {
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.setColour(FXPal::textDim);
    g.drawText(labelText,
        juce::Rectangle<int>(0, getHeight() - 13, getWidth(), 13),
        juce::Justification::centred, false);
}

// ================================================================
//  Constructor
// ================================================================
AlphaBetaFXAudioProcessorEditor::AlphaBetaFXAudioProcessorEditor(
    AlphaBetaFXAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&laf);

    // Total width = sum of all column widths
    int totalW = 0;
    for (int c : CW) totalW += c;
    int totalH = TITLE_H + NROWS * 95;
    setSize(totalW, totalH);

    // Add all knobs
    addAndMakeVisible(knobDrive);
    addAndMakeVisible(fCutoff);
    addAndMakeVisible(fRes);
    addAndMakeVisible(hWet);
    addAndMakeVisible(hTime);
    addAndMakeVisible(hRate);

    // Filter type combo
    fType.addItem("LP12",  1);
    fType.addItem("LP24",  2);
    fType.addItem("LP24+", 3);
    fType.addItem("BP",    4);
    fType.addItem("HP",    5);
    addAndMakeVisible(fType);

    // Res curve combo
    resType.addItem("Quadratic", 1);
    resType.addItem("Cubic",     2);
    addAndMakeVisible(resType);


    // Attachments
    auto& apvts = proc.apvts;
    att_drv  = std::make_unique<SA>(apvts, FXPID::DRV,  knobDrive.slider);
    att_fcut = std::make_unique<SA>(apvts, FXPID::FCUT, fCutoff.slider);
    att_fres = std::make_unique<SA>(apvts, FXPID::FRES, fRes.slider);
    att_hwet = std::make_unique<SA>(apvts, FXPID::HWET, hWet.slider);
    att_htim = std::make_unique<SA>(apvts, FXPID::HTIM, hTime.slider);
    att_hrat = std::make_unique<SA>(apvts, FXPID::HRAT, hRate.slider);
    att_ftyp = std::make_unique<CA>(apvts, FXPID::FTYP, fType);
    att_rtyp = std::make_unique<CA>(apvts, FXPID::RTYP, resType);
}

AlphaBetaFXAudioProcessorEditor::~AlphaBetaFXAudioProcessorEditor() {
    setLookAndFeel(nullptr);
}

// ================================================================
//  Layout helpers
// ================================================================
juce::Rectangle<int>
AlphaBetaFXAudioProcessorEditor::cell(int col, int row) const {
    int x = 0;
    for (int c = 0; c < col; ++c) x += CW[c];
    int rowH = (getHeight() - TITLE_H) / NROWS;
    return { x, TITLE_H + row * rowH, CW[col], rowH };
}

void AlphaBetaFXAudioProcessorEditor::placeCB(juce::ComboBox& cb, int col, int row) {
    auto r = cell(col, row);
    cb.setBounds(r.withSizeKeepingCentre(r.getWidth() - 8, 22));
}

// ================================================================
//  resized
// ================================================================
void AlphaBetaFXAudioProcessorEditor::resized() {
    // Row 0: knobs
    // Col 0 – Drive
    knobDrive.setBounds(cell(0, 0));

    // Col 2, 3 – Cutoff / Res  (col 1 is separator)
    fCutoff.setBounds(cell(2, 0));
    fRes   .setBounds(cell(3, 0));

    // Col 5, 6, 7 – Chorus  (col 4 is separator)
    hWet .setBounds(cell(5, 0));
    hTime.setBounds(cell(6, 0));
    hRate.setBounds(cell(7, 0));

    // Row 1: combos under filter columns
    placeCB(fType,   2, 1);
    placeCB(resType, 3, 1);
    // Chorus row 1 is intentionally empty
}

// ================================================================
//  paint
// ================================================================
void AlphaBetaFXAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    // Background
    g.setColour(FXPal::bg);
    g.fillRect(0, TITLE_H, W, H - TITLE_H);

    // Separator column positions
    int x1 = CW[0];                               // after Drive
    int x4 = CW[0] + CW[1] + CW[2] + CW[3];      // after Res

    g.setColour(FXPal::sepBg);
    g.fillRect(x1, TITLE_H, CW[1], H - TITLE_H);
    g.fillRect(x4, TITLE_H, CW[4], H - TITLE_H);

    // Blue accent lines on inner edges of separators
    g.setColour(FXPal::blueDark.withAlpha(0.25f));
    g.fillRect(x1,           TITLE_H, 1, H - TITLE_H);
    g.fillRect(x1 + CW[1]-1, TITLE_H, 1, H - TITLE_H);
    g.fillRect(x4,           TITLE_H, 1, H - TITLE_H);
    g.fillRect(x4 + CW[4]-1, TITLE_H, 1, H - TITLE_H);

    // Section labels (painted above knobs in row 0)
    const int rowH = (H - TITLE_H) / NROWS;
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
    g.setColour(FXPal::textBright.withAlpha(0.55f));

    // "DRIVE" label centred over col 0
    g.drawText("DRIVE",
        juce::Rectangle<int>(0, TITLE_H + 2, CW[0], 12),
        juce::Justification::centred, false);

    // "FILTER" label centred over cols 2+3
    g.drawText("FILTER",
        juce::Rectangle<int>(x1 + CW[1], TITLE_H + 2, CW[2] + CW[3], 12),
        juce::Justification::centred, false);

    // "CHORUS" label centred over cols 5+6+7
    g.drawText("CHORUS",
        juce::Rectangle<int>(x4 + CW[4], TITLE_H + 2, CW[5] + CW[6] + CW[7], 12),
        juce::Justification::centred, false);

    // Title bar
    g.setColour(FXPal::titleBg);
    g.fillRect(0, 0, W, TITLE_H);

    // Title accent line
    juce::ColourGradient titleLine(
        FXPal::blue.withAlpha(0.9f),       0.f, (float)TITLE_H,
        FXPal::blueBright.withAlpha(0.5f), (float)W, (float)TITLE_H, false);
    g.setGradientFill(titleLine);
    g.fillRect(0, TITLE_H - 2, W, 2);

    // Plugin name
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 15.0f, juce::Font::bold));
    g.setColour(FXPal::blue);
    g.drawText("ALPHA", juce::Rectangle<int>(10, 0, 72, TITLE_H), juce::Justification::centredLeft);
    g.setColour(FXPal::blueBright);
    g.drawText("BETA", juce::Rectangle<int>(57, 0, 55, TITLE_H), juce::Justification::centredLeft);
    g.setColour(FXPal::blue.withAlpha(0.7f));
    g.drawText("FX", juce::Rectangle<int>(101, 0, 32, TITLE_H), juce::Justification::centredLeft);

    // Subtitle
    g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 8.5f, juce::Font::plain));
    g.setColour(FXPal::textBright.withAlpha(0.6f));
    g.drawText("Stereo Drive + Filter + Chorus",
        juce::Rectangle<int>(145, 0, 280, TITLE_H), juce::Justification::centredLeft);
}
