// ============================================================================
// PARKED - excluded from the build (compile="0" in AquaNova.jucer).
//
// The container format here is correct and verified against real bank dumps.
// What is not finished is the byte-to-parameter map in SysExMap.cpp, which is
// why loading a .syx moved almost no controls. Rather than ship something that
// half works, AquaNova now keeps its whole state in the APVTS and saves patches
// as .aquanova XML instead.
//
// To bring this back: finish the map (see the note in SysExMap.cpp), set both
// .cpp files to compile="1", and restore the load/save calls in
// PluginProcessor and the two buttons in PluginEditor.
// ============================================================================

#include "SysEx.h"
#include "Parameters.h"

namespace aquanova {
namespace SysExMap {

//==============================================================================
// WHAT IS CONFIRMED
//
// Verified against a real 13-program Supernova II bank dump:
//
//   * the container: 296-byte block 1 (type 00) + 98-byte block 2 (type 1E)
//   * the 16-character ASCII program name at offset 9 of block 1
//   * 270 bytes of voice data at offset 25 of block 1
//   * 88 bytes of effects data at offset 9 of block 2
//   * the three oscillator blocks are laid out identically and repeat every
//     34 bytes, starting at voice-data offset 4 (so osc 1 at 4, osc 2 at 38,
//     osc 3 at 72). Across thirteen programs every byte in those blocks stays
//     inside the range its parameter allows, and the bipolar slots sit at 64.
//
// WHAT IS NOT YET CONFIRMED
//
// Which parameter each byte inside a block is. Thirteen programs is not enough
// to pin all of them down by range analysis alone - too many parameters share
// the same 0..127 range and sat at their default in every patch. The entries
// below marked (inferred) are the ones the range analysis makes very likely;
// the rest of the layout is deliberately absent rather than guessed.
//
// Anything not listed here is preserved byte-for-byte on load and save, so an
// incomplete map can never corrupt a bank - it only means those controls do
// not move when you load a patch.
//
// TO FINISH THE MAP
//
// The reliable method is a differential one: set a single parameter on a real
// unit (or any emulation that already reads the format), dump the program,
// and diff it against a dump taken before the change. One byte moves. Repeat.
// Each finding is one line added below.
//==============================================================================

static constexpr int kOscBase   = 4;    // voice-data offset of oscillator 1
static constexpr int kOscStride = 34;   // confirmed

static const Entry kEntries[] =
{
    // ---- Oscillator 1 -------------------------------------------- (inferred)
    { P::Osc1Semitone,      1, kOscBase + 1 },
    { P::Osc1Octave,        1, kOscBase + 2 },
    { P::Osc1FineTune,      1, kOscBase + 5 },
    { P::Osc1Soften,        1, kOscBase + 7 },
    { P::Osc1MixLevel,      1, kOscBase + 15 },

    // ---- Oscillator 2 -------------------------------------------- (inferred)
    { P::Osc1Semitone  + P::OscStride, 1, kOscBase + kOscStride + 1 },
    { P::Osc1Octave    + P::OscStride, 1, kOscBase + kOscStride + 2 },
    { P::Osc1FineTune  + P::OscStride, 1, kOscBase + kOscStride + 5 },
    { P::Osc1Soften    + P::OscStride, 1, kOscBase + kOscStride + 7 },
    { P::Osc1MixLevel  + P::OscStride, 1, kOscBase + kOscStride + 15 },

    // ---- Oscillator 3 -------------------------------------------- (inferred)
    { P::Osc1Semitone  + 2 * P::OscStride, 1, kOscBase + 2 * kOscStride + 1 },
    { P::Osc1Octave    + 2 * P::OscStride, 1, kOscBase + 2 * kOscStride + 2 },
    { P::Osc1FineTune  + 2 * P::OscStride, 1, kOscBase + 2 * kOscStride + 5 },
    { P::Osc1Soften    + 2 * P::OscStride, 1, kOscBase + 2 * kOscStride + 7 },
    { P::Osc1MixLevel  + 2 * P::OscStride, 1, kOscBase + 2 * kOscStride + 15 }
};

const Entry* entries()  { return kEntries; }
int numEntries()        { return (int) (sizeof (kEntries) / sizeof (kEntries[0])); }

} // namespace SysExMap
} // namespace aquanova
