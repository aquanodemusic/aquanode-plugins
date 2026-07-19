#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Colour palette descriptor — every colour the UI ever touches lives here.
//==============================================================================
struct ColorSet
{
    juce::Colour background;
    juce::Colour panelBg;
    juce::Colour displayBg;
    juce::Colour accent;
    juce::Colour accentBright;
    juce::Colour accentDark;
    juce::Colour accentDim;
    juce::Colour barTop;
    juce::Colour barFill;
    juce::Colour textCol;
    juce::Colour textDim;
    juce::Colour gridLine;
    juce::Colour autoBadgeBg;
    juce::Colour autoBadgeFg;
};

//==============================================================================
// Palette 1: Green Black — classic phosphor terminal (original dark design)
//==============================================================================
inline const ColorSet& greenBlackPalette()
{
    static const ColorSet p{
        juce::Colour(0xff0a120a),  // background   -- deep terminal black-green
        juce::Colour(0xff0d180d),  // panelBg      -- dark panel
        juce::Colour(0xff050d05),  // displayBg    -- near-black display
        juce::Colour(0xff00cc44),  // accent       -- phosphor green
        juce::Colour(0xff00ff66),  // accentBright -- hot green
        juce::Colour(0xff004422),  // accentDark   -- dark forest
        juce::Colour(0xff007733),  // accentDim    -- dim green
        juce::Colour(0xff44ff88),  // barTop       -- bright cap
        juce::Colour(0xff00cc44),  // barFill      -- phosphor fill
        juce::Colour(0xff00dd55),  // textCol      -- readable green
        juce::Colour(0xff008833),  // textDim      -- subdued green
        juce::Colour(0xff071207),  // gridLine     -- subtle scan line
        juce::Colour(0xff003311),  // autoBadgeBg
        juce::Colour(0xff44ff88),  // autoBadgeFg
    };
    return p;
}

//==============================================================================
// Palette 2: Green White — light background, forest-green accents
//==============================================================================
inline const ColorSet& greenWhitePalette()
{
    static const ColorSet p{
        juce::Colour(0xfff2f7f3),  // background   -- fresh white-green
        juce::Colour(0xffe2efe4),  // panelBg      -- soft sage panel
        juce::Colour(0xffd2e5d5),  // displayBg    -- pale mint display
        juce::Colour(0xff2a7a3a),  // accent       -- medium forest green
        juce::Colour(0xff3aaa50),  // accentBright -- bright leaf green
        juce::Colour(0xff1a4d25),  // accentDark   -- deep forest
        juce::Colour(0xff5a9a68),  // accentDim    -- muted sage
        juce::Colour(0xffe8f8ec),  // barTop       -- near-white cap
        juce::Colour(0xff359a48),  // barFill      -- mid green fill
        juce::Colour(0xff152a1a),  // textCol      -- near-black dark green
        juce::Colour(0xff486852),  // textDim      -- muted green-slate
        juce::Colour(0xffc0dcc5),  // gridLine     -- subtle pale green
        juce::Colour(0xff1a4d25),  // autoBadgeBg  -- dark green badge
        juce::Colour(0xffe8f8ec),  // autoBadgeFg  -- near-white text
    };
    return p;
}

//==============================================================================
// Palette 3: Brown White — warm wooden/parchment; Claude-inspired earth tones
//==============================================================================
inline const ColorSet& brownWhitePalette()
{
    static const ColorSet p{
        juce::Colour(0xfffff8f0),  // background   -- warm ivory
        juce::Colour(0xffede0cc),  // panelBg      -- aged cream panel
        juce::Colour(0xffe0cfb8),  // displayBg    -- parchment display
        juce::Colour(0xff7a4a20),  // accent       -- walnut brown
        juce::Colour(0xffc47a35),  // accentBright -- warm amber / honey oak
        juce::Colour(0xff4d2e10),  // accentDark   -- dark walnut
        juce::Colour(0xffaa7a50),  // accentDim    -- light oak
        juce::Colour(0xfffff5e5),  // barTop       -- near-white warm cap
        juce::Colour(0xff9a6035),  // barFill      -- mid walnut fill
        juce::Colour(0xff1e1008),  // textCol      -- near-black warm brown
        juce::Colour(0xff7a6050),  // textDim      -- medium wood brown
        juce::Colour(0xffddc8a8),  // gridLine     -- subtle wood-grain line
        juce::Colour(0xff4d2e10),  // autoBadgeBg  -- dark walnut badge
        juce::Colour(0xfffff5e5),  // autoBadgeFg  -- ivory text
    };
    return p;
}

//==============================================================================
// Palette 4: Blue White — light blue-grey background, navy blue bars & knobs
//==============================================================================
inline const ColorSet& blueWhitePalette()
{
    static const ColorSet p{
        juce::Colour(0xfff0f4f8),  // background   -- light blue-grey
        juce::Colour(0xffe0eaf5),  // panelBg      -- soft sky-blue panel
        juce::Colour(0xffd0dff0),  // displayBg    -- pale steel-blue display
        juce::Colour(0xff2d5ba3),  // accent       -- cornflower / royal blue
        juce::Colour(0xff4a80d8),  // accentBright -- bright cobalt
        juce::Colour(0xff1a3870),  // accentDark   -- deep navy
        juce::Colour(0xff5a80b8),  // accentDim    -- muted steel blue
        juce::Colour(0xfff0f8ff),  // barTop       -- alice-blue bright cap
        juce::Colour(0xff3a6cc0),  // barFill      -- mid cornflower fill
        juce::Colour(0xff0d1e38),  // textCol      -- near-black navy
        juce::Colour(0xff486080),  // textDim      -- muted slate-blue
        juce::Colour(0xffc0d4e8),  // gridLine     -- subtle pale blue grid
        juce::Colour(0xff1a3870),  // autoBadgeBg  -- deep navy badge
        juce::Colour(0xfff0f8ff),  // autoBadgeFg  -- alice-blue text
    };
    return p;
}

//==============================================================================
class VocoderDisplay : public juce::Component,
    private juce::Timer
{
public:
    VocoderDisplay(VocodeAudioProcessor& p, const ColorSet*& pal);
    ~VocoderDisplay() override;

    void paint(juce::Graphics& g) override;
    void resized() override {}

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override { repaint(); }
    int  getNumBands() const noexcept;
    void applyBrushAt(const juce::MouseEvent& e, int fromIndex, int toIndex);
    void saveBandVolumeCurve() noexcept;
    void loadBandVolumeCurve() noexcept;

    VocodeAudioProcessor& proc;
    const ColorSet*& pal;
    int  dragIndex = -1;
    bool isEditingBandVolume = false;

    // Per-band output-level multiplier, range 0.0 – 1.2.
    // Always applied to bar heights; user-drawable in "Band Vol" mode.
    float bandVolumeCurve[VocodeAudioProcessor::kMaxBands];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderDisplay)
};

//==============================================================================
/**
 * Control row: BANDS | ATTACK | RELEASE | BAND ORDER | COMP | DRY/WET | GAIN
 *
 * Top-right corner: ComboBox palette picker.
 *   1 = Green Black   (dark phosphor terminal)
 *   2 = Green White   (light, forest green)
 *   3 = Brown White   (light, warm wooden — default)
 *   4 = Blue White    (light, navy blue)
 *
 * Selected palette index is stored on apvts.state so it round-trips
 * through getStateInformation / setStateInformation automatically.
 */
class VocodeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit VocodeAudioProcessorEditor(VocodeAudioProcessor&);
    ~VocodeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    VocodeAudioProcessor& audioProcessor;

    // Active palette pointer. VocoderDisplay holds a reference to this so
    // it automatically picks up the new value when the combo fires.
    const ColorSet* activePalette = &brownWhitePalette();

    juce::Slider bandsSlider, attackSlider, releaseSlider,
        orderSlider, compressionSlider, dryWetSlider, gainSlider;

    juce::Label  bandsLabel, attackLabel, releaseLabel,
        orderLabel, compressionLabel, dryWetLabel, gainLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        bandsAttach, attackAttach, releaseAttach,
        orderAttach, compressionAttach, dryWetAttach, gainAttach;

    VocoderDisplay   display;
    juce::ComboBox   paletteCombo;

    // Returns the ColorSet for the given combo item ID (1-4).
    static const ColorSet* getPaletteForIndex(int id) noexcept;

    void setupKnob(juce::Slider& s, juce::Label& l, const juce::String& text);
    void applyPaletteToKnobs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessorEditor)
};