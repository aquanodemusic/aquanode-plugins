/*  PluginProcessor.cpp  -  see PluginProcessor.h.  GPLv3.  */
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include "External/Synth.h"   // DX7Synth, App_ToSynth, App_ToGui, Message, dx7.h
#include "StarterBank.h"      // bundled original patches, used when no ROM is loaded

// ============================================================================
//  Firmware and factory-voice data.
//
//  Nothing is embedded. The emulator (External/dx7.cc) reaches the ROM only
//  through these two process-wide globals - that is baked into how dx7.cc is
//  written and isn't worth changing. Each plugin instance now owns its own
//  ROM bytes in its own RomStore (VDX7AudioProcessor::roms_), so these two
//  pointers can't just be set once and left alone any more: several
//  instances with different ROM situations can exist at once, and each has
//  to see its own bytes here, not another instance's.
//
//  The fix is RomPointerScope, just below: every place that is about to call
//  into the emulator (booting it, or running a chunk of audio) wraps that one
//  call in a RomPointerScope built from its own instance's RomStore. The
//  scope takes a mutex, points these two globals at that instance's bytes,
//  and only then lets the call happen; the mutex is released again as soon
//  as the scope ends. Because dx7.cc only ever reads these pointers
//  synchronously while such a call is running (the firmware is copied into
//  the emulator's own per-instance memory the moment it's read - see
//  DX7::loadFirmwareFromMemory - and a factory-bank select does the same via
//  DX7::setBank), this is enough to make every instance's audio correct
//  regardless of what any other instance is doing, at the cost of a
//  vanishingly short lock around each individual emulator call rather than
//  anything held for the length of a boot or a whole audio block.
// ============================================================================
const uint8_t* vdx7_firmware_rom   = nullptr;
const uint8_t* vdx7_factory_voices = nullptr;

namespace vdx7 {

namespace {
    std::mutex g_romPointerMutex;

    // vdx7_factory_voices is dereferenced unconditionally by DX7::setBank()
    // (it can be reached at any time via a bank-select SysEx or MIDI CC32),
    // so it always has to point at 8 real banks' worth of data, even for an
    // instance that has no voice ROM loaded. This is the starter bank
    // repeated 8 times, built once and shared read-only by everyone (it's
    // the same bundled data for every instance, so sharing it is fine -
    // unlike the user's own ROM bytes, nothing here is instance-specific).
    const uint8_t* starterVoicesFlat()
    {
        static const std::vector<uint8_t> flat = [] {
            std::vector<uint8_t> v (kVoicesSize);
            for (int b = 0; b < kNumFactoryBanksMax; ++b)
                std::memcpy (v.data() + (size_t) b * kBankSize, starterBank().packed(), kBankSize);
            return v;
        }();
        return flat.data();
    }
}

// RAII guard: while it's alive, the two emulator-global ROM pointers point at
// `roms`'s own bytes, and no other instance can change them out from under
// it. Construct one immediately before, and only around, a call into the
// emulator (dx7.cc) - never hold it for longer than that single call.
class RomPointerScope
{
public:
    explicit RomPointerScope (const RomStore& roms) : lock_ (g_romPointerMutex)
    {
        vdx7_firmware_rom   = roms.firmwareData();
        vdx7_factory_voices = roms.hasVoices() ? roms.voicesData() : starterVoicesFlat();
    }
private:
    std::lock_guard<std::mutex> lock_;
};

// ----------------------------------------------------------------------------
//  Voice: pack / unpack (VCED 155 <-> VMEM 128)
// ----------------------------------------------------------------------------
void Voice::pack (uint8_t* bulk) const {
    const uint8_t* src = data_.data();
    for (int op = 0; op < kNumOps; ++op) {
        uint8_t* pp = bulk + op * 17;
        const uint8_t* up = src + op * 21;
        std::memcpy (pp, up, 11);                                   // R1-4,L1-4,BP,LD,RD
        pp[11] = (up[OP_LC] & 0x03) | ((up[OP_RC] & 0x03) << 2);    // curves
        pp[12] = (up[OP_RS] & 0x07) | ((up[OP_DET] & 0x0F) << 3);   // rate scale + detune
        pp[13] = (up[OP_AMS] & 0x03) | ((up[OP_KVS] & 0x07) << 2);  // ams + kvs
        pp[14] =  up[OP_OL];                                        // output level
        pp[15] = (up[OP_MODE] & 0x01) | ((up[OP_FC] & 0x1F) << 1);  // mode + coarse
        pp[16] =  up[OP_FF];                                        // fine
    }
    std::memcpy (bulk + 102, src + 126, 9);                         // pitch EG(8) + algorithm
    bulk[111] = (src[G_FB] & 0x07) | ((src[G_OKS] & 0x01) << 3);    // feedback + osc key sync
    std::memcpy (bulk + 112, src + 137, 4);                         // LFO speed/delay/pmd/amd
    bulk[116] = (src[G_LFKS] & 0x01)
              | ((src[G_LFW]  & 0x07) << 1)
              | ((src[G_LPMS] & 0x07) << 4);                        // lfo sync/wave/pms
    bulk[117] = src[G_TRANSPOSE];
    for (int i = 0; i < 10; ++i) bulk[118 + i] = src[G_NAME + i];   // name
}

static inline uint8_t clamp7 (uint8_t v, uint8_t max) { v &= 0x7F; return v <= max ? v : max; }

void Voice::unpack (const uint8_t* bulk) {
    uint8_t* dst = data_.data();
    for (int op = 0; op < kNumOps; ++op) {
        const uint8_t* pp = bulk + op * 17;
        uint8_t* up = dst + op * 21;
        for (int i = 0; i < 11; ++i) up[i] = clamp7 (pp[i], 99);
        up[OP_BP]  = clamp7 (pp[8], 99);
        up[OP_LD]  = clamp7 (pp[9], 99);
        up[OP_RD]  = clamp7 (pp[10], 99);
        uint8_t curves = pp[11] & 0x0F;
        up[OP_LC]  = curves & 0x03;
        up[OP_RC]  = (curves >> 2) & 0x03;
        uint8_t rsdet = pp[12];
        up[OP_RS]  = rsdet & 0x07;
        up[OP_DET] = (rsdet >> 3) & 0x0F;
        uint8_t amskvs = pp[13];
        up[OP_AMS] = amskvs & 0x03;
        up[OP_KVS] = (amskvs >> 2) & 0x07;
        up[OP_OL]  = clamp7 (pp[14], 99);
        uint8_t modecoarse = pp[15];
        up[OP_MODE]= modecoarse & 0x01;
        up[OP_FC]  = (modecoarse >> 1) & 0x1F;
        up[OP_FF]  = clamp7 (pp[16], 99);
    }
    for (int i = 0; i < 8; ++i) dst[126 + i] = clamp7 (bulk[102 + i], 99); // pitch EG
    dst[G_ALG] = clamp7 (bulk[110], 31);
    dst[G_FB]  = bulk[111] & 0x07;
    dst[G_OKS] = (bulk[111] >> 3) & 0x01;
    dst[G_LFS] = clamp7 (bulk[112], 99);
    dst[G_LFD] = clamp7 (bulk[113], 99);
    dst[G_LPMD]= clamp7 (bulk[114], 99);
    dst[G_LAMD]= clamp7 (bulk[115], 99);
    uint8_t lfo = bulk[116];
    dst[G_LFKS]= lfo & 0x01;
    dst[G_LFW] = (lfo >> 1) & 0x07;
    dst[G_LPMS]= (lfo >> 4) & 0x07;
    dst[G_TRANSPOSE] = clamp7 (bulk[117], 48);
    for (int i = 0; i < 10; ++i) {
        uint8_t c = bulk[118 + i] & 0x7F;
        dst[G_NAME + i] = (c < 32 || c > 126) ? ' ' : c;
    }
}

std::string Voice::name() const {
    std::string s ((const char*) data_.data() + G_NAME, 10);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void Voice::setName (const std::string& n) {
    for (int i = 0; i < 10; ++i) {
        char c = i < (int) n.size() ? n[i] : ' ';
        if (c < 32 || c > 126) c = ' ';
        data_[G_NAME + i] = (uint8_t) c;
    }
}

int Voice::clampToRange (int off, int value) {
    for (const auto& p : paramTable())
        if (p.vcedOffset == off) {
            if (value < p.minVal) return p.minVal;
            if (value > p.maxVal) return p.maxVal;
            return value;
        }
    if (value < 0) return 0;
    if (value > 99) return 99;
    return value;
}

void Voice::set (int off, int value) {
    if (off < 0 || off >= kVcedSize) return;
    data_[(size_t) off] = (uint8_t) clampToRange (off, value);
}

void Voice::initDefault() {
    data_.fill (0);
    for (int op = 0; op < kNumOps; ++op) {
        uint8_t* u = data_.data() + op * 21;
        u[OP_R1]=99; u[OP_R2]=99; u[OP_R3]=99; u[OP_R4]=60;
        u[OP_L1]=99; u[OP_L2]=99; u[OP_L3]=99; u[OP_L4]=0;
        u[OP_BP]=39; u[OP_LD]=0; u[OP_RD]=0; u[OP_LC]=0; u[OP_RC]=0;
        u[OP_RS]=0; u[OP_AMS]=0; u[OP_KVS]=0;
        u[OP_OL]= (op == 5) ? 99 : 0;    // op index 5 == OP1 carrier
        u[OP_MODE]=0; u[OP_FC]=1; u[OP_FF]=0; u[OP_DET]=7;
    }
    for (int i = 0; i < 8; ++i) data_[126 + i] = (i < 4) ? 99 : 50;
    data_[G_PL1]=50; data_[G_PL2]=50; data_[G_PL3]=50; data_[G_PL4]=50;
    data_[G_ALG]=0;  data_[G_FB]=0; data_[G_OKS]=1;
    data_[G_LFS]=35; data_[G_LFD]=0; data_[G_LPMD]=0; data_[G_LAMD]=0;
    data_[G_LFKS]=1; data_[G_LFW]=0; data_[G_LPMS]=3;
    data_[G_TRANSPOSE]=24;
    setName ("INIT VOICE");
}

const std::vector<ParamInfo>& Voice::paramTable() {
    static const std::vector<ParamInfo> table = []{
        std::vector<ParamInfo> t;
        struct OpF { const char* n; int off; int mn; int mx; };
        static const OpF of[] = {
            {"EG R1",OP_R1,0,99},{"EG R2",OP_R2,0,99},{"EG R3",OP_R3,0,99},{"EG R4",OP_R4,0,99},
            {"EG L1",OP_L1,0,99},{"EG L2",OP_L2,0,99},{"EG L3",OP_L3,0,99},{"EG L4",OP_L4,0,99},
            {"Break Pt",OP_BP,0,99},{"L Depth",OP_LD,0,99},{"R Depth",OP_RD,0,99},
            {"L Curve",OP_LC,0,3},{"R Curve",OP_RC,0,3},{"Rate Scale",OP_RS,0,7},
            {"Amp Mod",OP_AMS,0,3},{"Vel Sens",OP_KVS,0,7},{"Output",OP_OL,0,99},
            {"Mode",OP_MODE,0,1},{"Coarse",OP_FC,0,31},{"Fine",OP_FF,0,99},{"Detune",OP_DET,0,14},
        };
        for (int op = 0; op < 6; ++op)
            for (const auto& f : of)
                t.push_back ({ f.n, op*21 + f.off, f.mn, f.mx });
        static const ParamInfo g[] = {
            {"Pitch R1",G_PR1,0,99},{"Pitch R2",G_PR2,0,99},{"Pitch R3",G_PR3,0,99},{"Pitch R4",G_PR4,0,99},
            {"Pitch L1",G_PL1,0,99},{"Pitch L2",G_PL2,0,99},{"Pitch L3",G_PL3,0,99},{"Pitch L4",G_PL4,0,99},
            {"Algorithm",G_ALG,0,31},{"Feedback",G_FB,0,7},{"Osc Sync",G_OKS,0,1},
            {"LFO Speed",G_LFS,0,99},{"LFO Delay",G_LFD,0,99},{"LFO PMD",G_LPMD,0,99},{"LFO AMD",G_LAMD,0,99},
            {"LFO Sync",G_LFKS,0,1},{"LFO Wave",G_LFW,0,5},{"P Mod Sens",G_LPMS,0,7},{"Transpose",G_TRANSPOSE,0,48},
        };
        for (const auto& gi : g) t.push_back (gi);
        return t;
    }();
    return table;
}

std::vector<uint8_t> Voice::toSingleVoiceSysex (uint8_t channel) const {
    std::vector<uint8_t> m; m.reserve (163);
    m.push_back (0xF0); m.push_back (0x43); m.push_back (0x00 | (channel & 0x0F));
    m.push_back (0x00); m.push_back (0x01); m.push_back (0x1B);      // format 0, 155 bytes
    uint8_t sum = 0;
    for (int i = 0; i < kVcedSize; ++i) { m.push_back (data_[(size_t) i]); sum += data_[(size_t) i]; }
    m.push_back ((uint8_t) ((-sum) & 0x7F));
    m.push_back (0xF7);
    return m;
}

std::string Bank::voiceName (int idx) const { return voice (idx).name(); }

std::vector<uint8_t> Bank::toBankSysex (uint8_t channel) const {
    std::vector<uint8_t> m; m.reserve (4104);
    m.push_back (0xF0); m.push_back (0x43); m.push_back (0x00 | (channel & 0x0F));
    m.push_back (0x09); m.push_back (0x20); m.push_back (0x00);      // format 9, 4096 bytes
    uint8_t sum = 0;
    for (int i = 0; i < 4096; ++i) { m.push_back (data_[(size_t) i]); sum += data_[(size_t) i]; }
    m.push_back ((uint8_t) ((-sum) & 0x7F));
    m.push_back (0xF7);
    return m;
}

bool usingStarterBank (const RomStore& roms) { return ! roms.hasVoices(); }

// The bundled starter bank, packed once on first use.
const Bank& starterBank()
{
    static const Bank b = []
    {
        Bank tmp;
        for (int i = 0; i < 32; ++i)
        {
            uint8_t vced[kVcedSize];
            vdx7starter::buildVced (i, vced);
            Voice v;
            std::memcpy (v.vced(), vced, kVcedSize);
            tmp.setVoice (i, v);
        }
        return tmp;
    }();
    return b;
}

Bank factoryBank (const RomStore& roms, int bank) {
    bank = juce::jlimit (0, kNumFactoryBanks - 1, bank);

    Bank out;
    if (roms.copyBank (bank, out.packed()))
        return out;

    return starterBank();
}

const char* factoryBankName (const RomStore& roms, int bank) {
    if (bank < 0 || bank >= kNumFactoryBanks) return "ROM";

    if (usingStarterBank (roms))
        return "STARTER";

    static const char* names[kNumFactoryBanks] =
        { "ROM1A","ROM1B","ROM2A","ROM2B","ROM3A","ROM3B","ROM4A","ROM4B" };
    return names[bank];
}

bool findBankInSysex (const uint8_t* p, size_t len, Bank& out) {
    if (p == nullptr) return false;
    for (size_t i = 0; i + 4104 <= len; ++i)
        if (p[i]==0xF0 && p[i+1]==0x43 && p[i+3]==0x09 && p[i+4]==0x20
            && p[i+5]==0x00 && p[i+4103]==0xF7)
        {
            out.load (p + i + 6);
            return true;
        }
    return false;
}

// ----------------------------------------------------------------------------
//  DX7Engine
// ----------------------------------------------------------------------------
// Panel-button CtrlID numeric values (from Message::CtrlID).
static constexpr int kB_dash = 37;  // INTERNAL (memory select / play)
static constexpr int kB_x    = 33;  // INTERNAL memory protect
static constexpr int kB_no   = 40;  // -1 / OFF
static constexpr int kB_yes  = 41;  // +1 / ON
static constexpr int kB_sp   = 39;  // FUNCTION
static constexpr int kB_8     = 7;  // MIDI / SYS INFO group
// b_1 == 0 ... b_32 == 31

// The emulated firmware's note mapping sits one semitone flat of standard
// MIDI (see DX7Engine::pushMidi): +1 here means MIDI C4 sounds as C4 instead
// of B3. If a different ROM dump ever needs a different correction, this is
// the only place it has to change.
static constexpr int kEmulatorNoteBias = 1;

DX7Engine::DX7Engine (RomStore& roms) : roms_ (roms) {
    toSynth_ = new App_ToSynth();
    toGui_   = new App_ToGui();
    synth_   = new DX7Synth (nullptr);   // no ram file; we build RAM ourselves
    synth_->toGui   = toGui_;
    synth_->toSynth = toSynth_;

    vdx7native::NativeEngine::defaultVced (nativeVced_);
    native_.setVoice (nativeVced_);

    // If an MTS-ESP master is running in this session, follow it. With the
    // library absent this hands over empty functions and changes nothing.
    // Microtuning belongs to the native engine only, so the client is switched
    // off for as long as a firmware ROM is in charge.
    if (auto p = mts_.provider())
        native_.tuning().setFrequencyProvider (std::move (p));
    if (auto f = mts_.noteFilter())
        native_.tuning().setNoteFilter (std::move (f));
    mts_.setEnabled (! roms_.hasFirmware());
    emulatorWanted_.store (roms_.hasFirmware(), std::memory_order_release);
    updateNativeDisplay();
}

DX7Engine::~DX7Engine() {
    if (bootThread_.joinable()) bootThread_.join();
    delete synth_;   synth_   = nullptr;
    delete toGui_;   toGui_   = nullptr;
    delete toSynth_; toSynth_ = nullptr;
}

void DX7Engine::prepare (double sr) {
    sampleRate_ = sr;
    native_.prepare (sr);

    // The emulator is only worth booting if there is a firmware ROM to boot it
    // from; without one the native engine covers everything.
    if (! roms_.hasFirmware())
        return;

    if (! bootStarted_.exchange (true))
        bootThread_ = std::thread (&DX7Engine::bootThreadFn, this, sr);
    else if (ready_.load (std::memory_order_acquire))
        synth_->setSampleRate (sr);      // audio stopped in prepareToPlay; safe
}

// A ROM has been loaded or cleared. Message thread.
void DX7Engine::romsChanged() {
    const bool haveFirmware = roms_.hasFirmware();
    emulatorWanted_.store (haveFirmware, std::memory_order_release);

    // Loading firmware turns microtuning off, clearing it turns microtuning
    // back on: the real DX7's tuning is fixed, and the emulator is the real
    // DX7. See MtsEsp.h for why that is the hardware's limitation, not ours.
    mts_.setEnabled (! haveFirmware);

    if (haveFirmware && ! bootStarted_.exchange (true))
        bootThread_ = std::thread (&DX7Engine::bootThreadFn, this, sampleRate_);

    if (! haveFirmware)
        updateNativeDisplay();
}

void DX7Engine::setNativeVoice (const uint8_t vced[155]) {
    std::memcpy (nativeVced_, vced, 155);
    native_.setVoice (vced);
    if (backend() == Backend::Native)
        updateNativeDisplay();
}

// The native engine has no HD44780 to read back, so it reports the same
// information the DX7's play mode would: what is loaded, and on which engine.
void DX7Engine::updateNativeDisplay() {
    char name[11] = {};
    for (int i = 0; i < 10; ++i) {
        const unsigned char c = nativeVced_[145 + i];
        name[i] = (c < 32 || c > 126) ? ' ' : (char) c;
    }
    native_.setLcd ("NATIVE FM ENGINE", name);
}

void DX7Engine::bootThreadFn (double fs) {
    // Each call into the emulator gets its own short-lived RomPointerScope so
    // the two global ROM pointers are only ever "ours" for the instant a call
    // is actually happening - never for the whole boot (which takes a couple
    // of seconds and must not block any other instance's audio thread).
    auto runSecs = [&](double s){
        int n = (int)(s * fs / kChunk);
        for (int i=0;i<n;i++) { RomPointerScope rp (roms_); synth_->run(); }
    };
    auto pressDirect = [&](int ctrlId){
        auto id = (Message::CtrlID) ctrlId;
        toSynth_->buttondown (id); runSecs (0.06);
        toSynth_->buttonup   (id); runSecs (0.10);
    };
    { RomPointerScope rp (roms_); synth_->dx7.loadFirmwareFromMemory(); }
    synth_->start();

    // Battery-backed RAM. The original project shipped a dump of a real
    // machine's memory for this; it is not needed. The firmware is happy to
    // start from an image we assemble ourselves - 4 KB of internal voices
    // followed by a zeroed function/system area, which it validates and repairs
    // on the way up.
    {
        uint8_t fallback[4096];
        std::memcpy (fallback, factoryBank (roms_, 0).packed(), 4096);
        const auto ram = roms_.buildBatteryRam (fallback);
        std::memcpy (&synth_->dx7.memory[0x1000], ram.data(), ram.size());
    }
    toSynth_->analog (Message::CtrlID::battery, 82);                    // battery OK
    synth_->setSampleRate (fs);

    runSecs (2.5);                       // let the firmware finish its boot self-test

    // Enable "SYS INFO AVAIL" so the firmware accepts parameter-change SysEx
    // (this is what makes live single-parameter edits take effect).
    pressDirect (kB_sp);                 // FUNCTION
    pressDirect (kB_8); pressDirect (kB_8);   // cycle to SYS INFO UNAVAIL
    pressDirect (kB_yes);                // -> SYS INFO AVAIL

    pressDirect (kB_x);                  // INTERNAL memory protect...
    pressDirect (kB_no);                 // ...OFF, so voice loads/stores are unhindered
    pressDirect (kB_dash);               // INTERNAL play mode
    pressDirect (0);                     // select voice 1 (b_1 == 0)
    runSecs (0.1);

    updateDisplaySnapshot();
    ready_.store (true, std::memory_order_release);
}

void DX7Engine::pushCommand (std::unique_ptr<Command> c) {
    std::lock_guard<std::mutex> lk (ringMutex_);
    int h = head_.load (std::memory_order_relaxed);
    int nxt = (h + 1) % kCmdCap;
    if (nxt == tail_.load (std::memory_order_acquire)) return;   // full: drop
    ring_[h] = std::move (c);
    head_.store (nxt, std::memory_order_release);
}

void DX7Engine::drainCommands() {
    for (;;) {
        int t = tail_.load (std::memory_order_relaxed);
        if (t == head_.load (std::memory_order_acquire)) break;
        std::unique_ptr<Command> c = std::move (ring_[t]);
        tail_.store ((t + 1) % kCmdCap, std::memory_order_release);
        if (c) applyCommand (*c);
    }
}

void DX7Engine::applyCommand (Command& c) {
    // Patch changes go to the native engine too, whichever backend is live, so
    // the two never drift apart.
    switch (c.type) {
        case Cmd::LoadVoice:
            if (c.blob) {
                Voice v; v.unpack (c.blob.get());
                std::memcpy (nativeVced_, v.vced(), kVcedSize);
                native_.setVoice (nativeVced_);
                if (backend() == Backend::Native) updateNativeDisplay();
            }
            break;
        case Cmd::LoadBank:
            if (c.blob) {
                Voice v; v.unpack (c.blob.get() + (size_t) juce::jlimit (0, 31, c.ival) * 128);
                std::memcpy (nativeVced_, v.vced(), kVcedSize);
                native_.setVoice (nativeVced_);
                if (backend() == Backend::Native) updateNativeDisplay();
            }
            break;
        case Cmd::ParamChange:
            if (c.ival >= 0 && c.ival < kVcedSize) {
                nativeVced_[c.ival] = (uint8_t) juce::jlimit (0, 127, c.ival2);
                native_.setParam (c.ival, c.ival2);
                if (c.ival >= G_NAME && backend() == Backend::Native) updateNativeDisplay();
            }
            break;
        default: break;
    }

    // Everything below is the emulator's own state machine; with no ROM there
    // is nothing to drive.
    if (backend() == Backend::Native || ! ready_.load (std::memory_order_acquire))
        return;

    switch (c.type) {
        case Cmd::LoadVoice:
            if (c.blob) {
                std::memcpy (&synth_->dx7.memory[0x1000], c.blob.get(), 128);
                scheduleSelect (0);        // reselect voice 1 -> reload edit buffer
            }
            break;
        case Cmd::LoadBank:
            if (c.blob) {
                std::memcpy (&synth_->dx7.memory[0x1000], c.blob.get(), 4096);
                scheduleSelect (c.ival);   // select program c.ival (0..31)
            }
            break;
        case Cmd::Button:
            sched_.push_back ({0,  c.ival, true});
            sched_.push_back ({20, c.ival, false});
            break;
        case Cmd::Protect:
            sched_.push_back ({0,  kB_x, true});  sched_.push_back ({20, kB_x, false});
            { int yn = c.bval ? kB_yes : kB_no;
              sched_.push_back ({28, yn, true});  sched_.push_back ({48, yn, false}); }
            break;
        case Cmd::ParamChange: {
            // DX7 voice parameter change: F0 43 1n g p v F7  (g = offset>127, p = offset&0x7F)
            uint8_t m[7] = { 0xF0, 0x43, 0x10,
                             (uint8_t) (c.ival > 127 ? 1 : 0),
                             (uint8_t) (c.ival & 0x7F),
                             (uint8_t) (c.ival2 & 0x7F), 0xF7 };
            synth_->queueMidiRx (7, m);
            break;
        }
    }
}

void DX7Engine::scheduleSelect (int prog) {
    prog = juce::jlimit (0, 31, prog);
    sched_.push_back ({0,  kB_dash, true });
    sched_.push_back ({20, kB_dash, false});
    sched_.push_back ({28, prog,    true });
    sched_.push_back ({48, prog,    false});
}

void DX7Engine::pumpSchedule() {
    for (auto it = sched_.begin(); it != sched_.end(); ) {
        if (it->chunksLeft <= 0) {
            auto id = (Message::CtrlID) it->ctrlId;
            if (it->down) toSynth_->buttondown (id); else toSynth_->buttonup (id);
            it = sched_.erase (it);
        } else { it->chunksLeft--; ++it; }
    }
}

void DX7Engine::loadVoice (const uint8_t packed128[128]) {
    auto c = std::make_unique<Command>();
    c->type = Cmd::LoadVoice;
    c->blob.reset (new uint8_t[128]);
    std::memcpy (c->blob.get(), packed128, 128);
    pushCommand (std::move (c));
}

void DX7Engine::loadBankAndSelect (const uint8_t packed4096[4096], int prog) {
    auto c = std::make_unique<Command>();
    c->type = Cmd::LoadBank; c->ival = prog;
    c->blob.reset (new uint8_t[4096]);
    std::memcpy (c->blob.get(), packed4096, 4096);
    pushCommand (std::move (c));
}

void DX7Engine::pressButton (int ctrlId) {
    auto c = std::make_unique<Command>(); c->type = Cmd::Button; c->ival = ctrlId;
    pushCommand (std::move (c));
}

void DX7Engine::setInternalProtect (bool on) {
    auto c = std::make_unique<Command>(); c->type = Cmd::Protect; c->bval = on;
    pushCommand (std::move (c));
}

void DX7Engine::sendParamChange (int vcedOffset, int value) {
    auto c = std::make_unique<Command>();
    c->type = Cmd::ParamChange; c->ival = vcedOffset; c->ival2 = value;
    pushCommand (std::move (c));
}

void DX7Engine::pushMidi (const uint8_t* bytes, int len) {
    if (len <= 0) return;

    // Both engines are fed, always. The emulator may still be booting, and the
    // native engine has to stay in step so that switching between them mid-note
    // does not strand a key down.
    native_.pushMidi (bytes, len);

    if (backend() == Backend::Emulator && ready_.load (std::memory_order_acquire)) {
        // The emulated HD6303 firmware maps MIDI note numbers to its internal
        // key numbers one semitone flat of what every other MIDI device (and
        // the native engine, which was calibrated against real hardware
        // rather than against this firmware's own note math) expects: MIDI
        // C4 (60) arrives at the emulator and sounds as B3. kEmulatorNoteBias
        // corrects that at the door, for note on/off only, so everything
        // else (velocity, channel, all other message types) passes through
        // exactly as received.
        if (len == 3 && ((bytes[0] & 0xF0) == 0x80 || (bytes[0] & 0xF0) == 0x90)) {
            uint8_t fixed[3] = { bytes[0], bytes[1], bytes[2] };
            const int note = (int) fixed[1] + kEmulatorNoteBias;
            fixed[1] = (uint8_t) juce::jlimit (0, 127, note);
            synth_->queueMidiRx (3, fixed);
        } else {
            synth_->queueMidiRx ((uint32_t) len, bytes);
        }
    }
}

void DX7Engine::renderMono (float* out, int n) {
    // Native engine: no boot, no command queue, no chunking.
    if (backend() == Backend::Native || ! ready_.load (std::memory_order_acquire)) {
        drainCommands();
        native_.render (out, n);
        return;
    }

    drainCommands();
    int i = 0;
    while (i < n) {
        if (chunkPos_ >= kChunk) {
            pumpSchedule();
            { RomPointerScope rp (roms_); synth_->run(); }
            std::memcpy (chunkBuf_, synth_->outputBuffer, sizeof (float) * kChunk);
            chunkPos_ = 0;
        }
        int take = kChunk - chunkPos_;
        if (take > n - i) take = n - i;
        std::memcpy (out + i, chunkBuf_ + chunkPos_, sizeof (float) * (size_t) take);
        chunkPos_ += take; i += take;
    }
    updateDisplaySnapshot();
}

void DX7Engine::updateDisplaySnapshot() {
    const uint8_t* st = nullptr;
    synth_->dx7.lcd.save (st);
    char l1[17], l2[17];
    for (int k = 0; k < 16; ++k) {
        unsigned char a = st ? st[k]      : ' ';
        unsigned char b = st ? st[40 + k] : ' ';
        l1[k] = (a < 32 || a > 126) ? ' ' : (char) a;
        l2[k] = (b < 32 || b > 126) ? ' ' : (char) b;
    }
    l1[16] = l2[16] = 0;
    {
        std::lock_guard<std::mutex> lk (lcdMutex_);
        std::memcpy (lcd1_, l1, 17);
        std::memcpy (lcd2_, l2, 17);
    }
    led1_.store (synth_->dx7.P_LED1, std::memory_order_relaxed);
    led2_.store (synth_->dx7.P_LED2, std::memory_order_relaxed);
}

void DX7Engine::getLcd (char line1[17], char line2[17]) const {
    if (backend() == Backend::Native || ! ready_.load (std::memory_order_acquire)) {
        native_.getLcd (line1, line2);
        return;
    }
    std::lock_guard<std::mutex> lk (lcdMutex_);
    std::memcpy (line1, lcd1_, 17);
    std::memcpy (line2, lcd2_, 17);
}

} // namespace vdx7

// ============================================================================
//  VDX7AudioProcessor
// ============================================================================
VDX7AudioProcessor::VDX7AudioProcessor()
    : juce::AudioProcessor (BusesProperties()
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VDX7", createLayout()),
      engine_ (roms_)
{
    buildAutoParams();
    fx_.bindParameters (apvts);

    // By default a freshly-inserted instance has no ROM loaded (roms_ starts
    // empty) and runs the native engine, entirely independently of whatever
    // ROM any other instance in the project has or hasn't loaded. A saved
    // project brings its own ROM back via setStateInformation(), which
    // re-loads from romPath_/voicesPath_ - this instance's own remembered
    // location, not a shared one.

    currentVoice_ = vdx7::factoryBank (roms_, 0).voice (0);   // first patch of bank 1
    bank_ = 0; prog_ = 0;
    pushVoiceToApvts (currentVoice_);                  // seed the APVTS with that patch
}

juce::AudioProcessorValueTreeState::ParameterLayout VDX7AudioProcessor::createLayout()
{
    using namespace vdx7;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Static, called before any instance (and so before any RomStore) exists,
    // so this always uses the bundled starter bank rather than a ROM.
    const Voice def = starterBank().voice (0);         // sensible defaults
    for (const auto& pi : Voice::paramTable())
    {
        const int off = pi.vcedOffset;
        juce::String name;
        if (off < kNumOps * kOpVcedStride) {            // per-operator parameter
            const int blk   = off / kOpVcedStride;      // 0..5  (block 0 == OP6)
            const int opNum = kNumOps - blk;            // 6..1  front-panel number
            name = "OP" + juce::String (opNum) + " " + pi.name;
        } else {
            name = pi.name;
        }
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { paramId (off), 1 }, name,
            pi.minVal, pi.maxVal, (int) def.get (off)));
    }

    // Global effects. These use "fx..." IDs, so buildAutoParams() - which walks
    // Voice::paramTable() - never picks them up and no FX value can end up
    // being sent to the emulator as a VCED byte.
    vdx7fx::FxChain::addParameters (layout);

    return layout;
}

juce::RangedAudioParameter* VDX7AudioProcessor::paramForOffset (int off) const
{
    return apvts.getParameter (paramId (off));
}

void VDX7AudioProcessor::buildAutoParams()
{
    autoParams_.clear();
    for (const auto& pi : vdx7::Voice::paramTable()) {
        auto* rp  = apvts.getParameter        (paramId (pi.vcedOffset));
        auto* raw = apvts.getRawParameterValue (paramId (pi.vcedOffset));
        if (rp && raw) autoParams_.push_back ({ pi.vcedOffset, rp, raw });
    }
    lastSent_.assign (autoParams_.size(), 0);
}

void VDX7AudioProcessor::pushVoiceToApvts (const vdx7::Voice& v)
{
    for (auto& ap : autoParams_) {
        const float val = (float) v.get (ap.offset);
        ap.param->setValueNotifyingHost (ap.param->convertTo0to1 (val));
    }
}

VDX7AudioProcessor::~VDX7AudioProcessor() = default;

void VDX7AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine_.prepare (sampleRate);
    mono_.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    fx_.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    sendCurrentVoice();
}

bool VDX7AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VDX7AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numOutCh   = buffer.getNumChannels();

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // Chord section: expands each note-on/off already in the buffer into up
    // to 6 extra transposed notes, in place, before anything is forwarded to
    // the engine - so the added notes are voices the engine itself renders.
    fx_.processMidi (midi);

    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        engine_.pushMidi (msg.getRawData(), msg.getRawDataSize());
    }
    midi.clear();

    // Forward parameter changes (knob edits *and* host automation both land in
    // the APVTS) to the emulator.  We diff the atomic parameter values against
    // the last values sent; only the deltas become live parameter-change SysEx.
    // After a whole-patch load (baselineReload_), we simply adopt the new values
    // as the baseline so the wholesale voice load handles the sound instead of
    // emitting 145 redundant edits.
    if (engine_.isReady())
    {
        const bool reload = baselineReload_.exchange (false, std::memory_order_acquire);
        for (size_t i = 0; i < autoParams_.size(); ++i)
        {
            const int v = (int) std::lround (autoParams_[i].raw->load (std::memory_order_relaxed));
            if (reload) {
                lastSent_[i] = (uint8_t) v;
            } else if ((uint8_t) v != lastSent_[i]) {
                lastSent_[i] = (uint8_t) v;
                engine_.sendParamChange (autoParams_[i].offset, v);
            }
        }
    }

    if ((int) mono_.size() < numSamples) mono_.assign ((size_t) numSamples, 0.0f);
    engine_.renderMono (mono_.data(), numSamples);

    for (int ch = 0; ch < numOutCh; ++ch)
        buffer.copyFrom (ch, 0, mono_.data(), numSamples);

    // Global effects, after the emulator. The chain is stereo (the chorus,
    // phaser and reverb all widen a mono source), so on a mono bus we run it
    // against a scratch right channel and fold the result back down.
    if (numOutCh >= 2)
    {
        fx_.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    }
    else if (numOutCh == 1)
    {
        if ((int) scratchR_.size() < numSamples) scratchR_.assign ((size_t) numSamples, 0.0f);
        juce::FloatVectorOperations::copy (scratchR_.data(), mono_.data(), numSamples);

        float* l = buffer.getWritePointer (0);
        fx_.process (l, scratchR_.data(), numSamples);

        for (int n = 0; n < numSamples; ++n)
            l[n] = (l[n] + scratchR_[(size_t) n]) * 0.5f;
    }
}

void VDX7AudioProcessor::sendCurrentVoice()
{
    uint8_t packed[128];
    currentVoice_.pack (packed);
    engine_.loadVoice (packed);
}

// ---------------------------------------------------------------------------
//  ROM loading
// ---------------------------------------------------------------------------
bool VDX7AudioProcessor::loadFirmwareRom (const void* data, size_t size,
                                          const juce::String& source,
                                          juce::String& errorOut)
{
    if (! roms_.loadFirmware (data, size, source, errorOut))
        return false;

    romPath_ = source;   // remembered in this instance's own plugin state only
    afterRomChange();
    return true;
}

bool VDX7AudioProcessor::loadFactoryVoices (const void* data, size_t size,
                                            const juce::String& source,
                                            juce::String& errorOut)
{
    if (! roms_.loadVoices (data, size, source, errorOut))
        return false;

    voicesPath_ = source;   // remembered in this instance's own plugin state only
    afterRomChange();
    return true;
}

void VDX7AudioProcessor::forgetRoms()
{
    roms_.clearFirmware();
    roms_.clearVoices();
    romPath_.clear();
    voicesPath_.clear();
    afterRomChange();
}

void VDX7AudioProcessor::afterRomChange()
{
    engine_.romsChanged();

    // A voice ROM appearing or disappearing changes what the bank selector is
    // pointing at, so re-resolve the current slot and push it through again.
    if (! userActive_)
        currentVoice_ = vdx7::factoryBank (roms_, bank_).voice (prog_);

    loadVoiceIntoModel (currentVoice_);   // broadcasts, so any open editor refreshes
}

juce::String VDX7AudioProcessor::engineDescription() const
{
    if (engine_.isBooting()) return "Emulator (booting...)";
    if (usingEmulator())     return "Emulator (firmware ROM)";

    juce::String s ("Native FM (no ROM)");
    if (engine_.mtsEspActive())
    {
        s += " - MTS-ESP";

        // A master that names its scale gets the name shown, trimmed so a long
        // one cannot push the rest of the line out of the label.
        const juce::String scale (engine_.mtsScaleName());
        if (scale.isNotEmpty())
            s += " (" + scale.substring (0, 16).trim() + ")";
    }
    else if (engine_.tuning().isActive())    s += " - microtuned";
    return s;
}

// Load a whole patch: make it the model base, mirror it into the APVTS (so the
// host + UI follow), and load it into the emulator wholesale.
void VDX7AudioProcessor::loadVoiceIntoModel (const vdx7::Voice& v, bool broadcast)
{
    currentVoice_ = v;
    engine_.setNativeVoice (v.vced());
    pushVoiceToApvts (v);
    sendCurrentVoice();
    baselineReload_.store (true, std::memory_order_release);
    if (broadcast) sendChangeMessage();
}

void VDX7AudioProcessor::setParam (int vcedOffset, int value)
{
    // Route single-parameter edits through the APVTS so the host sees the write;
    // the audio-thread diff then forwards the change to the emulator.
    if (auto* p = paramForOffset (vcedOffset))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
}

std::vector<uint8_t> VDX7AudioProcessor::exportVoiceSysex() const
{
    return getVoiceCopy().toSingleVoiceSysex (0);
}

std::vector<uint8_t> VDX7AudioProcessor::exportBankSysex() const
{
    vdx7::Bank b = (userActive_ && hasUserBank_) ? userBank_ : vdx7::factoryBank (roms_, bank_);
    b.setVoice (prog_, getVoiceCopy());
    return b.toBankSysex (0);
}

void VDX7AudioProcessor::setVoice (const vdx7::Voice& v, bool /*sendNow*/)
{
    loadVoiceIntoModel (v);
}

void VDX7AudioProcessor::selectFactory (int bank, int prog)
{
    bank_ = juce::jlimit (0, vdx7::kNumFactoryBanks - 1, bank);
    prog_ = juce::jlimit (0, 31, prog);
    userActive_ = false;
    loadVoiceIntoModel (vdx7::factoryBank (roms_, bank_).voice (prog_));
}

bool VDX7AudioProcessor::importSysexBank (const void* data, size_t len,
                                          const juce::String& displayName)
{
    vdx7::Bank parsed;
    if (! vdx7::findBankInSysex (static_cast<const uint8_t*> (data), len, parsed))
        return false;
    userBank_    = parsed;
    hasUserBank_ = true;
    if (displayName.isNotEmpty()) userBankName_ = displayName;
    selectUserProgram (0);
    return true;
}

void VDX7AudioProcessor::selectUserProgram (int prog)
{
    if (! hasUserBank_) return;
    prog_ = juce::jlimit (0, 31, prog);
    userActive_ = true;
    loadVoiceIntoModel (userBank_.voice (prog_));
}

void VDX7AudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    const vdx7::Voice v = getVoiceCopy();     // includes live edits/automation + name
    juce::ValueTree vt ("VDX7STATE");
    vt.setProperty ("bank",       bank_,       nullptr);
    vt.setProperty ("prog",       prog_,       nullptr);
    vt.setProperty ("userActive", userActive_, nullptr);
    vt.setProperty ("vced",
        juce::MemoryBlock (v.vced(), vdx7::kVcedSize).toBase64Encoding(), nullptr);
    if (hasUserBank_) {
        vt.setProperty ("userbank",
            juce::MemoryBlock (userBank_.packed(), 4096).toBase64Encoding(), nullptr);
        vt.setProperty ("userbankname", userBankName_, nullptr);
    }

    // Where the ROMs were loaded from. Saving the location rather than the
    // bytes is the whole point: the project file stays free of Yamaha's data,
    // and reopening it on a machine that has the ROM finds it again by itself.
    if (romPath_.isNotEmpty())    vt.setProperty ("romPath",    romPath_,    nullptr);
    if (voicesPath_.isNotEmpty()) vt.setProperty ("voicesPath", voicesPath_, nullptr);

    // FX settings are not part of the DX7 patch (they have no VCED bytes), so
    // they ride along as their own child tree rather than in "vced".
    juce::ValueTree fxTree ("FX");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith ("fx"))
                fxTree.setProperty (rp->paramID, rp->convertFrom0to1 (rp->getValue()), nullptr);
    vt.appendChild (fxTree, nullptr);

    if (auto xml = vt.createXml())
        copyXmlToBinary (*xml, dest);
}

// Restores the "FX" child written above. Missing properties keep their
// defaults, so a state saved before the FX section existed still loads.
void VDX7AudioProcessor::restoreFxState (const juce::ValueTree& vt)
{
    auto fxTree = vt.getChildWithName ("FX");
    if (! fxTree.isValid())
        return;

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith ("fx") && fxTree.hasProperty (rp->paramID))
                rp->setValueNotifyingHost (
                    rp->convertTo0to1 ((float) fxTree.getProperty (rp->paramID)));
}

void VDX7AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // New XML format.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto vt = juce::ValueTree::fromXml (*xml);
        if (vt.isValid() && vt.hasType ("VDX7STATE"))
        {
            juce::MemoryBlock vced;
            vced.fromBase64Encoding (vt.getProperty ("vced").toString());
            if (vced.getSize() >= (size_t) vdx7::kVcedSize)
            {
                bank_       = (int)  vt.getProperty ("bank", 0);
                prog_       = (int)  vt.getProperty ("prog", 0);
                userActive_ = (bool) vt.getProperty ("userActive", false);

                // Re-open the ROMs *this instance* was saved with, if they are
                // still where they were. This is this instance's own state -
                // it never looks at or affects any other instance's ROMs.
                // Failing is not an error: the plugin simply keeps running the
                // native engine, and the user can point it at the files again
                // from the ROM menu.
                {
                    const auto rp = vt.getProperty ("romPath",    "").toString();
                    const auto vp = vt.getProperty ("voicesPath", "").toString();

                    bool changed = false;
                    if (rp.isNotEmpty() && ! roms_.hasFirmware())
                        changed |= roms_.tryLoadFromPath (rp, true);
                    if (vp.isNotEmpty() && ! roms_.hasVoices())
                        changed |= roms_.tryLoadFromPath (vp, false);

                    romPath_    = roms_.firmwareSource();
                    voicesPath_ = roms_.voicesSource();
                    if (changed) engine_.romsChanged();
                }

                if (vt.hasProperty ("userbank")) {
                    juce::MemoryBlock ub;
                    ub.fromBase64Encoding (vt.getProperty ("userbank").toString());
                    if (ub.getSize() >= 4096) {
                        userBank_.load (static_cast<const uint8_t*> (ub.getData()));
                        hasUserBank_  = true;
                        userBankName_ = vt.getProperty ("userbankname", "USER").toString();
                    }
                }

                vdx7::Voice v;
                std::memcpy (v.vced(), vced.getData(), vdx7::kVcedSize);
                loadVoiceIntoModel (v);
                restoreFxState (vt);
                return;
            }
        }
    }

    // Legacy binary format ('VDX7' + 155 VCED + bank + prog).
    if (sizeInBytes < 4 + vdx7::kVcedSize) return;
    juce::MemoryInputStream is (data, (size_t) sizeInBytes, false);
    if (is.readInt() != 0x56445837) return;
    uint8_t vced[vdx7::kVcedSize];
    is.read (vced, vdx7::kVcedSize);
    vdx7::Voice v;
    std::memcpy (v.vced(), vced, vdx7::kVcedSize);
    if (is.getNumBytesRemaining() >= 8) { bank_ = is.readInt(); prog_ = is.readInt(); }
    loadVoiceIntoModel (v);
}

juce::AudioProcessorEditor* VDX7AudioProcessor::createEditor()
{
    return new VDX7AudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VDX7AudioProcessor();
}