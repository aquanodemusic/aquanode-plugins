#pragma once

#include <JuceHeader.h>
#include <array>
#include "PresetCommon.h"

class AquanodeModularAudioProcessor;

// The FM generator: rebuilds a 6-operator FM voice as a patch of six DX
// Operators wired to the voice's algorithm, a DX Pitch EG and a DX LFO. The
// factory voices (FMVoices.cpp) are sorted into general categories; any DX7
// .syx file can be loaded through the menu as well.
namespace aquanode::dx7
{
    // one DX7 voice in VCED order (the 155-byte single-voice format)
    struct Voice
    {
        std::array<uint8_t, 155> vced {};
        juce::String name;

        static Voice fromVced (const unsigned char* v);
    };

    // a loaded .syx file
    struct Bank
    {
        juce::String title;
        std::vector<Voice> voices;
    };

    // the built-in voices, each with its category (index into categories())
    struct FactoryVoice
    {
        Voice voice;
        int category { 0 };
        int number { 1 };      // its number within the category
    };

    juce::StringArray categories();                     // Bass, Bell, FX, Keys, Lead, Pad, Pluck
    const std::vector<FactoryVoice>& factoryVoices();

    // Reads a DX7 .syx: a 32-voice bulk dump (also several in one file, or a
    // bare 4096-byte bank), or single-voice dumps. Init voices are skipped.
    // Returns an empty bank when nothing in the data looks like a DX7 voice.
    Bank parseSysex (const juce::MemoryBlock& data, const juce::String& title);

    // Builds the voice into an empty patch under the given name; returns the
    // patch's display name.
    juce::String buildVoicePatch (AquanodeModularAudioProcessor& p, const Voice& voice, const juce::String& label);

    // a random built-in voice, of one category (-1 = any)
    presets::BuildFn randomVoiceBuilder (juce::uint32 seed, int category = -1);
}
