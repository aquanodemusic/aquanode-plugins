#include "SpringerModule.h"

using namespace aquanode;

// per-coil delay scale spread + density delay (ms), echoing the original's
// per-coil "coil length" and alternating densityA/densityB pattern
static const float kCoilScale[SpringerModule::maxCoils] = { 1.0f, 1.13f, 0.87f, 1.27f, 0.79f, 1.41f, 0.71f };
static const float kDensityMs[SpringerModule::maxCoils] = { 5.1f, 7.9f, 5.7f, 8.7f, 4.7f, 9.3f, 6.3f };

void SpringerModule::prepare (double sr)
{
    SynthModule::prepare (sr);

    juce::Random rng (0x5153);   // fixed seed: coil dispersion patterns are stable

    for (int c = 0; c < maxCoils; ++c)
    {
        auto& coil = coils[c];
        coil.delayBuf.assign ((size_t) juce::jmax (64, (int) (sr * 0.16)), 0.0f);   // 100ms max size * 1.41 coil scale + headroom
        coil.densityBuf.assign ((size_t) juce::jmax (8, (int) (kDensityMs[c] * 0.001 * sr)), 0.0f);

        // per-coil pseudo-random dispersion coefficients (the spring "boing")
        for (int i = 0; i < maxStages; ++i)
            coil.coeffs[i] = 0.35f + rng.nextFloat() * 0.45f;
    }

    reset();
}

void SpringerModule::reset()
{
    for (auto& coil : coils)
    {
        std::fill (coil.delayBuf.begin(), coil.delayBuf.end(), 0.0f);
        std::fill (coil.densityBuf.begin(), coil.densityBuf.end(), 0.0f);
        coil.writePos = 0;
        coil.densityPos = 0;
        for (auto& m : coil.apMem) m = 0.0f;
        coil.dampingMem = coil.hpMem = coil.lastOut = 0.0f;
    }
}

void SpringerModule::processSample (const StereoFrame* inputs, StereoFrame* outputs)
{
    const float sizeMs = param (pSizeMs);
    const int activeCoils = juce::jlimit (1, maxCoils, (int) param (pCoils));
    const int stages = juce::jlimit (1, maxStages, (int) param (pStages));
    const float feedback = juce::jlimit (0.0f, 1.02f, param (pResonance) * 0.85f);
    const float dampCoeff = juce::jlimit (0.01f, 1.0f, 1.0f - param (pDamping) * 0.0099f);
    const float chirpShift = (param (pChirp) * 0.01f - 0.5f) * 0.4f;
    const float wet = param (pDryWet) * 0.01f;

    const float inMono = (inputs[0][0] + inputs[0][1]) * 0.5f * 0.75f;   // original's input drive

    float wetL = 0.0f, wetR = 0.0f;

    for (int c = 0; c < activeCoils; ++c)
    {
        auto& coil = coils[c];
        const int delaySize = (int) coil.delayBuf.size();
        const int delaySamples = juce::jlimit (4, delaySize - 2,
            (int) (sizeMs * 0.001f * (float) sampleRate * kCoilScale[c]));

        // feedback with DC block (matches SpringLine::process)
        const float fbSig = coil.lastOut * feedback;
        coil.hpMem += 0.06f * (fbSig - coil.hpMem);
        const float inner = inMono + fbSig - coil.hpMem;

        // main delay
        int readPos = coil.writePos - delaySamples;
        if (readPos < 0) readPos += delaySize;
        float out = coil.delayBuf[(size_t) readPos];
        coil.delayBuf[(size_t) coil.writePos] = inner;
        coil.writePos = (coil.writePos + 1) % delaySize;

        // density smearing allpass (fixed -0.6 coeff like the original)
        {
            const float b1 = -0.6f;
            const float delayOut = coil.densityBuf[(size_t) coil.densityPos];
            const float y = out + b1 * delayOut;
            coil.densityBuf[(size_t) coil.densityPos] = y;
            coil.densityPos = (coil.densityPos + 1) % (int) coil.densityBuf.size();
            out = delayOut + (-b1) * y;
        }

        // allpass dispersion chain - THE spring chirp
        for (int i = 0; i < stages; ++i)
        {
            const float a = juce::jlimit (0.05f, 0.985f, coil.coeffs[i] + chirpShift);
            const float y = coil.apMem[i] - a * out;
            coil.apMem[i] = out + a * y;
            out = y;
        }

        // damping lowpass + soft saturation
        coil.dampingMem += dampCoeff * (out - coil.dampingMem);
        out = std::tanh (coil.dampingMem * 1.2f);
        coil.lastOut = out;

        // alternate coils L/R for width
        if (c & 1) wetR += out;
        else       wetL += out;
    }

    // loudness compensation for coil count (from the original)
    const float norm = 1.2f / (1.0f + std::sqrt ((float) activeCoils) * 0.4f);
    wetL *= norm;
    wetR *= norm;
    if (activeCoils == 1)
        wetR = wetL;

    outputs[0][0] = inputs[0][0] * (1.0f - wet) + wetL * wet;
    outputs[0][1] = inputs[0][1] * (1.0f - wet) + wetR * wet;
}

static ModuleDescriptor springerDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "fx.springer";
    d.displayName = "Spring Reverb";
    d.description =
        "Spring reverb: up to seven parallel coils, each a feedback loop of delay, allpass "
        "dispersion and damping, which is what produces the springy chirp. Chirp shifts the "
        "dispersion - kick a spring tank and hear it rattle.";
    d.section = ModuleSection::Effect;
    d.sidebarOrder = 7;
    d.sockets = {
        audioIn ("audioIn", "Audio In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("sizeMs",    "Size",      2.0f, 100.0f, 30.0f, 0, "ms", true),
        makeRotary ("coils",     "Coils",     1.0f, 7.0f, 2.0f, 0, {}, false, 1.0f),
        makeRotary ("stages",    "Stages",    1.0f, 128.0f, 32.0f, 0, {}, true, 1.0f),
        makeRotary ("resonance", "Resonance", 0.0f, 1.2f, 0.7f, 0),
        makeRotary ("damping",   "Damping",   0.0f, 100.0f, 40.0f, 0, "%"),
        makeRotary ("chirp",     "Chirp",     0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("dryWet",    "Dry/Wet",   0.0f, 100.0f, 40.0f, 1, "%")
    };
    return d;
}

AQUANODE_REGISTER_MODULE (SpringerModule, springerDescriptor)
