/*
  ==============================================================================

    PluginEditor.cpp
    IRForge — interface implementation.

  ==============================================================================
*/

#include "PluginEditor.h"

using namespace irforge;

//==============================================================================
// Look and feel
//==============================================================================
ForgeLookAndFeel::ForgeLookAndFeel()
{
    setColour(juce::Label::textColourId, Palette::text());
    setColour(juce::Slider::rotarySliderFillColourId, Palette::hot());
    setColour(juce::TextButton::buttonColourId, juce::Colours::white.withAlpha(0.09f));
    setColour(juce::TextButton::textColourOffId, Palette::text());
    setColour(juce::TextButton::textColourOnId, Palette::text());
    setColour(juce::PopupMenu::backgroundColourId, Palette::bgLift());
    setColour(juce::PopupMenu::textColourId, Palette::text());
}

juce::Font ForgeLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(13.5f));
}

void ForgeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float pos, float startAngle, float endAngle,
    juce::Slider& s)
{
    auto rawBounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(3.0f);
    // Force a square bounds so the knob backdrop is always a circle, never
    // an oval, regardless of the rectangular slot it's laid out in.
    const float side = juce::jmin(rawBounds.getWidth(), rawBounds.getHeight());
    auto bounds = juce::Rectangle<float>(side, side).withCentre(rawBounds.getCentre());
    const float radius = side * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thickness = juce::jmax(2.5f, radius * 0.13f);
    auto accent = s.findColour(juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc(c.x, c.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
        0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colours::black.withAlpha(0.42f));
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(c.x, c.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
        0.0f, startAngle, angle, true);
    g.setColour(accent);
    g.strokePath(value, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    auto cap = bounds.reduced(thickness * 1.9f);
    juce::ColourGradient grad(juce::Colours::white.withAlpha(0.13f), cap.getX(), cap.getY(),
        juce::Colours::black.withAlpha(0.38f), cap.getX(), cap.getBottom(), false);
    g.setGradientFill(grad);
    g.fillEllipse(cap);
    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.drawEllipse(cap.reduced(0.5f), 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle(-1.3f, -radius * 0.70f, 2.6f, radius * 0.36f, 1.3f);
    g.setColour(accent.brighter(0.6f));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(c));
}

void ForgeLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
    bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto pill = r.removeFromLeft(30.0f).withSizeKeepingCentre(26.0f, 14.0f);

    g.setColour(b.getToggleState() ? Palette::hot().withAlpha(0.85f)
        : juce::Colours::white.withAlpha(0.12f));
    g.fillRoundedRectangle(pill, 7.0f);
    g.setColour(juce::Colours::white.withAlpha(highlighted ? 0.40f : 0.20f));
    g.drawRoundedRectangle(pill, 7.0f, 1.0f);

    auto dot = juce::Rectangle<float>(11.0f, 11.0f)
        .withCentre({ b.getToggleState() ? pill.getRight() - 7.5f : pill.getX() + 7.5f,
                       pill.getCentreY() });
    g.setColour(juce::Colours::white.withAlpha(0.94f));
    g.fillEllipse(dot);

    g.setColour(Palette::textDim());
    g.setFont(juce::Font(juce::FontOptions(12.5f)));
    g.drawText(b.getButtonText(), r.translated(5.0f, 0.0f),
        juce::Justification::centredLeft, false);
}

void ForgeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
    const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.20f : (highlighted ? 0.14f : 0.09f)));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(Palette::edge());
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
}

//==============================================================================
// ForgeKnob
//==============================================================================
ForgeKnob::ForgeKnob(juce::String caption, bool large)
    : text(std::move(caption)), isLarge(large)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
        juce::MathConstants<float>::pi * 2.75f, true);
    slider.setPopupDisplayEnabled(true, true, nullptr);
    addAndMakeVisible(slider);
}

void ForgeKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom(isLarge ? 18 : 13);
    slider.setBounds(r);
}

void ForgeKnob::paint(juce::Graphics& g)
{
    g.setColour(isLarge ? Palette::text() : Palette::textDim());
    g.setFont(juce::Font(juce::FontOptions(isLarge ? 14.5f : 12.0f))
        .withExtraKerningFactor(isLarge ? 0.18f : 0.04f));
    g.drawText(text, getLocalBounds().removeFromBottom(isLarge ? 18 : 13),
        juce::Justification::centred, false);
}

//==============================================================================
// SourceDisplay
//==============================================================================
SourceDisplay::SourceDisplay(IRForgeAudioProcessor& p) : processor(p)
{
    startTimerHz(12);
}

void SourceDisplay::timerCallback()
{
    const juce::ScopedLock sl(processor.sourceLock);
    const int n = processor.getSourceBuffer().getNumSamples();
    if (n != lastSourceSamples)
    {
        lastSourceSamples = n;
        rebuildPeaks();
        repaint();
    }
}

void SourceDisplay::rebuildPeaks()
{
    // caller holds sourceLock
    const auto& buf = processor.getSourceBuffer();
    const int n = buf.getNumSamples();
    const int cols = juce::jmax(1, getWidth() > 0 ? getWidth() : 800);

    peaksMin.assign((size_t)cols, 0.0f);
    peaksMax.assign((size_t)cols, 0.0f);
    if (n < 2) return;

    for (int c = 0; c < cols; ++c)
    {
        const int a = (int)((juce::int64)c * n / cols);
        const int b = juce::jmax(a + 1, (int)((juce::int64)(c + 1) * n / cols));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            const auto* d = buf.getReadPointer(ch);
            for (int i = a; i < juce::jmin(b, n); ++i)
            {
                lo = juce::jmin(lo, d[i]);
                hi = juce::jmax(hi, d[i]);
            }
        }
        peaksMin[(size_t)c] = lo;
        peaksMax[(size_t)c] = hi;
    }
}

float SourceDisplay::xToNorm(float x) const
{
    return juce::jlimit(0.0f, 1.0f, (x - 4.0f) / juce::jmax(1.0f, (float)getWidth() - 8.0f));
}

void SourceDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(Palette::panel());
    g.fillRoundedRectangle(r, 8.0f);

    const bool hasSrc = !peaksMax.empty() && lastSourceSamples > 1;

    if (!hasSrc)
    {
        g.setColour(dragOver ? Palette::hot() : Palette::textDim());
        g.setFont(juce::Font(juce::FontOptions(15.0f)));
        g.drawText(dragOver ? "Drop it" : "Drop a sample here, or press Load",
            getLocalBounds(), juce::Justification::centred, false);
        g.setColour(Palette::edge());
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, dragOver ? 2.0f : 1.0f);
        return;
    }

    auto inner = r.reduced(4.0f);
    const float mid = inner.getCentreY();
    const float halfH = inner.getHeight() * 0.46f;

    const float cs = processor.apvts.getRawParameterValue(pid::cropStart)->load();
    const float ce = processor.apvts.getRawParameterValue(pid::cropEnd)->load();
    const float xs = inner.getX() + cs * inner.getWidth();
    const float xe = inner.getX() + ce * inner.getWidth();

    // waveform, dim outside the crop region and lit inside it
    const int cols = (int)peaksMax.size();
    for (int c = 0; c < cols; ++c)
    {
        const float x = inner.getX() + inner.getWidth() * (float)c / (float)cols;
        const bool inCrop = (x >= xs && x <= xe);
        g.setColour(inCrop ? Palette::cool().withAlpha(0.92f)
            : Palette::source().withAlpha(0.30f));
        const float y0 = mid - peaksMax[(size_t)c] * halfH;
        const float y1 = mid - peaksMin[(size_t)c] * halfH;
        g.drawLine(x, juce::jmin(y0, y1), x, juce::jmax(y0, y1) + 0.6f, 1.0f);
    }

    // crop shading and handles
    g.setColour(juce::Colours::black.withAlpha(0.42f));
    g.fillRect(juce::Rectangle<float>(inner.getX(), inner.getY(),
        xs - inner.getX(), inner.getHeight()));
    g.fillRect(juce::Rectangle<float>(xe, inner.getY(),
        inner.getRight() - xe, inner.getHeight()));

    g.setColour(Palette::hot());
    g.drawLine(xs, inner.getY(), xs, inner.getBottom(), 2.0f);
    g.drawLine(xe, inner.getY(), xe, inner.getBottom(), 2.0f);
    for (float hx : { xs, xe })
        g.fillRoundedRectangle(hx - 3.0f, inner.getY(), 6.0f, 9.0f, 2.0f);

    g.setColour(Palette::textDim());
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("SOURCE  -  drag to crop, click twice to reset",
        getLocalBounds().reduced(9, 5), juce::Justification::topLeft, false);

    g.setColour(Palette::edge());
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, dragOver ? 2.0f : 1.0f);
}

void SourceDisplay::mouseDown(const juce::MouseEvent& e)
{
    const float n = xToNorm((float)e.x);
    const float cs = processor.apvts.getRawParameterValue(pid::cropStart)->load();
    const float ce = processor.apvts.getRawParameterValue(pid::cropEnd)->load();
    draggingStart = std::abs(n - cs) < std::abs(n - ce);
    draggingEnd = !draggingStart;
    mouseDrag(e);
}

void SourceDisplay::mouseDrag(const juce::MouseEvent& e)
{
    const float n = xToNorm((float)e.x);
    auto set = [this](const juce::String& id, float v)
        {
            if (auto* p = processor.apvts.getParameter(id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v));
                p->endChangeGesture();
            }
        };
    if (draggingStart)
        set(pid::cropStart, juce::jmin(n, processor.apvts.getRawParameterValue(pid::cropEnd)->load() - 0.002f));
    else if (draggingEnd)
        set(pid::cropEnd, juce::jmax(n, processor.apvts.getRawParameterValue(pid::cropStart)->load() + 0.002f));
    repaint();
}

void SourceDisplay::mouseDoubleClick(const juce::MouseEvent&)
{
    for (auto pair : { std::make_pair(pid::cropStart, 0.0f), std::make_pair(pid::cropEnd, 1.0f) })
        if (auto* p = processor.apvts.getParameter(pair.first))
            p->setValueNotifyingHost(pair.second);
    repaint();
}

bool SourceDisplay::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif")
            || f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac")
            || f.endsWithIgnoreCase(".mp3") || f.endsWithIgnoreCase(".ogg"))
            return true;
    return false;
}

void SourceDisplay::fileDragEnter(const juce::StringArray&, int, int) { dragOver = true;  repaint(); }
void SourceDisplay::fileDragExit(const juce::StringArray&) { dragOver = false; repaint(); }

void SourceDisplay::filesDropped(const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (!files.isEmpty() && onFileDropped)
        onFileDropped(juce::File(files[0]));
    repaint();
}

//==============================================================================
// IRDisplay
//==============================================================================
IRDisplay::IRDisplay(IRForgeAudioProcessor& p) : processor(p)
{
    startTimerHz(10);
}

void IRDisplay::timerCallback()
{
    // Sample count alone isn't a reliable "changed" signal: Character, FFT
    // size, and Decay can all rebuild the IR without changing its length.
    // irGeneration is bumped on every rebuild, so use that instead.
    const int gen = processor.irGeneration.load(std::memory_order_relaxed);
    if (gen != lastGeneration || dirty)
    {
        lastGeneration = gen;
        dirty = false;

        const juce::ScopedLock sl(processor.irLock);
        lastLen = processor.getDisplayIR().getNumSamples();
        recompute();
        repaint();
    }
}

void IRDisplay::recompute()
{
    // caller holds irLock
    const auto& ir = processor.getDisplayIR();
    const int n = ir.getNumSamples();
    magDb.clear();
    impulse.clear();
    if (n < 8) return;

    // impulse envelope for the strip along the bottom
    const int cols = juce::jmax(1, getWidth() > 0 ? getWidth() : 800);
    impulse.assign((size_t)cols, 0.0f);
    const auto* d = ir.getReadPointer(0);
    for (int c = 0; c < cols; ++c)
    {
        const int a = (int)((juce::int64)c * n / cols);
        const int b = juce::jmax(a + 1, (int)((juce::int64)(c + 1) * n / cols));
        float hi = 0.0f;
        for (int i = a; i < juce::jmin(b, n); ++i) hi = juce::jmax(hi, std::abs(d[i]));
        impulse[(size_t)c] = hi;
    }

    // magnitude spectrum
    int order = 13;
    while ((1 << order) < juce::jmin(n, 1 << 15) && order < 15) ++order;
    const int fftSize = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<juce::dsp::Complex<float>> in((size_t)fftSize), out((size_t)fftSize);
    for (int i = 0; i < fftSize; ++i)
        in[(size_t)i] = { i < n ? d[i] : 0.0f, 0.0f };
    fft.perform(in.data(), out.data(), false);

    magDb.assign((size_t)(fftSize / 2), -120.0f);
    for (int i = 0; i < fftSize / 2; ++i)
        magDb[(size_t)i] = juce::jlimit(-90.0f, 60.0f,
            20.0f * std::log10(std::abs(out[(size_t)i]) + 1.0e-9f));
}

void IRDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(Palette::panel());
    g.fillRoundedRectangle(r, 8.0f);

    if (magDb.empty())
    {
        g.setColour(Palette::textDim());
        g.setFont(juce::Font(juce::FontOptions(14.0f)));
        g.drawText(processor.hasSource() ? "forging..." : "no impulse response yet",
            getLocalBounds(), juce::Justification::centred, false);
        g.setColour(Palette::edge());
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
        return;
    }

    auto inner = r.reduced(5.0f);
    auto strip = inner.removeFromBottom(inner.getHeight() * 0.22f);
    inner.removeFromBottom(3.0f);

    // --- log-frequency grid ----------------------------------------------
    const double sr = 44100.0;
    const float nyq = (float)(sr * 0.5);
    auto freqToX = [&](float f)
        {
            const float a = std::log10(juce::jmax(20.0f, f) / 20.0f);
            const float b = std::log10(nyq / 20.0f);
            return inner.getX() + inner.getWidth() * a / b;
        };

    g.setColour(juce::Colours::white.withAlpha(0.07f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawVerticalLine((int)freqToX(f), inner.getY(), inner.getBottom());

    // --- magnitude curve --------------------------------------------------
    // Range covers the full clamped dB span from recompute() (-90..60) so the
    // curve can never draw above/below the panel's own box. The upper bound
    // has headroom above 0 dB because a peaky harmonic comb (high CHARACTER)
    // can concentrate energy into a few FFT bins well past +30 dB.
    const int bins = (int)magDb.size();
    juce::Path p;
    bool started = false;
    for (int x = 0; x < (int)inner.getWidth(); ++x)
    {
        const float frac = (float)x / juce::jmax(1.0f, inner.getWidth());
        const float f = 20.0f * std::pow(nyq / 20.0f, frac);
        const int bin = juce::jlimit(0, bins - 1, (int)(f / nyq * (float)bins));
        const float db = magDb[(size_t)bin];
        const float y = juce::jlimit(inner.getY(), inner.getBottom(),
            juce::jmap(db, -90.0f, 60.0f, inner.getBottom(), inner.getY()));
        const float px = inner.getX() + (float)x;
        if (!started) { p.startNewSubPath(px, y); started = true; }
        else p.lineTo(px, y);
    }

    auto fill = p;
    fill.lineTo(inner.getRight(), inner.getBottom());
    fill.lineTo(inner.getX(), inner.getBottom());
    fill.closeSubPath();
    g.setColour(Palette::hot().withAlpha(0.13f));
    g.fillPath(fill);
    g.setColour(Palette::hot());
    g.strokePath(p, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved));

    // --- impulse strip ----------------------------------------------------
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRoundedRectangle(strip, 4.0f);
    const int cols = (int)impulse.size();
    for (int c = 0; c < cols && c < (int)strip.getWidth(); ++c)
    {
        const float x = strip.getX() + strip.getWidth() * (float)c / (float)cols;
        const float h = impulse[(size_t)c] * strip.getHeight() * 0.92f;
        g.setColour(Palette::cool().withAlpha(0.80f));
        g.drawLine(x, strip.getBottom() - h, x, strip.getBottom(), 1.0f);
    }

    g.setColour(Palette::textDim());
    g.setFont(juce::Font(juce::FontOptions(11.5f)));
    g.drawText("IMPULSE RESPONSE  -  spectrum and envelope",
        getLocalBounds().reduced(9, 5), juce::Justification::topLeft, false);

    if (processor.building.load())
    {
        g.setColour(Palette::hot().withAlpha(0.85f));
        g.drawText("forging", getLocalBounds().reduced(9, 5),
            juce::Justification::topRight, false);
    }

    g.setColour(Palette::edge());
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
}

//==============================================================================
// Editor
//==============================================================================
IRForgeAudioProcessorEditor::IRForgeAudioProcessorEditor(IRForgeAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lnf);

    sourceDisplay = std::make_unique<SourceDisplay>(processor);
    sourceDisplay->onFileDropped = [this](const juce::File& f)
        {
            if (processor.loadSourceFile(f))
                sourceLabel.setText(processor.getSourceName(), juce::dontSendNotification);
        };
    addAndMakeVisible(*sourceDisplay);

    irDisplay = std::make_unique<IRDisplay>(processor);
    addAndMakeVisible(*irDisplay);

    addAndMakeVisible(loadButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(clearButton);
    addAndMakeVisible(recordButton);
    loadButton.onClick = [this] { openLoadDialog(); };
    saveButton.onClick = [this] { openSaveDialog(); };
    recordButton.onClick = [this] { toggleRecording(); };
    updateRecordButtonAppearance();
    clearButton.onClick = [this]
        {
            processor.clearSource();
            sourceLabel.setText("no sample", juce::dontSendNotification);
        };

    titleLabel.setText("IRFORGE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)).withExtraKerningFactor(0.30f));
    titleLabel.setColour(juce::Label::textColourId, Palette::text());
    addAndMakeVisible(titleLabel);

    sourceLabel.setText(processor.hasSource() ? processor.getSourceName() : "no sample",
        juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId, Palette::textDim());
    sourceLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(sourceLabel);

    statusLabel.setColour(juce::Label::textColourId, Palette::textDim());
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setText("",
        juce::dontSendNotification);
    addAndMakeVisible(statusLabel);

    attach(characterKnob, pid::character, Palette::hot());
    attach(fftKnob, pid::fftOrder, Palette::hot());
    attach(lengthKnob, pid::irLength, Palette::hot());
    attach(decayKnob, pid::decayShape, Palette::hot());
    attach(stretchKnob, pid::stretch, Palette::cool());
    attach(predelayKnob, pid::predelay, Palette::cool());
    attach(lowCutKnob, pid::lowCut, Palette::cool());
    attach(highCutKnob, pid::highCut, Palette::cool());
    attach(tiltKnob, pid::tilt, Palette::cool());
    attach(widthKnob, pid::width, Palette::cool());
    attach(mixKnob, pid::mix, Palette::outputGreen());
    attach(gainKnob, pid::gain, Palette::outputGreen());

    attach(linearPhaseButton, pid::linearPhase);
    attach(reverseButton, pid::reverse);

    setResizable(true, true);
    setResizeLimits(860, 620, 1900, 1300);
    setSize(1000, 700);
}

IRForgeAudioProcessorEditor::~IRForgeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void IRForgeAudioProcessorEditor::attach(ForgeKnob& k, const juce::String& id, juce::Colour accent)
{
    k.setAccent(accent);
    addAndMakeVisible(k);
    sliderAttachments.push_back(std::make_unique<SA>(processor.apvts, id, k.slider));
}

void IRForgeAudioProcessorEditor::attach(juce::ToggleButton& b, const juce::String& id)
{
    addAndMakeVisible(b);
    buttonAttachments.push_back(std::make_unique<BA>(processor.apvts, id, b));
}

void IRForgeAudioProcessorEditor::openLoadDialog()
{
    chooser = std::make_unique<juce::FileChooser>("Load a sample to forge",
        juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f.existsAsFile() && processor.loadSourceFile(f))
                sourceLabel.setText(processor.getSourceName(), juce::dontSendNotification);
        });
}

void IRForgeAudioProcessorEditor::openSaveDialog()
{
    chooser = std::make_unique<juce::FileChooser>("Save impulse response",
        juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory)
        .getChildFile(processor.getSourceName() + "_IR.wav"),
        "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f != juce::File{})
                processor.saveIRToFile(f.withFileExtension(".wav"));
        });
}

void IRForgeAudioProcessorEditor::updateRecordButtonAppearance()
{
    const bool rec = processor.isRecordingNow();
    recordButton.setButtonText(rec ? "Stop" : "Record");
    recordButton.setColour(juce::TextButton::buttonColourId,
        rec ? Palette::hot().withAlpha(0.85f) : juce::Colours::white.withAlpha(0.09f));
}

void IRForgeAudioProcessorEditor::toggleRecording()
{
    if (!processor.isRecordingNow())
    {
        processor.startRecording();
        updateRecordButtonAppearance();
        return;
    }

    // stop capturing immediately — the file dialog can take a while and the
    // take shouldn't keep growing while the user picks a destination
    processor.stopRecording();
    updateRecordButtonAppearance();

    chooser = std::make_unique<juce::FileChooser>("Save recording",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("IRForge Recording.wav"),
        "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            processor.finalizeRecording(f == juce::File{} ? f : f.withFileExtension(".wav"));
            sourceLabel.setText(processor.getSourceName(), juce::dontSendNotification);
        });
}

void IRForgeAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient grad(Palette::bgLift(), 0.0f, 0.0f,
        Palette::bg(), 0.0f, (float)getHeight(), false);
    g.setGradientFill(grad);
    g.fillAll();

    // a warm glow behind the character section — this is the forge
    auto glowCentre = juce::Point<float>(getWidth() * 0.13f, getHeight() * 0.78f);
    juce::ColourGradient glow(Palette::hot().withAlpha(0.10f), glowCentre.x, glowCentre.y,
        Palette::hot().withAlpha(0.0f),
        glowCentre.x + getWidth() * 0.30f, glowCentre.y, true);
    g.setGradientFill(glow);
    g.fillEllipse(juce::Rectangle<float>(getWidth() * 0.6f, getWidth() * 0.6f)
        .withCentre(glowCentre));

    // section rules
    auto rule = [&g](juce::Rectangle<int> area, const juce::String& label)
        {
            g.setColour(juce::Colours::white.withAlpha(0.11f));
            g.drawHorizontalLine(area.getY(), (float)area.getX(), (float)area.getRight());
            g.setColour(Palette::textDim());
            g.setFont(juce::Font(juce::FontOptions(11.0f)).withExtraKerningFactor(0.22f));
            g.drawText(label, area.translated(0, 3), juce::Justification::topLeft, false);
        };

    const int ctrlTop = getHeight() - 214;
    auto b = getLocalBounds().reduced(16, 0);
    rule(b.withY(ctrlTop).withHeight(14).withWidth(b.getWidth() / 3 - 8), "FORGE");
    rule(b.withY(ctrlTop).withHeight(14)
        .withX(b.getX() + b.getWidth() / 3 + 4)
        .withWidth(b.getWidth() / 3 - 8), "SHAPE");
    rule(b.withY(ctrlTop).withHeight(14)
        .withX(b.getX() + 2 * b.getWidth() / 3 + 8)
        .withWidth(b.getWidth() / 3 - 8), "OUTPUT");
}

void IRForgeAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(16);

    // header
    auto header = r.removeFromTop(40);
    titleLabel.setBounds(header.removeFromLeft(150));
    clearButton.setBounds(header.removeFromRight(66).reduced(2, 6));
    header.removeFromRight(6);
    saveButton.setBounds(header.removeFromRight(78).reduced(2, 6));
    header.removeFromRight(6);
    loadButton.setBounds(header.removeFromRight(66).reduced(2, 6));
    header.removeFromRight(6);
    recordButton.setBounds(header.removeFromRight(66).reduced(2, 6));
    header.removeFromRight(12);
    sourceLabel.setBounds(header.removeFromLeft(juce::jmin(260, header.getWidth())));
    statusLabel.setBounds(header);

    r.removeFromTop(10);

    // displays
    auto controls = r.removeFromBottom(200);
    r.removeFromBottom(14);
    const int srcH = juce::jmax(90, r.getHeight() * 40 / 100);
    sourceDisplay->setBounds(r.removeFromTop(srcH));
    r.removeFromTop(10);
    irDisplay->setBounds(r);

    // controls: three columns
    const int colW = (controls.getWidth() - 24) / 3;
    auto forge = controls.removeFromLeft(colW); controls.removeFromLeft(12);
    auto shape = controls.removeFromLeft(colW); controls.removeFromLeft(12);
    auto output = controls;

    for (auto* a : { &forge, &shape, &output }) a->removeFromTop(18);

    // FORGE: the big Character knob plus its supports
    auto big = forge.removeFromLeft(juce::jmin(108, forge.getWidth() / 2));
    characterKnob.setBounds(big.reduced(4));
    {
        auto top = forge.removeFromTop(forge.getHeight() / 2);
        const int w = juce::jmax(40, top.getWidth() / 3);
        fftKnob.setBounds(top.removeFromLeft(w).reduced(3));
        lengthKnob.setBounds(top.removeFromLeft(w).reduced(3));
        decayKnob.setBounds(top.reduced(3));
        linearPhaseButton.setBounds(forge.removeFromTop(22).reduced(4, 2));
    }

    // SHAPE
    {
        auto row1 = shape.removeFromTop(shape.getHeight() / 2 - 12);
        const int w = juce::jmax(40, row1.getWidth() / 3);
        stretchKnob.setBounds(row1.removeFromLeft(w).reduced(3));
        predelayKnob.setBounds(row1.removeFromLeft(w).reduced(3));
        tiltKnob.setBounds(row1.reduced(3));

        auto row2 = shape.removeFromTop(shape.getHeight() - 24);
        lowCutKnob.setBounds(row2.removeFromLeft(w).reduced(3));
        highCutKnob.setBounds(row2.removeFromLeft(w).reduced(3));
        widthKnob.setBounds(row2.reduced(3));

        reverseButton.setBounds(shape.removeFromTop(22).reduced(4, 2));
    }

    // OUTPUT
    {
        auto row1 = output;
        const int w = juce::jmax(40, row1.getWidth() / 2);
        mixKnob.setBounds(row1.removeFromLeft(w).reduced(3));
        gainKnob.setBounds(row1.reduced(3));
    }
}