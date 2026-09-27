/*
    StarterBank.h  -  32 original patches, bundled so the plugin is playable
    the moment it loads, with no ROM files at all.

    These are not the Yamaha factory voices and are not derived from them. They
    are written here from scratch, in the same 155-byte VCED format, to cover
    the ground a player expects from a six-operator FM synth: keys, basses,
    brass, strings, organs, tuned percussion, leads and a few effects.

    To keep the table readable each operator is described by five numbers - an
    envelope "role", a frequency ratio as coarse/fine, a detune, and an output
    level - rather than by twenty-one raw bytes. The roles are the handful of
    envelope shapes that actually come up in FM programming, and giving them
    names makes the patch table something you can read and edit rather than a
    wall of digits.

    Operator numbering here is the musician's one: op[0] is OP1. The VCED's
    reversed block order is handled in buildVced().

    GPLv3.
*/
#pragma once

#include <cstdint>
#include <cstring>

namespace vdx7starter {

// ============================================================================
//  Envelope roles: rates R1-R4 then levels L1-L4.
// ============================================================================
struct EnvRole { uint8_t r[4]; uint8_t l[4]; };

enum Role : uint8_t
{
    ORGAN = 0,  // instant on, instant off, full sustain
    PAD,        // slow swell, long release
    STRINGS,    // moderate swell, gentle release
    BRASSY,     // firm attack with a slight overshoot settle
    PLUCK,      // immediate, decays to nothing
    PERC,       // immediate, decays fast
    CLICK,      // a transient only
    BELL,       // immediate, very long tail
    BASSY,      // immediate, decays to a held body
    FLUTEY,     // soft attack, full sustain
    SWEEP,      // very slow swell
    DECAYING    // immediate, settles to a mid sustain
};

static const EnvRole kRoles[] =
{
    /* ORGAN    */ { { 99, 99, 99, 99 }, { 99, 99, 99,  0 } },
    /* PAD      */ { { 42, 38, 32, 42 }, { 99, 92, 88,  0 } },
    /* STRINGS  */ { { 58, 34, 40, 48 }, { 99, 90, 84,  0 } },
    /* BRASSY   */ { { 72, 46, 52, 56 }, { 99, 90, 87,  0 } },
    /* PLUCK    */ { { 99, 58, 42, 62 }, { 99, 62,  0,  0 } },
    /* PERC     */ { { 99, 72, 54, 70 }, { 99, 42,  0,  0 } },
    /* CLICK    */ { { 99, 90, 82, 82 }, { 99, 16,  0,  0 } },
    /* BELL     */ { { 99, 34, 28, 40 }, { 99, 72, 40,  0 } },
    /* BASSY    */ { { 99, 56, 46, 68 }, { 99, 78, 62,  0 } },
    /* FLUTEY   */ { { 66, 52, 46, 54 }, { 99, 92, 88,  0 } },
    /* SWEEP    */ { { 40, 44, 38, 46 }, { 99, 84, 76,  0 } },
    /* DECAYING */ { { 99, 52, 44, 60 }, { 99, 82, 54,  0 } },
};

// ============================================================================
//  Patch descriptions
// ============================================================================
struct OpDesc
{
    uint8_t role;
    uint8_t coarse;   // 0 = half pitch, else the ratio's whole part
    uint8_t fine;     // hundredths on top of coarse
    uint8_t detune;   // 0..14, 7 = centre
    uint8_t level;    // 0..99
};

struct PatchDesc
{
    const char* name;         // exactly 10 characters, space padded
    uint8_t alg;              // 1..32 as printed on the panel
    uint8_t fb;               // 0..7
    OpDesc  op[6];            // op[0] is OP1
    uint8_t lfoWave;          // 0 tri, 1 saw down, 2 saw up, 3 square, 4 sine, 5 S/H
    uint8_t lfoSpeed, lfoDelay, pmd, amd, pms;
};

static const int kNumStarterPatches = 32;

// clang-format off
static const PatchDesc kPatches[kNumStarterPatches] =
{
//   name          alg fb    OP1                      OP2                      OP3                      OP4                      OP5                      OP6                     wave spd dly pmd amd pms
{ "SUITCASE  ",  5, 6, {{PLUCK,   1, 0,7,99},{PERC,   14, 0,7,74},{DECAYING,1, 0,8,82},{PERC,    1, 0,6,66},{ORGAN,   1, 0,7,38},{CLICK,  11, 0,7,58}},  4, 34, 12,  0,  0, 2 },
{ "GLASS KEYS",  5, 4, {{PLUCK,   1, 0,7,99},{BELL,    7, 0,9,68},{PLUCK,   2, 0,5,74},{BELL,   11, 0,7,60},{ORGAN,   1, 0,7,44},{PERC,   17, 0,7,52}},  4, 30, 20,  0,  0, 2 },
{ "TUBE BELL ",  5, 7, {{BELL,    1, 0,7,99},{BELL,    3,50,9,78},{BELL,    1, 0,5,80},{BELL,    7, 0,7,64},{BELL,    1, 0,7,52},{PERC,   14, 0,7,56}},  4, 26,  0,  0,  0, 1 },
{ "MARIMBA   ",  5, 0, {{PERC,    1, 0,7,99},{PERC,    4, 0,7,72},{PERC,    1, 0,9,66},{CLICK,   9, 0,7,58},{CLICK,   1, 0,7,40},{CLICK,  20, 0,7,44}},  4, 20,  0,  0,  0, 0 },
{ "WOOD BASS ", 17, 6, {{BASSY,   1, 0,7,99},{DECAYING,1, 0,8,80},{PERC,    3, 0,7,62},{PLUCK,   1, 0,6,58},{CLICK,   6, 0,7,50},{PERC,    1, 0,7,54}},  4, 22,  0,  0,  0, 1 },
{ "SYNTH BASS", 16, 7, {{BASSY,   1, 0,7,99},{BASSY,   1, 0,9,76},{DECAYING,2, 0,7,70},{PERC,    3, 0,7,62},{BASSY,   1, 0,5,58},{DECAYING,1, 0,7,72}},  4, 24,  0,  0,  0, 1 },
{ "BRIGHT BRS", 22, 7, {{BRASSY,  1, 0,7,99},{BRASSY,  1, 0,9,72},{BRASSY,  1, 0,5,92},{BRASSY,  2, 0,8,88},{BRASSY,  3, 0,6,80},{BRASSY,  1, 0,7,84}},  4, 36, 28, 12,  0, 3 },
{ "SOFT HORN ", 18, 5, {{BRASSY,  1, 0,7,99},{FLUTEY,  1, 0,9,66},{STRINGS, 2, 0,5,62},{BRASSY,  1, 0,7,70},{STRINGS, 1, 0,8,58},{FLUTEY,  1, 0,7,54}},  4, 30, 32, 14,  0, 3 },
{ "STRING PAD",  2, 6, {{STRINGS, 1, 0,7,99},{STRINGS, 1, 0,9,74},{STRINGS, 1, 0,5,90},{STRINGS, 2, 0,8,66},{PAD,     1, 0,6,60},{PAD,     3, 0,7,58}},  4, 28, 24, 16,  0, 3 },
{ "GLASS PAD ", 32, 5, {{PAD,     1, 0,7,99},{PAD,     2, 0,9,92},{PAD,     3, 0,5,86},{PAD,     4, 0,8,80},{PAD,     6, 0,6,72},{PAD,     8, 0,7,66}},  4, 18, 30, 18,  0, 2 },
{ "PIPE ORGAN", 32, 0, {{ORGAN,   1, 0,7,99},{ORGAN,   2, 0,7,94},{ORGAN,   3, 0,7,88},{ORGAN,   4, 0,7,82},{ORGAN,   6, 0,7,74},{ORGAN,   8, 0,7,68}},  4, 30, 40,  6,  0, 2 },
{ "ROCK ORGAN", 31, 6, {{ORGAN,   1, 0,7,99},{ORGAN,   2, 0,8,94},{ORGAN,   3, 0,6,86},{ORGAN,   4, 0,7,78},{ORGAN,   1, 0,7,72},{ORGAN,   7, 0,7,76}},  4, 44, 10, 10,  0, 3 },
{ "CLAVI     ", 16, 7, {{PLUCK,   1, 0,7,99},{PLUCK,   3, 0,9,78},{PERC,    7, 0,7,66},{CLICK,  11, 0,7,58},{PLUCK,   1, 0,5,54},{PERC,    2, 0,7,70}},  4, 26,  0,  0,  0, 1 },
{ "HARPSICORD",  3, 4, {{PLUCK,   1, 0,7,99},{PLUCK,   3, 0,9,76},{CLICK,   9, 0,7,60},{PLUCK,   1, 0,5,88},{PLUCK,   4, 0,8,68},{CLICK,  13, 0,7,54}},  4, 24,  0,  0,  0, 1 },
{ "NYLON GTR ",  5, 3, {{PLUCK,   1, 0,7,99},{PLUCK,   2, 0,9,70},{PLUCK,   1, 0,5,78},{PERC,    5, 0,7,58},{CLICK,   1, 0,7,46},{CLICK,   9, 0,7,50}},  4, 22,  0,  0,  0, 1 },
{ "STEEL GTR ",  7, 5, {{PLUCK,   1, 0,7,99},{PLUCK,   4, 0,9,74},{PLUCK,   1, 0,5,84},{PERC,    6, 0,7,66},{PERC,   10, 0,8,58},{PERC,    3, 0,7,62}},  4, 24,  0,  0,  0, 1 },
{ "WOOD FLUTE",  5, 0, {{FLUTEY,  1, 0,7,99},{FLUTEY,  1, 0,9,52},{FLUTEY,  2, 0,5,58},{FLUTEY,  4, 0,7,40},{FLUTEY,  1, 0,7,44},{CLICK,  12, 0,7,42}},  4, 32, 34, 20,  0, 4 },
{ "PAN PIPE  ",  6, 2, {{FLUTEY,  1, 0,7,99},{FLUTEY,  3, 0,9,56},{FLUTEY,  1, 0,5,70},{FLUTEY,  6, 0,7,48},{FLUTEY,  1, 0,8,62},{PERC,   19, 0,7,54}},  5, 46, 20, 16,  0, 3 },
{ "VIBRAPHONE", 23, 3, {{BELL,    1, 0,7,99},{BELL,    1, 0,9,95},{BELL,    4, 0,7,68},{BELL,    2, 0,5,95},{BELL,    3, 0,8,90},{BELL,    7, 0,7,62}},  4, 40,  0,  0, 42, 2 },
{ "CELESTA   ",  5, 2, {{BELL,    1, 0,7,99},{BELL,    9, 0,9,62},{BELL,    2, 0,5,76},{BELL,   14, 0,7,54},{PERC,    1, 0,7,48},{CLICK,  21, 0,7,46}},  4, 28,  0,  0,  0, 1 },
{ "SQUARE LD ", 16, 7, {{ORGAN,   1, 0,7,99},{ORGAN,   2, 0,9,72},{ORGAN,   3, 0,7,64},{ORGAN,   5, 0,7,56},{ORGAN,   1, 0,5,52},{ORGAN,   1, 0,7,78}},  4, 38, 36, 22,  0, 4 },
{ "SAW LEAD  ", 32, 7, {{ORGAN,   1, 0,7,99},{ORGAN,   2, 0,7,92},{ORGAN,   3, 0,7,86},{ORGAN,   4, 0,7,80},{ORGAN,   5, 0,7,74},{ORGAN,   1, 0,7,96}},  4, 40, 34, 24,  0, 4 },
{ "SYNC LEAD ",  1, 7, {{BRASSY,  1, 0,7,99},{BRASSY,  2, 0,9,80},{BRASSY,  1, 0,5,86},{BRASSY,  3, 0,7,74},{BRASSY,  5, 0,8,68},{BRASSY,  1, 0,7,82}},  4, 42, 30, 20,  0, 4 },
{ "BELL PAD  ", 21, 4, {{PAD,     1, 0,7,99},{PAD,     2, 0,9,95},{BELL,    5, 0,7,62},{BELL,    3, 0,5,92},{PAD,     1, 0,8,88},{BELL,    9, 0,7,58}},  4, 20, 26, 14,  0, 2 },
{ "GLOCKEN   ",  5, 1, {{BELL,    1, 0,7,99},{BELL,   11, 0,9,58},{BELL,    2, 0,5,72},{BELL,   17, 0,7,50},{PERC,    1, 0,7,44},{CLICK,  23, 0,7,48}},  4, 26,  0,  0,  0, 1 },
{ "KOTO      ",  5, 5, {{PLUCK,   1, 0,7,99},{PLUCK,   3, 0,9,72},{PLUCK,   1, 0,5,70},{PERC,    7, 0,7,60},{CLICK,   1, 0,7,42},{PERC,   11, 0,7,52}},  4, 30, 18, 12,  0, 2 },
{ "MUTED BASS", 17, 4, {{BASSY,   1, 0,7,99},{PERC,    1, 0,9,68},{PERC,    2, 0,7,56},{CLICK,   4, 0,7,50},{PERC,    1, 0,5,48},{PLUCK,   1, 0,7,62}},  4, 20,  0,  0,  0, 0 },
{ "FAT BRASS ", 22, 6, {{BRASSY,  1, 0,7,99},{BRASSY,  1, 0,10,74},{BRASSY, 1, 0,4,94},{BRASSY,  2, 0,9,84},{BRASSY,  1, 0,5,88},{BRASSY,  1, 0,7,76}},  4, 34, 30, 14,  0, 3 },
{ "AIR CHOIR ", 19, 3, {{SWEEP,   1, 0,7,99},{SWEEP,   3, 0,9,62},{SWEEP,   1, 0,5,70},{SWEEP,   1, 0,8,99},{SWEEP,   2, 0,6,95},{SWEEP,   4, 0,7,58}},  4, 22, 38, 18,  0, 3 },
{ "METAL PERC", 24, 7, {{PERC,    1, 0,7,99},{PERC,    3,50,9,95},{PERC,    5,20,5,90},{PERC,    7,70,8,86},{PERC,   11,30,6,80},{PERC,    2, 0,7,88}},  5, 50,  0,  0,  0, 2 },
{ "TIMPANI   ", 28, 6, {{PERC,    1, 0,7,99},{DECAYING,1, 0,9,72},{PERC,    1, 0,5,84},{PERC,    2, 0,7,66},{CLICK,   3, 0,8,70},{PERC,    1, 0,7,60}},  4, 18,  0,  0,  0, 1 },
{ "INIT VOICE",  1, 0, {{ORGAN,   1, 0,7,99},{ORGAN,   1, 0,7, 0},{ORGAN,   1, 0,7, 0},{ORGAN,   1, 0,7, 0},{ORGAN,   1, 0,7, 0},{ORGAN,   1, 0,7, 0}},  4, 35,  0,  0,  0, 3 },
};
// clang-format on

// ============================================================================
//  Expansion to VCED
// ============================================================================
// Writes patch `index` as a 155-byte VCED. Anything not described in the table
// above gets a neutral default: no keyboard scaling, mild rate scaling and
// velocity sensitivity on every operator, flat pitch envelope, no transpose.
inline void buildVced (int index, uint8_t v[155])
{
    if (index < 0 || index >= kNumStarterPatches) index = 0;
    const PatchDesc& p = kPatches[index];

    std::memset (v, 0, 155);

    for (int opIndex = 0; opIndex < 6; ++opIndex)
    {
        const OpDesc&  o = p.op[opIndex];
        const EnvRole& e = kRoles[o.role];

        uint8_t* b = v + (5 - opIndex) * 21;   // OP1 lives in the last block

        for (int i = 0; i < 4; ++i) b[0 + i] = e.r[i];
        for (int i = 0; i < 4; ++i) b[4 + i] = e.l[i];

        b[8]  = 39;      // break point at middle C
        b[9]  = 0;       // left depth
        b[10] = 0;       // right depth
        b[11] = 0;       // left curve  (-LIN)
        b[12] = 0;       // right curve (-LIN)
        b[13] = 2;       // a little rate scaling, so the top does not ring on
        b[14] = 0;       // amplitude mod sensitivity
        b[15] = 2;       // some velocity sensitivity on every operator
        b[16] = o.level;
        b[17] = 0;       // ratio mode
        b[18] = o.coarse;
        b[19] = o.fine;
        b[20] = o.detune;
    }

    // Modulators respond to velocity more strongly than carriers, which is what
    // makes a patch open up when you dig in rather than just get louder.
    for (int opIndex = 1; opIndex < 6; ++opIndex)
        v[(5 - opIndex) * 21 + 15] = 4;

    for (int i = 0; i < 4; ++i) v[126 + i] = 99;   // pitch EG rates: instant
    for (int i = 0; i < 4; ++i) v[130 + i] = 50;   // pitch EG levels: flat

    v[134] = (uint8_t) ((p.alg - 1) & 31);
    v[135] = p.fb;
    v[136] = 1;              // oscillator key sync on
    v[137] = p.lfoSpeed;
    v[138] = p.lfoDelay;
    v[139] = p.pmd;
    v[140] = p.amd;
    v[141] = 0;              // LFO key sync off, so it free-runs across a chord
    v[142] = p.lfoWave;
    v[143] = p.pms;
    v[144] = 24;             // no transposition

    for (int i = 0; i < 10; ++i)
        v[145 + i] = (uint8_t) p.name[i];
}

inline const char* patchName (int index)
{
    if (index < 0 || index >= kNumStarterPatches) index = 0;
    return kPatches[index].name;
}

} // namespace vdx7starter
