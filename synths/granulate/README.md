# Granulate

![Granulate banner](assets/Banner.webp)

**Latest version:** 1.4 — download builds from the [Releases](../../../../releases) page.

Granulate is a Granulator VST Plugin (and Standalone .exe) modelled after a classic, now-unavailable granulator plugin. It provides a sample playback engine with controls for grain density, position, spread, and duration. Users can trigger grains via MIDI notes for resampling or by clicking directly on the loaded waveform. A visual representation of grains as small playback heads is provided to aid in sound design.

The plugin supports audio files up to 3 hours long, with files over 1 hour being considered experimental due to memory requirements. Key features include 12 voices of polyphony, project state restoration, and a customizable sidebar for GUI colors and preset management. Additionally, Granulate includes a "Raw Data Import" feature that allows users to treat any file (such as images or text) as audio, which may result in white noise or unexpected experimental timbres.

---

## Manual

Granulate focuses on the fundamental elements of granular synthesis.

### Controls

| Control | Function |
| :--- | :--- |
| **Load Sample** | Opens a file browser to import a supported audio file. |
| **Waveform Display** | Visualizes the sample and allows you to click or drag to set the playback position. |
| **Grains** | Controls the number or density of grains playing simultaneously. |
| **Size** | Sets the duration of each individual audio grain. |
| **Position** | Selects the point in the sample buffer where grains are grabbed from. |
| **Spray** | Adds random variation to the playback position to create a wider texture. |
| **Window** | Defines the width of the playable area around the current position cursor. |
| **Rev Grains** | Sets how many grains are played in reverse (if the set amount exceeds total grains, all play in reverse). |
| **G-ADSR** | Attack / Decay / Sustain / Release; shapes the volume envelope of each individual grain. |
| **N-ADSR** | Shapes the volume envelope of the overall MIDI note. |
| **AM** | Applies amplitude modulation to the grains. |
| **AM Disp** | Adds random variation to the amplitude modulation. |
| **Pitch Disp** | Adds random pitch variation to the grains. |
| **Pitch (Mouse)** | Manually changes the playback pitch offset for the mouse click mode. |
| **Stereo** | Randomizes the panning of grains to widen the stereo image. |
| **Volume** | Controls the master output level of the plugin. |
