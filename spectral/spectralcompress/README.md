# SpectralCompress

SpectralCompress by aquanode

SpectralCompress is heavily inspired by [robbert-vdh's Spectral Compressor](https://github.com/robbert-vdh/nih-plug), with the addition that you can freely draw the **target curve** with your mouse instead of relying on a polynomial curve filter by default: In the original, the level that every FFT bin is compressed towards comes from a global polynomial you shape with threshold, center frequency, slope and curve knobs. Here that polynomial is gone — you simply draw the curve across the spectrum, one value per FFT bin, exactly like in my other, more simple plugins SpectralFilter and SpectralCompare. Everything the compressors do is measured relative to that drawn line.

---

## How it works

The signal is cut into overlapping FFT frames. Every bin gets its own envelope follower and its own compressor. If a bin sits above your drawn curve the downwards compressor pulls it down; if it sits below, the upwards compressor pushes it up. Draw a flat line and the plugin flattens the spectrum towards it. Draw a shape and the sound is squeezed into that shape.

Because the drawn curve replaces the threshold polynomial, the **Offset** knobs still do what they did in the original: they move each direction's threshold up or down relative to the curve. **Ratio** and **Knee** are unchanged.

---

## Controls

### Global

| Control | Type | Function |
| :--- | :--- | :--- |
| **Main Display Area** | Interactive | Click and drag to draw the target curve. Alt-drag draws a flat line, right-drag erases back to the default, double-click resets the whole curve. |
| **Visual Smooth** | Slider | Smoothing of the analyser display only. The spectrum can move very fast at small FFT sizes — turn this up to calm it down. Never affects the audio. |
| **FFT Size** | Dropdown | 512, 1024, 2048, 4096 or 8192. Bigger means finer frequency resolution and more latency. |
| **Overlap** | Dropdown | 2x, 4x, 8x or 16x. 4x and above are recommended; 2x has some amplitude ripple. |
| **Mode** | Dropdown | See below. |
| **Gain** | Knob | Output gain, ±30 dB. |
| **Mix** | Knob | Dry/wet. The dry path is delay-compensated so it stays phase aligned. |
| **Attack** | Knob | Attack time of the per-bin envelope followers. |
| **Release** | Knob | Release time of the per-bin envelope followers. |

### Downwards / Upwards

Both banks are identical and run at the same time. Set a ratio to *off* to disable that direction.

| Control | Function |
| :--- | :--- |
| **Offset** | Moves this direction's threshold relative to the drawn curve, ±40 dB. |
| **Ratio** | Compression ratio, up to 300:1. At *off* (1:1) the bank does nothing. |
| **Knee** | Soft knee width in dB around the threshold. |
| **Amount** | How much of this direction's gain change is actually applied, 0–100%. |

### Sidechain / Morph

| Control | Function |
| :--- | :--- |
| **Match** | In *Sidechain Match* mode, how far the target curve morphs from your drawn curve towards the sidechain's spectrum. |
| **Morph** | Spectral envelope transfer, as in SpectralCompare: the input takes on the sidechain's spectral shape while keeping its own fine structure. 0 = off, 1 = full, 2 = exaggerated. Works in every mode. |
| **Clarity** | Width of the envelope averaging used by Morph. Right = sharpest and most detailed, left = broad and smooth. |
| **SC Link** | How much the sidechain channels are mixed together before being used. |
| **ST Link** | Stereo linking of the main detector. At 100% both channels are compressed identically, which keeps the stereo image stable. |

### Curve

| Control | Function |
| :--- | :--- |
| **Shift** | Rotates the drawn curve across the bins, wrapping from one end to the other, ±100%. |
| **Reset** | Flattens the curve back to its default level. |
| **Learn** | Snaps the curve onto the spectrum currently playing. A good starting point: play your material, hit Learn, then use the Offsets. Recommended is a rather loud, full sound for impactful changes, but subtle changes can also be very helpful. |
| **Tilt − / Tilt +** | Tilts the whole curve by ∓1.5 dB per octave. Two presses of **Tilt −** gives you roughly the pink-noise slope the original used by default. |
| **Delta** | Delta	Outputs only the difference between the processed and the original signal, so you hear exactly what the compressors are adding or removing and nothing else. Very useful for dialling in the Offsets and Ratios: turn it on, tune until the difference sounds like the material you want to act on, then turn it off. The Mix knob scales how much of the difference is sent out; Mix is otherwise bypassed in this mode. A red DELTA tag appears in the display while it is active. The Visualizer is unchanged when Delta is active so you still see how the signal originally was shaped. |
| **Sidechain / Reduction / Output** | Show or hide those overlays in the display. |
| **Light** | White background mode. |

---

## Modes

| Mode | What it does |
| :--- | :--- |
| **Level Curve** | The envelope followers watch the input and every bin is compressed towards your drawn curve. This is the main mode: draw the spectrum you want and the plugin levels the material into it. |
| **Sidechain Match** | The target curve morphs from your drawn curve towards the sidechain's own spectrum, controlled by the **Match** knob. At 100% the input is compressed into the sidechain's spectral shape; in between you get a blend of your drawing and the sidechain. |
| **Sidechain Compress** | The envelope followers watch the *sidechain* instead of the input, while the thresholds still come from your drawn curve. This is spectral ducking: wherever the sidechain is loud, those exact frequencies get pulled out of the input. Great for making room for a vocal or a kick without touching the rest of the spectrum. |

The **Morph** knob is independent of the mode and always uses the sidechain, so you can for example duck with *Sidechain Compress* and transfer the envelope at the same time.

---

## Sidechain routing in FL Studio

The two sidechain modes and the Morph knob need a second signal fed into the plugin's sidechain input. In FL Studio:

1. Put the source you want to process (say a pad) on **Mixer Insert 1**, and load SpectralCompress on it.
2. Put the signal you want to use as the sidechain (say a kick, or a texture you want to morph towards) on **Mixer Insert 2**.
3. Select **Insert 2**, then right-click the little arrow at the bottom of **Insert 1** and choose **Sidechain to this track**. A small circle appears on Insert 2's send, meaning it is now sending to Insert 1 as a sidechain rather than as audio.
4. Open SpectralCompress, click the plugin wrapper's **cog icon** at the top left, go to the **Processing** tab, and set the **Sidechain input** for the plugin to the sidechain send you have created (it will be listed as *Sidechain 1*).
5. If you only want the sidechain to control the plugin and not be heard, turn Insert 2's own fader down or route it away from the Master — the sidechain send still works.

The sidebar shows you whether it worked: it reads *Sidechain connected* once signal is arriving.

For other DAWs the idea is the same — enable the plugin's sidechain input and route the second track into it. In Ableton Live and Bitwig you pick the source in the plugin's sidechain dropdown; in REAPER you route the source track to channels 3/4 of the SpectralCompress track and set the plugin's pin mapping accordingly.

---

## Display legend

The legend in the top right of the display lists whatever is currently visible:

- **Input spectrum** — what is coming in (after Morph, if Morph is active).
- **Output** — the input plus the gain change, so the two lines together show what the compressors are doing.
- **Turned down / Turned up** — per-bin gain reduction, drawn as shaded bars between the input and the output.
- **Sidechain** — the sidechain spectrum, in the sidechain modes and when morphing.
- **Target curve (drawn)** — the curve you drew, after Shift.
- **Downwards / Upwards threshold** — dashed lines showing where each bank actually starts working, i.e. the curve plus that direction's Offset.

Hovering anywhere over the display shows the frequency, bin number, input level, curve level and gain reduction for that bin.
