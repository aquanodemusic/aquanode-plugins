#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <mutex>
#include <memory>

//==============================================================================
/**  SpectrogramView
 *   Renders the 2-D spectral canvas and handles click-drag selection.
 *   Supports independent horizontal and vertical zoom (1x – 8x).
 *   Draws an orange start-position marker and a green moving playhead.
 *
 *   Y-axis is logarithmically scaled (base 20 Hz) so the 0–2 kHz region
 *   receives the majority of the vertical space.
 *
 *   Edit modes:
 *     0 = Select  – rubber-band rectangle selection (original behaviour)
 *     1 = Draw    – freehand spectral paint with configurable thickness + harmonics
 *     2 = Smear   – time-domain moving-average brush (magnitude-preserving)
 *     3 = Scrub   – drag to set the playback start position
 */
class SpectrogramView : public juce::Component
{
public:
    explicit SpectrogramView(SpectralEditProcessor& p);

    void paint(juce::Graphics& g) override;
    void resized()                   override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void rebuildImage();

    // ── Horizontal scroll (in frames) ────────────────────────────────────────
    void setScrollOffset(int o)  noexcept { scrollOffset = o; repaint(); }
    int  getScrollOffset()  const noexcept { return scrollOffset; }

    // ── Vertical scroll (in image-pixels from the top = Nyquist) ─────────────
    void setVScrollOffset(int o) noexcept { vScrollOffset = o; repaint(); }
    int  getVScrollOffset() const noexcept { return vScrollOffset; }

    // ── Zoom (1.0 = 1 px per frame/bin, higher = zoomed in) ─────────────────
    void setZoom(float x, float y) noexcept;
    float getZoomX() const noexcept { return zoomX; }
    float getZoomY() const noexcept { return zoomY; }

    int getTotalImageWidth()  const noexcept { return imgValid ? cachedNumFrames : 0; }
    int getTotalImageHeight() const noexcept { return imgValid ? cachedImgHeight : 0; }

    SpectralEditProcessor::SelRect getSelection() const noexcept { return sel; }
    void clearSelection() noexcept { sel = {}; repaint(); }

    // ── Edit mode ─────────────────────────────────────────────────────────────
    void setEditMode(int m) noexcept;          // 0=Select, 1=Draw, 2=Blur
    int  getEditMode()      const noexcept { return editMode; }

    // ── Draw tool parameters ─────────────────────────────────────────────────
    void setDrawThickness(int bins) noexcept { drawThickness = juce::jlimit(1, 200, bins); }
    void setDrawAmplitude(float a)  noexcept { drawAmplitude = juce::jlimit(0.0f, 1.0f, a); }
    void setDrawHarmonics(int n)    noexcept { drawHarmonics = juce::jlimit(1, 64, n); }

    // ── Smear tool parameters ──────────────────────────────────────────────────
    void setSmearRadius(int r) noexcept { smearRadius = juce::jlimit(1, 60, r); }

private:
    SpectralEditProcessor& proc;

    // ── Cached spectrogram image ──────────────────────────────────────────────
    juce::Image  img;
    bool         imgValid = false;
    int          cachedNumFrames = 0;
    int          cachedImgHeight = 0;
    float        cachedInvMax = 1.0f;   // 1 / maxMag, stored for partial rebuilds

    // ── Selection state (mode 0) ──────────────────────────────────────────────
    SpectralEditProcessor::SelRect sel;
    bool              isDragging = false;
    juce::Point<int>  anchor, liveEnd;

    // ── Scroll / zoom ─────────────────────────────────────────────────────────
    int   scrollOffset = 0;
    int   vScrollOffset = 0;
    float zoomX = 1.0f;
    float zoomY = 1.0f;

    // ── Edit mode & tool params ───────────────────────────────────────────────
    int   editMode = 0;
    int   drawThickness = 5;     // half-width in bins
    float drawAmplitude = 0.75f; // normalised 0..1
    int   drawHarmonics = 1;     // 1 = fundamental only, up to 64
    int   smearRadius = 8;     // in frames

    juce::Point<int> brushPos;   // last known mouse position for cursor drawing

    // ── Coordinate helpers (account for scroll, zoom, and log-scale Y) ───────
    int pxToFrame(int px) const noexcept;
    int pyToBin(int py) const noexcept;
    int frameToPx(int f)  const noexcept;
    int binToPy(int b)  const noexcept;

    // ── Drawing helpers ───────────────────────────────────────────────────────
    void applyBrushAt(juce::Point<int> pos);
    void rebuildImageColumns(int f0, int f1);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrogramView)
};

//==============================================================================
/**  SpectralEditEditor
 *   Layout:
 *     Top toolbar  : [Import WAV] [Export WAV] | filename | [FFT size] | status
 *     Mode strip   : [Select] [Draw] [Blur]  Thickness:[=] Amp:[=] Blur R:[=]
 *     Left canvas  : SpectrogramView (with H/V zoom sliders and scrollbars)
 *     Right sidebar: operation buttons and knobs
 *     Bottom strip : MIDI keyboard (full plugin width)
 */
class SpectralEditEditor : public juce::AudioProcessorEditor,
    public juce::Timer,
    public juce::ScrollBar::Listener
{
public:
    explicit SpectralEditEditor(SpectralEditProcessor&);
    ~SpectralEditEditor() override;

    void paint(juce::Graphics&) override;
    void resized()                 override;
    void timerCallback()            override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    bool keyPressed(const juce::KeyPress& key) override;

private:
    SpectralEditProcessor& proc;

    // ── Toolbar ──────────────────────────────────────────────────────────────
    juce::TextButton importBtn{ "Import WAV" };
    juce::TextButton exportBtn{ "Export WAV" };
    juce::Label      fileLabel, statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> exportChooser;

    // FFT window size selector
    juce::Label    fftSizeLbl{ {}, "FFT:" };
    juce::ComboBox fftSizeBox;

    // ── Mode strip ────────────────────────────────────────────────────────────
    juce::TextButton selectModeBtn{ "Select" };
    juce::TextButton drawModeBtn{ "Draw" };
    juce::TextButton smearModeBtn{ "Smear" };
    juce::TextButton scrubModeBtn{ "Scrub" };

    juce::Label  drawThickLbl{ {}, "Thickness:" };
    juce::Slider drawThickSlider;

    juce::Label  drawAmpLbl{ {}, "Amplitude:" };
    juce::Slider drawAmpSlider;

    juce::Label  harmonicsLbl{ {}, "Harmonics:" };
    juce::Slider harmonicsSlider;

    juce::Label  smearRadiusLbl{ {}, "Smear R:" };
    juce::Slider smearRadiusSlider;

    juce::Label  scrubSpeedLbl{ {}, "Scrub Spd:" };
    juce::Slider scrubSpeedSlider;

    // ── Canvas ───────────────────────────────────────────────────────────────
    SpectrogramView  spectrogramView;

    // Horizontal zoom (below H-scrollbar) – spans full canvas width
    juce::ScrollBar  scrollBar{ false };
    juce::Slider     hZoomSlider;
    juce::Label      hZoomLabel{ {}, "H" };

    // Vertical scroll + zoom (right edge of canvas)
    juce::ScrollBar  vScrollBar{ true };
    juce::Slider     vZoomSlider;
    juce::Label      vZoomLabel{ {}, "V" };

    // ── Sidebar ───────────────────────────────────────────────────────────────
    juce::Label      sideTitle{ {}, "OPERATIONS" };

    juce::TextButton mirrorLRBtn{ "Mirror L / R" };
    juce::TextButton mirrorUDBtn{ "Mirror U / D" };
    juce::TextButton deleteSelBtn{ "Delete" };

    // Start position
    juce::Label      startKnobLbl{ {}, "Start Pos" };
    juce::Slider     startKnob;

    // Rotate Bins
    juce::Label      rotBinsLbl{ {}, "Rotate Bins" };
    juce::Slider     rotBinsKnob;

    // Rotate Right
    juce::Label      rotRightLbl{ {}, "Rotate Right" };
    juce::Slider     rotRightKnob;

    // Change Volume
    juce::Label      volLbl{ {}, "Change Volume" };
    juce::Slider     volKnob;

    // Spectral Compress
    juce::Label      compressStrLbl{ {}, "Compress Str" };
    juce::Slider     compressStrKnob;
    juce::TextButton compressBtn{ "Compress Sel" };

    // Spectral Freeze
    juce::TextButton freezeBtn{ "Freeze Sel" };

    // ── Keyboard ─────────────────────────────────────────────────────────────
    juce::MidiKeyboardComponent keyboard;

    bool prevCompiling = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    static void initKnob(juce::Slider& s, double lo, double hi,
        double def, const juce::String& suffix = {});
    static void initZoomSlider(juce::Slider& s, bool vertical);
    static void initModeStripSlider(juce::Slider& s, double lo, double hi, double def);
    void styleButton(juce::TextButton& b,
        juce::Colour bg = juce::Colour(0xff1c4f8a),
        juce::Colour txt = juce::Colours::white);
    void updateScrollbar();
    void updateVScrollbar();
    void updateModeButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralEditEditor)
};