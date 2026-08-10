# Builds for Android (JUCE)

In here you find JUCE projects that are configured to generate Android SDK projects,
that will then generate Android apps from the JUCE plugins. These are already precompiled except
of the `app/` directory that contains personal details about the machine you're on.

## Requirements

- **JUCE** (with **Projucer**)
- **Android Studio** — includes the Android SDK and a JDK.
- **Android NDK** — NDK 26 for current JUCE. The NDK version must match what the
  Projucer's Android exporter expects (set in the exporter settings).
- **CMake** and **LLDB** — install via *Android Studio → SDK Manager → SDK Tools*
  (CMake builds the native code; LLDB is for C/C++ debugging). Recent Android
  Studio versions may already include these.
- **C++17** toolchain (provided by the NDK).

## Steps

1. Open the `.jucer` file in the Projucer.
2. **File → Global Paths…** and set the **Android SDK** location
   (e.g. `.../Android/Sdk`). Red text means the path is wrong. This should be already set correctly though.
3. Make sure an **Android** exporter exists in the project (add one if needed).
4. **Save the project** (Ctrl/Cmd+S). This regenerates `Builds/Android`,
   including the `app/` module (`build.gradle`, `CMakeLists.txt`,
   `AndroidManifest.xml`, the `com/rmsl/juce` Java glue, and resources).
5. Open `Builds/Android` in **Android Studio** and let Gradle sync. Android
   Studio creates `local.properties` (with your `sdk.dir`) automatically.
6. Build and run on a device or emulator.

## Notes

- If Gradle sync fails on the NDK version, install the exact NDK exporter
  by hand, by manually updating the version in the Projucer exporter. This took me a few tries!
