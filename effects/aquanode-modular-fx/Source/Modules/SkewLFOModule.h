#pragma once

#include "ModuleCore.h"
#include "VintageDSP.h"

// Skew LFO - a modulation generator. One Waveform knob skews the
// triangle from falling saw (0) through triangle (5) to rising ramp (10), and
// the same knob sets the duty cycle of the Pulse Out - which is what you clock
// an S&H with. Free-running and shared by every voice (global), as on the
// classic analog synth; optionally locked to the host tempo.
// No inputs. Outputs: 0 = Tri Out (bipolar), 1 = Pulse Out (0 / 1).
class SkewLFOModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pRate = 0, pWaveform, pSync, pDivision, pLevel };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::Global; }

    void prepare (double sr) override
    {
        SynthModule::prepare (sr);
        reset();
    }

    void reset() override { mg.reset(); }

    void processSample (const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        double hz = param (pRate);
        if ((int) param (pSync) == 1)
        {
            const double bpm = hostTransport.hasBpm ? hostTransport.bpm : (tempoBpm > 1.0 ? tempoBpm : 120.0);
            hz = bpm / 60.0 / vintagedsp::divisionBeats ((int) param (pDivision));
        }

        const auto o = mg.process (hz, param (pWaveform) * 0.1f, sampleRate);
        const float tri = o[0] * param (pLevel);
        outputs[0] = { tri, tri };
        outputs[1] = { o[1], o[1] };
    }

private:
    vintagedsp::SkewLfo mg;
};
