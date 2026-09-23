#include "RandomPatch.h"
#include "PluginProcessor.h"
#include <map>

namespace RandomPatch {
namespace {

    // ---- Wave indices (see InternalWaves.h) ----
    enum W {
        SINE = 0, TRI, SAW, SQUARE, NOISE, TABLE,
        SQUARE2, SQUARE3, ORGAN1, ORGAN2, ORGAN3,
        SPECTRA1, SPECTRA2, SPECTRA3, SPECTRA4,
        RICHSAW1, RICHSAW2, RICHSAW3, RICHSAW4, SAWSPEC1, SAWSPEC2,
        VINTSAW1, VINTSAW2, VINTSAW3,
        SAWBASS1, SAWBASS2, SAWBASS3, SAWBASS4, SAWBASS5, SAWBASS6, SAWBASS7, SAWBASS8
    };
    // Octave choice indices
    enum O { OCT_M2 = 0, OCT_M1, OCT_0, OCT_P1, OCT_P2 };
    // Filter types
    enum F { LP12 = 0, LP24, LP24P, BP, HP };

    // ---- Behaviour flags: each one wires up LFOs / matrix slots ----
    enum Flag : unsigned {
        FILTER_LFO   = 1u << 0,   // slow free LFO on cutoff
        WOBBLE       = 1u << 1,   // tempo-synced LFO on cutoff
        VIBRATO      = 1u << 2,   // delayed vibrato (LFO attack)
        PWM          = 1u << 3,   // slow LFO on both oscillators' symmetry
        KEYTRACK     = 1u << 4,   // note -> cutoff
        VEL_FILTER   = 1u << 5,   // velocity -> cutoff
        SAMPLE_HOLD  = 1u << 6,   // synced S&H on cutoff
        PITCH_DROP   = 1u << 7,   // filter envelope drops the pitch (blips, zaps)
        PITCH_RISE   = 1u << 8,   // slow filter envelope raises the pitch (risers)
        INHARMONIC   = 1u << 9,   // bell-like non-integer osc 2 interval
        BREATH       = 1u << 10,  // a little noise
        UNISON_LFO   = 1u << 11,  // LFO breathes the unison detune
        AMP_KEYSCALE = 1u << 12,  // low notes louder (Alpha factory bass trick)
        SYM_OFFSET   = 1u << 13,  // constant symmetry offset on osc 1
        SINGLE_OSC   = 1u << 14,  // osc 2 silent
        FM_VOICE     = 1u << 15   // osc 2 is a sine FM modulator
    };

    struct Rng { float lo, hi; };

    struct Profile {
        const char* label;
        const char* kind;              // used in the patch name
        std::vector<int> waves1, waves2;
        std::vector<int> oct;          // osc 1 octave choices
        std::vector<float> osc2Semis;  // osc 2 interval choices
        Rng morph{ 0.0f, 1.0f }, detune{ 2.0f, 10.0f }, mix{ 0.2f, 0.55f }, fm{ 0.0f, 0.0f };
        Rng ring{ 0.0f, 0.0f }, noise{ 0.0f, 0.0f };
        std::vector<int> ftypes{ LP24, LP24P };
        Rng cutoff{ 400.0f, 3000.0f }, res{ 0.1f, 0.5f }, drive{ 0.0f, 0.4f }, depth{ 0.2f, 0.6f };
        Rng fa{ 0.001f, 0.01f }, fd{ 0.15f, 0.6f }, fs{ 0.1f, 0.5f }, fr{ 0.1f, 0.6f }, ffade{ 0.45f, 0.55f };
        Rng aa{ 0.001f, 0.01f }, ad{ 0.2f, 0.8f }, as{ 0.6f, 1.0f }, ar{ 0.1f, 0.5f }, afade{ 0.48f, 0.52f };
        Rng chorus{ 0.0f, 0.3f }, unison{ 0.0f, 0.0f }, width{ 0.2f, 0.6f }, glide{ 0.0f, 0.0f };
        Rng analog{ 0.0f, 0.3f }, vel{ 0.2f, 0.6f };
        std::vector<int> voiceModes{ 0 };   // 0 Poly, 1 Mono, 2 Legato
        unsigned flags = 0;
        float gain = 1.0f;                  // loudness calibration (set from calibratedGain)
    };

    // Loudness calibration: measured loudest-100-ms level of 16 patches per
    // character (bass kinds on C3, pads/strings/organ/choir as a triad on C4,
    // everything else on C4), scaled towards a common target. Amp Vol tops
    // out at 1.0, so the quietest characters stay a few dB under the rest.
    float calibratedGain(const char* label) {
        static const std::map<juce::String, float> g{
            { "Bass", 0.85f },
            { "Sub Bass", 0.45f },
            { "Acid Bass", 1.60f },
            { "Funk Bass", 1.34f },
            { "Wobble Bass", 1.04f },
            { "Lead", 0.90f },
            { "Mono Lead", 0.86f },
            { "FM Classic", 0.68f },
            { "Brass", 0.92f },
            { "Strings", 0.46f },
            { "Organ", 0.44f },
            { "Electric Piano", 0.68f },
            { "Clav", 1.41f },
            { "Pad", 0.43f },
            { "Liquid Pad", 0.70f },
            { "Glass Pad", 0.51f },
            { "Drone", 1.90f },
            { "Choir", 0.73f },
            { "Pluck", 1.14f },
            { "Bell", 0.82f },
            { "Stab", 2.00f },
            { "Blip", 1.14f },
            { "Sequence", 1.32f },
            { "Reso Sweep", 1.49f },
            { "Riser", 1.23f },
            { "Noise FX", 1.94f },
            { "Atmosphere", 0.77f },
        };
        auto it = g.find(label);
        return it != g.end() ? it->second : 1.0f;
    }

    std::vector<int> range(int a, int b) { std::vector<int> v; for (int i = a; i <= b; ++i) v.push_back(i); return v; }
    std::vector<int> join(std::vector<int> a, const std::vector<int>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

    // ==========================================================
    //  The whole personality of the generator lives in this table.
    // ==========================================================
    const std::vector<Profile>& profiles() {
        static const std::vector<Profile> P = [] {
            std::vector<Profile> v;
            const auto sawBasses = range(SAWBASS1, SAWBASS8);
            const auto richSaws = range(RICHSAW1, RICHSAW4);
            const auto vintSaws = range(VINTSAW1, VINTSAW3);
            const auto spectra = range(SPECTRA1, SPECTRA4);
            auto add = [&v](Profile p) { p.gain = calibratedGain(p.label); v.push_back(std::move(p)); };

            // ---------------- low end ----------------
            { Profile p{ "Bass", "Bass" };
              p.waves1 = join(join({ SAW, SQUARE }, richSaws), sawBasses); p.waves2 = p.waves1;
              p.oct = { OCT_M1 }; p.osc2Semis = { 0, -12, 12 };
              p.ftypes = { LP24, LP24P }; p.cutoff = { 150, 700 }; p.res = { 0.1f, 0.5f }; p.drive = { 0.2f, 0.6f }; p.depth = { 0.3f, 0.7f };
              p.fd = { 0.1f, 0.5f }; p.fs = { 0.0f, 0.3f }; p.fr = { 0.05f, 0.25f };
              p.ad = { 0.2f, 0.8f }; p.as = { 0.5f, 1.0f }; p.ar = { 0.05f, 0.25f };
              p.chorus = { 0.0f, 0.15f }; p.width = { 0.0f, 0.2f };
              p.flags = VEL_FILTER | AMP_KEYSCALE; add(p); }

            { Profile p{ "Sub Bass", "Sub" };
              p.waves1 = { SINE, TRI, ORGAN1 }; p.waves2 = { SINE, TRI };
              p.oct = { OCT_M2, OCT_M1 }; p.osc2Semis = { 0, -12 }; p.mix = { 0.0f, 0.3f }; p.detune = { 0.0f, 3.0f };
              p.ftypes = { LP24 }; p.cutoff = { 200, 800 }; p.res = { 0.0f, 0.2f }; p.drive = { 0.0f, 0.3f }; p.depth = { 0.0f, 0.2f };
              p.as = { 0.8f, 1.0f }; p.ar = { 0.05f, 0.2f };
              p.chorus = { 0.0f, 0.0f }; p.width = { 0.0f, 0.0f }; p.analog = { 0.0f, 0.1f }; add(p); }

            { Profile p{ "Acid Bass", "Acid" };
              p.waves1 = { SAW, SQUARE, VINTSAW1 }; p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0 }; p.detune = { 0.0f, 0.0f };
              p.ftypes = { LP24P }; p.cutoff = { 150, 500 }; p.res = { 0.6f, 0.9f }; p.drive = { 0.3f, 0.7f }; p.depth = { 0.5f, 0.9f };
              p.fa = { 0.001f, 0.003f }; p.fd = { 0.1f, 0.35f }; p.fs = { 0.0f, 0.1f }; p.fr = { 0.05f, 0.15f };
              p.aa = { 0.001f, 0.002f }; p.ad = { 0.3f, 1.0f }; p.ar = { 0.05f, 0.12f };
              p.chorus = { 0.0f, 0.1f }; p.width = { 0.0f, 0.1f }; p.glide = { 0.05f, 0.15f }; p.vel = { 0.3f, 0.7f };
              p.voiceModes = { 2 }; p.flags = SINGLE_OSC | VEL_FILTER; add(p); }

            { Profile p{ "Funk Bass", "Funk" };      // the Alpha factory bass recipe
              p.waves1 = join({ SINE, TRI, SQUARE2 }, sawBasses); p.waves2 = { SINE, TRI };
              p.oct = { OCT_M1 }; p.osc2Semis = { 0, 12 }; p.mix = { 0.2f, 0.5f };
              p.ftypes = { LP24P }; p.cutoff = { 200, 600 }; p.res = { 0.4f, 0.7f }; p.drive = { 0.3f, 0.6f }; p.depth = { 0.5f, 0.8f };
              p.fd = { 0.08f, 0.25f }; p.fs = { 0.0f, 0.2f }; p.fr = { 0.05f, 0.15f };
              p.aa = { 0.001f, 0.003f }; p.ad = { 0.15f, 0.4f }; p.as = { 0.3f, 0.6f }; p.ar = { 0.05f, 0.15f };
              p.chorus = { 0.0f, 0.2f }; p.width = { 0.0f, 0.2f };
              p.flags = AMP_KEYSCALE | VEL_FILTER | SYM_OFFSET; add(p); }

            { Profile p{ "Wobble Bass", "Wobble" };
              p.waves1 = join({ SAW, SQUARE }, richSaws); p.waves2 = p.waves1;
              p.oct = { OCT_M1 }; p.osc2Semis = { 0, -12 }; p.unison = { 0.1f, 0.3f };
              p.ftypes = { LP24, LP24P }; p.cutoff = { 150, 400 }; p.res = { 0.3f, 0.6f }; p.drive = { 0.4f, 0.8f }; p.depth = { 0.0f, 0.2f };
              p.as = { 0.9f, 1.0f }; p.ar = { 0.05f, 0.2f }; p.width = { 0.1f, 0.4f };
              p.flags = WOBBLE; add(p); }

            // ---------------- melodic ----------------
            { Profile p{ "Lead", "Lead" };
              p.waves1 = join(join({ SAW, SQUARE, SQUARE2 }, richSaws), vintSaws); p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12, 7, -12 }; p.detune = { 4.0f, 15.0f };
              p.ftypes = { LP12, LP24, LP24P }; p.cutoff = { 1500, 5000 }; p.res = { 0.1f, 0.4f }; p.drive = { 0.1f, 0.4f };
              p.aa = { 0.001f, 0.02f }; p.as = { 0.7f, 1.0f }; p.ar = { 0.1f, 0.4f };
              p.chorus = { 0.1f, 0.3f }; p.unison = { 0.0f, 0.3f };
              p.flags = VIBRATO; add(p); }

            { Profile p{ "Mono Lead", "Solo" };
              p.waves1 = join({ SAW, SQUARE, SQUARE2 }, richSaws); p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12, -12 }; p.detune = { 3.0f, 12.0f };
              p.ftypes = { LP24, LP24P }; p.cutoff = { 1200, 4000 }; p.res = { 0.2f, 0.5f }; p.drive = { 0.2f, 0.5f };
              p.aa = { 0.001f, 0.01f }; p.as = { 0.8f, 1.0f }; p.ar = { 0.08f, 0.3f };
              p.chorus = { 0.1f, 0.3f }; p.glide = { 0.04f, 0.2f };
              p.voiceModes = { 1, 2 }; p.flags = VIBRATO; add(p); }

            { Profile p{ "FM Classic", "FM" };
              p.waves1 = { SINE, TRI }; p.waves2 = { SINE };
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12, 19, 24, -12 }; p.fm = { 1.0f, 4.0f }; p.detune = { 0.0f, 3.0f };
              p.ftypes = { LP12 }; p.cutoff = { 4000, 12000 }; p.res = { 0.0f, 0.2f }; p.depth = { 0.0f, 0.1f };
              p.ad = { 0.3f, 1.2f }; p.as = { 0.3f, 0.8f }; p.ar = { 0.1f, 0.5f };
              p.chorus = { 0.1f, 0.4f }; p.flags = FM_VOICE; add(p); }

            { Profile p{ "Brass", "Brass" };
              p.waves1 = { SAW, RICHSAW2, VINTSAW1, VINTSAW2 }; p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0, 12 }; p.detune = { 3.0f, 10.0f };
              p.ftypes = { LP24 }; p.cutoff = { 400, 1200 }; p.res = { 0.05f, 0.3f }; p.depth = { 0.4f, 0.7f };
              p.fa = { 0.03f, 0.12f }; p.fd = { 0.2f, 0.6f }; p.fs = { 0.3f, 0.6f };
              p.aa = { 0.02f, 0.08f }; p.as = { 0.7f, 0.9f }; p.ar = { 0.1f, 0.3f };
              p.chorus = { 0.1f, 0.3f }; p.flags = VIBRATO | VEL_FILTER; add(p); }

            { Profile p{ "Strings", "Strings" };
              p.waves1 = join({ SAW, RICHSAW3, SAWSPEC1, SAWSPEC2 }, vintSaws); p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12, -12 }; p.detune = { 6.0f, 18.0f }; p.unison = { 0.3f, 0.7f };
              p.ftypes = { LP12, LP24 }; p.cutoff = { 1500, 4500 }; p.res = { 0.0f, 0.2f }; p.depth = { 0.0f, 0.2f };
              p.aa = { 0.15f, 0.6f }; p.as = { 0.8f, 1.0f }; p.ar = { 0.4f, 1.5f };
              p.chorus = { 0.4f, 0.8f }; p.width = { 0.5f, 0.9f }; p.flags = PWM; add(p); }

            { Profile p{ "Organ", "Organ" };
              p.waves1 = { ORGAN1, ORGAN2, ORGAN3 }; p.waves2 = { ORGAN1, ORGAN2, SINE };
              p.oct = { OCT_0 }; p.osc2Semis = { 12, 0, 19 }; p.detune = { 0.0f, 3.0f };
              p.ftypes = { LP12 }; p.cutoff = { 3000, 9000 }; p.res = { 0.0f, 0.15f }; p.depth = { 0.0f, 0.0f }; p.drive = { 0.0f, 0.3f };
              p.aa = { 0.001f, 0.005f }; p.as = { 1.0f, 1.0f }; p.ar = { 0.02f, 0.08f };
              p.chorus = { 0.3f, 0.6f }; p.analog = { 0.0f, 0.15f }; add(p); }

            { Profile p{ "Electric Piano", "Keys" };
              p.waves1 = { SINE, TRI }; p.waves2 = { SINE };
              p.oct = { OCT_0 }; p.osc2Semis = { 12, 24, 19 }; p.fm = { 0.3f, 1.5f }; p.detune = { 0.0f, 4.0f };
              p.ftypes = { LP12 }; p.cutoff = { 2000, 6000 }; p.res = { 0.0f, 0.2f }; p.depth = { 0.1f, 0.3f }; p.fd = { 0.3f, 0.8f }; p.fs = { 0.0f, 0.3f };
              p.aa = { 0.001f, 0.003f }; p.ad = { 0.8f, 2.5f }; p.as = { 0.0f, 0.25f }; p.ar = { 0.2f, 0.6f };
              p.chorus = { 0.2f, 0.5f }; p.vel = { 0.5f, 0.8f }; p.flags = FM_VOICE | VEL_FILTER; add(p); }

            { Profile p{ "Clav", "Clav" };
              p.waves1 = { SQUARE2, SQUARE3, SAW }; p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12 }; p.detune = { 0.0f, 5.0f };
              p.ftypes = { LP12 }; p.cutoff = { 1200, 3500 }; p.res = { 0.3f, 0.6f }; p.depth = { 0.3f, 0.6f };
              p.fd = { 0.1f, 0.3f }; p.fs = { 0.1f, 0.3f };
              p.aa = { 0.001f, 0.002f }; p.ad = { 0.3f, 0.8f }; p.as = { 0.1f, 0.3f }; p.ar = { 0.03f, 0.1f };
              p.vel = { 0.5f, 0.8f }; p.flags = VEL_FILTER; add(p); }

            // ---------------- sustained ----------------
            { Profile p{ "Pad", "Pad" };
              p.waves1 = join(join({ SAW, SPECTRA1, SQUARE2 }, richSaws), vintSaws); p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0, 12, 7, -12 }; p.detune = { 5.0f, 15.0f }; p.unison = { 0.2f, 0.5f };
              p.ftypes = { LP12, LP24 }; p.cutoff = { 600, 2500 }; p.res = { 0.1f, 0.4f }; p.depth = { 0.1f, 0.4f };
              p.fa = { 0.5f, 2.0f }; p.fd = { 1.0f, 3.0f }; p.fs = { 0.3f, 0.7f }; p.fr = { 1.0f, 3.0f };
              p.aa = { 0.4f, 1.5f }; p.as = { 0.8f, 1.0f }; p.ar = { 1.0f, 3.0f };
              p.chorus = { 0.4f, 0.8f }; p.width = { 0.5f, 0.9f }; p.flags = PWM | FILTER_LFO; add(p); }

            { Profile p{ "Liquid Pad", "Liquid" };   // the Alpha signature: slow resonant sweep + chorus
              p.waves1 = { SAW, SQUARE, TRI, RICHSAW2 }; p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0, 12, -12 }; p.detune = { 4.0f, 12.0f };
              p.ftypes = { LP24P }; p.cutoff = { 200, 600 }; p.res = { 0.6f, 0.85f }; p.depth = { 0.5f, 0.8f }; p.drive = { 0.2f, 0.5f };
              p.fa = { 1.5f, 4.0f }; p.fd = { 2.0f, 5.0f }; p.fs = { 0.2f, 0.5f }; p.fr = { 2.0f, 4.0f }; p.ffade = { 0.3f, 0.7f };
              p.aa = { 0.6f, 2.0f }; p.as = { 0.8f, 1.0f }; p.ar = { 1.5f, 4.0f };
              p.chorus = { 0.5f, 0.9f }; p.width = { 0.6f, 1.0f }; p.analog = { 0.2f, 0.5f };
              p.flags = FILTER_LFO; add(p); }

            { Profile p{ "Glass Pad", "Glass" };
              p.waves1 = { SINE, ORGAN3, SPECTRA2, SQUARE3, TRI }; p.waves2 = { SINE, TRI, SPECTRA2 };
              p.oct = { OCT_0, OCT_P1 }; p.osc2Semis = { 12, 19, 24, 7 }; p.ring = { 0.2f, 0.5f };
              p.ftypes = { LP12 }; p.cutoff = { 3000, 10000 }; p.res = { 0.1f, 0.3f }; p.depth = { 0.0f, 0.2f };
              p.aa = { 0.3f, 1.0f }; p.as = { 0.8f, 1.0f }; p.ar = { 1.0f, 3.0f };
              p.chorus = { 0.4f, 0.7f }; p.unison = { 0.2f, 0.4f }; p.width = { 0.5f, 0.9f }; p.flags = PWM; add(p); }

            { Profile p{ "Drone", "Drone" };
              p.waves1 = join(join({ SAW, VINTSAW3, SAWSPEC1, SAWSPEC2 }, spectra), {}); p.waves2 = p.waves1;
              p.oct = { OCT_M2, OCT_M1 }; p.osc2Semis = { 7, -5, 12, 0 }; p.detune = { 3.0f, 10.0f }; p.unison = { 0.3f, 0.6f };
              p.ftypes = { LP24P, BP }; p.cutoff = { 350, 1400 }; p.res = { 0.4f, 0.75f }; p.depth = { 0.0f, 0.3f };
              p.fa = { 1.0f, 3.0f }; p.fd = { 2.0f, 5.0f };
              p.aa = { 1.0f, 3.0f }; p.as = { 1.0f, 1.0f }; p.ar = { 2.0f, 5.0f };
              p.chorus = { 0.3f, 0.6f }; p.width = { 0.5f, 0.9f }; p.analog = { 0.3f, 0.6f };
              p.flags = FILTER_LFO | UNISON_LFO; add(p); }

            { Profile p{ "Choir", "Choir" };
              p.waves1 = { SPECTRA4, SPECTRA1, ORGAN2 }; p.waves2 = { SPECTRA4, SINE };
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12 }; p.detune = { 5.0f, 12.0f }; p.unison = { 0.3f, 0.6f };
              p.ftypes = { BP, LP12 }; p.cutoff = { 800, 2000 }; p.res = { 0.2f, 0.5f }; p.depth = { 0.0f, 0.2f };
              p.aa = { 0.3f, 0.8f }; p.as = { 0.8f, 1.0f }; p.ar = { 0.8f, 2.0f };
              p.chorus = { 0.5f, 0.8f }; p.width = { 0.5f, 0.9f }; p.flags = VIBRATO | BREATH; add(p); }

            // ---------------- struck and short ----------------
            { Profile p{ "Pluck", "Pluck" };
              p.waves1 = join({ SAW, SQUARE, SQUARE2, TRI }, richSaws); p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12, 7 }; p.detune = { 2.0f, 10.0f };
              p.ftypes = { LP24, LP12 }; p.cutoff = { 300, 1200 }; p.res = { 0.1f, 0.5f }; p.depth = { 0.5f, 0.9f };
              p.fd = { 0.08f, 0.4f }; p.fs = { 0.0f, 0.1f }; p.fr = { 0.1f, 0.3f };
              p.aa = { 0.001f, 0.002f }; p.ad = { 0.3f, 1.2f }; p.as = { 0.0f, 0.1f }; p.ar = { 0.15f, 0.5f };
              p.chorus = { 0.1f, 0.4f }; p.vel = { 0.4f, 0.8f }; p.flags = VEL_FILTER | KEYTRACK; add(p); }

            { Profile p{ "Bell", "Bell" };
              p.waves1 = { SINE, TRI }; p.waves2 = { SINE, ORGAN1 };
              p.oct = { OCT_0, OCT_P1 }; p.osc2Semis = { 19.02f, 27.86f, 30.99f, 33.0f, 16.5f }; p.ring = { 0.3f, 0.8f }; p.mix = { 0.3f, 0.6f };
              p.ftypes = { LP12 }; p.cutoff = { 5000, 14000 }; p.res = { 0.0f, 0.2f }; p.depth = { 0.0f, 0.1f };
              p.aa = { 0.001f, 0.002f }; p.ad = { 1.5f, 4.0f }; p.as = { 0.0f, 0.0f }; p.ar = { 1.5f, 3.0f };
              p.chorus = { 0.2f, 0.4f }; p.flags = INHARMONIC; add(p); }

            { Profile p{ "Stab", "Stab" };
              p.waves1 = join({ SAW, SQUARE2 }, richSaws); p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 7, 12, 0 }; p.detune = { 4.0f, 12.0f }; p.unison = { 0.2f, 0.5f };
              p.ftypes = { LP24 }; p.cutoff = { 900, 2500 }; p.res = { 0.1f, 0.4f }; p.depth = { 0.4f, 0.7f };
              p.fd = { 0.1f, 0.3f }; p.fs = { 0.2f, 0.4f };
              p.aa = { 0.001f, 0.002f }; p.ad = { 0.2f, 0.5f }; p.as = { 0.1f, 0.3f }; p.ar = { 0.08f, 0.25f };
              p.chorus = { 0.2f, 0.4f }; add(p); }

            { Profile p{ "Blip", "Blip" };
              p.waves1 = { SINE, SQUARE, TRI }; p.waves2 = p.waves1;
              p.oct = { OCT_0, OCT_P1 }; p.osc2Semis = { 0 };
              p.ftypes = { LP12 }; p.cutoff = { 2000, 8000 }; p.res = { 0.0f, 0.3f }; p.depth = { 0.0f, 0.2f };
              p.fa = { 0.001f, 0.002f }; p.fd = { 0.02f, 0.1f }; p.fs = { 0.0f, 0.0f };
              p.aa = { 0.001f, 0.002f }; p.ad = { 0.03f, 0.15f }; p.as = { 0.0f, 0.0f }; p.ar = { 0.02f, 0.08f };
              p.flags = SINGLE_OSC | PITCH_DROP; add(p); }

            // ---------------- movement and effects ----------------
            { Profile p{ "Sequence", "Step" };
              p.waves1 = { SAW, SQUARE, RICHSAW1 }; p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0, 12 };
              p.ftypes = { LP24, LP24P }; p.cutoff = { 300, 900 }; p.res = { 0.5f, 0.8f }; p.depth = { 0.0f, 0.3f };
              p.as = { 0.9f, 1.0f }; p.ar = { 0.1f, 0.4f }; p.chorus = { 0.1f, 0.4f };
              p.flags = SAMPLE_HOLD; add(p); }

            { Profile p{ "Reso Sweep", "Sweep" };
              p.waves1 = { SAW, SAWSPEC2, RICHSAW3 }; p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0, 12, -12 }; p.detune = { 3.0f, 10.0f };
              p.ftypes = { LP24P, BP }; p.cutoff = { 150, 400 }; p.res = { 0.7f, 0.9f }; p.depth = { 0.6f, 0.95f };
              p.fa = { 1.0f, 4.0f }; p.fd = { 2.0f, 5.0f }; p.fs = { 0.3f, 0.7f }; p.fr = { 1.0f, 3.0f };
              p.aa = { 0.05f, 0.3f }; p.as = { 0.9f, 1.0f }; p.ar = { 0.5f, 2.0f };
              p.chorus = { 0.3f, 0.6f }; p.width = { 0.4f, 0.8f }; add(p); }

            { Profile p{ "Riser", "Riser" };
              p.waves1 = join({ SAW }, richSaws); p.waves2 = p.waves1;
              p.oct = { OCT_M1, OCT_0 }; p.osc2Semis = { 0, 7, 12 }; p.noise = { 0.1f, 0.35f }; p.unison = { 0.3f, 0.6f };
              p.ftypes = { LP24 }; p.cutoff = { 200, 600 }; p.res = { 0.2f, 0.5f }; p.depth = { 0.7f, 1.0f };
              p.fa = { 3.0f, 8.0f }; p.fs = { 1.0f, 1.0f }; p.fr = { 0.5f, 2.0f };
              p.aa = { 1.0f, 4.0f }; p.as = { 1.0f, 1.0f }; p.ar = { 0.5f, 2.0f };
              p.chorus = { 0.3f, 0.6f }; p.width = { 0.5f, 0.9f }; p.flags = PITCH_RISE; add(p); }

            { Profile p{ "Noise FX", "Noise" };
              p.waves1 = { NOISE }; p.waves2 = { NOISE, SINE };
              p.oct = { OCT_0 }; p.osc2Semis = { 0, 12 }; p.noise = { 0.5f, 1.0f };
              p.ftypes = { BP, HP, LP12 }; p.cutoff = { 400, 4000 }; p.res = { 0.4f, 0.85f }; p.depth = { -0.5f, 0.5f };
              p.fa = { 0.01f, 1.0f }; p.fd = { 0.2f, 2.0f };
              p.aa = { 0.001f, 0.5f }; p.as = { 0.3f, 1.0f }; p.ar = { 0.2f, 2.0f };
              p.chorus = { 0.2f, 0.6f }; p.width = { 0.5f, 1.0f }; p.flags = FILTER_LFO | SAMPLE_HOLD; add(p); }

            { Profile p{ "Atmosphere", "Atmos" };
              p.waves1 = join({ ORGAN3, SAWSPEC1, SAWSPEC2, SINE }, spectra); p.waves2 = p.waves1;
              p.oct = { OCT_0 }; p.osc2Semis = { 7, 12, 19, -5 }; p.noise = { 0.05f, 0.2f }; p.ring = { 0.0f, 0.3f };
              p.unison = { 0.2f, 0.5f };
              p.ftypes = { LP12, BP }; p.cutoff = { 800, 3000 }; p.res = { 0.2f, 0.5f }; p.depth = { 0.0f, 0.3f };
              p.aa = { 1.0f, 3.0f }; p.as = { 1.0f, 1.0f }; p.ar = { 2.0f, 5.0f };
              p.chorus = { 0.6f, 1.0f }; p.width = { 0.8f, 1.0f }; p.analog = { 0.3f, 0.7f };
              p.flags = FILTER_LFO | PWM | UNISON_LFO; add(p); }

            return v;
        }();
        return P;
    }

    // ==========================================================
    //  Helpers
    // ==========================================================
    struct Gen {
        juce::AudioProcessorValueTreeState& st;
        juce::Random& rng;
        int nextSlot = 0;

        float pick(Rng r) { return r.lo + (r.hi - r.lo) * rng.nextFloat(); }
        // log-spread pick for times and frequencies
        float pickLog(Rng r) {
            if (r.lo <= 0.0f || r.hi <= r.lo) return pick(r);
            return r.lo * std::pow(r.hi / r.lo, rng.nextFloat());
        }
        template <typename T> T choose(const std::vector<T>& v) { return v[(size_t)rng.nextInt((int)v.size())]; }
        bool chance(float p) { return rng.nextFloat() < p; }

        void set(const juce::String& id, float value) {
            if (auto* p = st.getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(value));
        }
        // Matrix slot helper; amounts in percent
        void route(int src, float amount, int dst) {
            if (nextSlot >= Mod::NUM_SLOTS) return;
            set(Mod::slotSrcID(nextSlot), (float)src);
            set(Mod::slotAmtID(nextSlot), amount);
            set(Mod::slotDstID(nextSlot), (float)dst);
            ++nextSlot;
        }
        void lfo(int k, int wave, float rateHz, int syncIndex, float attack, bool poly) {
            set(Mod::lfoWaveID(k), (float)wave);
            set(Mod::lfoRateID(k), rateHz);
            set(Mod::lfoSyncID(k), (float)syncIndex);
            set(Mod::lfoAttID(k), attack);
            set(Mod::lfoModeID(k), poly ? 1.0f : 0.0f);
        }
        int syncIndex(const char* name) { return juce::jmax(0, Mod::syncNames().indexOf(name)); }
    };

    // Everything back to defaults, except what belongs to the user rather
    // than to the patch: bend range, master tune and the voice-mode choice
    // is reset by the profile anyway.
    void resetForNewPatch(juce::AudioProcessorValueTreeState& st) {
        for (auto* param : st.processor.getParameters()) {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param);
            if (rp == nullptr) continue;
            auto id = rp->getParameterID();
            if (id == PID::BEND || id == PID::TUNE) continue;
            rp->setValueNotifyingHost(rp->getDefaultValue());
        }
    }

    juce::String nameFor(const char* kind, juce::Random& rng) {
        static const char* prefixes[] = {
            "Liquid", "Phosphor", "Cathode", "Quartz", "Tidal", "Velvet", "Neon", "Orbit",
            "Silver", "Glass", "Drift", "Signal", "Halo", "Nimbus", "Vapor", "Delta",
            "Prism", "Lumen", "Scanline", "Beta", "Alpha", "Cobalt", "Azure", "Mercury" };
        const int n = (int)(sizeof(prefixes) / sizeof(prefixes[0]));
        return juce::String(prefixes[rng.nextInt(n)]) + " " + kind;
    }

    juce::String wildName(juce::Random& rng) {
        static const char* first[] = { "Feral", "Unstable", "Rogue", "Broken", "Mutant", "Accidental",
                                       "Haywire", "Improbable", "Stray", "Volatile", "Wayward", "Glitched" };
        static const char* second[] = { "Tube", "Signal", "Specimen", "Anomaly", "Artefact", "Static",
                                        "Creature", "Weather", "Machine", "Phosphor", "Transmission", "Echo" };
        return juce::String(first[rng.nextInt(12)]) + " " + second[rng.nextInt(12)];
    }

    // ==========================================================
    //  Character generator
    // ==========================================================
    juce::String generateCharacter(juce::AudioProcessorValueTreeState& st, const Profile& pr, juce::Random& rng) {
        resetForNewPatch(st);
        Gen g{ st, rng };

        // ---- Oscillators ----
        const int oct = g.choose(pr.oct);
        const int octB = (g.chance(0.25f) && oct < OCT_P2) ? oct + 1 : oct;
        g.set(PID::O1WA, (float)g.choose(pr.waves1));
        g.set(PID::O1WB, (float)g.choose(pr.waves1));
        g.set(PID::O1OCA, (float)oct);
        g.set(PID::O1OCB, (float)octB);
        g.set(PID::O1MRP, g.pick(pr.morph));
        g.set(PID::O1DET, g.pick(pr.detune) * (g.chance(0.5f) ? 1.0f : -1.0f));

        g.set(PID::O2WA, (float)g.choose(pr.waves2));
        g.set(PID::O2WB, (float)g.choose(pr.waves2));
        g.set(PID::O2OCA, (float)oct);
        g.set(PID::O2OCB, (float)oct);
        g.set(PID::O2MRP, g.pick(pr.morph));
        g.set(PID::O2DET, g.pick(pr.detune) * 0.5f * (g.chance(0.5f) ? 1.0f : -1.0f));
        float semis = g.choose(pr.osc2Semis);
        if (pr.flags & INHARMONIC) semis += g.pick({ -0.25f, 0.25f });
        g.set(PID::O2PIT, semis);

        const bool single = (pr.flags & SINGLE_OSC) != 0;
        g.set(PID::MIX, single ? 0.0f : g.pick(pr.mix));
        g.set(PID::FM, g.pick(pr.fm));
        g.set(PID::RING, g.pick(pr.ring));
        g.set(PID::NOISE, g.pick(pr.noise) + ((pr.flags & BREATH) ? g.pick({ 0.04f, 0.12f }) : 0.0f));
        g.set(PID::DRV, g.pick(pr.drive));

        // ---- Filter ----
        g.set(PID::FTYP, (float)g.choose(pr.ftypes));
        g.set(PID::FCUT, g.pickLog(pr.cutoff));
        g.set(PID::FRES, g.pick(pr.res));
        g.set(PID::FDEP, g.pick(pr.depth));
        g.set(PID::FATT, g.pickLog(pr.fa));
        g.set(PID::FDEC, g.pickLog(pr.fd));
        g.set(PID::FSUS, g.pick(pr.fs));
        g.set(PID::FREL, g.pickLog(pr.fr));
        g.set(PID::FFAD, g.pick(pr.ffade));

        // ---- Amplifier ----
        g.set(PID::AATT, g.pickLog(pr.aa));
        g.set(PID::ADEC, g.pickLog(pr.ad));
        g.set(PID::ASUS, g.pick(pr.as));
        g.set(PID::AREL, g.pickLog(pr.ar));
        g.set(PID::AFAD, g.pick(pr.afade));
        g.set(PID::AVEL, g.pick(pr.vel));
        g.set(PID::AVOL, juce::jlimit(0.2f, 1.0f, 0.75f * pr.gain));

        // ---- Voice, chorus ----
        const int mode = g.choose(pr.voiceModes);
        g.set(PID::VMODE, (float)mode);
        g.set(PID::GLID, mode != 0 ? g.pick(pr.glide) : 0.0f);  // poly glide would drift per voice
        g.set(PID::UNI, g.pick(pr.unison));
        g.set(PID::SPR, g.pick(pr.width));
        g.set(PID::ANALOG, g.pick(pr.analog));
        g.set(PID::HWET, g.pick(pr.chorus));
        g.set(PID::HTIM, g.pick({ 15.0f, 40.0f }));
        g.set(PID::HRAT, g.pickLog({ 0.1f, 1.2f }));

        // ---- Behaviour: LFOs and matrix ----
        const unsigned f = pr.flags;
        if (f & WOBBLE) {
            const char* syncs[] = { "1/4", "1/8", "1/8T", "1/16", "1/4T", "1/2" };
            g.lfo(0, g.choose(std::vector<int>{ Mod::LFO_SINE, Mod::LFO_TRI, Mod::LFO_SAW }), 2.0f, g.syncIndex(syncs[rng.nextInt(6)]), 0.0f, false);
            g.route(Mod::SRC_LFO1, g.pick({ 30.0f, 60.0f }), Mod::DST_CUTOFF);
        }
        if (f & FILTER_LFO) {
            g.lfo(1, g.choose(std::vector<int>{ Mod::LFO_SINE, Mod::LFO_TRI }), g.pickLog({ 0.05f, 0.6f }), 0, 0.0f, g.chance(0.5f));
            g.route(Mod::SRC_LFO2, g.pick({ 6.0f, 20.0f }), Mod::DST_CUTOFF);
        }
        if (f & SAMPLE_HOLD) {
            const char* syncs[] = { "1/16", "1/8", "1/16T", "1/8T" };
            g.lfo(2, Mod::LFO_SAMHO, 4.0f, g.syncIndex(syncs[rng.nextInt(4)]), 0.0f, false);
            g.route(Mod::SRC_LFO3, g.pick({ 20.0f, 45.0f }), Mod::DST_CUTOFF);
        }
        if (f & VIBRATO) {
            // LFO 1 unless the wobble took it; delayed onset via LFO attack
            const int k = (f & WOBBLE) ? 2 : 0;
            g.lfo(k, Mod::LFO_SINE, g.pick({ 4.5f, 6.5f }), 0, g.pick({ 0.4f, 1.2f }), true);
            g.route(Mod::SRC_LFO1 + k, g.pick({ 0.6f, 1.4f }), Mod::DST_MAIN_PITCH);   // ~0.15-0.35 semitones
        }
        if (f & PWM) {
            const int k = (f & FILTER_LFO) ? 2 : 1;
            g.lfo(k, Mod::LFO_TRI, g.pickLog({ 0.1f, 0.8f }), 0, 0.0f, true);
            g.route(Mod::SRC_LFO1 + k, g.pick({ 20.0f, 45.0f }), Mod::DST_OSC1_SYM);
            g.route(Mod::SRC_LFO1 + k, -g.pick({ 20.0f, 45.0f }), Mod::DST_OSC2_SYM);
        }
        if (f & UNISON_LFO) {
            g.route(Mod::SRC_LFO2, g.pick({ 10.0f, 30.0f }), Mod::DST_UNISON);
        }
        if (f & KEYTRACK)     g.route(Mod::SRC_NOTE_LIN, g.pick({ 15.0f, 35.0f }), Mod::DST_CUTOFF);
        if (f & VEL_FILTER)   g.route(Mod::SRC_VELOCITY, g.pick({ 10.0f, 30.0f }), Mod::DST_CUTOFF);
        if (f & AMP_KEYSCALE) g.route(Mod::SRC_NOTE_LIN, -g.pick({ 30.0f, 70.0f }), Mod::DST_MAIN_AMP);
        if (f & SYM_OFFSET)   g.route(Mod::SRC_CONSTANT, g.pick({ 10.0f, 30.0f }), Mod::DST_OSC1_SYM);
        if (f & PITCH_DROP)   g.route(Mod::SRC_FILT_ENV, g.pick({ 15.0f, 45.0f }), Mod::DST_MAIN_PITCH);
        if (f & PITCH_RISE)   g.route(Mod::SRC_FILT_ENV, g.pick({ 15.0f, 40.0f }), Mod::DST_MAIN_PITCH);
        if (f & FM_VOICE) {   // osc 2 is a pure sine modulator
            g.set(PID::O2WA, (float)SINE); g.set(PID::O2WB, (float)SINE);
        }
        return nameFor(pr.kind, rng);
    }

    // ==========================================================
    //  Super Random: everything goes, with a short list of guards
    // ==========================================================
    juce::String superRandom(juce::AudioProcessorValueTreeState& st, juce::Random& rng) {
        Gen g{ st, rng };
        for (auto* param : st.processor.getParameters()) {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*>(param);
            if (rp == nullptr) continue;
            auto id = rp->getParameterID();
            if (id == PID::BEND || id == PID::TUNE || id == PID::VMODE) continue;
            if (id.startsWith("mm")) continue;               // matrix handled below
            rp->setValueNotifyingHost(rng.nextFloat());
        }
        // The matrix: up to four random routings
        for (int i = 0; i < Mod::NUM_SLOTS; ++i) {
            g.set(Mod::slotSrcID(i), 0.0f); g.set(Mod::slotAmtID(i), 0.0f); g.set(Mod::slotDstID(i), 0.0f);
        }
        const int routes = rng.nextInt(5);
        for (int i = 0; i < routes; ++i)
            g.route(1 + rng.nextInt(Mod::NUM_SOURCES - 1), g.pick({ -50.0f, 50.0f }), 1 + rng.nextInt(Mod::NUM_DESTS - 1));

        // --- guards, each earning its place ---
        // No user table in a random slot (it would silently play a sine)
        for (auto id : { PID::O1WA, PID::O1WB, PID::O2WA, PID::O2WB })
            if ((int)*st.getRawParameterValue(id) == TABLE) g.set(id, (float)SAW);
        // Audible and not deafening
        g.set(PID::AVOL, g.pick({ 0.55f, 0.8f }));
        g.set(PID::AATT, g.pickLog({ 0.001f, 1.5f }));
        g.set(PID::ASUS, g.pick({ 0.3f, 1.0f }));
        g.set(PID::AREL, g.pickLog({ 0.05f, 2.5f }));
        g.set(PID::FRES, g.pick({ 0.0f, 0.85f }));
        g.set(PID::NOISE, g.pick({ 0.0f, 0.5f }));
        g.set(PID::FM, g.pick({ 0.0f, 5.0f }));
        g.set(PID::GLID, g.pick({ 0.0f, 0.3f }));
        // Register: the pitch knobs (+-24 st) stack with the octave choosers, which
        // can push the tone several octaves above the filter - keep them musical
        for (auto id : { PID::O1OCA, PID::O1OCB, PID::O2OCA, PID::O2OCB })
            g.set(id, (float)(OCT_M1 + rng.nextInt(3)));
        const std::vector<float> intervals{ -12.0f, -7.0f, -5.0f, 0.0f, 0.0f, 5.0f, 7.0f, 12.0f };
        g.set(PID::O1PIT, 0.0f);
        g.set(PID::O2PIT, g.choose(intervals));
        // A strongly negative filter envelope keeps a low-pass shut for the whole note
        g.set(PID::FDEP, g.pick({ -0.3f, 1.0f }));
        // A low-pass that is almost closed is silence, and a high-pass that is
        // almost fully open is silence too
        const int ft = (int)*st.getRawParameterValue(PID::FTYP);
        if (ft <= LP24P) g.set(PID::FCUT, g.pickLog({ 400.0f, 12000.0f }));
        if (ft == HP)    g.set(PID::FCUT, g.pickLog({ 20.0f, 1500.0f }));
        if (ft == BP)    g.set(PID::FCUT, g.pickLog({ 500.0f, 5000.0f }));
        return wildName(rng);
    }
}

// ==============================================================
juce::StringArray characterNames() {
    juce::StringArray n;
    for (const auto& p : profiles()) n.add(p.label);
    n.add("Anything");
    n.add("Super Random");
    return n;
}

int anythingIndex() { return (int)profiles().size(); }
int superRandomIndex() { return (int)profiles().size() + 1; }

juce::String sectionBefore(int index) {
    if (index < 0 || index >= (int)profiles().size()) return index == anythingIndex() ? "Surprise" : "";
    const juce::String l = profiles()[(size_t)index].label;
    if (l == "Bass") return "Low end";
    if (l == "Lead") return "Melodic";
    if (l == "Pad") return "Sustained";
    if (l == "Pluck") return "Struck and short";
    if (l == "Sequence") return "Movement and effects";
    return {};
}

juce::String generate(juce::AudioProcessorValueTreeState& state, int character, juce::Random& rng) {
    if (character == superRandomIndex()) return superRandom(state, rng);
    if (character < 0 || character >= (int)profiles().size())      // Anything
        character = rng.nextInt((int)profiles().size());
    return generateCharacter(state, profiles()[(size_t)character], rng);
}

} // namespace RandomPatch
