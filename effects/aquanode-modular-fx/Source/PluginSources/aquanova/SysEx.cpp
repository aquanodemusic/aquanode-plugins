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

//==============================================================================
static const juce::uint8 kNovationId[3] = { 0x00, 0x20, 0x29 };
static const juce::uint8 kModelId[2]    = { 0x01, 0x22 };

bool SysEx::isSupernovaMessage (const juce::uint8* d, int size)
{
    if (size < 10 || d[0] != 0xF0)
        return false;

    return d[1] == kNovationId[0] && d[2] == kNovationId[1] && d[3] == kNovationId[2]
        && d[4] == kModelId[0]    && d[5] == kModelId[1];
}

//==============================================================================
juce::Array<SysEx::Program> SysEx::readBank (const juce::MemoryBlock& file)
{
    juce::Array<Program> programs;

    const auto* d = static_cast<const juce::uint8*> (file.getData());
    const int total = (int) file.getSize();

    int pos = 0;
    Program* pending = nullptr;

    while (pos < total)
    {
        // Find the next complete F0 ... F7 message.
        while (pos < total && d[pos] != 0xF0) ++pos;
        if (pos >= total) break;

        int end = pos;
        while (end < total && d[end] != 0xF7) ++end;
        if (end >= total) break;

        const int size = end - pos + 1;
        const auto* msg = d + pos;

        if (isSupernovaMessage (msg, size))
        {
            const int type = msg[7];

            if (type == 0x00 && size >= kBlock1Size)
            {
                Program p;

                juce::String name;
                for (int i = 0; i < kNameLength; ++i)
                {
                    const auto c = msg[kNameOffset + i];
                    if (c >= 32 && c < 127)
                        name += (juce::juce_wchar) c;
                }
                p.name = name.trim();

                std::memcpy (p.voiceData, msg + kBlock1DataOffset, (size_t) kBlock1DataLength);

                programs.add (p);
                pending = &programs.getReference (programs.size() - 1);
            }
            else if (type == 0x1E && size >= kBlock2Size && pending != nullptr)
            {
                std::memcpy (pending->effectData, msg + kBlock2DataOffset,
                             (size_t) kBlock2DataLength);
                pending->hasEffects = true;
                pending = nullptr;
            }
        }

        pos = end + 1;
    }

    return programs;
}

//==============================================================================
juce::MemoryBlock SysEx::writeBank (const juce::Array<Program>& programs, int deviceNumber)
{
    juce::MemoryOutputStream out;
    const auto dev = (juce::uint8) juce::jlimit (0, 127, deviceNumber);

    for (const auto& p : programs)
    {
        // ---- block 1
        out.writeByte ((char) 0xF0);
        out.write (kNovationId, 3);
        out.write (kModelId, 2);
        out.writeByte ((char) dev);
        out.writeByte ((char) 0x00);
        out.writeByte ((char) 0x09);

        auto padded = p.name.paddedRight (' ', kNameLength).substring (0, kNameLength);
        for (int i = 0; i < kNameLength; ++i)
            out.writeByte ((char) (juce::uint8) juce::jlimit (32, 126, (int) padded[i]));

        for (int i = 0; i < kBlock1DataLength; ++i)
            out.writeByte ((char) (juce::uint8) (p.voiceData[i] & 0x7F));

        out.writeByte ((char) 0xF7);

        // ---- block 2
        out.writeByte ((char) 0xF0);
        out.write (kNovationId, 3);
        out.write (kModelId, 2);
        out.writeByte ((char) dev);
        out.writeByte ((char) 0x1E);
        out.writeByte ((char) 0x09);

        for (int i = 0; i < kBlock2DataLength; ++i)
            out.writeByte ((char) (juce::uint8) (p.effectData[i] & 0x7F));

        out.writeByte ((char) 0xF7);
    }

    return out.getMemoryBlock();
}

//==============================================================================
void SysEx::applyToState (const Program& p, juce::AudioProcessorValueTreeState& state)
{
    const auto* map = SysExMap::entries();
    const int n = SysExMap::numEntries();

    for (int i = 0; i < n; ++i)
    {
        const auto& e = map[i];

        const juce::uint8* src = e.block == 1 ? p.voiceData : p.effectData;
        const int len = e.block == 1 ? kBlock1DataLength : kBlock2DataLength;

        if (! juce::isPositiveAndBelow (e.byteOffset, len))
            continue;

        if (e.block == 2 && ! p.hasEffects)
            continue;

        const int slot = paramSlotForIndex (e.hardwareIndex);
        if (slot < 0)
            continue;

        const auto& d = kParams[slot];
        if (auto* param = state.getParameter (d.id))
        {
            const int value = juce::jlimit (d.min, d.max, (int) src[e.byteOffset]);
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
            param->endChangeGesture();
        }
    }
}

SysEx::Program SysEx::captureFromState (juce::AudioProcessorValueTreeState& state,
                                        const juce::String& name,
                                        const Program* base)
{
    Program p;
    p.name = name;
    p.hasEffects = true;

    // Start from the program we loaded, so bytes we do not yet map are kept
    // rather than zeroed.
    if (base != nullptr)
    {
        std::memcpy (p.voiceData,  base->voiceData,  sizeof (p.voiceData));
        std::memcpy (p.effectData, base->effectData, sizeof (p.effectData));
    }

    const auto* map = SysExMap::entries();
    const int n = SysExMap::numEntries();

    for (int i = 0; i < n; ++i)
    {
        const auto& e = map[i];

        juce::uint8* dst = e.block == 1 ? p.voiceData : p.effectData;
        const int len = e.block == 1 ? kBlock1DataLength : kBlock2DataLength;

        if (! juce::isPositiveAndBelow (e.byteOffset, len))
            continue;

        const int slot = paramSlotForIndex (e.hardwareIndex);
        if (slot < 0)
            continue;

        const auto& d = kParams[slot];
        if (auto* param = state.getParameter (d.id))
        {
            const int value = (int) std::round (param->convertFrom0to1 (param->getValue()));
            dst[e.byteOffset] = (juce::uint8) juce::jlimit (0, 127, value);
        }
    }

    return p;
}

} // namespace aquanova
