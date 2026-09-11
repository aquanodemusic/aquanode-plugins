#include "DropletsModule.h"

using namespace aquanode;

void DropletsModule::processVoiceSample (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    // voice-steal de-click: a muted voice ramps out over a few ms instead of
    // being cut dead, and a re-used voice ramps back in
    const float voiceGain = pool.nextGain (v, sampleRate);
    if (pool.isSilent (v))
    {
        outputs[0][0] = 0.0f;
        outputs[0][1] = 0.0f;
        return;
    }

    renderVoice (v, inputs, outputs);

    outputs[0][0] *= voiceGain;
    outputs[0][1] *= voiceGain;
}

void DropletsModule::triggerDroplet (int v)
{
    const int maxActive = juce::jlimit (1, kMaxDroplets, (int) param (pDensity));

    // find a free slot, respecting the Density (CPU) cap
    Droplet* slot = nullptr;
    int active = 0;
    for (auto& d : droplets[v])
    {
        if (! d.active)
        {
            if (slot == nullptr)
                slot = &d;
        }
        else
            ++active;
    }
    if (slot == nullptr || active >= maxActive)
        return;

    // Size knob is in millimetres; the physics work in metres.
    // Gaussian bandwidth spreads the radius log-normally, like the plugin.
    const float sizeVar = param (pSizeVar) * 0.01f;
    float radius = (param (pSize) * 0.001f)
                 * std::pow (10.0f, gaussianRandom() * sizeVar * 0.5f);
    radius = juce::jlimit (0.00015f, 0.02f, radius);

    // bubble acoustics: freq = 3 / radius (metres)
    const float baseFreq = juce::jlimit (20.0f, 20000.0f, 3.0f / radius);

    // amplitude ~ radius^1.5, scaled up for audibility, Bright as distance
    const float bright = param (pBright) * 0.01f;
    float amplitude = radius * std::sqrt (radius) * 200.0f
                    * std::pow (10.0f, (bright - 0.5f) * 2.0f);

    // decay time follows the radius: tiny bubbles ping, big ones bloop
    const float decayTimeSeconds = 0.001f + radius * 2.0f;   // 1 ms .. ~41 ms

    slot->amplitude   = amplitude;
    slot->currentFreq = baseFreq;
    slot->decay = std::exp ((float) (-1.0 / (decayTimeSeconds * sampleRate)));

    // upward chirp: Depth sets how far the pitch rises, Rise how fast
    const float rise  = param (pRise)  * 0.01f;
    const float depth = param (pDepth) * 0.01f;
    if (rise > 0.01f)
    {
        const float targetFreq = baseFreq * (1.0f + depth * juce::MathConstants<float>::sqrt2);
        const float riseDuration = juce::jmax (0.0005f, decayTimeSeconds * (1.5f - rise));
        slot->pitchRiseRate = (targetFreq - baseFreq) / (float) (riseDuration * sampleRate);
    }
    else
        slot->pitchRiseRate = 0.0f;

    // per-droplet stereo placement
    const float width = param (pWidth) * 0.01f;
    slot->pan = width < 0.01f
        ? 0.5f
        : juce::jlimit (0.0f, 1.0f, 0.5f + (random.nextFloat() - 0.5f) * width);

    slot->phase = random.nextFloat() * juce::MathConstants<float>::twoPi;

    // fixed musical fades (the plugin's defaults): 1 ms in, 10 ms out
    slot->fadeInSamples  = juce::jmax (1.0f, (float) (0.001 * sampleRate));
    slot->fadeOutSamples = juce::jmax (1.0f, (float) (0.010 * sampleRate));
    slot->lifetimeSamples = (float) ((decayTimeSeconds * 5.0 + 0.010) * sampleRate);
    if (slot->fadeOutSamples > slot->lifetimeSamples * 0.5f)
        slot->fadeOutSamples = slot->lifetimeSamples * 0.5f;

    slot->age = 0;
    slot->active = true;
}

void DropletsModule::renderVoice (int v, const StereoFrame* inputs, StereoFrame* outputs)
{
    const bool  gateHigh = inputs[0][0] > 0.5f;
    const bool  envConnected = isInputConnected (1);
    const float envIn = envConnected ? inputs[1][0] : 1.0f;

    // ---- droplet clock: runs while the note is held or Gate In is high ----
    if (noteHeld[v] || gateHigh)
    {
        const float rateSeconds = juce::jmax (0.004f, param (pRate) * 0.001f);
        float inc = (float) (1.0 / (rateSeconds * sampleRate));

        const float rateVar = param (pRateVar) * 0.01f;
        inc *= 1.0f + gaussianRandom() * rateVar * 0.5f;

        clockPhase[v] += juce::jmax (0.0f, inc);
        if (clockPhase[v] >= 1.0f)
        {
            clockPhase[v] -= std::floor (clockPhase[v]);
            triggerDroplet (v);
        }
    }

    // ---- render the active droplets ----
    float outL = 0.0f, outR = 0.0f;

    for (auto& d : droplets[v])
    {
        if (! d.active)
            continue;

        // smoothstep fade-in / fade-out envelope
        float env = 1.0f;
        if ((float) d.age < d.fadeInSamples)
        {
            const float t = (float) d.age / d.fadeInSamples;
            env = t * t * (3.0f - 2.0f * t);
        }
        else if ((float) d.age > d.lifetimeSamples - d.fadeOutSamples)
        {
            float t = (d.lifetimeSamples - (float) d.age) / d.fadeOutSamples;
            t = juce::jlimit (0.0f, 1.0f, t);
            env = t * t * (3.0f - 2.0f * t);
        }

        // pitch chirp
        if (d.pitchRiseRate > 0.0f)
            d.currentFreq = juce::jmin (d.currentFreq + d.pitchRiseRate,
                                        (float) (sampleRate * 0.5));

        const float s = d.amplitude * std::sin (d.phase) * env;

        d.amplitude *= d.decay;   // natural exponential decay
        d.phase += juce::MathConstants<float>::twoPi * d.currentFreq / (float) sampleRate;
        if (d.phase >= juce::MathConstants<float>::twoPi)
            d.phase -= juce::MathConstants<float>::twoPi;

        ++d.age;

        // equal-power pan
        outL += s * std::cos (d.pan * juce::MathConstants<float>::halfPi);
        outR += s * std::sin (d.pan * juce::MathConstants<float>::halfPi);

        if ((float) d.age >= d.lifetimeSamples || d.amplitude < 1.0e-4f)
            d.active = false;
    }

    // Env In (when patched), velocity and Volume shape the stream's level
    const float gain = envIn * vel[v] * param (pVolume);
    outL *= gain;
    outR *= gain;

    // DC blocker (~5 Hz one-pole high-pass), same as the plugin
    const float dcCoeff = 0.995f;
    const float fL = outL - dcX1[v][0] + dcCoeff * dcY1[v][0];
    const float fR = outR - dcX1[v][1] + dcCoeff * dcY1[v][1];
    dcX1[v][0] = outL;  dcY1[v][0] = fL;
    dcX1[v][1] = outR;  dcY1[v][1] = fR;

    outputs[0][0] = fL;
    outputs[0][1] = fR;
}

//==============================================================================
static ModuleDescriptor dropletsDescriptor()
{
    ModuleDescriptor d;
    d.typeId = "osc.droplets";
    d.displayName = "Droplets";
    d.description =
        "The water-droplet generator from the Droplets plugin: randomised sine pings whose "
        "pitch, chirp and decay follow real bubble acoustics, drip-clocked at Rate with "
        "per-drop stereo spread. The stream runs while a note is held or Gate In is high - a "
        "Clock or Euclid gate works; Env In wants an ADSR and otherwise the drips play at "
        "full level and simply ring out when the stream stops.";
    d.section = ModuleSection::Oscillator;
    d.sidebarOrder = 18;
    d.sockets = {
        modIn    ("gateIn", "Gate In"),
        modIn    ("envIn",  "Env In"),
        audioOut ("audioOut", "Audio Out")
    };
    d.params = {
        makeRotary ("size",    "Size",     0.15f, 20.0f, 5.0f, 0, "mm", true),
        makeRotary ("sizeVar", "Size Var", 0.0f, 100.0f, 30.0f, 0, "%"),
        makeRotary ("rate",    "Rate",     4.0f, 2000.0f, 100.0f, 0, "ms", true),
        makeRotary ("rateVar", "Rate Var", 0.0f, 100.0f, 30.0f, 0, "%"),
        makeRotary ("rise",    "Rise",     0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("depth",   "Depth",    0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("bright",  "Bright",   0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("width",   "Width",    0.0f, 100.0f, 50.0f, 1, "%"),
        makeRotary ("volume",  "Volume",   0.0f, 1.0f, 0.5f, 2),
        makeRotary ("density", "Density",  1.0f, (float) DropletsModule::kMaxDroplets, 16.0f, 2, {}, false, 1.0f),
        makeRotary ("voices",  "Voices",   1.0f, (float) kMaxVoices, (float) kMaxVoices, 2, {}, false, 1.0f).noMod()
    };
    return d;
}

AQUANODE_REGISTER_MODULE (DropletsModule, dropletsDescriptor)
