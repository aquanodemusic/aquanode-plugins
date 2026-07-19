# Aquanode Plugins

Free VST3 plugins and standalone apps for Windows, made with JUCE. The C++ source code is included for every plugin and should compile for Windows, Mac and Linux (I only provide Windows builds since I only have a Windows machine).

# Downloads:
All builds (standalone .exe and .vst3) are on the [Releases](../../releases) page. 
For te source code and manuals: https://github.com/aquanodemusic/aquanode-plugins/archive/refs/heads/main.zip

🌊 [bandcamp](https://aquanode.bandcamp.com) · [youtube](https://www.youtube.com/@aquanodemusic)

## Synthesizers

| Plugin | Description |
|--------|-------------|
| <img src="synths/aliassynth/assets/Icon.webp" width="64" align="center" /><br />**[AliasSynth](synths/aliassynth/)** | A synthesizer and sample player that deliberately has no anti-aliasing built in — frequencies above the Nyquist limit get reflected back into the audible range for unusual, harsh timbres, great for pads, stabs, and extreme sample repitching. |
| <img src="synths/alphabetasynth/assets/Icon.webp" width="64" align="center" /><br />**[AlphaBetaSynth](synths/alphabetasynth/)** | A simple synthesizer modelled after the free but discontinued LinPlug Alpha 3, with its characteristic liquid, smooth sweeping filter sound that washes out beautifully with the chorus. |
| <img src="synths/aquanode-modular/assets/Icon.png" width="64" align="center" /><br />**[Aquanode Modular](synths/aquanode-modular/)** | A free modular synthesizer with ~80 modules, cable patching, an euclidean sequencer, granulator, physically modelled drums, spectral effects, preset mutation — and full feedback routing, including self-FM. Inspired by the Nord Modular G2 and Wren. |
| 🧵<br />**[CombWeave](synths/combweave/)** | An experimental additive synth where each sine wave is spaced equidistantly, exponentially, or harmonically from the next, with rolloff, note-lock, and bidirectional mirroring controls. |
| <img src="synths/droplets/assets/Icon.webp" width="64" align="center" /><br />**[Droplets](synths/droplets/)** | A 64-bit VST3 recreation of the old 32-bit freeware plugin "Water" by xoxos — a physical water droplet model that creates individual bubble sounds, splashes, or river textures. |
| <img src="synths/fm12/assets/Icon.webp" width="64" align="center" /><br />**[FM12](synths/fm12/)** | An easy-to-use 12-operator PM/FM synthesizer with a flexible modulation matrix, user waveform import, and integrated stereo chorus for simple yet complex sound design. |
| ✏️<br />**[Frequency Drawer](synths/frequency-drawer/)** | An experimental draft plugin with a canvas on which you draw sound that is played back left to right, with two rendering engines, frequency blur reverb, and harmonics controls. |
| <img src="synths/grainfreeze/assets/Icon.webp" width="64" align="center" /><br />**[Grainfreeze](synths/grainfreeze/)** | A real-time phase-vocoder time-stretching and freeze processor: drag the playhead freely through a loaded file (backwards too), or freeze a tiny crossfaded slice in spectral freeze mode. |
| <img src="synths/granulate/assets/Icon.webp" width="64" align="center" /><br />**[Granulate](synths/granulate/)** | A granulator focused on the essentials: grain amount, position, spread, and duration, with MIDI resampling, click-to-granulate on the waveform, and visual grain playheads. |
| <img src="synths/noteadder/assets/Icon.webp" width="64" align="center" /><br />**[NoteAdder](synths/noteadder/)** | A MIDI plugin that analyzes the notes you play and adds extra notes around them in real time — play a note, get a chord; play a chord, get many. With scale/custom/cluster modes, animation modes, humanize, and MIDI recording. GPLv3. |
| <img src="synths/phizmosc/assets/Icon.webp" width="64" align="center" /><br />**[PhizmOsc](synths/phizmosc/)** | A 2-oscillator transwave synthesizer loosely inspired by the Ensoniq Fizmo hardware synth, with wavetable import, transwave envelopes, and Fizmo wavetables included. |
| 🌾<br />**[SampleField](synths/samplefield/)** | A 24-voice polyphonic sampler that continuously loops and chains random audio files from a pool of up to 128 samples for generative, evolving textures as long as a note is held. |
| <img src="synths/slicer/assets/Icon.webp" width="64" align="center" /><br />**[Slicer](synths/slicer/)** | A simple recreation of a well-known drum slicing plugin — slicing only, with no installers, online activations, or crowded UI. |

## Effects

| Plugin | Description |
|--------|-------------|
| <img src="effects/adelaysr/assets/Icon.webp" width="64" align="center" /><br />**[ADelaySR](effects/adelaysr/)** | A tempo-syncable stereo delay where every tap is shaped by a fade-in (Attack) and fade-out (Release) envelope — instead of abrupt echoes, each repeat blooms in and dissolves out for a washed-out delay character. |
| <img src="effects/alphabetafx/assets/Icon.webp" width="64" align="center" /><br />**[AlphaBetaFX](effects/alphabetafx/)** | The filter and chorus section of AlphaBetaSynth as an effect: run any signal through the liquid, smooth sweeping Alpha 3–style filter sound. |
| <img src="effects/anyfm/assets/Icon.webp" width="64" align="center" /><br />**[anyFM](effects/anyfm/)** | FM-modulates a sidechained signal onto a carrier signal — any sound can FM (or ring-modulate) any other sound directly in your mixer. |
| <img src="effects/aquaton-reverb/assets/Icon.webp" width="64" align="center" /><br />**[Aquaton Reverb](effects/aquaton-reverb/)** | A large reverb inspired by lush, non-natural-space reverbs like Blackhole or Supermassive, with free control over feedback, tank dispersion, bloom, HF wash, and many parameters other reverbs hide internally. |
| <img src="effects/audioreroute/assets/Icon.jpeg" width="64" align="center" /><br />**[AudioReroute](effects/audioreroute/)** | A versatile DAW-routing utility and feedback loop creator — send audio between tracks in ways your DAW normally won't allow. |
| <img src="effects/audiostretcher/assets/Icon.webp" width="64" align="center" /><br />**[AudioStretcher](effects/audiostretcher/)** | Independent time-stretching (0.1×–10×) and pitch-shifting (±36 semitones) powered by the Rubber Band Library, with region selection and lossless FLAC export. GPL v2. |
| <img src="effects/automorpheq/assets/Icon.webp" width="64" align="center" /><br />**[AutoMorphEQ](effects/automorpheq/)** | A VST3 port of my AutoMorph EQ Patcher preset for FL Studio: an automated morphing EQ with randomizer and multiple LFO modes. |
| <img src="effects/bandpass-modulator/assets/Icon.webp" width="64" align="center" /><br />**[Bandpass Modulator](effects/bandpass-modulator/)** | A VST3 port of my FL Studio / PlugData bandpass modulators: a randomly jumping, skew-modulated bandpass filter with note lock — from Filter FM to bubbly effects to mellow panning. |
| <img src="effects/centercomb/assets/Icon.webp" width="64" align="center" /><br />**[CenterComb](effects/centercomb/)** | An experimental effect using equidistantly spaced filters to create interesting tones (add a limiter — volume spikes happen!). Sibling of the CenterWeave synth concept. |
| <img src="effects/cepstralir/assets/Icon.webp" width="64" align="center" /><br />**[CepstralIR](effects/cepstralir/)** | Extracts the acoustic signature (impulse response) of any recording via cepstral processing — capture the character of a guitar, space, or effect chain as a reusable IR file. Pairs with IRConvolve. |
| ✂️<br />**[ClipPreserve](effects/clippreserve/)** | A detail-preserving clipper: it extracts the content lost to clipping via a polarity-inverted delta chain and blends it back in, so hi-hats survive on top of loud subbass. |
| <img src="effects/eq-octave-resonator/assets/Icon.webp" width="64" align="center" /><br />**[EQ Octave Resonator](effects/eq-octave-resonator/)** | An array of parallel note-tuned bandpass filters with selectable resonating octaves and notes, per-octave volume sliders, and a wet-only mode that cancels the incoming signal. |
| <img src="effects/irconvolve/assets/Icon.webp" width="64" align="center" /><br />**[IRConvolve](effects/irconvolve/)** | The companion convolver to CepstralIR: load impulse responses and imprint their captured character onto any signal. |
| <img src="effects/liquidchor/assets/Icon.png" width="64" align="center" /><br />**[LiquidChor](effects/liquidchor/)** | A recreation of a famous BBD chorus, focused on spacious, free-flowing chorus delay lines and "analog" noise hiss reminiscent of waves crashing ashore. |
| <img src="effects/notecontrol/assets/Icon.webp" width="64" align="center" /><br />**[NoteControl](effects/notecontrol/)** | The MIDI-controlled sibling of PitchControl: select which notes pass through via MIDI input instead of the piano interface. |
| <img src="effects/phorest/assets/Icon.webp" width="64" align="center" /><br />**[Phorest](effects/phorest/)** | A phaser modelled after a well-known stock DAW phaser but with much more extensive control ranges: more stages, extra LFO modes (including a flattened tanh sine), and a two-mode GUI. |
| <img src="effects/pitchcontrol/assets/Icon.webp" width="64" align="center" /><br />**[PitchControl](effects/pitchcontrol/)** | The typical "colorbass" effect: up to 120 bell filters (or an FFT bin-shifting mode) attenuate all frequencies except the notes you activate on the piano interface. |
| <img src="effects/pitchcontroleq/assets/Icon.webp" width="64" align="center" /><br />**[PitchControlEQ](effects/pitchcontroleq/)** | The EQ-flavored variant of PitchControl, closest to my original "PitchControl EQ" Patcher preset from the AutoMorph pack. |
| <img src="effects/precisionutility/assets/Icon.png" width="64" align="center" /><br />**[PrecisionUtility](effects/precisionutility/)** | A utility for artificial latency, phase inversion, and channel panning — precise signal alignment tools, designed to pair with SignalControl and ClipPreserve. |
| <img src="effects/resonate/assets/Icon.webp" width="64" align="center" /><br />**[Resonate](effects/resonate/)** | A 5-voice resonator modelled after a famous DAW resonator, recreated from scratch in JUCE — with a much broader note range than the original. |
| ~<br />**[SignalControl](effects/signalcontrol/)** | Draw control voltage curves and gate signals and output them as audio (CV on the left channel, gates on the right) — reliable modulation routing for DAWs where parameter outputs aren't. |
| <img src="effects/springer-spring-reverb/assets/Icon.webp" width="64" align="center" /><br />**[Springer Spring Reverb](effects/springer-spring-reverb/)** | An algorithmic stereo spring reverb built from dual interacting spring lines with dispersion, damping, feedback, and modulation — from dub-style springiness to phasing laser sounds. |
| <img src="effects/tapelectric/assets/Icon.webp" width="64" align="center" /><br />**[TapElectric](effects/tapelectric/)** | A tape deck simulation adding warm electric hum and white tape noise — less about transforming the sound (though saturation and wobble are there) and more about the imperfect tape atmosphere. |
| <img src="effects/vocode/assets/Icon.webp" width="64" align="center" /><br />**[Vocode](effects/vocode/)** | A classic filter-bank vocoder with up to 256 bands: modulator envelopes gate the carrier's bands for the characteristic "talking synthesizer" effect, or richly self-gated robotic textures without a sidechain. |

## Spectral Suite

A collection of effect plugins (and one synth) built on a shared FFT-based engine for real-time spectral analysis and manipulation — especially suited for colorbass, botanica, and experimental sound design. See the [suite overview](spectral/) for details.

| Plugin | Description |
|--------|-------------|
| <img src="spectral/assets/Icon.webp" width="64" align="center" /><br />**[SpectralCompare](spectral/spectralcompare/)** | A live spectrogram with sidechain overlay, delta display, spectral EQ drawing, morphing/vocoding-style processing, rolling spectrogram mode, and a sharp spectral reassignment view — analysis and shaping in one interface. |
| **[SpectralEdit](spectral/spectraledit/)** | A synth that loads up to 1 minute of audio into an editable spectrogram: select, rotate, flip, blur, draw, compress, and scrub spectral content, then play it back repitched via MIDI. |
| **[SpectralEnhance](spectral/spectralenhance/)** | Same controls as SpectralGate, but frequencies are pulled *towards* the gate line instead of deleted — a spectral brickwall compressor, upwards or downwards. |
| **[SpectralFilter](spectral/spectralfilter/)** | Draw custom filter curves (±24 dB) directly onto the live spectrum, with multiple FFT resolutions, a random curve generator, per-bin frequency/phase/panning control, IR export, and a fully color-customizable UI. |
| **[SpectralGate](spectral/spectralgate/)** | Frequency-selective gating: two vertical frequency bounds and a drawable amplitude threshold decide what passes, with invert mode and tilt EQ — for band carving, noise removal, and creative filtering. |
| **[SpectralLatency](spectral/spectrallatency/)** | Spectral delay over up to 8192 FFT bins: draw a delay curve along the frequency range — including "negative" delay that sends the rest of the sound out earlier. |
| **[SpectralResolve](spectral/spectralresolve/)** | A high-resolution reassignment-based spectral analyzer focused on sharp low-frequency inspection, with FFT sizes up to 16384, decimation for enhanced bass resolution, and a sidechain comparison path. |
| **[SpectralStereoize](spectral/spectralstereoize/)** | A spectral Mid/Side processor: draw stereo width curves across the frequency spectrum, from full mono collapse to extreme expansion per frequency range, with sidechain reference display. |

## Apps

| App | Description |
|-----|-------------|
| <img src="simpledaw/assets/Icon.webp" width="64" align="center" /><br />**[SimpleDAW](simpledaw/)** | A very simple standalone DAW that can load 64-bit Windows .vst3 plugins — a minimal host for sketching and testing. Has many flaws and bugs in it, but it is usable. |

## Repository Structure

Every plugin folder follows the same layout:

```
plugin-name/
├── README.md      description, controls, version history
├── src/           JUCE/C++ source code of the current version
├── archive/       source snapshots of previous versions (v0.1, v1.0, ...)
├── assets/        icons, banners, GUI screenshots, HTML backgrounds
├── demos/         sound examples, videos, example FL Studio projects
├── docs/          PDF/ODT manuals (only where they exist)
└── presets/       factory presets (only where they exist)
```

Most plugins were made with the help of Claude AI. Unless noted otherwise in the plugin folder (NoteAdder is GPLv3, AudioStretcher is GPL v2 due to Rubber Band), see the repository license. Have fun with the plugins! — Aquanode
