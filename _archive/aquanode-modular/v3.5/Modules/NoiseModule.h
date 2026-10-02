#pragma once

#include "ModuleCore.h"
#include <cstdint>

// Noise - white/pink noise source. PerVoice (like the G2's voice area):
// each held note gets its own independent noise stream, so a noise burst
// gated by an ADSR behaves per note - the classic percussion / pluck
// excitation source (try Noise -> Resonator).
// Note: as a per-voice source it sounds while notes are held.
// No inputs. Output: 0 = Audio Out.
class NoiseModule : public aquanode::SynthModule
{
public:
    enum ParamIndex { pType = 0, pLevel };

    aquanode::VoiceMode voiceMode() const override { return aquanode::VoiceMode::PerVoice; }

    void reset() override
    {
        for (int v = 0; v < aquanode::kMaxVoices; ++v)
            voiceReset (v);
    }

    void voiceNoteOn (int v, int, bool) override { pool.noteOn (v, voiceLimit()); }
    void voiceNoteOff (int v) override { pool.noteOff (v, voiceLimit()); }

    void voiceReset (int v) override
    {
        pool.resetVoice (v);
        seed[v] = 0x9e3779b9u * (unsigned) (v + 1);
        b0[v] = b1[v] = b2[v] = 0.0f;
    }

    void processVoiceSample (int v, const aquanode::StereoFrame*, aquanode::StereoFrame* outputs) override
    {
        // voice-steal de-click: ramp out instead of cutting dead
        const float voiceGain = pool.nextGain (v, sampleRate);
        if (pool.isSilent (v))
        {
            outputs[0][0] = 0.0f;
            outputs[0][1] = 0.0f;
            return;
        }

        // fast LCG white noise, independent per voice
        seed[v] = seed[v] * 1664525u + 1013904223u;
        float white = (float) (int32_t) seed[v] / (float) 0x80000000;

        float out = white;
        if ((int) param (pType) == 1)
        {
            // pink: Paul Kellet's economy 3-pole approximation
            b0[v] = 0.99765f * b0[v] + white * 0.0990460f;
            b1[v] = 0.96300f * b1[v] + white * 0.2965164f;
            b2[v] = 0.57000f * b2[v] + white * 1.0526913f;
            out = (b0[v] + b1[v] + b2[v] + white * 0.1848f) * 0.35f;
        }

        out *= param (pLevel) * voiceGain;
        outputs[0][0] = out;
        outputs[0][1] = out;
    }

private:
    aquanode::ModuleVoicePool pool;
    uint32_t seed [aquanode::kMaxVoices] { };
    float b0 [aquanode::kMaxVoices] { }, b1 [aquanode::kMaxVoices] { }, b2 [aquanode::kMaxVoices] { };
};
