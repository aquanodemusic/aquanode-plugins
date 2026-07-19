#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SpectralLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ── Rotary knob ──────────────────────────────────────────────────────────
    void drawRotarySlider(juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPos,
        float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override
    {
        const float cx = x + width * 0.5f;
        const float cy = y + height * 0.5f;
        const float r = juce::jmin(width, height) * 0.44f;

        auto accent = slider.findColour(juce::Slider::thumbColourId);
        auto trackBg = slider.findColour(juce::Slider::backgroundColourId);

        // ── Drop shadow ──────────────────────────────────────────────────────
        {
            juce::ColourGradient sh(juce::Colours::black.withAlpha(0.75f), cx, cy + r * 0.6f,
                juce::Colours::transparentBlack, cx, cy + r + 9.f, true);
            g.setGradientFill(sh);
            g.fillEllipse(cx - r - 4.f, cy - r - 4.f, (r + 4.f) * 2.f, (r + 4.f) * 2.f);
        }

        // ── Body gradient ────────────────────────────────────────────────────
        {
            juce::ColourGradient body(juce::Colour(0xff3e3e3e), cx, cy - r,
                juce::Colour(0xff0b0b0b), cx, cy + r, false);
            g.setGradientFill(body);
            g.fillEllipse(cx - r, cy - r, r * 2.f, r * 2.f);
        }

        // ── Outer rim ────────────────────────────────────────────────────────
        g.setColour(juce::Colour(0xff4a4a4a));
        g.drawEllipse(cx - r, cy - r, r * 2.f, r * 2.f, 1.f);

        // ── Background arc ───────────────────────────────────────────────────
        {
            const float tr = r - 5.f;
            juce::Path arc;
            arc.addArc(cx - tr, cy - tr, tr * 2.f, tr * 2.f,
                rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(trackBg.withAlpha(0.35f));
            g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // ── Value arc + glow ─────────────────────────────────────────────────
        if (sliderPos > 0.001f)
        {
            const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            const float tr = r - 5.f;
            juce::Path arc;
            arc.addArc(cx - tr, cy - tr, tr * 2.f, tr * 2.f,
                rotaryStartAngle, angle, true);
            // Glow halo
            g.setColour(accent.withAlpha(0.18f));
            g.strokePath(arc, juce::PathStrokeType(9.f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
            // Core arc
            g.setColour(accent.withAlpha(0.92f));
            g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        // ── Pointer ──────────────────────────────────────────────────────────
        {
            const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            const float sina = std::sin(angle);
            const float cosa = std::cos(angle);
            const float inner = r * 0.22f;
            const float outer = r - 7.f;

            g.setColour(accent.withAlpha(0.28f));                         // glow
            g.drawLine(cx + sina * inner, cy - cosa * inner,
                cx + sina * outer, cy - cosa * outer, 4.5f);
            g.setColour(accent);                                            // line
            g.drawLine(cx + sina * inner, cy - cosa * inner,
                cx + sina * outer, cy - cosa * outer, 1.5f);
        }

        // ── Highlight sheen (top-left lens effect) ───────────────────────────
        {
            juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.13f),
                cx - r * 0.35f, cy - r * 0.65f,
                juce::Colours::transparentBlack,
                cx + r * 0.25f, cy + r * 0.15f, false);
            g.setGradientFill(sheen);
            g.fillEllipse(cx - r * 0.88f, cy - r * 0.88f, r * 1.76f, r * 1.76f);
        }

        // ── Centre pip ───────────────────────────────────────────────────────
        g.setColour(accent.withAlpha(0.55f));
        g.fillEllipse(cx - 2.2f, cy - 2.2f, 4.4f, 4.4f);
    }

    // ── Textbox for knobs ────────────────────────────────────────────────────
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox(slider);
        l->setColour(juce::Label::textColourId, slider.findColour(juce::Slider::thumbColourId));
        l->setColour(juce::Label::backgroundColourId, juce::Colour(0xff0e0e0e));
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour(juce::TextEditor::textColourId, slider.findColour(juce::Slider::thumbColourId));
        l->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0e0e0e));
        l->setFont(juce::Font(10.f));
        return l;
    }

    // ── Toggle button ─────────────────────────────────────────────────────────
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour& backgroundColour,
        bool isHighlighted, bool isDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto col = isDown ? backgroundColour.brighter(0.15f)
            : isHighlighted ? backgroundColour.brighter(0.07f)
            : backgroundColour;
        g.setColour(col);
        g.fillRoundedRectangle(bounds, 3.f);
        g.setColour(juce::Colour(0xff3a3a3a));
        g.drawRoundedRectangle(bounds, 3.f, 1.f);
    }

    // ── ComboBox ─────────────────────────────────────────────────────────────
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
        int, int, int, int, juce::ComboBox& box) override
    {
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(0.f, 0.f, (float)width, (float)height, 3.f);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(0.5f, 0.5f, width - 1.f, height - 1.f, 3.f, 1.f);

        juce::Path arrow;
        const float arrowX = (float)width - 14.f;
        const float arrowY = height * 0.5f;
        arrow.addTriangle(arrowX, arrowY - 3.f, arrowX + 8.f, arrowY - 3.f, arrowX + 4.f, arrowY + 3.f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }
};

//==============================================================================
class SpectralResolverEditor : public juce::AudioProcessorEditor,
    private juce::Timer
{
public:
    explicit SpectralResolverEditor(SpectralResolverProcessor&);
    ~SpectralResolverEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void toggleSidebar();

    // Colour param helpers
    juce::Colour getColourParam(int idx) const;  // 0=BG, 1=GradLow, 2=GradHigh
    void         setColourParam(int idx, juce::Colour c);
    void         applyHexInput(juce::TextEditor& te, int colIdx);
    void         syncHexBoxes();
    void         updateSliderColours();

    // Forward-map reassigned bins to pixel rows, computing sub-pixel alpha
    // from each bin's log-scale frequency extent.  Replaces frameToPixelMags.
    void frameToPixelMagsAlphas(const SpectralFrame& frame,
        float freqLow, float freqHigh, float binHz,
        std::vector<float>& outMags,
        std::vector<float>& outAlphas) const;

    // Layout constants
    static constexpr int CTRL_H = 162;
    static constexpr int TITLE_H = 20;
    static constexpr int PAD = 8;

    // Sidebar state
    bool sidebarVisible{ true };
    juce::ComponentBoundsConstrainer resizeConstrainer;
    std::unique_ptr<juce::ResizableBorderComponent> borderResizer;

    // Temporal interpolation — cosine blend between consecutive frames in pixel-row space.
    // prevMainPixMags[y] holds the pixel-row mags from the last painted main frame;
    // each new scroll column is a unique cosine blend between prev and curr,
    // so there are no repeated horizontal bars and transitions are smooth.
    // prevMain/ScPixAlphas[y] carries the sub-pixel coverage (0–1) for each row;
    // this gives high-frequency bins a narrower vertical mark proportional to
    // their log-scale bandwidth, reducing blur in the HF region.
    std::vector<float> prevMainPixMags;
    std::vector<float> prevMainPixAlphas;
    std::vector<float> prevScPixMags;
    std::vector<float> prevScPixAlphas;

    SpectralResolverProcessor& processor;
    SpectralLookAndFeel        laf;

    // Spectrogram image
    static constexpr int IMG_W = 1024;
    static constexpr int IMG_H = 512;
    juce::Image spectroImage{ juce::Image::ARGB, IMG_W, IMG_H, true };

    SpectralFrame lastFrame;
    SpectralFrame scLastFrame;
    bool          hasLastFrame{ false };
    bool          hasScLastFrame{ false };
    juce::int64   lastFrameTimeMs{ 0 };
    juce::int64   scLastFrameTimeMs{ 0 };
    static constexpr juce::int64 SILENCE_TIMEOUT_MS = 300;

    juce::Colour lastBgColour{ juce::Colour(13, 13, 13) };
    juce::Colour lastGradLow{ juce::Colours::black };
    juce::Colour lastGradHigh{ juce::Colours::white };

    // Sidebar toggle button (always visible in title bar)
    juce::TextButton btnToggleSidebar;

    // Rotary sliders
    juce::Slider sliderLow, sliderHigh, sliderThresh, sliderReassign, sliderSpeed;
    juce::Label  labelLow, labelHigh, labelThresh, labelReassign, labelSpeed;

    // Combo boxes
    juce::ComboBox comboFFT, comboHop, comboWindow, comboDecim, comboInterp;
    juce::Label    labelFFT, labelHop, labelWindow, labelDecim, labelInterp;

    // Hex colour inputs
    juce::TextEditor hexBgColor, hexGradLow, hexGradHigh;
    juce::Label      lblBgColor, lblGradLow, lblGradHigh;

    // APVTS attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        attachLow, attachHigh, attachThresh, attachReassign, attachSpeed;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        attachFFT, attachHop, attachWindow, attachDecim, attachInterp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralResolverEditor)
};