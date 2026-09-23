#pragma once
#include <JuceHeader.h>

// ==============================================================
//  LinPlug Alpha 3 .fxp importer
//
//  Alpha 3 stores its patches as an opaque VST chunk ("FPCh",
//  plugin ID 'LwAF'). The chunk is a list of tagged blocks:
//
//      [tag: 4 bytes, reversed ASCII] [size: int32 LE, incl. header] [body]
//
//  Bodies are int32 LE values mixed with length-prefixed strings.
//  Block meanings were worked out from one factory patch, so the
//  mapping below carries a confidence level per field. Everything
//  the importer reads is written to a report so it can be checked
//  against the original synth and refined.
// ==============================================================
namespace Alpha3Import {

    struct Result {
        bool ok = false;
        juce::String patchName;
        juce::String summary;   // one line for the CRT status bar
        juce::String report;    // full decode report (plain text)
    };

    // Parses `file` and writes the recognised values into `apvts`
    // (all parameters are first reset to their defaults).
    // Must be called on the message thread.
    Result importFile(const juce::File& file, juce::AudioProcessorValueTreeState& apvts);
}
