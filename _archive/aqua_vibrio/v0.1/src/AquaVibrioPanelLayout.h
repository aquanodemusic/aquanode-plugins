#pragma once
//==============================================================================
//  AquaVibrioPanelLayout.h
//
//  Where each region sits on the panel. The editor packs cards as masonry,
//  four equal-width lanes wide, and drops each one into whichever lane is
//  currently shortest - so the table's order is what decides which lane a
//  card lands in. The table below is ordered so the first four top-level
//  cards claim the four lanes as Oscillator, Filter, Effects, Mixer, with
//  everything else (envelopes, LFOs, matrix slots, arp, common) flowing in
//  after.
//
//  Two things here are advisory and one is not:
//
//    knobColumns  decides the module's WIDTH - it gets exactly that many knob
//                 columns and is sized to fit them. This is the field to edit.
//    row          is only the order things are packed in, which keeps related
//                 blocks near each other. It no longer forms a visual row.
//    widthWeight  is unused now; kept so the table still reads as intended.
//==============================================================================

#include <JuceHeader.h>

namespace aquavibrio
{

struct RegionSlot
{
    const char* regionId;   // matches ParamInfo::region
    const char* title;      // what the header bar says
    int         row;
    float       widthWeight;  // unused since the masonry change - see above
    int         knobColumns; // authoritative: sets both the columns and the width

    // Panels sharing a group name are stacked into ONE card with a selector
    // at the top - the three oscillators, the two filters, the four
    // envelopes, the six matrix slots and the effects rack. The card is
    // sized for whichever member needs the most room, so switching to a
    // smaller one leaves empty space rather than resizing the whole panel.
    const char* group;
};

struct RowSpec
{
    int row;
    int minHeight;   // unused since the masonry change
};

inline const std::vector<RowSpec>& panelRows()
{
    static const std::vector<RowSpec> rows
    {
        { 0, 200 },   // oscillators + amp
        { 1, 180 },   // filter, envelopes, LFOs
        { 2, 150 },   // modulation matrix and assigns
        { 3, 150 },   // effects rack
    };
    return rows;
}

inline const std::vector<RegionSlot>& panelLayout()
{
    static const std::vector<RegionSlot> slots
    {
        // The masonry packer (see AquaVibrioEditor::resized()) walks this list
        // in order and drops each card into whichever of the four unit-wide
        // lanes is currently shortest; since every card is the same width,
        // the first four cards it meets claim the four lanes in the order
        // they appear here. So the ORDER of the first member of each
        // top-level card below *is* the column order on screen:
        //   column 1 - Oscillator, column 2 - Filter,
        //   column 3 - Effects (fx rack), column 4 - Amp / Mixer.
        // Everything after that (envelopes, LFOs, matrix slots, arp, common)
        // just flows into whichever lane is shortest at that point.

        // --- column 1: oscillator ---------------------------------------------
        { "oscCommon",     "Oscillator Common", 0, 1.30f, 3 , "Oscillator" },
        { "oscA",          "Oscillator 1",      0, 1.45f, 3 , "Oscillator" },
        { "oscB",          "Oscillator 2",      0, 1.45f, 3 , "Oscillator" },
        { "oscC",          "Osc 3 / Noise",     0, 0.72f, 3 , "Oscillator" },
        { "noise",         "Noise",             0, 0.55f, 3 , "Oscillator" },

        // --- column 2: filter ---------------------------------------------------
        { "filterA",       "Filter 1",          1, 0.95f, 3 , "Filter" },
        { "filterB",       "Filter 2",          1, 0.95f, 3 , "Filter" },
        { "filterCommon",  "Filter Common",     1, 0.85f, 3 , "Filter" },

        // --- column 3: effects rack (unison shares this group/selector too) ---
        { "unison",        "Unison",            2, 0.62f, 3 , "Effects" },
        { "eq",            "EQ",                2, 0.80f, 3 , "Effects" },
        { "bassBoost",     "Bass Boost",        2, 0.45f, 3 , "Effects" },
        { "chorus",        "Chorus",            2, 0.75f, 3 , "Effects" },
        { "phaser",        "Phaser",            2, 0.80f, 3 , "Effects" },
        { "filterBank",    "Filter Bank",       2, 0.70f, 3 , "Effects" },
        { "distortion",    "Distortion",        2, 0.62f, 3 , "Effects" },
        { "punch",         "Charac",            2, 0.42f, 3 , "Effects" },
        { "delay",         "Delay",             2, 0.85f, 3 , "Effects" },
        { "reverb",        "Reverb",            2, 0.85f, 3 , "Effects" },
        { "vocoder",       "Vocoder",           2, 0.70f, 3 , "Effects" },
        { "inputFollower", "Input Follower",    2, 0.55f, 3 , "Effects" },
        { "atomizer",      "Atomizer",          2, 0.50f, 3 , "Effects" },
        { "input",         "Input",             2, 0.45f, 3 , "Effects" },

        // --- column 4: mixer -----------------------------------------------------
        { "amp",           "Amp / Mixer",       3, 1.05f, 3 , "" },

        // --- modulation, flows after the four named columns above --------------
        { "envFilter",     "Filter Envelope",   4, 0.70f, 3 , "Envelope" },
        { "envAmp",        "Amp Envelope",      4, 0.70f, 3 , "Envelope" },
        { "env3",          "Env 3",             4, 0.62f, 3 , "Envelope" },
        { "env4",          "Env 4",             4, 0.62f, 3 , "Envelope" },
        { "lfoA",          "LFO 1",             4, 1.00f, 3 , "LFO" },
        { "lfoB",          "LFO 2",             4, 1.00f, 3 , "LFO" },
        { "lfoC",          "LFO 3",             4, 0.85f, 3 , "LFO" },

        // The Matrix module used to live here as its own one-knob card; that
        // knob (AssignsSlotSelect) now belongs to region "assignA" instead,
        // so it appears inside Matrix Slot 1 in the group below, and the
        // module itself - which had nothing else in it - is gone.
        { "assignA",       "Matrix Slot 1",     4, 0.72f, 3 , "Matrix Slot" },
        { "assignB",       "Matrix Slot 2",     4, 0.72f, 3 , "Matrix Slot" },
        { "assignC",       "Matrix Slot 3",     4, 0.72f, 3 , "Matrix Slot" },
        { "assignD",       "Matrix Slot 4",     4, 0.72f, 3 , "Matrix Slot" },
        { "assignE",       "Matrix Slot 5",     4, 0.72f, 3 , "Matrix Slot" },
        { "assignF",       "Matrix Slot 6",     4, 0.72f, 3 , "Matrix Slot" },
        { "arp",           "Arpeggiator",       4, 1.40f, 3 , "" },
        { "common",        "Common",            4, 0.70f, 3 , "" },
    };
    return slots;
}

} // namespace aquavibrio
