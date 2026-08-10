#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FactoryPresets.h"

// Handles saving/loading K5 patches as standalone .xml files, independent of
// the host's own session/state save (getStateInformation/setStateInformation
// in PluginProcessor still handle that - this is a *user-facing preset
// browser* on top).
//
// File format is deliberately simple and human-editable (not a raw dump of
// the APVTS ValueTree, which stores normalised 0..1 values and would be
// unreadable/impossible to hand-author):
//
//   <K5Preset name="Glass Bells">
//     <PARAM id="s1_cutoff" value="8000"/>
//     <PARAM id="s1_harmMode" value="3"/>   <!-- choice index -->
//     ...
//   </K5Preset>
//
// Any parameter not mentioned in the file is left at its default value, so
// presets only need to specify what makes them distinctive.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".xml";
    static constexpr const char* presetTag = "K5Preset";

    explicit PresetManager(juce::AudioProcessorValueTreeState& stateIn) : apvts(stateIn)
    {
        auto dir = getPresetsFolder();
        if (!dir.exists())
            dir.createDirectory();

        installFactoryPresetsIfMissing();
    }

    juce::File getPresetsFolder() const
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("K5Additive")
            .getChildFile("Presets");
    }

    // Resets every parameter to default, then applies whatever the file
    // specifies. Returns false (and leaves the current state untouched) if
    // the file can't be read or isn't a K5 preset.
    bool loadPreset(const juce::File& file)
    {
        auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr || !xml->hasTagName(presetTag))
            return false;

        for (auto* p : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                ranged->setValueNotifyingHost(ranged->getDefaultValue());

        for (auto* child : xml->getChildIterator())
        {
            if (!child->hasTagName("PARAM"))
                continue;

            const auto id = child->getStringAttribute("id");
            const float value = (float)child->getDoubleAttribute("value");
            applyParam(id, value);
        }

        currentPresetName = file.getFileNameWithoutExtension();
        return true;
    }

    bool loadPresetByName(const juce::String& name)
    {
        return loadPreset(getPresetsFolder().getChildFile(name + fileExtension));
    }

    // Writes every parameter's current value out (not just non-default ones)
    // so a saved preset is a complete, self-contained snapshot.
    bool savePreset(const juce::String& name)
    {
        if (name.isEmpty())
            return false;

        juce::XmlElement xml(presetTag);
        xml.setAttribute("name", name);

        for (auto* p : apvts.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (ranged == nullptr)
                continue;

            auto* param = xml.createNewChildElement("PARAM");
            param->setAttribute("id", ranged->paramID);

            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged))
            {
                // AudioParameterChoice deliberately hides the inherited
                // getValue() (it's private there) and exposes getIndex()
                // instead, which is what we want anyway - no normalising.
                param->setAttribute("value", choice->getIndex());
            }
            else
            {
                const float raw = ranged->getNormalisableRange().convertFrom0to1(ranged->getValue());
                param->setAttribute("value", (double)raw);
            }
        }

        const auto file = getPresetsFolder().getChildFile(juce::File::createLegalFileName(name) + fileExtension);
        if (!xml.writeTo(file))
            return false;

        currentPresetName = name;
        return true;
    }

    bool deletePreset(const juce::String& name)
    {
        return getPresetsFolder().getChildFile(name + fileExtension).deleteFile();
    }

    juce::StringArray getAllPresetNames() const
    {
        juce::StringArray names;
        for (const auto& f : getPresetsFolder().findChildFiles(juce::File::findFiles, false,
            juce::String("*") + fileExtension))
            names.add(f.getFileNameWithoutExtension());
        names.sort(true);
        return names;
    }

    juce::String getCurrentPresetName() const { return currentPresetName; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentPresetName{ "Init" };

    void applyParam(const juce::String& id, float value)
    {
        auto* param = apvts.getParameter(id);
        if (param == nullptr)
            return;

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            const int index = juce::jlimit(0, choice->choices.size() - 1,
                juce::roundToInt(value));
            choice->setValueNotifyingHost(choice->getNormalisableRange()
                .convertTo0to1((float)index));
        }
        else if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            ranged->setValueNotifyingHost(ranged->getNormalisableRange().convertTo0to1(value));
        }
    }

    // Drops the built-in factory presets (see FactoryPresets.h) into the
    // presets folder the first time the plugin runs on a machine, so the
    // preset browser isn't empty out of the box. Never overwrites a file the
    // user already has (e.g. if they've since edited "Init" themselves).
    void installFactoryPresetsIfMissing()
    {
        for (auto& preset : FactoryPresets::all)
        {
            auto file = getPresetsFolder().getChildFile(juce::String(preset.name) + fileExtension);
            if (!file.existsAsFile())
                file.replaceWithText(preset.xml);
        }
    }
};