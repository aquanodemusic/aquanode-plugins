#include "PluginCatalog.h"
#include "../PluginProcessor.h"

namespace aquanode::plugins
{

namespace
{
    // everything the GUI module of a plugin needs except its create function
    struct BaseInfo
    {
        const char* id;
        const char* name;
        const char* description;
        juce::uint32 accent;
        ModuleSection section;
        bool midiIn, midiOut, audioIn, sideIn;
        int w, h;
        const char* presetPatterns;
    };

    // sidebar order: the synths, then the effects
    const std::vector<BaseInfo>& baseInfos()
    {
        static const std::vector<BaseInfo> infos {
            { "aquanova", "Aquanova",
              "The Supernova-style polysynth with its whole panel on the card: three oscillators, the "
              "oscillator and filter matrices, the 42-band vocoder and the effects. Audio In feeds its "
              "audio-in oscillators and the vocoder.",
              0xff5a9fe6, ModuleSection::Oscillator, true, false, true, false, 1200, 700, "*.aquanova" },

            { "aquavibrio", "Aqua Vibrio",
              "The Virus-style polysynth with both of its engines (Native and Original), the whole "
              "effects rack and the note layers. Audio In is its Input oscillator and vocoder carrier.",
              0xff2f7a4a, ModuleSection::Oscillator, true, false, true, false, 1200, 700, "*.json;*.syx;*.SYX;*.mid;*.midi" },

            { "alphabeta", "AlphaBeta",
              "The Alpha 3-style synth with its liquid filter and chorus, both engines (Native and "
              "Original), the CRT matrix and its randomizer. Loads .abpreset and the original's .fxp / .fxb.",
              0xff4fb3d9, ModuleSection::Oscillator, true, false, false, false, 1140, 586, "*.abpreset;*.fxp;*.fxb" },

            { "phizmo", "Phizmo",
              "The Fizmo remake: four sounds of two transwave oscillators each, the arpeggiator, the "
              "insert effects, global reverb and the performance macros.",
              0xffe0a040, ModuleSection::Oscillator, true, false, false, false, 1300, 714, "*.phizmo" },

            { "phizmosc", "PhizmOsc",
              "The two-oscillator transwave synth with its evolution envelopes, filter, chorus and reverb. "
              "Presets carry their wavetables with them.",
              0xffd4b05a, ModuleSection::Oscillator, true, false, false, false, 1100, 640, "*.phizm" },

            { "virtualdx7", "VirtualDX7",
              "The six-operator FM synth with both of its engines: the native engine, and the bit-accurate "
              "VDX7 emulation once you load your own DX7 firmware ROM. Loads any DX7 .syx bank.",
              0xffb8864a, ModuleSection::Oscillator, true, false, false, false, 1180, 594, "*.syx;*.SYX" },

            { "noteadder", "NoteAdder",
              "Adds notes around the ones you play - manual, scale and custom modes, animation, humanize "
              "and decay. Its Midi Out plays whatever is patched to it; Audio Out is its Audit sine.",
              0xff9ccc65, ModuleSection::Utility, true, true, false, false, 560, 592, "*.nastate" },

            { "soundscape", "Soundscape Ecology",
              "The procedural nature synth: three slots of Biophony, Geophony and Anthrophony models with "
              "weather, time of day and stereo placement. Midi In plays it in its MIDI modes.",
              0xff6abf69, ModuleSection::Oscillator, true, false, false, false, 1200, 700, "*.xml" },

            { "slicer", "Slicer",
              "The drum slicer: load a loop (or anything), it finds the hits and lays one slice on each key "
              "from C1 up, with reverse, loop, mono and B0's random stream. Its 'presets' are audio files.",
              0xffe58a4e, ModuleSection::Oscillator, true, false, false, false, 800, 450,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "combweave", "CombWeave",
              "Two banks of up to 128 sine partials on every note - spread linearly, exponentially or as "
              "harmonics, folded back (Wrap / Mirror) or note-locked - each with its own envelope and "
              "high-pass, summed into a hard limit.",
              0xff00c4aa, ModuleSection::Oscillator, true, false, false, false, 700, 787, "" },

            { "grainfreeze", "GrainFreeze",
              "The phase-vocoder player: load a sound, stretch it in time or freeze it on one spot (Spectral "
              "or Cepstral, with glide), scrub its waveform. Keys play it pitched from C4. Its 'presets' are "
              "audio files.",
              0xffa8d8ff, ModuleSection::Oscillator, true, false, false, false, 900, 650,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "samplefield", "SampleField",
              "A field of samples: every held key plays them one after another at random, with random pan, "
              "rate and level, a time limit and an echo per voice. Its 'presets' are audio files (each adds "
              "a sample to the field).",
              0xff00c8e0, ModuleSection::Oscillator, true, false, false, false, 480, 560,
              "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" },

            { "83chorusverb", "83ChorusVerb",
              "VirtualDX7's effects as a rack: Chorus, Delay, Phaser and Reverb (with Freeze), each with "
              "its own switch.",
              0xff7fd1c9, ModuleSection::Effect, false, false, true, false, 760, 400, "" },

            { "alphabetafx", "AlphaBetaFX",
              "AlphaBeta's drive, liquid filter (with its envelope, on the input level or on Midi In) and "
              "chorus as an effect, in both models: Native and Original.",
              0xff8ec9e8, ModuleSection::Effect, true, false, true, false, 900, 240, "" },

            { "automorpheq", "AutoMorphEQ",
              "Seven bell filters, each morphing on its own between a start and an end frequency and level "
              "in one of seven shapes, with Wet Only and a hard limit.",
              0xff00b3b3, ModuleSection::Effect, false, false, true, false, 1100, 700, "" },

            { "centercomb", "CenterComb",
              "Up to 129 bell filters mirrored around a centre frequency, with its response display, "
              "Mirror / Wrap modes and smoothing.",
              0xffc77dd9, ModuleSection::Effect, false, false, true, false, 600, 550, "" },

            { "floweq", "FlowEQ",
              "Three bell filters whose frequency, gain and Q follow hand-drawn curves (or wavetables), "
              "with gesture recording straight from the graph.",
              0xff3fc1c9, ModuleSection::Effect, false, false, true, false, 900, 600, "" },

            { "fullfilter", "FullFilter",
              "Up to 128 bell filters on the harmonics of the played note - an EQ in the shape of a "
              "sawtooth - up to twelve notes at once, with bar editors for every bell, acoustic shapes and "
              "wavetable import. Midi In plays it.",
              0xffff8a00, ModuleSection::Effect, true, false, true, false, 900, 500, "" },

            { "phorest", "Phorest",
              "The deep phaser: up to 48 allpass stages swept by seven LFO shapes (or by hand), with feedback, "
              "stereo offset, detune and Dry Cancel.",
              0xff00c957, ModuleSection::Effect, false, false, true, false, 550, 550, "" },

            { "tapelectric", "TapElectric",
              "Tape deck character: electric hum with six harmonics, shaped tape noise, drift, wobble and "
              "saturation on the input.",
              0xffd98c4a, ModuleSection::Effect, false, false, true, false, 580, 620, "" },

            { "vocode", "Vocode",
              "The filter-bank vocoder with up to 256 bands and drawn band curves. Carrier on Audio In, "
              "the voice (modulator) on Mod In - or its auto-vocode mode.",
              0xffb07cf0, ModuleSection::Effect, false, false, true, true, 980, 520, "*.vpreset" },
        };
        return infos;
    }

    const BaseInfo* findInfo (const juce::String& id)
    {
        for (const auto& i : baseInfos())
            if (id == i.id)
                return &i;
        return nullptr;
    }

    std::map<juce::String, std::function<juce::AudioProcessor*()>>& creators()
    {
        static std::map<juce::String, std::function<juce::AudioProcessor*()>> m;
        return m;
    }

    struct PendingPatch { PatchBuildFn build; PresetLoadFn load; };
    std::map<juce::String, PendingPatch>& pendingPatchVersions()
    {
        static std::map<juce::String, PendingPatch> m;
        return m;
    }
}

//==============================================================================
std::vector<CatalogEntry>& catalog()
{
    static std::vector<CatalogEntry> entries = []
    {
        std::vector<CatalogEntry> v;
        for (const auto& i : baseInfos())
        {
            CatalogEntry e;
            e.id = i.id;
            e.name = i.name;
            e.guiTypeId = juce::String ("plugin.") + i.id;
            e.presetPatterns = i.presetPatterns;
            v.push_back (e);
        }
        return v;
    }();

    // patch versions registered during static initialisation, whichever ran first
    auto& pending = pendingPatchVersions();
    if (! pending.empty())
    {
        for (auto& e : entries)
            if (auto it = pending.find (e.id); it != pending.end())
            {
                e.buildPatch = it->second.build;
                e.loadPreset = it->second.load;
            }
        pending.clear();
    }
    return entries;
}

CatalogEntry* findEntry (const juce::String& id)
{
    for (auto& e : catalog())
        if (e.id == id)
            return &e;
    return nullptr;
}

HostedSpec baseSpec (const juce::String& id)
{
    HostedSpec s;
    if (const auto* i = findInfo (id))
    {
        s.pluginId = i->id;
        s.typeId = juce::String ("plugin.") + i->id;
        s.displayName = i->name;
        s.description = i->description;
        s.accent = juce::Colour (i->accent);
        s.section = i->section;
        s.midiIn = i->midiIn;
        s.midiOut = i->midiOut;
        s.audioIn = i->audioIn;
        s.sideIn = i->sideIn;
        s.defaultWidth = i->w;
        s.defaultHeight = i->h;
    }
    jassert (s.pluginId.isNotEmpty());
    return s;
}

bool registerCreate (const juce::String& id, std::function<juce::AudioProcessor*()> create)
{
    creators()[id] = std::move (create);
    return true;
}

namespace
{
    std::map<juce::String, AudioAccess>& audioAccessors()
    {
        static std::map<juce::String, AudioAccess> m;
        return m;
    }
}

bool registerAudioAccess (const juce::String& id, AudioAccess access)
{
    audioAccessors()[id] = std::move (access);
    return true;
}

const AudioAccess* audioAccessFor (const juce::String& id)
{
    auto it = audioAccessors().find (id);
    return it != audioAccessors().end() ? &it->second : nullptr;
}

bool registerPatchVersion (const juce::String& id, PatchBuildFn build, PresetLoadFn load)
{
    // catalog() may not exist yet during static initialisation: park it
    pendingPatchVersions()[id] = { std::move (build), std::move (load) };
    return true;
}

std::unique_ptr<juce::AudioProcessor> createOriginal (const juce::String& id, double sampleRate)
{
    auto it = creators().find (id);
    if (it == creators().end() || it->second == nullptr)
        return nullptr;

    std::unique_ptr<juce::AudioProcessor> p (it->second());
    if (p != nullptr)
    {
        p->setRateAndBufferSizeDetails (sampleRate, 512);
        p->prepareToPlay (sampleRate, 512);
    }
    return p;
}

//==============================================================================
namespace
{
    int findFirst (AquanodeModularAudioProcessor& patch, const juce::String& typeId)
    {
        for (int id : patch.getModuleIds())
            if (auto* inst = patch.getInstance (id))
                if (inst->descriptor().typeId == typeId)
                    return id;
        return -1;
    }

    // right of everything already in the patch
    juce::Point<int> freeSpot (AquanodeModularAudioProcessor& patch)
    {
        int right = 40, top = 60;
        bool any = false;
        for (int id : patch.getModuleIds())
            if (auto* inst = patch.getInstance (id))
            {
                // right of everything already there (cards wider than the default
                // say so; rows of many knobs widen a card too)
                std::map<int, int> units;
                for (const auto& spec : inst->descriptor().params)
                    if (! spec.hidden)
                        units[spec.row] += juce::jlimit (1, 5, spec.widthUnits);
                int most = 5;
                for (const auto& u : units) most = juce::jmax (most, u.second);
                const int w = juce::jmax (inst->dsp->preferredModuleWidth(), 250, 20 + most * 46);
                right = juce::jmax (right, inst->position.x + w + 80);
                top = any ? juce::jmin (top, inst->position.y) : inst->position.y;
                any = true;
            }
        return { right, top };
    }
}

int addGuiVersion (AquanodeModularAudioProcessor& patch, const juce::String& id, juce::Point<int> where)
{
    auto* e = findEntry (id);
    if (e == nullptr)
        return -1;

    const int card = patch.addModule (e->guiTypeId, where);
    if (card < 0)
        return -1;

    auto* inst = patch.getInstance (card);
    const auto& d = inst->descriptor();

    // the card plays straight into Audio Out (made if the patch has none);
    // an effect card takes its input from Audio In
    int out = findFirst (patch, "io.audioOut");
    if (out < 0)
        out = patch.addModule ("io.audioOut", where + juce::Point<int> (inst->dsp->preferredModuleWidth() + 60, 0));
    if (out >= 0)
        patch.addCable (card, "audioOut", out, "audioIn");

    if (d.inputIndexOf ("audioIn") >= 0 && d.section == ModuleSection::Effect)
    {
        int in = findFirst (patch, "io.audioIn");
        if (in < 0)
            in = patch.addModule ("io.audioIn", where - juce::Point<int> (300, 0));
        if (in >= 0)
            patch.addCable (in, "audioOut", card, "audioIn");
    }
    return card;
}

namespace
{
    // The ports of a freshly built patch version: its output into Audio Out
    // (made if missing) and, for an effect, Audio In into its input.
    void wireToPatchIO (AquanodeModularAudioProcessor& patch, const PatchPorts& ports, bool isEffect)
    {
        if (! ports.audioOut.empty())
        {
            int out = findFirst (patch, "io.audioOut");
            if (out < 0)
            {
                auto pos = juce::Point<int> (60, 60);
                if (auto* src = patch.getInstance (ports.audioOut.front().module))
                    pos = src->position + juce::Point<int> (320, 0);
                out = patch.addModule ("io.audioOut", pos);
            }
            for (const auto& o : ports.audioOut)
                patch.addCable (o.module, o.socket, out, "audioIn");
        }

        if (isEffect && ! ports.audioIn.empty())
        {
            int in = findFirst (patch, "io.audioIn");
            if (in < 0)
            {
                auto pos = juce::Point<int> (60, 60);
                if (auto* dst = patch.getInstance (ports.audioIn.front().module))
                    pos = dst->position - juce::Point<int> (320, 0);
                in = patch.addModule ("io.audioIn", pos);
            }
            for (const auto& i : ports.audioIn)
                patch.addCable (in, "audioOut", i.module, i.socket);
        }
    }

    bool isEffectPlugin (const juce::String& id)
    {
        const auto* i = findInfo (id);
        return i != nullptr && i->section == ModuleSection::Effect;
    }
}

bool isEffectPluginId (const juce::String& id) { return isEffectPlugin (id); }

juce::String addPatchVersion (AquanodeModularAudioProcessor& patch, const juce::String& id,
                              const juce::File& presetFile, bool replace)
{
    auto* e = findEntry (id);
    if (e == nullptr || ! e->hasPatchVersion())
        return {};

    auto original = createOriginal (id, 48000.0);
    if (original == nullptr)
        return {};

    juce::String presetName;
    if (presetFile != juce::File())
    {
        if (e->loadPreset == nullptr || ! e->loadPreset (*original, presetFile))
            return {};
        presetName = presetFile.getFileNameWithoutExtension();
    }

    const bool effect = isEffectPlugin (id);
    juce::String name;

    if (replace)
    {
        patch.generatePatch ([&] (AquanodeModularAudioProcessor& p)
        {
            const auto ports = e->buildPatch (p, *original, { 60, 60 });
            wireToPatchIO (p, ports, effect);
            name = ports.name;
            return name;
        });
    }
    else
    {
        const auto ports = e->buildPatch (patch, *original, freeSpot (patch));
        wireToPatchIO (patch, ports, effect);
        name = ports.name;
    }

    if (presetName.isNotEmpty())
        name = e->name + " - " + presetName;
    if (replace)
        patch.patchName = name;
    return name;
}

juce::String convertGuiToPatch (AquanodeModularAudioProcessor& patch, int guiInstanceId)
{
    auto* inst = patch.getInstance (guiInstanceId);
    if (inst == nullptr)
        return {};

    auto* hosted = dynamic_cast<HostedPluginModule*> (inst->dsp.get());
    if (hosted == nullptr)
        return {};

    auto* e = findEntry (hosted->getSpec().pluginId);
    if (e == nullptr || ! e->hasPatchVersion())
        return {};

    // a copy of the card's sound in a fresh instance, so the card itself is
    // never touched by the builder
    auto original = createOriginal (e->id, 48000.0);
    if (original == nullptr)
        return {};
    {
        juce::MemoryBlock state;
        hosted->getPlugin()->getStateInformation (state);
        original->setStateInformation (state.getData(), (int) state.getSize());
    }

    // audio the plugin keeps outside its state (Slicer's sample) goes along too
    if (auto* access = audioAccessFor (e->id); access != nullptr && access->get && access->set)
    {
        double rate = 44100.0;
        if (auto audio = access->get (*hosted->getPlugin(), rate))
            access->set (*original, std::make_shared<juce::AudioBuffer<float>> (*audio), rate);
    }

    const auto where = inst->position;

    // the card's cables, to rewire its replacement the same way
    std::vector<CableInfo> touching;
    for (const auto& c : patch.getCables())
        if (c.fromModule == guiInstanceId || c.toModule == guiInstanceId)
            touching.push_back (c);

    patch.removeModule (guiInstanceId);
    const auto ports = e->buildPatch (patch, *original, where);

    auto reconnect = [&patch] (const std::vector<SocketRef>& targets, int fromModule, const juce::String& fromSocket,
                               bool targetsAreInputs)
    {
        for (const auto& t : targets)
        {
            if (targetsAreInputs) patch.addCable (fromModule, fromSocket, t.module, t.socket);
            else                  patch.addCable (t.module, t.socket, fromModule, fromSocket);
        }
    };

    for (const auto& c : touching)
    {
        if (c.toModule == guiInstanceId)   // something fed the card
        {
            if (c.toSocket == "audioIn") reconnect (ports.audioIn, c.fromModule, c.fromSocket, true);
            if (c.toSocket == "sideIn")  reconnect (ports.sideIn,  c.fromModule, c.fromSocket, true);
            if (c.toSocket == "midiIn")  reconnect (ports.midiIn,  c.fromModule, c.fromSocket, true);
        }
        else                               // the card fed something
        {
            if (c.fromSocket == "audioOut") reconnect (ports.audioOut, c.toModule, c.toSocket, false);
            if (c.fromSocket == "midiOut")  reconnect (ports.midiOut,  c.toModule, c.toSocket, false);
        }
    }
    return ports.name;
}

} // namespace aquanode::plugins
