#include "VaporizationPatch.h"
#include "../PluginProcessor.h"

namespace aquanode::presets
{

juce::String buildVaporizationPatch (AquanodeModularAudioProcessor& p)
{
    PatchSketch s (p);

    // ---- layer 1: Dual VCO, its filters bubbling from the S&H -----------------
    const int noise = s.add ("osc.noise", 0);
    s.setChoice (noise, "type", "Pink");
    s.set (noise, "level", 0.6f);

    const int lfo = s.add ("util.skewlfo", 0);
    s.set (lfo, "rate", 0.878f);
    s.set (lfo, "waveform", 10.0f);
    s.setChoice (lfo, "sync", "Tempo");
    s.setChoice (lfo, "division", "1/4");

    const int sh = s.add ("util.samplehold", 0);
    s.set (sh, "glide", 0.0f);

    const int vco = s.add ("osc.dualvco", 1);
    s.setChoice (vco, "wave1", "Saw");
    s.setChoice (vco, "scale1", "8'");
    s.setChoice (vco, "wave2", "Saw");
    s.set (vco, "pitch2", 7.0f);
    s.setChoice (vco, "scale2", "8'");
    s.set (vco, "level1", 2.65f);
    s.set (vco, "level2", 2.55f);
    s.set (vco, "analog", 50.0f);
    s.set (vco, "voices", 8.0f);

    const int eg2 = s.add ("util.analogeg", 1);
    s.set (eg2, "attack", 13.2f);
    s.set (eg2, "decay", 100.0f);
    s.set (eg2, "sustain", 1.0f);
    s.set (eg2, "release", 126.0f);

    const int flt = s.add ("filter.dualfilter", 2);
    s.set (flt, "hpfCutoff", 300.0f);
    s.set (flt, "hpfPeak", 6.1f);
    s.set (flt, "hpfExt", 8.2f);
    s.set (flt, "lpfCutoff", 300.0f);
    s.set (flt, "lpfPeak", 6.0f);
    s.set (flt, "lpfMg", 3.45f);

    const int vca1 = s.add ("util.volume", 3);
    s.set (vca1, "level", 0.33f);
    const int sum1 = s.add ("util.polysum", 4);
    s.set (sum1, "amount", 0.5f);
    const int eq = s.add ("filter.shelfeq", 5);
    s.set (eq, "lowFreq", 20.5f);
    s.set (eq, "lowGain", 8.0f);
    s.set (eq, "highFreq", 20000.0f);
    s.set (eq, "highGain", 8.2f);
    const int send1 = s.add ("util.volume", 6);        // layer 1 -> delay, post-fader
    s.set (send1, "level", 0.578f);

    s.wire (noise, "audioOut", sh, "sigIn");
    s.wire (lfo, "pulseOut", sh, "clockIn");
    s.wire (sh, "modOut", flt, "hpfExt");
    s.wire (lfo, "triOut", flt, "lpfMg");
    s.wire (vco, "audioOut", flt, "audioIn");
    s.wire (flt, "audioOut", vca1, "audioIn");
    s.wire (eg2, "modOut", vca1, "volMod");
    s.wire (vca1, "audioOut", sum1, "audioIn");
    s.wire (sum1, "audioOut", eq, "audioIn");
    s.wire (eq, "audioOut", send1, "audioIn");

    // ---- layer 2: PWM pad ------------------------------------------------------
    const int padLfo = s.add ("util.skewlfo", 0);
    s.set (padLfo, "rate", 0.178f);
    s.set (padLfo, "waveform", 5.0f);

    const int osc = s.add ("osc.pwmosc", 1);
    s.setChoice (osc, "octave", "8'");
    s.setChoice (osc, "wave", "PWM");
    s.set (osc, "pw", 10.0f);
    s.set (osc, "pwmSpeed", 3.8f);
    s.setChoice (osc, "sub", "Off");
    s.set (osc, "analog", 50.0f);

    const int eg = s.add ("util.analogeg", 1);
    s.set (eg, "attack", 43.7f);
    s.set (eg, "decay", 1148.0f);
    s.set (eg, "sustain", 0.645f);
    s.set (eg, "release", 2000.0f);

    const int lad = s.add ("filter.warmladder", 2);
    s.set (lad, "cutoff", 2500.0f);
    s.set (lad, "resonance", 6.0f);
    s.set (lad, "egInt", 1.667f);
    s.set (lad, "kbdTrack", 26.5f);
    s.set (lad, "mgLevel", 2.3f);

    const int vca2 = s.add ("util.volume", 3);
    s.set (vca2, "level", 0.327f);
    const int sum2 = s.add ("util.polysum", 4);
    s.set (sum2, "amount", 0.5f);
    const int ens = s.add ("fx.ensemble", 5);
    s.set (ens, "speed", 3.5f);
    const int ph = s.add ("fx.stereophaser", 5);
    s.set (ph, "rate", 0.0303f);
    s.set (ph, "lrPhase", 180.0f);
    s.set (ph, "manual", 12782.0f);
    s.set (ph, "depth", 43.0f);
    s.set (ph, "resonance", 80.0f);
    s.set (ph, "highDamp", 20000.0f);
    s.set (ph, "dryWet", 75.0f);
    const int fader2 = s.add ("util.volume", 6);
    s.set (fader2, "level", 0.36f);
    const int send2 = s.add ("util.volume", 6);       // pad -> delay
    s.set (send2, "level", 1.549f);
    const int send3 = s.add ("util.volume", 6);       // pad -> hall
    s.set (send3, "level", 1.549f);

    s.wire (osc, "audioOut", lad, "audioIn");
    s.wire (eg, "modOut", lad, "egIn");
    s.wire (padLfo, "triOut", lad, "mgIn");
    s.wire (lad, "audioOut", vca2, "audioIn");
    s.wire (eg, "modOut", vca2, "volMod");
    s.wire (vca2, "audioOut", sum2, "audioIn");
    s.wire (sum2, "audioOut", ens, "audioIn");
    s.wire (ens, "audioOut", ph, "audioIn");
    s.wire (ph, "audioOut", fader2, "audioIn");
    s.wire (fader2, "audioOut", send2, "audioIn");
    s.wire (fader2, "audioOut", send3, "audioIn");

    // ---- sends and mix ---------------------------------------------------------
    const int dly = s.add ("fx.crossdelay", 7);
    s.set (dly, "timeL", 750.0f);
    s.set (dly, "timeR", 770.0f);
    s.set (dly, "feedbackL", 28.0f);
    s.set (dly, "feedbackR", 26.0f);
    s.set (dly, "highDamp", 20000.0f);
    s.set (dly, "lowDamp", 20.0f);
    s.set (dly, "dryWet", 100.0f);
    const int hall = s.add ("fx.smoothhall", 7);
    s.set (hall, "time", 3.63f);
    s.set (hall, "preDelay", 120.0f);
    s.set (hall, "highDamp", 20000.0f);
    s.set (hall, "lowGain", 2.8f);
    s.set (hall, "highGain", 4.0f);
    s.set (hall, "dryWet", 100.0f);

    const int mix = s.add ("util.mixer", 8);
    const int out = s.add ("io.audioOut", 8);
    s.set (out, "level", 0.8f);

    s.wire (send1, "audioOut", dly, "audioIn");
    s.wire (send2, "audioOut", dly, "audioIn");
    s.wire (send3, "audioOut", hall, "audioIn");
    s.wire (eq, "audioOut", mix, "in1");
    s.wire (fader2, "audioOut", mix, "in2");
    s.wire (dly, "audioOut", mix, "in3");
    s.wire (hall, "audioOut", mix, "in4");
    s.wire (mix, "audioOut", out, "audioIn");

    s.layout();
    return "Vaporization Pad";
}

} // namespace aquanode::presets
