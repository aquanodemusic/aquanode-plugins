/*
  ==============================================================================
    FrequencyDrawer -- PluginEditor.cpp
  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
//  DrawingCanvas
//==============================================================================

DrawingCanvas::DrawingCanvas(FrequencyDrawerAudioProcessor& p) : proc_(p) {}

//------------------------------------------------------------------------------
// Coordinate conversions
//------------------------------------------------------------------------------

juce::Point<float> DrawingCanvas::tfToXY(double t, double f) const noexcept
{
    const float drawW = static_cast<float>(getWidth() - kML);
    const float drawH = static_cast<float>(getHeight() - kMB - kMT);

    const float x = kML + static_cast<float>(t / FrequencyDrawerAudioProcessor::kDuration) * drawW;

    const double logMin = std::log(FrequencyDrawerAudioProcessor::kFreqMin);
    const double logMax = std::log(FrequencyDrawerAudioProcessor::kFreqMax);
    const float  norm = static_cast<float>((std::log(f) - logMin) / (logMax - logMin));
    const float  y = kMT + (1.0f - norm) * drawH;

    return { x, y };
}

juce::Point<double> DrawingCanvas::xyToTF(int x, int y) const noexcept
{
    const double drawW = getWidth() - kML;
    const double drawH = getHeight() - kMB - kMT;

    const double t = juce::jlimit(0.0, FrequencyDrawerAudioProcessor::kDuration,
        (x - kML) / drawW * FrequencyDrawerAudioProcessor::kDuration);

    const double norm = juce::jlimit(0.0, 1.0, 1.0 - (y - kMT) / drawH);
    const double logMin = std::log(FrequencyDrawerAudioProcessor::kFreqMin);
    const double logMax = std::log(FrequencyDrawerAudioProcessor::kFreqMax);
    const double f = std::exp(logMin + norm * (logMax - logMin));

    return { t, f };
}

//------------------------------------------------------------------------------
// Scale snapping
//------------------------------------------------------------------------------

double DrawingCanvas::snapFrequency(double freq) const noexcept
{
    static const int cMajor[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int cMinor[] = { 0, 2, 3, 5, 7, 8, 10 };
    static const int cPentMajor[] = { 0, 2, 4, 7, 9 };

    const int* scale = nullptr;
    int        nNotes = 0;

    switch (scaleMode_)
    {
    case 1: scale = cMajor;     nNotes = 7; break;
    case 2: scale = cMinor;     nNotes = 7; break;
    case 3: scale = cPentMajor; nNotes = 5; break;
    default: return freq;
    }

    const double C0 = 16.3516;
    double bestFreq = freq;
    double bestDist = 1.0e18;

    for (int octave = 0; octave <= 10; ++octave)
    {
        for (int i = 0; i < nNotes; ++i)
        {
            const double noteFreq = C0 * std::pow(2.0, (octave * 12 + scale[i]) / 12.0);
            if (noteFreq < FrequencyDrawerAudioProcessor::kFreqMin) continue;
            if (noteFreq > FrequencyDrawerAudioProcessor::kFreqMax) continue;
            const double dist = std::abs(std::log2(noteFreq) - std::log2(freq));
            if (dist < bestDist) { bestDist = dist; bestFreq = noteFreq; }
        }
    }
    return bestFreq;
}

//------------------------------------------------------------------------------
// Cache management
//------------------------------------------------------------------------------

void DrawingCanvas::resized() { cacheDirty_ = true; }

void DrawingCanvas::clearHistory()
{
    stampHistory_.clear();
    interpPaths_.clear();
    currentInterpStroke_.clear();
    pendingAudioEvents_.clear();
    lastAudioEventTime_ = -1.0;
    committedImage_ = juce::Image(); // reset to null image
    cacheDirty_ = true;
    repaint();
}

/** Bake the current drawCache_ into committedImage_ and discard the raw
 *  data-point history.  After this call both the audio (in the processor's
 *  committedBuffer_) and the visual (in committedImage_) capture the full
 *  state, so stampHistory_ and interpPaths_ are no longer needed. */
void DrawingCanvas::commitCurrentImage()
{
    if (cacheDirty_ || drawCache_.getWidth() != getWidth() || drawCache_.getHeight() != getHeight())
        rebuildCache();

    committedImage_ = drawCache_.createCopy();

    stampHistory_.clear();
    interpPaths_.clear();
}

//------------------------------------------------------------------------------
// rebuildCache
//------------------------------------------------------------------------------
void DrawingCanvas::rebuildCache()
{
    const int W = juce::jmax(1, getWidth()), H = juce::jmax(1, getHeight());
    drawCache_ = juce::Image(juce::Image::ARGB, W, H, true);

    if (committedImage_.isValid())
    {
        juce::Graphics cg(drawCache_);
        cg.drawImage(committedImage_,
            0, 0, W, H,
            0, 0, committedImage_.getWidth(), committedImage_.getHeight());
    }

    if (!stampHistory_.empty())
    {
        juce::Graphics cg(drawCache_);
        for (const auto& rec : stampHistory_)
            stampEventGfx(cg, rec.t, rec.f, rec.blurEnabled, rec.blurSecs);
    }

    if (!interpPaths_.empty())
    {
        juce::Graphics cg(drawCache_);

        for (const auto& pathRec : interpPaths_)
        {
            if (pathRec.waypoints.size() < 2) continue;

            double maxF = 0.0;
            for (const auto& wp : pathRec.waypoints)
                maxF = std::max(maxF, wp.second);

            for (int h = 1; h <= pathRec.numHarmonics; ++h)
            {
                if (maxF * static_cast<double>(h) > FrequencyDrawerAudioProcessor::kFreqMax)
                    break;

                const float alpha = (h == 1) ? 0.85f : (0.60f / static_cast<float>(h));
                cg.setColour(juce::Colours::white.withAlpha(alpha));

                if (h == 1)
                {
                    drawInterpPolyline(cg, pathRec.waypoints, 1.5f);
                }
                else
                {
                    std::vector<std::pair<double, double>> scaledWps;
                    scaledWps.reserve(pathRec.waypoints.size());
                    for (const auto& wp : pathRec.waypoints)
                        scaledWps.push_back({ wp.first, wp.second * static_cast<double>(h) });
                    drawInterpPolyline(cg, scaledWps, 1.0f);
                }
            }
        }
    }

    cacheDirty_ = false;
}

//------------------------------------------------------------------------------
// stampEventGfx -- core dot drawing, reuses an already-open Graphics context
//------------------------------------------------------------------------------
void DrawingCanvas::stampEventGfx(juce::Graphics& cg, double t, double f,
    bool blurEnabled, float blurSecs) const noexcept
{
    const int W = getWidth(), H = getHeight();
    if (W < 1 || H < 1) return;

    const auto  pt = tfToXY(t, f);
    const float px = pt.x, py = pt.y;

    if (px < kML || px >= W || py < kMT || py >= H - kMB) return;

    const double logMin = std::log(FrequencyDrawerAudioProcessor::kFreqMin);
    const double logMax = std::log(FrequencyDrawerAudioProcessor::kFreqMax);
    const float  norm = static_cast<float>((std::log(f) - logMin) / (logMax - logMin));
    const juce::Colour col = juce::Colour::fromHSV(norm * 0.72f, 0.90f, 0.85f, 1.0f);

    if (blurEnabled && blurSecs > 0.001f)
    {
        const float pxPerSec = static_cast<float>(W - kML)
            / static_cast<float>(FrequencyDrawerAudioProcessor::kDuration);
        const float tailLen = juce::jmin(blurSecs * pxPerSec,
            static_cast<float>(W - 1) - px);
        if (tailLen > 1.0f)
        {
            juce::ColourGradient grad(col.withAlpha(0.55f), px, py,
                col.withAlpha(0.00f), px + tailLen, py, false);
            grad.addColour(0.25, col.withAlpha(0.28f));
            grad.addColour(0.50, col.withAlpha(0.12f));
            grad.addColour(0.75, col.withAlpha(0.04f));
            cg.setGradientFill(grad);
            cg.fillRect(px + 1.0f, py - 1.5f, tailLen, 3.0f);
        }
    }

    cg.setColour(col);
    cg.fillEllipse(px - 2.5f, py - 2.5f, 5.0f, 5.0f);
}

void DrawingCanvas::stampEvent(double t, double f, bool blurEnabled, float blurSecs)
{
    const int W = getWidth(), H = getHeight();
    if (W < 1 || H < 1) return;
    if (drawCache_.getWidth() != W || drawCache_.getHeight() != H || !drawCache_.isValid())
        drawCache_ = juce::Image(juce::Image::ARGB, W, H, true);
    juce::Graphics cg(drawCache_);
    stampEventGfx(cg, t, f, blurEnabled, blurSecs);
}

//------------------------------------------------------------------------------
// drawInterpPolyline
//------------------------------------------------------------------------------
void DrawingCanvas::drawInterpPolyline(juce::Graphics& g,
    const std::vector<std::pair<double, double>>& wps,
    float strokeWidth) const
{
    if (wps.size() < 2) return;
    juce::Path p;
    p.startNewSubPath(tfToXY(wps[0].first, wps[0].second));
    for (size_t i = 1; i < wps.size(); ++i)
        p.lineTo(tfToXY(wps[i].first, wps[i].second));
    g.strokePath(p, juce::PathStrokeType(strokeWidth));
}

//------------------------------------------------------------------------------
// Axes overlay
//------------------------------------------------------------------------------
void DrawingCanvas::drawGrid(juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    const int dL = kML, dT = kMT, dB = H - kMB;

    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, kML, H);
    g.fillRect(0, dB, W, kMB);

    g.setFont(juce::FontOptions(9.5f));

    static const double kRefFreqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (double rf : kRefFreqs)
    {
        const float fy = tfToXY(0.0, rf).y;
        g.setColour(juce::Colour(0x1affffff));
        g.drawHorizontalLine(static_cast<int>(fy),
            static_cast<float>(dL), static_cast<float>(W));
        g.setColour(juce::Colour(0xffaaaaaa));
        const juce::String lbl = rf >= 1000.0
            ? juce::String(static_cast<int>(rf / 1000)) + "k"
            : juce::String(static_cast<int>(rf));
        g.drawText(lbl, 0, static_cast<int>(fy - 6), kML - 4, 12,
            juce::Justification::centredRight, false);
    }

    for (int ts = 0; ts <= 30; ++ts)
    {
        const float tx = tfToXY(static_cast<double>(ts), 1000.0).x;
        const bool  major = (ts % 5 == 0);
        g.setColour(major ? juce::Colour(0x28ffffff) : juce::Colour(0x10ffffff));
        g.drawVerticalLine(static_cast<int>(tx),
            static_cast<float>(dT), static_cast<float>(dB));
        if (major)
        {
            g.setColour(juce::Colour(0xffaaaaaa));
            g.drawText(juce::String(ts) + "s",
                static_cast<int>(tx) - 12, dB + 3, 24, 14,
                juce::Justification::centred, false);
        }
    }

    g.setColour(juce::Colour(0xff777777));
    g.setFont(juce::FontOptions(8.5f));
    g.drawText("Hz", 1, kMT, kML - 4, 10, juce::Justification::right, false);

    g.setColour(juce::Colour(0xff555555));
    g.drawRect(dL, dT, W - dL, dB - dT, 1);
}

//------------------------------------------------------------------------------
// Paint
//------------------------------------------------------------------------------
void DrawingCanvas::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (cacheDirty_ || drawCache_.getWidth() != getWidth() || drawCache_.getHeight() != getHeight())
        rebuildCache();

    g.drawImageAt(drawCache_, 0, 0);
    drawGrid(g);

    // Playhead
    const double phSecs = proc_.getPlayheadSeconds();
    const float  phX = tfToXY(phSecs, 1000.0).x;
    g.setColour(juce::Colour(0xffff3333));
    g.drawLine(phX, static_cast<float>(kMT),
        phX, static_cast<float>(getHeight() - kMB), 1.5f);

    // Mode label
    g.setFont(juce::FontOptions(10.0f));
    g.setColour(juce::Colour(0x88ffffff));
    const juce::String modeStr = playheadMode_ ? " [PLAYHEAD]"
        : (interpMode_ ? " [INTERP]"
            : (drawMode_ ? " [DRAW]" : ""));
    g.drawText(modeStr, kML + 4, kMT + 2, 120, 14, juce::Justification::left, false);

    // Live interp stroke preview
    if (interpMode_ && currentInterpStroke_.size() >= 2)
    {
        const int nh = proc_.getNumHarmonics();
        double maxF = 0.0;
        for (const auto& wp : currentInterpStroke_)
            maxF = std::max(maxF, wp.second);

        for (int h = 1; h <= nh; ++h)
        {
            if (maxF * static_cast<double>(h) > FrequencyDrawerAudioProcessor::kFreqMax)
                break;
            const float alpha = (h == 1) ? 0.90f : (0.55f / static_cast<float>(h));
            g.setColour(juce::Colours::white.withAlpha(alpha));

            if (h == 1)
            {
                drawInterpPolyline(g, currentInterpStroke_, 1.5f);
            }
            else
            {
                std::vector<std::pair<double, double>> scaledWps;
                scaledWps.reserve(currentInterpStroke_.size());
                for (const auto& wp : currentInterpStroke_)
                    scaledWps.push_back({ wp.first, wp.second * static_cast<double>(h) });
                drawInterpPolyline(g, scaledWps, 1.0f);
            }
        }
    }

    // Rendering overlay
    if (proc_.getIsRendering())
    {
        g.setColour(juce::Colour(0xd0000000));
        g.fillAll();
        g.setFont(juce::FontOptions(22.0f));
        g.setColour(juce::Colours::white);
        g.drawText("Rendering...", getLocalBounds(), juce::Justification::centred, false);
    }
}

//------------------------------------------------------------------------------
// Mouse interaction
//------------------------------------------------------------------------------

void DrawingCanvas::addDrawPoint(int x, int y)
{
    if (proc_.getIsRendering()) return;

    const auto   tf = xyToTF(x, y);
    const double baseT = tf.x;
    const double baseF = snapFrequency(tf.y);

    // ---- INTERP mode: collect waypoints ----
    if (interpMode_)
    {
        if (currentInterpStroke_.empty())
        {
            currentInterpStroke_.push_back({ baseT, baseF });
        }
        else
        {
            const auto& last = currentInterpStroke_.back();
            const auto  lastPx = tfToXY(last.first, last.second);
            const auto  thisPx = tfToXY(baseT, baseF);
            if (lastPx.getDistanceFrom(thisPx) >= 2.0f)
                currentInterpStroke_.push_back({ baseT, baseF });
        }
        return;
    }

    // ---- DRAW mode ----
    const int nh = proc_.getNumHarmonics();

    double hsum = 0.0;
    for (int h = 1; h <= nh; ++h)
    {
        if (baseF * h > FrequencyDrawerAudioProcessor::kFreqMax) break;
        hsum += 1.0 / static_cast<double>(h);
    }
    if (hsum < 1.0e-10) return;

    // Visual: stamp all harmonics directly onto the cache
    {
        const int W = getWidth(), H = getHeight();
        if (W >= 1 && H >= 1)
        {
            if (drawCache_.getWidth() != W || drawCache_.getHeight() != H || !drawCache_.isValid())
                drawCache_ = juce::Image(juce::Image::ARGB, W, H, true);
            juce::Graphics cg(drawCache_);
            for (int h = 1; h <= nh; ++h)
            {
                const double hf = baseF * static_cast<double>(h);
                if (hf > FrequencyDrawerAudioProcessor::kFreqMax) break;
                stampEventGfx(cg, baseT, hf, showBlur_, blurSecs_);
                stampHistory_.push_back({ baseT, hf, showBlur_, blurSecs_ });
            }
        }
    }

    // Audio: buffer events with minimum time-gap filter
    const double kMinDt = FrequencyDrawerAudioProcessor::kDuration
        / static_cast<double>(juce::jmax(1, getWidth() - kML));

    if (baseT >= lastAudioEventTime_ + kMinDt || lastAudioEventTime_ < 0.0)
    {
        for (int h = 1; h <= nh; ++h)
        {
            const double hf = baseF * static_cast<double>(h);
            if (hf > FrequencyDrawerAudioProcessor::kFreqMax) break;
            const double amp = (0.25 / static_cast<double>(h)) / hsum;
            pendingAudioEvents_.push_back({ baseT, hf, amp, showBlur_, blurSecs_ });
        }
        lastAudioEventTime_ = baseT;
    }
}

void DrawingCanvas::mouseDown(const juce::MouseEvent& e)
{
    hasPrevDrag_ = false;
    lastAudioEventTime_ = -1.0;
    pendingAudioEvents_.clear();
    currentInterpStroke_.clear();

    if (playheadMode_)
    {
        const auto tf = xyToTF(e.x, e.y);
        if (onPlayheadMoved) onPlayheadMoved(tf.x);
        repaint();
        return;
    }

    if (drawMode_)
    {
        addDrawPoint(e.x, e.y);
        if (!interpMode_)
        {
            prevDrag_ = { e.x, e.y };
            hasPrevDrag_ = true;
        }
        repaint();
    }
}

void DrawingCanvas::mouseDrag(const juce::MouseEvent& e)
{
    if (playheadMode_)
    {
        const auto tf = xyToTF(e.x, e.y);
        if (onPlayheadMoved) onPlayheadMoved(tf.x);
        repaint();
        return;
    }

    if (drawMode_)
    {
        if (interpMode_)
        {
            addDrawPoint(e.x, e.y);
            repaint();
        }
        else
        {
            if (hasPrevDrag_)
            {
                const int dx = e.x - prevDrag_.x;
                const int dy = e.y - prevDrag_.y;
                const int steps = juce::jmin(48, juce::jmax(1,
                    juce::jmax(std::abs(dx), std::abs(dy))));
                for (int i = 1; i <= steps; ++i)
                {
                    const float frac = static_cast<float>(i) / static_cast<float>(steps);
                    addDrawPoint(prevDrag_.x + static_cast<int>(frac * dx),
                        prevDrag_.y + static_cast<int>(frac * dy));
                }
            }
            else
            {
                addDrawPoint(e.x, e.y);
            }
            prevDrag_ = { e.x, e.y };
            hasPrevDrag_ = true;
            repaint();
        }
    }
}

void DrawingCanvas::mouseUp(const juce::MouseEvent&)
{
    hasPrevDrag_ = false;
    if (!drawMode_) return;

    if (interpMode_)
    {
        if (currentInterpStroke_.size() >= 2)
        {
            const int nh = proc_.getNumHarmonics();

            interpPaths_.push_back({ currentInterpStroke_, nh, showBlur_, blurSecs_ });
            cacheDirty_ = true;

            DrawnPath path;
            path.waypoints = currentInterpStroke_;
            path.numHarmonics = nh;
            path.amplitude = 0.25;
            path.blurEnabled = showBlur_;
            path.blurSecs = blurSecs_;

            if (onPathAdded) onPathAdded(std::move(path));
        }
    }
    else
    {
        if (!pendingAudioEvents_.empty() && onEventsCommitted)
            onEventsCommitted(std::move(pendingAudioEvents_));
    }

    pendingAudioEvents_.clear();
    lastAudioEventTime_ = -1.0;
    currentInterpStroke_.clear();

    if (onStrokeFinished) onStrokeFinished();
    commitCurrentImage();
}

//==============================================================================
//  FrequencyDrawerAudioProcessorEditor
//==============================================================================

FrequencyDrawerAudioProcessorEditor::FrequencyDrawerAudioProcessorEditor(
    FrequencyDrawerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor_(p), canvas_(p)
{
    setSize(1020, 660);
    setResizable(true, true);
    setResizeLimits(800, 480, 2400, 1600);

    //--------------------------------------------------------------------------
    // Register child components
    //--------------------------------------------------------------------------

    auto addToggle = [&](juce::TextButton& btn, juce::Colour onColour)
        {
            addAndMakeVisible(btn);
            btn.setClickingTogglesState(true);
            btn.setColour(juce::TextButton::buttonOnColourId, onColour);
        };

    addToggle(btnPlayheadMode, juce::Colour(0xffcc7722));
    addToggle(btnDraw, juce::Colour(0xff2266ee));
    addToggle(btnInterp, juce::Colour(0xff229966));

    addAndMakeVisible(btnPlay);
    addAndMakeVisible(btnPause);
    addAndMakeVisible(btnClear);
    addAndMakeVisible(btnExport);

    btnDraw.setToggleState(true, juce::dontSendNotification);

    //--------------------------------------------------------------------------
    // Harmonics slider
    //--------------------------------------------------------------------------
    addAndMakeVisible(lblHarmonics);
    addAndMakeVisible(sldHarmonics);
    sldHarmonics.setSliderStyle(juce::Slider::LinearHorizontal);
    sldHarmonics.setRange(1.0, 64.0, 1.0);
    sldHarmonics.setValue(1.0, juce::dontSendNotification);
    sldHarmonics.setTextBoxStyle(juce::Slider::TextBoxRight, false, 28, 20);

    //--------------------------------------------------------------------------
    // Blur strength slider
    //--------------------------------------------------------------------------
    addAndMakeVisible(lblBlurStrength);
    addAndMakeVisible(sldBlurStrength);
    sldBlurStrength.setSliderStyle(juce::Slider::LinearHorizontal);
    sldBlurStrength.setRange(0.0, 10.0, 0.05);
    sldBlurStrength.setValue(0.0, juce::dontSendNotification);
    sldBlurStrength.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 20);

    //--------------------------------------------------------------------------
    // Scale combobox
    //--------------------------------------------------------------------------
    addAndMakeVisible(lblScale);
    addAndMakeVisible(cmbScale);
    cmbScale.addItem("No Snapping", 1);
    cmbScale.addItem("C Major", 2);
    cmbScale.addItem("C Minor", 3);
    cmbScale.addItem("C Pent. Major", 4);
    cmbScale.setSelectedId(1, juce::dontSendNotification);

    //--------------------------------------------------------------------------
    // Engine combobox (top-right)
    //   Item IDs map directly to processor engine modes:
    //     1 = Engine 1  (incremental WaypointOscillator, supports Interp paths)
    //     2 = Engine 2  (full re-render, simpler decay -- default)
    //--------------------------------------------------------------------------
    addAndMakeVisible(lblEngine);
    addAndMakeVisible(cmbEngine);
    cmbEngine.addItem("Engine 1", 1);
    cmbEngine.addItem("Engine 2", 2);
    // Default: Engine 2 (item ID 2, processor mode 1)
    cmbEngine.setSelectedId(2, juce::dontSendNotification);

    addAndMakeVisible(canvas_);

    //--------------------------------------------------------------------------
    // Button callbacks
    //--------------------------------------------------------------------------

    btnDraw.onClick = [this]
        {
            isDrawMode_ = btnDraw.getToggleState();
            isInterpMode_ = false;
            isPlayheadMode_ = false;
            btnPlayheadMode.setToggleState(false, juce::dontSendNotification);
            btnInterp.setToggleState(false, juce::dontSendNotification);
            canvas_.setDrawMode(isDrawMode_);
            canvas_.setPlayheadMode(false);
            canvas_.setInterpMode(false);
        };

    btnInterp.onClick = [this]
        {
            isInterpMode_ = btnInterp.getToggleState();
            isDrawMode_ = isInterpMode_;
            isPlayheadMode_ = false;
            btnPlayheadMode.setToggleState(false, juce::dontSendNotification);
            if (isInterpMode_)
                btnDraw.setToggleState(false, juce::dontSendNotification);
            canvas_.setDrawMode(isDrawMode_);
            canvas_.setPlayheadMode(false);
            canvas_.setInterpMode(isInterpMode_);
        };

    btnPlayheadMode.onClick = [this]
        {
            isPlayheadMode_ = btnPlayheadMode.getToggleState();
            isDrawMode_ = false;
            isInterpMode_ = false;
            btnDraw.setToggleState(false, juce::dontSendNotification);
            btnInterp.setToggleState(false, juce::dontSendNotification);
            canvas_.setDrawMode(false);
            canvas_.setPlayheadMode(isPlayheadMode_);
            canvas_.setInterpMode(false);
        };

    btnPlay.onClick = [this] { audioProcessor_.setPlaying(true);  };
    btnPause.onClick = [this] { audioProcessor_.setPlaying(false); };

    btnClear.onClick = [this]
        {
            audioProcessor_.clearAllEvents();
            canvas_.clearHistory();
        };

    sldBlurStrength.onValueChange = [this]
        {
            const float v = static_cast<float>(sldBlurStrength.getValue());
            audioProcessor_.setBlurStrength(v);
            canvas_.setBlurViz(v > 0.001f, v);
        };

    sldHarmonics.onValueChange = [this]
        {
            audioProcessor_.setNumHarmonics(static_cast<int>(sldHarmonics.getValue()));
        };

    cmbScale.onChange = [this]
        {
            canvas_.setScaleMode(cmbScale.getSelectedId() - 1);
        };

    // Engine switch: item ID 1 -> processor mode 0 (Engine 1)
    //                item ID 2 -> processor mode 1 (Engine 2)
    cmbEngine.onChange = [this]
        {
            const int processorMode = cmbEngine.getSelectedId() - 1;  // 0 or 1
            audioProcessor_.setEngineMode(processorMode);
        };

    btnExport.onClick = [this]
        {
            fileChooser_ = std::make_unique<juce::FileChooser>(
                "Export as FLAC",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                "*.flac");

            const int flags = juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles;

            fileChooser_->launchAsync(flags, [this](const juce::FileChooser& fc)
                {
                    const auto results = fc.getResults();
                    if (results.isEmpty()) return;

                    const auto file = results[0].withFileExtension("flac");

                    juce::Thread::launch([this, file]
                        {
                            const bool ok = audioProcessor_.exportToFlac(file);
                            juce::MessageManager::callAsync([ok, file]
                                {
                                    juce::AlertWindow::showMessageBoxAsync(
                                        ok ? juce::AlertWindow::InfoIcon
                                        : juce::AlertWindow::WarningIcon,
                                        "Export FLAC",
                                        ok ? "Exported to:\n" + file.getFullPathName()
                                        : "Export failed -- check JUCE_USE_FLAC=1 and file permissions.");
                                });
                        });
                });
        };

    //--------------------------------------------------------------------------
    // Canvas callbacks
    //--------------------------------------------------------------------------

    canvas_.onEventsCommitted = [this](std::vector<DrawnEvent> events)
        {
            audioProcessor_.addEvents(std::move(events));
        };

    canvas_.onPathAdded = [this](DrawnPath path)
        {
            audioProcessor_.addPath(std::move(path));
        };

    canvas_.onStrokeFinished = [this]
        {
            audioProcessor_.triggerBackgroundRender();
        };

    canvas_.onPlayheadMoved = [this](double t)
        {
            audioProcessor_.requestSeek(t);
        };

    startTimerHz(30);
}

FrequencyDrawerAudioProcessorEditor::~FrequencyDrawerAudioProcessorEditor()
{
    stopTimer();
}

//------------------------------------------------------------------------------

void FrequencyDrawerAudioProcessorEditor::timerCallback()
{
    const bool   rendering = audioProcessor_.getIsRendering();
    const double pos = audioProcessor_.getPlayheadSeconds();

    if (rendering != prevRendering_ || std::abs(pos - prevPlayheadPos_) > 0.0005)
    {
        prevPlayheadPos_ = pos;
        prevRendering_ = rendering;
        canvas_.repaint();
    }

    btnPlay.setEnabled(!rendering);
    btnPause.setEnabled(!rendering);
    btnClear.setEnabled(!rendering);
    btnExport.setEnabled(!rendering);

    if (audioProcessor_.getIsPlaying())
        btnPlay.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a6a2a));
    else
        btnPlay.removeColour(juce::TextButton::buttonColourId);
}

void FrequencyDrawerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

//------------------------------------------------------------------------------
// resized -- single toolbar row
//
//  Engine combobox + label are placed first via removeFromRight() so they
//  always sit flush against the right edge regardless of window width.
//------------------------------------------------------------------------------
void FrequencyDrawerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    auto bar = area.removeFromTop(30).reduced(4, 2);

    auto placeBtn = [](juce::Rectangle<int>& row, juce::Component& c, int w)
        {
            c.setBounds(row.removeFromLeft(w).reduced(2, 1));
        };
    auto placeLabel = [](juce::Rectangle<int>& row, juce::Component& c, int w)
        {
            c.setBounds(row.removeFromLeft(w).reduced(2, 4));
        };

    // ---- Engine selector -- anchored to the far right ----
    cmbEngine.setBounds(bar.removeFromRight(100).reduced(2, 2));
    lblEngine.setBounds(bar.removeFromRight(46).reduced(2, 4));

    // ---- Left-to-right controls ----

    // Mode buttons
    placeBtn(bar, btnPlayheadMode, 78);
    placeBtn(bar, btnDraw, 56);
    placeBtn(bar, btnInterp, 56);
    bar.removeFromLeft(6);

    // Transport
    placeBtn(bar, btnPlay, 68);
    placeBtn(bar, btnPause, 68);
    placeBtn(bar, btnClear, 56);
    bar.removeFromLeft(6);

    // Export
    placeBtn(bar, btnExport, 94);
    bar.removeFromLeft(8);

    // Harmonics
    placeLabel(bar, lblHarmonics, 38);
    sldHarmonics.setBounds(bar.removeFromLeft(80).reduced(2, 2));
    bar.removeFromLeft(6);

    // Blur
    placeLabel(bar, lblBlurStrength, 46);
    sldBlurStrength.setBounds(bar.removeFromLeft(88).reduced(2, 2));
    bar.removeFromLeft(6);

    // Scale
    placeLabel(bar, lblScale, 36);
    cmbScale.setBounds(bar.removeFromLeft(130).reduced(2, 2));

    canvas_.setBounds(area.reduced(4, 4));
}