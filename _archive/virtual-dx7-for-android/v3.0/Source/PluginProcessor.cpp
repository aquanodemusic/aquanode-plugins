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
const uint8_t* vdx7_firmware_rom = nullptr;
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
                std::vector<uint8_t> v(kVoicesSize);
                for (int b = 0; b < kNumFactoryBanksMax; ++b)
                    std::memcpy(v.data() + (size_t)b * kBankSize, starterBank().packed(), kBankSize);
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
        explicit RomPointerScope(const RomStore& roms) : lock_(g_romPointerMutex)
        {
            vdx7_firmware_rom = roms.firmwareData();
            vdx7_factory_voices = roms.hasVoices() ? roms.voicesData() : starterVoicesFlat();
        }
    private:
        std::lock_guard<std::mutex> lock_;
    };

    // ----------------------------------------------------------------------------
    //  Voice: pack / unpack (VCED 155 <-> VMEM 128)
    // ----------------------------------------------------------------------------
    void Voice::pack(uint8_t* bulk) const {
        const uint8_t* src = data_.data();
        for (int op = 0; op < kNumOps; ++op) {
            uint8_t* pp = bulk + op * 17;
            const uint8_t* up = src + op * 21;
            std::memcpy(pp, up, 11);                                   // R1-4,L1-4,BP,LD,RD
            pp[11] = (up[OP_LC] & 0x03) | ((up[OP_RC] & 0x03) << 2);    // curves
            pp[12] = (up[OP_RS] & 0x07) | ((up[OP_DET] & 0x0F) << 3);   // rate scale + detune
            pp[13] = (up[OP_AMS] & 0x03) | ((up[OP_KVS] & 0x07) << 2);  // ams + kvs
            pp[14] = up[OP_OL];                                        // output level
            pp[15] = (up[OP_MODE] & 0x01) | ((up[OP_FC] & 0x1F) << 1);  // mode + coarse
            pp[16] = up[OP_FF];                                        // fine
        }
        std::memcpy(bulk + 102, src + 126, 9);                         // pitch EG(8) + algorithm
        bulk[111] = (src[G_FB] & 0x07) | ((src[G_OKS] & 0x01) << 3);    // feedback + osc key sync
        std::memcpy(bulk + 112, src + 137, 4);                         // LFO speed/delay/pmd/amd
        bulk[116] = (src[G_LFKS] & 0x01)
            | ((src[G_LFW] & 0x07) << 1)
            | ((src[G_LPMS] & 0x07) << 4);                        // lfo sync/wave/pms
        bulk[117] = src[G_TRANSPOSE];
        for (int i = 0; i < 10; ++i) bulk[118 + i] = src[G_NAME + i];   // name
    }

    static inline uint8_t clamp7(uint8_t v, uint8_t max) { v &= 0x7F; return v <= max ? v : max; }

    void Voice::unpack(const uint8_t* bulk) {
        uint8_t* dst = data_.data();
        for (int op = 0; op < kNumOps; ++op) {
            const uint8_t* pp = bulk + op * 17;
            uint8_t* up = dst + op * 21;
            for (int i = 0; i < 11; ++i) up[i] = clamp7(pp[i], 99);
            up[OP_BP] = clamp7(pp[8], 99);
            up[OP_LD] = clamp7(pp[9], 99);
            up[OP_RD] = clamp7(pp[10], 99);
            uint8_t curves = pp[11] & 0x0F;
            up[OP_LC] = curves & 0x03;
            up[OP_RC] = (curves >> 2) & 0x03;
            uint8_t rsdet = pp[12];
            up[OP_RS] = rsdet & 0x07;
            up[OP_DET] = (rsdet >> 3) & 0x0F;
            uint8_t amskvs = pp[13];
            up[OP_AMS] = amskvs & 0x03;
            up[OP_KVS] = (amskvs >> 2) & 0x07;
            up[OP_OL] = clamp7(pp[14], 99);
            uint8_t modecoarse = pp[15];
            up[OP_MODE] = modecoarse & 0x01;
            up[OP_FC] = (modecoarse >> 1) & 0x1F;
            up[OP_FF] = clamp7(pp[16], 99);
        }
        for (int i = 0; i < 8; ++i) dst[126 + i] = clamp7(bulk[102 + i], 99); // pitch EG
        dst[G_ALG] = clamp7(bulk[110], 31);
        dst[G_FB] = bulk[111] & 0x07;
        dst[G_OKS] = (bulk[111] >> 3) & 0x01;
        dst[G_LFS] = clamp7(bulk[112], 99);
        dst[G_LFD] = clamp7(bulk[113], 99);
        dst[G_LPMD] = clamp7(bulk[114], 99);
        dst[G_LAMD] = clamp7(bulk[115], 99);
        uint8_t lfo = bulk[116];
        dst[G_LFKS] = lfo & 0x01;
        dst[G_LFW] = (lfo >> 1) & 0x07;
        dst[G_LPMS] = (lfo >> 4) & 0x07;
        dst[G_TRANSPOSE] = clamp7(bulk[117], 48);
        for (int i = 0; i < 10; ++i) {
            uint8_t c = bulk[118 + i] & 0x7F;
            dst[G_NAME + i] = (c < 32 || c > 126) ? ' ' : c;
        }
    }

    std::string Voice::name() const {
        std::string s((const char*)data_.data() + G_NAME, 10);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }

    void Voice::setName(const std::string& n) {
        for (int i = 0; i < 10; ++i) {
            char c = i < (int)n.size() ? n[i] : ' ';
            if (c < 32 || c > 126) c = ' ';
            data_[G_NAME + i] = (uint8_t)c;
        }
    }

    int Voice::clampToRange(int off, int value) {
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

    void Voice::set(int off, int value) {
        if (off < 0 || off >= kVcedSize) return;
        data_[(size_t)off] = (uint8_t)clampToRange(off, value);
    }

    void Voice::initDefault() {
        data_.fill(0);
        for (int op = 0; op < kNumOps; ++op) {
            uint8_t* u = data_.data() + op * 21;
            u[OP_R1] = 99; u[OP_R2] = 99; u[OP_R3] = 99; u[OP_R4] = 60;
            u[OP_L1] = 99; u[OP_L2] = 99; u[OP_L3] = 99; u[OP_L4] = 0;
            u[OP_BP] = 39; u[OP_LD] = 0; u[OP_RD] = 0; u[OP_LC] = 0; u[OP_RC] = 0;
            u[OP_RS] = 0; u[OP_AMS] = 0; u[OP_KVS] = 0;
            u[OP_OL] = (op == 5) ? 99 : 0;    // op index 5 == OP1 carrier
            u[OP_MODE] = 0; u[OP_FC] = 1; u[OP_FF] = 0; u[OP_DET] = 7;
        }
        for (int i = 0; i < 8; ++i) data_[126 + i] = (i < 4) ? 99 : 50;
        data_[G_PL1] = 50; data_[G_PL2] = 50; data_[G_PL3] = 50; data_[G_PL4] = 50;
        data_[G_ALG] = 0;  data_[G_FB] = 0; data_[G_OKS] = 1;
        data_[G_LFS] = 35; data_[G_LFD] = 0; data_[G_LPMD] = 0; data_[G_LAMD] = 0;
        data_[G_LFKS] = 1; data_[G_LFW] = 0; data_[G_LPMS] = 3;
        data_[G_TRANSPOSE] = 24;
        setName("INIT VOICE");
    }

    const std::vector<ParamInfo>& Voice::paramTable() {
        static const std::vector<ParamInfo> table = [] {
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
                    t.push_back({ f.n, op * 21 + f.off, f.mn, f.mx });
            static const ParamInfo g[] = {
                {"Pitch R1",G_PR1,0,99},{"Pitch R2",G_PR2,0,99},{"Pitch R3",G_PR3,0,99},{"Pitch R4",G_PR4,0,99},
                {"Pitch L1",G_PL1,0,99},{"Pitch L2",G_PL2,0,99},{"Pitch L3",G_PL3,0,99},{"Pitch L4",G_PL4,0,99},
                {"Algorithm",G_ALG,0,31},{"Feedback",G_FB,0,7},{"Osc Sync",G_OKS,0,1},
                {"LFO Speed",G_LFS,0,99},{"LFO Delay",G_LFD,0,99},{"LFO PMD",G_LPMD,0,99},{"LFO AMD",G_LAMD,0,99},
                {"LFO Sync",G_LFKS,0,1},{"LFO Wave",G_LFW,0,5},{"P Mod Sens",G_LPMS,0,7},{"Transpose",G_TRANSPOSE,0,48},
            };
            for (const auto& gi : g) t.push_back(gi);
            return t;
            }();
        return table;
    }

    std::vector<uint8_t> Voice::toSingleVoiceSysex(uint8_t channel) const {
        std::vector<uint8_t> m; m.reserve(163);
        m.push_back(0xF0); m.push_back(0x43); m.push_back(0x00 | (channel & 0x0F));
        m.push_back(0x00); m.push_back(0x01); m.push_back(0x1B);      // format 0, 155 bytes
        uint8_t sum = 0;
        for (int i = 0; i < kVcedSize; ++i) { m.push_back(data_[(size_t)i]); sum += data_[(size_t)i]; }
        m.push_back((uint8_t)((-sum) & 0x7F));
        m.push_back(0xF7);
        return m;
    }

    std::string Bank::voiceName(int idx) const { return voice(idx).name(); }

    std::vector<uint8_t> Bank::toBankSysex(uint8_t channel) const {
        std::vector<uint8_t> m; m.reserve(4104);
        m.push_back(0xF0); m.push_back(0x43); m.push_back(0x00 | (channel & 0x0F));
        m.push_back(0x09); m.push_back(0x20); m.push_back(0x00);      // format 9, 4096 bytes
        uint8_t sum = 0;
        for (int i = 0; i < 4096; ++i) { m.push_back(data_[(size_t)i]); sum += data_[(size_t)i]; }
        m.push_back((uint8_t)((-sum) & 0x7F));
        m.push_back(0xF7);
        return m;
    }

    bool usingStarterBank(const RomStore& roms) { return !roms.hasVoices(); }

    // The bundled starter bank, packed once on first use.
    const Bank& starterBank()
    {
        static const Bank b = []
            {
                Bank tmp;
                for (int i = 0; i < 32; ++i)
                {
                    uint8_t vced[kVcedSize];
                    vdx7starter::buildVced(i, vced);
                    Voice v;
                    std::memcpy(v.vced(), vced, kVcedSize);
                    tmp.setVoice(i, v);
                }
                return tmp;
            }();
        return b;
    }

    Bank factoryBank(const RomStore& roms, int bank) {
        if (bank == kStarterBankIndex)
            return starterBank();
        bank = juce::jlimit(0, kNumFactoryBanks - 1, bank);

        Bank out;
        if (roms.copyBank(bank, out.packed()))
            return out;

        return starterBank();
    }

    const char* factoryBankName(const RomStore& roms, int bank) {
        if (bank == kStarterBankIndex) return "STARTER";
        if (bank < 0 || bank >= kNumFactoryBanks) return "ROM";

        if (usingStarterBank(roms))
            return "STARTER";

        static const char* names[kNumFactoryBanks] =
        { "ROM1A","ROM1B","ROM2A","ROM2B","ROM3A","ROM3B","ROM4A","ROM4B" };
        return names[bank];
    }

    bool findBankInSysex(const uint8_t* p, size_t len, Bank& out) {
        if (p == nullptr) return false;
        for (size_t i = 0; i + 4104 <= len; ++i)
            if (p[i] == 0xF0 && p[i + 1] == 0x43 && p[i + 3] == 0x09 && p[i + 4] == 0x20
                && p[i + 5] == 0x00 && p[i + 4103] == 0xF7)
            {
                out.load(p + i + 6);
                return true;
            }
        return false;
    }

    // ----------------------------------------------------------------------------
    //  DX7Engine
    // ----------------------------------------------------------------------------
    // Panel-button CtrlID numeric values (from Message::CtrlID).
    static constexpr int kB_dash = 37;  // INTERNAL (memory select / play)
    static constexpr int kB_x = 33;  // INTERNAL memory protect
    static constexpr int kB_no = 40;  // -1 / OFF
    static constexpr int kB_yes = 41;  // +1 / ON
    static constexpr int kB_sp = 39;  // FUNCTION
    static constexpr int kB_8 = 7;  // MIDI / SYS INFO group
    // b_1 == 0 ... b_32 == 31

    // Originally set to 1: the emulated firmware's note mapping was measured as
    // one semitone flat of standard MIDI, and this shifted MIDI notes up by one
    // at the door to compensate. That measurement, however, was almost certainly
    // taken while the master-tune boot bug (see the tune(0) call in
    // DX7Engine::bootThreadFn) was still landing on a heavily-detuned default -
    // a large enough error that "flat" could easily be misjudged as "flat by a
    // whole semitone" rather than "flat by a large but sub-semitone amount".
    // With that bug fixed, a +1 bias here is no longer compensating for
    // anything real and just adds a genuine semitone of sharpness on top of an
    // otherwise-correct mapping - which matches what's now being heard: notes
    // consistently a full note sharp, not a fractional cents error.
    // Set back to a nonzero value only if a tuner confirms a genuine, fixed
    // semitone-scale note-index error remains once tune(0) is in place.
    static constexpr int kEmulatorNoteBias = 0;

    DX7Engine::DX7Engine(RomStore& roms) : roms_(roms) {
        toSynth_ = new App_ToSynth();
        toGui_ = new App_ToGui();
        synth_ = new DX7Synth(nullptr);   // no ram file; we build RAM ourselves
        synth_->toGui = toGui_;
        synth_->toSynth = toSynth_;

        vdx7native::NativeEngine::defaultVced(nativeVced_);
        native_.setVoice(nativeVced_);

        // If an MTS-ESP master is running in this session, follow it. With the
        // library absent this hands over empty functions and changes nothing.
        // Microtuning belongs to the native engine only, so the client is switched
        // off for as long as a firmware ROM is in charge.
        if (auto p = mts_.provider())
            native_.tuning().setFrequencyProvider(std::move(p));
        if (auto f = mts_.noteFilter())
            native_.tuning().setNoteFilter(std::move(f));
        mts_.setEnabled(!roms_.hasFirmware());
        emulatorWanted_.store(roms_.hasFirmware(), std::memory_order_release);
        updateNativeDisplay();
    }

    DX7Engine::~DX7Engine() {
        if (bootThread_.joinable()) bootThread_.join();
        delete synth_;   synth_ = nullptr;
        delete toGui_;   toGui_ = nullptr;
        delete toSynth_; toSynth_ = nullptr;
    }

    void DX7Engine::prepare(double sr) {
        sampleRate_ = sr;
        native_.prepare(sr);

        // The emulator is only worth booting if there is a firmware ROM to boot it
        // from; without one the native engine covers everything.
        if (!roms_.hasFirmware())
            return;

        if (!bootStarted_.exchange(true))
            bootThread_ = std::thread(&DX7Engine::bootThreadFn, this, sr);
        else if (ready_.load(std::memory_order_acquire)) {
            synth_->setSampleRate(sr);      // audio stopped in prepareToPlay; safe
            emuRate_ = sr;
        }
        // Still booting: the boot thread started with an older rate. The audio
        // thread corrects it when the emulator takes over (primeEmulator).
    }

    // A ROM has been loaded or cleared. Message thread.
    void DX7Engine::romsChanged() {
        const bool haveFirmware = roms_.hasFirmware();
        emulatorWanted_.store(haveFirmware, std::memory_order_release);

        // Loading firmware turns microtuning off, clearing it turns microtuning
        // back on: the real DX7's tuning is fixed, and the emulator is the real
        // DX7. See MtsEsp.h for why that is the hardware's limitation, not ours.
        mts_.setEnabled(!haveFirmware);

        if (haveFirmware && !bootStarted_.exchange(true))
            bootThread_ = std::thread(&DX7Engine::bootThreadFn, this, sampleRate_);

        if (!haveFirmware)
            updateNativeDisplay();
    }

    void DX7Engine::setNativeVoice(const uint8_t vced[155]) {
        std::memcpy(nativeVced_, vced, 155);
        native_.setVoice(vced);
        if (backend() == Backend::Native)
            updateNativeDisplay();
    }

    // The native engine has no HD44780 to read back, so it reports the same
    // information the DX7's play mode would: what is loaded, and on which engine.
    void DX7Engine::updateNativeDisplay() {
        char name[11] = {};
        for (int i = 0; i < 10; ++i) {
            const unsigned char c = nativeVced_[145 + i];
            name[i] = (c < 32 || c > 126) ? ' ' : (char)c;
        }
        native_.setLcd("NATIVE FM ENGINE", name);
    }

    void DX7Engine::bootThreadFn(double fs) {
        // Each call into the emulator gets its own short-lived RomPointerScope so
        // the two global ROM pointers are only ever "ours" for the instant a call
        // is actually happening - never for the whole boot (which takes a couple
        // of seconds and must not block any other instance's audio thread).
        auto runSecs = [&](double s) {
            int n = (int)(s * fs / kChunk);
            for (int i = 0;i < n;i++) { RomPointerScope rp(roms_); synth_->run(); }
            };
        auto pressDirect = [&](int ctrlId) {
            auto id = (Message::CtrlID)ctrlId;
            toSynth_->buttondown(id); runSecs(0.06);
            toSynth_->buttonup(id); runSecs(0.10);
            };
        { RomPointerScope rp(roms_); synth_->dx7.loadFirmwareFromMemory(); }
        synth_->start();

        // Battery-backed RAM. The original project shipped a dump of a real
        // machine's memory for this; it is not needed. The firmware is happy to
        // start from an image we assemble ourselves - 4 KB of internal voices
        // followed by a zeroed function/system area, which it validates and repairs
        // on the way up.
        {
            uint8_t fallback[4096];
            std::memcpy(fallback, factoryBank(roms_, 0).packed(), 4096);
            const auto ram = roms_.buildBatteryRam(fallback);
            std::memcpy(&synth_->dx7.memory[0x1000], ram.data(), ram.size());
        }
        toSynth_->analog(Message::CtrlID::battery, 82);                    // battery OK
        synth_->setSampleRate(fs);
        emuRate_ = fs;     // published to the audio thread by ready_ below

        runSecs(2.5);                       // let the firmware finish its boot self-test

        // buildBatteryRam() hands the firmware a blank master-tune field (part of
        // the zeroed system-parameter region), which the firmware's own boot-time
        // repair routine fills in - but not at true A440; measured against a
        // tuner it lands consistently sharp, on every note, every time, so it's
        // a fixed register value the repair settles on rather than a per-key
        // error. tune(0) is the same call dx7.cc itself uses for a normal
        // fresh-RAM boot ("Default master tuning to A440 = 0x100"), so it's the
        // one value here that's correct by construction rather than by a step
        // count we'd have to trust. We call it again here, after the self-test
        // has finished writing its own value and before any notes can sound, to
        // override the firmware's repaired value with that known-good default.
        //
        // NOTE: if a tuner still shows a residual offset after this, do NOT
        // "fix" it by guessing a step count from the ~0.3-cents/step figure in
        // dx7.h - that figure has not been verified against this build (a -87
        // step trial here produced roughly -26 semitones, not the expected
        // -26 cents, so treat that number as unverified until measured
        // directly). Bisect a small integer step count against a tuner instead.
        synth_->dx7.tune(0);

        // Enable "SYS INFO AVAIL" so the firmware accepts parameter-change SysEx
        // (this is what makes live single-parameter edits take effect).
        pressDirect(kB_sp);                 // FUNCTION
        pressDirect(kB_8); pressDirect(kB_8);   // cycle to SYS INFO UNAVAIL
        pressDirect(kB_yes);                // -> SYS INFO AVAIL

        pressDirect(kB_x);                  // INTERNAL memory protect...
        pressDirect(kB_no);                 // ...OFF, so voice loads/stores are unhindered
        pressDirect(kB_dash);               // INTERNAL play mode
        pressDirect(0);                     // select voice 1 (b_1 == 0)
        runSecs(0.1);

        updateDisplaySnapshot();
        ready_.store(true, std::memory_order_release);
    }

    void DX7Engine::pushCommand(std::unique_ptr<Command> c) {
        std::lock_guard<std::mutex> lk(ringMutex_);
        int h = head_.load(std::memory_order_relaxed);
        int nxt = (h + 1) % kCmdCap;
        if (nxt == tail_.load(std::memory_order_acquire)) return;   // full: drop
        ring_[h] = std::move(c);
        head_.store(nxt, std::memory_order_release);
    }

    void DX7Engine::drainCommands() {
        for (;;) {
            int t = tail_.load(std::memory_order_relaxed);
            if (t == head_.load(std::memory_order_acquire)) break;
            std::unique_ptr<Command> c = std::move(ring_[t]);
            tail_.store((t + 1) % kCmdCap, std::memory_order_release);
            if (c) applyCommand(*c);
        }
    }

    void DX7Engine::applyCommand(Command& c) {
        // Patch changes go to the native engine too, whichever backend is live, so
        // the two never drift apart.
        switch (c.type) {
        case Cmd::LoadVoice:
            if (c.blob) {
                Voice v; v.unpack(c.blob.get());
                std::memcpy(nativeVced_, v.vced(), kVcedSize);
                native_.setVoice(nativeVced_);
                if (backend() == Backend::Native) updateNativeDisplay();
            }
            break;
        case Cmd::LoadBank:
            if (c.blob) {
                Voice v; v.unpack(c.blob.get() + (size_t)juce::jlimit(0, 31, c.ival) * 128);
                std::memcpy(nativeVced_, v.vced(), kVcedSize);
                native_.setVoice(nativeVced_);
                if (backend() == Backend::Native) updateNativeDisplay();
            }
            break;
        case Cmd::ParamChange:
            if (c.ival >= 0 && c.ival < kVcedSize) {
                nativeVced_[c.ival] = (uint8_t)juce::jlimit(0, 127, c.ival2);
                native_.setParam(c.ival, c.ival2);
                if (c.ival >= G_NAME && backend() == Backend::Native) updateNativeDisplay();
            }
            break;
        default: break;
        }

        // Everything below is the emulator's own state machine; with no ROM there
        // is nothing to drive.
        if (backend() == Backend::Native || !ready_.load(std::memory_order_acquire))
            return;

        switch (c.type) {
        case Cmd::LoadVoice:
            if (c.blob) {
                std::memcpy(&synth_->dx7.memory[0x1000], c.blob.get(), 128);
                scheduleSelect(0);        // reselect voice 1 -> reload edit buffer
            }
            break;
        case Cmd::LoadBank:
            if (c.blob) {
                std::memcpy(&synth_->dx7.memory[0x1000], c.blob.get(), 4096);
                scheduleSelect(c.ival);   // select program c.ival (0..31)
            }
            break;
        case Cmd::Button:
            sched_.push_back({ 0,  c.ival, true });
            sched_.push_back({ 20, c.ival, false });
            break;
        case Cmd::Protect:
            // INTERNAL memory protect, then ON/OFF, then INTERNAL again to put the
            // panel back in play mode. That last press only changes what the LCD
            // shows: the edit buffer, and with it any live edits, is kept.
            schedulePress(kB_x);
            schedulePress(c.bval ? kB_yes : kB_no);
            schedulePress(kB_dash);
            break;
        case Cmd::ParamChange: {
            // DX7 voice parameter change: F0 43 1n g p v F7  (g = offset>127, p = offset&0x7F)
            uint8_t m[7] = { 0xF0, 0x43, 0x10,
                             (uint8_t)(c.ival > 127 ? 1 : 0),
                             (uint8_t)(c.ival & 0x7F),
                             (uint8_t)(c.ival2 & 0x7F), 0xF7 };
            synth_->queueMidiRx(7, m);
            break;
        }
        }
    }

    void DX7Engine::scheduleSelect(int prog) {
        prog = juce::jlimit(0, 31, prog);
        schedulePress(kB_dash);   // with nothing queued: down 0, up 20
        schedulePress(prog);      //                      down 28, up 48
    }

    // Queues one press (down, then up 20 chunks later) to start after everything
    // already scheduled, so two sequences never interleave their buttons.
    void DX7Engine::schedulePress(int ctrlId) {
        int start = 0;
        for (const auto& b : sched_)
            start = juce::jmax(start, b.chunksLeft + 8);
        sched_.push_back({ start,      ctrlId, true });
        sched_.push_back({ start + 20, ctrlId, false });
    }

    // ---- FUNCTION settings ------------------------------------------------------
    void DX7Engine::setFunctions(const FunctionSettings& f) {
        fnWanted_ = f;

        // The native engine takes them in its own units.
        vdx7native::Functions n;
        n.mono            = f.mono;
        n.portaFollow     = f.portaMode != 0;
        vdx7native::portamentoTimeToGlide(f.portaTime, n.portamentoTau, n.portamentoDelay);
        n.bendRange       = (float)juce::jlimit(0, 12, f.pbRange);
        n.masterTuneCents = (float)juce::jlimit(-256, 255, f.masterTune) * kMasterTuneCentsPerStep;
        auto conv = [](const FunctionSettings::Controller& c, vdx7native::Functions::Assign& a) {
            a.range  = (float)juce::jlimit(0, 99, c.range) / 99.0f;
            a.pitch  = (c.assign & 1) != 0;
            a.amp    = (c.assign & 2) != 0;
            a.egBias = (c.assign & 4) != 0;
        };
        conv(f.modWheel, n.modWheel);
        conv(f.foot, n.foot);
        conv(f.breath, n.breath);
        conv(f.aftertouch, n.aftertouch);
        native_.setFunctions(n);
        native_.setMemoryProtect(f.memoryProtect);

        // The emulator hears about changes only while it is the one playing;
        // primeEmulator() hands it the full set when it takes over.
        if (emuLive_ && backend() == Backend::Emulator && ready_.load(std::memory_order_acquire))
            sendFunctionsToEmulator(false);
    }

    // The firmware's own route in: function parameter change SysEx, group 2
    // (F0 43 1n 08 pp vv F7, pp = 64..77), accepted because the boot switched
    // SYS INFO AVAIL on - the firmware then updates everything that depends on
    // the value itself. Master tune has no SysEx on the DX7, so it is written
    // straight to its RAM location (DX7::tune), and memory protect is pressed
    // on the panel like any other button.
    void DX7Engine::sendFunctionsToEmulator(bool all) {
        const auto& w = fnWanted_;
        auto& s = fnSent_;
        auto send = [this, all](int param, int value, int& sent) {
            if (!all && value == sent) return;
            sent = value;
            uint8_t m[7] = { 0xF0, 0x43, 0x10, 0x08, (uint8_t)param, (uint8_t)(value & 0x7F), 0xF7 };
            synth_->queueMidiRx(7, m);
        };
        int mono = s.mono ? 1 : 0;
        send(64, w.mono ? 1 : 0, mono);                      s.mono = mono != 0;
        send(65, juce::jlimit(0, 12, w.pbRange), s.pbRange);
        send(67, w.portaMode != 0 ? 1 : 0, s.portaMode);
        send(69, juce::jlimit(0, 99, w.portaTime), s.portaTime);
        const FunctionSettings::Controller* want[4] = { &w.modWheel, &w.foot, &w.breath, &w.aftertouch };
        FunctionSettings::Controller* sent[4] = { &s.modWheel, &s.foot, &s.breath, &s.aftertouch };
        for (int c = 0; c < 4; ++c) {
            send(70 + 2 * c, juce::jlimit(0, 99, want[c]->range),     sent[c]->range);
            send(71 + 2 * c, juce::jlimit(0, 7,  want[c]->assign),    sent[c]->assign);
        }

        const int tune = juce::jlimit(-256, 255, w.masterTune);
        if (all || tune != s.masterTune) {
            s.masterTune = tune;
            synth_->dx7.tune(tune);
        }

        if (all ? w.memoryProtect : (w.memoryProtect != s.memoryProtect)) {
            schedulePress(kB_x);
            schedulePress(w.memoryProtect ? kB_yes : kB_no);
            schedulePress(kB_dash);
        }
        s.memoryProtect = w.memoryProtect;
    }

    void DX7Engine::pumpSchedule() {
        for (auto it = sched_.begin(); it != sched_.end(); ) {
            if (it->chunksLeft <= 0) {
                auto id = (Message::CtrlID)it->ctrlId;
                if (it->down) toSynth_->buttondown(id); else toSynth_->buttonup(id);
                it = sched_.erase(it);
            }
            else { it->chunksLeft--; ++it; }
        }
    }

    void DX7Engine::loadVoice(const uint8_t packed128[128]) {
        auto c = std::make_unique<Command>();
        c->type = Cmd::LoadVoice;
        c->blob.reset(new uint8_t[128]);
        std::memcpy(c->blob.get(), packed128, 128);
        pushCommand(std::move(c));
    }

    void DX7Engine::loadBankAndSelect(const uint8_t packed4096[4096], int prog) {
        auto c = std::make_unique<Command>();
        c->type = Cmd::LoadBank; c->ival = prog;
        c->blob.reset(new uint8_t[4096]);
        std::memcpy(c->blob.get(), packed4096, 4096);
        pushCommand(std::move(c));
    }

    void DX7Engine::pressButton(int ctrlId) {
        auto c = std::make_unique<Command>(); c->type = Cmd::Button; c->ival = ctrlId;
        pushCommand(std::move(c));
    }

    void DX7Engine::setInternalProtect(bool on) {
        auto c = std::make_unique<Command>(); c->type = Cmd::Protect; c->bval = on;
        pushCommand(std::move(c));
    }

    void DX7Engine::sendParamChange(int vcedOffset, int value) {
        auto c = std::make_unique<Command>();
        c->type = Cmd::ParamChange; c->ival = vcedOffset; c->ival2 = value;
        pushCommand(std::move(c));
    }

    void DX7Engine::pushMidi(const uint8_t* bytes, int len) {
        if (len <= 0) return;

        // Both engines are fed, always. The emulator may still be booting, and the
        // native engine has to stay in step so that switching between them mid-note
        // does not strand a key down.
        native_.pushMidi(bytes, len);

        if (backend() == Backend::Emulator && ready_.load(std::memory_order_acquire)) {
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
                const int note = (int)fixed[1] + kEmulatorNoteBias;
                fixed[1] = (uint8_t)juce::jlimit(0, 127, note);
                synth_->queueMidiRx(3, fixed);
            }
            else {
                synth_->queueMidiRx((uint32_t)len, bytes);
            }
        }
    }

    void DX7Engine::renderMono(float* out, int n) {
        // Native engine: no boot, no command queue, no chunking.
        if (backend() == Backend::Native || !ready_.load(std::memory_order_acquire)) {
            drainCommands();
            native_.render(out, n);
            emuLive_ = false;
            return;
        }

        drainCommands();
        if (!emuLive_) primeEmulator();
        int i = 0;
        while (i < n) {
            if (chunkPos_ >= kChunk) {
                pumpSchedule();
                { RomPointerScope rp(roms_); synth_->run(); }
                std::memcpy(chunkBuf_, synth_->outputBuffer, sizeof(float) * kChunk);
                chunkPos_ = 0;
            }
            int take = kChunk - chunkPos_;
            if (take > n - i) take = n - i;
            std::memcpy(out + i, chunkBuf_ + chunkPos_, sizeof(float) * (size_t)take);
            chunkPos_ += take; i += take;
        }
        updateDisplaySnapshot();
    }

    // The emulator is taking over from the native engine: right after its boot,
    // or when a firmware ROM is loaded again later. Everything that happened in
    // the meantime - the patch a project restored, program changes, knob moves -
    // was applied to the native engine only (the emulator was not listening yet),
    // and the native side mirrors every one of those into nativeVced_. Hand that
    // voice over exactly as a LoadVoice command would, so both engines play the
    // same patch, and make sure the emulator runs at the host's current rate
    // (prepareToPlay may have changed it while the boot was still running).
    // Audio thread; the boot thread has finished (ready_ was acquired).
    void DX7Engine::primeEmulator() {
        emuLive_ = true;
        if (!juce::exactlyEqual(emuRate_, sampleRate_)) {
            synth_->setSampleRate(sampleRate_);
            emuRate_ = sampleRate_;
        }
        Voice v;
        std::memcpy(v.vced(), nativeVced_, kVcedSize);
        uint8_t packed[128];
        v.pack(packed);
        std::memcpy(&synth_->dx7.memory[0x1000], packed, 128);
        scheduleSelect(0);   // reselect voice 1 -> reload the edit buffer

        // The FUNCTION page too: the firmware booted with a blank one, and
        // anything changed while the native engine was playing never reached it.
        sendFunctionsToEmulator(true);
    }

    void DX7Engine::updateDisplaySnapshot() {
        const uint8_t* st = nullptr;
        synth_->dx7.lcd.save(st);
        char l1[17], l2[17];
        for (int k = 0; k < 16; ++k) {
            unsigned char a = st ? st[k] : ' ';
            unsigned char b = st ? st[40 + k] : ' ';
            l1[k] = (a < 32 || a > 126) ? ' ' : (char)a;
            l2[k] = (b < 32 || b > 126) ? ' ' : (char)b;
        }
        l1[16] = l2[16] = 0;
        {
            std::lock_guard<std::mutex> lk(lcdMutex_);
            std::memcpy(lcd1_, l1, 17);
            std::memcpy(lcd2_, l2, 17);
        }
        led1_.store(synth_->dx7.P_LED1, std::memory_order_relaxed);
        led2_.store(synth_->dx7.P_LED2, std::memory_order_relaxed);
    }

    void DX7Engine::getLcd(char line1[17], char line2[17]) const {
        if (backend() == Backend::Native || !ready_.load(std::memory_order_acquire)) {
            native_.getLcd(line1, line2);
            return;
        }
        std::lock_guard<std::mutex> lk(lcdMutex_);
        std::memcpy(line1, lcd1_, 17);
        std::memcpy(line2, lcd2_, 17);
    }

} // namespace vdx7

// ============================================================================
//  VDX7AudioProcessor
// ============================================================================
VDX7AudioProcessor::VDX7AudioProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "VDX7", createLayout()),
    engine_(roms_)
{
    buildAutoParams();
    fx_.bindParameters(apvts);
    bindFunctionParameters();

    // By default a freshly-inserted instance has no ROM loaded (roms_ starts
    // empty) and runs the native engine, entirely independently of whatever
    // ROM any other instance in the project has or hasn't loaded. A saved
    // project brings its own ROM back via setStateInformation(), which
    // re-loads from romPath_/voicesPath_ - this instance's own remembered
    // location, not a shared one.

    currentVoice_ = vdx7::factoryBank(roms_, 0).voice(0);   // first patch of bank 1
    bank_ = 0; prog_ = 0;
    pushVoiceToApvts(currentVoice_);                  // seed the APVTS with that patch
}

juce::AudioProcessorValueTreeState::ParameterLayout VDX7AudioProcessor::createLayout()
{
    using namespace vdx7;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // Static, called before any instance (and so before any RomStore) exists,
    // so this always uses the bundled starter bank rather than a ROM.
    const Voice def = starterBank().voice(0);         // sensible defaults
    for (const auto& pi : Voice::paramTable())
    {
        const int off = pi.vcedOffset;
        juce::String name;
        if (off < kNumOps * kOpVcedStride) {            // per-operator parameter
            const int blk = off / kOpVcedStride;      // 0..5  (block 0 == OP6)
            const int opNum = kNumOps - blk;            // 6..1  front-panel number
            name = "OP" + juce::String(opNum) + " " + pi.name;
        }
        else {
            name = pi.name;
        }
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{ paramId(off), 1 }, name,
            pi.minVal, pi.maxVal, (int)def.get(off)));
    }

    // Global effects. These use "fx..." IDs, so buildAutoParams() - which walks
    // Voice::paramTable() - never picks them up and no FX value can end up
    // being sent to the emulator as a VCED byte.
    vdx7fx::FxChain::addParameters(layout);

    // The FUNCTION page (and the native engine's DC blocker).
    addFunctionParameters(layout);

    return layout;
}

// ---------------------------------------------------------------------------
//  FUNCTION page
// ---------------------------------------------------------------------------
// Defaults are the settings the native engine has always used (bend 2
// semitones, the mod wheel on pitch at full range, the other controllers at full
// range but assigned to nothing, no portamento, poly, no memory protect), so a
// project saved before these parameters existed sounds exactly as it did. The
// emulator used to keep whatever its blank function memory gave it (no bend,
// no wheel) - it now follows the same settings.
void VDX7AudioProcessor::addFunctionParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using namespace vdx7;
    auto intParam = [&layout](const juce::String& id, const juce::String& name, int lo, int hi, int def,
                              std::function<juce::String(int, int)> toText = nullptr) {
        auto attr = juce::AudioParameterIntAttributes();
        if (toText) attr = attr.withStringFromValueFunction(std::move(toText));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ id, 1 }, name, lo, hi, def, attr));
    };
    auto boolParam = [&layout](const juce::String& id, const juce::String& name, bool def) {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ id, 1 }, name, def));
    };

    intParam(fnid::masterTune, "Master Tune", -256, 255, 0, [](int v, int) {
        const float c = (float)v * kMasterTuneCentsPerStep;
        return (c > 0.0f ? "+" : "") + juce::String(c, 1) + " ct";
    });
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ fnid::polyMono, 1 },
        "Poly/Mono", juce::StringArray{ "Poly", "Mono" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ fnid::portaMode, 1 },
        "Portamento Mode", juce::StringArray{ "Retain / Fingered", "Follow / Full Time" }, 0));
    intParam(fnid::portaTime, "Portamento Time", 0, 99, 0);
    intParam(fnid::pbRange, "Pitch Bend Range", 0, 12, 2, [](int v, int) { return juce::String(v) + " st"; });

    for (int c = 0; c < 4; ++c) {
        const juce::String n = fnid::controllerNames[c];
        intParam(fnid::range(c), n + " Range", 0, 99, 99);
        boolParam(fnid::pitch(c),  n + " Pitch",   c == 0);   // only the wheel starts on pitch
        boolParam(fnid::amp(c),    n + " Amp",     false);
        boolParam(fnid::egBias(c), n + " EG Bias", false);
    }

    juce::StringArray channels{ "Omni" };
    for (int ch = 1; ch <= 16; ++ch) channels.add(juce::String(ch));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ fnid::midiChannel, 1 },
        "MIDI Channel", channels, 0));
    boolParam(fnid::memProtect, "Memory Protect", false);

    // Native engine only: a leaky integrator taking out DC offset, 0 = off.
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ fnid::dcBlock, 1 },
        "DC Block", juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            return v <= 0.0f ? juce::String("Off") : juce::String(v, 2) + " Hz";
        })));
}

void VDX7AudioProcessor::bindFunctionParameters()
{
    using namespace vdx7;
    auto raw = [this](const juce::String& id) {
        auto* r = apvts.getRawParameterValue(id);
        jassert(r != nullptr);
        return r;
    };
    fnRaw_.masterTune = raw(fnid::masterTune);
    fnRaw_.polyMono   = raw(fnid::polyMono);
    fnRaw_.portaMode  = raw(fnid::portaMode);
    fnRaw_.portaTime  = raw(fnid::portaTime);
    fnRaw_.pbRange    = raw(fnid::pbRange);
    fnRaw_.midiCh     = raw(fnid::midiChannel);
    fnRaw_.protect    = raw(fnid::memProtect);
    fnRaw_.dcBlock    = raw(fnid::dcBlock);
    for (int c = 0; c < 4; ++c) {
        fnRaw_.range[c]  = raw(fnid::range(c));
        fnRaw_.pitch[c]  = raw(fnid::pitch(c));
        fnRaw_.amp[c]    = raw(fnid::amp(c));
        fnRaw_.egBias[c] = raw(fnid::egBias(c));
    }
}

vdx7::FunctionSettings VDX7AudioProcessor::readFunctionParameters() const
{
    auto get = [](const std::atomic<float>* a) { return (int)std::lround(a->load(std::memory_order_relaxed)); };
    vdx7::FunctionSettings f;
    f.masterTune    = get(fnRaw_.masterTune);
    f.mono          = get(fnRaw_.polyMono) != 0;
    f.portaMode     = get(fnRaw_.portaMode);
    f.portaTime     = get(fnRaw_.portaTime);
    f.pbRange       = get(fnRaw_.pbRange);
    f.memoryProtect = get(fnRaw_.protect) != 0;
    vdx7::FunctionSettings::Controller* ctl[4] = { &f.modWheel, &f.foot, &f.breath, &f.aftertouch };
    for (int c = 0; c < 4; ++c) {
        ctl[c]->range  = get(fnRaw_.range[c]);
        ctl[c]->assign = (get(fnRaw_.pitch[c]) != 0 ? 1 : 0)
                       | (get(fnRaw_.amp[c])   != 0 ? 2 : 0)
                       | (get(fnRaw_.egBias[c]) != 0 ? 4 : 0);
    }
    return f;
}

// MIDI CHANNEL. With a channel set, channel messages on any other channel are
// dropped; whatever is left is moved to channel 1, the one the emulated
// firmware listens on (its own receive channel stays at 1, so its SysEx device
// number never has to change). Omni just does the moving. System messages -
// SysEx included - always pass. This runs before the on-screen keyboard is
// merged in, so the keyboard plays whatever the channel is.
void VDX7AudioProcessor::filterMidiChannel(juce::MidiBuffer& midi)
{
    const int want = (int)std::lround(fnRaw_.midiCh->load(std::memory_order_relaxed));   // 0 = omni
    channelScratch_.clear();
    for (const auto meta : midi) {
        const auto* d = meta.data;
        const int n = meta.numBytes;
        if (n > 0 && d[0] >= 0x80 && d[0] < 0xF0) {
            if (want != 0 && (d[0] & 0x0F) != want - 1)
                continue;
            uint8_t b[3] = { (uint8_t)(d[0] & 0xF0), n > 1 ? d[1] : (uint8_t)0, n > 2 ? d[2] : (uint8_t)0 };
            channelScratch_.addEvent(b, juce::jmin(n, 3), meta.samplePosition);
        }
        else {
            channelScratch_.addEvent(d, n, meta.samplePosition);
        }
    }
    midi.swapWith(channelScratch_);
}

juce::RangedAudioParameter* VDX7AudioProcessor::paramForOffset(int off) const
{
    return apvts.getParameter(paramId(off));
}

void VDX7AudioProcessor::buildAutoParams()
{
    autoParams_.clear();
    for (const auto& pi : vdx7::Voice::paramTable()) {
        auto* rp = apvts.getParameter(paramId(pi.vcedOffset));
        auto* raw = apvts.getRawParameterValue(paramId(pi.vcedOffset));
        if (rp && raw) autoParams_.push_back({ pi.vcedOffset, rp, raw });
    }
    lastSent_.assign(autoParams_.size(), 0);
}

void VDX7AudioProcessor::pushVoiceToApvts(const vdx7::Voice& v)
{
    for (auto& ap : autoParams_) {
        const float val = (float)v.get(ap.offset);
        ap.param->setValueNotifyingHost(ap.param->convertTo0to1(val));
    }
}

VDX7AudioProcessor::~VDX7AudioProcessor() = default;

void VDX7AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(sampleRate);
    soundingNotes.reset();
    mono_.assign((size_t)juce::jmax(1, samplesPerBlock), 0.0f);
    fx_.prepare(sampleRate, juce::jmax(1, samplesPerBlock));
    channelScratch_.ensureSize(4096);
    sendCurrentVoice();
}

bool VDX7AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void VDX7AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numOutCh = buffer.getNumChannels();

    filterMidiChannel(midi);
    keyboardState.processNextMidiBuffer(midi, 0, numSamples, true);

    // Chord section: expands each note-on/off already in the buffer into up
    // to 6 extra transposed notes, in place, before anything is forwarded to
    // the engine - so the added notes are voices the engine itself renders.
    fx_.processMidi(midi);

    // What the engine is about to play, chord notes included - for the
    // on-screen keyboard only (display state, never fed back into MIDI).
    soundingNotes.processNextMidiBuffer(midi, 0, numSamples, false);

    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        engine_.pushMidi(msg.getRawData(), msg.getRawDataSize());
    }
    midi.clear();

    // Forward parameter changes (knob edits *and* host automation both land in
    // the APVTS) to the emulator.  We diff the atomic parameter values against
    // the last values sent; only the deltas become live parameter-change SysEx.
    // After a whole-patch load (baselineReload_), we simply adopt the new values
    // as the baseline so the wholesale voice load handles the sound instead of
    // emitting 145 redundant edits.
    //
    // This runs while the emulator is still booting too: the edits then land
    // in the native engine (which is what is playing), and the emulator picks
    // them all up in one go when it takes over (DX7Engine::primeEmulator).
    // Holding them back until the boot finished used to lose them for good.
    {
        const bool reload = baselineReload_.exchange(false, std::memory_order_acquire);
        for (size_t i = 0; i < autoParams_.size(); ++i)
        {
            const int v = (int)std::lround(autoParams_[i].raw->load(std::memory_order_relaxed));
            if (reload) {
                lastSent_[i] = (uint8_t)v;
            }
            else if ((uint8_t)v != lastSent_[i]) {
                lastSent_[i] = (uint8_t)v;
                engine_.sendParamChange(autoParams_[i].offset, v);
            }
        }
    }

    // FUNCTION page and the native DC blocker. Cheap: only changes go further.
    engine_.setFunctions(readFunctionParameters());
    engine_.setDcBlockHz(fnRaw_.dcBlock->load(std::memory_order_relaxed));

    if ((int)mono_.size() < numSamples) mono_.assign((size_t)numSamples, 0.0f);
    engine_.renderMono(mono_.data(), numSamples);

    for (int ch = 0; ch < numOutCh; ++ch)
        buffer.copyFrom(ch, 0, mono_.data(), numSamples);

    // Global effects, after the emulator. The chain is stereo (the chorus,
    // phaser and reverb all widen a mono source), so on a mono bus we run it
    // against a scratch right channel and fold the result back down.
    if (numOutCh >= 2)
    {
        fx_.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    }
    else if (numOutCh == 1)
    {
        if ((int)scratchR_.size() < numSamples) scratchR_.assign((size_t)numSamples, 0.0f);
        juce::FloatVectorOperations::copy(scratchR_.data(), mono_.data(), numSamples);

        float* l = buffer.getWritePointer(0);
        fx_.process(l, scratchR_.data(), numSamples);

        for (int n = 0; n < numSamples; ++n)
            l[n] = (l[n] + scratchR_[(size_t)n]) * 0.5f;
    }
}

void VDX7AudioProcessor::sendCurrentVoice()
{
    uint8_t packed[128];
    currentVoice_.pack(packed);
    engine_.loadVoice(packed);
}

// ---------------------------------------------------------------------------
//  ROM loading
// ---------------------------------------------------------------------------
bool VDX7AudioProcessor::loadFirmwareRom(const void* data, size_t size,
    const juce::String& source,
    juce::String& errorOut)
{
    if (!roms_.loadFirmware(data, size, source, errorOut))
        return false;

    romPath_ = source;   // remembered in this instance's own plugin state only
    afterRomChange();
    return true;
}

bool VDX7AudioProcessor::loadFactoryVoices(const void* data, size_t size,
    const juce::String& source,
    juce::String& errorOut)
{
    const bool hadVoices = roms_.hasVoices();
    if (!roms_.loadVoices(data, size, source, errorOut))
        return false;

    // Without a voice ROM, bank 0 *is* the starter bank. When the ROM arrives
    // the starter bank moves to its own slot after ROM1A..ROM4B, so follow it
    // there: loading the file adds eight banks, it does not change the sound.
    if (!hadVoices && !userActive_ && bank_ == 0)
        bank_ = vdx7::kStarterBankIndex;

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

void VDX7AudioProcessor::unloadFirmware()
{
    roms_.clearFirmware();
    romPath_.clear();
    afterRomChange();
}

void VDX7AudioProcessor::unloadFactoryVoices()
{
    roms_.clearVoices();
    voicesPath_.clear();
    if (bank_ == vdx7::kStarterBankIndex) bank_ = 0;   // the starter bank is bank 0 again
    afterRomChange();
}

void VDX7AudioProcessor::afterRomChange()
{
    engine_.romsChanged();

    // A voice ROM appearing or disappearing changes what the bank selector is
    // pointing at, so re-resolve the current slot and push it through again.
    if (!userActive_)
        currentVoice_ = vdx7::factoryBank(roms_, bank_).voice(prog_);

    loadVoiceIntoModel(currentVoice_);   // broadcasts, so any open editor refreshes
}

// True when the native engine's tuning actually moves any key off 12-TET A440.
// Tuning::isActive() alone cannot answer that: it is also true whenever the
// MTS-ESP client has attached its lookup, which it does even with no master
// running (the lookup then just answers "no opinion" and 12-TET plays) - so
// the status line would claim "microtuned" permanently.
static bool anyKeyRetuned(const vdx7native::Tuning& t)
{
    for (int n = 0; n < 128; ++n)
    {
        const float et = 440.0f * std::exp2((float)(n - 69) / 12.0f);
        if (std::abs(t.noteToHz(n) / et - 1.0f) > 1.0e-4f)   // ~0.2 cent
            return true;
    }
    return false;
}

juce::String VDX7AudioProcessor::engineDescription() const
{
    if (engine_.isBooting()) return "Emulator (booting...)";
    if (usingEmulator())     return "Emulator (firmware ROM)";

    juce::String s("Native FM (no ROM)");
    if (engine_.mtsEspActive())
    {
        s += " - MTS-ESP";

        // A master that names its scale gets the name shown, trimmed so a long
        // one cannot push the rest of the line out of the label.
        const juce::String scale(engine_.mtsScaleName());
        if (scale.isNotEmpty())
            s += " (" + scale.substring(0, 16).trim() + ")";
    }
    else if (anyKeyRetuned(engine_.tuning())) s += " - microtuned";
    return s;
}

// Load a whole patch: make it the model base, mirror it into the APVTS (so the
// host + UI follow), and load it into the emulator wholesale.
void VDX7AudioProcessor::loadVoiceIntoModel(const vdx7::Voice& v, bool broadcast)
{
    currentVoice_ = v;
    engine_.setNativeVoice(v.vced());
    pushVoiceToApvts(v);
    sendCurrentVoice();
    baselineReload_.store(true, std::memory_order_release);
    if (broadcast) sendChangeMessage();
}

void VDX7AudioProcessor::setParam(int vcedOffset, int value)
{
    // Route single-parameter edits through the APVTS so the host sees the write;
    // the audio-thread diff then forwards the change to the emulator.
    if (auto* p = paramForOffset(vcedOffset))
        p->setValueNotifyingHost(p->convertTo0to1((float)value));
}

std::vector<uint8_t> VDX7AudioProcessor::exportVoiceSysex() const
{
    return getVoiceCopy().toSingleVoiceSysex(0);
}

std::vector<uint8_t> VDX7AudioProcessor::exportBankSysex() const
{
    vdx7::Bank b = (userActive_ && hasUserBank_) ? userBank_ : vdx7::factoryBank(roms_, bank_);
    b.setVoice(prog_, getVoiceCopy());
    return b.toBankSysex(0);
}

void VDX7AudioProcessor::setVoice(const vdx7::Voice& v, bool /*sendNow*/)
{
    loadVoiceIntoModel(v);
}

void VDX7AudioProcessor::selectFactory(int bank, int prog)
{
    bank_ = juce::jlimit(0, vdx7::kStarterBankIndex, bank);
    prog_ = juce::jlimit(0, 31, prog);
    userActive_ = false;
    loadVoiceIntoModel(vdx7::factoryBank(roms_, bank_).voice(prog_));
}

bool VDX7AudioProcessor::importSysexBank(const void* data, size_t len,
    const juce::String& displayName)
{
    vdx7::Bank parsed;
    if (!vdx7::findBankInSysex(static_cast<const uint8_t*> (data), len, parsed))
        return false;
    userBank_ = parsed;
    hasUserBank_ = true;
    if (displayName.isNotEmpty()) userBankName_ = displayName;
    selectUserProgram(0);
    return true;
}

void VDX7AudioProcessor::selectUserProgram(int prog)
{
    if (!hasUserBank_) return;
    prog_ = juce::jlimit(0, 31, prog);
    userActive_ = true;
    loadVoiceIntoModel(userBank_.voice(prog_));
}

void VDX7AudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    const vdx7::Voice v = getVoiceCopy();     // includes live edits/automation + name
    juce::ValueTree vt("VDX7STATE");
    vt.setProperty("bank", bank_, nullptr);
    vt.setProperty("prog", prog_, nullptr);
    vt.setProperty("userActive", userActive_, nullptr);
    vt.setProperty("vced",
        juce::MemoryBlock(v.vced(), vdx7::kVcedSize).toBase64Encoding(), nullptr);
    if (hasUserBank_) {
        vt.setProperty("userbank",
            juce::MemoryBlock(userBank_.packed(), 4096).toBase64Encoding(), nullptr);
        vt.setProperty("userbankname", userBankName_, nullptr);
    }

    // Where the ROMs were loaded from. Saving the location rather than the
    // bytes is the whole point: the project file stays free of Yamaha's data,
    // and reopening it on a machine that has the ROM finds it again by itself.
    if (romPath_.isNotEmpty())    vt.setProperty("romPath", romPath_, nullptr);
    if (voicesPath_.isNotEmpty()) vt.setProperty("voicesPath", voicesPath_, nullptr);

    // FX settings are not part of the DX7 patch (they have no VCED bytes), so
    // they ride along as their own child tree rather than in "vced".
    juce::ValueTree fxTree("FX");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith("fx"))
                fxTree.setProperty(rp->paramID, rp->convertFrom0to1(rp->getValue()), nullptr);
    vt.appendChild(fxTree, nullptr);

    // The FUNCTION page, the same way.
    juce::ValueTree fnTree("FN");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith("fn"))
                fnTree.setProperty(rp->paramID, rp->convertFrom0to1(rp->getValue()), nullptr);
    vt.appendChild(fnTree, nullptr);

    if (auto xml = vt.createXml())
        copyXmlToBinary(*xml, dest);
}

// Restores the "FX" child written above. Missing properties keep their
// defaults, so a state saved before the FX section existed still loads.
void VDX7AudioProcessor::restoreFxState(const juce::ValueTree& vt)
{
    auto fxTree = vt.getChildWithName("FX");
    if (!fxTree.isValid())
        return;

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith("fx") && fxTree.hasProperty(rp->paramID))
                rp->setValueNotifyingHost(
                    rp->convertTo0to1((float)fxTree.getProperty(rp->paramID)));
}

// Restores the "FN" child written above. A state from before the FUNCTION page
// existed has none, and then every setting keeps its default.
void VDX7AudioProcessor::restoreFnState(const juce::ValueTree& vt)
{
    auto fnTree = vt.getChildWithName("FN");
    if (!fnTree.isValid())
        return;

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith("fn") && fnTree.hasProperty(rp->paramID))
                rp->setValueNotifyingHost(
                    rp->convertTo0to1((float)fnTree.getProperty(rp->paramID)));
}

void VDX7AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // New XML format.
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto vt = juce::ValueTree::fromXml(*xml);
        if (vt.isValid() && vt.hasType("VDX7STATE"))
        {
            juce::MemoryBlock vced;
            vced.fromBase64Encoding(vt.getProperty("vced").toString());
            if (vced.getSize() >= (size_t)vdx7::kVcedSize)
            {
                bank_ = (int)vt.getProperty("bank", 0);
                prog_ = (int)vt.getProperty("prog", 0);
                userActive_ = (bool)vt.getProperty("userActive", false);

                // Re-open the ROMs *this instance* was saved with, if they are
                // still where they were. This is this instance's own state -
                // it never looks at or affects any other instance's ROMs.
                // Failing is not an error: the plugin simply keeps running the
                // native engine, and the user can point it at the files again
                // from the ROM menu.
                {
                    const auto rp = vt.getProperty("romPath", "").toString();
                    const auto vp = vt.getProperty("voicesPath", "").toString();

                    bool changed = false;
                    if (rp.isNotEmpty() && !roms_.hasFirmware())
                        changed |= roms_.tryLoadFromPath(rp, true);
                    if (vp.isNotEmpty() && !roms_.hasVoices())
                        changed |= roms_.tryLoadFromPath(vp, false);

                    romPath_ = roms_.firmwareSource();
                    voicesPath_ = roms_.voicesSource();
                    if (changed) engine_.romsChanged();
                }

                if (vt.hasProperty("userbank")) {
                    juce::MemoryBlock ub;
                    ub.fromBase64Encoding(vt.getProperty("userbank").toString());
                    if (ub.getSize() >= 4096) {
                        userBank_.load(static_cast<const uint8_t*> (ub.getData()));
                        hasUserBank_ = true;
                        userBankName_ = vt.getProperty("userbankname", "USER").toString();
                    }
                }

                vdx7::Voice v;
                std::memcpy(v.vced(), vced.getData(), vdx7::kVcedSize);
                loadVoiceIntoModel(v);
                restoreFxState(vt);
                restoreFnState(vt);
                return;
            }
        }
    }

    // Legacy binary format ('VDX7' + 155 VCED + bank + prog).
    if (sizeInBytes < 4 + vdx7::kVcedSize) return;
    juce::MemoryInputStream is(data, (size_t)sizeInBytes, false);
    if (is.readInt() != 0x56445837) return;
    uint8_t vced[vdx7::kVcedSize];
    is.read(vced, vdx7::kVcedSize);
    vdx7::Voice v;
    std::memcpy(v.vced(), vced, vdx7::kVcedSize);
    if (is.getNumBytesRemaining() >= 8) { bank_ = is.readInt(); prog_ = is.readInt(); }
    loadVoiceIntoModel(v);
}

juce::AudioProcessorEditor* VDX7AudioProcessor::createEditor()
{
    return new VDX7AudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VDX7AudioProcessor();
}