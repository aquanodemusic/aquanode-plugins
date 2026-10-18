#include "ChordWavetableModule.h"

using namespace aquanode;

//==============================================================================
// The chord-wavetable generator.
//
// A generic engine built on the harmonic series, no sampled or copied
// material of any kind: every frame is a SPARSE set of harmonics, chosen
// because their pitch class lands close to a tone of a chord built on the
// played note. Plain music theory decides which harmonics qualify - for a
// minor 9 chord that is 1 2 3 4 6 7 8 9 12 14 16 18 19 24 ..., for a major 9
// chord 1 3 4 5 6 9 12 15 18 20 24 ... - with a tolerance of a third of a
// semitone, so the 7th harmonic (31 cents flat of the minor 7th) counts.
// Everything else is random within round, generic ranges:
//   - nearly every qualifying harmonic up to a random top harmonic, then a
//     sparse tail; level falls 3 to 12 dB per octave (Bright)
//   - the lowest harmonic is usually the fundamental, sometimes the octave,
//     fifth or twelfth above it
//   - keyframes keep the chord but change which tones are loud (voicing)
//   - phases are either all aligned (a peaky, plucky cycle) or scattered
//==============================================================================
namespace cbgen
{
namespace
{
    constexpr int kSynth = 2048;

    struct ChordDef { const char* name; int tones[6]; int numTones; };

    // index = chordChoices() index - 1
    const ChordDef kChords[] = {
        { "Minor 9",  { 0, 3, 7, 10, 2 },  5 },
        { "Minor 7",  { 0, 3, 7, 10 },     4 },
        { "Minor",    { 0, 3, 7 },         3 },
        { "Major 9",  { 0, 4, 7, 11, 2 },  5 },
        { "Major 7",  { 0, 4, 7, 11 },     4 },
        { "Sus 2",    { 0, 2, 7 },         3 },
        { "Sus 4",    { 0, 5, 7, 10 },     4 },
        { "Power",    { 0, 7 },            2 },
        { "Saw",      { 0 },               0 },   // every harmonic: the plain saw layer
        { "Square",   { 0 },               0 },   // odd harmonics
    };
    constexpr int kNumChords = (int) (sizeof (kChords) / sizeof (kChords[0]));
    constexpr int kSawChord = 8, kSquareChord = 9;

    void spectrumToCycle (const std::vector<float>& amp, const std::vector<float>& phase,
                          float* out, int cycleSamples)
    {
        static thread_local juce::dsp::FFT fft (11);    // 2048
        static thread_local std::vector<float> buf;
        buf.assign ((size_t) (2 * kSynth), 0.0f);

        const int maxH = juce::jmin ((int) amp.size() - 1, juce::jmin (kSynth / 2 - 1, juce::jmax (1, cycleSamples / 2 - 1)));
        for (int h = 1; h <= maxH; ++h)
        {
            const float a = amp[(size_t) h];
            if (a <= 0.0f) continue;
            const float p = phase[(size_t) h];
            const float re = a * std::cos (p);
            const float im = -a * std::sin (p);
            buf[(size_t) (2 * h)] = re;
            buf[(size_t) (2 * h) + 1] = im;
            const int mirror = kSynth - h;
            buf[(size_t) (2 * mirror)] = re;
            buf[(size_t) (2 * mirror) + 1] = -im;
        }
        fft.performRealOnlyInverseTransform (buf.data());

        if (cycleSamples == kSynth)
        {
            std::copy (buf.begin(), buf.begin() + kSynth, out);
            return;
        }
        // safe to decimate by interpolation: content is already below cycleSamples/2
        for (int i = 0; i < cycleSamples; ++i)
        {
            const double src = (double) i * kSynth / (double) cycleSamples;
            const int i0 = (int) src;
            const int i1 = (i0 + 1) % kSynth;
            const float f = (float) (src - i0);
            out[i] = buf[(size_t) i0] * (1.0f - f) + buf[(size_t) i1] * f;
        }
    }

    float gauss (juce::Random& r)
    {
        const float u1 = juce::jmax (1.0e-7f, r.nextFloat());
        const float u2 = r.nextFloat();
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (juce::MathConstants<float>::twoPi * u2);
    }

    float smooth01 (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); }

    // distance in semitones from pitch class pc (0..12) to the nearest chord tone
    float chordDistance (float pc, const ChordDef& c)
    {
        float best = 12.0f;
        for (int i = 0; i < c.numTones; ++i)
        {
            float d = std::abs (pc - (float) c.tones[i]);
            d = juce::jmin (d, 12.0f - d);
            best = juce::jmin (best, d);
        }
        return best;
    }

    int nearestTone (float pc, const ChordDef& c)
    {
        int best = 0; float bd = 99.0f;
        for (int i = 0; i < c.numTones; ++i)
        {
            float d = std::abs (pc - (float) c.tones[i]);
            d = juce::jmin (d, 12.0f - d);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }

    int pickWeighted (juce::Random& r, const float* w, int n)
    {
        float total = 0.0f;
        for (int i = 0; i < n; ++i) total += w[i];
        float x = r.nextFloat() * total;
        for (int i = 0; i < n; ++i) { x -= w[i]; if (x <= 0.0f) return i; }
        return n - 1;
    }
}

    int numChords() { return kNumChords; }
    const char* chordName (int i) { return (i >= 0 && i < kNumChords) ? kChords[i].name : "Auto"; }

    // One keyframe spectrum (linear amplitudes, index = harmonic number).
    std::vector<float> makeKeyframe (juce::Random& rng, int chordIdx, int maxH, float slopeDb, float density)
    {
        std::vector<float> amp ((size_t) maxH + 1, 0.0f);
        const auto& C = kChords[chordIdx];

        if (chordIdx == kSawChord || chordIdx == kSquareChord)
        {
            // a plain saw / square layer for unison stacks, with a little tilt
            // variation so two rolls are not identical
            const float tiltExp = juce::jmap (rng.nextFloat(), 0.9f, 1.15f);
            for (int h = 1; h <= maxH; ++h)
            {
                if (chordIdx == kSquareChord && (h & 1) == 0) continue;
                amp[(size_t) h] = std::pow ((float) h, -tiltExp);
            }
            return amp;
        }

        // lowest harmonic: usually the fundamental, sometimes an octave or a
        // fifth above it (tables that start on harmonic 3 or 6)
        static const int lows[]  = { 1, 2, 3, 4, 6 };
        static const float lowW[] = { 0.6f, 0.1f, 0.1f, 0.1f, 0.1f };
        int low = lows[pickWeighted (rng, lowW, 5)];

        // per-keyframe voicing: how loud each chord tone is
        float toneDb[6];
        for (int i = 0; i < 6; ++i)
            toneDb[i] = juce::jmap (rng.nextFloat(), -5.0f, 3.0f);
        toneDb[0] = juce::jmax (toneDb[0], 0.0f);          // the root stays strong

        // the top of the near-complete region: 16..64 harmonics with Bright
        const float topH = (float) low * juce::jmap (rng.nextFloat(), 0.7f, 1.3f)
                         * juce::jmap ((float) maxH, 48.0f, 200.0f, 16.0f, 64.0f) / (float) juce::jmax (1, low);
        const float jitterDb = 2.0f;
        for (int h = low; h <= maxH; ++h)
        {
            const float st = 12.0f * std::log2 ((float) h);
            const float pc = std::fmod (st, 12.0f);
            const float dist = chordDistance (pc, C);
            const float tol = (h <= 40) ? 0.33f : 0.21f;    // harmonic 7 is 31 cents flat
            if (dist > tol)
                continue;

            if (h != low)
            {
                // nearly every chord tone up to a top harmonic, then a sparse tail
                const float keep = h <= topH ? 0.55f + 0.45f * density
                                             : density * 0.35f * std::exp (-(float) (h - topH) / 60.0f);
                if (rng.nextFloat() > keep)
                    continue;
            }

            float db = -slopeDb * std::log2 ((float) h / (float) low);
            db += toneDb[nearestTone (pc, C)];
            db += gauss (rng) * jitterDb;
            db -= dist * 18.0f;                               // out-of-tune partials a bit quieter
            if (h == low) db = 0.0f;
            const bool octaveOfLow = (h % low == 0) && juce::isPowerOfTwo (h / low);
            if (octaveOfLow && h != low) db += 2.0f;
            amp[(size_t) h] = std::pow (10.0f, juce::jmin (db, 0.0f) / 20.0f);
        }
        return amp;
    }

    std::shared_ptr<ChordWavetableModule::Table> generate (juce::uint32 seed, int chordSetting, int keysSetting,
                                                         int morphType, int brightSetting, int densitySetting)
    {
        auto t = std::make_shared<ChordWavetableModule::Table>();
        t->seed = seed;
        t->chordSetting = chordSetting;
        t->keysSetting = keysSetting;
        t->morphType = morphType;
        t->brightSetting = brightSetting;
        t->densitySetting = densitySetting;

        juce::Random rng ((juce::int64) ((seed * 2654435761u) ^ (seed >> 15) ^ 0xC0B5u));

        // Auto chord: any of the chords, equally likely (never the plain saw/square)
        int chordIdx = chordSetting - 1;
        if (chordIdx < 0 || chordIdx >= kNumChords)
        {
            static const float w[kNumChords] = { 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 };
            chordIdx = pickWeighted (rng, w, kNumChords);
        }
        t->chord = chordIdx;

        static const int keyCounts[] = { 0, 1, 2, 3, 4, 6 };
        int keys = keyCounts[juce::jlimit (0, 5, keysSetting)];
        if (keys <= 0)
        {
            static const float kw[] = { 1, 1, 1, 1 };
            static const int kv[] = { 1, 2, 3, 4 };
            keys = kv[pickWeighted (rng, kw, 4)];
        }
        if (chordIdx == kSawChord || chordIdx == kSquareChord)
            keys = 1;
        t->keys = keys;

        const float bright = juce::jlimit (0.0f, 1.0f, (float) brightSetting / 100.0f);
        const float density = juce::jlimit (0.05f, 1.0f, (float) densitySetting / 100.0f);
        const float slopeDb = juce::jmap (bright, 12.0f, 3.0f);
        const int maxH = juce::roundToInt (juce::jmap (bright, 48.0f, 200.0f));
        constexpr int H = kSynth / 2 - 1;

        std::vector<std::vector<float>> keySpec ((size_t) keys);
        for (int k = 0; k < keys; ++k)
        {
            keySpec[(size_t) k] = makeKeyframe (rng, chordIdx, maxH, slopeDb, density);
            keySpec[(size_t) k].resize ((size_t) H + 1, 0.0f);
        }

        // phases: all aligned (peaky, plucky cycle) or scattered once per table
        std::vector<float> basePhase ((size_t) H + 1, 0.0f);
        const bool aligned = rng.nextFloat() < 0.6f;
        const float common = rng.nextBool() ? juce::MathConstants<float>::pi : 0.0f;
        for (int h = 1; h <= H; ++h)
            basePhase[(size_t) h] = aligned ? common : rng.nextFloat() * juce::MathConstants<float>::twoPi;

        // random-amplitude anchors: 4 per harmonic across the morph axis
        std::vector<std::array<float, 4>> randAnchor ((size_t) H + 1);
        {
            juce::Random rr ((juce::int64) seed ^ 0x0C0B0A55);
            for (auto& a : randAnchor)
                for (auto& x : a) x = rr.nextFloat();
        }

        const int numFrames = keys == 1 ? 1 : 16;
        const int numMorphs = morphType == ChordWavetableModule::mOff ? 1 : 9;
        t->numFrames = numFrames;
        t->numMorphs = numMorphs;
        t->data.assign ((size_t) numFrames * (size_t) numMorphs * ChordWavetableModule::kMipStride, 0.0f);

        int harmonicsInFirst = 0;
        for (int h = 1; h <= H; ++h)
            if (keySpec[0][(size_t) h] > 1.0e-4f) ++harmonicsInFirst;
        t->numHarmonics = harmonicsInFirst;

        std::vector<float> amp ((size_t) H + 1), mamp ((size_t) H + 1), mph ((size_t) H + 1);
        std::vector<float> full ((size_t) kSynth);

        for (int f = 0; f < numFrames; ++f)
        {
            // frames interpolate linearly between keyframes (linear amplitude,
            // the way a drawn table crossfades), eased with a smoothstep
            const float pos = numFrames == 1 ? 0.0f : (float) f / (float) (numFrames - 1) * (float) (keys - 1);
            const int kA = juce::jlimit (0, keys - 1, (int) pos);
            const int kB = juce::jmin (keys - 1, kA + 1);
            const float mix = smooth01 (pos - (float) kA);
            for (int h = 1; h <= H; ++h)
                amp[(size_t) h] = keySpec[(size_t) kA][(size_t) h] * (1.0f - mix) + keySpec[(size_t) kB][(size_t) h] * mix;

            float frameGain = 1.0f;
            for (int m = 0; m < numMorphs; ++m)
            {
                const float tm = numMorphs == 1 ? 0.0f : (float) m / (float) (numMorphs - 1);
                for (int h = 1; h <= H; ++h)
                {
                    float a = amp[(size_t) h];
                    float p = basePhase[(size_t) h];
                    switch (morphType)
                    {
                        case ChordWavetableModule::mRandomAmp:
                        {
                            // the randomisation fades in over the first half,
                            // then the random values keep drifting
                            const auto& an = randAnchor[(size_t) h];
                            const float x = tm * 3.0f;
                            const int i0 = juce::jlimit (0, 2, (int) x);
                            const float r = an[(size_t) i0] + (an[(size_t) i0 + 1] - an[(size_t) i0]) * smooth01 (x - (float) i0);
                            const float depth = juce::jmin (1.0f, tm * 2.0f);
                            a *= (1.0f - depth) + depth * (2.2f * r * r);
                            break;
                        }
                        case ChordWavetableModule::mLowPass:
                        {
                            const float cut = std::pow (2.0f, (1.0f - tm) * 8.5f);   // 362 .. 1
                            const float x = (float) h / juce::jmax (1.0f, cut);
                            a *= 1.0f / (1.0f + x * x * x * x);
                            break;
                        }
                        case ChordWavetableModule::mHighPass:
                        {
                            const float cut = std::pow (2.0f, tm * 6.5f);             // 1 .. 90
                            const float x = (float) h / cut;
                            const float x4 = x * x * x * x;
                            a *= tm <= 0.0f ? 1.0f : x4 / (1.0f + x4);
                            break;
                        }
                        case ChordWavetableModule::mPhaseDisperse:
                            p += tm * juce::MathConstants<float>::twoPi * (float) (h * h) / 48.0f;
                            break;
                        default: break;
                    }
                    mamp[(size_t) h] = a;
                    mph[(size_t) h] = p;
                }

                spectrumToCycle (mamp, mph, full.data(), kSynth);
                if (m == 0)
                {
                    // constant-RMS normalisation, capped by peak (as Transwave)
                    float peak = 1.0e-9f, sumSq = 0.0f;
                    for (float v : full) { peak = juce::jmax (peak, std::abs (v)); sumSq += v * v; }
                    const float rms = std::sqrt (sumSq / (float) kSynth) + 1.0e-9f;
                    frameGain = juce::jmin (0.30f / rms, 0.97f / peak);
                }

                float* dst = const_cast<float*> (t->cycle (f, m, 0));
                for (int i = 0; i < kSynth; ++i) dst[i] = juce::jlimit (-1.5f, 1.5f, full[(size_t) i] * frameGain);
                for (int mip = 1; mip < ChordWavetableModule::kNumMips; ++mip)
                {
                    float* d = const_cast<float*> (t->cycle (f, m, mip));
                    spectrumToCycle (mamp, mph, d, kSynth >> mip);
                    for (int i = 0; i < (kSynth >> mip); ++i) d[i] = juce::jlimit (-1.5f, 1.5f, d[i] * frameGain);
                }
            }
        }
        return t;
    }
} // namespace cbgen

//==============================================================================
struct ChordWavetableModule::Rebuilder : public juce::AsyncUpdater
{
    explicit Rebuilder (ChordWavetableModule& m) : owner (m) {}
    void handleAsyncUpdate() override { owner.regenerateIfNeeded(); }
    ChordWavetableModule& owner;
};

ChordWavetableModule::ChordWavetableModule()
{
    rebuilder = std::make_unique<Rebuilder> (*this);
    seed = 0xC0B5u;
    // Auto chord, Auto keys, Random Amp morph, bright 55, density 60 = descriptor defaults
    published = cbgen::generate (seed, 0, 0, mRandomAmp, 55, 60);
}

ChordWavetableModule::~ChordWavetableModule()
{
    rebuilder->cancelPendingUpdate();
}

const juce::StringArray& ChordWavetableModule::chordChoices()
{
    static const juce::StringArray c = []
    {
        juce::StringArray a { "Auto" };
        for (int i = 0; i < cbgen::numChords(); ++i)
            a.add (cbgen::chordName (i));
        return a;
    }();
    return c;
}

const juce::StringArray& ChordWavetableModule::keysChoices()
{
    static const juce::StringArray c { "Auto K", "1 Key", "2 Keys", "3 Keys", "4 Keys", "6 Keys" };
    return c;
}

const juce::StringArray& ChordWavetableModule::morphChoices()
{
    static const juce::StringArray c { "Morph: Off", "Random Amp", "Low Pass", "High Pass", "Phase Disp" };
    return c;
}

const juce::StringArray& ChordWavetableModule::stackChoices()
{
    static const juce::StringArray c { "Stack: Normal", "Octave", "2x Octave", "Power", "2x Power",
                                       "Major", "Minor", "Harmonic", "Odd Harm" };
    return c;
}

std::shared_ptr<const ChordWavetableModule::Table> ChordWavetableModule::currentTable() const
{
    const juce::SpinLock::ScopedLockType sl (tableLock);
    return published;
}

void ChordWavetableModule::regenerateIfNeeded()
{
    const int chord   = (int) std::lround (getParameter ("chord"));
    const int keys    = (int) std::lround (getParameter ("keys"));
    const int morph   = (int) std::lround (getParameter ("morphType"));
    const int bright  = (int) std::lround (getParameter ("bright"));
    const int density = (int) std::lround (getParameter ("density"));

    if (auto cur = currentTable())
        if (cur->seed == seed && cur->chordSetting == chord && cur->keysSetting == keys
            && cur->morphType == morph && cur->brightSetting == bright && cur->densitySetting == density)
            return;

    std::shared_ptr<const Table> fresh = cbgen::generate (seed, chord, keys, morph, bright, density);
    std::shared_ptr<const Table> old;
    {
        const juce::SpinLock::ScopedLockType sl (tableLock);
        old = std::move (published);
        published = std::move (fresh);
    }
    retired.push_back (std::move (old));
    retired.erase (std::remove_if (retired.begin(), retired.end(),
                       [] (const std::shared_ptr<const Table>& t) { return t == nullptr || t.use_count() == 1; }),
                   retired.end());
}

void ChordWavetableModule::rollNewSeed()
{
    juce::uint32 s = 0;
    while (s == 0)
        s = (juce::uint32) juce::Random::getSystemRandom().nextInt();
    seed = s;
    regenerateIfNeeded();
}

void ChordWavetableModule::setSeed (juce::uint32 newSeed)
{
    seed = newSeed != 0 ? newSeed : 1u;
    regenerateIfNeeded();
}

void ChordWavetableModule::setParameter (const juce::String& id, float value)
{
    SynthModule::setParameter (id, value);
    if (id == "chord" || id == "keys" || id == "morphType" || id == "bright" || id == "density")
        rebuilder->triggerAsyncUpdate();
}

void ChordWavetableModule::uiButtonClicked (const juce::String& paramId)
{
    if (paramId == "roll")
        rollNewSeed();
}

juce::String ChordWavetableModule::saveCustomState() const
{
    return "CBO1 " + juce::String ((juce::int64) seed);
}

void ChordWavetableModule::loadCustomState (const juce::String& state)
{
    auto t = juce::StringArray::fromTokens (state, " ", {});
    if (t.size() >= 2 && t[0] == "CBO1")
    {
        const auto s = (juce::uint32) t[1].getLargeIntValue();
        if (s != 0)
            seed = s;
    }
    regenerateIfNeeded();
}

//==============================================================================
// playback (audio thread)
//==============================================================================
void ChordWavetableModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
        voiceReset (v);
    lastUnison = -1;
}

void ChordWavetableModule::voiceReset (int v)
{
    pool.resetVoice (v);
    glide.resetVoice (v);
    gate.resetVoice (v);
    for (int u = 0; u < kMaxUnison; ++u)
        phase[v][u] = 0.0;
}

void ChordWavetableModule::voiceNoteOn (int v, int note, bool retrigger)
{
    pool.noteOn (v, voiceLimit());
    glide.noteOn (v, (float) note, isMonoVoice());
    gate.noteOn (v);
    if (! retrigger)
    {
        // random unison phases (a wide, free-running stack), but the
        // centre voice starts at zero so a single voice still has an attack
        for (int u = 0; u < kMaxUnison; ++u)
            phase[v][u] = u == 0 ? 0.0 : (double) phaseRng.nextFloat();
    }
}

void ChordWavetableModule::voiceNoteOff (int v)
{
    pool.noteOff (v, voiceLimit());
    gate.noteOff (v);
}

void ChordWavetableModule::updateUnisonLayout()
{
    const int n = juce::jlimit (1, kMaxUnison, (int) std::lround (param (pUnison)));
    const int stack = (int) std::lround (param (pStack));
    const float detune = param (pDetune);
    const float spread = param (pSpread) * 0.01f;
    if (n == lastUnison && stack == lastStack && detune == lastDetune && spread == lastSpread)
        return;
    lastUnison = n; lastStack = stack; lastDetune = detune; lastSpread = spread;

    // stack intervals in semitones, handed out to the unison voices in turn
    static const float st_normal[]  = { 0 };
    static const float st_oct[]     = { 0, 12 };
    static const float st_oct2[]    = { 0, 12, 24 };
    static const float st_power[]   = { 0, 7 };
    static const float st_power2[]  = { 0, 7, 12 };
    static const float st_major[]   = { 0, 4, 7 };
    static const float st_minor[]   = { 0, 3, 7 };
    const float* pattern = st_normal; int len = 1;
    switch (stack)
    {
        case 1: pattern = st_oct;    len = 2; break;
        case 2: pattern = st_oct2;   len = 3; break;
        case 3: pattern = st_power;  len = 2; break;
        case 4: pattern = st_power2; len = 3; break;
        case 5: pattern = st_major;  len = 3; break;
        case 6: pattern = st_minor;  len = 3; break;
        default: break;
    }

    for (int u = 0; u < n; ++u)
    {
        float semis = 0.0f;
        if (stack == 7)      semis = 12.0f * std::log2 ((float) (u + 1));          // harmonic series
        else if (stack == 8) semis = 12.0f * std::log2 ((float) (2 * u + 1));      // odd harmonics
        else                 semis = pattern[u % len];

        // detune spread symmetric around the stack note
        const float pos = n == 1 ? 0.0f : 2.0f * (float) u / (float) (n - 1) - 1.0f;
        const float cents = detune * pos;
        unisonRatio[u] = std::pow (2.0f, (semis + cents * 0.01f) / 12.0f);

        const float pan = juce::jlimit (-1.0f, 1.0f, spread * pos * ((u & 1) ? -1.0f : 1.0f));
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        unisonPanL[u] = std::cos (ang) * juce::MathConstants<float>::sqrt2;
        unisonPanR[u] = std::sin (ang) * juce::MathConstants<float>::sqrt2;
    }
    unisonCount = n;
    unisonGain = 1.0f / std::sqrt ((float) n);
}

void ChordWavetableModule::blockStart()
{
    {
        const juce::SpinLock::ScopedLockType sl (tableLock);
        if (audioTable != published)
            audioTable = published;
    }
    updateUnisonLayout();

    if (! uiVoiceShown)
    {
        uiPosition.store (juce::jlimit (0.0f, 1.0f, param (pPosition) * 0.01f), std::memory_order_relaxed);
        uiMorph.store (juce::jlimit (0.0f, 1.0f, param (pMorph) * 0.01f), std::memory_order_relaxed);
    }
    uiVoiceShown = false;
}

void ChordWavetableModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v) || audioTable == nullptr || audioTable->numFrames <= 0)
    {
        outputs[0] = { 0.0f, 0.0f };
        return;
    }

    const float amp = gate.next (v, isInputConnected (4), inputs[4][0], sampleRate);
    float level = param (pVolume) * voiceGain * amp;
    if (isInputConnected (3))
        level *= juce::jlimit (0.0f, 1.0f, inputs[3][0]);

    const auto& T = *audioTable;

    double semis = (double) glide.next (v, glideMillis(), isMonoVoice(), ! pool.isMuted (v), sampleRate)
                 + (double) pitchBendSemitones() + (double) param (pTranspose);
    if (isInputConnected (2))
        semis += (double) (inputs[2][0] * param (pPitchDepth));
    const double baseFreq = midiNoteToHz (semis);

    const float pos01 = juce::jlimit (0.0f, 1.0f, param (pPosition) * 0.01f + inputs[0][0] * param (pPosDepth) * 0.01f);
    const float morph01 = juce::jlimit (0.0f, 1.0f, param (pMorph) * 0.01f + inputs[1][0] * param (pMorphDepth) * 0.01f);
    if (v == 0 || gate.isOn (v) || ! uiVoiceShown)
    {
        uiPosition.store (pos01, std::memory_order_relaxed);
        uiMorph.store (morph01, std::memory_order_relaxed);
        uiVoiceShown = true;
    }

    // phases advance even when silent (level 0 from a chop) so the unison
    // stays free-running and the chop does not restart the cycle
    if (level <= 0.0f)
    {
        for (int u = 0; u < unisonCount; ++u)
        {
            phase[v][u] += baseFreq * unisonRatio[u] / sampleRate;
            phase[v][u] -= std::floor (phase[v][u]);
        }
        outputs[0] = { 0.0f, 0.0f };
        return;
    }

    const float fpos = pos01 * (float) (T.numFrames - 1);
    const int f0 = (int) fpos;
    const int f1 = juce::jmin (T.numFrames - 1, f0 + 1);
    const float ff = fpos - (float) f0;

    const float mpos = morph01 * (float) (T.numMorphs - 1);
    const int m0 = (int) mpos;
    const int m1 = juce::jmin (T.numMorphs - 1, m0 + 1);
    const float mf = mpos - (float) m0;

    const double nyq = sampleRate * 0.45;
    float outL = 0.0f, outR = 0.0f;

    for (int u = 0; u < unisonCount; ++u)
    {
        const double freq = baseFreq * unisonRatio[u];
        double& ph = phase[v][u];
        ph += freq / sampleRate;
        ph -= std::floor (ph);
        if (freq >= nyq)
            continue;

        int mip = 0;
        while (mip < kNumMips - 1 && (double) ((kBaseLength >> mip) / 2 - 1) * freq > nyq)
            ++mip;

        const int len = kBaseLength >> mip;
        const double x = ph * len;
        const int i0 = (int) x & (len - 1);
        const int i1 = (i0 + 1) & (len - 1);
        const float fr = (float) (x - std::floor (x));

        auto rd = [&] (int fr_, int mo_)
        {
            const float* c = T.cycle (fr_, mo_, mip);
            return c[i0] + (c[i1] - c[i0]) * fr;
        };

        float s = rd (f0, m0);
        if (f1 != f0) s += (rd (f1, m0) - s) * ff;
        if (m1 != m0)
        {
            float s2 = rd (f0, m1);
            if (f1 != f0) s2 += (rd (f1, m1) - s2) * ff;
            s += (s2 - s) * mf;
        }

        outL += s * unisonPanL[u];
        outR += s * unisonPanR[u];
    }

    const float g = level * unisonGain;
    outputs[0] = { outL * g, outR * g };
}

//==============================================================================
// display: current frame + morph, chord, harmonic count, seed
//==============================================================================
class ChordWavetableDisplay : public juce::Component, private juce::Timer
{
public:
    explicit ChordWavetableDisplay (ChordWavetableModule& m) : module (&m) { startTimerHz (20); }

    void paint (juce::Graphics& g) override
    {
        auto* osc = dynamic_cast<ChordWavetableModule*> (module.get());
        auto box = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff141218));
        g.fillRoundedRectangle (box, 4.0f);
        if (osc == nullptr)
            return;

        auto table = osc->currentTable();
        auto area = box.reduced (6.0f, 4.0f);
        auto caption = area.removeFromTop (13.0f);
        auto strip = area.removeFromBottom (6.0f);

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        if (table == nullptr)
        {
            g.drawText ("building...", caption, juce::Justification::centredLeft, false);
            return;
        }
        g.drawText (ChordWavetableModule::chordChoices()[table->chord + 1] + "  "
                        + juce::String (table->numHarmonics) + " partials  " + juce::String (table->keys) + "k",
                    caption, juce::Justification::centredLeft, false);
        g.drawText (juce::String::toHexString ((juce::int64) table->seed).toUpperCase(),
                    caption, juce::Justification::centredRight, false);

        const float pos = osc->getUiPosition();
        const float mo = osc->getUiMorph();
        const int frame = juce::jlimit (0, table->numFrames - 1, juce::roundToInt (pos * (float) (table->numFrames - 1)));
        const int morph = juce::jlimit (0, table->numMorphs - 1, juce::roundToInt (mo * (float) (table->numMorphs - 1)));
        const float* cyc = table->cycle (frame, morph, 0);

        juce::Path p;
        const int n = juce::jmax (2, (int) area.getWidth());
        for (int i = 0; i <= n; ++i)
        {
            const float s = cyc[juce::jlimit (0, ChordWavetableModule::kBaseLength - 1, i * ChordWavetableModule::kBaseLength / n)];
            const float x = area.getX() + (float) i;
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, s) * area.getHeight() * 0.46f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (juce::Colour (0xffb98cff));
        g.strokePath (p, juce::PathStrokeType (1.3f));

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (strip, 2.0f);
        auto half = strip.withWidth (strip.getWidth() * 0.5f - 2.0f);
        g.setColour (juce::Colour (0xff00e5ff));
        g.fillRoundedRectangle (half.withWidth (juce::jmax (3.0f, half.getWidth() * pos)), 2.0f);
        auto right = strip.withTrimmedLeft (strip.getWidth() * 0.5f + 2.0f);
        g.setColour (juce::Colour (0xffff7ad9));
        g.fillRoundedRectangle (right.withWidth (juce::jmax (3.0f, right.getWidth() * mo)), 2.0f);
    }

private:
    void timerCallback() override { repaint(); }
    juce::WeakReference<SynthModule> module;
};

std::unique_ptr<juce::Component> ChordWavetableModule::createExtraContentComponent()
{
    regenerateIfNeeded();
    return std::make_unique<ChordWavetableDisplay> (*this);
}

//==============================================================================
static ModuleDescriptor chordWavetableDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.chordwavetable";
    d.displayName = "Chord Wavetable";
    d.description =
        "A wavetable oscillator that generates colour-bass chord tables: each frame is a sparse set of "
        "harmonics that land on an extended chord (Minor 9, Major 9...) above the played note. Roll for "
        "a new table. Stack spreads the unison voices over a chord, the classic glittery top layer. "
        "Morph In sweeps the spectral morph (Random Amp is the shimmer) and Level In takes a Multi "
        "LFO for rhythmic chops while the ADSR stays on Env In. Part of the Colorbass set.";
    d.section = ModuleSection::Oscillator;
    d.family = ModuleFamily::Colorbass;
    d.sidebarOrder = 1;
    d.sockets = {
        modIn    ("posIn",     "Pos In"),
        modIn    ("morphIn",   "Morph In"),
        modIn    ("pitchIn",   "Pitch In"),
        modIn    ("levelIn",   "Level In"),
        modIn    ("envIn",     "Env In"),
        midiIn   ("addMidiIn", "Add Midi In"),
        audioOut ("audioOut",  "Audio Out")
    };
    d.params = {
        makeRotary ("volume",     "Volume",    0.0f, 1.0f, 0.7f, 0),
        makeCombo  ("chord",      "Chord",     ChordWavetableModule::chordChoices(), 0, 0, 3),
        makeButton ("roll",       "Roll",      0, 1),
        makeCombo  ("keys",       "Keys",      ChordWavetableModule::keysChoices(), 0, 1, 2),
        makeCombo  ("morphType",  "Morph",     ChordWavetableModule::morphChoices(), 1, 1, 3),
        makeRotary ("bright",     "Bright",    0.0f, 100.0f, 55.0f, 2, "%", false, 1.0f).noMod(),
        makeRotary ("density",    "Density",   5.0f, 100.0f, 60.0f, 2, "%", false, 1.0f).noMod(),
        makeRotary ("position",   "Position",  0.0f, 100.0f, 0.0f, 2, "%"),
        makeRotary ("posDepth",   "Pos Mod",   -100.0f, 100.0f, 100.0f, 2, "%"),
        makeRotary ("transpose",  "Transpose", -48.0f, 48.0f, 0.0f, 2, "st", false, 1.0f),
        makeRotary ("morph",      "Morph Amt", 0.0f, 100.0f, 0.0f, 3, "%"),
        makeRotary ("morphDepth", "Morph In", -100.0f, 100.0f, 100.0f, 3, "%"),
        makeRotary ("pitchDepth", "Pitch Mod", -48.0f, 48.0f, 12.0f, 3, "st"),
        makeRotary ("unison",     "Unison",    1.0f, (float) ChordWavetableModule::kMaxUnison, 1.0f, 3, {}, false, 1.0f).noMod(),
        makeRotary ("detune",     "Detune",    0.0f, 100.0f, 12.0f, 3, "ct"),
        makeCombo  ("stack",      "Stack",     ChordWavetableModule::stackChoices(), 0, 4, 3),
        makeRotary ("spread",     "Spread",    0.0f, 100.0f, 80.0f, 4, "%"),
        makeRotary ("voices",     "Voices",    1.0f, (float) kMaxVoices, (float) kMaxVoices, 4, {}, false, 1.0f).noMod(),
        makeRotary ("glide",      "Glide",     0.0f, 1000.0f, 0.0f, 5, "ms").visibleWhen ("voices", 1.0f)
    };
    return d;
}

AQUANODE_REGISTER_MODULE (ChordWavetableModule, chordWavetableDescriptor)
