#include "CrtMatrix.h"
#include "PluginProcessor.h"

namespace {
    // Phosphor palette - bright blue tube, near-white phosphor
    const juce::Colour phBright{ 0xFFF4FCFF };
    const juce::Colour phMid{ 0xFFBFE9FF };
    const juce::Colour phDim{ 0xFF7FC0EE };
    const juce::Colour glassTop{ 0xFF2A86DA };
    const juce::Colour glassBot{ 0xFF0B3C82 };

    juce::Font crtFont(float size, bool bold = false) {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size,
                                            bold ? juce::Font::bold : juce::Font::plain));
    }

    // Text with a soft phosphor bloom around it
    void glowText(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r,
                  juce::Justification j, juce::Colour c, float intensity = 1.0f) {
        g.setColour(c.withAlpha(0.10f * intensity));
        for (auto o : { juce::Point<float>(-1.2f, 0.0f), { 1.2f, 0.0f }, { 0.0f, -1.2f }, { 0.0f, 1.2f } })
            g.drawText(t, r.translated(o.x, o.y), j, false);
        g.setColour(c.withAlpha(0.18f * intensity));
        g.drawText(t, r.translated(0.5f, 0.0f), j, false);
        g.setColour(c.withMultipliedAlpha(intensity));
        g.drawText(t, r, j, false);
    }
}

// ==============================================================
CrtMatrix::CrtMatrix(AlphaBetaAudioProcessor& p) : proc(p) {
    setRepaintsOnMouseActivity(false);
    startTimerHz(20);
}

CrtMatrix::~CrtMatrix() { stopTimer(); }

juce::RangedAudioParameter* CrtMatrix::param(const juce::String& id) const {
    return proc.apvts.getParameter(id);
}

int CrtMatrix::getChoice(const juce::String& id) const {
    return (int)proc.apvts.getRawParameterValue(id)->load();
}

float CrtMatrix::getAmount(int slot) const {
    return proc.apvts.getRawParameterValue(Mod::slotAmtID(slot))->load();
}

void CrtMatrix::setParamReal(const juce::String& id, float value) {
    if (auto* p = param(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

// "2:40" for pitch destinations (semitones:cents), "+75.00" otherwise
juce::String CrtMatrix::formatAmount(float amt, int dest) {
    if (Mod::isPitchDest(dest)) {
        float semis = amt * 0.01f * Mod::PITCH_SEMIS_FULL;
        int totalCents = juce::roundToInt(std::abs(semis) * 100.0f);
        auto s = juce::String(totalCents / 100) + ":" + juce::String(totalCents % 100).paddedLeft('0', 2);
        return (semis < 0.0f ? "-" : "+") + s;
    }
    return (amt < 0.0f ? "-" : "+") + juce::String(std::abs(amt), 2);
}

void CrtMatrix::showStatus(const juce::String& text) {
    status = text;
    statusTime = juce::Time::getMillisecondCounterHiRes();
    repaint();
}

juce::String CrtMatrix::snapshot() const {
    juce::String s = proc.getPatchName();
    for (int i = 0; i < Mod::NUM_SLOTS; ++i)
        s << "|" << getChoice(Mod::slotSrcID(i)) << "," << getAmount(i) << "," << getChoice(Mod::slotDstID(i));
    return s;
}

void CrtMatrix::timerCallback() {
    auto snap = snapshot();
    bool fading = status.isNotEmpty();
    if (snap != lastSnapshot || fading) {
        lastSnapshot = snap;
        if (fading && juce::Time::getMillisecondCounterHiRes() - statusTime > 8000.0)
            status.clear();
        repaint();
    }
}

// ==============================================================
//  Layout
// ==============================================================
void CrtMatrix::resized() {
    auto b = getLocalBounds().reduced(2);
    screen = b.reduced(10);

    const int headerH = 24, statusH = 20;
    auto inner = screen.reduced(10, 6);
    inner.removeFromTop(headerH);
    inner.removeFromBottom(statusH);
    rowH = juce::jmax(14, inner.getHeight() / Mod::NUM_SLOTS);
    rowsArea = inner.withHeight(rowH * Mod::NUM_SLOTS);

    // Columns: idx | source | amount | destination
    const int w = rowsArea.getWidth();
    const int idxW = 14;
    const int amtW = juce::roundToInt(w * 0.19f);
    const int nameW = (w - idxW - amtW) / 2;
    colX[0] = rowsArea.getX() + idxW;
    colX[1] = colX[0] + nameW;
    colX[2] = colX[1] + amtW;
    colX[3] = rowsArea.getRight();

    // Cached scanline + vignette overlay
    scanlines = juce::Image(juce::Image::ARGB, juce::jmax(1, screen.getWidth()), juce::jmax(1, screen.getHeight()), true);
    {
        juce::Graphics sg(scanlines);
        for (int y = 0; y < screen.getHeight(); y += 3) {
            sg.setColour(juce::Colour(0xFF02163A).withAlpha(0.20f));
            sg.fillRect(0, y, screen.getWidth(), 1);
        }
        auto sr = scanlines.getBounds().toFloat();
        juce::ColourGradient vig(juce::Colours::transparentBlack, sr.getCentreX(), sr.getCentreY(),
                                 juce::Colour(0xFF021230).withAlpha(0.60f), sr.getX(), sr.getY(), true);
        vig.addColour(0.6, juce::Colours::transparentBlack);
        sg.setGradientFill(vig);
        sg.fillRect(sr);
    }
}

juce::Rectangle<int> CrtMatrix::cellBounds(int row, int col) const {
    int y = rowsArea.getY() + row * rowH;
    return { colX[col], y, colX[col + 1] - colX[col], rowH };
}

int CrtMatrix::rowAt(juce::Point<int> p) const {
    if (!rowsArea.contains(p)) return -1;
    return juce::jlimit(0, Mod::NUM_SLOTS - 1, (p.y - rowsArea.getY()) / rowH);
}

int CrtMatrix::colAt(juce::Point<int> p) const {
    if (!rowsArea.contains(p)) return COL_NONE;
    for (int c = 0; c < 3; ++c)
        if (p.x >= colX[c] && p.x < colX[c + 1]) return c;
    return COL_NONE;
}

// ==============================================================
//  Painting
// ==============================================================
void CrtMatrix::paint(juce::Graphics& g) {
    auto bez = getLocalBounds().reduced(2).toFloat();

    // ---- Bezel ----
    g.setColour(juce::Colour(0x60000000));
    g.fillRoundedRectangle(bez.translated(0.0f, 2.0f), 12.0f);
    juce::ColourGradient metal(juce::Colour(0xFF434A57), bez.getX(), bez.getY(),
                               juce::Colour(0xFF151920), bez.getX(), bez.getBottom(), false);
    metal.addColour(0.15, juce::Colour(0xFF2E3440));
    g.setGradientFill(metal);
    g.fillRoundedRectangle(bez, 12.0f);
    g.setColour(juce::Colour(0xFF0C0E13));
    g.drawRoundedRectangle(bez.reduced(0.5f), 12.0f, 1.2f);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawLine(bez.getX() + 14, bez.getY() + 1.5f, bez.getRight() - 14, bez.getY() + 1.5f, 1.0f);

    // Screws in the corners
    for (auto c : { bez.getTopLeft() + juce::Point<float>(6.5f, 6.5f), bez.getTopRight() + juce::Point<float>(-6.5f, 6.5f),
                    bez.getBottomLeft() + juce::Point<float>(6.5f, -6.5f), bez.getBottomRight() + juce::Point<float>(-6.5f, -6.5f) }) {
        g.setColour(juce::Colour(0xFF0E1015));
        g.fillEllipse(c.x - 2.6f, c.y - 2.6f, 5.2f, 5.2f);
        g.setColour(juce::Colour(0xFF5A6272));
        g.drawLine(c.x - 1.6f, c.y - 1.6f, c.x + 1.6f, c.y + 1.6f, 0.8f);
    }

    // ---- Glass ----
    auto sc = screen.toFloat();
    const float rad = 14.0f;
    juce::Path glass;
    glass.addRoundedRectangle(sc, rad);

    // recessed rim
    g.setColour(juce::Colour(0xFF05070B));
    g.fillRoundedRectangle(sc.expanded(2.5f), rad + 2.0f);

    juce::ColourGradient tube(glassTop, sc.getCentreX(), sc.getCentreY() - sc.getHeight() * 0.15f,
                              glassBot, sc.getX(), sc.getBottom(), true);
    g.setGradientFill(tube);
    g.fillPath(glass);

    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(glass);

        // ---- Header ----
        auto inner = screen.reduced(10, 6);
        auto header = inner.removeFromTop(24).toFloat();
        g.setFont(crtFont(13.0f, true));
        glowText(g, "MATRIX", header, juce::Justification::centredLeft, phBright);
        g.setFont(crtFont(10.5f));
        glowText(g, proc.getPatchName().toUpperCase(), header, juce::Justification::centredRight, phMid, 0.9f);
        g.setColour(phDim.withAlpha(0.8f));
        g.fillRect(header.getX(), header.getBottom() - 3.0f, header.getWidth(), 1.0f);

        // ---- Rows ----
        const auto& srcN = Mod::sourceNames();
        const auto& dstN = Mod::destNames();
        const float fs = juce::jlimit(9.0f, 11.5f, rowH * 0.58f);
        g.setFont(crtFont(fs));

        for (int r = 0; r < Mod::NUM_SLOTS; ++r) {
            int s = getChoice(Mod::slotSrcID(r));
            int d = getChoice(Mod::slotDstID(r));
            float a = getAmount(r);
            bool active = s != Mod::SRC_OFF && d != Mod::DST_OFF;
            auto rowR = juce::Rectangle<int>(rowsArea.getX(), rowsArea.getY() + r * rowH, rowsArea.getWidth(), rowH).toFloat();

            if (r == hoverRow || r == dragRow) {
                g.setColour(phBright.withAlpha(0.07f));
                g.fillRoundedRectangle(rowR.reduced(0.0f, 1.0f), 3.0f);
                int c = (r == dragRow) ? (int)COL_AMT : hoverCol;
                if (c != COL_NONE) {
                    g.setColour(phMid.withAlpha(0.45f));
                    g.drawRoundedRectangle(cellBounds(r, c).toFloat().reduced(1.0f, 1.5f), 3.0f, 1.0f);
                }
            }

            float bright = active ? 1.0f : 0.55f;
            glowText(g, juce::String(r + 1), rowR.withWidth((float)(colX[0] - rowsArea.getX()) - 2.0f),
                     juce::Justification::centredRight, phDim, 1.0f);

            auto srcR = cellBounds(r, COL_SRC).toFloat().withTrimmedLeft(5.0f);
            auto amtR = cellBounds(r, COL_AMT).toFloat();
            auto dstR = cellBounds(r, COL_DST).toFloat().withTrimmedLeft(5.0f);

            glowText(g, srcN[s], srcR, juce::Justification::centredLeft, s ? phBright : phDim, bright);
            glowText(g, formatAmount(a, d), amtR, juce::Justification::centred,
                     std::abs(a) > 0.001f ? phBright : phDim, bright);
            glowText(g, dstN[d], dstR, juce::Justification::centredLeft, d ? phBright : phDim, bright);

            // Amount bar: grows from the centre of the amount cell
            if (std::abs(a) > 0.001f) {
                float cx = amtR.getCentreX(), half = amtR.getWidth() * 0.42f;
                float len = half * std::abs(a) / 100.0f;
                float y = amtR.getBottom() - 2.5f;
                g.setColour(phMid.withAlpha(active ? 0.75f : 0.35f));
                g.fillRect(a > 0 ? cx : cx - len, y, len, 1.5f);
                g.setColour(phDim);
                g.fillRect(cx - 0.5f, y - 1.5f, 1.0f, 4.0f);
            }
        }

        // ---- Status line ----
        auto statusR = juce::Rectangle<float>((float)rowsArea.getX(), (float)rowsArea.getBottom() + 2.0f,
                                              (float)rowsArea.getWidth(), 18.0f);
        g.setColour(phDim.withAlpha(0.6f));
        g.fillRect(statusR.getX(), statusR.getY(), statusR.getWidth(), 1.0f);
        g.setFont(crtFont(9.5f));
        if (status.isNotEmpty()) {
            double age = juce::Time::getMillisecondCounterHiRes() - statusTime;
            float alpha = age < 6000.0 ? 1.0f : (float)juce::jmax(0.0, 1.0 - (age - 6000.0) / 2000.0);
            bool blinkOn = ((int)(age / 500.0) % 2 == 0) || age > 2000.0;
            glowText(g, status + (blinkOn ? " _" : "  "), statusR, juce::Justification::centredLeft, phBright, alpha);
        } else {
            juce::String hint = "SOURCE  >  AMOUNT  >  DESTINATION";
            if (hoverCol == COL_AMT) hint = "DRAG  SHIFT=FINE  2-CLICK=0";
            else if (hoverCol == COL_SRC || hoverCol == COL_DST) hint = "CLICK TO SELECT  R-CLICK=CLEAR";
            glowText(g, hint, statusR, juce::Justification::centredLeft, phDim, 1.0f);
        }

        // ---- Tube effects: scanlines, vignette, glass reflection ----
        // drawImage uses the current colour's opacity - reset it, or the overlay
        // fades along with the status text and the tube briefly looks brighter
        g.setOpacity(1.0f);
        g.drawImageAt(scanlines, screen.getX(), screen.getY());
        juce::ColourGradient refl(juce::Colours::white.withAlpha(0.09f), sc.getX(), sc.getY(),
                                  juce::Colours::transparentWhite, sc.getX(), sc.getY() + sc.getHeight() * 0.45f, false);
        g.setGradientFill(refl);
        g.fillEllipse(sc.getX() - sc.getWidth() * 0.1f, sc.getY() - sc.getHeight() * 0.35f,
                      sc.getWidth() * 1.2f, sc.getHeight() * 0.75f);
    }

    // Glass edge
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.strokePath(glass, juce::PathStrokeType(1.5f));
    g.setColour(phMid.withAlpha(0.12f));
    g.drawRoundedRectangle(sc.reduced(1.5f), rad - 1.0f, 1.0f);
}

// ==============================================================
//  Mouse
// ==============================================================
void CrtMatrix::mouseMove(const juce::MouseEvent& e) {
    int r = rowAt(e.getPosition()), c = colAt(e.getPosition());
    if (r != hoverRow || c != hoverCol) {
        hoverRow = r; hoverCol = c;
        setMouseCursor(c == COL_AMT ? juce::MouseCursor::UpDownResizeCursor
                     : (c != COL_NONE ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
        repaint();
    }
}

void CrtMatrix::mouseExit(const juce::MouseEvent&) {
    hoverRow = -1; hoverCol = COL_NONE;
    repaint();
}

void CrtMatrix::mouseDown(const juce::MouseEvent& e) {
    int r = rowAt(e.getPosition()), c = colAt(e.getPosition());
    if (r < 0 || c == COL_NONE) return;

    if (e.mods.isPopupMenu()) {
        juce::PopupMenu m;
        m.addItem(1, "Clear slot " + juce::String(r + 1));
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
            [this, r](int res) {
                if (res != 1) return;
                setParamReal(Mod::slotSrcID(r), 0.0f);
                setParamReal(Mod::slotAmtID(r), 0.0f);
                setParamReal(Mod::slotDstID(r), 0.0f);
            });
        return;
    }

    if (c == COL_AMT) {
        dragRow = r;
        dragStartValue = getAmount(r);
        if (auto* p = param(Mod::slotAmtID(r))) p->beginChangeGesture();
        repaint();
    } else {
        showMenu(r, c);
    }
}

void CrtMatrix::mouseDrag(const juce::MouseEvent& e) {
    if (dragRow < 0) return;
    float perPixel = e.mods.isShiftDown() ? 0.05f : 0.5f;
    float v = juce::jlimit(-100.0f, 100.0f, dragStartValue - (float)e.getDistanceFromDragStartY() * perPixel);
    if (auto* p = param(Mod::slotAmtID(dragRow)))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void CrtMatrix::mouseUp(const juce::MouseEvent&) {
    if (dragRow >= 0)
        if (auto* p = param(Mod::slotAmtID(dragRow))) p->endChangeGesture();
    dragRow = -1;
    repaint();
}

void CrtMatrix::mouseDoubleClick(const juce::MouseEvent& e) {
    int r = rowAt(e.getPosition());
    if (r < 0 || colAt(e.getPosition()) != COL_AMT) return;
    float a = getAmount(r);
    if (std::abs(a) > 0.0001f) { savedAmount[(size_t)r] = a; setParamReal(Mod::slotAmtID(r), 0.0f); }
    else                         setParamReal(Mod::slotAmtID(r), savedAmount[(size_t)r]);
}

void CrtMatrix::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    int r = rowAt(e.getPosition());
    if (r < 0 || colAt(e.getPosition()) != COL_AMT) return;
    float step = e.mods.isShiftDown() ? 0.1f : 1.0f;
    float dir = (w.deltaY > 0.0f) ? 1.0f : (w.deltaY < 0.0f ? -1.0f : 0.0f);
    setParamReal(Mod::slotAmtID(r), juce::jlimit(-100.0f, 100.0f, getAmount(r) + dir * step));
}

void CrtMatrix::showMenu(int row, int col) {
    const bool isSrc = (col == COL_SRC);
    const auto& names = isSrc ? Mod::sourceNames() : Mod::destNames();
    const auto id = isSrc ? Mod::slotSrcID(row) : Mod::slotDstID(row);
    const int current = getChoice(id);

    juce::PopupMenu m;
    for (int i = 0; i < names.size(); ++i) {
        // separators between the groups of the list
        if (isSrc && (i == 1 || i == 3 || i == 14 || i == 16 || i == 19)) m.addSeparator();
        if (!isSrc && (i == 1 || i == 4 || i == 8 || i == 12 || i == 14 || i == 17 || i == 19)) m.addSeparator();
        m.addItem(i + 1, names[i], true, i == current);
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this)
                        .withTargetScreenArea(localAreaToGlobal(cellBounds(row, col)))
                        .withMinimumWidth(cellBounds(row, col).getWidth()),
        [this, id](int res) {
            if (res > 0) setParamReal(id, (float)(res - 1));
        });
}
