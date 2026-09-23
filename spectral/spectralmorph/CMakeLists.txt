cmake_minimum_required(VERSION 3.22)
project(SpectralMorph VERSION 1.0.0 LANGUAGES C CXX)

# Pass -DJUCE_SOURCE_DIR=/path/to/JUCE to build from a local checkout.
if (JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 72782788ce18c2d4d760b28e0921d6ffc6431102)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SpectralMorph
    COMPANY_NAME aquanodemusic
    BUNDLE_ID com.aquanodemusic.SpectralMorph
    PLUGIN_MANUFACTURER_CODE Aqnm
    PLUGIN_CODE SpMo
    FORMATS AU VST3
    PRODUCT_NAME SpectralMorph
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_SYNTH FALSE)

target_sources(SpectralMorph PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PluginProcessor.h
    src/PluginEditor.h
    src/MorphEngine.h
    src/SpectrogramComponent.h)

target_compile_features(SpectralMorph PUBLIC cxx_std_17)
target_compile_definitions(SpectralMorph PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SpectralMorph PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp)
