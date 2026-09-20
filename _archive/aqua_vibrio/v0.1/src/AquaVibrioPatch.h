#pragma once
//==============================================================================
//  AquaVibrioPatch.h
//
//  Reading and writing original Access Virus single dumps - A, B, C, Classic,
//  Rack, Snow, TI and TI2 alike. The format is the same message in two
//  lengths, so one parser covers every model:
//
//      F0 00 20 33 01 <dev> 10 <bank> <prog>
//      [256 bytes: pages A and B]                    <- every model
//      <checksum>
//      [256 bytes: pages C and D]                    <- TI family only
//      <checksum>
//      F7
//
//  A preset is a flat byte array; page 112 is bytes 0..127, page 113 is
//  128..255, and so on. Since every entry in the parameter table carries its
//  page and index, applying a patch is a lookup, not an interpretation.
//
//  A patch loaded from an older OS simply leaves the TI-only bytes at their
//  defaults, which is what the hardware does too.
//==============================================================================

#include <JuceHeader.h>
#include <cstring>
#include "AquaVibrioParameters.h"

namespace aquavibrio
{

struct Patch
{
    static constexpr int kPresetSizeABC = 256;   // A, B, C, Classic, Rack
    static constexpr int kPresetSizeTI  = 512;   // TI, TI2, Snow
    static constexpr int kHeaderSize    = 9;     // F0 .. program number
    static constexpr int kNameOffset    = 240;   // page B, index 112
    static constexpr int kNameLength    = 10;

    std::array<juce::uint8, kPresetSizeTI> bytes {};
    bool isTI { false };

    juce::String getName() const
    {
        juce::String n;
        for (int i = 0; i < kNameLength; ++i)
        {
            const auto c = bytes[(size_t) (kNameOffset + i)];
            n += juce::String::charToString ((juce::juce_wchar) (c >= 32 && c <= 127 ? c : ' '));
        }
        return n.trimEnd();
    }

    void setName (const juce::String& n)
    {
        const auto padded = (n + "          ").substring (0, kNameLength);
        for (int i = 0; i < kNameLength; ++i)
            bytes[(size_t) (kNameOffset + i)] = (juce::uint8) juce::jlimit (32, 127, (int) padded[i]);
    }

    // The hardware refuses a preset whose first name character is not
    // printable, and so do we - it is the cheapest way to spot a truncated or
    // misaligned dump before it turns into a patch full of noise.
    bool looksValid() const
    {
        return bytes[kNameOffset] >= 32 && bytes[kNameOffset] <= 127;
    }

    juce::uint8 byteAt (int page, int index) const
    {
        const int offset = (page - 112) * 128 + index;
        return offset >= 0 && offset < kPresetSizeTI ? bytes[(size_t) offset] : 0;
    }

    void setByteAt (int page, int index, juce::uint8 value)
    {
        const int offset = (page - 112) * 128 + index;
        if (offset >= 0 && offset < kPresetSizeTI)
            bytes[(size_t) offset] = value;
    }
};

//==============================================================================
// Sysex
//==============================================================================
inline juce::uint8 checksum (const juce::uint8* data, int start, int end)
{
    int sum = 0;
    for (int i = start; i < end; ++i)
        sum += data[i];
    return (juce::uint8) (sum & 0x7f);
}

inline bool isVirusSingleDump (const juce::uint8* d, int size)
{
    return size > Patch::kHeaderSize + 2
        && d[0] == 0xf0 && d[1] == 0x00 && d[2] == 0x20 && d[3] == 0x33
        && d[4] == 0x01 && d[6] == 0x10;   // 0x10 = DUMP_SINGLE
}

// Pulls the preset out of one single-dump message. Checksums are verified but
// a mismatch is not fatal: plenty of banks in circulation were assembled by
// editors that got the sum wrong, and the payload is usually fine.
inline bool parseSingleDump (const juce::uint8* d, int size, Patch& out, bool* checksumOk = nullptr)
{
    if (! isVirusSingleDump (d, size))
        return false;

    const int payload = size - Patch::kHeaderSize - 1;   // minus trailing F7

    // 256 data + 1 checksum, or 512 data + 2 checksums (one after each half)
    const bool ti = payload >= Patch::kPresetSizeTI + 2;
    if (! ti && payload < Patch::kPresetSizeABC + 1)
        return false;

    out = {};
    out.isTI = ti;

    const int firstHalf = Patch::kHeaderSize;
    std::memcpy (out.bytes.data(), d + firstHalf, Patch::kPresetSizeABC);

    bool ok = d[firstHalf + Patch::kPresetSizeABC]
                == checksum (d, 5, firstHalf + Patch::kPresetSizeABC);

    if (ti)
    {
        const int secondHalf = firstHalf + Patch::kPresetSizeABC + 1;
        std::memcpy (out.bytes.data() + Patch::kPresetSizeABC, d + secondHalf, Patch::kPresetSizeABC);
        ok = ok && d[secondHalf + Patch::kPresetSizeABC]
                     == checksum (d, 5, secondHalf + Patch::kPresetSizeABC);
    }

    if (checksumOk != nullptr)
        *checksumOk = ok;

    return out.looksValid();
}

inline juce::MemoryBlock createSingleDump (const Patch& p, int bank = 0, int program = 0, int deviceId = 0x10)
{
    std::vector<juce::uint8> s;
    s.reserve (Patch::kPresetSizeTI + 16);

    for (auto b : { 0xf0, 0x00, 0x20, 0x33, 0x01 })
        s.push_back ((juce::uint8) b);
    s.push_back ((juce::uint8) deviceId);
    s.push_back (0x10);
    s.push_back ((juce::uint8) bank);
    s.push_back ((juce::uint8) program);

    for (int i = 0; i < Patch::kPresetSizeABC; ++i)
        s.push_back (p.bytes[(size_t) i]);
    s.push_back (checksum (s.data(), 5, (int) s.size()));

    if (p.isTI)
    {
        for (int i = Patch::kPresetSizeABC; i < Patch::kPresetSizeTI; ++i)
            s.push_back (p.bytes[(size_t) i]);
        s.push_back (checksum (s.data(), 5, (int) s.size()));
    }

    s.push_back (0xf7);
    return { s.data(), s.size() };
}

//==============================================================================
// Standard MIDI File support
//==============================================================================
// A .syx file is just the raw F0 .. F7 messages back to back, so scanning
// for those two bytes is enough. A .mid file is not: it is a Standard MIDI
// File, and inside it a sysex dump is stored with the leading F0 stripped
// off (it is the event type itself) and a variable-length quantity in its
// place giving the length of what follows; a dump that doesn't fit in one
// event can also be split across an F0 event and one or more F7
// "continuation" events. Scanning raw bytes for F0 .. F7 therefore finds
// the right start but the wrong header (it includes the length bytes),
// which is why every dump was being rejected.

inline juce::uint32 readVarLen (const juce::uint8*& p, const juce::uint8* end)
{
    juce::uint32 value = 0;
    while (p < end)
    {
        const juce::uint8 b = *p++;
        value = (value << 7) | (juce::uint32) (b & 0x7f);
        if ((b & 0x80) == 0)
            break;
    }
    return value;
}

inline int dataBytesForStatus (juce::uint8 status)
{
    const auto hi = status & 0xf0;
    return (hi == 0xc0 || hi == 0xd0) ? 1 : 2;   // program change / channel pressure take 1
}

// Walks every track chunk of a Standard MIDI File and reassembles each
// sysex dump into the same F0 .. F7 layout a raw .syx file would have
// (restoring the F0 that the file format omits), ready for parseSingleDump.
inline std::vector<std::vector<juce::uint8>> extractSysexFromMidiFile (const juce::uint8* d, int size)
{
    std::vector<std::vector<juce::uint8>> messages;
    if (size < 14 || std::memcmp (d, "MThd", 4) != 0)
        return messages;

    const juce::uint8* end = d + size;
    const juce::uint8* p = d + 8 + (int) ((d[4] << 24) | (d[5] << 16) | (d[6] << 8) | d[7]); // past MThd + header data

    while (p + 8 <= end && std::memcmp (p, "MTrk", 4) == 0)
    {
        p += 4;
        const size_t trackLen = ((size_t) p[0] << 24) | ((size_t) p[1] << 16)
                               | ((size_t) p[2] << 8)  | (size_t) p[3];
        p += 4;
        const juce::uint8* trackEnd = ((size_t) (end - p) >= trackLen) ? p + trackLen : end;

        juce::uint8 runningStatus = 0;
        std::vector<juce::uint8> pending;

        while (p < trackEnd)
        {
            readVarLen (p, trackEnd);   // delta time, not needed here
            if (p >= trackEnd)
                break;

            juce::uint8 status = *p;
            if (status & 0x80)
                ++p;
            else
                status = runningStatus;   // running status: this byte is data, not a new status

            if (status == 0xf0 || status == 0xf7)
            {
                runningStatus = 0;
                const auto len = juce::jmin (readVarLen (p, trackEnd), (juce::uint32) (trackEnd - p));

                if (status == 0xf0)
                {
                    pending.clear();
                    pending.push_back (0xf0);
                }

                pending.insert (pending.end(), p, p + len);
                p += len;

                if (! pending.empty() && pending.back() == 0xf7)
                {
                    messages.push_back (pending);
                    pending.clear();
                }
                // otherwise the dump continues in a following F7 event
            }
            else if (status == 0xff)
            {
                runningStatus = 0;
                if (p < trackEnd)
                    ++p;   // meta type
                const auto len = juce::jmin (readVarLen (p, trackEnd), (juce::uint32) (trackEnd - p));
                p += len;
            }
            else if (status >= 0x80 && status < 0xf0)
            {
                runningStatus = status;
                p += juce::jmin ((size_t) dataBytesForStatus (status), (size_t) (trackEnd - p));
            }
            else
            {
                break;   // unrecognised/corrupt - stop reading this track
            }
        }

        p = trackEnd;
    }

    return messages;
}

// Walks a .syx / .mid / .bin file and returns every single dump inside it.
// Bank files are simply 128 of these messages back to back, so no separate
// bank parser is needed.
inline std::vector<Patch> parseBankFile (const juce::MemoryBlock& data)
{
    std::vector<Patch> patches;
    const auto* d = static_cast<const juce::uint8*> (data.getData());
    const int size = (int) data.getSize();

    if (size >= 4 && std::memcmp (d, "MThd", 4) == 0)
    {
        for (const auto& message : extractSysexFromMidiFile (d, size))
        {
            Patch p;
            if (parseSingleDump (message.data(), (int) message.size(), p))
                patches.push_back (p);
        }
        return patches;
    }

    for (int i = 0; i < size; ++i)
    {
        if (d[i] != 0xf0)
            continue;

        int end = i + 1;
        while (end < size && d[end] != 0xf7)
            ++end;
        if (end >= size)
            break;

        Patch p;
        if (parseSingleDump (d + i, end - i + 1, p))
            patches.push_back (p);

        i = end;
    }

    return patches;
}

//==============================================================================
// Patch <-> APVTS
//==============================================================================
inline void applyPatch (const Patch& patch, juce::AudioProcessorValueTreeState& apvts)
{
    for (const auto& info : allParameters())
    {
        auto* param = apvts.getParameter (info.id);
        if (param == nullptr)
            continue;

        const int raw = juce::jlimit (info.minValue, info.maxValue,
                                      (int) patch.byteAt (info.page, info.index));

        // Enums are stored as a choice index counted from the parameter's own
        // minimum; everything else keeps the hardware value as-is.
        const int span = info.scale == ValueScale::Enum
                           ? juce::jmax (1, info.maxValue - info.minValue)
                           : juce::jmax (1, info.maxValue - info.minValue);
        const float norm = (float) (raw - info.minValue) / (float) span;

        param->beginChangeGesture();
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        param->endChangeGesture();
    }
}

inline Patch capturePatch (const juce::AudioProcessorValueTreeState& apvts, bool asTI = true)
{
    Patch p;
    p.isTI = asTI;

    // Start from the hardware defaults so bytes we never expose still read
    // sensibly on a real Virus.
    for (const auto& info : allParameters())
        p.setByteAt (info.page, info.index, (juce::uint8) info.defaultValue);

    for (const auto& info : allParameters())
    {
        if (auto* param = apvts.getParameter (info.id))
        {
            const int span = juce::jmax (1, info.maxValue - info.minValue);
            const int raw = info.minValue + juce::roundToInt (param->getValue() * (float) span);
            p.setByteAt (info.page, info.index, (juce::uint8) juce::jlimit (0, 127, raw));
        }
    }

    return p;
}

//==============================================================================
// User presets (JSON)
//==============================================================================
//  Our own preset format, not a hardware one. Every parameter's normalised
//  0..1 value is written straight from the APVTS, by parameter ID, so a
//  round trip through this never loses precision or aliases two parameters
//  that happen to share a hardware byte address the way the Patch <-> byte
//  mapping above can. This is what the top bar's Load / Save buttons use.
//==============================================================================
inline juce::String presetToJson (const juce::AudioProcessorValueTreeState& apvts, const juce::String& name)
{
    auto params = std::make_unique<juce::DynamicObject>();
    for (const auto& info : allParameters())
        if (auto* param = apvts.getParameter (info.id))
            params->setProperty (juce::String (info.id), param->getValue());

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "AquaVibrioPreset");
    root->setProperty ("version", 1);
    root->setProperty ("name", name);
    root->setProperty ("parameters", juce::var (params.release()));

    return juce::JSON::toString (juce::var (root.release()));
}

// Reads a preset name out of preset JSON without touching the APVTS - used
// when scanning a folder of presets to fill the patch list.
inline juce::String presetNameFromJson (const juce::String& json, const juce::File& fallback)
{
    const auto parsed = juce::JSON::parse (json);
    if (parsed.isObject())
    {
        const auto name = parsed.getProperty ("name", {}).toString();
        if (name.isNotEmpty())
            return name;
    }
    return fallback.getFileNameWithoutExtension();
}

// Applies every parameter named in a preset's JSON to the APVTS. Unknown
// keys (an older or newer preset) are simply skipped rather than failing the
// whole load, and parameters the preset doesn't mention keep their current
// value rather than being reset - a JSON preset only has to say what makes
// it that sound, the same way the built-in factory presets do.
inline bool applyJsonPreset (const juce::String& json, juce::AudioProcessorValueTreeState& apvts)
{
    const auto parsed = juce::JSON::parse (json);
    if (! parsed.isObject())
        return false;

    if (parsed.getProperty ("format", {}).toString() != "AquaVibrioPreset")
        return false;

    auto* params = parsed.getProperty ("parameters", {}).getDynamicObject();
    if (params == nullptr)
        return false;

    for (const auto& prop : params->getProperties())
    {
        if (auto* param = apvts.getParameter (prop.name.toString()))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, (float) prop.value));
            param->endChangeGesture();
        }
    }

    return true;
}

} // namespace aquavibrio
