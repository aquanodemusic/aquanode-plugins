# FullFilter

![FullFilter banner](assets/GUI.png)

FullFilter is a real-time playable filter. A bank of up to 128 bell (peaking) filters stacked on the harmonic series of a root frequency essentially form an EQ in the shape of a sawtooth, or square wave depending on the setting. It's polyphonic (up to 12 voices), MIDI-playable, and every harmonic can be individually reshaped by hand, by preset, or by scanning a wavetable across the whole harmonic stack.

---

## Manual

All you need to do to make MIDI work is to route MIDI into the plugin, for example using a MIDI Out Module in your DAW like FL Studio and set both plugins to the same MIDI channel.

FullFilter has one always-on "base voice" (the root frequency you play/turn the Root knob to) when the ADSR is off, plus extra polyphonic voices that activate as soon as more than one note is held.

### Core Processing Overview
The plugin builds its sound entirely from stacked bell filters, not oscillators:
*   A bank of peaking filters is placed on the harmonic series (1x, 2x, 3x... the root frequency), one per active bell.
*   Each bell's gain and frequency can be edited individually, mirrored across every voice.
*   Wavetable import re-analyzes those bell gains/frequencies by scanning a loaded wavetable's harmonic content.
*   Extra voices beyond the base voice are shaped by a per-voice ADSR envelope and mixed with automatic loudness compensation.

### Controls

| Control | Function |
| :--- | :--- |
| **Root** | Sets the base voice's root/fundamental frequency (20 Hz-2 kHz). Follows incoming MIDI notes. |
| **Level** | Overall output gain. |
| **Low Pass** | Global low-pass filter over the whole bell stack. |
| **Q** | Resonance/bandwidth of every bell filter. |
| **Amount** | How many of the 128 harmonic bells are active. |
| **Glide** | Portamento time between notes (monophonic base voice, meaning Glide is only active when Polyphony = 1 only). |
| **Polyphony** | Highest number of extra (non-base) voices that can sound at once. More voices will heavily increase CPU. |
| **Attack / Decay / Sustain / Release** | ADSR envelope for extra (polyphonic) voices. |
| **ADSR** | Toggles between the full ADSR envelope and a simplified attack-only/quick-release mode. |
| **Square Wave** | Mutes every 2nd harmonic (the even ones), turning the sawtooth-like stack into an odd-harmonics-only, square-wave one. |
| **Reset Freq** | Resets every bell's frequency multiplier back to 1x (no detune). |
| **Acoustic Preset** | Reshapes every bell's volume/frequency at once toward the idealized partial character of a struck object (Beam, Bell, Plate, Vibraphone, Marimba). |
| **Wavetable Import** | Loads a multi-cycle wavetable (.wav) and analyzes every frame's harmonic content. 2048 samples per cycle. |
| **Position** | Scans the loaded wavetable's frames (0 = first frame, 1 = last frame), interpolating harmonic strength between neighbors. |
| **Mode** | What the wavetable scan drives: bell Volume, bell Filter Position (frequency), or Both. |
| **Bar-Chart Editors** | Per-bell volume and frequency bars; click/drag to hand-edit any individual harmonic. |
