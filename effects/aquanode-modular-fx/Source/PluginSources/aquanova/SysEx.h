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

#pragma once
#include <JuceHeader.h>
#include "ParameterTable.h"

namespace aquanova {

//==============================================================================
/** Reading and writing Supernova II System Exclusive.

    Container format, confirmed against real bank dumps:

        F0 00 20 29 01 22 dd tt 09  <16 byte name>  <270 data bytes>  F7      (296 total)
        F0 00 20 29 01 22 dd 1E 09  <88 data bytes>                    F7      (98 total)

      00 20 29   Novation's manufacturer ID
      01 22      Supernova II model ID
      dd         device number
      tt         message type: 00 = program block 1, 1E = program block 2

    A Program is the pair. Block 1 carries the name plus the voice parameters,
    block 2 the effects parameters.

    IMPORTANT - the byte-to-parameter mapping in SysExMap.cpp is still partly
    provisional. Everything this class does not yet understand is preserved
    byte-for-byte, so loading a bank and saving it again is always lossless,
    even where we cannot yet name a given byte.
*/
class SysEx
{
public:
    static constexpr int kBlock1Size = 296;
    static constexpr int kBlock2Size = 98;
    static constexpr int kNameOffset = 9;
    static constexpr int kNameLength = 16;
    static constexpr int kBlock1DataOffset = 25;
    static constexpr int kBlock1DataLength = 270;
    static constexpr int kBlock2DataOffset = 9;
    static constexpr int kBlock2DataLength = 88;

    //==============================================================================
    struct Program
    {
        juce::String name;
        juce::uint8 voiceData  [kBlock1DataLength] { };
        juce::uint8 effectData [kBlock2DataLength] { };
        bool hasEffects = false;
    };

    /** True if this looks like Supernova II sysex at all. */
    static bool isSupernovaMessage (const juce::uint8* d, int size);

    /** Splits a .syx file into programs. Returns an empty array if nothing parsed. */
    static juce::Array<Program> readBank (const juce::MemoryBlock& fileContents);

    /** Serialises programs back into a .syx image. */
    static juce::MemoryBlock writeBank (const juce::Array<Program>& programs, int deviceNumber = 0);

    /** Applies a program to the value tree state. Unknown bytes are ignored. */
    static void applyToState (const Program& p, juce::AudioProcessorValueTreeState& state);

    /** Captures the current state into a program, starting from `base` so that
        bytes we do not understand survive the round trip. */
    static Program captureFromState (juce::AudioProcessorValueTreeState& state,
                                     const juce::String& name,
                                     const Program* base = nullptr);
};

//==============================================================================
/** The byte layout. Separated out because this is the one piece that still
    needs verifying against hardware, and it should be correctable in one place.
*/
namespace SysExMap
{
    struct Entry
    {
        int hardwareIndex;   ///< parameter index, as in ParameterTable
        int block;           ///< 1 or 2
        int byteOffset;      ///< offset within that block's data
    };

    const Entry* entries();
    int numEntries();
}

} // namespace aquanova
