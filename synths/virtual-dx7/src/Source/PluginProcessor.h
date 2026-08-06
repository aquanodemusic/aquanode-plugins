/*
    PluginProcessor.h  -  VDX7-Dexed audio processor.

    This single header contains everything the processor side needs:

      1. vdx7::Voice / vdx7::Bank   - the DX7 patch data model (VCED 155 <-> VMEM
         128 pack/unpack, SysEx builders, factory-ROM access).  JUCE-free so the
         exact same code can be unit-tested against the emulator.

      2. vdx7::DX7Engine            - a thin real-time wrapper that owns the VDX7
         DX7 emulation (real firmware ROM on an emulated HD6303 CPU + OPS/EGS),
         boots it on a background thread, and exchanges MIDI / audio / commands
         with it in a lock-free way.

      3. VDX7AudioProcessor         - the juce::AudioProcessor itself.

    GPLv3.  Voice pack/unpack layout follows the public DX7 SysEx specification
    (as also implemented by Dexed).
*/
#pragma once
#include <JuceHeader.h>

#include <atomic>
#include <thread>
#include <deque>
#include <mutex>
#include <memory>
#include <vector>
#include <array>
#include <string>

#include "FxChain.h"     // global effects placed after the engine
#include "RomStore.h"    // user-supplied firmware / factory voices
#include "NativeFM.h"    // the ROM-free fallback engine
#include "MtsEsp.h"      // optional MTS-ESP microtuning (native engine only)
#include <cstdint>
#include <cstring>
#include <cmath>

// Forward declarations from the emulator (External/Synth.h), so this header
// stays free of the heavy engine include.
class DX7Synth;
struct App_ToSynth;
struct App_ToGui;

namespace vdx7 {

// ============================================================================
//  Patch data model
// ============================================================================
static constexpr int kVcedSize      = 155;   // unpacked, one parameter per byte
static constexpr int kVmemSize      = 128;   // packed, as kept in DX7 RAM
static constexpr int kNumOps        = 6;
static constexpr int kOpVcedStride  = 21;

// VCED byte offsets within one 21-byte operator block.
enum OpParam {
    OP_R1=0, OP_R2, OP_R3, OP_R4,   // EG rates
    OP_L1, OP_L2, OP_L3, OP_L4,     // EG levels
    OP_BP,                          // 8  break point
    OP_LD,                          // 9  L scale depth
    OP_RD,                          // 10 R scale depth
    OP_LC,                          // 11 L scale curve (0..3)
    OP_RC,                          // 12 R scale curve (0..3)
    OP_RS,                          // 13 rate scaling (0..7)
    OP_AMS,                         // 14 amp-mod sensitivity (0..3)
    OP_KVS,                         // 15 key-velocity sensitivity (0..7)
    OP_OL,                          // 16 output level (0..99)
    OP_MODE,                        // 17 oscillator mode (0 ratio / 1 fixed)
    OP_FC,                          // 18 frequency coarse (0..31)
    OP_FF,                          // 19 frequency fine (0..99)
    OP_DET                          // 20 detune (0..14, 7 = centre)
};

// VCED global byte offsets.
enum GlobalParam {
    G_PR1=126, G_PR2, G_PR3, G_PR4, // pitch-EG rates
    G_PL1, G_PL2, G_PL3, G_PL4,     // pitch-EG levels
    G_ALG=134,                      // algorithm (0..31)
    G_FB=135,                       // feedback (0..7)
    G_OKS=136,                      // oscillator key sync (0..1)
    G_LFS=137,                      // LFO speed (0..99)
    G_LFD=138,                      // LFO delay (0..99)
    G_LPMD=139,                     // LFO pitch-mod depth (0..99)
    G_LAMD=140,                     // LFO amp-mod depth (0..99)
    G_LFKS=141,                     // LFO key sync (0..1)
    G_LFW=142,                      // LFO wave (0..5)
    G_LPMS=143,                     // LFO pitch-mod sensitivity (0..7)
    G_TRANSPOSE=144,                // transpose (0..48, 24 = C3)
    G_NAME=145                      // 10 chars
};

struct ParamInfo {
    const char* name;
    int vcedOffset;   // absolute offset into the 155-byte VCED
    int minVal;
    int maxVal;
};

// A DX7 voice / patch.  Operator ordering follows DX7/Dexed convention:
// op index 0 == OP6 (first 21-byte block), op index 5 == OP1.
class Voice {
public:
    Voice() { initDefault(); }

    uint8_t*       vced()       { return data_.data(); }
    const uint8_t* vced() const { return data_.data(); }

    uint8_t get (int vcedOffset) const { return data_[(size_t) vcedOffset]; }
    void    set (int vcedOffset, int value);   // clamps to that param's range

    uint8_t getOp (int op, int opParam) const { return data_[(size_t)(op*kOpVcedStride + opParam)]; }
    void    setOp (int op, int opParam, int v){ set (op*kOpVcedStride + opParam, v); }

    std::string name() const;
    void setName (const std::string& n);

    int algorithm() const { return data_[G_ALG]; }

    void pack   (uint8_t* dst) const;   // VCED -> 128-byte VMEM (dst holds 128)
    void unpack (const uint8_t* src);   // 128-byte VMEM -> VCED

    void initDefault();                 // a simple, audible "INIT VOICE"

    // Single-voice bulk dump SysEx (163 bytes): F0 43 0n 00 01 1B ... F7
    std::vector<uint8_t> toSingleVoiceSysex (uint8_t channel = 0) const;

    static const std::vector<ParamInfo>& paramTable();
    static int clampToRange (int vcedOffset, int value);

private:
    std::array<uint8_t, kVcedSize> data_{};
};

// A bank of 32 voices (packed 32*128 = 4096 bytes, as in DX7 internal RAM).
class Bank {
public:
    Bank() { data_.fill (0); }
    explicit Bank (const uint8_t* packed4096) { load (packed4096); }

    void load (const uint8_t* packed4096) { std::memcpy (data_.data(), packed4096, 4096); }
    const uint8_t* packed() const { return data_.data(); }
    uint8_t*       packed()       { return data_.data(); }

    Voice voice (int idx) const {
        Voice v; v.unpack (data_.data() + (size_t) idx*kVmemSize); return v;
    }
    void setVoice (int idx, const Voice& v) {
        v.pack (data_.data() + (size_t) idx*kVmemSize);
    }
    std::string voiceName (int idx) const;

    // 32-voice bulk dump SysEx (4104 bytes): F0 43 0n 09 20 00 ... F7
    std::vector<uint8_t> toBankSysex (uint8_t channel = 0) const;

private:
    std::array<uint8_t, 4096> data_{};
};

// Access to the 8 factory banks. When the given instance's RomStore has a
// factory voice ROM loaded these are its ROM1A..ROM4B; otherwise there is a
// single bundled starter bank of original patches, repeated so the selector
// always has something to offer. Each plugin instance owns its own RomStore
// (VDX7AudioProcessor::roms_), so which of these two an instance sees is
// entirely its own business - it never depends on what any other instance
// has loaded.
Bank        factoryBank (const RomStore& roms, int bank);
const char* factoryBankName (const RomStore& roms, int bank);
bool        usingStarterBank (const RomStore& roms);   // true when no voice ROM is loaded
static constexpr int kNumFactoryBanks = 8;

// The bundled starter bank on its own, with no RomStore involved. Used for
// the APVTS's static default parameter values (created before any instance,
// and so before any RomStore, exists) and by factoryBank() as its fallback.
const Bank& starterBank();

// Scan an arbitrary byte range for a standard DX7 32-voice bulk dump
// (4104-byte SysEx: F0 43 0n 09 20 00 <4096> csum F7) and, if found, load its
// 4096 packed voice bytes into `out`.  Returns true on success.  Shared by the
// bank-import and save-into-bank code paths.
bool findBankInSysex (const uint8_t* data, size_t len, Bank& out);

// ============================================================================
//  Real-time engine wrapper
// ============================================================================
// Two engines live behind this one interface:
//
//   Emulator - the original bit-accurate path. Needs the firmware ROM, boots an
//              HD6303 on a background thread, and takes a couple of seconds.
//   Native   - NativeFM.h. No ROM, ready instantly, calibrated against the
//              emulator but running in floating point.
//
// Which one is live depends purely on whether a firmware ROM has been loaded.
// The processor and the editor above never need to know which it is: the same
// MIDI goes in, the same audio and the same 2x16 display come out.
class DX7Engine {
public:
    enum class Backend { Native, Emulator };

    // `roms` is this engine's owning instance's own RomStore - never shared
    // with any other instance. See RomPointerScope in PluginProcessor.cpp for
    // how its bytes reach the (necessarily process-wide) emulator pointers.
    explicit DX7Engine (RomStore& roms);
    ~DX7Engine();

    // Called from prepareToPlay (audio stopped): starts the background boot the
    // first time and updates the sample rate thereafter.
    void prepare (double sampleRate);

    // Ready to make sound. The native engine is always ready; the emulator only
    // once its boot thread has finished.
    bool isReady() const
    {
        return backend() == Backend::Native
                 || ready_.load (std::memory_order_acquire);
    }

    Backend backend() const
    {
        return emulatorWanted_.load (std::memory_order_acquire) ? Backend::Emulator
                                                                : Backend::Native;
    }

    // True when a firmware ROM is present but the emulator is still booting.
    bool isBooting() const
    {
        return backend() == Backend::Emulator
                 && ! ready_.load (std::memory_order_acquire);
    }

    // Called when the ROM situation changes. Starts the emulator boot if a
    // firmware image has appeared; falls back to the native engine if it has
    // gone away. Message thread.
    void romsChanged();

    // Mirrors the patch into whichever engine is not currently driving the
    // audio, so switching backends does not lose the sound.
    void setNativeVoice (const uint8_t vced[155]);

    // Microtuning. Only meaningful on the native backend: the DX7 firmware has
    // no tuning parameter, so the emulated engine is always 12-TET.
    vdx7native::Tuning& tuning() { return native_.tuning(); }
    const vdx7native::Tuning& tuning() const { return native_.tuning(); }
    bool microtuningAvailable() const { return backend() == Backend::Native; }

    // Only true on the native backend: mts_ is switched off while a firmware
    // ROM is loaded, so this cannot claim microtuning that is not happening.
    bool mtsEspActive() const { return mts_.hasMaster(); }
    bool mtsEspCompiledIn() const { return vdx7::MtsEspClient::isCompiledIn(); }
    std::string mtsScaleName() const { return mts_.scaleName(); }

    void pushMidi   (const uint8_t* bytes, int len);   // audio thread
    void renderMono (float* out, int n);               // audio thread

    // Commands (any thread; applied on the audio thread).
    void loadVoice         (const uint8_t packed128[128]);
    void loadBankAndSelect (const uint8_t packed4096[4096], int prog);
    void pressButton       (int ctrlId);
    void setInternalProtect(bool on);
    void sendParamChange   (int vcedOffset, int value); // live single-parameter edit

    // Display readback (audio thread writes, any thread reads).
    void getLcd (char line1[17], char line2[17]) const;
    int  led1() const { return led1_.load (std::memory_order_relaxed); }
    int  led2() const { return led2_.load (std::memory_order_relaxed); }
    double sampleRate() const { return sampleRate_; }

private:
    enum class Cmd : uint8_t { LoadVoice, LoadBank, Button, Protect, ParamChange };
    struct Command {
        Cmd type;
        int  ival  = 0;
        int  ival2 = 0;
        bool bval  = false;
        std::unique_ptr<uint8_t[]> blob;   // 128 or 4096 bytes
    };
    void pushCommand  (std::unique_ptr<Command> c);
    void drainCommands();
    void applyCommand (Command& c);

    struct SchedBtn { int chunksLeft; int ctrlId; bool down; };
    std::deque<SchedBtn> sched_;
    void scheduleSelect (int prog);
    void pumpSchedule();

    void bootThreadFn (double fs);
    void updateDisplaySnapshot();
    void updateNativeDisplay();

    RomStore&                roms_;   // owning instance's ROM data; never shared

    // mts_ is declared BEFORE native_ deliberately. native_'s Tuning holds
    // std::functions that capture a pointer to this client, so the client has
    // to outlive them: members are destroyed in reverse declaration order, and
    // this way the callbacks go first and the client second.
    vdx7::MtsEspClient       mts_;
    vdx7native::NativeEngine native_;
    std::atomic<bool> emulatorWanted_ { false };
    uint8_t nativeVced_[155] {};

    App_ToSynth* toSynth_ = nullptr;
    App_ToGui*   toGui_   = nullptr;
    DX7Synth*    synth_   = nullptr;

    std::atomic<bool> ready_{false};
    std::atomic<bool> bootStarted_{false};
    std::thread bootThread_;
    double sampleRate_ = 48000.0;

    static constexpr int kChunk = 128;
    float chunkBuf_[kChunk];
    int   chunkPos_ = kChunk;

    static constexpr int kCmdCap = 64;
    std::unique_ptr<Command> ring_[kCmdCap];
    std::atomic<int> head_{0}, tail_{0};
    std::mutex ringMutex_;

    mutable std::mutex lcdMutex_;
    char lcd1_[17]{}; char lcd2_[17]{};
    std::atomic<int> led1_{-1}, led2_{-1};
};

} // namespace vdx7

// ============================================================================
//  juce::AudioProcessor
// ============================================================================
class VDX7AudioProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster
{public:
    VDX7AudioProcessor();
    ~VDX7AudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VDX7-Dexed"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // The FDN reverb rings well past the emulator's own 2 s envelope tail.
    double getTailLengthSeconds() const override { return 8.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int sizeInBytes) override;

    // ---- DAW-automatable parameters ------------------------------------
    // Every editable VCED parameter (all six operators + the global / LFO /
    // pitch-EG block, 145 params in total; the 10 name bytes are the only
    // non-automated fields) is exposed through this APVTS so hosts can read,
    // write and automate them.  The editor binds its knobs to these via
    // SliderParameterAttachments.
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String paramId (int vcedOffset) { return "p" + juce::String (vcedOffset); }
    juce::RangedAudioParameter* paramForOffset (int vcedOffset) const;

    // ---- editor API (message thread) ----
    // The authoritative "current voice" = the last-loaded patch (which carries
    // the name and any non-automated bytes) with the live APVTS values laid on
    // top, so it always reflects knob moves and host automation.
    vdx7::Voice getVoiceCopy() const {
        vdx7::Voice v = currentVoice_;
        for (const auto& ap : autoParams_)
            v.set (ap.offset, (int) std::lround (ap.raw->load (std::memory_order_relaxed)));
        return v;
    }
    void setParam       (int vcedOffset, int value);
    void setVoice       (const vdx7::Voice& v, bool sendNow = true);
    void selectFactory  (int bank, int prog);

    bool importSysexBank (const void* data, size_t len,
                          const juce::String& displayName = {}); // true if a bank was found
    std::vector<uint8_t> exportVoiceSysex() const;         // 163-byte .syx
    std::vector<uint8_t> exportBankSysex()  const;         // 4104-byte .syx

    bool       hasUserBank() const { return hasUserBank_; }
    bool       userActive()  const { return userActive_; }
    vdx7::Bank userBank()    const { return userBank_; }
    juce::String userBankName() const { return userBankName_; }
    void       selectUserProgram (int prog);
    int        currentBank() const { return bank_; }
    int        currentProg() const { return prog_; }

    juce::MidiKeyboardState keyboardState;   // on-screen keyboard

    bool engineReady() const { return engine_.isReady(); }

    // ---- ROM loading (message thread) ----
    // `source` is shown to the user and, on desktop, remembered so the next
    // session reloads it without asking.
    bool loadFirmwareRom (const void* data, size_t size,
                          const juce::String& source, juce::String& errorOut);
    bool loadFactoryVoices (const void* data, size_t size,
                            const juce::String& source, juce::String& errorOut);
    void forgetRoms();

    bool usingEmulator() const
        { return engine_.backend() == vdx7::DX7Engine::Backend::Emulator; }
    bool engineBooting() const { return engine_.isBooting(); }
    juce::String engineDescription() const;
    void getLcd (char l1[17], char l2[17]) const { engine_.getLcd (l1, l2); }
    int  led1() const { return engine_.led1(); }
    int  led2() const { return engine_.led2(); }

    // This instance's own ROM data. Read-only from the outside (the editor
    // uses it to describe what's loaded); loading/clearing goes through
    // loadFirmwareRom() / loadFactoryVoices() / forgetRoms() above so the
    // saved-state bookkeeping and the engine reboot always happen together.
    const vdx7::RomStore& roms() const { return roms_; }

private:
    void sendCurrentVoice();
    void buildAutoParams();                       // link APVTS params <-> VCED offsets
    void pushVoiceToApvts (const vdx7::Voice& v);  // reflect a loaded patch in the APVTS
    void loadVoiceIntoModel (const vdx7::Voice& v, bool broadcast = true);
    void restoreFxState (const juce::ValueTree& vt);   // reads the "FX" child of a saved state
    void afterRomChange();                             // re-boot, re-send the patch, tell the UI

    // Where the ROMs came from, carried in the plugin state so a reopened
    // project finds them again without asking.
    juce::String romPath_, voicesPath_;

    // This instance's own ROM bytes. Declared before engine_ so it is fully
    // constructed first - engine_ keeps a reference to it. By default this is
    // empty: a freshly-inserted instance has no ROM loaded until the user
    // loads one or a saved project state restores it (see
    // setStateInformation), and that choice is never shared with any other
    // instance in the project.
    vdx7::RomStore roms_;
    vdx7::DX7Engine engine_;
    vdx7::Voice     currentVoice_;
    vdx7::Bank      userBank_;
    bool            hasUserBank_ = false;
    bool            userActive_  = false;
    juce::String    userBankName_;
    int bank_ = 0, prog_ = 0;

    // Maps each automatable APVTS parameter to its VCED offset + a real-time
    // atomic value pointer.  The audio thread diffs these against lastSent_ each
    // block and forwards only the changes to the emulator (so both knob edits
    // and host automation drive the sound through one path).
    struct AutoParam { int offset; juce::RangedAudioParameter* param; std::atomic<float>* raw; };
    std::vector<AutoParam> autoParams_;
    std::vector<uint8_t>   lastSent_;             // audio-thread-owned baseline
    std::atomic<bool>      baselineReload_ { true };

    std::vector<float> mono_;   // scratch, sized in prepareToPlay
    std::vector<float> scratchR_;   // scratch right channel for a mono output bus

    // Global effects, applied to the emulator's output. Their parameters live
    // in the same APVTS but under "fx..." IDs, outside the "p<offset>" VCED
    // namespace, so they are automatable without ever being mistaken for a
    // patch byte by the autoParams_ diff above.
    vdx7fx::FxChain fx_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VDX7AudioProcessor)
};
