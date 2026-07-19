/*
  ==============================================================================
    FrequencyDrawer — PluginEditor.cpp
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
    const float drawW = static_cast<float>(getWidth()  - kML);
    const float drawH = static_cast<float>(getHeight() - kMB - kMT);

    const float x = kML + static_cast<float>(t / FrequencyDrawerAudioProcessor::kDuration) * drawW;

    const double logMin = std::log(FrequencyDrawerAudioProcessor::kFreqMin);
    const double logMax = std::log(FrequencyDrawerAudioProcessor::kFreqMax);
    const float  norm   = static_cast<float>((std::log(f) - logMin) / (logMax - logMin));
    const float  y      = kMT + (1.0f - norm) * drawH;

    return { x, y };
}

juce::Point<double> DrawingCanvas::xyToTF(int x, int y) const noexcept
{
    const double drawW = getWidth()  - kML;
    const double drawH = getHeight() - kMB - kMT;

    const double t = juce::jlimit(0.0, FrequencyDrawerAudioProcessor::kDuration,
                                  (x - kML) / drawW * FrequencyDrawerAudioProcessor::kDuration);

    const double norm   = juce::jlimit(0.0, 1.0, 1.0 - (y - kMT) / drawH);
    const double logMin = std::log(FrequencyDrawerAudioProcessor::kFreqMin);
    const double logMax = std::log(FrequencyDrawerAudioProcessor::kFreqMax);
    const double f      = std::exp(logMin + norm * (logMax - logMin));

    return { t, f };
}

//------------------------------------------------------------------------------
// Cache management
//------------------------------------------------------------------------------

void DrawingCanvas::resized() { cacheDirty_ = true; }

void DrawingCanvas::stampEvent(double t, double f)
{
    const int W = getWidth(), H = getHeight();
    if (W < 1 || H < 1) return;

    if (drawCache_.getWidth() != W || drawCache_.getHeight() != H || !drawCache_.isValid())
        drawCache_ = juce::Image(juce::Image::ARGB, W, H, true);

    const auto pt = tfToXY(t, f);
    const float px = pt.x, py = pt.y;

    if (px < kML || px >= W || py < kMT || py >= H - kMB) return;

    // Colour based on log-frequency position (red → orange → yellow → green → cyan → blue)
    const double logMin = std::log(FrequencyDrawerAudioProcessor::kFreqMin);
    const double logMax = std::log(FrequencyDrawerAudioProcessor::kFreqMax);
    const float  norm   = static_cast<float>((std::log(f) - logMin) / (logMax - logMin));
    const juce::Colour col = juce::Colour::fromHSV(norm * 0.72f, 0.90f, 0.85f, 1.0f);

    juce::Graphics cg(drawCache_);

    // Blur tail — gradient rectangle, approximate exp(-5x) with colour stops
    if (showBlur_ && blurSecs_ > 0.05f)
    {
        const float pxPerSec = static_cast<float>(W - kML)
                             / static_cast<float>(FrequencyDrawerAudioProcessor::kDuration);
        const float tailLen  = juce::jmin(blurSecs_ * pxPerSec,
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

    // Event dot
    cg.setColour(col);
    cg.fillEllipse(px - 2.5f, py - 2.5f, 5.0f, 5.0f);
}

void DrawingCanvas::rebuildCache()
{
    const int W = juce::jmax(1, getWidth()), H = juce::jmax(1, getHeight());
    drawCache_ = juce::Image(juce::Image::ARGB, W, H, true);
    for (const auto& evt : proc_.getEventsCopy())
        stampEvent(evt.time, evt.frequency);
    cacheDirty_ = false;
}

//------------------------------------------------------------------------------
// Axes overlay
//------------------------------------------------------------------------------

void DrawingCanvas::drawGrid(juce::Graphics& g)
{
    const int W = getWidth(), H = getHeight();
    const int dL = kML, dT = kMT, dB = H - kMB;

    // Wipe margin strips so labels are always clean
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, kML, H);
    g.fillRect(0, dB, W, kMB);

    g.setFont(juce::FontOptions(9.5f));

    // ---- Horizontal frequency lines ----
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

    // ---- Vertical time lines ----
    for (int ts = 0; ts <= 30; ++ts)
    {
        const float tx    = tfToXY(static_cast<double>(ts), 1000.0).x;
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
    // Drawing area background
    g.fillAll(juce::Colours::white);

    // Rebuild cached event image if stale
    if (cacheDirty_ || drawCache_.getWidth() != getWidth() || drawCache_.getHeight() != getHeight())
        rebuildCache();

    g.drawImageAt(drawCache_, 0, 0);

    // Axes overlay (dark margins + grid)
    drawGrid(g);

    // Playhead — bright red vertical line
    const double phSecs = proc_.getPlayheadSeconds();
    const float  phX    = tfToXY(phSecs, 1000.0).x;
    g.setColour(juce::Colour(0xffff3333));
    g.drawLine(phX, static_cast<float>(kMT),
               phX, static_cast<float>(getHeight() - kMB), 1.5f);

    // Mode indicator text
    g.setFont(juce::FontOptions(10.0f));
    g.setColour(juce::Colour(0x88ffffff));
    const juce::String modeStr = playheadMode_ ? " [PLAYHEAD]"
                                : (drawMode_   ? " [DRAW]"    : "");
    g.drawText(modeStr, kML + 4, kMT + 2, 100, 14, juce::Justification::left, false);

    // ---- "Rendering…" overlay ----
    // Drawn last so it covers everything; also blocks visual draw feedback.
    if (proc_.getIsRendering())
    {
        // Dark semi-transparent veil
        g.setColour(juce::Colour(0xd0000000));
        g.fillAll();

        // Pulsing "Rendering…" label
        g.setFont(juce::FontOptions(22.0f));
        g.setColour(juce::Colours::white);
        g.drawText("Rendering\xe2\x80\xa6",   // "Rendering…"
                   getLocalBounds(),
                   juce::Justification::centred, false);
    }
}

//------------------------------------------------------------------------------
// Mouse interaction
//------------------------------------------------------------------------------

/** Stamps the fundamental + all harmonic overtones at pixel (x, y). */
void DrawingCanvas::addDrawPoint(int x, int y)
{
    // Block drawing while the audio engine is rendering
    if (proc_.getIsRendering()) return;

    const auto tf       = xyToTF(x, y);
    const double baseT  = tf.x;
    const double baseF  = tf.y;
    const int    nh     = proc_.getNumHarmonics();

    // Pre-compute normalisation so the loudness of each draw-point is constant
    // regardless of how many harmonics are active.
    double hsum = 0.0;
    for (int h = 1; h <= nh; ++h)
    {
        if (baseF * h > FrequencyDrawerAudioProcessor::kFreqMax) break;
        hsum += 1.0 / static_cast<double>(h);
    }
    if (hsum < 1.0e-10) return;

    for (int h = 1; h <= nh; ++h)
    {
        const double hf  = baseF * h;
        if (hf > FrequencyDrawerAudioProcessor::kFreqMax) break;
        const double amp = (0.25 / h) / hsum;

        stampEvent(baseT, hf);

        if (onEventAdded) onEventAdded(baseT, hf, amp);
    }
}

void DrawingCanvas::mouseDown(const juce::MouseEvent& e)
{
    hasPrevDrag_ = false;

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
        prevDrag_    = { e.x, e.y };
        hasPrevDrag_ = true;
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
        if (hasPrevDrag_)
        {
            // Interpolate between the previous and current positions so that
            // fast mouse moves don't leave gaps.
            const int dx    = e.x - prevDrag_.x;
            const int dy    = e.y - prevDrag_.y;
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

        prevDrag_    = { e.x, e.y };
        hasPrevDrag_ = true;
        repaint();
    }
}

void DrawingCanvas::mouseUp(const juce::MouseEvent&)
{
    hasPrevDrag_ = false;

    // Trigger a background audio render whenever the user finishes a stroke.
    if (drawMode_)
    {
        if (onStrokeFinished) onStrokeFinished();
    }
}

//==============================================================================
//  FrequencyDrawerAudioProcessorEditor
//==============================================================================

FrequencyDrawerAudioProcessorEditor::FrequencyDrawerAudioProcessorEditor(
        FrequencyDrawerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor_(p), canvas_(p)
{
    setSize(960, 640);
    setResizable(true, true);
    setResizeLimits(720, 440, 2400, 1600);

    //--------------------------------------------------------------------------
    // Register all child components
    //--------------------------------------------------------------------------

    auto addToggle = [&](juce::TextButton& btn, juce::Colour onColour)
    {
        addAndMakeVisible(btn);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonOnColourId, onColour);
    };

    addToggle(btnPlayheadMode, juce::Colour(0xffcc7722));
    addToggle(btnDraw,         juce::Colour(0xff2266ee));
    addToggle(btnBlur,         juce::Colour(0xff882299));

    addAndMakeVisible(btnPlay);
    addAndMakeVisible(btnPause);
    addAndMakeVisible(btnClear);
    addAndMakeVisible(btnExport);

    btnDraw.setToggleState(true, juce::dontSendNotification);

    //--------------------------------------------------------------------------
    // Harmonics slider — now controls the *drawing* mode, range 1–64
    //--------------------------------------------------------------------------
    addAndMakeVisible(lblHarmonics);
    addAndMakeVisible(sldHarmonics);
    sldHarmonics.setSliderStyle(juce::Slider::LinearHorizontal);
    sldHarmonics.setRange(1.0, 64.0, 1.0);
    sldHarmonics.setValue(1.0, juce::dontSendNotification);
    sldHarmonics.setTextBoxStyle(juce::Slider::TextBoxRight, false, 28, 20);

    //--------------------------------------------------------------------------
    // Blur strength slider  (0.05–10 s)
    //--------------------------------------------------------------------------
    addAndMakeVisible(lblBlurStrength);
    addAndMakeVisible(sldBlurStrength);
    sldBlurStrength.setSliderStyle(juce::Slider::LinearHorizontal);
    sldBlurStrength.setRange(0.05, 10.0, 0.05);
    sldBlurStrength.setValue(1.0, juce::dontSendNotification);
    sldBlurStrength.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 20);
    sldBlurStrength.setEnabled(false);

    addAndMakeVisible(canvas_);

    //--------------------------------------------------------------------------
    // Button callbacks
    //--------------------------------------------------------------------------

    btnDraw.onClick = [this]
    {
        isDrawMode_     = btnDraw.getToggleState();
        isPlayheadMode_ = false;
        btnPlayheadMode.setToggleState(false, juce::dontSendNotification);
        canvas_.setDrawMode(isDrawMode_);
        canvas_.setPlayheadMode(false);
    };

    btnPlayheadMode.onClick = [this]
    {
        isPlayheadMode_ = btnPlayheadMode.getToggleState();
        isDrawMode_     = false;
        btnDraw.setToggleState(false, juce::dontSendNotification);
        canvas_.setDrawMode(false);
        canvas_.setPlayheadMode(isPlayheadMode_);
    };

    btnPlay.onClick  = [this] { audioProcessor_.setPlaying(true);  };
    btnPause.onClick = [this] { audioProcessor_.setPlaying(false); };

    btnClear.onClick = [this]
    {
        audioProcessor_.clearAllEvents();
        canvas_.invalidateCache();
    };

    btnBlur.onClick = [this]
    {
        isBlurMode_ = btnBlur.getToggleState();
        audioProcessor_.setBlurEnabled(isBlurMode_);
        sldBlurStrength.setEnabled(isBlurMode_);
        canvas_.setBlurViz(isBlurMode_, static_cast<float>(sldBlurStrength.getValue()));
    };

    sldBlurStrength.onValueChange = [this]
    {
        audioProcessor_.setBlurStrength(static_cast<float>(sldBlurStrength.getValue()));
        if (isBlurMode_)
            canvas_.setBlurViz(true, static_cast<float>(sldBlurStrength.getValue()));
    };

    // Harmonics slider now only configures drawing; no audio-engine call needed.
    sldHarmonics.onValueChange = [this]
    {
        audioProcessor_.setNumHarmonics(static_cast<int>(sldHarmonics.getValue()));
    };

    // Export as FLAC with maximum compression
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

            // renderOffline may take a few seconds — run on a background thread
            juce::Thread::launch([this, file]
            {
                const bool ok = audioProcessor_.exportToFlac(file);
                juce::MessageManager::callAsync([ok, file]
                {
                    juce::AlertWindow::showMessageBoxAsync(
                        ok ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                        "Export FLAC",
                        ok ? "Exported to:\n" + file.getFullPathName()
                           : "Export failed — check JUCE_USE_FLAC=1 and file permissions.");
                });
            });
        });
    };

    //--------------------------------------------------------------------------
    // Canvas callbacks
    //--------------------------------------------------------------------------

    // Each event drawn (one per harmonic) is forwarded to the processor.
    canvas_.onEventAdded = [this](double t, double f, double amp)
    {
        audioProcessor_.addEvent(t, f, amp);
    };

    // After a stroke is finished, start a background render.
    canvas_.onStrokeFinished = [this]
    {
        audioProcessor_.triggerBackgroundRender();
    };

    canvas_.onPlayheadMoved = [this](double t)
    {
        audioProcessor_.requestSeek(t);
    };

    startTimerHz(30);   // 30 fps for playhead + rendering-state refresh
}

FrequencyDrawerAudioProcessorEditor::~FrequencyDrawerAudioProcessorEditor()
{
    stopTimer();
}

//------------------------------------------------------------------------------

void FrequencyDrawerAudioProcessorEditor::timerCallback()
{
    const bool rendering = audioProcessor_.getIsRendering();

    // Repaint canvas when rendering state changes or playhead moves
    const double pos = audioProcessor_.getPlayheadSeconds();
    if (rendering != prevRendering_
        || std::abs(pos - prevPlayheadPos_) > 0.0005)
    {
        prevPlayheadPos_ = pos;
        prevRendering_   = rendering;
        canvas_.repaint();
    }

    // Disable transport + clear controls while rendering so the user knows
    // not to interact with them.
    btnPlay.setEnabled(!rendering);
    btnPause.setEnabled(!rendering);
    btnClear.setEnabled(!rendering);
    btnExport.setEnabled(!rendering);

    // Green tint on play button while running
    if (audioProcessor_.getIsPlaying())
        btnPlay.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a6a2a));
    else
        btnPlay.removeColour(juce::TextButton::buttonColourId);
}

void FrequencyDrawerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void FrequencyDrawerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    //--------------------------------------------------------------------------
    // Top bar
    //--------------------------------------------------------------------------
    auto bar = area.removeFromTop(54).reduced(4, 5);

    auto placeBtn = [&](juce::Component& c, int w)
    {
        c.setBounds(bar.removeFromLeft(w).reduced(2, 0));
    };

    placeBtn(btnPlayheadMode, 80);
    placeBtn(btnDraw,         66);
    placeBtn(btnPlay,         76);
    placeBtn(btnPause,        78);
    placeBtn(btnClear,        60);

    bar.removeFromLeft(10);   // spacer

    lblHarmonics.setBounds(bar.removeFromLeft(70).reduced(2, 8));
    sldHarmonics.setBounds(bar.removeFromLeft(100).reduced(2, 4));

    bar.removeFromLeft(10);   // spacer

    placeBtn(btnBlur, 60);
    lblBlurStrength.setBounds(bar.removeFromLeft(56).reduced(2, 8));
    sldBlurStrength.setBounds(bar.removeFromLeft(112).reduced(2, 4));

    bar.removeFromLeft(14);   // spacer

    placeBtn(btnExport, 110);

    //--------------------------------------------------------------------------
    // Drawing canvas fills the rest
    //--------------------------------------------------------------------------
    canvas_.setBounds(area.reduced(4, 4));
}