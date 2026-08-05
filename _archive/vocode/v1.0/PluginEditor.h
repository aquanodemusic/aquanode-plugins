#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Both colour palettes live here so VocoderDisplay can read the active one
// via the pointer reference passed in from the editor.
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

inline const ColorSet& earthPalette()
{
    static const ColorSet p{
        juce::Colour(0xff1a1714),  // background   -- soft charcoal earth
        juce::Colour(0xff23201d),  // panelBg      -- deep shadow clay
        juce::Colour(0xff12100e),  // displayBg    -- deep crevice soil
        juce::Colour(0xffa67c5e),  // accent       -- weathered stone/clay
        juce::Colour(0xffd4b483),  // accentBright -- soft sandstone
        juce::Colour(0xff4a3728),  // accentDark   -- rich coffee bean
        juce::Colour(0xff8c7360),  // accentDim    -- muted beige-brown
        juce::Colour(0xffe0d1b5),  // barTop       -- limestone dust
        juce::Colour(0xff9e7a60),  // barFill      -- sun-dried clay
        juce::Colour(0xffc9b9a6),  // textCol      -- natural parchment
        juce::Colour(0xff75695a),  // textDim      -- weathered wood
        juce::Colour(0xff2e2a25),  // gridLine     -- subtle silt vein
        juce::Colour(0xff3d3228),  // autoBadgeBg  -- dark muted brown
        juce::Colour(0xffd4b483),  // autoBadgeFg  -- soft ochre
    };
    return p;
}

// Classic green terminal palette (original)
inline const ColorSet& greenPalette()
{
    static const ColorSet p{
        juce::Colour(0xff0a120a),  // background
        juce::Colour(0xff0d180d),  // panelBg
        juce::Colour(0xff050d05),  // displayBg
        juce::Colour(0xff00cc44),  // accent
        juce::Colour(0xff00ff66),  // accentBright
        juce::Colour(0xff004422),  // accentDark
        juce::Colour(0xff007733),  // accentDim
        juce::Colour(0xff44ff88),  // barTop
        juce::Colour(0xff00cc44),  // barFill
        juce::Colour(0xff00dd55),  // textCol
        juce::Colour(0xff008833),  // textDim
        juce::Colour(0xff071207),  // gridLine
        juce::Colour(0xff003311),  // autoBadgeBg
        juce::Colour(0xff44ff88),  // autoBadgeFg
    };
    return p;
}

//==============================================================================
class VocoderDisplay : public juce::Component,
    private juce::Timer
{
public:
    // pal is a reference to the editor's activePalette pointer, so
    // the display always draws with whichever palette is currently live.
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

    VocodeAudioProcessor& proc;
    const ColorSet*& pal;   // reference to editor's pointer
    int dragIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderDisplay)
};

//==============================================================================
/**
 * Control row (left to right):
 *   BANDS | ATTACK | RELEASE | BAND ORDER | COMP | DRY/WET | GAIN
 *
 * Top-right corner: palette toggle button.
 *   Earth palette active  -> button reads "GREEN DESIGN"
 *   Green palette active  -> button reads "BROWN DESIGN"
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
    // it automatically picks up the new value when the toggle fires.
    const ColorSet* activePalette = &earthPalette();

    juce::Slider bandsSlider, attackSlider, releaseSlider,
        orderSlider, compressionSlider, dryWetSlider, gainSlider;

    juce::Label  bandsLabel, attackLabel, releaseLabel,
        orderLabel, compressionLabel, dryWetLabel, gainLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        bandsAttach, attackAttach, releaseAttach,
        orderAttach, compressionAttach, dryWetAttach, gainAttach;

    VocoderDisplay   display;
    juce::TextButton paletteToggle;

    void setupKnob(juce::Slider& s, juce::Label& l, const juce::String& text);
    void applyPaletteToKnobs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocodeAudioProcessorEditor)
};