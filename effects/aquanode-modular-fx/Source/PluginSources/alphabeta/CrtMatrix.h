#pragma once
#include <JuceHeader.h>
#include "ModMatrix.h"

class AlphaBetaAudioProcessor;

// What the screen edits: the native matrix by default, or the original
// engine's matrix (other parameter IDs, name lists and amount scaling).
struct CrtConfig {
    int numSlots = Mod::NUM_SLOTS;
    std::function<juce::String(int)> srcID = Mod::slotSrcID, amtID = Mod::slotAmtID, dstID = Mod::slotDstID;
    juce::StringArray srcNames = Mod::sourceNames(), dstNames = Mod::destNames();
    std::function<juce::String(float amt, int dest)> formatAmount;     // empty = native formatting
    std::vector<int> srcSeparators{ 1, 3, 14, 16, 19 }, dstSeparators{ 1, 4, 8, 12, 14, 17, 19 };
    juce::String title = "MATRIX";
};

// ==============================================================
//  CrtMatrix – the modulation matrix drawn as a blue phosphor
//  screen, like the display in the original Alpha 3.
//
//  click source / destination  -> pick from a menu
//  drag amount up/down         -> change depth (shift = fine)
//  mouse wheel on amount       -> nudge
//  double-click amount         -> set to 0, again -> restore
//  right-click a row           -> clear the slot
// ==============================================================
class CrtMatrix : public juce::Component, private juce::Timer {
public:
    explicit CrtMatrix(AlphaBetaAudioProcessor& p, CrtConfig config = {});
    ~CrtMatrix() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Temporary message on the status line (fades after a few seconds)
    void showStatus(const juce::String& text);

private:
    enum Col { COL_NONE = -1, COL_SRC = 0, COL_AMT, COL_DST };

    AlphaBetaAudioProcessor& proc;
    CrtConfig cfg;

    juce::Rectangle<int> screen;        // glass area
    juce::Rectangle<int> rowsArea;      // the 11 slot rows
    int rowH = 18;
    int colX[4] = {};                   // idx | src | amt | dst | end

    int hoverRow = -1, hoverCol = COL_NONE;
    int dragRow = -1;
    float dragStartValue = 0.0f;
    std::vector<float> savedAmount;

    juce::String status;
    double statusTime = 0.0;
    juce::String lastSnapshot;
    juce::Image scanlines;              // cached overlay
    int scanVersion = -1;               // Pal::version the overlay was built with

    void timerCallback() override;
    juce::String snapshot() const;

    int rowAt(juce::Point<int>) const;
    int colAt(juce::Point<int>) const;
    juce::Rectangle<int> cellBounds(int row, int col) const;

    juce::RangedAudioParameter* param(const juce::String& id) const;
    int   getChoice(const juce::String& id) const;
    float getAmount(int slot) const;
    void  setParamReal(const juce::String& id, float value);
    void  showMenu(int row, int col);

    juce::String formatAmount(float amt, int dest) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrtMatrix)
};
