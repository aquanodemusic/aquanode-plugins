#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

//=============================================================================
// Colour helpers
//=============================================================================
static juce::Colour spectralColour(float v)
{
    v = juce::jlimit(0.0f, 1.0f, v);
    const uint8_t r = (uint8_t)(255 - (int)(240 * v));
    const uint8_t g = (uint8_t)(255 - (int)(175 * v));
    const uint8_t b = (uint8_t)(255 - (int)(55 * v));
    return juce::Colour(r, g, b);
}

//=============================================================================
// Log-frequency scale helpers
//   The Y axis maps linearly from 0 px (top = Nyquist) to H px (bottom).
//   The frequency mapping uses a logarithmic scale with a 20 Hz floor so
//   the musically important 20–2000 Hz band gets the bulk of the display.
//=============================================================================
static constexpr float kLogFreqMin = 20.0f;   // Hz – bottom anchor of log scale

/** Map a frequency [kLogFreqMin, nyquist] to a normalised 0..1 (0=bottom, 1=top). */
static float freqToLogT(float hz, float nyquist)
{
    hz = std::max(hz, kLogFreqMin);
    nyquist = std::max(nyquist, kLogFreqMin + 1.0f);
    return std::log(hz / kLogFreqMin) / std::log(nyquist / kLogFreqMin);
}

/** Map a normalised t ∈ [0,1] back to a frequency (Hz). */
static float logTToFreq(float t, float nyquist)
{
    nyquist = std::max(nyquist, kLogFreqMin + 1.0f);
    return kLogFreqMin * std::pow(nyquist / kLogFreqMin,
        juce::jlimit(0.0f, 1.0f, t));
}

//=============================================================================
// SpectrogramView
//=============================================================================

SpectrogramView::SpectrogramView(SpectralEditProcessor& p) : proc(p)
{
    setOpaque(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

//------------------------------------------------------------------------------
void SpectrogramView::setEditMode(int m) noexcept
{
    // Stop live scrub audio if leaving scrub mode
    if (editMode == 3 && m != 3)
        proc.scrubActive.store(false);

    editMode = m;
    repaint();
}

//------------------------------------------------------------------------------
void SpectrogramView::setZoom(float x, float y) noexcept
{
    zoomX = juce::jlimit(1.0f, 8.0f, x);
    zoomY = juce::jlimit(1.0f, 8.0f, y);

    const int H = getHeight();
    const int srcH = juce::jlimit(1, std::max(1, H), (int)(H / zoomY));
    vScrollOffset = juce::jlimit(0, std::max(0, H - srcH), vScrollOffset);
    repaint();
}

//------------------------------------------------------------------------------
// Coordinate helpers – log-scaled Y axis
//------------------------------------------------------------------------------

int SpectrogramView::pxToFrame(int px) const noexcept
{
    const int W = getWidth();
    if (W <= 0) return scrollOffset;
    // Mirror the srcW clamping that paint() applies in drawImage.
    // When the whole image fits (nF < W/zoomX), srcW is clamped to nF and
    // the image is stretched to fill W – the effective scale is W/srcW, not zoomX.
    const int nF = imgValid ? cachedNumFrames : 0;
    const int srcW = (nF > 0)
        ? juce::jlimit(1, nF, (int)((float)W / zoomX))
        : std::max(1, (int)((float)W / zoomX));
    return scrollOffset + (int)((float)px * (float)srcW / (float)W);
}

int SpectrogramView::frameToPx(int f) const noexcept
{
    const int W = getWidth();
    if (W <= 0) return 0;
    const int nF = imgValid ? cachedNumFrames : 0;
    const int srcW = (nF > 0)
        ? juce::jlimit(1, nF, (int)((float)W / zoomX))
        : std::max(1, (int)((float)W / zoomX));
    return (int)((float)(f - scrollOffset) * (float)W / (float)srcW);
}

/** Screen pixel py → spectral bin index (log-scaled). */
int SpectrogramView::pyToBin(int py) const noexcept
{
    const int   H = getHeight();
    const int   NB = proc.getNumBins();
    const float nyq = (float)(proc.sourceSampleRate > 0
        ? proc.sourceSampleRate * 0.5 : 22050.0);
    if (H <= 1 || NB <= 1) return 0;

    // Map screen pixel to image row (accounting for vScroll/vZoom)
    const float imgRow = (float)vScrollOffset + (float)py / zoomY;
    // t in [0,1]: 0 = image bottom (low freq), 1 = image top (Nyquist)
    const float t = 1.0f - imgRow / (float)(H - 1);
    // Log-scale t to frequency
    const float hz = logTToFreq(t, nyq);
    const int   b = (int)(hz * (float)(NB - 1) / nyq);
    return juce::jlimit(0, NB - 1, b);
}

/** Spectral bin index → screen pixel (log-scaled). */
int SpectrogramView::binToPy(int b) const noexcept
{
    const int   H = getHeight();
    const int   NB = proc.getNumBins();
    const float nyq = (float)(proc.sourceSampleRate > 0
        ? proc.sourceSampleRate * 0.5 : 22050.0);
    if (H <= 1 || NB <= 1) return 0;

    const float hz = (float)b / (float)(NB - 1) * nyq;
    const float t = freqToLogT(hz, nyq);                    // 0..1
    const float imgRow = (1.0f - t) * (float)(H - 1);           // image row
    const float py = (imgRow - (float)vScrollOffset) * zoomY;
    return juce::jlimit(0, H - 1, (int)py);
}

//------------------------------------------------------------------------------
void SpectrogramView::rebuildImage()
{
    std::lock_guard<std::mutex> lk(proc.spectralMutex);

    const int   nF = proc.numFrames;
    const int   NB = proc.getNumBins();
    const int   H = getHeight();
    const float nyq = (float)(proc.sourceSampleRate > 0
        ? proc.sourceSampleRate * 0.5 : 22050.0);

    if (nF == 0 || H <= 0)
    {
        img = juce::Image();
        imgValid = false;
        cachedNumFrames = 0;
        cachedImgHeight = 0;
        repaint();
        return;
    }

    cachedNumFrames = nF;
    cachedImgHeight = H;

    // Compute max magnitude for normalisation
    float maxMag2 = 1e-20f;
    for (int f = 0; f < nF; ++f)
        for (int b = 0; b < NB; ++b)
        {
            const auto& c = proc.spectralData[f][b];
            const float m2 = c.real() * c.real() + c.imag() * c.imag();
            if (m2 > maxMag2) maxMag2 = m2;
        }
    cachedInvMax = 1.0f / std::sqrt(maxMag2);

    img = juce::Image(juce::Image::RGB, nF, H, false);
    juce::Image::BitmapData bmp(img, juce::Image::BitmapData::writeOnly);

    for (int px = 0; px < nF; ++px)
    {
        const auto& frameData = proc.spectralData[px];
        for (int py = 0; py < H; ++py)
        {
            // Log-scaled: map image row → frequency → bin
            const float t = 1.0f - (float)py / (float)(H - 1);
            const float hz = logTToFreq(t, nyq);
            const int   bn = juce::jlimit(0, NB - 1,
                (int)(hz * (float)(NB - 1) / nyq));

            const auto& c = frameData[bn];
            const float  mag = std::sqrt(c.real() * c.real() + c.imag() * c.imag())
                * cachedInvMax;
            const float  v = std::sqrt(juce::jlimit(0.0f, 1.0f, mag));
            bmp.setPixelColour(px, py, spectralColour(v));
        }
    }

    imgValid = true;
    repaint();
}

/** Rebuild only the image columns [f0, f1] using the cached normalisation.
 *  Called during brush strokes for fast visual feedback. */
void SpectrogramView::rebuildImageColumns(int f0, int f1)
{
    if (!imgValid || !img.isValid()) return;

    std::lock_guard<std::mutex> lk(proc.spectralMutex);

    const int   nF = proc.numFrames;
    const int   NB = proc.getNumBins();
    const int   H = img.getHeight();
    const float nyq = (float)(proc.sourceSampleRate > 0
        ? proc.sourceSampleRate * 0.5 : 22050.0);

    f0 = std::max(0, f0);
    f1 = std::min(nF - 1, f1);
    if (f0 > f1) return;

    juce::Image::BitmapData bmp(img, juce::Image::BitmapData::readWrite);

    for (int px = f0; px <= f1; ++px)
    {
        const auto& frameData = proc.spectralData[px];
        for (int py = 0; py < H; ++py)
        {
            const float t = 1.0f - (float)py / (float)(H - 1);
            const float hz = logTToFreq(t, nyq);
            const int   bn = juce::jlimit(0, NB - 1,
                (int)(hz * (float)(NB - 1) / nyq));

            const auto& c = frameData[bn];
            const float  mag = std::sqrt(c.real() * c.real() + c.imag() * c.imag())
                * cachedInvMax;
            const float  v = std::sqrt(juce::jlimit(0.0f, 1.0f, mag));
            bmp.setPixelColour(px, py, spectralColour(v));
        }
    }
}

void SpectrogramView::resized()
{
    rebuildImage();
}

//------------------------------------------------------------------------------
void SpectrogramView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);

    const int W = getWidth();
    const int H = getHeight();
    const int NB = proc.getNumBins();

    if (imgValid && img.isValid())
    {
        const int imgW = img.getWidth();
        const int imgH = img.getHeight();

        const int srcW = juce::jlimit(1, imgW, (int)((float)W / zoomX));
        const int srcH = juce::jlimit(1, imgH, (int)((float)H / zoomY));
        const int srcX = juce::jlimit(0, std::max(0, imgW - srcW), scrollOffset);
        const int srcY = juce::jlimit(0, std::max(0, imgH - srcH), vScrollOffset);

        g.drawImage(img, 0, 0, W, H, srcX, srcY, srcW, srcH);
    }
    else if (!proc.isFileLoaded())
    {
        g.setColour(juce::Colour(0xffaaccdd));
        g.setFont(juce::Font("Courier New", 15.0f, juce::Font::italic));
        g.drawText(">> Import a sound file to begin",
            getBounds().reduced(24), juce::Justification::centred);
    }

    // ── Confirmed selection ──────────────────────────────────────────────────
    if (sel.valid)
    {
        const int px0 = frameToPx(sel.left());
        const int px1 = frameToPx(sel.right());
        const int py0 = binToPy(sel.top());
        const int py1 = binToPy(sel.bottom());

        const auto selR = juce::Rectangle<int>::leftTopRightBottom(
            std::min(px0, px1), std::min(py0, py1),
            std::max(px0, px1), std::max(py0, py1)).toFloat();

        g.setColour(juce::Colour(0x44ffe066));
        g.fillRect(selR);
        g.setColour(juce::Colours::gold);
        g.drawRect(selR, 1.5f);

        constexpr float hs = 5.0f;
        g.setColour(juce::Colours::gold);
        for (auto corner : { selR.getTopLeft(),    selR.getTopRight(),
                             selR.getBottomLeft(), selR.getBottomRight() })
            g.fillRect(corner.x - hs * 0.5f, corner.y - hs * 0.5f, hs, hs);
    }

    // ── Live drag rectangle (Select mode) ────────────────────────────────────
    if (isDragging && editMode == 0)
    {
        const auto dragR = juce::Rectangle<int>(anchor, liveEnd).toFloat();
        g.setColour(juce::Colour(0x33ff9900));
        g.fillRect(dragR);
        g.setColour(juce::Colours::orange);
        g.drawRect(dragR, 1.0f);
    }

    // ── Brush cursor (Draw / Smear modes) ─────────────────────────────────────
    if ((editMode == 1 || editMode == 2) && isMouseOver())
    {
        const int bx = brushPos.x;
        const int by = brushPos.y;

        if (editMode == 1) // Draw mode – vertical bar showing bin extent
        {
            const int centerBin = pyToBin(by);
            const int topPy = binToPy(std::min(NB - 1, centerBin + drawThickness));
            const int botPy = binToPy(std::max(0, centerBin - drawThickness));
            const int yTop = std::min(topPy, botPy);
            const int yBot = std::max(topPy, botPy);

            g.setColour(juce::Colour(0xccff6600));
            juce::Rectangle<float> area(bx - 1.0f, yTop, 3.0f, std::max(2.0f, (float)(yBot - yTop)));
            g.drawRect(area, 1.5f);
            // Small crosshair dot
            g.fillEllipse((float)bx - 2.5f, (float)by - 2.5f, 5.0f, 5.0f);
        }
        else // Smear mode – circle showing smear radius
        {
            const int centerBin = pyToBin(by);
            const int edgePy = binToPy(std::max(0, centerBin - smearRadius));
            const int r = juce::jlimit(4, 120, std::abs(by - edgePy));

            g.setColour(juce::Colour(0xcc44ddff));
            g.drawEllipse((float)(bx - r), (float)(by - r),
                (float)(r * 2), (float)(r * 2), 1.5f);
            g.fillEllipse((float)bx - 2.5f, (float)by - 2.5f, 5.0f, 5.0f);
        }
    }

    // ── Scrub mode crosshair ──────────────────────────────────────────────────
    if (editMode == 3 && isMouseOver())
    {
        const int bx = brushPos.x;
        g.setColour(juce::Colour(0xccff8800));
        g.drawVerticalLine(bx, 0.0f, (float)H);
        g.fillEllipse((float)bx - 3.5f, (float)(H / 2) - 3.5f, 7.0f, 7.0f);
    }

    // ── Log-frequency axis ticks (right edge) ────────────────────────────────
    g.setFont(juce::Font("Courier New", 10.0f, juce::Font::plain));

    const float nyq = (float)(proc.sourceSampleRate > 0
        ? proc.sourceSampleRate * 0.5 : 22050.0);

    // Logarithmically-spaced tick frequencies (Hz)
    const float ticks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float hz : ticks)
    {
        if (hz >= nyq) break;
        const int bin = juce::jlimit(0, NB - 1, (int)(hz * (float)(NB - 1) / nyq));
        const int py = binToPy(bin);
        if (py < 0 || py >= H) continue;

        g.setColour(juce::Colour(0x55336699));
        g.drawHorizontalLine(py, (float)(W - 44), (float)W);

        const juce::String lbl = hz >= 1000.0f
            ? juce::String(hz / 1000.0f, 1) + "k"
            : juce::String((int)hz);
        g.setColour(juce::Colour(0xaa223355));
        g.drawText(lbl, W - 42, py - 9, 40, 10,
            juce::Justification::centredRight);
    }

    // ── Start-position marker ─────────────────────────────────────────────────
    if (proc.isFileLoaded())
    {
        const int nF = proc.numFrames;
        if (nF > 0)
        {
            const float sf = proc.startFraction.load();
            const int   startFrame = (int)(sf * (float)nF);
            const int   spx = frameToPx(startFrame);

            if (spx >= 0 && spx < W)
            {
                g.setColour(juce::Colour(0x15ffaa00));
                g.fillRect(0, 0, spx, H);

                g.setColour(juce::Colour(0xddff8800));
                g.drawVerticalLine(spx, 0.0f, (float)H);

                juce::Path tri;
                tri.addTriangle((float)spx - 5.f, 0.f,
                    (float)spx + 5.f, 0.f,
                    (float)spx, 9.f);
                g.setColour(juce::Colour(0xffff8800));
                g.fillPath(tri);
            }
        }
    }

    // ── Playhead (bright green moving bar) ───────────────────────────────────
    const float ph = proc.getPlayheadFraction();
    if (ph >= 0.0f && proc.isFileLoaded())
    {
        const int nF = proc.numFrames;
        if (nF > 0)
        {
            const int playFrame = juce::jlimit(0, nF - 1, (int)(ph * (float)nF));
            const int ppx = frameToPx(playFrame);

            if (ppx >= 0 && ppx < W)
            {
                g.setColour(juce::Colour(0xff00ff66));
                g.drawVerticalLine(ppx, 0.0f, (float)H);

                g.setColour(juce::Colour(0x4400ff66));
                if (ppx > 0)     g.drawVerticalLine(ppx - 1, 0.0f, (float)H);
                if (ppx < W - 1) g.drawVerticalLine(ppx + 1, 0.0f, (float)H);
            }
        }
    }
}

//------------------------------------------------------------------------------
// Mouse handlers
//------------------------------------------------------------------------------

void SpectrogramView::mouseDown(const juce::MouseEvent& e)
{
    brushPos = e.getPosition();

    if (editMode == 0) // Select
    {
        isDragging = true;
        anchor = liveEnd = e.getPosition();
        sel.valid = false;
    }
    else if (editMode == 3) // Scrub – start live audio playback at mouse position
    {
        const int nF = proc.numFrames;
        if (nF > 0)
        {
            const int   frame = juce::jlimit(0, nF - 1, pxToFrame(e.x));
            const float frac = (float)frame / (float)nF;

            // Snap position uses synthContentLen (= nF*hopSize) so it is
            // consistent with the processBlock target calculation.
            const double snapSample =
                (double)frac * (double)proc.synthContentLen.load();

            // Speed-limited path: set scrubSnapPos so the audio thread snaps
            // scrubCurrentPos on the very next block instead of gliding from 0.
            proc.scrubSnapPos.store(snapSample);

            // Smooth-glide path (scrubMaxSpeed == 0): snap the SmoothedValue
            // so the first block doesn't glide in from wherever it was before.
            // Guard with synthMutex because SmoothedValue is not thread-safe.
            {
                std::lock_guard<std::mutex> lk(proc.synthMutex);
                proc.scrubSmoothed.setCurrentAndTargetValue(snapSample);
            }
            proc.scrubTargetFraction.store(frac);
            proc.scrubActive.store(true);
        }
    }
    else
    {
        applyBrushAt(e.getPosition());
    }
    repaint();
}

void SpectrogramView::mouseDrag(const juce::MouseEvent& e)
{
    brushPos = e.getPosition();

    if (editMode == 0)
    {
        liveEnd = e.getPosition();
    }
    else if (editMode == 3) // Scrub – glide playhead to new mouse position
    {
        const int nF = proc.numFrames;
        if (nF > 0)
        {
            const int   frame = juce::jlimit(0, nF - 1, pxToFrame(e.x));
            proc.scrubTargetFraction.store((float)frame / (float)nF);
        }
    }
    else
    {
        applyBrushAt(e.getPosition());
    }
    repaint();
}

void SpectrogramView::mouseMove(const juce::MouseEvent& e)
{
    if (editMode != 0)
    {
        brushPos = e.getPosition();
        repaint();
    }
}

void SpectrogramView::mouseUp(const juce::MouseEvent& e)
{
    brushPos = e.getPosition();

    if (editMode == 0) // Select – finalise selection rectangle
    {
        isDragging = false;
        liveEnd = e.getPosition();

        const int f0 = pxToFrame(anchor.x);
        const int f1 = pxToFrame(liveEnd.x);
        const int b0 = pyToBin(anchor.y);
        const int b1 = pyToBin(liveEnd.y);

        const int nF = proc.numFrames;
        const int NB = proc.getNumBins();

        if (std::abs(f1 - f0) >= 1 && std::abs(b1 - b0) >= 1 && nF > 0)
        {
            sel.x0 = juce::jlimit(0, nF - 1, std::min(f0, f1));
            sel.x1 = juce::jlimit(0, nF - 1, std::max(f0, f1));
            sel.y0 = juce::jlimit(0, NB - 1, std::min(b0, b1));
            sel.y1 = juce::jlimit(0, NB - 1, std::max(b0, b1));
            sel.valid = true;
        }
    }
    else if (editMode == 3) // Scrub – stop live audio on release
    {
        proc.scrubActive.store(false);
    }
    else // Draw / Smear – commit to audio
    {
        proc.resynthesize();
        rebuildImage();
    }
    repaint();
}

//------------------------------------------------------------------------------
void SpectrogramView::applyBrushAt(juce::Point<int> pos)
{
    const int frame = pxToFrame(pos.x);
    const int bin = pyToBin(pos.y);

    if (frame < 0 || frame >= proc.numFrames) return;

    if (editMode == 1) // Draw – fundamental + harmonics
    {
        // cachedInvMax = 1/maxMag → rawAmp = drawAmplitude * maxMag
        const float rawAmp = (cachedInvMax > 1e-20f)
            ? drawAmplitude / cachedInvMax
            : drawAmplitude;

        for (int h = 1; h <= drawHarmonics; ++h)
        {
            const int harmonicBin = bin * h;
            if (harmonicBin >= proc.getNumBins()) break;
            // Amplitude rolls off as 1/h so higher harmonics are softer
            const float hAmp = rawAmp / (float)h;
            proc.applyPaintStroke(frame, harmonicBin, drawThickness, hAmp);
        }
        rebuildImageColumns(frame, frame);
    }
    else if (editMode == 2) // Smear – time-domain moving average
    {
        proc.applySmearStroke(frame, bin, smearRadius);
        rebuildImageColumns(frame - smearRadius, frame + smearRadius);
    }
}

//=============================================================================
// SpectralEditEditor – static helpers
//=============================================================================

void SpectralEditEditor::initKnob(juce::Slider& s, double lo, double hi,
    double def, const juce::String& suffix)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
    s.setRange(lo, hi, 1.0);
    s.setValue(def, juce::dontSendNotification);
    s.setDoubleClickReturnValue(true, def);
    if (suffix.isNotEmpty()) s.setTextValueSuffix(suffix);

    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff1c4f8a));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xffbbccdd));
    s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff1c4f8a));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void SpectralEditEditor::initZoomSlider(juce::Slider& s, bool vertical)
{
    s.setSliderStyle(vertical ? juce::Slider::LinearVertical
        : juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setRange(1.0, 8.0, 0.1);
    s.setValue(1.0, juce::dontSendNotification);
    s.setDoubleClickReturnValue(true, 1.0);
    s.setPopupDisplayEnabled(true, true, nullptr);
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xffbbccdd));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff1c4f8a));
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xffdde8f0));
}

void SpectralEditEditor::initModeStripSlider(juce::Slider& s,
    double lo, double hi, double def)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 38, 16);
    s.setRange(lo, hi, 1.0);
    s.setValue(def, juce::dontSendNotification);
    s.setDoubleClickReturnValue(true, def);
    s.setColour(juce::Slider::trackColourId, juce::Colour(0xff2a5a8a));
    s.setColour(juce::Slider::thumbColourId, juce::Colour(0xff88ccff));
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff0c2848));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xff88ccff));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void SpectralEditEditor::styleButton(juce::TextButton& b,
    juce::Colour bg, juce::Colour txt)
{
    b.setColour(juce::TextButton::buttonColourId, bg);
    b.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a7acc));
    b.setColour(juce::TextButton::textColourOffId, txt);
    b.setColour(juce::TextButton::textColourOnId, txt);
}

void SpectralEditEditor::updateModeButtons()
{
    const int m = spectrogramView.getEditMode();
    selectModeBtn.setToggleState(m == 0, juce::dontSendNotification);
    drawModeBtn.setToggleState(m == 1, juce::dontSendNotification);
    smearModeBtn.setToggleState(m == 2, juce::dontSendNotification);
    scrubModeBtn.setToggleState(m == 3, juce::dontSendNotification);
}

//=============================================================================
// Scrollbar helpers
//=============================================================================

void SpectralEditEditor::updateScrollbar()
{
    const int   imgW = spectrogramView.getTotalImageWidth();
    const int   visW = spectrogramView.getWidth();
    const float zX = spectrogramView.getZoomX();

    scrollBar.setVisible(true);

    if (imgW == 0)
    {
        scrollBar.setRangeLimits(0.0, 1.0);
        scrollBar.setCurrentRange(0.0, 1.0);
        return;
    }

    const int visFrames = juce::jlimit(1, std::max(1, imgW),
        (int)((float)visW / zX));

    scrollBar.setRangeLimits(0.0, (double)imgW);
    scrollBar.setCurrentRange((double)spectrogramView.getScrollOffset(),
        (double)visFrames);
}

void SpectralEditEditor::updateVScrollbar()
{
    const int   H = spectrogramView.getHeight();
    const float zY = spectrogramView.getZoomY();
    const int srcH = juce::jlimit(1, std::max(1, H), (int)((float)H / zY));

    if (srcH >= H || zY <= 1.0f + 1e-3f)
    {
        vScrollBar.setVisible(false);
        spectrogramView.setVScrollOffset(0);
        return;
    }

    vScrollBar.setVisible(true);
    vScrollBar.setRangeLimits(0.0, (double)H);
    vScrollBar.setCurrentRange((double)spectrogramView.getVScrollOffset(),
        (double)srcH);
}

//=============================================================================
// SpectralEditEditor – constructor / destructor
//=============================================================================

SpectralEditEditor::SpectralEditEditor(SpectralEditProcessor& p)
    : AudioProcessorEditor(&p),
    proc(p),
    spectrogramView(p),
    keyboard(p.keyboardState,
        juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setSize(1000, 660);
    setResizable(true, true);
    setResizeLimits(720, 500, 1920, 1200);

    setWantsKeyboardFocus(true);

    // ── Toolbar ───────────────────────────────────────────────────────────────
    addAndMakeVisible(importBtn);
    styleButton(importBtn, juce::Colour(0xff0a3060));
    importBtn.onClick = [this]()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Load audio file", juce::File{},
                "*.wav;*.aif;*.aiff;*.flac;*.mp3");
            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& fc)
                {
                    auto results = fc.getResults();
                    if (results.isEmpty()) return;

                    const juce::File chosenFile = results[0];

                    // Check file duration
                    juce::AudioFormatManager fm;
                    fm.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> rdr(fm.createReaderFor(chosenFile));

                    if (!rdr)
                    {
                        proc.loadFile(chosenFile, 0.0);
                        return;
                    }

                    const double     fileSR = rdr->sampleRate;
                    const juce::int64 nFileSamples = rdr->lengthInSamples;
                    const double duration = (double)nFileSamples / fileSR;
                    rdr.reset(); // release before spawning dialog

                    // Use integer sample comparison (+0.5 rounding) to avoid
                    // floating-point precision making exactly-60 s files appear
                    // fractionally over the limit and triggering the dialog.
                    const juce::int64 maxLoadSamples =
                        (juce::int64)(fileSR * 60.0 + 0.5);

                    if (nFileSamples <= maxLoadSamples)
                    {
                        // Short file – load immediately from the beginning
                        proc.loadFile(chosenFile, 0.0);
                        return;
                    }

                    // ── Long file: ask the user where to start ──────────────────
                    const int totalSec = (int)duration;
                    const int dh = totalSec / 3600;
                    const int dm = (totalSec % 3600) / 60;
                    const int ds = totalSec % 60;
                    const juce::String durStr = juce::String::formatted(
                        "%02d:%02d:%02d", dh, dm, ds);

                    auto* aw = new juce::AlertWindow(
                        "File exceeds 60 seconds",
                        "File duration: " + durStr + "\n\n"
                        "Only 60 s will be loaded at a time.\n"
                        "Enter the start time (HH:MM:SS) below:",
                        juce::MessageBoxIconType::QuestionIcon,
                        this);

                    aw->addTextEditor("startTime", "00:00:00", "Start time (HH:MM:SS):");
                    aw->addButton("Load", 1, juce::KeyPress(juce::KeyPress::returnKey));
                    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                    // deleteWhenDismissed = false so the ptr remains valid in the callback
                    aw->enterModalState(false,
                        juce::ModalCallbackFunction::create(
                            [aw, chosenFile, duration, this](int result)
                            {
                                double startSec = 0.0;
                                if (result == 1)
                                {
                                    const juce::String ts =
                                        aw->getTextEditorContents("startTime").trim();
                                    const auto parts =
                                        juce::StringArray::fromTokens(ts, ":", "");

                                    if (parts.size() >= 3)
                                        startSec = parts[0].getIntValue() * 3600.0
                                        + parts[1].getIntValue() * 60.0
                                        + parts[2].getDoubleValue();
                                    else if (parts.size() == 2)
                                        startSec = parts[0].getIntValue() * 60.0
                                        + parts[1].getDoubleValue();
                                    else
                                        startSec = ts.getDoubleValue();

                                    // Clamp so at least 1 s of audio is available
                                    startSec = juce::jlimit(0.0, std::max(0.0, duration - 1.0),
                                        startSec);
                                    proc.loadFile(chosenFile, startSec);
                                }
                                delete aw;
                            }));
                });
        };

    addAndMakeVisible(exportBtn);
    styleButton(exportBtn, juce::Colour(0xff1a6040));
    exportBtn.onClick = [this]()
        {
            if (!proc.isFileLoaded())
                return;

            exportChooser = std::make_unique<juce::FileChooser>(
                "Export audio", juce::File{}, "*.wav");
            exportChooser->launchAsync(
                juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
                [this](const juce::FileChooser& fc)
                {
                    auto results = fc.getResults();
                    if (results.isEmpty()) return;

                    juce::AudioBuffer<float> buf = proc.copySynthBuffer();
                    if (buf.getNumSamples() == 0) return;

                    juce::WavAudioFormat fmt;
                    juce::File outFile = results[0].withFileExtension("wav");
                    outFile.deleteFile();

                    std::unique_ptr<juce::FileOutputStream> fos(outFile.createOutputStream());
                    if (!fos) return;

                    std::unique_ptr<juce::AudioFormatWriter> writer(
                        fmt.createWriterFor(fos.get(),
                            proc.sourceSampleRate,
                            (unsigned)buf.getNumChannels(),
                            24, {}, 0));
                    if (!writer) return;
                    fos.release(); // writer owns the stream

                    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
                });
        };

    addAndMakeVisible(fileLabel);
    fileLabel.setFont(juce::Font("Courier New", 13.0f, juce::Font::plain));
    fileLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaccee));
    fileLabel.setText("No file loaded", juce::dontSendNotification);

    addAndMakeVisible(fftSizeLbl);
    fftSizeLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    fftSizeLbl.setColour(juce::Label::textColourId, juce::Colour(0xffaaccee));
    fftSizeLbl.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(fftSizeBox);
    fftSizeBox.addItem("512", 1);
    fftSizeBox.addItem("1024", 2);
    fftSizeBox.addItem("2048", 3);
    fftSizeBox.addItem("4096", 4);
    fftSizeBox.addItem("8192", 5);
    fftSizeBox.addItem("16384", 6);   // order 14 → 8+6 = 14
    fftSizeBox.setSelectedId(5, juce::dontSendNotification);
    fftSizeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d2240));
    fftSizeBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xff88ccff));
    fftSizeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff1c4f8a));
    fftSizeBox.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff88ccff));
    fftSizeBox.onChange = [this]()
        {
            // Reset start position: old fraction no longer maps to the same place
            startKnob.setValue(0.0, juce::sendNotification);
            proc.setFftOrder(8 + fftSizeBox.getSelectedId());
        };

    addAndMakeVisible(statusLabel);
    statusLabel.setFont(juce::Font("Courier New", 13.0f, juce::Font::italic));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc05020));
    statusLabel.setJustificationType(juce::Justification::centredRight);

    // ── Mode strip ────────────────────────────────────────────────────────────
    // Radio-group buttons: only one active at a time
    for (auto* btn : { &selectModeBtn, &drawModeBtn, &smearModeBtn, &scrubModeBtn })
    {
        addAndMakeVisible(*btn);
        btn->setRadioGroupId(42);
        btn->setClickingTogglesState(true);
        styleButton(*btn, juce::Colour(0xff1a3a5e));
    }
    selectModeBtn.setToggleState(true, juce::dontSendNotification);

    selectModeBtn.onClick = [this]() { spectrogramView.setEditMode(0); };
    drawModeBtn.onClick = [this]() { spectrogramView.setEditMode(1); };
    smearModeBtn.onClick = [this]() { spectrogramView.setEditMode(2); };
    scrubModeBtn.onClick = [this]() { spectrogramView.setEditMode(3); };

    // Draw thickness slider
    addAndMakeVisible(drawThickLbl);
    drawThickLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    drawThickLbl.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
    drawThickLbl.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(drawThickSlider);
    initModeStripSlider(drawThickSlider, 1.0, 100.0, 5.0);
    drawThickSlider.setTooltip("Draw brush half-width in frequency bins");
    drawThickSlider.onValueChange = [this]()
        {
            spectrogramView.setDrawThickness((int)drawThickSlider.getValue());
        };

    // Draw amplitude slider
    addAndMakeVisible(drawAmpLbl);
    drawAmpLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    drawAmpLbl.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
    drawAmpLbl.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(drawAmpSlider);
    initModeStripSlider(drawAmpSlider, 0.0, 100.0, 75.0);
    drawAmpSlider.setTextValueSuffix("%");
    drawAmpSlider.setTooltip("Draw amplitude as % of maximum spectral energy");
    drawAmpSlider.onValueChange = [this]()
        {
            spectrogramView.setDrawAmplitude((float)(drawAmpSlider.getValue() / 100.0));
        };

    // Harmonics slider
    addAndMakeVisible(harmonicsLbl);
    harmonicsLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    harmonicsLbl.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
    harmonicsLbl.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(harmonicsSlider);
    initModeStripSlider(harmonicsSlider, 1.0, 64.0, 1.0);
    harmonicsSlider.setTooltip("Number of harmonics drawn above the fundamental (1 = fundamental only)");
    harmonicsSlider.onValueChange = [this]()
        {
            spectrogramView.setDrawHarmonics((int)harmonicsSlider.getValue());
        };

    // Smear radius slider
    addAndMakeVisible(smearRadiusLbl);
    smearRadiusLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    smearRadiusLbl.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
    smearRadiusLbl.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(smearRadiusSlider);
    initModeStripSlider(smearRadiusSlider, 1.0, 60.0, 8.0);
    smearRadiusSlider.setTooltip("Smear brush radius in frames (time-domain moving average)");
    smearRadiusSlider.onValueChange = [this]()
        {
            spectrogramView.setSmearRadius((int)smearRadiusSlider.getValue());
        };

    // Scrub speed limit slider
    addAndMakeVisible(scrubSpeedLbl);
    scrubSpeedLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    scrubSpeedLbl.setColour(juce::Label::textColourId, juce::Colour(0xff88aacc));
    scrubSpeedLbl.setJustificationType(juce::Justification::centredRight);

    addAndMakeVisible(scrubSpeedSlider);
    initModeStripSlider(scrubSpeedSlider, 0.0, 1.0, 0.0);
    scrubSpeedSlider.setRange(0.0, 1.0, 0.01);
    scrubSpeedSlider.setTooltip(
        "Scrub max speed: 0 = original smooth glide (no cap), "
        "1 = max 1x (real-time), linear in between");
    scrubSpeedSlider.onValueChange = [this]()
        {
            proc.scrubMaxSpeed.store((float)scrubSpeedSlider.getValue());
        };

    // ── Spectrogram ───────────────────────────────────────────────────────────
    addAndMakeVisible(spectrogramView);

    // ── Horizontal scroll + zoom ──────────────────────────────────────────────
    addAndMakeVisible(scrollBar);
    scrollBar.setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff1c4f8a));
    scrollBar.setColour(juce::ScrollBar::backgroundColourId, juce::Colour(0xffdde8f0));
    scrollBar.addListener(this);

    addAndMakeVisible(hZoomSlider);
    initZoomSlider(hZoomSlider, false);
    hZoomSlider.onValueChange = [this]()
        {
            spectrogramView.setZoom((float)hZoomSlider.getValue(),
                spectrogramView.getZoomY());
            updateScrollbar();
            updateVScrollbar();
        };

    addAndMakeVisible(hZoomLabel);
    hZoomLabel.setFont(juce::Font("Courier New", 9.0f, juce::Font::plain));
    hZoomLabel.setColour(juce::Label::textColourId, juce::Colour(0xff334455));
    hZoomLabel.setJustificationType(juce::Justification::centred);

    // ── Vertical scroll + zoom ────────────────────────────────────────────────
    addAndMakeVisible(vScrollBar);
    vScrollBar.setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff1c4f8a));
    vScrollBar.setColour(juce::ScrollBar::backgroundColourId, juce::Colour(0xffdde8f0));
    vScrollBar.addListener(this);
    vScrollBar.setVisible(false);

    addAndMakeVisible(vZoomSlider);
    initZoomSlider(vZoomSlider, true);
    vZoomSlider.onValueChange = [this]()
        {
            spectrogramView.setZoom(spectrogramView.getZoomX(),
                (float)vZoomSlider.getValue());
            updateScrollbar();
            updateVScrollbar();
        };

    addAndMakeVisible(vZoomLabel);
    vZoomLabel.setFont(juce::Font("Courier New", 9.0f, juce::Font::plain));
    vZoomLabel.setColour(juce::Label::textColourId, juce::Colour(0xff334455));
    vZoomLabel.setJustificationType(juce::Justification::centred);

    // ── Sidebar ───────────────────────────────────────────────────────────────
    addAndMakeVisible(sideTitle);
    sideTitle.setFont(juce::Font("Courier New", 12.0f, juce::Font::bold));
    sideTitle.setColour(juce::Label::textColourId, juce::Colour(0xff0a3060));
    sideTitle.setJustificationType(juce::Justification::centred);

    for (auto* btn : { &mirrorLRBtn, &mirrorUDBtn })
        styleButton(*btn, juce::Colour(0xff1c4f8a));
    styleButton(deleteSelBtn, juce::Colour(0xff8a1c1c));

    addAndMakeVisible(mirrorLRBtn);
    addAndMakeVisible(mirrorUDBtn);
    addAndMakeVisible(deleteSelBtn);

    mirrorLRBtn.onClick = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (s.valid) proc.applyMirrorLR(s);
        };
    mirrorUDBtn.onClick = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (s.valid) proc.applyMirrorUD(s);
        };
    deleteSelBtn.onClick = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (s.valid) { proc.applyDelete(s); spectrogramView.clearSelection(); }
        };

    // Start position
    addAndMakeVisible(startKnobLbl);
    startKnobLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    startKnobLbl.setColour(juce::Label::textColourId, juce::Colour(0xffaa5500));

    addAndMakeVisible(startKnob);
    startKnob.setSliderStyle(juce::Slider::LinearHorizontal);
    startKnob.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    startKnob.setRange(0.0, 100.0, 0.1);
    startKnob.setValue(0.0, juce::dontSendNotification);
    startKnob.setTextValueSuffix("%");
    startKnob.setDoubleClickReturnValue(true, 0.0);
    startKnob.setColour(juce::Slider::trackColourId, juce::Colour(0xffbbccdd));
    startKnob.setColour(juce::Slider::thumbColourId, juce::Colour(0xffff8800));
    startKnob.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffaa5500));
    startKnob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    startKnob.onValueChange = [this]()
        {
            proc.startFraction.store((float)(startKnob.getValue() / 100.0));
            spectrogramView.repaint();
        };

    // Rotate Bins
    addAndMakeVisible(rotBinsLbl);
    rotBinsLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    rotBinsLbl.setColour(juce::Label::textColourId, juce::Colour(0xff334455));

    addAndMakeVisible(rotBinsKnob);
    initKnob(rotBinsKnob, -100.0, 100.0, 0.0, "%");
    rotBinsKnob.onDragEnd = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (!s.valid) return;
            const int range = s.top() - s.bottom() + 1;
            const int bins = (int)std::round(rotBinsKnob.getValue() / 100.0 * range);
            proc.applyRotateBinsUp(s, bins);
            rotBinsKnob.setValue(0.0, juce::dontSendNotification);
        };

    // Rotate Right
    addAndMakeVisible(rotRightLbl);
    rotRightLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    rotRightLbl.setColour(juce::Label::textColourId, juce::Colour(0xff334455));

    addAndMakeVisible(rotRightKnob);
    initKnob(rotRightKnob, -100.0, 100.0, 0.0, "%");
    rotRightKnob.onDragEnd = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (!s.valid) return;
            const int range = s.right() - s.left() + 1;
            const int frames = (int)std::round(rotRightKnob.getValue() / 100.0 * range);
            proc.applyRotateRight(s, frames);
            rotRightKnob.setValue(0.0, juce::dontSendNotification);
        };

    // Change Volume
    addAndMakeVisible(volLbl);
    volLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    volLbl.setColour(juce::Label::textColourId, juce::Colour(0xff334455));

    addAndMakeVisible(volKnob);
    initKnob(volKnob, -100.0, 100.0, 0.0, "%");
    volKnob.onDragEnd = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (!s.valid) return;
            proc.applyChangeVolume(s, (float)volKnob.getValue());
            volKnob.setValue(0.0, juce::dontSendNotification);
        };

    // Spectral Compress strength
    addAndMakeVisible(compressStrLbl);
    compressStrLbl.setFont(juce::Font("Courier New", 11.0f, juce::Font::plain));
    compressStrLbl.setColour(juce::Label::textColourId, juce::Colour(0xff334455));

    addAndMakeVisible(compressStrKnob);
    initKnob(compressStrKnob, 0.0, 4.0, 0.0);
    compressStrKnob.setRange(0.0, 4.0, 0.01);
    compressStrKnob.setValue(0.0, juce::dontSendNotification);
    // Apply on drag-end (same pattern as Rotate / Volume knobs), then reset to 0
    compressStrKnob.onDragEnd = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (!s.valid) return;
            proc.applySpectralCompress(s, (float)compressStrKnob.getValue());
            compressStrKnob.setValue(0.0, juce::dontSendNotification);
        };

    addAndMakeVisible(compressBtn);
    styleButton(compressBtn, juce::Colour(0xff1c4f8a));
    compressBtn.onClick = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (!s.valid) return;
            proc.applySpectralCompress(s, (float)compressStrKnob.getValue());
            // Reset to 0 after applying, same pattern as the other operation knobs
            // and matching the onDragEnd callback above.
            compressStrKnob.setValue(0.0, juce::dontSendNotification);
        };

    // Spectral Freeze
    addAndMakeVisible(freezeBtn);
    styleButton(freezeBtn, juce::Colour(0xff1a5050));
    freezeBtn.onClick = [this]()
        {
            auto s = spectrogramView.getSelection();
            if (s.valid) proc.applyFreeze(s);
        };

    // ── MIDI keyboard ─────────────────────────────────────────────────────────
    addAndMakeVisible(keyboard);
    keyboard.setAvailableRange(24, 96);
    keyboard.setLowestVisibleKey(36);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId,
        juce::Colours::white);
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId,
        juce::Colour(0xff102030));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId,
        juce::Colour(0xffaabbcc));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
        juce::Colour(0x441c4f8a));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,
        juce::Colour(0x881c4f8a));

    startTimerHz(30);
}

SpectralEditEditor::~SpectralEditEditor()
{
    scrollBar.removeListener(this);
    vScrollBar.removeListener(this);
    stopTimer();
}

//=============================================================================
// SpectralEditEditor – keyboard
//=============================================================================

bool SpectralEditEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        proc.stopAllVoices();
        return true;
    }
    return false;
}

//=============================================================================
// SpectralEditEditor – scroll callbacks
//=============================================================================

void SpectralEditEditor::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &scrollBar)
    {
        const int   imgW = spectrogramView.getTotalImageWidth();
        const float zX = spectrogramView.getZoomX();
        const int   visF = juce::jlimit(1, std::max(1, imgW),
            (int)((float)spectrogramView.getWidth() / zX));
        const int   maxOff = std::max(0, imgW - visF);
        spectrogramView.setScrollOffset(juce::jlimit(0, maxOff, (int)newRangeStart));
    }
    else if (bar == &vScrollBar)
    {
        const int   H = spectrogramView.getHeight();
        const float zY = spectrogramView.getZoomY();
        const int   srcH = juce::jlimit(1, std::max(1, H), (int)((float)H / zY));
        const int   maxOff = std::max(0, H - srcH);
        spectrogramView.setVScrollOffset(juce::jlimit(0, maxOff, (int)newRangeStart));
    }
}

//=============================================================================
// SpectralEditEditor – layout
//=============================================================================

void SpectralEditEditor::resized()
{
    auto area = getLocalBounds();

    // ── Toolbar (42 px) ───────────────────────────────────────────────────────
    auto toolbar = area.removeFromTop(42);
    toolbar.reduce(6, 4);

    importBtn.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(6);
    exportBtn.setBounds(toolbar.removeFromLeft(110));
    toolbar.removeFromLeft(8);

    statusLabel.setBounds(toolbar.removeFromRight(150));
    fftSizeBox.setBounds(toolbar.removeFromRight(76));
    fftSizeLbl.setBounds(toolbar.removeFromRight(36));
    toolbar.removeFromRight(6);

    fileLabel.setBounds(toolbar);

    // ── Keyboard (full plugin width) ──────────────────────────────────────────
    auto kbArea = area.removeFromBottom(88);
    keyboard.setBounds(kbArea.reduced(0, 2));

    // ── Mode strip (32 px, full width) ───────────────────────────────────────
    auto modeStrip = area.removeFromTop(32);
    modeStrip.reduce(6, 4);

    selectModeBtn.setBounds(modeStrip.removeFromLeft(58));
    modeStrip.removeFromLeft(3);
    drawModeBtn.setBounds(modeStrip.removeFromLeft(58));
    modeStrip.removeFromLeft(3);
    smearModeBtn.setBounds(modeStrip.removeFromLeft(58));
    modeStrip.removeFromLeft(3);
    scrubModeBtn.setBounds(modeStrip.removeFromLeft(58));
    modeStrip.removeFromLeft(10);

    // Thickness (draw)
    drawThickLbl.setBounds(modeStrip.removeFromLeft(62));
    drawThickSlider.setBounds(modeStrip.removeFromLeft(88));
    modeStrip.removeFromLeft(6);

    // Amplitude (draw)
    drawAmpLbl.setBounds(modeStrip.removeFromLeft(62));
    drawAmpSlider.setBounds(modeStrip.removeFromLeft(88));
    modeStrip.removeFromLeft(6);

    // Harmonics (draw)
    harmonicsLbl.setBounds(modeStrip.removeFromLeft(70));
    harmonicsSlider.setBounds(modeStrip.removeFromLeft(88));
    modeStrip.removeFromLeft(6);

    // Smear radius
    smearRadiusLbl.setBounds(modeStrip.removeFromLeft(58));
    smearRadiusSlider.setBounds(modeStrip.removeFromLeft(88));

    // Scrub speed limit (uses the remaining ~115 px)
    modeStrip.removeFromLeft(6);
    scrubSpeedLbl.setBounds(modeStrip.removeFromLeft(52));
    scrubSpeedSlider.setBounds(modeStrip.removeFromLeft(57));

    // ── Sidebar (right 210 px) ────────────────────────────────────────────────
    auto side = area.removeFromRight(210);
    side.reduce(8, 6);

    sideTitle.setBounds(side.removeFromTop(22));
    side.removeFromTop(6);

    mirrorLRBtn.setBounds(side.removeFromTop(28));
    side.removeFromTop(4);
    mirrorUDBtn.setBounds(side.removeFromTop(28));
    side.removeFromTop(4);
    deleteSelBtn.setBounds(side.removeFromTop(28));
    side.removeFromTop(10);

    startKnobLbl.setBounds(side.removeFromTop(16));
    startKnob.setBounds(side.removeFromTop(26));
    side.removeFromTop(10);

    auto layoutKnobRow = [&](juce::Label& lbl, juce::Slider& knob)
        {
            lbl.setBounds(side.removeFromTop(16));
            auto knobArea = side.removeFromTop(52);
            knob.setBounds(knobArea.removeFromLeft(52));
            side.removeFromTop(8);
        };

    layoutKnobRow(rotBinsLbl, rotBinsKnob);
    layoutKnobRow(rotRightLbl, rotRightKnob);
    layoutKnobRow(volLbl, volKnob);
    layoutKnobRow(compressStrLbl, compressStrKnob);

    compressBtn.setBounds(side.removeFromTop(28));
    side.removeFromTop(4);
    freezeBtn.setBounds(side.removeFromTop(28));
    side.removeFromTop(4);

    // ── Right edge of canvas: V-zoom | V-scroll ───────────────────────────────
    auto vZoomArea = area.removeFromRight(16);
    auto vScrollArea = area.removeFromRight(12);
    vZoomLabel.setBounds(vZoomArea.removeFromTop(14));
    vZoomSlider.setBounds(vZoomArea);
    vScrollBar.setBounds(vScrollArea);

    // ── H-scroll strip ────────────────────────────────────────────────────────
    auto hScrollArea = area.removeFromBottom(14);
    scrollBar.setBounds(hScrollArea);

    // ── H-zoom strip ──────────────────────────────────────────────────────────
    auto hZoomStrip = area.removeFromBottom(18);
    hZoomLabel.setBounds(hZoomStrip.removeFromLeft(14));
    hZoomSlider.setBounds(hZoomStrip);

    // ── Spectrogram canvas ────────────────────────────────────────────────────
    spectrogramView.setBounds(area);

    updateScrollbar();
    updateVScrollbar();
}

//=============================================================================
// SpectralEditEditor – paint
//=============================================================================

void SpectralEditEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xfff0f4f8));

    // Toolbar
    g.setColour(juce::Colour(0xff0a2040));
    g.fillRect(0, 0, getWidth(), 42);

    // Mode strip
    g.setColour(juce::Colour(0xff0c2848));
    g.fillRect(0, 42, getWidth(), 32);

    // Subtle separator line between toolbar and mode strip
    g.setColour(juce::Colour(0xff1c4f8a));
    g.drawHorizontalLine(42, 0.0f, (float)getWidth());

    // Sidebar background (starts below mode strip)
    const int sideX = getWidth() - 226;
    const int kbTop = getHeight() - 90;
    g.setColour(juce::Colour(0xffe4ecf5));
    g.fillRect(sideX, 74, 226, kbTop - 74);

    // Sidebar separator
    g.setColour(juce::Colour(0xffaabbcc));
    g.drawVerticalLine(sideX, 74.0f, (float)kbTop);

    // Keyboard separator
    g.drawHorizontalLine(kbTop, 0.0f, (float)getWidth());

    // H-zoom + H-scroll strip backgrounds (canvas width)
    const int stripBottom = kbTop;
    const int stripTop = stripBottom - 14 - 18;
    g.setColour(juce::Colour(0xffdde8f0));
    g.fillRect(0, stripTop, sideX, stripBottom - stripTop);

    // V-zoom strip background
    g.fillRect(sideX - 28, 74, 28, kbTop - 74);
}

//=============================================================================
// SpectralEditEditor – timer
//=============================================================================

void SpectralEditEditor::timerCallback()
{
    const bool compiling = proc.isCompiling();

    if (compiling)
    {
        static int dotCount = 0;
        dotCount = (dotCount + 1) % 4;
        static const char* dotFrames[] = { "Compiling .", "Compiling ..",
                                           "Compiling ...", "Compiling .." };
        statusLabel.setText(dotFrames[dotCount], juce::dontSendNotification);
    }
    else if (prevCompiling && !compiling)
    {
        statusLabel.setText("Ready", juce::dontSendNotification);
        spectrogramView.rebuildImage();
        updateScrollbar();
        updateVScrollbar();
    }
    else if (!compiling && !proc.isFileLoaded())
    {
        statusLabel.setText({}, juce::dontSendNotification);
    }

    prevCompiling = compiling;

    // Sync filename label
    if (proc.isFileLoaded())
    {
        const auto name = proc.getFileName();
        if (fileLabel.getText() != name)
            fileLabel.setText(name, juce::dontSendNotification);
    }

    // Build image if needed on first load
    if (!compiling && proc.isFileLoaded() && !spectrogramView.getTotalImageWidth())
    {
        spectrogramView.rebuildImage();
        updateScrollbar();
        updateVScrollbar();
    }

    // Always repaint the spectrogram for smooth playhead animation
    if (proc.isFileLoaded())
        spectrogramView.repaint();
}