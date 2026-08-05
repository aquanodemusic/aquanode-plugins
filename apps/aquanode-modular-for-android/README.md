# Aquanode Modular Synthesizer – Android Port

This is the **Android port** of the **Aquanode Modular Synthesizer**. It uses the exact same source code as the VST3 version, but is packaged separately in the `apps` directory to clearly indicate that it is intended for Android.

> **Note:** Due to JUCE licensing/copyright restrictions, the Android folder does **not** contain all of the files required to compile the project. Thus you need to

1. Open the `.jucer` project in **JUCE's Projucer** and locate the codebase correctly.
2. Export/compile the project for **Android**.
3. Open the generated Android project in **Android Studio**.
4. Build the project to generate the `.apk` file.

## Features

- Built-in **on-screen MIDI keyboard** located at the bottom of the display.
- Supports **external MIDI keyboards**, provided your Android device and MIDI controller are compatible.
- **Preset import and export** is compatible with the VST3 version (tested with several small presets).

## Documentation

Additional documentation and details can be found in the project in the respective **synths** folder directory.

---

Have fun creating music with **Aquanode Modular**!