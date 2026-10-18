#include "PresetGenerators.h"
#include "ChordWavetableGenerator.h"
#include "DX7Generator.h"
#include "GenreGenerator.h"
#include "VaporizationPatch.h"
#include "../PluginProcessor.h"
#include <map>

namespace aquanode::presets
{

namespace
{
    //==========================================================================
    // the engines behind the categories
    //==========================================================================
    struct Engine
    {
        juce::String name;                                  // "Ambient", "Colorbass", ...
        std::function<BuildFn (juce::uint32 seed)> make;    // a random patch of the category
    };

    struct Category
    {
        juce::String name;                                  // "Chord"
        std::vector<Engine> engines;                        // A-Z
    };

    // every category with the engines that make it, A-Z (FM is its own row)
    const std::vector<Category>& categoryTable()
    {
        static const std::vector<Category> table = []
        {
            std::map<juce::String, std::vector<Engine>> byName;

            // Colorbass: one category per archetype (Chord, Growl, Pluck, Pad)
            const auto archetypes = chordWavetableArchetypeNames();
            for (int a = 0; a < archetypes.size(); ++a)
                byName[archetypes[a]].push_back ({ "Colorbass", [a] (juce::uint32 seed) -> BuildFn
                {
                    return [seed, a] (AquanodeModularAudioProcessor& p) { return buildChordWavetablePatch (p, seed, a); };
                } });

            // the genre engines: whatever categories their recipes cover
            const auto names = genre::genreNames();
            for (int g = 0; g < names.size(); ++g)
            {
                juce::StringArray cats;
                for (const auto& e : genre::recipesOf (g))
                    cats.addIfNotAlreadyThere (e.category);
                for (const auto& cat : cats)
                    byName[cat].push_back ({ names[g], [g, cat] (juce::uint32 seed)
                    {
                        return genre::randomBuilder (g, seed, cat);
                    } });
            }

            // the Vaporization Pad is one more recipe of the Ambient pads: it
            // comes up as often as any single recipe of theirs does
            for (int g = 0; g < names.size(); ++g)
            {
                if (names[g] != "Ambient")
                    continue;
                int padRecipes = 0;
                for (const auto& e : genre::recipesOf (g))
                    if (e.category == "Pad")
                        ++padRecipes;
                for (auto& e : byName["Pad"])
                    if (e.name == "Ambient")
                    {
                        auto inner = e.make;
                        e.make = [inner, padRecipes] (juce::uint32 seed) -> BuildFn
                        {
                            Dice dice (seed ^ 0x7a9e5u);
                            if (dice.index (padRecipes + 1) == 0)
                                return [] (AquanodeModularAudioProcessor& p) { return buildVaporizationPatch (p); };
                            return inner (seed);
                        };
                    }
            }

            std::vector<Category> out;
            for (auto& [name, engines] : byName)
            {
                std::sort (engines.begin(), engines.end(), [] (const Engine& a, const Engine& b)
                           { return a.name.compareNatural (b.name) < 0; });
                out.push_back ({ name, std::move (engines) });
            }
            std::sort (out.begin(), out.end(), [] (const Category& a, const Category& b)
                       { return a.name.compareNatural (b.name) < 0; });
            return out;
        }();
        return table;
    }

    // the ticked engine of each row (category name -> engine name; absent = all)
    // for this session
    std::map<juce::String, juce::String>& ticked()
    {
        static std::map<juce::String, juce::String> t;
        return t;
    }

    juce::String tickedOf (const juce::String& row)
    {
        const auto it = ticked().find (row);
        return it != ticked().end() ? it->second : juce::String();
    }

    void toggleTick (const juce::String& row, const juce::String& item)
    {
        if (tickedOf (row) == item)
            ticked().erase (row);
        else
            ticked()[row] = item;
    }

    // one tick item per choice: picking the ticked one unticks it
    void addTickItems (juce::PopupMenu& menu, const juce::String& row, const juce::StringArray& items,
                       const MenuContext& ctx)
    {
        const auto current = tickedOf (row);
        const auto changed = ctx.selectionChanged;
        for (const auto& item : items)
            menu.addItem (item, true, item == current, [row, item, changed]
            {
                toggleTick (row, item);
                if (changed != nullptr)
                    changed();
            });
    }

    BuildFn categoryBuilder (const Category& cat, juce::uint32 seed, const juce::String& engineName)
    {
        if (cat.engines.empty())
            return {};

        for (const auto& e : cat.engines)
            if (e.name == engineName)
                return e.make (seed);

        Dice dice (seed);
        const auto& e = cat.engines[(size_t) dice.index ((int) cat.engines.size())];
        return e.make (seed * 2246822519u + 3u);
    }

    Generator makeCategory (const Category& cat)
    {
        Generator g;
        g.id = "gen." + cat.name;
        g.label = cat.name;
        g.tooltipTitle = cat.name;

        juce::StringArray engineNames;
        for (const auto& e : cat.engines)
            engineNames.add (e.name);

        const auto name = cat.name;
        const auto lower = name == "FX" ? juce::String ("effect") : name.toLowerCase();
        if (engineNames.size() > 1)
            g.tooltipBody = "Clears the patch and builds a new " + lower + " sound with every tap, from one of the "
                            "engines that make " + lower + " sounds: " + engineNames.joinIntoString (", ")
                          + ". The arrow on the right ticks one engine to use only that; tick it again to go back "
                            "to all of them.";
        else
            g.tooltipBody = "Clears the patch and builds a new " + lower + " sound with every tap ("
                          + engineNames.joinIntoString (", ") + ").";

        g.random = [name] (juce::uint32 seed) -> BuildFn
        {
            for (const auto& c : categoryTable())
                if (c.name == name)
                    return categoryBuilder (c, seed, tickedOf (name));
            return {};
        };

        // a menu only where there is a choice to make
        if (engineNames.size() > 1)
        {
            g.fillMenu = [name, engineNames] (juce::PopupMenu& menu, const MenuContext& ctx)
            {
                menu.addSectionHeader (name);
                addTickItems (menu, name, engineNames, ctx);
            };
            g.selection = [name] { return tickedOf (name); };
        }
        return g;
    }

    //==========================================================================
    // FM: the built-in voices by sub-category, plus loaded .syx files
    //==========================================================================
    const juce::String fmRow { "FM" };

    // a loaded file's voices, numbered by their slot in the file
    juce::PopupMenu fileMenu (const dx7::Bank& bank, std::function<void (BuildFn)> apply)
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < bank.voices.size(); ++i)
        {
            const auto voice = bank.voices[i];
            const auto label = voice.name.trim().isNotEmpty() ? voice.name.trim() : bank.title;
            sub.addItem (juce::String ((int) i + 1).paddedLeft ('0', 2) + "  " + voice.name, [apply, voice, label]
            {
                apply ([voice, label] (AquanodeModularAudioProcessor& p) { return dx7::buildVoicePatch (p, voice, label); });
            });
        }
        return sub;
    }

    // files loaded this session, newest first
    std::vector<dx7::Bank>& loadedFiles()
    {
        static std::vector<dx7::Bank> files;
        return files;
    }

    BuildFn fmBuilder (juce::uint32 seed, const juce::String& subCategory)
    {
        return dx7::randomVoiceBuilder (seed, dx7::categories().indexOf (subCategory));
    }

    Generator makeFM()
    {
        Generator g;
        g.id = "gen.FM";
        g.label = fmRow;
        g.tooltipTitle = "FM";
        g.tooltipBody = "Clears the patch and builds a 6-operator FM voice from modules with every tap: six DX "
                        "Operators wired to the voice's algorithm (laid out as the algorithm chart, operators "
                        "folded - tap a card's top tab to open it), a DX Pitch EG and a DX LFO for the pitch "
                        "envelope and vibrato, and the mod wheel on the vibrato. The arrow on the right ticks a "
                        "sub-category (" + dx7::categories().joinIntoString (", ") + "; tick it again for all), "
                        "or loads a DX7 .syx file and lists its voices.";

        g.random = [] (juce::uint32 seed) { return fmBuilder (seed, tickedOf (fmRow)); };
        g.selection = [] { return tickedOf (fmRow); };

        g.fillMenu = [] (juce::PopupMenu& menu, const MenuContext& ctx)
        {
            menu.addSectionHeader ("FM");
            addTickItems (menu, fmRow, dx7::categories(), ctx);

            const auto apply = ctx.apply;
            if (! loadedFiles().empty())
            {
                menu.addSectionHeader ("Loaded");
                for (const auto& bank : loadedFiles())
                    menu.addSubMenu (bank.title, fileMenu (bank, apply));
            }

            menu.addSeparator();
            const auto pickFile = ctx.pickFile;
            juce::Component::SafePointer<juce::Component> anchor (ctx.anchor);
            menu.addItem ("Load DX7 SysEx (.syx)...", [pickFile, apply, anchor]
            {
                if (pickFile == nullptr)
                    return;
                pickFile ("Load DX7 SysEx", "*.syx;*.SYX", [apply, anchor] (juce::MemoryBlock data, juce::String name)
                {
                    auto bank = dx7::parseSysex (data, name.isNotEmpty() ? name : juce::String ("SysEx"));
                    if (bank.voices.empty())
                    {
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "FM",
                                                                "No DX7 voices found in that file.");
                        return;
                    }

                    // remember it for later menus, then offer its voices right away
                    auto& files = loadedFiles();
                    files.erase (std::remove_if (files.begin(), files.end(),
                                                 [&bank] (const dx7::Bank& b) { return b.title == bank.title; }),
                                 files.end());
                    files.insert (files.begin(), bank);
                    if (files.size() > 8)
                        files.pop_back();

                    auto menuOfFile = fileMenu (bank, apply);
                    auto opts = juce::PopupMenu::Options();
                    if (anchor != nullptr)
                        opts = opts.withTargetComponent (anchor.getComponent());
                    menuOfFile.showMenuAsync (opts);
                });
            });
        };
        return g;
    }

    //==========================================================================
    // Random: any category, any engine (the ticks are ignored)
    //==========================================================================
    Generator makeRandom()
    {
        Generator g;
        g.id = "gen.random";
        g.label = "Random";
        g.tooltipTitle = "Random";
        g.tooltipBody = "Clears the patch and builds a completely random one with every tap: any category, "
                        "any engine, FM included.";

        g.random = [] (juce::uint32 seed) -> BuildFn
        {
            const auto& table = categoryTable();
            Dice dice (seed);
            const int pick = dice.index ((int) table.size() + 1);   // + 1 = FM
            const auto next = seed * 2654435761u + 7u;
            if (pick == (int) table.size())
                return fmBuilder (next, {});
            return categoryBuilder (table[(size_t) pick], next, {});
        };
        return g;
    }
}

const std::vector<Generator>& generators()
{
    static const std::vector<Generator> list = []
    {
        std::vector<Generator> v;
        for (const auto& cat : categoryTable())
            v.push_back (makeCategory (cat));
        v.push_back (makeFM());

        std::sort (v.begin(), v.end(), [] (const Generator& a, const Generator& b)
                   { return a.label.compareNatural (b.label) < 0; });

        v.push_back (makeRandom());   // always last
        return v;
    }();
    return list;
}

const Generator* findGenerator (const juce::String& id)
{
    for (const auto& g : generators())
        if (g.id == id)
            return &g;
    return nullptr;
}

} // namespace aquanode::presets
