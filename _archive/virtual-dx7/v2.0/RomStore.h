/*
    RomStore.h  -  runtime loading of the DX7 firmware and factory voice data.

    Nothing copyrighted ships with this plugin. The bit-accurate engine needs
    Yamaha's firmware ROM to run at all, and the factory cartridge banks are
    likewise Yamaha's, so both are loaded from files the user points us at.

    What the user can supply:

      dx7.bin     16384 bytes   the HD6303 firmware. Required for the emulated
                                engine; without it the plugin runs the native
                                engine instead (see NativeFM.h).
      voices.bin  32768 bytes   the eight factory cartridge banks, 4096 each.
                                Optional. Without it the factory bank selector
                                offers the bundled starter bank instead.

    The third file the original project embedded, example.ram, is deliberately
    NOT loadable here: it is a dump of a real machine's battery-backed memory
    and is not needed. The firmware boots perfectly well from a RAM image we
    synthesise ourselves out of whatever voice bank is available, which is what
    buildBatteryRam() below does.

    Persistence differs by platform, and has to:

      - Desktop hosts get a plain filesystem path, saved into the plugin state
        so reopening a project reloads the ROM silently, and also into a global
        settings file so a freshly-inserted instance finds it too.
      - Android returns a document URI from the picker. Those are not
        filesystem paths and the permission behind them does not reliably
        survive an app restart, so the URI is remembered as a convenience but a
        failure to re-open it is treated as normal, not as an error.

    GPLv3.
*/
#pragma once
#include <JuceHeader.h>

#include <vector>
#include <mutex>
#include <memory>
#include <cstdint>
#include <cstring>

namespace vdx7 {

static constexpr size_t kFirmwareSize   = 16384;
static constexpr size_t kBankSize       = 4096;
static constexpr int    kNumFactoryBanksMax = 8;
static constexpr size_t kVoicesSize     = kBankSize * kNumFactoryBanksMax;   // 32768
static constexpr size_t kBatteryRamSize = 6144;

// ============================================================================
//  RomStore
// ============================================================================
// A process-wide singleton, because External/dx7.cc reaches the ROM through two
// global pointers (`vdx7_firmware_rom`, `vdx7_factory_voices`) and the emulator
// boots once per plugin instance from whatever those point at. Holding the
// bytes in one place also means several instances in the same session share a
// single 16 KB copy and only one of them has to be pointed at the file.
class RomStore
{
public:
    static RomStore& get()
    {
        static RomStore instance;
        return instance;
    }

    // ---- state ------------------------------------------------------------
    bool hasFirmware() const { std::lock_guard<std::mutex> l (m_); return ! firmware_.empty(); }
    bool hasVoices()   const { std::lock_guard<std::mutex> l (m_); return ! voices_.empty(); }

    juce::String firmwareSource() const { std::lock_guard<std::mutex> l (m_); return firmwareSrc_; }
    juce::String voicesSource()   const { std::lock_guard<std::mutex> l (m_); return voicesSrc_; }

    // ---- loading ----------------------------------------------------------
    // `source` is whatever we should show the user and try to reload later: a
    // file path on desktop, a document URI on Android.
    bool loadFirmware (const void* data, size_t size,
                       const juce::String& source, juce::String& errorOut)
    {
        if (data == nullptr || size == 0)
        {
            errorOut = "The file could not be read.";
            return false;
        }

        // Some dumps are the two 8 KB halves concatenated, which is already the
        // right thing; some are padded to 32 KB by doubling. Accept both, plus
        // anything longer that starts with a plausible image.
        const uint8_t* p = static_cast<const uint8_t*> (data);

        if (size < kFirmwareSize)
        {
            errorOut = "That file is " + juce::String ((int) size)
                     + " bytes; the DX7 firmware ROM is 16384.";
            return false;
        }

        std::vector<uint8_t> image (p, p + kFirmwareSize);

        if (! looksLikeFirmware (image))
        {
            errorOut = "That does not look like a DX7 firmware ROM "
                       "(the 6303 reset vector is missing).";
            return false;
        }

        {
            std::lock_guard<std::mutex> l (m_);
            firmware_   = std::move (image);
            firmwareSrc_ = source;
        }
        publish();
        return true;
    }

    bool loadVoices (const void* data, size_t size,
                     const juce::String& source, juce::String& errorOut)
    {
        if (data == nullptr || size < kBankSize)
        {
            errorOut = "That file is too small to contain a 32-voice bank.";
            return false;
        }

        const uint8_t* p = static_cast<const uint8_t*> (data);

        // Accept the full 32 KB set, or any whole number of 4 KB banks; short
        // files are padded by repeating what we were given, so the bank
        // selector always has eight entries to offer.
        const size_t usable = juce::jmin (size, kVoicesSize);
        const size_t banks  = usable / kBankSize;

        if (banks == 0)
        {
            errorOut = "That file is not a whole number of 4096-byte banks.";
            return false;
        }

        std::vector<uint8_t> image (kVoicesSize);
        for (size_t b = 0; b < (size_t) kNumFactoryBanksMax; ++b)
            std::memcpy (image.data() + b * kBankSize,
                         p + (b % banks) * kBankSize, kBankSize);

        {
            std::lock_guard<std::mutex> l (m_);
            voices_    = std::move (image);
            voicesSrc_ = source;
            numRealBanks_ = (int) banks;
        }
        publish();
        return true;
    }

    void clearFirmware()
    {
        { std::lock_guard<std::mutex> l (m_); firmware_.clear(); firmwareSrc_.clear(); }
        publish();
    }

    void clearVoices()
    {
        { std::lock_guard<std::mutex> l (m_); voices_.clear(); voicesSrc_.clear(); numRealBanks_ = 0; }
        publish();
    }

    int numRealBanks() const { std::lock_guard<std::mutex> l (m_); return numRealBanks_; }

    // ---- access -----------------------------------------------------------
    // Copies out one 4096-byte bank. Returns false if no voice ROM is loaded,
    // in which case the caller should fall back to the starter bank.
    bool copyBank (int index, uint8_t* dest4096) const
    {
        std::lock_guard<std::mutex> l (m_);
        if (voices_.empty()) return false;
        index = juce::jlimit (0, kNumFactoryBanksMax - 1, index);
        std::memcpy (dest4096, voices_.data() + (size_t) index * kBankSize, kBankSize);
        return true;
    }

    // Builds the 6 KB battery-backed RAM image the emulator starts from:
    // 4096 bytes of internal voice memory followed by 2048 bytes of function
    // and system parameters. The firmware validates and repairs that second
    // region itself on boot, so zeros are fine there.
    std::vector<uint8_t> buildBatteryRam (const uint8_t* fallbackBank4096) const
    {
        std::vector<uint8_t> ram (kBatteryRamSize, 0);

        std::lock_guard<std::mutex> l (m_);
        if (! voices_.empty())
            std::memcpy (ram.data(), voices_.data(), kBankSize);
        else if (fallbackBank4096 != nullptr)
            std::memcpy (ram.data(), fallbackBank4096, kBankSize);

        return ram;
    }

    // ---- global settings (desktop path recall) ----------------------------
    static juce::PropertiesFile& settings()
    {
        static std::unique_ptr<juce::PropertiesFile> file;
        if (file == nullptr)
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "VirtualDX7";
            o.filenameSuffix      = "settings";
            o.folderName          = "VirtualDX7";
            o.osxLibrarySubFolder = "Application Support";
            file = std::make_unique<juce::PropertiesFile> (o);
        }
        return *file;
    }

    static void rememberPath (const juce::String& key, const juce::String& value)
    {
        settings().setValue (key, value);
        settings().saveIfNeeded();
    }

    static juce::String recallPath (const juce::String& key)
    {
        return settings().getValue (key, {});
    }

    // Reads a remembered location and loads it, quietly. Desktop only: on
    // Android a remembered URI usually cannot be reopened without the picker,
    // and that is expected rather than an error.
    bool tryLoadFromPath (const juce::String& path, bool isFirmware)
    {
       #if JUCE_ANDROID
        juce::ignoreUnused (path, isFirmware);
        return false;
       #else
        if (path.isEmpty()) return false;

        juce::File f (path);
        if (! f.existsAsFile()) return false;

        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) return false;

        juce::String err;
        return isFirmware ? loadFirmware (mb.getData(), mb.getSize(), path, err)
                          : loadVoices   (mb.getData(), mb.getSize(), path, err);
       #endif
    }

    // Broadcast so every open editor can refresh its status line.
    juce::ChangeBroadcaster changed;

private:
    RomStore() = default;

    // The 6303 fetches its reset vector from the top of the address space, so a
    // genuine DX7 image ends with a pointer back into ROM. It is a weak check,
    // but it catches the common mistakes (picking a .syx, a zip, or an 8 KB
    // half) without rejecting legitimate variants.
    static bool looksLikeFirmware (const std::vector<uint8_t>& image)
    {
        if (image.size() < kFirmwareSize) return false;

        const uint16_t reset = (uint16_t) ((image[kFirmwareSize - 2] << 8)
                                          | image[kFirmwareSize - 1]);
        if (reset < 0xC000) return false;          // must point into the ROM

        // An all-zero or all-0xFF image passes the vector test by accident.
        size_t same = 0;
        for (size_t i = 1; i < 512; ++i)
            if (image[i] == image[0]) ++same;
        return same < 500;
    }

    void publish()
    {
        // Hand the emulator its pointers. Safe to call while an instance is
        // running only in the sense that dx7.cc reads them at boot; the
        // processor takes care to re-boot its engine after a ROM change.
        {
            std::lock_guard<std::mutex> l (m_);
            extern_firmware_ = firmware_.empty() ? nullptr : firmware_.data();
            extern_voices_   = voices_.empty()   ? nullptr : voices_.data();
        }
        installPointers();
        juce::MessageManager::callAsync ([this] { changed.sendChangeMessage(); });
    }

    void installPointers();   // defined in PluginProcessor.cpp, next to the globals

    mutable std::mutex m_;
    std::vector<uint8_t> firmware_, voices_;
    juce::String firmwareSrc_, voicesSrc_;
    int numRealBanks_ = 0;

public:
    const uint8_t* extern_firmware_ = nullptr;
    const uint8_t* extern_voices_   = nullptr;
};

// Keys used for the desktop settings file and for the plugin state.
static const char* const kRomPathKey    = "firmwareRomPath";
static const char* const kVoicesPathKey = "factoryVoicesPath";

} // namespace vdx7
