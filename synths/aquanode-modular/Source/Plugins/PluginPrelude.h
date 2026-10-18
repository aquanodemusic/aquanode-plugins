#pragma once

//==============================================================================
// Included first by every Plugins/Plugin*.cpp - the translation units that
// compile one of the merged plugins' original sources inside a namespace of
// its own (namespace aqp_<plugin> { #include "...PluginProcessor.cpp" }).
//
// Every system and JUCE header any of those sources include has to be in
// before the namespace opens: the sources' own #includes then hit include
// guards and add nothing, instead of pulling the standard library into the
// plugin's namespace. A plugin that starts using a new system header needs
// it added here.
//==============================================================================
#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cfloat>
#include <climits>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
 // <windows.h> would define min / max as macros, which breaks every
 // std::min (...) / std::max (...) / numeric_limits<>::max() in the plugins
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <intrin.h>
 #undef min
 #undef max
#else
 #include <dlfcn.h>
 #include <unistd.h>
 #if ! defined(__APPLE__)
  #include <endian.h>
 #endif
#endif

#include "HostedPlugin.h"

//==============================================================================
// The JucePlugin_ macros the Projucer sets for Aquanode Modular itself would
// tell a merged plugin it is Aquanode Modular. Each Plugin*.cpp includes one
// of these two blocks (through AQP_PLUGIN_IS_SYNTH / AQP_PLUGIN_IS_EFFECT)
// right before its namespace, with the plugin's own name.
//==============================================================================
#undef JucePlugin_Name
#undef JucePlugin_IsSynth
#undef JucePlugin_WantsMidiInput
#undef JucePlugin_ProducesMidiOutput
#undef JucePlugin_IsMidiEffect
#undef JucePlugin_PreferredChannelConfigurations

// The plugins' sources can tell they run inside Aquanode Modular: there they
// write no files of their own accord (preset folders, factory banks, reports)
// - only what you save through a file dialog.
#define AQP_HOSTED 1

// AQP_<PLUGIN>_SRC(file) in the Plugin<Name>Config.h headers
#define AQP_STRINGIFY_IMPL(x) #x
#define AQP_STRINGIFY(x) AQP_STRINGIFY_IMPL(x)

//==============================================================================
// Android: the merged plugins' file pickers hand back real files (see
// AndroidFileChooserShim.h). Every header of Aquanode Modular's own that
// names juce::FileChooser comes in first, so only the plugins' sources see
// the replacement.
//==============================================================================
#if JUCE_ANDROID
 #include "../PluginProcessor.h"
 #include "../PluginEditor.h"
 #include "../Modules/PianoRollModule.h"
 #include "../Modules/IRForgeModule.h"
 #include "../Modules/RecordModule.h"
 #include "PatchKit.h"
 #include "PatchWidgets.h"
 #include "AndroidFileChooserShim.h"
 #define FileChooser AqpFileChooser
#endif
