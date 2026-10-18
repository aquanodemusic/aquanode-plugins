/*
    FlowEQ - PluginEditor.cpp
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
FlowLookAndFeel::FlowLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, FlowEQColours::cyan);
    setColour(juce::Slider::rotarySliderFillColourId, FlowEQColours::cyan);
    setColour(juce::Slider::rotarySliderOutlineColourId, FlowEQColours::panelBabyBlueLo);
    setColour(juce::Slider::textBoxTextColourId, FlowEQColours::textDark);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::backgroundColourId, FlowEQColours::panelBabyBlueLo);
    setColour(juce::ComboBox::textColourId, FlowEQColours::textDark);
    setColour(juce::TextButton::buttonColourId, FlowEQColours::panelBabyBlueLo);
    setColour(juce::TextButton::textColourOffId, FlowEQColours::textDark);
}

void FlowLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
    float pos, float startAngle, float endAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(2.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Skeuomorphic little metal cap with radial gradient
    juce::ColourGradient capGrad(FlowEQColours::metalHighlight, centre.x, centre.y - radius,
        colour.darker(0.3f), centre.x, centre.y + radius, false);
    g.setGradientFill(capGrad);
    g.fillEllipse(bounds.reduced(radius * 0.18f));

    g.setColour(colour.withAlpha(0.55f));
    g.drawEllipse(bounds.reduced(radius * 0.18f), 1.5f);

    // Fine tick ring
    juce::Path ticks;
    for (int i = 0; i <= 10; ++i)
    {
        float a = startAngle + (endAngle - startAngle) * (float)i / 10.0f;
        auto p1 = centre.getPointOnCircumference(radius * 0.98f, a);
        auto p2 = centre.getPointOnCircumference(radius * 0.86f, a);
        ticks.startNewSubPath(p1);
        ticks.lineTo(p2);
    }
    g.setColour(colour.withAlpha(0.5f));
    g.strokePath(ticks, juce::PathStrokeType(1.2f));

    // Pointer
    float angle = startAngle + pos * (endAngle - startAngle);
    juce::Path pointer;
    pointer.addRoundedRectangle(-1.6f, -radius * 0.82f, 3.2f, radius * 0.6f, 1.6f);
    g.setColour(colour.darker(0.2f));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre));
}

void FlowLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
    bool highlighted, bool down)
{
    auto bounds = b.getLocalBounds().toFloat().reduced(1.0f);
    auto colour = bg;
    if (down) colour = colour.darker(0.2f);
    else if (highlighted) colour = colour.brighter(0.1f);

    juce::ColourGradient grad(colour.brighter(0.25f), bounds.getX(), bounds.getY(),
        colour.darker(0.15f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(colour.darker(0.4f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

//==============================================================================
TopEqGraph::TopEqGraph(FlowEQAudioProcessor& p) : proc(p)
{
    freqTable.resize(200);
    for (size_t i = 0; i < freqTable.size(); ++i)
        freqTable[i] = kMinFreq * std::pow(kMaxFreq / kMinFreq, (double)i / (double)(freqTable.size() - 1));

    startTimerHz(30);
    setInterceptsMouseClicks(true, false);
}

TopEqGraph::~TopEqGraph() { stopTimer(); }

void TopEqGraph::timerCallback() { repaint(); }

float TopEqGraph::freqToX(float freq) const
{
    float logMin = std::log10(kMinFreq), logMax = std::log10(kMaxFreq);
    float logF = std::log10(juce::jlimit(kMinFreq, kMaxFreq, freq));
    return graphArea.getX() + (logF - logMin) / (logMax - logMin) * graphArea.getWidth();
}

float TopEqGraph::xToFreq(float x) const
{
    float logMin = std::log10(kMinFreq), logMax = std::log10(kMaxFreq);
    float t = juce::jlimit(0.0f, 1.0f, (x - graphArea.getX()) / graphArea.getWidth());
    return std::pow(10.0f, logMin + t * (logMax - logMin));
}

float TopEqGraph::gainToY(float gainDb) const
{
    float t = juce::jlimit(0.0f, 1.0f, (gainDb - kMinGainDb) / (kMaxGainDb - kMinGainDb));
    return graphArea.getBottom() - t * graphArea.getHeight();
}

float TopEqGraph::yToGain(float y) const
{
    float t = juce::jlimit(0.0f, 1.0f, (graphArea.getBottom() - y) / graphArea.getHeight());
    return kMinGainDb + t * (kMaxGainDb - kMinGainDb);
}

int TopEqGraph::hitTestCircle(juce::Point<float> pos) const
{
    for (int i = kNumFilters - 1; i >= 0; --i)
    {
        float freq = ((juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(i) + "_freq"))->get();
        float gain = ((juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(i) + "_gain"))->get();
        juce::Point<float> c(freqToX(freq), gainToY(gain));
        if (c.getDistanceFrom(pos) < 14.0f)
            return i;
    }
    return -1;
}

void TopEqGraph::mouseDown(const juce::MouseEvent& e)
{
    int hit = hitTestCircle(e.position);
    draggingFilter = hit; // -1 when nothing was hit, so mouseDrag can never
    // act on a stale filter left over from a previous drag
    if (hit >= 0)
    {
        proc.selectedFilter.store(hit);
        if (onFilterSelected) onFilterSelected(hit);
        repaint();
    }
}

void TopEqGraph::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingFilter < 0) return;
    auto i = draggingFilter;
    auto* freqP = (juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(i) + "_freq");
    auto* gainP = (juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(i) + "_gain");

    float newFreq = xToFreq(e.position.x);
    float newGain = yToGain(e.position.y);

    freqP->setValueNotifyingHost(freqP->convertTo0to1(newFreq));
    gainP->setValueNotifyingHost(gainP->convertTo0to1(newGain));

    if (recordMode)
        recordDragIntoCurves(i, newFreq, newGain);
}

void TopEqGraph::mouseUp(const juce::MouseEvent&)
{
    draggingFilter = -1;
}

void TopEqGraph::recordDragIntoCurves(int filterIndex, float freqHz, float gainDb)
{
    // Freq curve is absolute, 20-20000Hz on a log scale - the dragged
    // frequency maps onto it directly, same formula used to play it back.
    float freqNorm = juce::jlimit(0.0f, 1.0f,
        std::log(freqHz / kMinFreq) / std::log(kMaxFreq / kMinFreq));

    // Gain curve is a +-24dB delta centred on 0.5 == 0dB, the same mapping
    // used for playback. Recording treats the dragged dB value as that
    // delta directly, so the filter's base Gain knob should stay at 0dB
    // while recording for the played-back result to match what was drawn.
    float gainNorm = juce::jlimit(0.0f, 1.0f, (gainDb / (2.0f * kMaxGainDb)) + 0.5f);

    // Writes at the index matching the curve's own currently running cycle
    // phase, so recording "paints" the curve as it plays through its loop -
    // freq and gain curves can have different cycle times, so each uses
    // its own live phase.
    auto writeAtLivePhase = [this, filterIndex](CurveTarget target, float normValue)
        {
            if (proc.isUsingWavetable(filterIndex, target))
                return; // wavetable data is read-only, nothing to record into

            double phase = proc.getLiveState(filterIndex, target).phase;
            int idx = juce::jlimit(0, kNumCurvePoints - 1,
                (int)std::round(phase * (double)(kNumCurvePoints - 1)));
            proc.getCurve(filterIndex, target).points[(size_t)idx] = normValue;
        };

    writeAtLivePhase(CurveTarget::Frequency, freqNorm);
    writeAtLivePhase(CurveTarget::Gain, gainNorm);
}

void TopEqGraph::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int hit = hitTestCircle(e.position);
    if (hit < 0) hit = proc.selectedFilter.load();
    auto* qP = (juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(hit) + "_q");
    float newQ = juce::jlimit(kMinQ, kMaxQ, qP->get() + wheel.deltaY * 1.5f);
    qP->setValueNotifyingHost(qP->convertTo0to1(newQ));
}

void TopEqGraph::buildResponsePath(juce::Path& path, const std::vector<double>& magsDb)
{
    path.clear();
    for (size_t i = 0; i < freqTable.size(); ++i)
    {
        float x = freqToX((float)freqTable[i]);
        float y = gainToY(juce::jlimit((double)kMinGainDb, (double)kMaxGainDb, magsDb[i]));
        if (i == 0) path.startNewSubPath(x, y);
        else path.lineTo(x, y);
    }
}

void TopEqGraph::resized()
{
    graphArea = getLocalBounds().toFloat().reduced(14.0f, 10.0f);
}

void TopEqGraph::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Panel background: baby blue, rounded, subtle inner shading
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.fillRoundedRectangle(bounds.translated(0, 2.0f), 14.0f);

    juce::ColourGradient bgGrad(FlowEQColours::panelBabyBlue, bounds.getX(), bounds.getY(),
        FlowEQColours::panelBabyBlueLo, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, 14.0f);

    g.saveState();
    juce::Path clip; clip.addRoundedRectangle(bounds, 14.0f);
    g.reduceClipRegion(clip);

    // Hertz grid like on analog gear: vertical lines that get denser
    g.setColour(FlowEQColours::textDark.withAlpha(0.18f));
    for (float f = 20.0f; f <= 20000.0f; )
    {
        float x = freqToX(f);
        bool bold = (f == 100.0f || f == 1000.0f || f == 10000.0f);
        g.setColour(FlowEQColours::textDark.withAlpha(bold ? 0.35f : 0.15f));
        g.drawLine(x, graphArea.getY(), x, graphArea.getBottom(), bold ? 1.2f : 0.6f);

        float step = f < 100.0f ? 10.0f : (f < 1000.0f ? 100.0f : (f < 10000.0f ? 1000.0f : 10000.0f));
        f += step;
    }
    // dB grid, horizontal
    for (float db = kMinGainDb; db <= kMaxGainDb; db += 6.0f)
    {
        float y = gainToY(db);
        bool zero = std::abs(db) < 0.01f;
        g.setColour(FlowEQColours::textDark.withAlpha(zero ? 0.4f : 0.15f));
        g.drawLine(graphArea.getX(), y, graphArea.getRight(), y, zero ? 1.4f : 0.6f);
    }

    // Individual curves per filter (semi-transparent, in filter colour)
    std::vector<double> mags;
    for (int i = 0; i < kNumFilters; ++i)
    {
        proc.getFilterMagnitudeResponse(i, freqTable, mags);
        juce::Path p;
        buildResponsePath(p, mags);
        g.setColour(FlowEQColours::forFilter(i).withAlpha(0.55f));
        g.strokePath(p, juce::PathStrokeType(2.0f));
    }

    // White sum curve
    proc.getTotalMagnitudeResponse(freqTable, mags);
    juce::Path total;
    buildResponsePath(total, mags);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.strokePath(total, juce::PathStrokeType(2.6f));

    g.restoreState();

    // Circles (filter handles), skeuomorphic with glow when active
    int selected = proc.selectedFilter.load();
    for (int i = 0; i < kNumFilters; ++i)
    {
        float freq = ((juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(i) + "_freq"))->get();
        float gain = ((juce::AudioParameterFloat*)proc.apvts.getParameter("f" + juce::String(i) + "_gain"))->get();
        juce::Point<float> c(freqToX(freq), gainToY(gain));
        auto colour = FlowEQColours::forFilter(i);
        bool isSelected = (i == selected);

        if (isSelected)
        {
            g.setColour(colour.withAlpha(0.35f));
            g.fillEllipse(juce::Rectangle<float>(24.0f, 24.0f).withCentre(c));
        }

        juce::ColourGradient dropGrad(colour.brighter(0.5f), c.x, c.y - 8.0f,
            colour.darker(0.3f), c.x, c.y + 8.0f, false);
        g.setGradientFill(dropGrad);
        g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(c));
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre(c), isSelected ? 2.0f : 1.0f);
    }

    g.setColour(FlowEQColours::textDark.withAlpha(0.6f));
    g.setFont(11.0f);
    g.drawText("20Hz", graphArea.getX(), graphArea.getBottom() - 14.0f, 40, 14, juce::Justification::left);
    g.drawText("20kHz", graphArea.getRight() - 44.0f, graphArea.getBottom() - 14.0f, 44, 14, juce::Justification::right);

    // Record mode indicator: red frame + pulsing "REC" label, drawn last
    // so it's clearly visible above the grid and curves.
    if (recordMode)
    {
        auto red = juce::Colours::crimson;
        g.setColour(red.withAlpha(0.8f));
        g.drawRoundedRectangle(bounds.reduced(1.5f), 14.0f, 2.0f);

        float pulse = 0.5f + 0.5f * std::sin(juce::Time::getMillisecondCounterHiRes() * 0.006);
        g.setColour(red.withAlpha(0.6f + 0.4f * pulse));
        g.fillEllipse(bounds.getRight() - 78.0f, bounds.getY() + 8.0f, 8.0f, 8.0f);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText("REC", bounds.getRight() - 66.0f, bounds.getY() + 3.0f, 50, 16, juce::Justification::left);
    }
}

//==============================================================================
CurveDrawer::CurveDrawer(FlowEQAudioProcessor& p, CurveTarget t, juce::String cap)
    : proc(p), target(t), caption(std::move(cap))
{
    timeSlider.setRange(kMinTimeSec, kMaxTimeSec, 0.01);
    timeSlider.setTextValueSuffix("s");
    // Compact readout to the right of the knob (instead of a tall block
    // below it) so the whole control fits into a single header row.
    timeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 20);
    addAndMakeVisible(timeSlider);

    syncToggle.setClickingTogglesState(true);
    syncToggle.setButtonText("Sync");
    addAndMakeVisible(syncToggle);
    syncToggle.onClick = [this] { updateSyncVisibility(); };

    for (auto& d : getSyncDivisions())
        syncDivBox.addItem(d.label, syncDivBox.getNumItems() + 1);
    addAndMakeVisible(syncDivBox);

    loadWavetableButton.onClick = [this] { chooseWavetableFile(); };
    addAndMakeVisible(loadWavetableButton);

    drawModeButton.setClickingTogglesState(false);
    drawModeButton.onClick = [this]
        {
            proc.setUseWavetable(activeFilter, target, false);
            updateModeControls();
            repaint();
        };
    addAndMakeVisible(drawModeButton);

    smoothButton.setClickingTogglesState(false);
    smoothButton.setTooltip("Smooth the drawn curve");
    smoothButton.onClick = [this] { smoothDrawnCurve(); };
    addAndMakeVisible(smoothButton);

    frameKnob.setRange(0.0, 1.0, 1.0);
    frameKnob.setTooltip("Wavetable frame");
    frameKnob.onValueChange = [this]
        {
            if (!proc.isUsingWavetable(activeFilter, target)) return;
            proc.setWavetableFrame(activeFilter, target, (int)std::round(frameKnob.getValue()));
            repaint();
        };
    addAndMakeVisible(frameKnob);

    setActiveFilter(0);
    startTimerHz(30);
}

CurveDrawer::~CurveDrawer() { stopTimer(); }

void CurveDrawer::setActiveFilter(int filterIndex)
{
    activeFilter = filterIndex;
    updateSliderAttachment();
    updateModeControls();
    repaint();
}

void CurveDrawer::updateModeControls()
{
    bool wt = proc.isUsingWavetable(activeFilter, target);
    int numFrames = proc.getWavetableNumFrames(activeFilter, target);

    // "Draw" only makes sense/is clickable while a wavetable is active.
    drawModeButton.setVisible(wt);

    // Smooth only makes sense for a freehand-drawn curve, not read-only
    // wavetable data.
    smoothButton.setEnabled(!wt);

    // Frame knob only makes sense if there's actually more than one frame.
    bool showFrameKnob = wt && numFrames > 1;
    frameKnob.setVisible(showFrameKnob);
    if (showFrameKnob)
    {
        frameKnob.setRange(0.0, (double)(numFrames - 1), 1.0);
        frameKnob.setValue(proc.getWavetableFrame(activeFilter, target), juce::dontSendNotification);
    }
}

void CurveDrawer::smoothDrawnCurve()
{
    proc.smoothCurve(activeFilter, target);
    repaint();
}

void CurveDrawer::chooseWavetableFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load wavetable (mono WAV, multiple of 2048 samples)...",
        juce::File(), "*.wav");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                if (proc.loadWavetable(activeFilter, target, file))
                {
                    updateModeControls();
                    repaint();
                }
            }
        });
}

void CurveDrawer::scrollFrame(int delta)
{
    if (!proc.isUsingWavetable(activeFilter, target)) return;
    int numFrames = proc.getWavetableNumFrames(activeFilter, target);
    if (numFrames <= 1) return;
    int cur = proc.getWavetableFrame(activeFilter, target);
    proc.setWavetableFrame(activeFilter, target, (cur + delta + numFrames) % numFrames);
    repaint();
}

void CurveDrawer::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (curveArea.contains(e.position) && proc.isUsingWavetable(activeFilter, target))
        scrollFrame(wheel.deltaY > 0 ? 1 : -1);
}

void CurveDrawer::updateSliderAttachment()
{
    juce::String tName = target == CurveTarget::Frequency ? "freq" : (target == CurveTarget::Gain ? "gain" : "q");
    auto baseId = "f" + juce::String(activeFilter) + "_" + tName;

    timeAttachment.reset();
    syncAttachment.reset();
    syncDivAttachment.reset();

    timeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, baseId + "_time", timeSlider);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, baseId + "_sync", syncToggle);
    syncDivAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc.apvts, baseId + "_syncdiv", syncDivBox);

    updateSyncVisibility();
}

void CurveDrawer::updateSyncVisibility()
{
    bool sync = syncToggle.getToggleState();
    timeSlider.setVisible(!sync);
    syncDivBox.setVisible(sync);
}

void CurveDrawer::timerCallback()
{
    // Keep the knob in sync if the frame was changed via mouse wheel/drag
    // on the curve (but not while the user is actively turning the knob).
    if (frameKnob.isVisible() && !frameKnob.isMouseButtonDown())
    {
        double target_ = (double)proc.getWavetableFrame(activeFilter, target);
        if (std::abs(frameKnob.getValue() - target_) > 0.001)
            frameKnob.setValue(target_, juce::dontSendNotification);
    }
    repaint();
}

void CurveDrawer::resized()
{
    auto b = getLocalBounds().reduced(10);

    // Dedicated header row just for the caption, so it's never covered by
    // any control (controls only ever live in the row below).
    captionBounds = b.removeFromTop(16);
    b.removeFromTop(4);

    // Single control row: every knob, button and toggle for this curve
    // gets its own fixed-width slot here, left to right, in a fixed order.
    // A control that doesn't currently apply (e.g. the wavetable frame
    // knob outside wavetable mode, or Draw outside wavetable mode) simply
    // stays invisible in its reserved slot rather than being skipped, so
    // nothing else ever shifts around or overlaps.
    auto row = b.removeFromTop(26);

    auto timeSlot = row.removeFromLeft(76);
    timeSlider.setBounds(timeSlot);
    // The sync-division box (e.g. "1/4T") replaces the time knob in the
    // same slot when tempo sync is enabled.
    syncDivBox.setBounds(timeSlot.withSizeKeepingCentre(timeSlot.getWidth(), 22));

    row.removeFromLeft(2);
    syncToggle.setBounds(row.removeFromLeft(34).withSizeKeepingCentre(34, 22));

    row.removeFromLeft(2);
    smoothButton.setBounds(row.removeFromLeft(52).withSizeKeepingCentre(50, 22));

    row.removeFromLeft(2);
    loadWavetableButton.setBounds(row.removeFromLeft(32).withSizeKeepingCentre(32, 22));

    row.removeFromLeft(2);
    // "Draw" only makes sense while a wavetable is active (see
    // updateModeControls), but always reserves its own slot here.
    drawModeButton.setBounds(row.removeFromLeft(38).withSizeKeepingCentre(38, 22));

    row.removeFromLeft(2);
    // Wavetable frame knob: only shown once a multi-frame wavetable is
    // active, but always reserves its own fixed slot so it never has to
    // fight another control for space.
    frameKnob.setBounds(row.removeFromLeft(28).withSizeKeepingCentre(26, 26));

    b.removeFromTop(6);

    curveArea = b.toFloat().reduced(2.0f, 4.0f);
}

void CurveDrawer::paintCurveArea(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(juce::Colours::white.withAlpha(0.35f));
    g.fillRoundedRectangle(area, 8.0f);

    // Fine grid lines
    g.setColour(FlowEQColours::textDark.withAlpha(0.12f));
    for (int i = 1; i < 8; ++i)
    {
        float y = area.getY() + area.getHeight() * (float)i / 8.0f;
        g.drawLine(area.getX(), y, area.getRight(), y, 0.5f);
    }
    for (int i = 1; i < 8; ++i)
    {
        float x = area.getX() + area.getWidth() * (float)i / 8.0f;
        g.drawLine(x, area.getY(), x, area.getBottom(), 0.5f);
    }

    // Emphasise the centre (rest) line
    g.setColour(FlowEQColours::textDark.withAlpha(0.25f));
    g.drawLine(area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);

    auto colour = FlowEQColours::forFilter(activeFilter);
    bool wt = proc.isUsingWavetable(activeFilter, target);

    // Only the currently active window of points is drawn: 128 in
    // freehand mode, 2048 from the active wavetable frame - both are
    // rendered unrolled across the full width of the window.
    int numPoints = proc.getActiveNumPoints(activeFilter, target);

    juce::Path path;
    for (int i = 0; i < numPoints; ++i)
    {
        float x = area.getX() + area.getWidth() * (float)i / (float)(numPoints - 1);
        float y = area.getBottom() - proc.getActivePointNormalized(activeFilter, target, i) * area.getHeight();
        if (i == 0) path.startNewSubPath(x, y);
        else path.lineTo(x, y);
    }
    g.setColour(wt ? colour.withAlpha(0.9f) : colour);
    g.strokePath(path, juce::PathStrokeType(wt ? 1.4f : 2.2f, juce::PathStrokeType::curved));

    // Travelling dot (live phase)
    auto live = proc.getLiveState(activeFilter, target);
    float normAtPhase = wt ? 0.0f : proc.getCurve(activeFilter, target).getInterpolated((float)live.phase);
    if (wt)
    {
        // a rough recalculation from the phase is enough for the dot
        int idx = juce::jlimit(0, numPoints - 1, (int)(live.phase * numPoints));
        normAtPhase = proc.getActivePointNormalized(activeFilter, target, idx);
    }
    float px = area.getX() + area.getWidth() * (float)live.phase;
    float py = area.getBottom() - normAtPhase * area.getHeight();
    g.setColour(juce::Colours::white);
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ px, py }));
    g.setColour(colour);
    g.drawEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ px, py }), 1.5f);

    if (wt)
    {
        int numFrames = proc.getWavetableNumFrames(activeFilter, target);
        int curFrame = proc.getWavetableFrame(activeFilter, target);
        g.setColour(FlowEQColours::textDark.withAlpha(0.6f));
        g.setFont(10.0f);
        g.drawText(proc.getWavetableName(activeFilter, target) + "  " + juce::String(curFrame + 1)
            + "/" + juce::String(juce::jmax(1, numFrames)),
            area.getX(), area.getY() - 12.0f, area.getWidth(), 12.0f, juce::Justification::left);
    }
}

void CurveDrawer::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.1f));
    g.fillRoundedRectangle(bounds.translated(0, 2.0f), 12.0f);

    auto colour = FlowEQColours::forFilter(activeFilter);
    juce::ColourGradient bgGrad(FlowEQColours::panelBabyBlue, bounds.getX(), bounds.getY(),
        FlowEQColours::panelBabyBlueLo, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(colour.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.2f);

    g.setColour(FlowEQColours::textDark);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText(caption, captionBounds, juce::Justification::centredLeft);

    paintCurveArea(g, curveArea);
}

void CurveDrawer::paintValueLine(const juce::MouseEvent& e)
{
    if (!curveArea.contains(e.position)) return;

    auto& curve = proc.getCurve(activeFilter, target);
    float xNorm = juce::jlimit(0.0f, 1.0f, (e.position.x - curveArea.getX()) / curveArea.getWidth());
    float yNorm = juce::jlimit(0.0f, 1.0f, (curveArea.getBottom() - e.position.y) / curveArea.getHeight());

    int idx = (int)std::round(xNorm * (float)(kNumCurvePoints - 1));
    idx = juce::jlimit(0, kNumCurvePoints - 1, idx);
    curve.points[(size_t)idx] = yNorm;

    // Gently pull in nearby points for a smooth line while drawing
    for (int d = 1; d <= 2; ++d)
    {
        float w = 1.0f - (float)d / 3.0f;
        if (idx - d >= 0)
            curve.points[(size_t)(idx - d)] = juce::jmap(w, curve.points[(size_t)(idx - d)], yNorm);
        if (idx + d < kNumCurvePoints)
            curve.points[(size_t)(idx + d)] = juce::jmap(w, curve.points[(size_t)(idx + d)], yNorm);
    }

    repaint();
}

void CurveDrawer::mouseDown(const juce::MouseEvent& e)
{
    dragFrameOffset = 0;
    if (proc.isUsingWavetable(activeFilter, target))
        return; // curve is read-only in wavetable mode (only frame scroll/scrub is allowed)
    paintValueLine(e);
}

void CurveDrawer::mouseDrag(const juce::MouseEvent& e)
{
    if (proc.isUsingWavetable(activeFilter, target))
    {
        // Horizontal dragging scrubs frame by frame through the wavetable,
        // ~6px of mouse movement per frame step.
        int dragDx = e.getPosition().x - e.getMouseDownPosition().x;
        int framesToMove = dragDx / 6;
        int delta = framesToMove - dragFrameOffset;
        if (delta != 0)
        {
            scrollFrame(delta);
            dragFrameOffset = framesToMove;
        }
        return;
    }
    paintValueLine(e);
}

//==============================================================================
FlowEQAudioProcessorEditor::FlowEQAudioProcessorEditor(FlowEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
    topGraph(p),
    freqDrawer(p, CurveTarget::Frequency, "Frequency"),
    gainDrawer(p, CurveTarget::Gain, "Gain"),
    qDrawer(p, CurveTarget::Q, "Q")
{
    setLookAndFeel(&lookAndFeel);
    setSize(900, 600);

    titleLabel.setText("FlowEQ", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(20.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, FlowEQColours::textDark.withAlpha(0.85f));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    addAndMakeVisible(topGraph);
    addAndMakeVisible(freqDrawer);
    addAndMakeVisible(gainDrawer);
    addAndMakeVisible(qDrawer);

    topGraph.onFilterSelected = [this](int i)
        {
            freqDrawer.setActiveFilter(i);
            gainDrawer.setActiveFilter(i);
            qDrawer.setActiveFilter(i);
        };

    recordButton.setClickingTogglesState(true);
    recordButton.setColour(juce::TextButton::buttonOnColourId, juce::Colours::crimson);
    recordButton.setTooltip("While active, dragging a filter circle records the "
        "gesture into that filter's Freq and Gain curves");
    recordButton.onClick = [this] { topGraph.setRecordMode(recordButton.getToggleState()); };
    addAndMakeVisible(recordButton);

    resetButton.onClick = [this] { audioProcessor.resetAll(); repaint(); };
    addAndMakeVisible(resetButton);
}

FlowEQAudioProcessorEditor::~FlowEQAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void FlowEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Outer frame: bubblegum green, slightly metallic gradient
    juce::ColourGradient outerGrad(FlowEQColours::outerGummi.brighter(0.15f), bounds.getX(), bounds.getY(),
        FlowEQColours::outerGummiDark, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(outerGrad);
    g.fillAll();

    // Subtle brushed-metal texture
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (float y = 0; y < bounds.getHeight(); y += 3.0f)
        g.drawLine(0, y, bounds.getWidth(), y, 1.0f);

    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.drawRect(bounds, 2.0f);
}

void FlowEQAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced(14);

    auto header = b.removeFromTop(30);
    titleLabel.setBounds(header.removeFromLeft(200));
    resetButton.setBounds(header.removeFromRight(90).reduced(0, 2));
    header.removeFromRight(6);
    recordButton.setBounds(header.removeFromRight(60).reduced(0, 2));

    b.removeFromTop(6);

    auto topArea = b.removeFromTop((int)(b.getHeight() * 0.52f));
    topGraph.setBounds(topArea);

    b.removeFromTop(10);

    auto bottomArea = b;
    int gap = 10;
    int w = (bottomArea.getWidth() - gap * 2) / 3;
    freqDrawer.setBounds(bottomArea.removeFromLeft(w));
    bottomArea.removeFromLeft(gap);
    gainDrawer.setBounds(bottomArea.removeFromLeft(w));
    bottomArea.removeFromLeft(gap);
    qDrawer.setBounds(bottomArea);
}