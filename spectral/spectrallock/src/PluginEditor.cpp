#include "PluginEditor.h"

static juce::Font prysmFont(float height, bool bold = false)
{
#if JUCE_MAJOR_VERSION >= 8
    return juce::Font(juce::FontOptions(height,
        bold ? juce::Font::bold : juce::Font::plain));
#else
    return juce::Font(height, bold ? juce::Font::bold : juce::Font::plain);
#endif
}

static void setParamValue(juce::AudioProcessorValueTreeState& s,
    const juce::String& id, float value)
{
    if (auto* p = s.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

const juce::Colour SpectralLockLookAndFeel::bg{ 0xff2b2119 };
const juce::Colour SpectralLockLookAndFeel::panel{ 0xff3c2f24 };
const juce::Colour SpectralLockLookAndFeel::line{ 0xff54432f };
const juce::Colour SpectralLockLookAndFeel::text{ 0xfff1e6d8 };
const juce::Colour SpectralLockLookAndFeel::dim{ 0xffab9682 };
const juce::Colour SpectralLockLookAndFeel::accent{ 0xff7dd3fc };

juce::Colour SpectralLockLookAndFeel::prism(float t)
{
    // a light-through-glass ramp: violet -> blue -> green -> amber -> red
    t = juce::jlimit(0.0f, 1.0f, t);
    return juce::Colour::fromHSV(0.75f - 0.75f * t, 0.62f, 0.95f, 1.0f);
}

SpectralLockLookAndFeel::SpectralLockLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, dim);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.25f));
    setColour(juce::TextButton::textColourOffId, dim);
    setColour(juce::TextButton::textColourOnId, bg);
}

juce::Font SpectralLockLookAndFeel::getLabelFont(juce::Label&) { return prysmFont(15.0f); }

void SpectralLockLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle,
    juce::Slider& s)
{
    auto area = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6.0f);
    auto radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    auto centre = area.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
        startAngle, endAngle, true);
    g.setColour(line);
    g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // value arc - bipolar sliders fill outwards from the centre
    const bool bipolar = s.getMinimum() < -0.001 && s.getMaximum() > 0.001;
    const float from = bipolar ? (startAngle + 0.5f * (endAngle - startAngle)) : startAngle;

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
        juce::jmin(from, angle), juce::jmax(from, angle), true);
    g.setColour(prism(sliderPos));
    g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    // pointer
    juce::Point<float> tip(centre.x + std::sin(angle) * (radius - 2.0f),
        centre.y - std::cos(angle) * (radius - 2.0f));
    juce::Point<float> tail(centre.x + std::sin(angle) * (radius * 0.42f),
        centre.y - std::cos(angle) * (radius * 0.42f));
    g.setColour(text);
    g.drawLine({ tail, tip }, 2.0f);

    g.setColour(panel);
    g.fillEllipse(juce::Rectangle<float>(radius * 0.55f, radius * 0.55f).withCentre(centre));
}

void SpectralLockLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
    float sliderPos, float minPos, float maxPos,
    juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::TwoValueHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
        return;
    }

    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
    const float cy = r.getCentreY();

    g.setColour(panel);
    g.fillRoundedRectangle(r.getX(), cy - 5.0f, r.getWidth(), 10.0f, 5.0f);

    juce::ColourGradient grad(prism(0.15f), minPos, cy, prism(0.9f), maxPos, cy, false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(minPos, cy - 5.0f, juce::jmax(2.0f, maxPos - minPos), 10.0f, 5.0f);

    g.setColour(text);
    for (float hx : { minPos, maxPos })
        g.fillRoundedRectangle(hx - 3.0f, cy - 11.0f, 6.0f, 22.0f, 3.0f);
}

void SpectralLockLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
    const juce::Colour& backgroundColour,
    bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();

    g.setColour(on ? backgroundColour
        : panel.brighter(highlighted || down ? 0.25f : 0.0f));
    g.fillRoundedRectangle(r, 5.0f);

    g.setColour(on ? backgroundColour.brighter(0.4f) : line);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

SpectralLockKnob::SpectralLockKnob(juce::AudioProcessorValueTreeState& state,
    const juce::String& paramID,
    const juce::String& caption)
    : captionText(caption)
{
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 20);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
        juce::MathConstants<float>::pi * 2.75f, true);
    slider.setDoubleClickReturnValue(true, 0.0);
    addAndMakeVisible(slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
        (state, paramID, slider);
}

void SpectralLockKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(20);
    slider.setBounds(r);
}

void SpectralLockKnob::paint(juce::Graphics& g)
{
    g.setColour(SpectralLockLookAndFeel::dim);
    g.setFont(prysmFont(13.0f, true));
    g.drawText(captionText, getLocalBounds().removeFromTop(20),
        juce::Justification::centred);
}

MatrixComponent::MatrixComponent(juce::AudioProcessorValueTreeState& s) : apvts(s)
{
    startTimerHz(24);
}

void MatrixComponent::timerCallback()
{
    bool changed = false;

    const int r = (int)*apvts.getRawParameterValue("root");
    if (r != root) { root = r; changed = true; }

    for (int i = 0; i < 12; ++i)
    {
        const int v = (int)*apvts.getRawParameterValue("matrix" + juce::String(i));
        if (v != current[(size_t)i]) { current[(size_t)i] = v; changed = true; }
    }

    if (changed) repaint();
}

juce::Rectangle<float> MatrixComponent::rootBounds(int col) const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft(30.0f);
    auto strip = r.removeFromTop((float)rootStripHeight);
    const float w = strip.getWidth() / 12.0f;
    return { strip.getX() + w * (float)col, strip.getY(), w, strip.getHeight() };
}

juce::Rectangle<float> MatrixComponent::cellBounds(int col, int rowFromBottom) const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft(30.0f);
    r.removeFromTop((float)(rootStripHeight + gap));
    r.removeFromBottom(20.0f);

    const float w = r.getWidth() / 12.0f;
    const float h = r.getHeight() / 12.0f;
    return { r.getX() + w * (float)col,
             r.getBottom() - h * (float)(rowFromBottom + 1), w, h };
}

void MatrixComponent::paint(juce::Graphics& g)
{
    using LNF = SpectralLockLookAndFeel;

    // root strip background
    auto strip = juce::Rectangle<float>(rootBounds(0).getX(), rootBounds(0).getY(),
        rootBounds(11).getRight() - rootBounds(0).getX(),
        (float)rootStripHeight);
    g.setColour(juce::Colour(0xff1c150f));
    g.fillRoundedRectangle(strip, 4.0f);

    for (int c = 0; c < 12; ++c)
    {
        auto rb = rootBounds(c).reduced(1.5f);
        if (c == root)
        {
            g.setColour(LNF::accent.withAlpha(0.9f));
            g.fillRoundedRectangle(rb, 3.0f);
        }
        g.setColour(c == root ? LNF::bg : LNF::dim);
        g.setFont(prysmFont(13.0f, c == root));
        g.drawText(SpectralLockAudioProcessor::noteNames[c], rb, juce::Justification::centred);
    }

    g.setColour(LNF::dim.withAlpha(0.7f));
    g.setFont(prysmFont(10.5f, true));
    g.drawText("ROOT", getLocalBounds().removeFromLeft(30).withHeight(rootStripHeight),
        juce::Justification::centredRight);

    // grid
    for (int c = 0; c < 12; ++c)
    {
        for (int r = 0; r < 12; ++r)
        {
            auto cb = cellBounds(c, r).reduced(1.5f);
            const bool isActive = (current[(size_t)c] == r);
            const bool isIdentity = (c == r);
            const bool isHover = (c == hoverCol && r == hoverRow);

            if (isActive)
            {
                g.setColour(LNF::prism((float)r / 11.0f));
                g.fillRoundedRectangle(cb, 3.0f);
                g.setColour(juce::Colours::white.withAlpha(0.55f));
                g.drawRoundedRectangle(cb, 3.0f, 1.0f);
            }
            else
            {
                g.setColour(isHover ? LNF::line.brighter(0.5f)
                    : LNF::panel.brighter(isIdentity ? 0.10f : 0.0f));
                g.fillRoundedRectangle(cb, 3.0f);

                if (isIdentity)
                {
                    g.setColour(LNF::line.brighter(0.6f));
                    g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre(cb.getCentre()));
                }
            }
        }

        // output note names down the left edge
        auto lb = cellBounds(0, c);
        g.setColour(LNF::dim);
        g.setFont(prysmFont(11.5f));
        g.drawText(SpectralLockAudioProcessor::noteNames[c],
            juce::Rectangle<float>(0.0f, lb.getY(), 28.0f, lb.getHeight()),
            juce::Justification::centredRight);

        // input note names along the bottom
        auto bb = cellBounds(c, 0);
        g.drawText(SpectralLockAudioProcessor::noteNames[c],
            juce::Rectangle<float>(bb.getX(), bb.getBottom(), bb.getWidth(), 20.0f),
            juce::Justification::centred);
    }

    g.setColour(LNF::dim.withAlpha(0.55f));
    g.setFont(prysmFont(10.5f, true));
    g.drawText("IN >", getLocalBounds().removeFromBottom(20).removeFromLeft(30),
        juce::Justification::centredRight);
}

void MatrixComponent::handleMouse(juce::Point<int> p)
{
    const auto pf = p.toFloat();

    for (int c = 0; c < 12; ++c)
        if (rootBounds(c).contains(pf))
        {
            setParamValue(apvts, "root", (float)c);
            return;
        }

    for (int c = 0; c < 12; ++c)
        for (int r = 0; r < 12; ++r)
            if (cellBounds(c, r).contains(pf))
            {
                hoverCol = c; hoverRow = r;
                setParamValue(apvts, "matrix" + juce::String(c), (float)r);
                repaint();
                return;
            }
}

void MatrixComponent::mouseDown(const juce::MouseEvent& e) { handleMouse(e.getPosition()); }
void MatrixComponent::mouseDrag(const juce::MouseEvent& e) { handleMouse(e.getPosition()); }

void MatrixComponent::applyScale(int scaleIndex)
{
    scaleIndex = juce::jlimit(0, SpectralLockAudioProcessor::numScales - 1, scaleIndex);
    for (int i = 0; i < 12; ++i)
        setParamValue(apvts, "matrix" + juce::String(i),
            (float)SpectralLockAudioProcessor::scaleTables[scaleIndex][i]);
}

RangeSelector::RangeSelector(juce::AudioProcessorValueTreeState& s) : apvts(s)
{
    range.setRange(60.0, 4800.0, 1.0);
    range.setSkewFactorFromMidPoint(600.0);
    range.setMinAndMaxValues(*apvts.getRawParameterValue("rangelo"),
        *apvts.getRawParameterValue("rangehi"),
        juce::dontSendNotification);
    range.onValueChange = [this] { pushValues(); };
    addAndMakeVisible(range);

    for (auto* b : { &muteLow, &muteHigh })
    {
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffef6f6f));
        addAndMakeVisible(*b);
    }

    aLow = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "mutelow", muteLow);
    aHigh = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, "mutehigh", muteHigh);

    startTimerHz(12);
}

void RangeSelector::pushValues()
{
    setParamValue(apvts, "rangelo", (float)range.getMinValue());
    setParamValue(apvts, "rangehi", (float)range.getMaxValue());
    repaint();
}

void RangeSelector::timerCallback()
{
    if (range.isMouseButtonDown()) return;

    const float lo = *apvts.getRawParameterValue("rangelo");
    const float hi = *apvts.getRawParameterValue("rangehi");

    if (std::abs((float)range.getMinValue() - lo) > 0.5f
        || std::abs((float)range.getMaxValue() - hi) > 0.5f)
    {
        range.setMinAndMaxValues(lo, hi, juce::dontSendNotification);
        repaint();
    }
}

juce::String RangeSelector::hzToNote(float hz)
{
    const int n = juce::roundToInt(69.0f + 12.0f * std::log2(juce::jmax(1.0f, hz) / 440.0f));
    return juce::String(SpectralLockAudioProcessor::noteNames[((n % 12) + 12) % 12])
        + juce::String(n / 12 - 1);
}

void RangeSelector::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop(20);
    muteLow.setBounds(r.removeFromLeft(46).reduced(2, 8));
    muteHigh.setBounds(r.removeFromRight(46).reduced(2, 8));
    r.removeFromLeft(52);
    r.removeFromRight(52);
    range.setBounds(r);
}

void RangeSelector::paint(juce::Graphics& g)
{
    using LNF = SpectralLockLookAndFeel;

    g.setColour(LNF::dim);
    g.setFont(prysmFont(13.0f, true));
    g.drawText("RANGE", getLocalBounds().removeFromTop(20), juce::Justification::centredLeft);

    const float octaves = std::log2((float)range.getMaxValue()
        / juce::jmax(1.0f, (float)range.getMinValue()));
    g.setColour(LNF::dim);
    g.setFont(prysmFont(11.5f));
    g.drawText(juce::String(octaves, 2) + " OCT",
        getLocalBounds().removeFromTop(20), juce::Justification::centredRight);

    auto r = getLocalBounds();
    r.removeFromTop(20);
    auto loArea = r.removeFromLeft(46 + 52).removeFromRight(52);
    auto hiArea = r.removeFromRight(46 + 52).removeFromLeft(52);

    g.setColour(LNF::text);
    g.setFont(prysmFont(14.5f, true));
    g.drawText(hzToNote((float)range.getMinValue()), loArea, juce::Justification::centred);
    g.drawText(hzToNote((float)range.getMaxValue()), hiArea, juce::Justification::centred);
}

BandView::BandView(SpectralLockAudioProcessor& p) : proc(p)
{
    smoothed.fill(0.0f);
    startTimerHz(30);
}

void BandView::timerCallback()
{
    for (size_t i = 0; i < smoothed.size(); ++i)
    {
        const float v = proc.display[i].load();
        smoothed[i] = juce::jmax(v, smoothed[i] * 0.82f);
    }
    repaint();
}

void BandView::paint(juce::Graphics& g)
{
    using LNF = SpectralLockLookAndFeel;
    auto r = getLocalBounds().toFloat();

    g.setColour(LNF::panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(LNF::line);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);

    r = r.reduced(6.0f);
    const int   n = (int)smoothed.size();
    const float w = r.getWidth() / (float)n;

    for (int i = 0; i < n; ++i)
    {
        const float v = juce::jlimit(0.0f, 1.0f, smoothed[(size_t)i]);
        if (v < 0.002f) continue;

        const float h = v * r.getHeight();
        auto bar = juce::Rectangle<float>(r.getX() + w * (float)i,
            r.getBottom() - h,
            juce::jmax(1.0f, w - 1.0f), h);
        g.setColour(LNF::prism((float)i / (float)(n - 1)).withAlpha(0.25f + 0.75f * v));
        g.fillRect(bar);
    }

    g.setColour(LNF::dim.withAlpha(0.6f));
    g.setFont(prysmFont(11.0f, true));
    g.drawText("OSCILLATOR BANK", getLocalBounds().reduced(10, 6),
        juce::Justification::topLeft);
}

SpectralLockAudioProcessorEditor::SpectralLockAudioProcessorEditor(SpectralLockAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
    bandView(p), matrix(p.apvts), rangeSel(p.apvts)
{
    setLookAndFeel(&lnf);

    addAndMakeVisible(bandView);
    addAndMakeVisible(matrix);
    addAndMakeVisible(rangeSel);

    scaleBox.setTextWhenNothingSelected("SCALE PRESET");
    for (int i = 0; i < SpectralLockAudioProcessor::numScales; ++i)
        scaleBox.addItem(SpectralLockAudioProcessor::scaleNames[i], i + 1);
    scaleBox.onChange = [this]
        {
            const int idx = scaleBox.getSelectedId() - 1;
            if (idx >= 0) matrix.applyScale(idx);
        };
    addAndMakeVisible(scaleBox);

    for (auto* b : { &midiButton, &freezeButton })
    {
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonOnColourId, SpectralLockLookAndFeel::accent);
        addAndMakeVisible(*b);
    }
    aMidi = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "midi", midiButton);
    aFreeze = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "freeze", freezeButton);

    addKnob("amount", "AMOUNT");
    addKnob("isolate", "ISOLATE");
    addKnob("pitch", "PITCH");
    addKnob("width", "WIDTH");

    addKnob("glide", "GLIDE");
    addKnob("bend", "BEND");
    addKnob("bendrange", "+ / -");
    addKnob("tilt", "TILT");

    addKnob("shimmer", "SHIMMER");
    addKnob("spray", "SPRAY");
    addKnob("level", "LEVEL");
    addKnob("mix", "MIX");

    startTimerHz(15);
    setSize(980, 726);
}

SpectralLockAudioProcessorEditor::~SpectralLockAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

SpectralLockKnob* SpectralLockAudioProcessorEditor::addKnob(const juce::String& id, const juce::String& caption)
{
    auto* k = knobs.add(new SpectralLockKnob(proc.apvts, id, caption));
    addAndMakeVisible(k);
    return k;
}

void SpectralLockAudioProcessorEditor::timerCallback()
{
    const bool active = proc.midiIsActive.load();
    const auto c = active ? juce::Colour(0xff8ef2a4) : SpectralLockLookAndFeel::accent;
    if (midiButton.findColour(juce::TextButton::buttonOnColourId) != c)
    {
        midiButton.setColour(juce::TextButton::buttonOnColourId, c);
        midiButton.repaint();
    }
}

void SpectralLockAudioProcessorEditor::paint(juce::Graphics& g)
{
    using LNF = SpectralLockLookAndFeel;

    g.fillAll(LNF::bg);

    // header
    auto header = getLocalBounds().removeFromTop(68).reduced(20, 8);

    g.setColour(LNF::text);
    g.setFont(prysmFont(30.0f, true));
    g.drawText("SpectralLock", header.removeFromLeft(230), juce::Justification::centredLeft);

    g.setColour(LNF::dim);
    g.setFont(prysmFont(15.5f));
    g.drawText("polyphonic filterbank resynthesis",
        header.removeFromLeft(330), juce::Justification::centredLeft);

    // thin spectrum rule under the title
    auto rule = juce::Rectangle<float>(20.0f, 60.0f, 190.0f, 2.5f);
    for (int i = 0; i < 95; ++i)
    {
        g.setColour(LNF::prism((float)i / 94.0f));
        g.fillRect(rule.getX() + (float)i * 2.0f, rule.getY(), 2.0f, 2.5f);
    }

    g.setColour(LNF::dim.withAlpha(0.7f));
    g.setFont(prysmFont(13.5f));
    g.drawText("matrix: columns = incoming note, rows = outgoing note",
        20, 696, 540, 20, juce::Justification::centredLeft);
    g.drawText("isolate gates each oscillator - and, above zero, the whole wet path",
        340, 696, 620, 20, juce::Justification::centredRight);
}

void SpectralLockAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();

    auto header = r.removeFromTop(68).reduced(20, 16);
    freezeButton.setBounds(header.removeFromRight(76));
    header.removeFromRight(8);
    midiButton.setBounds(header.removeFromRight(66));

    bandView.setBounds(r.removeFromTop(74).reduced(20, 2));

    auto body = r.reduced(20, 10);
    auto left = body.removeFromLeft(420);
    body.removeFromLeft(20);

    scaleBox.setBounds(left.removeFromBottom(46).removeFromTop(32));
    matrix.setBounds(left.reduced(0, 4));

    rangeSel.setBounds(body.removeFromTop(84));
    body.removeFromTop(14);

    const int rows = 3, cols = 4;
    const int kw = body.getWidth() / cols;
    const int kh = juce::jmin(120, body.getHeight() / rows);

    for (int i = 0; i < knobs.size(); ++i)
    {
        const int col = i % cols, row = i / cols;
        knobs[i]->setBounds(body.getX() + col * kw,
            body.getY() + row * kh,
            kw, kh);
    }
}