# ClipPreserve

![ClipPreserve banner](assets/Banner.webp)

**Latest version:** 1.1 — download builds from the [Releases](../../../../releases) page.

ClipPreserve Detail Preserving Clipper / PrecisionUtility Artificial Latency, Phase Inversion and Channel Panning Utility VSTs (Free and Open Source)

ClipPreserve is a detail-preserving clipper plugin. A signal that is pushed past its threshold usually clips and does not keep higher frequency content, for example HiHats that travel on a loud subbass for example. ClipPreserve extracts the clipped contents, isolates them and blends them back into the final output. It does so using a "Delta Chain", a polarity inverted signal. This means that you don't even need sidechaining tools or similar to restore the signal. The plugin has a sidechain knob, but this is for loud sidechain signals that - while they are louder than the remaining signal - further boost the quieter signals.

Precision Utility is a stereo utility plugin for precise audio manipulation. It provides independent control over signal delay, phase inversion, and stereo panning with bit-perfect pass-through at default settings (i.e. a zero delay does not change anything in the audio). Signal delay is not to be confused with a delay effect - in this plugin, the signal is delayed as if it had latency.

It features a delay (0 - 10000 ms) to introduce time delay to the audio signal with 0.1 ms precision as artificial latency (which is deliberately NOT reported to your DAW), useful for phase alignment, timing correction or comb filtering effects that sound similar to Pulse Width Modulation due to phase cancellations if you add it on a sidechain channel.

You can also phase invert the signal by any percent you like (0 to 100%), where 50% is complete cancellation and 100% is fully flipped for aligning phases, cancel out only specific sounds or convert ramp up signals to ramp down signals.

Furthermore, the left and right channels can be panned individually towards the center or the respective other channel without affecting the other.

Technical specifications are Sample-accurate delay processing, Independent delay buffers per channel (no channel crosstalk), Up to 10 seconds of artificial signal delay, Zero latency at 0 ms delay, Bit-perfect pass-through at default settings, Stereo processing (2 channels), Deliberately NO reporting of latency and no trigger of global plugin delay compensation to the host / DAW.

## Manual

ClipPreserve is a detail-preserving clipper plugin. A signal that is pushed past its threshold usually clips and does not keep higher frequency content that travels on a loud subbass for example. ClipPreserve extracts the clipped transients, isolates their higher-frequency details (in this example), and blends them back into the final output. This gives you aggressive peak-taming while keeping your transients crisp.

### Signal Flow

- **Drive**: Boosts the incoming signal gain.
- **Hard Clip**: Transparently chops off any audio extending past your Threshold.
- **Delta Extraction**: The plugin captures only the parts of the waveform that were clipped away (the "overflow").
- **High-Pass Filter**: The overflow is run through a cascaded 12dB/Oct high-pass filter (HP Freq) to keep only the sharp, top-end detail. Reduce this to match your quiet signal's frequency range.
- **Low Pass Filter**: Same.
- **Preserve (Foldback)**: The filtered high-frequency details are added back to the clipped signal, recovering transient clarity.
- **Output**: Applies final clean makeup or attenuation gain.

### Interface & Controls

ClipPreserve features a 7-knob layout split into four dashboard zones, with three toggles in the header.

**1. Clipper Section**
- DRIVE (0.0 to +40.0 dB) controls the input gain pushed into the clipping engine.
- THRESHOLD (-24.0 to 0.0 dBFS) sets the ceiling where hard clipping begins. Pull this down to aggressively clamp down on quieter transients.

**2. Preserve Section**
- PRESERVE (0% to 100%) tweaks the "Foldback" amount, which is the percentage of clipped high-frequency detail re-introduced into the final output.
- HP FREQ (500 Hz to 20.0 kHz) sets the cutoff frequency for the extracted clipped audio. Higher values ensure only crisp, high-end air and transient cracks are preserved.
- LP FREQ (200 Hz to 20.0 kHz) same but for low frequency content.

**3. Sidechain Section**
- SC GAIN (0% to 400%) Controls how strongly an external sidechain signal modulates the preserve amount. When a loud sidechain signal (e.g. a drum bus routed pre-fader) is connected, the plugin boosts the foldback at exactly those transient moments. At 0% the sidechain has no effect. The sidechain source should be gain-staged close to 0 dBFS for meaningful modulation (meaning you need a LOUD sidechain). Without a sidechain connected, this knob does nothing.

**4. Gain Section**
- OUTPUT (-12.0 to +12.0 dB) controls the final master volume of the plugin after all clipping and reconstruction is finished.

**Header Toggles**
- **HP FILTER**: Enables the high-pass filter on the extracted overflow. When off, the full-band overflow is folded back unfiltered from below.
- **LP FILTER**: Enables the low-pass filter on the extracted overflow. When off, the overflow passes through unfiltered from above.
- **2x OVERSAMPLE**: Enables 2x oversampling to reduce aliasing artifacts introduced by the hard clipper. Slightly increases CPU. If you like aliasing artefacts like me, you don't need to activate this - this plugin deals with loud and harsh frequencies anyway (in general, feel free to get creative).

Thanks for checking it out! It is free and open source.
— aquanode
