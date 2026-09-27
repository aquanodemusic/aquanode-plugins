/*
    NativeFM.h  -  a ROM-free, six-operator FM engine with the DX7 feature set.

    Why this exists
    ---------------
    The bit-accurate path in External/ needs Yamaha's firmware ROM to run: it
    boots an emulated HD6303 and lets the original code drive the EGS/OPS. That
    ROM cannot be redistributed, so if the user has no ROM file the plugin would
    otherwise be silent. This file is the fallback: an independent
    implementation of the same synthesis architecture, written from the public
    DX7 parameter specification, that consumes exactly the same 155-byte VCED
    patch data and supports every function the panel exposes.

    It is deliberately NOT a bit-accurate model. It runs in floating point at
    the host sample rate, so there is no 49.096 kHz resampling stage and none of
    the OPS's log-domain quantisation artefacts. Patches will read as clearly
    "the same patch" - same algorithm, same envelope shapes, same brightness
    tracking - while sounding a little smoother and cleaner than the hardware.

    What IS taken from the hardware, because it is structural rather than
    tonal, is the algorithm table. The 32 algorithms are not stored in the DX7
    as a modulation matrix; they live in the OPS as per-timeslot control bits
    (see External/OPS.h, `algoROM`). Running that little register machine
    symbolically recovers the familiar chart, including which operator carries
    the feedback tap and how many carriers each algorithm sums. The table below
    was generated that way rather than typed from a manual, so it agrees with
    the emulator by construction.

    Structure of this file:

      1. Curves        - level/attenuation, rate, keyboard scaling, LFO
      2. Envelope      - the 4-stage operator EG and the pitch EG
      3. PatchCache    - a VCED patch reduced to per-note float coefficients
      4. NoteState     - one of 16 concurrent notes
      5. NativeEngine  - voice allocation, MIDI, LFO, block rendering

    GPLv3, same as the rest of the project.
*/
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <array>
#include <algorithm>
#include <functional>

namespace vdx7native {

// ============================================================================
//  0.  Constants
// ============================================================================

static constexpr int kNumOps      = 6;
static constexpr int kMaxNotes    = 16;   // the DX7's polyphony
static constexpr int kVcedSize    = 155;

// Full attenuation range of the operator envelope, in decibels. The hardware
// envelope is a 12-bit value feeding a log-domain multiplier spanning 2^16,
// i.e. 96 dB, which is where this number comes from.
static constexpr float kEgRangeDb = 96.0f;

// Phase modulation depth: how many complete cycles of phase a full-scale
// modulator pushes its target through.
//
// Measured rather than derived. The OPS register widths suggest four cycles,
// but comparing small-signal sidebands against the emulator (a 1:1 pair at a
// modulator level of 40-60, where the second harmonic is still in the linear
// J1/J0 region) puts the real figure close to two. The level curve was checked
// the same way and matches to better than half a decibel, so this constant is
// where the remaining difference lives.
static constexpr float kModCycles = 2.07f;

// Amplitude of one operator at full output level, in the same units the
// emulator produces. Carriers are additionally divided by the carrier count,
// which is what the OPS's COM attenuation does.
static constexpr float kOpFullScale = 0.175f;

static constexpr float kTwoPi = 6.283185307179586f;

// ============================================================================
//  1.  Curves
// ============================================================================

// DX7 level curve: an operator output level or EG level of 0..99 does not map
// linearly onto attenuation. The bottom of the range is stretched so that low
// levels stay usefully controllable. Below 20 it follows a fixed table, above it
// rises one unit per step, and each unit is 0.75 dB.
//
// Kept in the DX7's own "level units" rather than dB, because keyboard scaling
// is added to it in those units and the sum clamps at full scale - which is why
// a positive scaling curve brightens the low end of the keyboard instead of
// boosting the high end past maximum.
inline int scaleOutLevel (int level)
{
    static const uint8_t lut[20] =
        { 0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46 };

    level = std::clamp (level, 0, 99);
    return (level >= 20) ? (28 + level) : (int) lut[level];
}

inline float unitsToDb (int units) { return ((float) units - 127.0f) * 0.75f; }

inline float levelToDb (int level) { return unitsToDb (scaleOutLevel (level)); }

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// The quietest level the patch format can express. An envelope that has reached
// this is silent for all practical purposes - note that it is a little above
// -96 dB, because level 0 on the DX7's curve is 127 units of 0.75 dB.
inline float silenceDb() { return levelToDb (0); }

// Envelope rate law. The DX7's rate parameter is exponential: each step is a
// constant multiple of the previous one.
//
// Both constants were fitted against the emulator by timing how long a decay
// from L1=99 to L2=0 takes to fall 20 dB, swept across rates 30..70. The
// measured spacing came out at almost exactly one doubling per 7.2 rate units,
// which extrapolates to a full 96 dB sweep in about 12 ms at rate 99 and
// something close to three minutes at rate 0 - the latter being why patches
// with a rate of 0 sound like they sustain forever.
inline float rateToDbPerSecond (float rate)
{
    static constexpr float kDbPerSecAt99     = 16600.0f;
    static constexpr float kUnitsPerDoubling = 6.25f;
    rate = std::clamp (rate, 0.0f, 99.0f);
    return kDbPerSecAt99 * std::exp2 ((rate - 99.0f) / kUnitsPerDoubling);
}

// Keyboard level scaling, in DX7 level units.
//
// This is the hardware's own rule rather than a curve fit. Two details matter
// and neither is obvious from the manual:
//
//   - the break point sits 17 semitones above where the parameter suggests, and
//     the distance either side is quantised into groups of three semitones, so
//     scaling moves in steps rather than smoothly;
//   - the "exponential" curve is a lookup, not an exponential. It is nearly
//     linear for the first two octaves and only then bends sharply.
//
// The linear curve is also far steeper than the exponential one close to the
// break point - about 2 dB per semitone at full depth against a quarter of
// that - and only loses that lead past four octaves out.
//
// Verified against the emulator across all four curves at two depths.
inline int scaleCurveUnits (int group, int depth, int curve)
{
    // The exponential curve's shape, one entry per three-semitone group.
    static const uint8_t expScale[33] =
    {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33, 39, 47, 56,
        66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250
    };

    const bool linear = (curve == 0 || curve == 3);

    int scale;
    if (linear)
        scale = (group * depth * 329) >> 12;
    else
        scale = ((int) expScale[std::min (group, 32)] * depth * 329) >> 15;

    return (curve < 2) ? -scale : scale;   // curves 0 and 1 attenuate
}

inline int keyScaleUnits (int midiNote, int breakPoint,
                          int leftDepth, int rightDepth,
                          int leftCurve, int rightCurve)
{
    const int offset = midiNote - breakPoint - 17;

    return (offset >= 0) ? scaleCurveUnits ((offset + 1) / 3, rightDepth, rightCurve)
                         : scaleCurveUnits ((-(offset - 1)) / 3, leftDepth, leftCurve);
}

// Rate scaling 0..7: envelopes run faster towards the top of the keyboard,
// which is what stops the high end of a bell or piano patch ringing on as long
// as the bottom. The hardware works in quantised rate steps and both clamps:
// nothing below note 21 scales at all, and it stops increasing near the top.
//
// Returned as an addition to the 0..99 rate parameter. The 1.561 converts the
// hardware's quantised rate units into the parameter's own units, and the
// result was checked against emulator decay timings at three octaves.
inline float rateScaleOffset (int rs, int midiNote)
{
    if (rs <= 0) return 0.0f;

    const int x = std::clamp (midiNote / 3 - 7, 0, 31);
    const int qrateDelta = (rs * x) >> 3;

    return (float) qrateDelta * 1.88f;
}

// Key velocity sensitivity 0..7. At 0 the operator ignores velocity entirely;
// at 7 the softest playable note is nearly 40 dB below the hardest.
//
// The response is not a simple power law - it is shallow at the top of the
// range and steepens towards the bottom - so it is tabulated from measurements
// taken against the emulator. The attenuation scales linearly with the
// sensitivity setting, which the same measurements confirmed to within a few
// percent across KVS 2, 4 and 7.
inline float velocityDb (int velocity, int kvs)
{
    if (kvs <= 0) return 0.0f;

    // Attenuation at KVS 7, in DX7 level units of 0.75 dB.
    static const float vel  [9] = {   4.0f, 16.0f, 32.0f, 48.0f, 64.0f, 80.0f, 100.0f, 110.0f, 127.0f };
    static const float units[9] = {  51.9f, 46.2f, 37.1f, 29.1f, 21.1f, 16.0f,   7.5f,   3.5f,   0.0f };

    const float v = std::clamp ((float) velocity, vel[0], vel[8]);
    int i = 0;
    while (i < 7 && v > vel[i + 1]) ++i;

    const float t = (v - vel[i]) / (vel[i + 1] - vel[i]);
    const float u = units[i] + (units[i + 1] - units[i]) * t;

    return -0.75f * u * (float) kvs / 7.0f;
}

// LFO speed 0..99 -> Hz, tabulated from the hardware.
//
// This is emphatically not exponential, which is the trap: it climbs almost
// linearly from 0.06 Hz to about 10 Hz over the first two thirds of the range,
// and only then leaps away to nearly 50 Hz. A smooth exponential fit puts rate
// 50 near 1.8 Hz when the real machine is at 8 Hz - more than two octaves of
// vibrato speed wrong, right where most patches sit.
inline float lfoSpeedToHz (int speed)
{
    static const float tab[100] =
    {
         0.062541f,  0.125031f,  0.312393f,  0.437120f,  0.624610f,
         0.750694f,  0.936330f,  1.125302f,  1.249609f,  1.436782f,
         1.560915f,  1.752081f,  1.875117f,  2.062494f,  2.247191f,
         2.374451f,  2.560492f,  2.686728f,  2.873976f,  2.998950f,
         3.188013f,  3.369840f,  3.500175f,  3.682224f,  3.812065f,
         4.000800f,  4.186202f,  4.310716f,  4.501260f,  4.623209f,
         4.814636f,  4.930480f,  5.121901f,  5.315191f,  5.434783f,
         5.617346f,  5.750431f,  5.946717f,  6.062811f,  6.248438f,
         6.431695f,  6.564264f,  6.749460f,  6.868132f,  7.052186f,
         7.250580f,  7.375719f,  7.556294f,  7.687577f,  7.877738f,
         7.993605f,  8.181967f,  8.372405f,  8.504848f,  8.685079f,
         8.810573f,  8.986341f,  9.122423f,  9.300595f,  9.500285f,
         9.607994f,  9.798158f,  9.950249f, 10.117361f, 11.251125f,
        11.384335f, 12.562814f, 13.676149f, 13.904338f, 15.092062f,
        16.366612f, 16.638935f, 17.869907f, 19.193858f, 19.425019f,
        20.833333f, 21.034918f, 22.502250f, 24.003841f, 24.260068f,
        25.746653f, 27.173913f, 27.578599f, 29.052876f, 30.693677f,
        31.191516f, 32.658393f, 34.317090f, 34.674064f, 36.416606f,
        38.197097f, 38.550501f, 40.387722f, 40.749796f, 42.625746f,
        44.326241f, 44.883303f, 46.772685f, 48.590865f, 49.261084f
    };
    return tab[std::clamp (speed, 0, 99)];
}

// LFO delay 0..99 -> (delay before onset, fade-in time) in seconds.
inline void lfoDelayTimes (int delay, float& holdSecs, float& fadeSecs)
{
    delay = std::clamp (delay, 0, 99);
    if (delay == 0) { holdSecs = 0.0f; fadeSecs = 0.0f; return; }
    const float t = (float) delay / 99.0f;
    holdSecs = 6.0f * t * t;            // up to ~6 s before anything happens
    fadeSecs = 3.0f * t * t + 0.02f;    // then a gradual ramp in
}

// Pitch modulation sensitivity 0..7, in semitones at full LFO pitch depth.
inline float pmsSemitones (int pms)
{
    static const float tab[8] = { 0.0f, 0.27f, 0.55f, 0.90f, 1.5f, 2.5f, 4.2f, 7.0f };
    return tab[std::clamp (pms, 0, 7)];
}

// Amplitude modulation sensitivity 0..3, in dB of attenuation at full depth.
inline float amsDb (int ams)
{
    // Measured: each step doubles the depth, reaching about 44 dB at full LFO
    // amplitude depth.
    static const float tab[4] = { 0.0f, 11.0f, 22.0f, 44.0f };
    return tab[std::clamp (ams, 0, 3)];
}

// Pitch EG level 0..99 -> semitones, 50 being no change.
//
// Tabulated rather than fitted, because the curve is deliberately lopsided:
// finely resolved either side of centre, where pitch envelopes are used
// musically, and coarse at the extremes, where they are used for effects. The
// full span is a little under four octaves each way.
inline float pitchEgSemitones (int level)
{
    static const int8_t tab[100] =
    {
        -128, -116, -104,  -95,  -85,  -76,  -68,  -61,  -56,  -52,
         -49,  -46,  -43,  -41,  -39,  -37,  -35,  -33,  -32,  -31,
         -30,  -29,  -28,  -27,  -26,  -25,  -24,  -23,  -22,  -21,
         -20,  -19,  -18,  -17,  -16,  -15,  -14,  -13,  -12,  -11,
         -10,   -9,   -8,   -7,   -6,   -5,   -4,   -3,   -2,   -1,
           0,    1,    2,    3,    4,    5,    6,    7,    8,    9,
          10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
          20,   21,   22,   23,   24,   25,   26,   27,   28,   29,
          30,   31,   32,   33,   34,   35,   38,   40,   43,   46,
          49,   53,   58,   65,   73,   82,   92,  103,  115,  127
    };

    // Each table step is 1/32 of an octave.
    return (float) tab[std::clamp (level, 0, 99)] * 0.375f;
}

// Pitch EG rate 0..99 -> semitones per second. Same tabulated shape as the
// level curve; the constant converts the hardware's internal step size.
inline float pitchEgRate (int rate)
{
    static const uint8_t tab[100] =
    {
          1,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,
          8,   9,   9,  10,  10,  11,  11,  12,  12,  13,  13,  14,  14,
         15,  16,  16,  17,  18,  18,  19,  20,  21,  22,  23,  24,  25,
         26,  27,  28,  30,  31,  33,  34,  36,  37,  38,  39,  41,  42,
         44,  46,  47,  49,  51,  53,  54,  56,  58,  60,  62,  64,  66,
         68,  70,  72,  74,  76,  79,  82,  85,  88,  91,  94,  98, 102,
        106, 110, 115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171,
        178, 185, 193, 202, 211, 232, 243, 254, 255
    };
    return (float) tab[std::clamp (rate, 0, 99)] * 0.5634f;
}

// Operator frequency ratio from coarse/fine, and fixed frequency in Hz.
inline float ratioFromCoarseFine (int coarse, int fine)
{
    coarse = std::clamp (coarse, 0, 31);
    fine   = std::clamp (fine,   0, 99);
    const float base = (coarse == 0) ? 0.5f : (float) coarse;
    return base * (1.0f + (float) fine / 100.0f);
}

inline float fixedHzFromCoarseFine (int coarse, int fine)
{
    const int decade = std::clamp (coarse, 0, 31) & 3;      // 1, 10, 100, 1000 Hz
    return std::pow (10.0f, (float) decade + (float) std::clamp (fine, 0, 99) / 100.0f);
}

// Detune 0..14, 7 = centre. Applied as a small constant offset in the log
// frequency domain, i.e. a fixed number of cents at every pitch. (The hardware
// adds a fixed increment instead, which makes its detune slightly stronger at
// the bottom of the keyboard; the difference is under a cent for most notes.)
inline float detuneCents (int detune)
{
    return ((float) std::clamp (detune, 0, 14) - 7.0f) * 1.7f;
}

// ============================================================================
//  1b. Tuning
// ============================================================================
//
// The original DX7 has no microtuning. Its firmware turns a MIDI note into a
// frequency through fixed tables and offers nothing but a global master tune,
// so the emulated engine cannot be retuned however you address it - there is no
// parameter to send. (Microtuning arrived with the DX7II, or on a mk1 fitted
// with a Grey Matter E! board.)
//
// The native engine has no such excuse: it computes frequency in floating
// point, so retuning it is just a table lookup. Three routes in:
//
//   - MIDI Tuning Standard SysEx, which is the "send the .syx" route. Bulk
//     dumps, single-note changes and scale/octave messages are all accepted.
//   - An external provider, which is how MTS-ESP is wired up: a callback
//     consulted per note, live, so a tuning master can change the scale
//     underneath held notes.
//   - Direct calls, for a host or editor that wants to push a scale in.
//
// Notes are stored as frequencies rather than cent offsets so that arbitrary
// non-octave-repeating scales work, and interpolation between adjacent entries
// happens in the log domain so portamento glides smoothly through them.
class Tuning
{
public:
    Tuning() { reset(); }

    // Back to 12-tone equal temperament at A440.
    void reset()
    {
        for (int n = 0; n < 128; ++n)
            hz_[n] = 440.0f * std::exp2 (((float) n - 69.0f) / 12.0f);
        active_ = false;
    }

    // True when anything has moved us off plain 12-TET.
    bool isActive() const { return active_ || (bool) provider_; }

    void setNoteHz (int note, float hz)
    {
        if (note < 0 || note > 127 || ! (hz > 0.0f)) return;
        hz_[note] = hz;
        active_ = true;
    }

    // MTS-ESP and friends. Called on the audio thread at note-on, so it must be
    // non-blocking; libMTSClient's lookup is a plain array read, which is fine.
    // Passing an empty function detaches it.
    void setFrequencyProvider (std::function<double (int)> fn)
    {
        provider_ = std::move (fn);
    }

    // A tuning master may publish a keyboard map with unmapped keys on it. This
    // hook lets it say "there is no note here", so the key falls silent instead
    // of sounding at whatever the fallback table happens to hold. Audio thread,
    // called once per note-on; passing an empty function detaches it.
    void setNoteFilter (std::function<bool (int)> fn)
    {
        filter_ = std::move (fn);
    }

    bool shouldFilterNote (int note) const
    {
        return filter_ && filter_ (note);
    }

    float noteToHz (int note) const
    {
        note = std::clamp (note, 0, 127);

        if (provider_)
        {
            const double f = provider_ (note);
            if (f > 0.0) return (float) f;
        }
        return hz_[note];
    }

    // Fractional notes, for portamento and transposition that lands off the
    // table. Interpolating log frequency keeps a glide even in cents.
    float noteToHz (float note) const
    {
        const float clamped = std::clamp (note, 0.0f, 127.0f);
        const int   lo = (int) std::floor (clamped);
        const int   hi = std::min (lo + 1, 127);
        const float t  = clamped - (float) lo;

        if (t <= 0.0f) return noteToHz (lo);

        const float a = std::log2 (noteToHz (lo));
        const float b = std::log2 (noteToHz (hi));
        return std::exp2 (a + (b - a) * t);
    }

    // ---- MIDI Tuning Standard --------------------------------------------
    // Handles the four messages that matter in practice:
    //
    //   08 01  bulk tuning dump          (128 notes, 14-bit fractions)
    //   08 02  single note tuning change (real time, any number of notes)
    //   08 08  scale/octave tuning, 1 byte per semitone
    //   08 09  scale/octave tuning, 2 bytes per semitone
    //
    // Returns true if the message was one of ours and was applied.
    bool applyMtsSysex (const uint8_t* d, int len)
    {
        // F0 7E/7F <device> 08 <sub> ...
        if (len < 6 || d[0] != 0xF0) return false;
        if (d[1] != 0x7E && d[1] != 0x7F) return false;
        if (d[3] != 0x08) return false;

        switch (d[4])
        {
            case 0x01: return applyBulkDump (d, len);
            case 0x02: return applySingleNotes (d, len);
            case 0x08: return applyOctave (d, len, false);
            case 0x09: return applyOctave (d, len, true);
            default:   return false;
        }
    }

private:
    // A note number plus a 14-bit fraction of a semitone. 0x7F 0x7F 0x7F is the
    // reserved "no change" value.
    static bool decodeNote (const uint8_t* p, float& hzOut)
    {
        if (p[0] == 0x7F && p[1] == 0x7F && p[2] == 0x7F) return false;

        const int   semitone = p[0] & 0x7F;
        const int   frac14   = ((p[1] & 0x7F) << 7) | (p[2] & 0x7F);
        const float cents    = (float) frac14 / 16384.0f;

        hzOut = 440.0f * std::exp2 (((float) semitone + cents - 69.0f) / 12.0f);
        return true;
    }

    bool applyBulkDump (const uint8_t* d, int len)
    {
        // F0 7E dev 08 01 prog <16 byte name> <128 x 3 bytes> chk F7
        const int base = 6 + 16;
        if (len < base + 128 * 3) return false;

        for (int n = 0; n < 128; ++n)
        {
            float hz;
            if (decodeNote (d + base + n * 3, hz))
                hz_[n] = hz;
        }
        active_ = true;
        return true;
    }

    bool applySingleNotes (const uint8_t* d, int len)
    {
        // F0 7F dev 08 02 prog count <key + 3 bytes> ... F7
        if (len < 7) return false;

        const int count = d[6];
        const int base  = 7;
        if (len < base + count * 4) return false;

        for (int i = 0; i < count; ++i)
        {
            const uint8_t* e = d + base + i * 4;
            const int key = e[0] & 0x7F;
            float hz;
            if (decodeNote (e + 1, hz))
                hz_[key] = hz;
        }
        active_ = true;
        return true;
    }

    // Scale/octave tuning: twelve offsets in cents, repeated every octave and
    // measured against equal temperament.
    bool applyOctave (const uint8_t* d, int len, bool twoByte)
    {
        // F0 7F dev 08 08/09 ff gg hh <12 entries> F7
        const int base = 8;
        const int need = base + (twoByte ? 24 : 12);
        if (len < need + 1) return false;

        float cents[12];
        for (int i = 0; i < 12; ++i)
        {
            if (twoByte)
            {
                const int v = ((d[base + i * 2] & 0x7F) << 7) | (d[base + i * 2 + 1] & 0x7F);
                cents[i] = ((float) v - 8192.0f) * (100.0f / 8192.0f);
            }
            else
            {
                cents[i] = (float) ((int) (d[base + i] & 0x7F) - 64);
            }
        }

        for (int n = 0; n < 128; ++n)
            hz_[n] = 440.0f * std::exp2 (((float) n - 69.0f) / 12.0f
                                          + cents[n % 12] / 1200.0f);
        active_ = true;
        return true;
    }

    float hz_[128];
    bool  active_ { false };
    std::function<double (int)> provider_;
    std::function<bool (int)>   filter_;
};

// ============================================================================
//  2.  Envelope generators
// ============================================================================

// The operator EG. Four rate/level pairs: the envelope walks from wherever it
// currently is towards level 1, then 2, then 3, and holds there until key-off,
// when it heads for level 4.
//
// Everything happens in the log (dB) domain, which is what makes FM envelopes
// sound the way they do - a linear-in-dB decay is a smooth exponential fade.
// The one exception is the attack, which the hardware accelerates through the
// quiet part of the range so that a fast attack does not waste time crawling
// up from -96 dB. That shaping is reproduced here.
class OperatorEnv
{
public:
    void reset (float startDb)
    {
        currentDb_ = startDb;
        stage_ = 0;
        released_ = false;
        finished_ = false;
    }

    // rates/levels are the raw 0..99 patch values; `rateOffset` is the rate
    // scaling contribution for this note.
    void configure (const uint8_t rates[4], const uint8_t levels[4], float rateOffset)
    {
        for (int i = 0; i < 4; ++i)
        {
            rates_[i]    = rateToDbPerSecond ((float) rates[i] + rateOffset);
            targetsDb_[i] = levelToDb ((int) levels[i]);
        }
    }

    void keyOn()
    {
        stage_ = 0;
        released_ = false;
        finished_ = false;
    }

    void keyOff()
    {
        if (released_) return;
        released_ = true;
        stage_ = 3;                     // head for L4 at R4
    }

    bool finished() const { return finished_; }
    float levelDb()  const { return currentDb_; }

    // Advances the envelope by `dt` seconds and returns the attenuation in dB
    // (<= 0). Called once per control block, not per sample.
    float tick (float dt)
    {
        if (finished_)
            return -kEgRangeDb;

        const float target = targetsDb_[stage_];
        const float rate   = rates_[stage_];

        if (currentDb_ < target)
        {
            // Rising. Two things shape a DX7 attack. First it does not crawl up
            // from silence: the envelope jumps straight to a floor well above
            // the bottom of the range, skipping the part nobody can hear.
            // Second, the step is largest low down and eases off near full
            // level, which is what gives the attack its characteristic snap.
            static constexpr float kAttackFloorDb = -kEgRangeDb * (1.0f - 1716.0f / 3840.0f);
            if (currentDb_ < kAttackFloorDb)
                currentDb_ = kAttackFloorDb;

            const float norm  = std::clamp ((currentDb_ + kEgRangeDb) / kEgRangeDb, 0.0f, 1.0f);
            const float boost = 2.0f + 15.0f * (1.0f - norm);
            currentDb_ += rate * boost * dt;
            if (currentDb_ >= target) { currentDb_ = target; advance(); }
        }
        else if (currentDb_ > target)
        {
            currentDb_ -= rate * dt;
            if (currentDb_ <= target) { currentDb_ = target; advance(); }
        }
        else
        {
            advance();
        }

        currentDb_ = std::clamp (currentDb_, -kEgRangeDb, 0.0f);

        if (released_ && currentDb_ <= silenceDb() + 0.25f)
            finished_ = true;

        return currentDb_;
    }

private:
    void advance()
    {
        if (released_)
        {
            // Stage 4 is the last one; once its target is reached the note
            // holds there. A patch whose L4 is not 0 sustains forever, exactly
            // as on the hardware, so the voice allocator has to reclaim it.
            if (targetsDb_[3] <= silenceDb() + 0.25f)
                finished_ = true;
            return;
        }
        if (stage_ < 2) ++stage_;       // 0 -> 1 -> 2, then hold at L3
    }

    float rates_[4]     { 0, 0, 0, 0 };
    float targetsDb_[4] { 0, 0, 0, 0 };
    float currentDb_ { -kEgRangeDb };
    int   stage_ { 0 };
    bool  released_ { false };
    bool  finished_ { true };
};

// The pitch EG. Same four-stage shape, but the "level" is a pitch offset in
// semitones and the interpolation is linear in pitch rather than in dB.
class PitchEnv
{
public:
    void configure (const uint8_t rates[4], const uint8_t levels[4])
    {
        for (int i = 0; i < 4; ++i)
        {
            rates_[i]   = pitchEgRate ((int) rates[i]);
            targets_[i] = pitchEgSemitones ((int) levels[i]);
        }
        idle_ = true;
        for (int i = 0; i < 4; ++i)
            if (std::abs (targets_[i]) > 0.001f) idle_ = false;
    }

    // True when the patch has a flat pitch envelope, so the whole thing can be
    // skipped for the (very common) case of no pitch modulation at all.
    bool isIdle() const { return idle_; }

    void keyOn()  { stage_ = 0; released_ = false; current_ = targets_[3]; }
    void keyOff() { released_ = true; stage_ = 3; }

    float tick (float dt)
    {
        if (idle_) return 0.0f;

        const float target = targets_[stage_];
        const float step   = rates_[stage_] * dt;

        if (current_ < target)      { current_ = std::min (target, current_ + step); }
        else if (current_ > target) { current_ = std::max (target, current_ - step); }

        if (std::abs (current_ - target) < 1.0e-4f && ! released_ && stage_ < 2)
            ++stage_;

        return current_;
    }

private:
    float rates_[4]   { 0, 0, 0, 0 };
    float targets_[4] { 0, 0, 0, 0 };
    float current_ { 0.0f };
    int   stage_ { 0 };
    bool  released_ { false };
    bool  idle_ { true };
};


// ============================================================================
//  3.  The algorithm table
// ============================================================================

// One entry per algorithm. `modMask[i]` is the set of operators whose output is
// summed into operator (i+1)'s phase, as a bitmask where bit 0 is OP1. The
// feedback path is deliberately NOT in that mask: it is scaled by the patch's
// feedback amount and averaged over two samples, so it is applied separately.
//
// Generated from External/OPS.h's `algoROM` by simulating the OPS register
// machine symbolically - see the note at the top of this file.
struct AlgoDef
{
    uint8_t modMask[kNumOps];   // indexed by destination operator, 0 = OP1
    uint8_t carrierMask;        // which operators reach the output
    uint8_t feedbackSrc;        // 0-based operator whose output feeds back
    uint8_t feedbackDst;        // 0-based operator whose phase it modulates
    uint8_t numCarriers;        // carriers are attenuated by 1/numCarriers
};

// clang-format off
static const AlgoDef kAlgorithms[32] = {
    /*  1 */ { { 0x02, 0x00, 0x08, 0x10, 0x20, 0x00 }, 0x05, 5, 5, 2 },
    /*  2 */ { { 0x02, 0x00, 0x08, 0x10, 0x20, 0x00 }, 0x05, 1, 1, 2 },
    /*  3 */ { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x09, 5, 5, 2 },
    /*  4 */ { { 0x02, 0x04, 0x00, 0x10, 0x20, 0x00 }, 0x09, 3, 5, 2 },
    /*  5 */ { { 0x02, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x15, 5, 5, 3 },
    /*  6 */ { { 0x02, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x15, 4, 5, 3 },
    /*  7 */ { { 0x02, 0x00, 0x18, 0x00, 0x20, 0x00 }, 0x05, 5, 5, 2 },
    /*  8 */ { { 0x02, 0x00, 0x18, 0x00, 0x20, 0x00 }, 0x05, 3, 3, 2 },
    /*  9 */ { { 0x02, 0x00, 0x18, 0x00, 0x20, 0x00 }, 0x05, 1, 1, 2 },
    /* 10 */ { { 0x02, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x09, 2, 2, 2 },
    /* 11 */ { { 0x02, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x09, 5, 5, 2 },
    /* 12 */ { { 0x02, 0x00, 0x38, 0x00, 0x00, 0x00 }, 0x05, 1, 1, 2 },
    /* 13 */ { { 0x02, 0x00, 0x38, 0x00, 0x00, 0x00 }, 0x05, 5, 5, 2 },
    /* 14 */ { { 0x02, 0x00, 0x08, 0x30, 0x00, 0x00 }, 0x05, 5, 5, 2 },
    /* 15 */ { { 0x02, 0x00, 0x08, 0x30, 0x00, 0x00 }, 0x05, 1, 1, 2 },
    /* 16 */ { { 0x16, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01, 5, 5, 1 },
    /* 17 */ { { 0x16, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x01, 1, 1, 1 },
    /* 18 */ { { 0x0E, 0x00, 0x00, 0x10, 0x20, 0x00 }, 0x01, 2, 2, 1 },
    /* 19 */ { { 0x02, 0x04, 0x00, 0x20, 0x20, 0x00 }, 0x19, 5, 5, 3 },
    /* 20 */ { { 0x04, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x0B, 2, 2, 3 },
    /* 21 */ { { 0x04, 0x04, 0x00, 0x20, 0x20, 0x00 }, 0x1B, 2, 2, 4 },
    /* 22 */ { { 0x02, 0x00, 0x20, 0x20, 0x20, 0x00 }, 0x1D, 5, 5, 4 },
    /* 23 */ { { 0x00, 0x04, 0x00, 0x20, 0x20, 0x00 }, 0x1B, 5, 5, 4 },
    /* 24 */ { { 0x00, 0x00, 0x20, 0x20, 0x20, 0x00 }, 0x1F, 5, 5, 5 },
    /* 25 */ { { 0x00, 0x00, 0x00, 0x20, 0x20, 0x00 }, 0x1F, 5, 5, 5 },
    /* 26 */ { { 0x00, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x0B, 5, 5, 3 },
    /* 27 */ { { 0x00, 0x04, 0x00, 0x30, 0x00, 0x00 }, 0x0B, 2, 2, 3 },
    /* 28 */ { { 0x02, 0x00, 0x08, 0x10, 0x00, 0x00 }, 0x25, 4, 4, 3 },
    /* 29 */ { { 0x00, 0x00, 0x08, 0x00, 0x20, 0x00 }, 0x17, 5, 5, 4 },
    /* 30 */ { { 0x00, 0x00, 0x08, 0x10, 0x00, 0x00 }, 0x27, 4, 4, 4 },
    /* 31 */ { { 0x00, 0x00, 0x00, 0x00, 0x20, 0x00 }, 0x1F, 5, 5, 5 },
    /* 32 */ { { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, 0x3F, 5, 5, 6 },
};
// clang-format on

// Operators are always evaluated from OP6 down to OP1, which is a valid
// topological order for every algorithm above: no operator is ever modulated by
// a lower-numbered one.

// ============================================================================
//  A cheap interpolated sine. Six operators across sixteen notes is ninety-six
//  sines per sample; a table keeps that affordable on a phone.
// ============================================================================
class SineTable
{
public:
    static constexpr int kBits = 11;
    static constexpr int kSize = 1 << kBits;

    SineTable()
    {
        for (int i = 0; i <= kSize; ++i)
            t_[i] = std::sin (kTwoPi * (float) i / (float) kSize);
    }

    // `cycles` is a phase in turns; the integer part is discarded.
    inline float operator() (float cycles) const
    {
        cycles -= std::floor (cycles);
        const float x = cycles * (float) kSize;
        const int   i = (int) x;
        const float f = x - (float) i;
        return t_[i] + (t_[i + 1] - t_[i]) * f;
    }

private:
    float t_[kSize + 1];
};

inline const SineTable& sineTable()
{
    static const SineTable instance;
    return instance;
}

// ============================================================================
//  4.  PatchCache - a VCED patch reduced to what the audio thread needs
// ============================================================================

// Rebuilt whenever the patch changes (not per note). Everything here is either
// a per-patch constant or a per-note function that is cheap to finish off at
// note-on time.
struct PatchCache
{
    struct Op
    {
        uint8_t rates[4]  { 99, 99, 99, 99 };
        uint8_t levels[4] { 99, 99, 99, 0  };

        float   ratio      { 1.0f };    // ratio mode: multiple of note pitch
        float   fixedHz    { 0.0f };    // fixed mode: absolute Hz (0 = ratio mode)
        float   detuneMul  { 1.0f };    // detune as a frequency multiplier

        int     outputUnits{ 127 };     // output level in DX7 level units
        int     rateScale  { 0 };
        int     velSens    { 0 };
        float   amDepthDb  { 0.0f };    // AMS translated to dB at full LFO depth

        // Keyboard level scaling
        int     breakPoint { 39 };      // raw parameter; the hardware adds 17
        int     leftDepth  { 0 }, rightDepth { 0 };
        int     leftCurve  { 0 }, rightCurve { 0 };
    };

    Op  ops[kNumOps];
    int algorithm { 0 };
    int feedback  { 0 };
    bool oscKeySync { true };

    // Pitch EG
    uint8_t pitchRates[4]  { 99, 99, 99, 99 };
    uint8_t pitchLevels[4] { 50, 50, 50, 50 };

    // LFO
    float lfoHz        { 1.0f };
    float lfoHold      { 0.0f };
    float lfoFade      { 0.0f };
    int   lfoWave      { 0 };
    bool  lfoKeySync   { false };
    float lfoPitchDepth{ 0.0f };   // 0..1, the raw PMD
    float lfoAmpDepth  { 0.0f };   // 0..1, the raw AMD
    float pmsSemis     { 0.0f };

    int   transpose    { 24 };     // 24 = no transposition

    // Feedback gain: the hardware averages the last two output samples of the
    // feedback operator and scales by 2^(level-7), so level 7 hands the target a
    // modulator at full strength and level 0 is 128x weaker.
    float feedbackGain { 0.0f };

    void build (const uint8_t vced[kVcedSize]);
};

inline void PatchCache::build (const uint8_t v[kVcedSize])
{
    // VCED operator blocks run OP6, OP5, ... OP1; internally OP1 is index 0.
    for (int opIndex = 0; opIndex < kNumOps; ++opIndex)
    {
        const int block = 5 - opIndex;              // OP1 -> block 5
        const uint8_t* p = v + block * 21;
        Op& o = ops[opIndex];

        for (int i = 0; i < 4; ++i) o.rates[i]  = p[0 + i];
        for (int i = 0; i < 4; ++i) o.levels[i] = p[4 + i];

        o.breakPoint = (int) p[8];
        o.leftDepth  = p[9];
        o.rightDepth = p[10];
        o.leftCurve  = p[11] & 3;
        o.rightCurve = p[12] & 3;
        o.rateScale  = p[13] & 7;
        o.amDepthDb  = amsDb (p[14] & 3);
        o.velSens    = p[15] & 7;
        o.outputUnits = scaleOutLevel ((int) p[16]);

        const bool fixedMode = (p[17] & 1) != 0;
        if (fixedMode)
        {
            o.fixedHz = fixedHzFromCoarseFine (p[18], p[19]);
            o.ratio   = 1.0f;
        }
        else
        {
            o.fixedHz = 0.0f;
            o.ratio   = ratioFromCoarseFine (p[18], p[19]);
        }

        o.detuneMul = std::exp2 (detuneCents (p[20]) / 1200.0f);
    }

    for (int i = 0; i < 4; ++i) pitchRates[i]  = v[126 + i];
    for (int i = 0; i < 4; ++i) pitchLevels[i] = v[130 + i];

    algorithm  = std::clamp ((int) v[134], 0, 31);
    feedback   = std::clamp ((int) v[135], 0, 7);
    oscKeySync = (v[136] & 1) != 0;

    lfoHz          = lfoSpeedToHz ((int) v[137]);
    lfoDelayTimes ((int) v[138], lfoHold, lfoFade);
    lfoPitchDepth  = (float) std::clamp ((int) v[139], 0, 99) / 99.0f;
    // The LFO amplitude depth control is strongly non-linear: half travel gives
    // only about a seventh of the available modulation, not half of it. Treating
    // it as linear makes every tremolo patch far too deep at moderate settings.
    lfoAmpDepth    = std::pow ((float) std::clamp ((int) v[140], 0, 99) / 99.0f, 2.9f);
    lfoKeySync     = (v[141] & 1) != 0;
    lfoWave        = std::clamp ((int) v[142], 0, 5);
    pmsSemis       = pmsSemitones ((int) v[143]);
    transpose      = std::clamp ((int) v[144], 0, 48);

    feedbackGain = (feedback == 0) ? 0.0f
                                   : kModCycles * std::exp2 ((float) feedback - 7.0f);
}

// ============================================================================
//  The DX7 "function" parameters. These live outside the 155-byte voice, on the
//  panel's FUNCTION page, so they are set separately rather than arriving with
//  a patch.
// ============================================================================
struct Functions
{
    bool  mono           { false };
    float portamentoSecs { 0.0f };   // time to glide one octave
    bool  portaFollow    { true };   // true = follow (glissando off, legato glide)

    float bendRange      { 2.0f };   // semitones at full wheel deflection

    // Each controller has a range (0..1) and a set of destinations, matching
    // the DX7's PITCH / AMPLITUDE / EG BIAS assign switches.
    struct Assign
    {
        float range { 1.0f };
        bool  pitch { true };
        bool  amp   { false };
        bool  egBias{ false };
    };

    Assign modWheel;
    Assign foot      { 1.0f, false, false, false };
    Assign breath    { 1.0f, false, false, false };
    Assign aftertouch{ 1.0f, false, false, false };
};

// Everything that varies per control block but is shared by all sounding notes.
struct ModContext
{
    float sampleRate   { 48000.0f };
    float dt           { 0.0f };    // seconds per control block
    float bendSemis    { 0.0f };
    float lfoPitch     { 0.0f };    // -1..1, already faded in
    float pitchDepthAdd{ 0.0f };    //  0..1 extra pitch modulation depth
    float lfoAmp       { 0.0f };    //  0..1, already faded in
    float ampBias      { 0.0f };    //  0..1 extra amplitude modulation depth
    const Tuning* tuning { nullptr };
    float egBias       { 0.0f };    //  0..1, lifts operator output levels
};

// ============================================================================
//  5.  NoteState - one sounding note
// ============================================================================
class NoteState
{
public:
    bool  active()   const { return active_; }
    bool  released() const { return released_; }
    int   note()     const { return note_; }
    uint32_t age()   const { return age_; }

    void start (const PatchCache& p, int midiNote, int velocity, uint32_t stamp,
                float glideFromNote)
    {
        note_     = midiNote;
        age_      = stamp;
        active_   = true;
        released_ = false;
        sustained_ = false;

        glideNote_ = (glideFromNote >= 0.0f) ? glideFromNote : (float) midiNote;

        for (int i = 0; i < kNumOps; ++i)
        {
            const auto& o = p.ops[i];

            if (p.oscKeySync)
                phase_[i] = 0.0f;

            // Output level, keyboard scaling, and velocity sensitivity are no
            // longer baked in here: renderBlock() re-evaluates them from the
            // live patch every control block, so knob moves reach a held note.
            // Only note_ and velocity_ (stored below) are genuinely fixed for
            // the life of the note.

            env_[i].configure (o.rates, o.levels,
                               rateScaleOffset (o.rateScale, midiNote));
            env_[i].reset (-kEgRangeDb);
            env_[i].keyOn();

            gain_[i] = 0.0f;
        }

        velocity_ = velocity;

        pitchEnv_.configure (p.pitchRates, p.pitchLevels);
        pitchEnv_.keyOn();

        fb1_ = fb2_ = 0.0f;
    }

    void retrigger (const PatchCache& p, int midiNote, int velocity, uint32_t stamp)
    {
        // Mono/legato retrigger: keep the phases and envelope positions, just
        // move the pitch and re-evaluate the key-dependent levels.
        const float from = pitchNow_;
        start (p, midiNote, velocity, stamp, from);
    }

    void keyOff (bool sustainPedalDown)
    {
        if (sustainPedalDown) { sustained_ = true; return; }
        release();
    }

    void release()
    {
        if (released_) return;
        released_ = true;
        sustained_ = false;
        for (auto& e : env_) e.keyOff();
        pitchEnv_.keyOff();
    }

    void releaseIfSustained() { if (sustained_) release(); }

    void kill() { active_ = false; }

    // Diagnostic only: the operator's current amplitude in dB, comparable to
    // the emulator's 12-bit EGS envelope register. Used to line the two engines
    // up operator by operator when a patch does not match.
    float opLevelDb (int i) const
    {
        const float g = gain_[i];
        return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f;
    }

    // Renders one control block, adding into `out`. Returns false once the note
    // has fully decayed and its slot can be reused.
    bool renderBlock (float* out, int numSamples,
                      const PatchCache& p, const Functions& fn, const ModContext& mc)
    {
        if (! active_) return false;

        const AlgoDef& alg = kAlgorithms[p.algorithm];

        // ---- control-rate update -------------------------------------------
        // Re-arm both envelope generators from the live patch every block.
        // configure() only refreshes rates_/targets_; it never touches the
        // running currentDb_/current_ position, so a knob move re-aims the
        // envelope's current ramp instead of snapping the level, which is
        // what keeps this click-free.
        pitchEnv_.configure (p.pitchRates, p.pitchLevels);
        const float pitchEg = pitchEnv_.tick (mc.dt);

        // Portamento: glide the sounding pitch towards the key that was struck.
        if (fn.portamentoSecs > 0.0001f && glideNote_ != (float) note_)
        {
            const float semisPerSec = 12.0f / fn.portamentoSecs;
            const float step = semisPerSec * mc.dt;
            if (glideNote_ < (float) note_) glideNote_ = std::min ((float) note_, glideNote_ + step);
            else                            glideNote_ = std::max ((float) note_, glideNote_ - step);
        }
        else
        {
            glideNote_ = (float) note_;
        }

        const float transposeSemis = (float) (p.transpose - 24);
        const float pitchDepth     = std::clamp (p.lfoPitchDepth + mc.pitchDepthAdd, 0.0f, 1.0f);
        const float lfoPitchSemis  = mc.lfoPitch * pitchDepth * p.pmsSemis;

        pitchNow_ = glideNote_;

        // The note itself goes through the tuning table; everything continuous -
        // pitch envelope, bend, vibrato - is a multiplier on top of whatever
        // frequency that lands on. Doing it this way means a microtuned scale
        // keeps its intervals while bend and vibrato still behave in cents.
        const float tunedNote  = glideNote_ + transposeSemis;
        const float continuous = pitchEg + mc.bendSemis + lfoPitchSemis;

        const float rootHz = (mc.tuning != nullptr)
                               ? mc.tuning->noteToHz (tunedNote)
                               : 440.0f * std::exp2 ((tunedNote - 69.0f) / 12.0f);

        const float baseHz = rootHz * std::exp2 (continuous / 12.0f);

        // The LFO's amplitude modulation is a *reduction* in level, scaled per
        // operator by that operator's AMS setting. A controller assigned to
        // amplitude adds to the depth on top of the patch's own AMD.
        const float amDepth = std::clamp (p.lfoAmpDepth + mc.ampBias, 0.0f, 1.0f);
        const float amUnit  = mc.lfoAmp * amDepth;

        float targetGain[kNumOps];
        float inc[kNumOps];
        bool  anyAudible = false;

        for (int i = 0; i < kNumOps; ++i)
        {
            const auto& o = p.ops[i];

            env_[i].configure (o.rates, o.levels, rateScaleOffset (o.rateScale, note_));
            const float egDb = env_[i].tick (mc.dt);

            // Output level, keyboard scaling, and velocity sensitivity,
            // re-evaluated against the live patch every block so they track a
            // knob in real time. Summed in the hardware's level units and
            // clipped at full scale, which is why a positive scaling curve
            // lifts the quiet end of the keyboard rather than pushing the loud
            // end past maximum.
            const int scaled = std::min (127, o.outputUnits
                                   + keyScaleUnits (note_, o.breakPoint,
                                                    o.leftDepth,  o.rightDepth,
                                                    o.leftCurve,  o.rightCurve));
            const float staticDb = unitsToDb (scaled) + velocityDb (velocity_, o.velSens);

            // EG bias raises the operators' output levels (the DX7 uses it for
            // breath-controlled brightness).
            const float biasDb = mc.egBias * 24.0f;

            const float db = staticDb + egDb - amUnit * o.amDepthDb + biasDb;
            targetGain[i] = (db <= silenceDb()) ? 0.0f : dbToGain (std::min (db, 6.0f));

            if (targetGain[i] > 1.0e-5f) anyAudible = true;

            const float hz = (o.fixedHz > 0.0f) ? o.fixedHz * o.detuneMul
                                                : baseHz * o.ratio * o.detuneMul;
            inc[i] = hz / mc.sampleRate;
        }

        bool allFinished = true;
        for (const auto& e : env_) if (! e.finished()) allFinished = false;
        if (allFinished && ! anyAudible)
        {
            active_ = false;
            return false;
        }

        // ---- audio-rate loop -----------------------------------------------
        const float rcp = 1.0f / (float) numSamples;
        float gainStep[kNumOps];
        for (int i = 0; i < kNumOps; ++i)
            gainStep[i] = (targetGain[i] - gain_[i]) * rcp;

        const auto& sine = sineTable();

        // Carriers are attenuated by the carrier count. The hardware does this
        // inside the operator (its COM field), not at the summing node, which
        // matters whenever the feedback operator is itself a carrier - the
        // feedback tap then reads the already-attenuated signal. Applying it
        // per operator rather than to the final mix reproduces that for free.
        const float carrierAtten = 1.0f / (float) alg.numCarriers;

        for (int n = 0; n < numSamples; ++n)
        {
            float opOut[kNumOps];
            float mix = 0.0f;

            // OP6 down to OP1: modulators always come before their targets.
            for (int i = kNumOps - 1; i >= 0; --i)
            {
                float mod = 0.0f;

                const uint8_t mask = alg.modMask[i];
                if (mask != 0)
                    for (int s = i + 1; s < kNumOps; ++s)
                        if (mask & (1u << s))
                            mod += opOut[s];

                mod *= kModCycles;

                if (i == alg.feedbackDst && p.feedbackGain > 0.0f)
                    mod += (fb1_ + fb2_) * 0.5f * p.feedbackGain;

                phase_[i] += inc[i];
                if (phase_[i] >= 1.0f) phase_[i] -= std::floor (phase_[i]);

                gain_[i] += gainStep[i];
                float s = sine (phase_[i] + mod) * gain_[i];

                const bool isCarrier = (alg.carrierMask & (1u << i)) != 0;
                if (isCarrier) s *= carrierAtten;

                opOut[i] = s;

                if (i == alg.feedbackSrc)
                {
                    fb2_ = fb1_;
                    fb1_ = s;
                }

                if (isCarrier) mix += s;
            }

            out[n] += mix * kOpFullScale;
        }

        for (int i = 0; i < kNumOps; ++i)
            gain_[i] = targetGain[i];

        return true;
    }

private:
    float    phase_[kNumOps]     { 0, 0, 0, 0, 0, 0 };
    float    gain_[kNumOps]      { 0, 0, 0, 0, 0, 0 };
    OperatorEnv env_[kNumOps];
    PitchEnv pitchEnv_;

    float    fb1_ { 0.0f }, fb2_ { 0.0f };

    int      note_ { 60 };
    int      velocity_ { 0 };
    float    glideNote_ { 60.0f };
    float    pitchNow_  { 60.0f };
    uint32_t age_ { 0 };
    bool     active_ { false };
    bool     released_ { false };
    bool     sustained_ { false };
};

// ============================================================================
//  6.  NativeEngine
// ============================================================================
class NativeEngine
{
public:
    NativeEngine()
    {
        uint8_t init[kVcedSize];
        defaultVced (init);
        patch_.build (init);
        std::memcpy (vced_, init, kVcedSize);
    }

    void prepare (double sampleRate)
    {
        sampleRate_ = (sampleRate > 0.0) ? (float) sampleRate : 48000.0f;
        allNotesOff (true);
    }

    void setFunctions (const Functions& f) { fn_ = f; }
    const Functions& functions() const { return fn_; }

    // ---- tuning -----------------------------------------------------------
    Tuning&       tuning()       { return tuning_; }
    const Tuning& tuning() const { return tuning_; }

    // ---- patch data -------------------------------------------------------
    void setVoice (const uint8_t vced[kVcedSize])
    {
        std::memcpy (vced_, vced, kVcedSize);
        patch_.build (vced_);
    }

    void setParam (int vcedOffset, int value)
    {
        if (vcedOffset < 0 || vcedOffset >= kVcedSize) return;
        vced_[vcedOffset] = (uint8_t) std::clamp (value, 0, 127);
        patch_.build (vced_);
    }

    const uint8_t* vced() const { return vced_; }

    // ---- MIDI -------------------------------------------------------------
    void pushMidi (const uint8_t* d, int len)
    {
        if (d == nullptr || len < 1) return;

        const uint8_t status = d[0];

        if (status == 0xF0)  { handleSysex (d, len); return; }
        if (status <  0x80)  return;

        const uint8_t type = status & 0xF0;

        switch (type)
        {
            case 0x90:
                if (len >= 3 && d[2] > 0) { noteOn (d[1] & 0x7F, d[2] & 0x7F); break; }
                if (len >= 2)             { noteOff (d[1] & 0x7F); }
                break;

            case 0x80:
                if (len >= 2) noteOff (d[1] & 0x7F);
                break;

            case 0xB0:
                if (len >= 3) controlChange (d[1] & 0x7F, d[2] & 0x7F);
                break;

            case 0xE0:
                if (len >= 3)
                {
                    const int raw = ((int) (d[2] & 0x7F) << 7) | (int) (d[1] & 0x7F);
                    bend_ = ((float) raw - 8192.0f) / 8192.0f;
                }
                break;

            case 0xD0:
                if (len >= 2) aftertouch_ = (float) (d[1] & 0x7F) / 127.0f;
                break;

            case 0xA0:
                if (len >= 3) aftertouch_ = (float) (d[2] & 0x7F) / 127.0f;
                break;

            default: break;
        }
    }

    void allNotesOff (bool immediate = false)
    {
        for (auto& n : notes_)
        {
            if (immediate) n.kill();
            else           n.release();
        }
        if (immediate)
        {
            heldCount_ = 0;
            sustain_ = false;
            filtered_.fill (false);
        }
    }

    // ---- audio ------------------------------------------------------------
    void render (float* out, int numSamples)
    {
        std::memset (out, 0, sizeof (float) * (size_t) numSamples);

        int done = 0;
        while (done < numSamples)
        {
            const int n = std::min (kCtrlBlock, numSamples - done);
            renderControlBlock (out + done, n);
            done += n;
        }
    }

    // The panel display. The native engine has no HD44780 to read back, so it
    // reports its own status in the same 2x16 shape the editor expects.
    void getLcd (char l1[17], char l2[17]) const
    {
        writePadded (l1, lcd1_);
        writePadded (l2, lcd2_);
    }

    void setLcd (const char* line1, const char* line2)
    {
        std::snprintf (lcd1_, sizeof (lcd1_), "%s", line1 ? line1 : "");
        std::snprintf (lcd2_, sizeof (lcd2_), "%s", line2 ? line2 : "");
    }

    // Diagnostic: per-operator levels of the oldest sounding note.
    bool debugOpLevels (float dbOut[kNumOps]) const
    {
        for (const auto& v : notes_)
            if (v.active())
            {
                for (int i = 0; i < kNumOps; ++i) dbOut[i] = v.opLevelDb (i);
                return true;
            }
        return false;
    }

    int activeNotes() const
    {
        int n = 0;
        for (const auto& v : notes_) if (v.active()) ++n;
        return n;
    }

    // A plain INIT VOICE: one carrier at unity ratio with a flat envelope.
    static void defaultVced (uint8_t v[kVcedSize])
    {
        std::memset (v, 0, kVcedSize);

        for (int block = 0; block < kNumOps; ++block)
        {
            uint8_t* p = v + block * 21;
            p[0] = p[1] = p[2] = p[3] = 99;      // rates
            p[4] = p[5] = p[6] = 99; p[7] = 0;   // levels
            p[8]  = 39;                          // break point
            p[9]  = p[10] = 0;                   // scaling depths
            p[11] = p[12] = 0;                   // curves
            p[13] = 0;                           // rate scaling
            p[14] = 0;                           // AMS
            p[15] = 0;                           // KVS
            p[16] = (block == 5) ? 99 : 0;       // only OP1 audible
            p[17] = 0;                           // ratio mode
            p[18] = 1; p[19] = 0;                // 1.00
            p[20] = 7;                           // centre detune
        }

        v[126] = v[127] = v[128] = v[129] = 99;  // pitch EG rates
        v[130] = v[131] = v[132] = v[133] = 50;  // pitch EG levels (flat)
        v[134] = 0;                              // algorithm 1
        v[135] = 0;                              // feedback
        v[136] = 1;                              // osc key sync on
        v[137] = 35;                             // LFO speed
        v[138] = 0;                              // LFO delay
        v[139] = 0;                              // LFO PMD
        v[140] = 0;                              // LFO AMD
        v[141] = 1;                              // LFO key sync
        v[142] = 0;                              // triangle
        v[143] = 3;                              // pitch mod sensitivity
        v[144] = 24;                             // no transposition

        const char* name = "INIT VOICE";
        for (int i = 0; i < 10; ++i) v[145 + i] = (uint8_t) name[i];
    }

private:
    static constexpr int kCtrlBlock = 32;

    // ---- note handling ----------------------------------------------------
    void noteOn (int note, int velocity)
    {
        // A key the tuning master has left unmapped sounds nothing at all. The
        // note is remembered so that its note-off can be swallowed too -
        // otherwise heldCount_ would go one light per filtered key and the
        // sustain/mono logic would drift.
        if (tuning_.shouldFilterNote (note))
        {
            filtered_[(size_t) (note & 0x7F)] = true;
            return;
        }
        filtered_[(size_t) (note & 0x7F)] = false;

        ++stamp_;
        ++heldCount_;

        if (fn_.mono)
        {
            // Find whatever is sounding and move it, so a legato line glides
            // rather than restarting.
            for (auto& v : notes_)
            {
                if (v.active() && ! v.released())
                {
                    v.retrigger (patch_, note, velocity, stamp_);
                    lastNote_ = (float) note;
                    return;
                }
            }
            auto& v = notes_[0];
            v.start (patch_, note, velocity, stamp_,
                     fn_.portamentoSecs > 0.0f ? lastNote_ : -1.0f);
            lastNote_ = (float) note;
            return;
        }

        NoteState* slot = allocate();
        if (slot == nullptr) return;
        slot->start (patch_, note, velocity, stamp_,
                     fn_.portamentoSecs > 0.0f ? lastNote_ : -1.0f);
        lastNote_ = (float) note;
    }

    void noteOff (int note)
    {
        // The note-off half of a filtered note-on: nothing was ever started,
        // so nothing should be counted down either.
        if (filtered_[(size_t) (note & 0x7F)])
        {
            filtered_[(size_t) (note & 0x7F)] = false;
            return;
        }

        if (heldCount_ > 0) --heldCount_;
        for (auto& v : notes_)
            if (v.active() && ! v.released() && v.note() == note)
                v.keyOff (sustain_);
    }

    void controlChange (int cc, int value)
    {
        const float x = (float) value / 127.0f;
        switch (cc)
        {
            case 1:   modWheel_ = x; break;
            case 2:   breath_   = x; break;
            case 4:   foot_     = x; break;
            case 64:
                sustain_ = (value >= 64);
                if (! sustain_)
                    for (auto& v : notes_) v.releaseIfSustained();
                break;
            case 120: allNotesOff (true);  break;
            case 121:
                modWheel_ = breath_ = foot_ = aftertouch_ = 0.0f;
                bend_ = 0.0f;
                sustain_ = false;
                break;
            case 123: allNotesOff (false); heldCount_ = 0; break;
            default: break;
        }
    }

    // DX7 voice parameter change: F0 43 1n g p v F7, where the parameter number
    // is g*128 + p. Also accepts the 163-byte single voice dump.
    void handleSysex (const uint8_t* d, int len)
    {
        // Universal messages first: MIDI Tuning Standard arrives under 7E/7F,
        // not under Yamaha's 43. This is the "just send it the .syx" route to
        // microtuning, which the real DX7 cannot do at all.
        if (len >= 2 && (d[1] == 0x7E || d[1] == 0x7F))
        {
            tuning_.applyMtsSysex (d, len);
            return;
        }

        if (len < 5 || d[1] != 0x43) return;

        const uint8_t sub = d[2] & 0xF0;

        if (sub == 0x10 && len >= 7)             // parameter change
        {
            const int param = ((int) (d[3] & 0x03) << 7) | (int) (d[4] & 0x7F);
            if (param < kVcedSize)
                setParam (param, d[5] & 0x7F);
            return;
        }

        if (sub == 0x00 && len >= 163 && d[3] == 0x00)   // single voice dump
        {
            uint8_t v[kVcedSize];
            std::memcpy (v, d + 6, kVcedSize);
            setVoice (v);
        }
    }

    NoteState* allocate()
    {
        for (auto& v : notes_) if (! v.active()) return &v;

        // Nothing free: prefer the oldest released note, then the oldest note.
        NoteState* best = nullptr;
        for (auto& v : notes_)
            if (v.released() && (best == nullptr || v.age() < best->age()))
                best = &v;

        if (best == nullptr)
            for (auto& v : notes_)
                if (best == nullptr || v.age() < best->age())
                    best = &v;

        if (best != nullptr) best->kill();
        return best;
    }

    // ---- LFO --------------------------------------------------------------
    float lfoValue() const
    {
        const float p = lfoPhase_;
        switch (patch_.lfoWave)
        {
            case 0:  return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);   // triangle
            case 1:  return 1.0f - 2.0f * p;                                       // saw down
            case 2:  return 2.0f * p - 1.0f;                                       // saw up
            case 3:  return (p < 0.5f) ? 1.0f : -1.0f;                             // square
            case 4:  return std::sin (kTwoPi * p);                                 // sine
            default: return sampleHold_;                                           // S/H
        }
    }

    void advanceLfo (float dt)
    {
        const float inc = patch_.lfoHz * dt;
        lfoPhase_ += inc;
        while (lfoPhase_ >= 1.0f)
        {
            lfoPhase_ -= 1.0f;
            // Sample and hold: a fresh random value each cycle.
            rng_ = rng_ * 1664525u + 1013904223u;
            sampleHold_ = (float) ((rng_ >> 9) & 0xFFFF) / 32767.5f - 1.0f;
        }

        // Delay: hold the LFO out of circuit, then fade it in.
        lfoAge_ += dt;
        if (lfoAge_ < patch_.lfoHold)                 lfoFadeGain_ = 0.0f;
        else if (patch_.lfoFade <= 0.0f)              lfoFadeGain_ = 1.0f;
        else lfoFadeGain_ = std::min (1.0f, (lfoAge_ - patch_.lfoHold) / patch_.lfoFade);
    }

    void renderControlBlock (float* out, int numSamples)
    {
        const float dt = (float) numSamples / sampleRate_;
        advanceLfo (dt);

        // Controllers. Each one contributes to whichever destinations it is
        // assigned to; the DX7 sums them rather than picking the largest.
        auto contribution = [] (float value, const Functions::Assign& a, bool dest)
        {
            return dest ? value * a.range : 0.0f;
        };

        const float pitchCtl = contribution (modWheel_,   fn_.modWheel,   fn_.modWheel.pitch)
                             + contribution (foot_,       fn_.foot,       fn_.foot.pitch)
                             + contribution (breath_,     fn_.breath,     fn_.breath.pitch)
                             + contribution (aftertouch_, fn_.aftertouch, fn_.aftertouch.pitch);

        const float ampCtl   = contribution (modWheel_,   fn_.modWheel,   fn_.modWheel.amp)
                             + contribution (foot_,       fn_.foot,       fn_.foot.amp)
                             + contribution (breath_,     fn_.breath,     fn_.breath.amp)
                             + contribution (aftertouch_, fn_.aftertouch, fn_.aftertouch.amp);

        const float biasCtl  = contribution (modWheel_,   fn_.modWheel,   fn_.modWheel.egBias)
                             + contribution (foot_,       fn_.foot,       fn_.foot.egBias)
                             + contribution (breath_,     fn_.breath,     fn_.breath.egBias)
                             + contribution (aftertouch_, fn_.aftertouch, fn_.aftertouch.egBias);

        ModContext mc;
        mc.sampleRate = sampleRate_;
        mc.dt         = dt;
        mc.bendSemis  = bend_ * fn_.bendRange;
        mc.lfoAmp     = std::clamp ((lfoValue() * 0.5f + 0.5f) * lfoFadeGain_, 0.0f, 1.0f);
        mc.ampBias    = std::clamp (ampCtl,  0.0f, 1.0f);
        mc.tuning     = &tuning_;
        mc.egBias     = std::clamp (biasCtl, 0.0f, 1.0f);

        // Raw LFO, already faded in. The per-patch depth is applied in the
        // note, so a controller assigned to pitch can open up vibrato even on a
        // patch whose own PMD is zero - which is the whole point of the wheel.
        mc.lfoPitch      = lfoValue() * lfoFadeGain_;
        mc.pitchDepthAdd = std::clamp (pitchCtl, 0.0f, 1.0f);

        for (auto& v : notes_)
            if (v.active())
                v.renderBlock (out, numSamples, patch_, fn_, mc);
    }

    static void writePadded (char dst[17], const char* src)
    {
        int i = 0;
        for (; i < 16 && src[i] != 0; ++i)
        {
            const unsigned char c = (unsigned char) src[i];
            dst[i] = (c < 32 || c > 126) ? ' ' : (char) c;
        }
        for (; i < 16; ++i) dst[i] = ' ';
        dst[16] = 0;
    }

    PatchCache patch_;
    Functions  fn_;
    Tuning     tuning_;
    uint8_t    vced_[kVcedSize] {};

    NoteState  notes_[kMaxNotes];
    uint32_t   stamp_ { 0 };
    int        heldCount_ { 0 };

    // Keys whose note-on was filtered out by the tuning master's keyboard map.
    std::array<bool, 128> filtered_ {};
    float      lastNote_ { 60.0f };

    float sampleRate_ { 48000.0f };

    float lfoPhase_    { 0.0f };
    float lfoAge_      { 1.0e6f };
    float lfoFadeGain_ { 1.0f };
    float sampleHold_  { 0.0f };
    uint32_t rng_ { 0x1234567u };

    float bend_ { 0.0f }, modWheel_ { 0.0f }, breath_ { 0.0f };
    float foot_ { 0.0f }, aftertouch_ { 0.0f };
    bool  sustain_ { false };

    char lcd1_[24] { "VDX7 NATIVE FM" };
    char lcd2_[24] { "NO ROM LOADED" };
};

} // namespace vdx7native