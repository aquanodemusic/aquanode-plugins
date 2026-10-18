#include "DX7Generator.h"
#include "../PluginProcessor.h"
#include "../Modules/DXNativeFM.h"

namespace aquanode::dx7
{

//==============================================================================
// voices and sysex
//==============================================================================
Voice Voice::fromVced (const unsigned char* v)
{
    Voice voice;
    std::copy (v, v + 155, voice.vced.begin());

    char name[11] {};
    for (int i = 0; i < 10; ++i)
    {
        const auto c = (char) (v[145 + i] & 0x7F);
        name[i] = (c < 32 || c > 126) ? ' ' : c;
    }
    voice.name = juce::String (name).trimEnd();
    return voice;
}

namespace
{
    uint8_t clamp7 (uint8_t v, uint8_t max) { v &= 0x7F; return v <= max ? v : max; }

    // one 128-byte packed voice of a bulk dump -> VCED (the DX7's bulk format)
    Voice unpackBulkVoice (const uint8_t* bulk)
    {
        std::array<uint8_t, 155> d {};
        for (int op = 0; op < 6; ++op)
        {
            const uint8_t* pp = bulk + op * 17;
            uint8_t* up = d.data() + op * 21;
            for (int i = 0; i < 11; ++i) up[i] = clamp7 (pp[i], 99);
            const uint8_t curves = pp[11] & 0x0F;
            up[11] = curves & 3;
            up[12] = (curves >> 2) & 3;
            up[13] = pp[12] & 7;                     // rate scaling
            up[20] = clamp7 ((pp[12] >> 3) & 0x0F, 14);   // detune
            up[14] = pp[13] & 3;                     // AMS
            up[15] = (pp[13] >> 2) & 7;              // key velocity sensitivity
            up[16] = clamp7 (pp[14], 99);            // output level
            up[17] = pp[15] & 1;                     // oscillator mode
            up[18] = (pp[15] >> 1) & 0x1F;           // coarse
            up[19] = clamp7 (pp[16], 99);            // fine
        }
        for (int i = 0; i < 8; ++i) d[126 + (size_t) i] = clamp7 (bulk[102 + i], 99);
        d[134] = clamp7 (bulk[110], 31);
        d[135] = bulk[111] & 7;
        d[136] = (bulk[111] >> 3) & 1;
        for (int i = 0; i < 4; ++i) d[137 + (size_t) i] = clamp7 (bulk[112 + i], 99);
        d[141] = bulk[116] & 1;
        d[142] = (uint8_t) juce::jmin (5, (bulk[116] >> 1) & 7);
        d[143] = (bulk[116] >> 4) & 7;
        d[144] = clamp7 (bulk[117], 48);
        for (int i = 0; i < 10; ++i) d[145 + (size_t) i] = bulk[118 + i] & 0x7F;
        return Voice::fromVced (d.data());
    }

    bool isInitName (const juce::String& n)
    {
        const auto t = n.trim().toLowerCase();
        return t.isEmpty() || t == "init" || t == "init voice" || t == "default" || t == "-";
    }

    void addVoice (Bank& b, Voice v)
    {
        if (! isInitName (v.name))
            b.voices.push_back (std::move (v));
    }
}

Bank parseSysex (const juce::MemoryBlock& block, const juce::String& title)
{
    Bank bank;
    bank.title = title;
    const auto* d = static_cast<const uint8_t*> (block.getData());
    const size_t n = block.getSize();

    // a bare 32-voice bank without the sysex envelope
    if (n == 4096)
    {
        for (int i = 0; i < 32; ++i)
            addVoice (bank, unpackBulkVoice (d + i * 128));
        return bank;
    }

    for (size_t i = 0; i + 6 < n; ++i)
    {
        if (d[i] != 0xF0 || d[i + 1] != 0x43)
            continue;

        const uint8_t format = d[i + 3];
        if (format == 0x09 && i + 6 + 4096 <= n)          // 32-voice bulk dump
        {
            for (int v = 0; v < 32; ++v)
                addVoice (bank, unpackBulkVoice (d + i + 6 + (size_t) v * 128));
            i += 6 + 4096;
        }
        else if (format == 0x00 && i + 6 + 155 <= n)      // single voice (VCED)
        {
            std::array<uint8_t, 155> v {};
            for (size_t k = 0; k < 155; ++k)
                v[k] = d[i + 6 + k] & 0x7F;
            addVoice (bank, Voice::fromVced (v.data()));
            i += 6 + 155;
        }
    }
    return bank;
}

//==============================================================================
// patch building
//==============================================================================
namespace
{
    // collapsed DX Operator card (header with five inputs) and the chart grid
    constexpr int kChartColW = 280, kChartRowH = 225;

    struct ChartPos { float x = 0.0f; int level = 0; };

    // Lays the algorithm out the way the DX7's panel charts it: carriers on
    // the bottom row, each modulator above what it modulates.
    std::array<ChartPos, 6> chartFor (const dxnative::AlgoDef& alg)
    {
        std::array<ChartPos, 6> pos;

        // what each operator modulates (feedback excluded, as on the chart)
        std::array<std::vector<int>, 6> targets;
        for (int dst = 0; dst < 6; ++dst)
            for (int src = 0; src < 6; ++src)
                if (alg.modMask[dst] & (1u << src))
                    targets[(size_t) src].push_back (dst);

        // level: carriers 0, a modulator one above the highest thing it feeds.
        // Operators only ever modulate lower-numbered ones, so walking from
        // OP1 upwards sees every target first.
        for (int op = 0; op < 6; ++op)
        {
            int lvl = 0;
            for (int t : targets[(size_t) op])
                lvl = juce::jmax (lvl, pos[(size_t) t].level + 1);
            pos[(size_t) op].level = targets[(size_t) op].empty() ? 0 : lvl;
        }

        // carriers left to right, OP1 first
        float col = 0.0f;
        for (int op = 0; op < 6; ++op)
            if (alg.carrierMask & (1u << op))
                pos[(size_t) op].x = col++;

        int maxLevel = 0;
        for (const auto& p : pos) maxLevel = juce::jmax (maxLevel, p.level);

        for (int lvl = 1; lvl <= maxLevel; ++lvl)
        {
            std::vector<int> row;
            for (int op = 0; op < 6; ++op)
                if (pos[(size_t) op].level == lvl)
                {
                    // above the middle of what it modulates
                    float sum = 0.0f;
                    for (int t : targets[(size_t) op]) sum += pos[(size_t) t].x;
                    pos[(size_t) op].x = sum / (float) juce::jmax<size_t> (1, targets[(size_t) op].size());
                    row.push_back (op);
                }

            // siblings over the same target spread out, one column apart
            std::sort (row.begin(), row.end(), [&] (int a, int b)
            {
                return pos[(size_t) a].x != pos[(size_t) b].x ? pos[(size_t) a].x < pos[(size_t) b].x : a < b;
            });
            for (size_t k = 1; k < row.size(); ++k)
                pos[(size_t) row[k]].x = juce::jmax (pos[(size_t) row[k]].x, pos[(size_t) row[k - 1]].x + 1.0f);
        }

        float minX = 1.0e9f;
        for (const auto& p : pos) minX = juce::jmin (minX, p.x);
        for (auto& p : pos) p.x -= minX;
        return pos;
    }
}

juce::String buildVoicePatch (AquanodeModularAudioProcessor& p, const Voice& voice, const juce::String& label)
{
    presets::PatchSketch s (p);
    const auto& v = voice.vced;
    const int algorithm = juce::jlimit (0, 31, (int) v[134]);
    const auto& alg = dxnative::kAlgorithms[algorithm];
    const int fb = juce::jlimit (0, 7, (int) v[135]);

    // ---- the six operators -------------------------------------------------
    std::array<int, 6> op {};
    for (int i = 0; i < 6; ++i)
    {
        const int id = s.add ("osc.dxop", 1);
        op[(size_t) i] = id;
        const uint8_t* b = v.data() + (5 - i) * 21;    // VCED runs OP6 .. OP1

        for (int k = 0; k < 4; ++k)
        {
            s.set (id, "r" + juce::String (k + 1), (float) b[k]);
            s.set (id, "l" + juce::String (k + 1), (float) b[4 + k]);
        }
        s.set (id, "breakPoint", (float) b[8]);
        s.set (id, "leftDepth",  (float) b[9]);
        s.set (id, "rightDepth", (float) b[10]);
        s.set (id, "leftCurve",  (float) (b[11] & 3));
        s.set (id, "rightCurve", (float) (b[12] & 3));
        s.set (id, "rateScale",  (float) (b[13] & 7));
        s.set (id, "ams",        (float) (b[14] & 3));
        s.set (id, "velSens",    (float) (b[15] & 7));
        s.set (id, "level",      (float) b[16]);
        s.set (id, "mode",       (float) (b[17] & 1));
        s.set (id, "coarse",     (float) (b[18] & 31));
        s.set (id, "fine",       (float) b[19]);
        s.set (id, "detune",     (float) juce::jlimit (0, 14, (int) b[20]));
        s.set (id, "keySync",    v[136] != 0 ? 1.0f : 0.0f);
        s.set (id, "opNum",      (float) (i + 1));

        const bool carrier = (alg.carrierMask & (1u << i)) != 0;
        s.set (id, "com", carrier ? (float) (alg.numCarriers - 1) : 0.0f);   // "/N"
        s.collapse (id);
    }

    // ---- the algorithm -----------------------------------------------------
    for (int dst = 0; dst < 6; ++dst)
        for (int src = 0; src < 6; ++src)
            if (alg.modMask[dst] & (1u << src))
                s.wire (op[(size_t) src], "audioOut", op[(size_t) dst], "fmIn");

    if (fb > 0)
    {
        s.set (op[alg.feedbackDst], "feedback", (float) fb);
        if (alg.feedbackSrc != alg.feedbackDst)   // algorithms 4 and 6
            s.wire (op[alg.feedbackSrc], "audioOut", op[alg.feedbackDst], "fbIn");
    }

    // ---- pitch EG, LFO, mod wheel ---------------------------------------------
    const int peg = s.add ("util.dxpitcheg", 0);
    for (int k = 0; k < 4; ++k)
    {
        s.set (peg, "r" + juce::String (k + 1), (float) v[126 + (size_t) k]);
        s.set (peg, "l" + juce::String (k + 1), (float) v[130 + (size_t) k]);
    }
    s.set (peg, "transpose", (float) ((int) v[144] - 24));

    const int lfo = s.add ("util.dxlfo", 0);
    s.set (lfo, "speed", (float) v[137]);
    s.set (lfo, "delay", (float) v[138]);
    s.set (lfo, "pmd",   (float) v[139]);
    s.set (lfo, "amd",   (float) v[140]);
    s.set (lfo, "keySync", v[141] != 0 ? 1.0f : 0.0f);
    s.set (lfo, "wave",  (float) juce::jlimit (0, 5, (int) v[142]));
    s.set (lfo, "pms",   (float) juce::jlimit (0, 7, (int) v[143]));

    // the DX7's default: the mod wheel opens the vibrato
    const int wheel = s.add ("io.midicontrols", 0);
    s.wire (wheel, "wheelOut", lfo, "wheelIn");

    for (int i = 0; i < 6; ++i)
    {
        s.wire (peg, "pitchOut", op[(size_t) i], "pitchIn");
        s.wire (lfo, "pitchOut", op[(size_t) i], "pitchIn");
        s.wire (lfo, "amOut",    op[(size_t) i], "amIn");
    }

    // ---- output --------------------------------------------------------------
    const int sum = s.add ("util.polysum", 2);
    const int out = s.add ("io.audioOut", 2);
    for (int i = 0; i < 6; ++i)
        if (alg.carrierMask & (1u << i))
            s.wire (op[(size_t) i], "audioOut", sum, "audioIn");
    s.wire (sum, "audioOut", out, "audioIn");
    s.set (out, "level", 0.8f);

    // ---- layout: utilities | algorithm chart | output -------------------------
    const auto chart = chartFor (alg);
    int maxLevel = 0;
    float maxX = 0.0f;
    for (const auto& c : chart) { maxLevel = juce::jmax (maxLevel, c.level); maxX = juce::jmax (maxX, c.x); }

    const int chartX = 40 + 290;
    const int baseY = 40 + maxLevel * kChartRowH;
    for (int i = 0; i < 6; ++i)
        s.place (op[(size_t) i], { chartX + juce::roundToInt (chart[(size_t) i].x * (float) kChartColW),
                                   baseY - chart[(size_t) i].level * kChartRowH });

    int y = 40;
    for (int id : { peg, lfo, wheel })
    {
        s.place (id, { 40, y });
        if (auto* inst = p.getInstance (id))
            y += presets::estimateCardHeight (*inst) + 30;
    }

    const int outX = chartX + juce::roundToInt ((maxX + 1.0f) * (float) kChartColW) + 20;
    s.place (sum, { outX, baseY });
    s.place (out, { outX, baseY + 230 });

    return label + " (alg " + juce::String (algorithm + 1) + ")";
}

juce::StringArray categories()
{
    return { "Bass", "Bell", "FX", "Keys", "Lead", "Pad", "Pluck" };
}

presets::BuildFn randomVoiceBuilder (juce::uint32 seed, int category)
{
    std::vector<const FactoryVoice*> pool;
    for (const auto& v : factoryVoices())
        if (category < 0 || v.category == category)
            pool.push_back (&v);
    if (pool.empty())
        return {};

    presets::Dice dice (seed);
    const auto& pick = *pool[(size_t) dice.index ((int) pool.size())];
    const Voice voice = pick.voice;
    const juce::String label = "FM " + categories()[pick.category] + " " + juce::String (pick.number).paddedLeft ('0', 2);
    return [voice, label] (AquanodeModularAudioProcessor& proc) { return buildVoicePatch (proc, voice, label); };
}

} // namespace aquanode::dx7
