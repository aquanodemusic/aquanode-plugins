/*
    StarterBank.h  -  32 original patches, bundled so the plugin is playable
    the moment it loads, with no ROM files at all.

    These are not the Yamaha factory voices and are not derived from them. They
    are written here from scratch, in the same 155-byte VCED format, to cover
    the ground a player expects from a six-operator FM synth: keys, basses,
    brass, strings, organs, tuned percussion, leads, reeds and a few effects.

    Every operator is written out in full - its own eight-knob envelope, its
    frequency, its level and sensitivities, and its keyboard scaling - one line
    per operator, in the same order the editor panel shows them. An earlier
    version of this table shared a dozen stock envelope shapes between all the
    operators; that kept the table short but made every patch sound alike, and
    it hid the one thing that matters most when programming a DX7:

        levels are logarithmic.

    Each step of an output level or an EG level is 0.75 dB. Level 99 is full
    scale, 90 is about -7 dB, 80 is -14 dB, 70 is -22 dB and 60 is already
    -29 dB. So a modulator at "62" barely moves its carrier (the tone stays a
    near-sine), and an envelope that decays from L1 99 to L2 62 has lost 28 dB
    by the end of its second segment - which is heard as a short blip, not a
    decay. Useful modulator levels live roughly between 60 (a hint of colour)
    and 85 (bright, brassy); carriers want to sit in the 80s and 90s; and a
    plucked or struck sound is built from a quick drop to an L2 in the 80s
    followed by a slow R3 in the 20s-30s towards L3 = 0.

    Rates are exponential too: a rate step of about 6 doubles the speed. As a
    rough guide for a full decay at middle C, R 60 takes a fraction of a
    second, R 40 a few seconds and R 25 well over ten.

    Operator numbering here is the musician's one: op[0] is OP1. The VCED's
    reversed block order is handled in buildVced().

    GPLv3.
*/
#pragma once

#include <cstdint>
#include <cstring>

namespace vdx7starter {

// ============================================================================
//  Patch descriptions
// ============================================================================
struct OpDesc
{
    uint8_t r1, r2, r3, r4;          // EG rates   (attack, decay 1, decay 2, release)
    uint8_t l1, l2, l3, l4;          // EG levels  (L3 is the sustain level)
    uint8_t coarse, fine, detune;    // ratio = (coarse ? coarse : 0.5) * (1 + fine/100); detune 7 = centre
    uint8_t level;                   // output level 0..99
    uint8_t kvs, rs, ams;            // velocity sens 0..7, rate scaling 0..7, amp-mod sens 0..3
    uint8_t bp, ld, rd, lc, rc;      // keyboard level scaling: break point, depths, curves
};                                   //   (curves: 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN)

struct PatchDesc
{
    const char* name;                // exactly 10 characters, space padded
    uint8_t alg;                     // 1..32 as printed on the panel
    uint8_t fb;                      // 0..7
    uint8_t lfoSpeed, lfoDelay, pmd, amd, lfoSync, lfoWave, pms;
                                     // wave: 0 tri, 1 saw down, 2 saw up, 3 square, 4 sine, 5 S/H
    uint8_t pr[4], pl[4];            // pitch EG rates / levels (50 = no pitch change)
    uint8_t transpose;               // 24 = none, 36 = up an octave
    OpDesc  op[6];                   // op[0] is OP1
};

static const int kNumStarterPatches = 32;

// Pitch-EG shortcuts. The pitch EG starts every note from L4 and returns to L4
// on release, so these keep L4 at 50 (no pitch change) and put the movement
// in the first two segments - a scoop or a drop that never makes the tail of
// the note slide around when the key is let go.
#define FLAT_PEG      { 99, 99, 99, 99 }, { 50, 50, 50, 50 }
#define SCOOP_PEG(r)  { 99,  r, 99, 99 }, { 47, 50, 50, 50 }   // starts ~1 semitone flat
#define DROP_PEG(l,r) { 99,  r, 99, 99 }, {  l, 50, 50, 50 }   // starts sharp, settles

// clang-format off
static const PatchDesc kPatches[kNumStarterPatches] =
{
// ---------------------------------------------------------------------------
//  op columns:  R1 R2 R3 R4   L1 L2 L3 L4   crs fin det  lvl  kvs rs ams   bp ld rd lc rc
// ---------------------------------------------------------------------------

// 1 - a warm tine electric piano: three stacked pairs - body, tine, bark -
//     with a gentle suitcase-amp tremolo on the carriers.
{ "SUITCASE  ",  5, 6,   30,  0,  0, 58, 0, 4, 0,   FLAT_PEG, 24, {
    { 96,32,26,58,  99,88, 0, 0,   1,  0,  7,   99,  2, 2, 1,  39, 0, 0, 0, 0 },
    { 96,38,30,58,  99,92,80, 0,   1,  0,  7,   86,  5, 2, 0,  39, 0,40, 0, 1 },
    { 98,36,28,58,  99,80, 0, 0,   1,  0,  9,   88,  2, 2, 1,  39, 0, 0, 0, 0 },
    { 99,55,45,70,  99,60, 0, 0,  14,  0,  7,   75,  6, 3, 0,  39, 0,50, 0, 1 },
    { 96,30,24,58,  99,86, 0, 0,   1,  0,  5,   80,  3, 2, 1,  39, 0, 0, 0, 0 },
    { 99,50,35,60,  99,84, 0, 0,   1,  0,  5,   78,  7, 2, 0,  39, 0,40, 0, 1 } } },

// 2 - glassy FM keys: a round 1:1 body plus an inharmonic 2 : 3.5 shimmer.
{ "GLASS KEYS",  5, 4,   30, 20,  4,  0, 0, 4, 2,   FLAT_PEG, 24, {
    { 97,30,25,55,  99,85, 0, 0,   1,  0,  7,   99,  2, 2, 0,  39, 0, 0, 0, 0 },
    { 97,40,30,55,  99,90,80, 0,   1,  0,  7,   84,  5, 2, 0,  39, 0,40, 0, 1 },
    { 99,38,28,55,  99,76, 0, 0,   2,  0,  8,   86,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,48,36,60,  99,80,50, 0,   3, 17,  7,   84,  6, 3, 0,  39, 0,50, 0, 1 },
    { 97,32,26,55,  99,84, 0, 0,   1,  0, 10,   82,  2, 2, 0,  39, 0, 0, 0, 0 },
    { 99,45,35,55,  99,86,70, 0,   1,  0, 10,   70,  5, 2, 0,  39, 0,40, 0, 1 } } },

// 3 - tubular bell: strike tone, octave partial and a low hum, each with an
//     inharmonic modulator, ringing on after the key is released.
{ "TUBE BELL ",  5, 0,   26,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,30,22,36,  99,84, 0, 0,   1,  0,  7,   92,  2, 2, 0,  39, 0, 0, 0, 0 },
    { 99,34,24,38,  99,86, 0, 0,   3, 17,  7,   82,  3, 2, 0,  39, 0,30, 0, 1 },
    { 99,32,22,36,  99,80, 0, 0,   2,  0,  9,   79,  2, 2, 0,  39, 0, 0, 0, 0 },
    { 99,36,26,40,  99,84, 0, 0,   5,  3,  7,   72,  4, 2, 0,  39, 0,40, 0, 1 },
    { 99,26,20,34,  99,86, 0, 0,   0,  0,  6,   77,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 99,40,30,40,  99,80, 0, 0,   1, 41,  7,   66,  4, 2, 0,  39, 0,40, 0, 1 } } },

// 4 - marimba: warm fundamental, a quicker 4th partial and a soft mallet knock.
{ "MARIMBA   ",  7, 0,   20,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,40,32,50,  99,70, 0, 0,   1,  0,  7,   99,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,60,40,55,  99,70, 0, 0,   1,  0,  7,   76,  6, 3, 0,  39, 0,30, 0, 1 },
    { 99,50,40,55,  99,70, 0, 0,   4,  0,  7,   90,  4, 3, 0,  39, 0, 0, 0, 0 },
    { 99,70,50,60,  99,30, 0, 0,   1,  0,  7,   70,  6, 3, 0,  39, 0,30, 0, 1 },
    { 99,80,60,70,  99, 0, 0, 0,   3, 17,  7,   80,  7, 2, 0,  39, 0,30, 0, 1 },
    { 99,80,60,70,  99, 0, 0, 0,   9,  0,  7,    0,  7, 2, 0,  39, 0,40, 0, 1 } } },

// 5 - upright bass: finger thump into a long, woody decay.
{ "WOOD BASS ",  3, 5,   22,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,42,30,60,  99,86, 0, 0,   1,  0,  7,   99,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,50,35,60,  99,88,70, 0,   1,  0,  7,   84,  5, 2, 0,  39, 0,30, 0, 1 },
    { 99,70,50,60,  99,40, 0, 0,   3,  0,  7,   64,  7, 2, 0,  39, 0,30, 0, 1 },
    { 99,55,34,60,  99,78, 0, 0,   1,  0,  8,   86,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,75,45,60,  99,50, 0, 0,   1,  0,  8,   72,  6, 2, 0,  39, 0,30, 0, 1 },
    { 99,80,60,70,  99, 0, 0, 0,   5,  0,  7,   62,  7, 2, 0,  39, 0,30, 0, 1 } } },

// 6 - punchy synth bass: a saw-like feedback modulator that closes like a
//     filter, over a sine sub an octave down.
{ "SYNTH BASS",  2, 5,   24,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,55,42,62,  99,94,88, 0,   1,  0,  7,   99,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 99,50,40,62,  99,94,88, 0,   1,  0,  7,   88,  5, 1, 0,  39, 0,40, 0, 1 },
    { 99,60,45,62,  99,92,88, 0,   0,  0,  7,   86,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 99,70,50,62,  99,60,40, 0,   1,  0,  7,   64,  5, 1, 0,  39, 0,30, 0, 1 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 } } },

// 7 - bright brass: modulators swell in after the carriers (the "blat"), a
//     small pitch scoop on the attack, and delayed vibrato.
{ "BRIGHT BRS", 22, 6,   34, 40,  8,  0, 0, 0, 3,   SCOOP_PEG(55), 24, {
    { 64,50,40,60,  99,94,92, 0,   1,  0,  7,   99,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 55,42,35,58,  99,96,94, 0,   1,  0,  7,   86,  3, 1, 0,  39, 0,20, 0, 0 },
    { 62,50,40,60,  99,94,92, 0,   1,  0, 10,   94,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 62,50,40,60,  99,94,92, 0,   1,  0,  4,   94,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 60,50,40,60,  99,94,92, 0,   2,  0,  7,   76,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 54,44,36,58,  99,96,94, 0,   1,  0,  7,   86,  2, 1, 0,  39, 0,20, 0, 0 } } },

// 8 - soft horn: slower, rounder, with a warm sub-octave underneath.
{ "SOFT HORN ",  5, 2,   30, 45,  7,  0, 0, 0, 3,   SCOOP_PEG(45), 24, {
    { 52,46,40,56,  99,96,94, 0,   1,  0,  7,   99,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 45,40,35,56,  99,95,93, 0,   1,  0,  7,   84,  4, 1, 0,  39, 0,15, 0, 0 },
    { 50,46,40,56,  99,96,94, 0,   1,  0,  9,   92,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 44,40,35,56,  99,95,92, 0,   1,  0,  9,   80,  4, 1, 0,  39, 0,15, 0, 0 },
    { 50,46,40,56,  99,96,94, 0,   0,  0,  7,   72,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 48,40,35,56,  99,92,90, 0,   1,  0,  7,   58,  3, 1, 0,  39, 0,30, 0, 1 } } },

// 9 - string ensemble: three detuned saw-ish pairs, slow bow, long release.
{ "STRING PAD",  5, 5,   30, 30,  6,  0, 0, 0, 3,   FLAT_PEG, 24, {
    { 44,36,30,48,  99,94,90, 0,   1,  0,  7,   88,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 40,34,30,48,  99,96,95, 0,   1,  0,  7,   93,  2, 1, 0,  39, 0,22, 0, 0 },
    { 44,36,30,48,  99,94,90, 0,   1,  0, 11,   88,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 40,34,30,48,  99,96,95, 0,   1,  0, 11,   93,  2, 1, 0,  39, 0,22, 0, 0 },
    { 44,36,30,48,  99,94,90, 0,   1,  0,  3,   88,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 40,34,30,48,  99,96,95, 0,   1,  0,  3,   91,  2, 1, 0,  39, 0,22, 0, 0 } } },

// 10 - hollow glass pad: odd-harmonic 1:2 pairs with a faint shimmer above.
{ "GLASS PAD ",  5, 3,   22, 20,  5,  0, 0, 4, 3,   FLAT_PEG, 24, {
    { 38,30,26,42,  99,95,92, 0,   1,  0,  6,   92,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 34,28,24,42,  99,96,94, 0,   2,  0,  6,   76,  2, 1, 0,  39, 0,15, 0, 0 },
    { 38,30,26,42,  99,95,92, 0,   2,  0,  9,   86,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 34,28,24,42,  99,96,94, 0,   4,  0,  9,   74,  2, 1, 0,  39, 0,15, 0, 0 },
    { 30,26,24,40,  99,94,90, 0,   4,  0,  7,   72,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 30,26,24,40,  99,95,92, 0,   7,  0,  7,   66,  2, 1, 0,  39, 0,40, 0, 1 } } },

// 11 - pipe organ: six ranks (8', 16', 4', 2 2/3', 2' and a celeste 8'),
//      with the upper ranks speaking a touch early for a little chiff.
{ "PIPE ORGAN", 32, 3,   30,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 78,60,99,60,  99,96,96, 0,   1,  0,  7,   98,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 70,60,99,58,  99,96,96, 0,   0,  0,  7,   94,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 80,60,99,62,  99,96,96, 0,   2,  0,  7,   95,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 88,55,99,62,  99,90,90, 0,   3,  0,  7,   88,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 88,55,99,64,  99,88,88, 0,   4,  0,  7,   90,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 76,60,99,60,  99,96,96, 0,   1,  0,  9,   90,  0, 0, 0,  39, 0, 0, 0, 0 } } },

// 12 - drawbar rock organ: 16', 8', 5 1/3', 4', a decaying percussion third
//      and a gritty feedback 8', with a rotary-style wobble.
{ "ROCK ORGAN", 32, 6,   43,  0,  6, 45, 0, 4, 3,   FLAT_PEG, 24, {
    { 99,99,99,72,  99,99,99, 0,   1,  0,  7,   94,  0, 0, 1,  39, 0, 0, 0, 0 },
    { 99,99,99,72,  99,99,99, 0,   0,  0,  7,   92,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,72,  99,99,99, 0,   1, 50,  7,   88,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,72,  99,99,99, 0,   2,  0,  7,   90,  0, 0, 1,  39, 0, 0, 0, 0 },
    { 99,46,99,72,  99, 0, 0, 0,   3,  0,  7,   90,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,72,  99,99,99, 0,   1,  0,  9,   92,  0, 0, 0,  39, 0, 0, 0, 0 } } },

// 13 - clavinet: nasal 1:3 bite, a quick twang and a hard damper on release.
{ "CLAVI     ",  5, 4,   26,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,50,34,78,  99,86, 0, 0,   1,  0,  7,   99,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,55,38,78,  99,92,80, 0,   3,  0,  7,   90,  5, 3, 0,  39, 0,40, 0, 1 },
    { 99,55,36,78,  99,80, 0, 0,   1,  0,  9,   96,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,62,40,78,  99,88,70, 0,   1,  0,  9,   92,  6, 3, 0,  39, 0,40, 0, 1 },
    { 99,65,45,78,  99,60, 0, 0,   2,  0,  5,   84,  4, 3, 0,  39, 0, 0, 0, 0 },
    { 99,75,50,78,  99,40, 0, 0,   5,  0,  5,   66,  7, 3, 0,  39, 0,40, 0, 1 } } },

// 14 - harpsichord: bright 8' and 4' choirs plus the jack's pluck.
{ "HARPSICORD",  5, 3,   24,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,40,30,64,  99,84, 0, 0,   1,  0,  7,   95,  1, 3, 0,  39, 0, 0, 0, 0 },
    { 99,44,32,64,  99,92,80, 0,   3,  0,  7,   84,  2, 3, 0,  39, 0,45, 0, 1 },
    { 99,42,30,64,  99,82, 0, 0,   2,  0,  9,   86,  1, 3, 0,  39, 0, 0, 0, 0 },
    { 99,46,34,64,  99,90,76, 0,   5,  0,  9,   78,  2, 3, 0,  39, 0,45, 0, 1 },
    { 99,70,50,70,  99, 0, 0, 0,   1,  0,  5,   76,  1, 2, 0,  39, 0, 0, 0, 0 },
    { 99,72,55,70,  99, 0, 0, 0,   7,  0,  5,   76,  2, 2, 0,  39, 0,40, 0, 1 } } },

// 15 - nylon guitar: soft thumb pluck that mellows as it rings.
{ "NYLON GTR ",  5, 2,   22,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,38,30,58,  99,86, 0, 0,   1,  0,  7,   99,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,50,34,58,  99,88,70, 0,   1,  0,  7,   84,  5, 3, 0,  39, 0,40, 0, 1 },
    { 99,42,32,58,  99,80, 0, 0,   2,  0,  8,   88,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,62,40,60,  99,70, 0, 0,   3,  0,  8,   76,  6, 3, 0,  39, 0,40, 0, 1 },
    { 99,78,60,70,  99, 0, 0, 0,   1,  0,  6,   78,  4, 2, 0,  39, 0, 0, 0, 0 },
    { 99,80,60,70,  99, 0, 0, 0,   6,  0,  6,   72,  6, 2, 0,  39, 0,40, 0, 1 } } },

// 16 - steel-string guitar: brighter, chorused courses and a pick attack.
{ "STEEL GTR ",  5, 4,   24,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,36,28,56,  99,86, 0, 0,   1,  0,  7,   99,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,44,32,56,  99,92,80, 0,   1,  0,  7,   88,  4, 3, 0,  39, 0,40, 0, 1 },
    { 99,38,28,56,  99,84, 0, 0,   1,  0, 10,   92,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,46,34,56,  99,88,72, 0,   3,  0, 10,   86,  5, 3, 0,  39, 0,40, 0, 1 },
    { 99,40,30,56,  99,80, 0, 0,   2,  0,  4,   82,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,70,45,60,  99,70, 0, 0,   4,  0,  4,   78,  6, 3, 0,  39, 0,40, 0, 1 } } },

// 17 - wooden flute: a breath chiff on the attack, a noisy feedback breath
//      underneath, and delayed vibrato.
{ "WOOD FLUTE",  5, 7,   32, 40, 10,  0, 0, 0, 4,   FLAT_PEG, 24, {
    { 62,50,40,60,  99,96,94, 0,   1,  0,  7,   99,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 72,50,40,60,  99,88,86, 0,   1,  0,  7,   70,  4, 1, 0,  39, 0,40, 0, 1 },
    { 60,50,40,60,  99,94,92, 0,   2,  0,  7,   80,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 70,45,40,60,  99,80,76, 0,   3,  0,  7,   68,  3, 1, 0,  39, 0, 0, 0, 0 },
    { 70,45,40,60,  99,90,86, 0,   1,  0,  7,   78,  2, 1, 0,  39, 0, 0, 0, 0 } } },

// 18 - pan pipe: hollow odd harmonics and a big breathy chiff.
{ "PAN PIPE  ",  5, 7,   30, 45,  8,  0, 0, 0, 4,   FLAT_PEG, 24, {
    { 58,45,40,58,  99,95,92, 0,   1,  0,  7,   99,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 80,44,40,58,  99,84,82, 0,   2,  0,  7,   72,  3, 1, 0,  39, 0,40, 0, 1 },
    { 58,45,40,58,  99,95,92, 0,   1,  0,  9,   86,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 78,40,36,58,  99,76,70, 0,   2,  0,  7,   74,  3, 1, 0,  39, 0, 0, 0, 0 },
    { 78,40,36,58,  99,88,84, 0,   1,  0,  7,   80,  2, 1, 0,  39, 0, 0, 0, 0 } } },

// 19 - vibraphone: long ringing bars, a faster 4th partial, a mallet tick,
//      and the motor tremolo on the two tone carriers.
{ "VIBRAPHONE",  5, 0,   32,  0,  0, 66, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,30,24,50,  99,88, 0, 0,   1,  0,  7,   99,  2, 2, 1,  39, 0, 0, 0, 0 },
    { 99,50,30,50,  99,76, 0, 0,   1,  0,  7,   64,  6, 2, 0,  39, 0,30, 0, 1 },
    { 99,42,30,50,  99,70, 0, 0,   4,  0,  7,   88,  2, 2, 1,  39, 0, 0, 0, 0 },
    { 99,60,40,55,  99,40, 0, 0,   1,  0,  7,   58,  6, 2, 0,  39, 0,30, 0, 1 },
    { 99,75,55,65,  99, 0, 0, 0,   1,  0,  7,   80,  5, 2, 0,  39, 0, 0, 0, 0 },
    { 99,78,60,65,  99, 0, 0, 0,  10,  0,  7,   76,  7, 2, 0,  39, 0,40, 0, 1 } } },

// 20 - celesta: sweet struck plates an octave up, with a brief metallic tink.
{ "CELESTA   ",  5, 0,   28,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 36, {
    { 99,40,30,52,  99,80, 0, 0,   1,  0,  7,   99,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,55,36,52,  99,76, 0, 0,   1,  0,  7,   70,  5, 3, 0,  39, 0,40, 0, 1 },
    { 99,48,34,52,  99,70, 0, 0,   4,  0,  8,   78,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,62,40,55,  99,60, 0, 0,   3, 17,  7,   66,  6, 3, 0,  39, 0,40, 0, 1 },
    { 99,44,32,52,  99,76, 0, 0,   2,  0,  6,   84,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,70,50,60,  99,20, 0, 0,   7,  0,  6,   62,  6, 3, 0,  39, 0,40, 0, 1 } } },

// 21 - square lead: two slightly detuned 1:2 pairs (odd harmonics only).
{ "SQUARE LD ",  1, 0,   35, 35, 12,  0, 0, 0, 4,   FLAT_PEG, 24, {
    { 90,60,99,64,  99,96,96, 0,   1,  0,  7,   85,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 90,50,40,64,  99,96,95, 0,   2,  0,  7,   86,  2, 1, 0,  39, 0,25, 0, 0 },
    { 90,60,99,64,  99,96,96, 0,   1,  0, 10,   80,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 90,50,40,64,  99,96,95, 0,   2,  0, 10,   86,  2, 1, 0,  39, 0,25, 0, 0 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 } } },

// 22 - saw lead: a feedback modulator for the saw edge, doubled and detuned.
{ "SAW LEAD  ",  2, 5,   35, 35, 12,  0, 0, 0, 4,   FLAT_PEG, 24, {
    { 90,60,99,64,  99,96,96, 0,   1,  0,  7,   89,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 90,55,45,64,  99,96,94, 0,   1,  0,  7,   84,  2, 1, 0,  39, 0,20, 0, 0 },
    { 90,60,99,64,  99,96,96, 0,   1,  0, 11,   85,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 90,55,45,64,  99,96,94, 0,   1,  0, 11,   84,  2, 1, 0,  39, 0,20, 0, 0 },
    { 90,60,50,64,  99,80,76, 0,   1,  0, 11,   64,  2, 1, 0,  39, 0,20, 0, 0 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 } } },

// 23 - sync-style lead: a bright modulator sweeping down while two slower
//      ones fade up, so the formant moves under a held note.
{ "SYNC LEAD ", 16, 4,   38, 30, 10,  0, 0, 0, 4,   FLAT_PEG, 24, {
    { 92,60,99,64,  99,96,96, 0,   1,  0,  7,   83,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 92,40,30,64,  99,88,88, 0,   1,  0,  7,   84,  3, 1, 0,  39, 0,20, 0, 0 },
    { 50,40,35,64,  99,90,88, 0,   2,  0,  7,   72,  2, 1, 0,  39, 0,20, 0, 0 },
    { 60,40,35,64,  99,80,70, 0,   3,  0,  7,   60,  2, 1, 0,  39, 0,20, 0, 0 },
    { 40,36,30,64,  99,92,90, 0,   3,  0,  7,   72,  2, 1, 0,  39, 0,20, 0, 0 },
    { 40,36,30,64,  99,92,90, 0,   1,  0,  7,   56,  2, 1, 0,  39, 0,20, 0, 0 } } },

// 24 - bell pad: a struck bell on top of a slowly blooming pad.
{ "BELL PAD  ",  5, 3,   24, 30,  5,  0, 0, 4, 3,   FLAT_PEG, 24, {
    { 99,30,22,40,  99,82, 0, 0,   1,  0,  7,   90,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,34,24,40,  99,74, 0, 0,   3, 17,  7,   74,  4, 2, 0,  39, 0,40, 0, 1 },
    { 40,32,28,44,  99,94,90, 0,   1,  0,  9,   94,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 36,30,26,44,  99,96,94, 0,   1,  0,  9,   78,  2, 1, 0,  39, 0,40, 0, 1 },
    { 38,32,28,44,  99,94,90, 0,   2,  0,  5,   84,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 36,30,26,44,  99,96,94, 0,   1,  0,  5,   72,  2, 1, 0,  39, 0,40, 0, 1 } } },

// 25 - glockenspiel: bright bars an octave up with their stretched partials.
{ "GLOCKEN   ",  5, 0,   26,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 36, {
    { 99,34,26,40,  99,80, 0, 0,   1,  0,  7,   99,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,42,30,42,  99,78, 0, 0,   3, 17,  7,   70,  4, 3, 0,  39, 0,30, 0, 1 },
    { 99,40,30,42,  99,72, 0, 0,   2, 38,  7,   84,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,60,40,50,  99,30, 0, 0,   1,  0,  7,   56,  6, 3, 0,  39, 0,30, 0, 1 },
    { 99,48,34,45,  99,60, 0, 0,   5,  8,  7,   76,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,70,50,60,  99, 0, 0, 0,   9,  0,  7,   66,  7, 3, 0,  39, 0,40, 0, 1 } } },

// 26 - koto: twangy nasal pluck that starts a hair sharp and settles.
{ "KOTO      ",  5, 5,   30, 30,  6,  0, 0, 0, 3,   DROP_PEG(52, 70), 24, {
    { 99,44,32,62,  99,84, 0, 0,   1,  0,  7,   99,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,50,36,62,  99,90,76, 0,   3,  0,  7,   86,  4, 3, 0,  39, 0,40, 0, 1 },
    { 99,46,32,62,  99,80, 0, 0,   1,  0,  9,   90,  2, 3, 0,  39, 0, 0, 0, 0 },
    { 99,56,40,62,  99,86,70, 0,   1,  0,  9,   84,  5, 3, 0,  39, 0,40, 0, 1 },
    { 99,70,50,66,  99,30, 0, 0,   3,  0,  5,   74,  3, 3, 0,  39, 0, 0, 0, 0 },
    { 99,76,55,66,  99,20, 0, 0,   1,  0,  5,   72,  6, 3, 0,  39, 0,40, 0, 1 } } },

// 27 - muted pick bass: a quick thump that dies away naturally.
{ "MUTED BASS",  1, 0,   20,  0,  0,  0, 0, 4, 0,   FLAT_PEG, 24, {
    { 99,50,36,70,  99,88, 0, 0,   1,  0,  7,   99,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,62,44,70,  99,84,60, 0,   1,  0,  7,   84,  5, 2, 0,  39, 0,30, 0, 1 },
    { 99,50,36,70,  99,88, 0, 0,   0,  0,  7,   90,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,70,50,70,  99,40, 0, 0,   1,  0,  7,   66,  6, 2, 0,  39, 0,30, 0, 1 },
    { 99,80,60,70,  99, 0, 0, 0,   3,  0,  7,   60,  7, 2, 0,  39, 0,30, 0, 1 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 } } },

// 28 - fat brass section: wide detune, a sub-octave, slower swell.
{ "FAT BRASS ", 22, 6,   32, 45,  8,  0, 0, 0, 3,   SCOOP_PEG(50), 24, {
    { 55,45,40,55,  99,95,93, 0,   1,  0,  7,   99,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 48,40,35,55,  99,96,94, 0,   1,  0,  7,   85,  4, 1, 0,  39, 0,20, 0, 0 },
    { 55,45,40,55,  99,95,93, 0,   1,  0, 11,   96,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 55,45,40,55,  99,95,93, 0,   1,  0,  3,   96,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 53,45,40,55,  99,95,93, 0,   0,  0,  7,   84,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 46,38,34,55,  99,96,94, 0,   1,  0,  7,   84,  4, 1, 0,  39, 0,20, 0, 0 } } },

// 29 - airy choir: soft "aah" partials, a breath of feedback noise, vibrato.
{ "AIR CHOIR ",  5, 7,   28, 40, 10,  0, 0, 0, 4,   FLAT_PEG, 24, {
    { 40,36,30,42,  99,95,92, 0,   1,  0,  7,   99,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 38,34,30,42,  99,96,94, 0,   1,  0,  7,   76,  2, 1, 0,  39, 0,15, 0, 0 },
    { 40,36,30,42,  99,95,92, 0,   2,  0,  9,   86,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 38,34,30,42,  99,96,94, 0,   1,  0,  9,   74,  2, 1, 0,  39, 0,15, 0, 0 },
    { 40,36,30,42,  99,95,92, 0,   3,  0,  5,   76,  1, 1, 0,  39, 0, 0, 0, 0 },
    { 40,36,30,42,  99,96,94, 0,   1,  0,  5,   74,  2, 1, 0,  39, 0, 0, 0, 0 } } },

// 30 - metal percussion: stacked inharmonic pairs, the upper ones dying first.
{ "METAL PERC",  5, 4,   50,  0,  0,  0, 0, 5, 0,   FLAT_PEG, 24, {
    { 99,36,26,45,  99,80, 0, 0,   1,  0,  7,   99,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,40,30,45,  99,86, 0, 0,   1, 41,  7,   86,  4, 2, 0,  39, 0,30, 0, 1 },
    { 99,42,30,45,  99,72, 0, 0,   2, 12,  9,   86,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,46,34,45,  99,84, 0, 0,   3,  6,  7,   84,  4, 2, 0,  39, 0,30, 0, 1 },
    { 99,50,36,45,  99,60, 0, 0,   3, 45,  5,   80,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,60,40,50,  99,70, 0, 0,   5,  9,  5,   84,  6, 2, 0,  39, 0,30, 0, 1 } } },

// 31 - timpani: a mallet strike whose pitch settles from slightly sharp,
//      then a long boom.
{ "TIMPANI   ",  7, 0,   18,  0,  0,  0, 0, 4, 0,   DROP_PEG(54, 48), 24, {
    { 99,40,30,46,  99,82, 0, 0,   1,  0,  7,   99,  2, 2, 0,  39, 0, 0, 0, 0 },
    { 99,55,38,50,  99,76, 0, 0,   1, 50,  7,   70,  5, 2, 0,  39, 0,30, 0, 1 },
    { 99,50,36,48,  99,70, 0, 0,   1, 50,  9,   90,  3, 2, 0,  39, 0, 0, 0, 0 },
    { 99,65,45,55,  99,40, 0, 0,   1,  0,  7,   62,  5, 2, 0,  39, 0,30, 0, 1 },
    { 99,80,60,70,  99, 0, 0, 0,   3, 10,  7,   82,  7, 2, 0,  39, 0,30, 0, 1 },
    { 99,80,60,70,  99, 0, 0, 0,   3, 10,  7,    0,  7, 2, 0,  39, 0,30, 0, 1 } } },

// 32 - clarinet: the odd-harmonic 1:2 reed, with a little buzz on the attack.
{ "CLARINET  ",  1, 0,   30, 50,  6,  0, 0, 0, 3,   FLAT_PEG, 24, {
    { 64,52,40,62,  99,96,94, 0,   1,  0,  7,   92,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 66,50,40,62,  99,96,94, 0,   2,  0,  7,   84,  4, 1, 0,  39, 0,22, 0, 0 },
    { 64,52,40,62,  99,96,94, 0,   1,  0,  8,   65,  2, 1, 0,  39, 0, 0, 0, 0 },
    { 66,50,40,62,  99,96,94, 0,   2,  0,  8,   80,  4, 1, 0,  39, 0,22, 0, 0 },
    { 80,50,40,62,  99,70,60, 0,   1,  0,  7,   60,  3, 1, 0,  39, 0,30, 0, 1 },
    { 99,99,99,99,  99,99,99, 0,   1,  0,  7,    0,  0, 0, 0,  39, 0, 0, 0, 0 } } },
};
// clang-format on

#undef FLAT_PEG
#undef SCOOP_PEG
#undef DROP_PEG

// ============================================================================
//  Expansion to VCED
// ============================================================================
// Writes patch `index` as a 155-byte VCED. Everything the table above does not
// mention - oscillator mode, oscillator key sync - gets a neutral default:
// ratio mode and key sync on, so every note starts from the same phase.
inline void buildVced (int index, uint8_t v[155])
{
    if (index < 0 || index >= kNumStarterPatches) index = 0;
    const PatchDesc& p = kPatches[index];

    std::memset (v, 0, 155);

    for (int opIndex = 0; opIndex < 6; ++opIndex)
    {
        const OpDesc& o = p.op[opIndex];
        uint8_t* b = v + (5 - opIndex) * 21;   // OP1 lives in the last block

        b[0]  = o.r1;  b[1] = o.r2;  b[2] = o.r3;  b[3] = o.r4;
        b[4]  = o.l1;  b[5] = o.l2;  b[6] = o.l3;  b[7] = o.l4;
        b[8]  = o.bp;
        b[9]  = o.ld;
        b[10] = o.rd;
        b[11] = o.lc;
        b[12] = o.rc;
        b[13] = o.rs;
        b[14] = o.ams;
        b[15] = o.kvs;
        b[16] = o.level;
        b[17] = 0;             // ratio mode
        b[18] = o.coarse;
        b[19] = o.fine;
        b[20] = o.detune;
    }

    for (int i = 0; i < 4; ++i) v[126 + i] = p.pr[i];   // pitch EG rates
    for (int i = 0; i < 4; ++i) v[130 + i] = p.pl[i];   // pitch EG levels

    v[134] = (uint8_t) ((p.alg - 1) & 31);
    v[135] = p.fb;
    v[136] = 1;              // oscillator key sync on
    v[137] = p.lfoSpeed;
    v[138] = p.lfoDelay;
    v[139] = p.pmd;
    v[140] = p.amd;
    v[141] = p.lfoSync;
    v[142] = p.lfoWave;
    v[143] = p.pms;
    v[144] = p.transpose;

    for (int i = 0; i < 10; ++i)
        v[145 + i] = (uint8_t) p.name[i];
}

inline const char* patchName (int index)
{
    if (index < 0 || index >= kNumStarterPatches) index = 0;
    return kPatches[index].name;
}

} // namespace vdx7starter
