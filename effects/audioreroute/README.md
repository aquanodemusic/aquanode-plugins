# AudioReroute

![AudioReroute banner](assets/Banner.webp)

**Latest version:** 1.2 — download builds from the [Releases](../../../../releases) page.

Audio Rerouter - Versatile DAW-Routing / Feedback Loop Creator VST (Free Windows VST, Open Source for Win/Mac/Linux)

Audio Rerouter lets you send audio from one plugin instance of it to another within the same DAW session. Next to much more flexibility for routing audio, the main use case is creating feedback loops, where a receiver early in your audio / FX chain receives signals from a sender further down the line.

See it in action:

To use it, place two instances of the plugin in your session. One instance is set to Receive, the other to Send on the same channel. You have up to 8 channels which the plugin can store audio in to transmit it.

The plugins communicate via a shared ring buffer on the selected channel. One sender and multiple receivers works perfectly, multiple senders on the same channel may lead to crackling sounds due to buffer overwrites.

The Limiter (Clipper) prevents runaway feedback. Feedback will still definitely happen when the limiter is on, but it will clip its volume. A warning is shown when you try to disable it. It has a slightly different behaviour depending on whether the sender or receiver is limited. Limiting the sender hard clips the sound, limiting the receiver sounds smoother. This is because feedback energy passing through a receiver limiter has already lost its sharpest edges, making the limiting feel more like gentle compression than hard clipping.

Dry controls how much of the Receive instance's input signal (e.g. a synth) is blended into the output. This is of course necessary to seed or sustain a feedback loop. Wet, the returned ring buffer signal, can go up to 200% controlling the "speed" of the feedback buildup.

The Plugin also has an automatic feedback control in the top right corner (in a previous version, there was a plugin delay compensator for latency handling, but this one was in fact not necessary). With the automatic feedback controller active, it takes over the control of the wet knob and dials in exactly the right amount to sustain your feedback loop without it becoming too agressive. So, the loop can only ever go to silence, but never drastically exceed your limiter's amount.

## Recommended Companion Effects

- **Circulate** by GullDSP: Its Phase Smearing adds "liquid" sounding characters to the signal.
- **SpectralCompressor** by Robbert-vdh: Spectral Up- and Downwards Compression enables you to better hear the original signal. Far less sine wave / square wave / noise buildup will occur.
- **KiloHearts Free Effects Bundle**: Especially the Pitch Shifter. Together with a reverb, you get a shimmer reverb with this, as each loop the pitch is additionally changed from the last step.
- A few of my own plugins, like **PrecisionUtility** to add artificial latency, turning it into a feedback delay network, or **ClipPreserve** so you hear more frequencies which would have been cut off otherwise by the limiter.

## Notes

1. The plugin only links instances within the same DAW session. Cross-DAW routing, like from FL Studio to Ableton and back to FL Studio or something, would require a virtual audio driver which this plugin is not about. The advantage my plugin provides over these driver methods is very low latency, only up to 1 block (usually around 12 ms).
2. Some DAWs like FL Studio uses variable buffer sizes. The plugin still works in these DAWs, but may sound more like a delay effect rather than a feedback effect. This is nice too, but for the intended sound you may need to activate fixed buffer sizes (a setting in FL Studio's Wrapper around each VST) for both sender and receiver. Then it also sounds correct in these DAWs.
3. If you use old 32 bit plugins in a 64 bit daw or otherwise sandboxed plugins, my plugin will not work - sandboxing changes the cpu process the VST works in, which breaks the connection between AudioReroute instances. In normal operations though, this will not be the case.

## Manual

### Basic Feedback Setup

1. Insert AudioRerouter in Receiving Mode on a mixer track with Dry > 0.
2. Add some FX after it.
3. Add AudioRerouter in Send Mode after the FX on the same channel the Receiver works on.
4. Control the Feedback amount with the Wet Knob.

Like this (in FL Studio):

### Version History

- **1.2** — Removed PDC as it was not needed in fact. Replaced it with the Automatic Feedback Controller.
- **1.1** — Added PDC (Plugin Delay Compensation) On/Off Toggle. Might align the sound better.
- **1.0** — Initial Release.

Thanks for checking it out!
