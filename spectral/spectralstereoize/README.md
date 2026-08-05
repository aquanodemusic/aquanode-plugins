# SpectralStereoize

Download builds from the [Releases](../../../../releases) page.

---

## 📖 Overview

SpectralStereoize is a spectral Mid/Side processor. It decomposes an incoming stereo signal into its Mono (Mid) and Stereo (Side) components. Using the interactive graphical interface, you can surgically edit the stereo width for every individual frequency band from an FFT.

> 💡 **Audio Integrity Note:** Converting your stereo signal into Mid/Side components is mathematically safe and happens entirely in the FFT realm; the output is reconstructed as natural stereo. If the Width Curve is set to `1.0` (default), the plugin outputs the exact same audio bit-for-bit. Because the processing is spectral, phase relationships remain secure even when applying surgical width changes.

---

## 🦋 The Butterfly Plot (Visualization)

The main interface features a dual-color butterfly plot to help you visualize your stereo field and reference tracks:
* **Green Area:** Represents your **Processed Stereo Output**. The further it spreads from the center line, the wider the sound is at that specific frequency.
* **Pink Area:** Represents the **Sidechain Input** reference (if you route external audio into the plugin; off by default). This is perfect for seeing where other instruments (like a lead vocal) sit so you can move your stereo energy out of their way.
* **Horizontal Center Line:** The **Mono Axis**. A perfectly mono signal will appear only as a flat line sitting exactly on this axis.

---

## 🎛️ Controls & Settings

| Control Area | Type | Function |
| :--- | :--- | :--- |
| **Main Curve Area** | Interactive | **Click & Drag** to draw your custom Width Curve. **Right-Click** to reset a specific frequency point to its default (`1.0`). |
| **Top Half (> 1.0)** | Curve Parameter | Boosts the Side signal, making the selected frequency range **wider**. |
| **Bottom Half (< 1.0)**| Curve Parameter | Attenuates the Side signal, making the selected frequency range **more mono**. |
| **Gain Slider** | Sidebar | **Visual Gain only.** Scales the height of the green and pink visualization areas to help you see quiet signals better. *This does not affect the actual audio output.* |
| **FFT Size** | Dropdown | Sets frequency resolution. **Small (1024):** Low latency but "blurry" in the lows. **Large (4096+):** High surgical precision for bass, but uses more CPU and introduces higher latency. |

---

## 🛠️ Practical Use Cases

* **Mono-Compatible Bass:** Draw a curve that stays at `0.0` for everything below 150 Hz. This ensures your low-end is rock-solid mono while keeping the highs wide. *(Note: Wide bass can still be an excellent creative choice, especially for ambient tracks designed for headphones!)*
* **Airy Vocals & Hats:** Boost the curve above a few kHz to add a sense of "air" and width to high-frequency elements without cluttering the center of your mix.
* **Spectral Unmasking:** If your Sidechain (Pink Area) shows a large amount of energy at 2 kHz, dip your Width Curve (Green Area) at that exact spot. This "makes a hole" in the stereo field for the sidechain signal to pop through clearly.