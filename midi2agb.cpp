#include <string>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <unordered_map>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cmath>
#include <cassert>
#include <cstring>

#include "cppmidi/cppmidi.h"

static void dbg(const char *msg, ...);
static void die(const char *msg, ...);
static void err(const char *msg, ...);

static void usage() {
    err("midi2agb, version %s\n", GIT_VERSION);
    err("\n");
    err("Usage:\n$ midi2agb <input.mid> [<output.mid>] [options]\n\n");
    err("Options:\n");
    err("-s <sym>      | symbol name for song header (default: file name)\n");
    err("-m <mvl>      | master volume 0..128 (default: 128)\n");
    err("-g <vgr>      | voicegroup symbol name (default: voicegroup000)\n");
    err("-p <pri>      | song priority 0..127 (default: 0)\n");
    err("-r <rev>      | song reverb 0..127 (default: 0)\n");
    err("-n            | apply natural volume scale\n");
    err("-v            | output debug information\n");
    err("--modt <val>  | global modulation type 0..2\n");
    err("--modsc <val> | global modulation scale 0.0 - 16.0\n");
    err("--lfos <val>  | global modulation speed 0..255\n");
    err("              | (val * 24 / 256) oscillations per beat\n");
    err("--lfodl <val> | global modulation delay 0..255 ticks\n");
    exit(1);
}

static void fix_str(std::string& str) {
    // replaces all characters that are not alphanumerical
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] >= 'a' && str[i] <= 'z')
            continue;
        if (str[i] >= 'A' && str[i] <= 'Z')
            continue;
        if (str[i] >= '0' && str[i] <= '9' && i > 0)
            continue;
        str[i] = '_';
    }
}

static char path_seperators[] = {
    '/',
#ifdef _WIN32
    '\\',
#endif
    '\0'
};

static std::string filename_without_ext(const std::string& str) {
    size_t last_path_seperator = 0;
    char *sep = path_seperators;
    while (*sep) {
        size_t pos = str.find_last_of(*sep);
        if (pos != std::string::npos)
            last_path_seperator = std::max(pos, last_path_seperator);
        sep += 1;
    }
    size_t file_ext_dot_pos = str.find_last_of('.');
    if (file_ext_dot_pos == std::string::npos)
        return std::string(str);
    assert(file_ext_dot_pos != last_path_seperator);
    if (file_ext_dot_pos > last_path_seperator)
        return str.substr(0, file_ext_dot_pos);
    return std::string(str);
}

static std::string filename_without_dir(const std::string& str) {
    size_t last_path_seperator = 0;
    bool path_seperator_found = false;
    char *sep = path_seperators;
    while (*sep) {
        size_t pos = str.find_last_of(*sep);
        if (pos != std::string::npos) {
            last_path_seperator = std::max(pos, last_path_seperator);
            path_seperator_found = true;
        }
        sep += 1;
    }
    if (str.size() > 0 && path_seperator_found) {
        return str.substr(last_path_seperator + 1);
    } else {
        return std::string(str);
    }
}

static std::string arg_sym;
static uint8_t arg_mvl = 128;
static std::string arg_vgr;
static uint8_t arg_pri = 0;
static uint8_t arg_rev = 0;
static bool arg_natural = false;

// conditional global event options

static uint8_t arg_modt = 0;
static bool arg_modt_global = false;
static uint8_t arg_lfos = 0;
static bool arg_lfos_global = false;
static uint8_t arg_lfodl = 0;
static bool arg_lfodl_global = false;
static float arg_mod_scale = 1.0f;

static std::string arg_input_file;
static bool arg_input_file_read = false;
static std::string arg_output_file;
static bool arg_output_file_read = false;

// misc arguments

static bool arg_debug_output = false;

// 

static cppmidi::midi_file mf;

static void midi_read_infile_arguments();

static void midi_remove_empty_tracks();
static void midi_apply_filters();
static void midi_apply_loop_and_state_reset();
static void midi_remove_redundant_events();

static void midi_to_agb();

static void agb_optimize();

static void write_agb();

int main(int argc, char *argv[]) {
    if (argc == 1)
        usage();
    auto start = std::chrono::high_resolution_clock::now();
    try {
        // argument parsing
        for (int i = 1; i < argc; i++) {
            std::string st(argv[i]);
            if (!st.compare("-s")) {
                if (++i >= argc)
                    die("-s: missing parameter\n");
                arg_sym = argv[i];
                fix_str(arg_sym);
            } else if (!st.compare("-m")) {
                if (++i >= argc)
                    die("-m: missing parameter\n");
                int mvl = std::stoi(argv[i]);
                if (mvl < 0 || mvl > 128)
                    die("-m: parameter %d out of range\n", mvl);
                arg_mvl = static_cast<uint8_t>(mvl);
            } else if (!st.compare("-g")) {
                if (++i >= argc)
                    die("-g missing parameter\n");
                arg_vgr = argv[i];
                fix_str(arg_vgr);
            } else if (!st.compare("-p")) {
                if (++i >= argc)
                    die("-p: missing parameter\n");
                int prio = std::stoi(argv[i]);
                if (prio < 0 || prio > 127)
                    die("-p: parameter %d out of range\n", prio);
                arg_pri = static_cast<uint8_t>(prio);
            } else if (!st.compare("-r")) {
                if (++i >= argc)
                    die("-r: missing parameter\n");
                int rev = std::stoi(argv[i]);
                if (rev < 0 || rev > 127)
                    die("-r: parameter %d out of range\n", rev);
                arg_rev = static_cast<uint8_t>(rev);
            } else if (!st.compare("-n")) {
                arg_natural = true;
            } else if (!st.compare("-v")) {
                arg_debug_output = true;
            } else if (!st.compare("--modt")) {
                if (++i >= argc)
                    die("--modt: missing parameter\n");
                int modt = std::stoi(argv[i]);
                if (modt < 0 || modt > 2)
                    die("--modt: parameter %d out of range\n", modt);
                arg_modt = static_cast<uint8_t>(modt);
                arg_modt_global = true;
            } else if (!st.compare("--modsc")) {
                if (++i >= argc)
                    die("--modsc: missing parameter\n");
                float modscale = std::stof(std::string(argv[i]));
                if (modscale < 0.0f || modscale > 16.0f)
                    die("--modscale: parameter %f out of range\n", modscale);
                arg_mod_scale = modscale;
            } else if (!st.compare("--lfos")) {
                if (++i >= argc)
                    die("--lfos: missing parameter\n");
                int lfos = std::stoi(argv[i]);
                if (lfos < 0 || lfos > 127)
                    die("--lfos: parameter %d out of range\n", lfos);
                arg_lfos = static_cast<uint8_t>(lfos);
                arg_lfos_global = true;
            } else if (!st.compare("--lfodl")) {
                if (++i >= argc)
                    die("--lfodl: missing parameter\n");
                int lfodl = std::stoi(argv[i]);
                if (lfodl < 0 || lfodl > 127)
                    die("--lfodl: parameter %d out of range\n", lfodl);
                arg_lfodl = static_cast<uint8_t>(lfodl);
                arg_lfodl_global = true;
            } else {
                if (!st.compare("--")) {
                    if (++i >= argc)
                        die("--: missing file name\n");
                }
                if (!arg_input_file_read) {
                    arg_input_file = argv[i];
                    if (arg_input_file.size() < 1)
                        die("empty input file name\n");
                    arg_input_file_read = true;
                } else if (!arg_output_file_read) {
                    arg_output_file = argv[i];
                    if (arg_output_file.size() < 1)
                        die("empty output file name\n");
                    arg_output_file_read = true;
                } else {
                    die("Too many files specified\n");
                }
            }
        }

        // check arguments
        if (!arg_input_file_read) {
            die("No input file specified\n");
        }

        if (!arg_output_file_read) {
            // create output file name if none is provided
            arg_output_file = filename_without_ext(arg_input_file) + ".s";
            arg_output_file_read = true;
        }

        if (arg_sym.size() == 0) {
            arg_sym = filename_without_dir(filename_without_ext(arg_output_file));
            fix_str(arg_sym);
        }
        if (arg_vgr.size() == 0) {
            arg_vgr = "voicegroup000";
        }

        // load midi file
        mf.load_from_file(arg_input_file);

        // 24 clocks per quarter note is pretty much the standard for GBA
        mf.convert_time_division(24);

        midi_read_infile_arguments();

        midi_remove_empty_tracks();
        midi_apply_filters();
        midi_apply_loop_and_state_reset();
        midi_remove_redundant_events();

        midi_to_agb();

        agb_optimize();

        write_agb();
    } catch (const cppmidi::xcept& ex) {
        fprintf(stderr, "cppmidi lib error:\n%s\n", ex.what());
        return 1;
    } catch (const std::exception& ex) {
        fprintf(stderr, "std lib error:\n%s\n", ex.what());
        return 1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    dbg("took %ld us\n",
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
}

static const uint8_t MIDI_CC_EX_BENDR = 20;
static const uint8_t MIDI_CC_EX_LFOS = 21;
static const uint8_t MIDI_CC_EX_MODT = 22;
static const uint8_t MIDI_CC_EX_TUNE = 24;
static const uint8_t MIDI_CC_EX_LFODL = 26;
static const uint8_t MIDI_CC_EX_LOOP = 30;
static const uint8_t MIDI_CC_EX_PRIO = 33;

static const uint8_t EX_LOOP_START = 100;
static const uint8_t EX_LOOP_END = 101;

struct agb_ev {
    enum class ty {
        WAIT, LOOP_START, LOOP_END, PRIO, TEMPO, KEYSH, VOICE, VOL, PAN,
        BEND, BENDR, LFOS, LFODL, MOD, MODT, TUNE, XCMD, EOT, TIE, NOTE,
    } type;
    agb_ev(ty type) : type(type) {}
    union {
        uint32_t wait;
        uint8_t prio;
        uint8_t tempo;
        int8_t keysh;
        uint8_t voice;
        uint8_t vol;
        int8_t pan;
        int8_t bend;
        uint8_t bendr;
        uint8_t lfos;
        uint8_t lfodl;
        uint8_t mod;
        uint8_t modt;
        int8_t tune;
        struct { uint8_t type; uint8_t par; } xcmd;
        struct { uint8_t key; } eot;
        struct { uint8_t key; uint8_t vel; } tie;
        struct { uint8_t len; uint8_t key; uint8_t vel; } note;
    };
    size_t size() const {
        switch (type) {
        case ty::WAIT: return 1;
        case ty::LOOP_START: return 0;
        case ty::LOOP_END: return 5;
        case ty::PRIO: return 2;
        case ty::TEMPO: return 2;
        case ty::KEYSH: return 2;
        case ty::VOICE: return 2;
        case ty::VOL: return 2;
        case ty::PAN: return 2;
        case ty::BEND: return 2;
        case ty::BENDR: return 2;
        case ty::LFOS: return 2;
        case ty::LFODL: return 2;
        case ty::MOD: return 2;
        case ty::MODT: return 2;
        case ty::TUNE: return 2;
        case ty::XCMD: return 3;
        case ty::EOT: return 2;
        case ty::TIE: return 3;
        case ty::NOTE: return 4;
        default: throw std::runtime_error("agb_ev::size() error");
        }
    }
    bool operator==(const agb_ev& rhs) const {
        if (type != rhs.type)
            return false;
        switch (type) {
        case ty::WAIT: return wait == rhs.wait;
        case ty::LOOP_START: return true;
        case ty::LOOP_END: return true;
        case ty::PRIO: return prio == rhs.prio;
        case ty::TEMPO: return tempo == rhs.tempo;
        case ty::KEYSH: return keysh == rhs.keysh;
        case ty::VOICE: return voice == rhs.voice;
        case ty::VOL: return vol == rhs.vol;
        case ty::PAN: return pan == rhs.pan;
        case ty::BEND: return bend == rhs.bend;
        case ty::BENDR: return bendr == rhs.bendr;
        case ty::LFOS: return lfos == rhs.lfos;
        case ty::LFODL: return lfodl == rhs.lfodl;
        case ty::MOD: return mod == rhs.mod;
        case ty::MODT: return modt == rhs.modt;
        case ty::TUNE: return tune == rhs.tune;
        case ty::XCMD: return xcmd.type == rhs.xcmd.type && xcmd.par == rhs.xcmd.par;
        case ty::EOT: return eot.key == rhs.eot.key;
        case ty::TIE: return tie.key == rhs.tie.key && tie.vel == rhs.tie.vel;
        case ty::NOTE: return note.len == rhs.note.len && note.key == rhs.note.key && note.vel == rhs.note.vel;
        default: throw std::runtime_error("agb_ev::operator== error");
        }
    }
    bool operator!=(const agb_ev& rhs) const {
        return !operator==(rhs);
    }
    size_t hash() const {
        switch (type) {
        case ty::WAIT:
            return wait ^ 0xd5697712;
        case ty::LOOP_START:
        case ty::LOOP_END:
            return static_cast<size_t>(type);
        case ty::PRIO:
        case ty::TEMPO:
        case ty::KEYSH:
        case ty::VOICE:
        case ty::VOL:
        case ty::PAN:
        case ty::BEND:
        case ty::BENDR:
        case ty::LFOS:
        case ty::LFODL:
        case ty::MOD:
        case ty::MODT:
        case ty::TUNE:
        case ty::EOT:
            return prio ^ static_cast<size_t>(type);
        case ty::XCMD:
        case ty::TIE:
            return static_cast<size_t>(xcmd.type) ^
                static_cast<size_t>(xcmd.par << 1) ^ 0x709124be;
        case ty::NOTE:
            return note.len ^ static_cast<size_t>(note.key << 1) ^
                static_cast<size_t>(note.vel << 2) ^ 0x41698a8e;
        default:
            throw std::runtime_error("hash error");
        }
    }
};

struct agb_bar {
    agb_bar() : is_referenced(false), does_reference(false) {}
    std::vector<agb_ev> events;
    bool operator==(const agb_bar& rhs) const {
        if (events.size() != rhs.events.size())
            return false;
        for (size_t i = 0; i < events.size(); i++) {
            if (events[i] != rhs.events[i])
                return false;
        }
        return true;
    }
    bool operator!=(const agb_bar& rhs) const {
        return !operator==(rhs);
    }
    size_t hash() const {
        size_t hs = 0x1;
        for (const agb_ev& ev : events) {
            size_t h = ev.hash();
            hs *= h;
            hs ^= h;
        }
        return hs;
    }
    size_t size() const {
        size_t s = 0;
        for (const agb_ev& ev : events) {
            s += ev.size();
        }
        return s;
    }
    bool is_referenced;
    bool does_reference;
};

struct agb_bar_ref_hasher {
    typedef std::reference_wrapper<agb_bar> rw_agb_bar;
    size_t operator()(const rw_agb_bar& x) const {
        return x.get().hash();
    }
    bool operator()(const rw_agb_bar& a,const rw_agb_bar& b) const {
        return a.get() == b.get();
    }
};

struct agb_track {
    std::vector<agb_bar> bars;
};

struct agb_song {
    std::vector<agb_track> tracks;
};

agb_song as;

/*
 * std::clamp theoretically is in C++17 but at least today
 * gcc won't even compile it's own headers with c++1z enabled
 */
template<typename T>
const T& clamp(const T& v, const T& lo, const T& hi) {
    assert(lo <= hi);
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

static inline bool ev_tick_cmp(
        const std::unique_ptr<cppmidi::midi_event>& a,
        const std::unique_ptr<cppmidi::midi_event>& b) {
    return a->ticks < b->ticks;
}

static int trk_get_channel_num(const cppmidi::midi_track& trk) {
    using namespace cppmidi;

    int channel = -1;
    for (const std::unique_ptr<midi_event>& ev : trk.midi_events) {
        const message_midi_event *mev = dynamic_cast<const message_midi_event*>(&*ev);
        if (mev) {
            channel = mev->channel();
            break;
        }
    }
    return channel;
}

template<class T, int ctrl = -1>
static bool find_next_event_at_tick_index(const cppmidi::midi_track& mtrk,
        const size_t start_event, size_t& next_event) {
    size_t next_index = start_event + 1;
    while (1) {
        if (next_index >= mtrk.midi_events.size())
            return false;
        if (mtrk[next_index]->ticks > mtrk[start_event]->ticks)
            return false;
        if (typeid(*mtrk[next_index]) == typeid(T)) {
            if (ctrl != -1) {
                const cppmidi::controller_message_midi_event& cev =
                    static_cast<const cppmidi::controller_message_midi_event&>(
                            *mtrk[next_index]);
                if (cev.get_controller() == ctrl) {
                    next_event = next_index;
                    return true;
                }
            } else {
                next_event = next_index;
                return true;
            }
        }
        next_index += 1;
    }
}

const uint8_t MIDI_NOTE_PARSE_INIT = 0x0;
const uint8_t MIDI_NOTE_PARSE_SHORT = 0x1;
const uint8_t MIDI_NOTE_PARSE_TIE = 0x2;

static void midi_read_infile_arguments() {
    using namespace cppmidi;
    /*
     * Special Events (use Marker/Text/Cuepoint):
     * - "[": loop start for all tracks
     * - "]": loop end for all tracks
     * - "modt=%d": set's modulation type at position
     * - "tune=%d": set's tuning (+-1 key, range -64 to +63)
     * - "lfos=%d": set's lfo speed
     * - "lfos_global=%d": ^ globally
     * - "lfodl=%d": set's lfo delay
     * - "lfodl_global=%d": ^ globally
     * - "modscale_global=%f": scales modulation by factor %f
     * - "modt_global=%d": set's modulation type for whole song
     */
    bool found_start = false, found_end = false;
    uint32_t loop_start = 0, loop_end = 0;

    uint8_t lsb_rpn = 0, msb_rpn = 0;
    uint32_t last_event = 0;

    std::vector<bool> volume_init(mf.midi_tracks.size(), false);

    // parse meta events
    for (size_t itrk = 0; itrk < mf.midi_tracks.size(); itrk++) {
        midi_track& mtrk = mf[itrk];
        for (size_t ievt = 0; ievt < mtrk.midi_events.size(); ievt++) {
            midi_event& ev = *mtrk[ievt];
            last_event = std::max(ev.ticks, last_event);
            std::string ev_text;
            if (typeid(ev) == typeid(marker_meta_midi_event)) {
                // marker
                const marker_meta_midi_event& mev = 
                    static_cast<const marker_meta_midi_event&>(ev);
                ev_text = mev.get_text();
            } else if (typeid(ev) == typeid(text_meta_midi_event)) {
                // text
                const text_meta_midi_event& tev =
                    static_cast<const text_meta_midi_event&>(ev);
                ev_text = tev.get_text();
            } else if (typeid(ev) == typeid(cuepoint_meta_midi_event)) {
                // cuepoint
                const cuepoint_meta_midi_event& cev =
                    static_cast<const cuepoint_meta_midi_event&>(ev);
                ev_text = cev.get_text();
            } else if (typeid(ev) == typeid(controller_message_midi_event)) {
                const controller_message_midi_event& cev =
                    static_cast<const controller_message_midi_event&>(ev);
                if (cev.get_controller() == MIDI_CC_LSB_RPN) {
                    lsb_rpn = cev.get_value();
                } else if (cev.get_controller() == MIDI_CC_MSB_RPN) {
                    msb_rpn = cev.get_value();
                } else if (cev.get_controller() == MIDI_CC_MSB_DATA_ENTRY &&
                        msb_rpn == 0 && lsb_rpn == 0) {
                    // found a bend range command
                    mtrk[ievt] = std::make_unique<controller_message_midi_event>(
                            cev.ticks, cev.channel(),
                            MIDI_CC_EX_BENDR, cev.get_value());
                } else if (cev.get_controller() == MIDI_CC_MSB_VOLUME) {
                    volume_init[itrk] = true;
                }
                continue;
            } else if (typeid(ev) == typeid(noteoff_message_midi_event)) {
                // for correct midi note parsing the velocity in the noteoff
                // command get's used for marking, reset to intial 0
                noteoff_message_midi_event& noteoff_ev =
                    static_cast<noteoff_message_midi_event&>(ev);
                noteoff_ev.set_velocity(MIDI_NOTE_PARSE_INIT);
                continue;
            } else {
                continue;
            }
            // found an event with a possibly valid text
            if (!ev_text.compare("[") || !ev_text.compare("loopStart")) {
                found_start = true;
                loop_start = ev.ticks;
            } else if (!ev_text.compare("]") || !ev_text.compare("loopEnd")) {
                found_end = true;
                loop_end = ev.ticks;
            } else if (!strncmp(ev_text.c_str(), "modt=", 5)) {
                int modt = std::stoi(ev_text.substr(5));
                modt = clamp(modt, 0, 2);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[ievt] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_MODT,
                            static_cast<uint8_t>(modt));
                }
            } else if (!strncmp(ev_text.c_str(), "modt_global=", 12)) {
                arg_modt_global = true;
                int modt = std::stoi(ev_text.substr(12));
                modt = clamp(modt, 0, 2);
                arg_modt = static_cast<uint8_t>(modt);
            } else if (!strncmp(ev_text.c_str(), "tune=", 5)) {
                int tune = std::stoi(ev_text.substr(5));
                tune = clamp(tune, -64, 63);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[ievt] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_TUNE,
                            static_cast<uint8_t>(tune));
                }
            } else if (!strncmp(ev_text.c_str(), "lfos=", 5)) {
                int lfos = std::stoi(ev_text.substr(5));
                lfos = clamp(lfos, 0, 127);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[ievt] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_LFOS,
                            static_cast<uint8_t>(lfos));
                }
            } else if (!strncmp(ev_text.c_str(), "lfos_global=", 12)) {
                arg_lfos_global = true;
                int lfos = std::stoi(ev_text.substr(12));
                lfos = clamp(lfos, 0, 127);
                arg_lfos = static_cast<uint8_t>(lfos);
            } else if (!strncmp(ev_text.c_str(), "lfodl=", 6)) {
                int lfodl = std::stoi(ev_text.substr(6));
                lfodl = clamp(lfodl, 0, 127);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[ievt] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_LFODL,
                            static_cast<uint8_t>(lfodl));
                }
            } else if (!strncmp(ev_text.c_str(), "lfodl_global=", 13)) {
                arg_lfodl_global = true;
                int lfodl = std::stoi(ev_text.substr(13));
                lfodl = clamp(lfodl, 0, 127);
                arg_lfodl = static_cast<uint8_t>(lfodl);
            } else if (!strncmp(ev_text.c_str(), "prio=", 5)) {
                int prio = std::stoi(ev_text.substr(5));
                prio = clamp(prio, 0, 127);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[ievt] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_PRIO,
                            static_cast<uint8_t>(prio));
                }
            } else if (!strncmp(ev_text.c_str(), "modscale_global=", 16)) {
                arg_mod_scale = std::stof(ev_text.substr(16));
                arg_mod_scale = clamp(arg_mod_scale, 0.0f, 16.0f);
                // the actual scale get's applied in a seperate filter
            }
        } // end event loop
    } // end track loop

    // insert loop and global events
    for (size_t itrk = 0; itrk < mf.midi_tracks.size(); itrk++) {
        midi_track& mtrk = mf[itrk];
        int chn = trk_get_channel_num(mtrk);
        // chn mustn't be negative because track with no message events
        // have been sorted out previously
        if (chn < 0)
            continue;
        if (found_start) {
            std::unique_ptr<midi_event> cev(new controller_message_midi_event(
                        loop_start, static_cast<uint8_t>(chn),
                        MIDI_CC_EX_LOOP, EX_LOOP_START));
            auto loop_start_event_pos = std::lower_bound(
                    mtrk.midi_events.begin(),
                    mtrk.midi_events.end(),
                    cev, ev_tick_cmp);
            mtrk.midi_events.insert(loop_start_event_pos, std::move(cev));
        }
        if (found_end) {
            std::unique_ptr<midi_event> cev(new controller_message_midi_event(
                        loop_end, static_cast<uint8_t>(chn),
                        MIDI_CC_EX_LOOP, EX_LOOP_END));
            auto loop_end_event_pos = std::upper_bound(
                    mtrk.midi_events.begin(),
                    mtrk.midi_events.end(),
                    cev, ev_tick_cmp);
            mtrk.midi_events.insert(loop_end_event_pos, std::move(cev));
        }
        if (arg_modt_global) {
            std::unique_ptr<midi_event> cev(new controller_message_midi_event(
                        0, static_cast<uint8_t>(chn),
                        MIDI_CC_EX_MODT, arg_modt));
            auto insert_pos = std::upper_bound(
                    mtrk.midi_events.begin(),
                    mtrk.midi_events.end(),
                    cev, ev_tick_cmp);
            mtrk.midi_events.insert(insert_pos, std::move(cev));
        }
        if (arg_lfos_global) {
            std::unique_ptr<midi_event> cev(new controller_message_midi_event(
                        0, static_cast<uint8_t>(chn),
                        MIDI_CC_EX_LFOS, arg_lfos));
            auto insert_pos = std::upper_bound(
                    mtrk.midi_events.begin(),
                    mtrk.midi_events.end(),
                    cev, ev_tick_cmp);
            mtrk.midi_events.insert(insert_pos, std::move(cev));
        }
        if (arg_lfodl_global) {
            std::unique_ptr<midi_event> cev(new controller_message_midi_event(
                        0, static_cast<uint8_t>(chn),
                        MIDI_CC_EX_LFODL, arg_lfos));
            auto insert_pos = std::upper_bound(
                    mtrk.midi_events.begin(),
                    mtrk.midi_events.end(),
                    cev, ev_tick_cmp);
            mtrk.midi_events.insert(insert_pos, std::move(cev));
        }
        if (!volume_init[itrk]) {
            std::unique_ptr<midi_event> cev(new controller_message_midi_event(
                        0, static_cast<uint8_t>(chn),
                        MIDI_CC_MSB_VOLUME, 127));
            auto insert_pos = std::upper_bound(
                    mtrk.midi_events.begin(),
                    mtrk.midi_events.end(),
                    cev, ev_tick_cmp);
            mtrk.midi_events.insert(insert_pos, std::move(cev));
        }
        std::unique_ptr<midi_event> dev(new dummy_midi_event(last_event));
        auto insert_pos = std::upper_bound(
                mtrk.midi_events.begin(),
                mtrk.midi_events.end(),
                dev, ev_tick_cmp);
        mtrk.midi_events.insert(insert_pos, std::move(dev));
    }
}

static void midi_remove_empty_tracks() {
    using namespace cppmidi;
    midi_track tempo_track;
    midi_track timesignature_track;

    // seperate tempo events
    for (midi_track& mtrk : mf.midi_tracks) {
        for (size_t ievt = 0; ievt < mtrk.midi_events.size(); ievt++) {
            if (typeid(*mtrk[ievt]) == typeid(tempo_meta_midi_event)) {
                uint32_t tick = mtrk[ievt]->ticks;
                tempo_track.midi_events.emplace_back(std::move(mtrk[ievt]));
                mtrk[ievt] = std::make_unique<dummy_midi_event>(tick);
            } else if (typeid(*mtrk[ievt]) == typeid(timesignature_meta_midi_event)) {
                uint32_t tick = mtrk[ievt]->ticks;
                timesignature_track.midi_events.emplace_back(std::move(mtrk[ievt]));
                mtrk[ievt] = std::make_unique<dummy_midi_event>(tick);
            }
        }
    }

    tempo_track.sort_events();
    timesignature_track.sort_events();

    // remove tracks without notes
    for (size_t itrk = 0; itrk < mf.midi_tracks.size(); itrk++) {
        // set false if a note event was found
        bool del = true;
        for (const std::unique_ptr<midi_event>& ev : mf[itrk].midi_events) {
            if (typeid(*ev) == typeid(noteon_message_midi_event)) {
                del = false;
                break;
            }
        }
        if (del) {
            dbg("deleting meta only track: %d\n", itrk);
            mf.midi_tracks.erase(mf.midi_tracks.begin() +
                    static_cast<long>(itrk--));
        }
    }

    if (mf.midi_tracks.size() == 0)
        return;

    // remove surplus time signature events
    for (size_t ievt = 0; ievt < timesignature_track.midi_events.size(); ievt++) {
        size_t dummy;
        if (find_next_event_at_tick_index<timesignature_meta_midi_event>(
                    timesignature_track, ievt, dummy)) {
            timesignature_track.midi_events.erase(timesignature_track.midi_events.begin() +
                    static_cast<long>(ievt--));
        }
    }

    // all empty tracks deleted, now reinsert tempo events to first track
    for (std::unique_ptr<midi_event>& tev : tempo_track.midi_events) {
        // locate position for insertion
        auto position = std::lower_bound(
                mf[0].midi_events.begin(),
                mf[0].midi_events.end(),
                tev, ev_tick_cmp);
        mf[0].midi_events.insert(position, std::move(tev));
    }

    for (std::unique_ptr<midi_event>& tev : timesignature_track.midi_events) {
        auto position = std::lower_bound(
                mf[0].midi_events.begin(),
                mf[0].midi_events.end(),
                tev, ev_tick_cmp);
        mf[0].midi_events.insert(position, std::move(tev));
    }
    // done
}

/*
 * midi_apply_filters() :
 *
 * Volume / Velocity:
 * The GBA engine will multiply the sample's waveform by the volume values.
 * Since this will do a linear scale and not a natural scale, this function
 * applies the scale beforehand. Also, expression and volume are combined
 * to volume only.
 *
 * Modulation Scale:
 * The scale of modulation intensity isn't really standardized.
 * Therefore an option to globally scale the modulation is offerred.
 */
static void midi_apply_filters() {
    using namespace cppmidi;

    auto vol_scale = [&](uint8_t vol, uint8_t expr) {
        double x = vol * expr * arg_mvl;
        if (arg_natural) {
            x /= 127.0 * 127.0 * 128.0;
            x = pow(x, 10.0 / 6.0);
            x *= 127.0;
            x = std::round(x);
        } else {
            x /= 127.0 * 128.0;
            x = std::round(x);
        }
        return static_cast<uint8_t>(clamp(static_cast<int>(x), 0, 127));
    };

    auto vel_scale = [&](uint8_t vel) {
        double x = vel;
        if (arg_natural) {
            x /= 127.0;
            x = pow(x, 10.0 / 6.0);
            x *= 127.0;
            x = std::round(x);
        }
        // clamp to lower 1 because midi velocity 0 is a note off
        return static_cast<uint8_t>(clamp(static_cast<int>(x), 0, 127));
    };

    for (midi_track& mtrk : mf.midi_tracks) {
        uint8_t volume = 100;
        uint8_t expression = 127;

        for (size_t ievt = 0; ievt < mtrk.midi_events.size(); ievt++) {
            midi_event& ev = *mtrk[ievt];
            if (typeid(ev) == typeid(controller_message_midi_event)) {
                controller_message_midi_event& ctrl_ev = 
                    static_cast<controller_message_midi_event&>(ev);
                if (ctrl_ev.get_controller() == MIDI_CC_MSB_VOLUME) {
                    volume = ctrl_ev.get_value();
                    ctrl_ev.set_value(vol_scale(volume, expression));
                } else if (ctrl_ev.get_controller() == MIDI_CC_MSB_EXPRESSION) {
                    expression = ctrl_ev.get_value();
                    ctrl_ev.set_controller(MIDI_CC_MSB_VOLUME);
                    ctrl_ev.set_value(vol_scale(volume, expression));
                } else if (ctrl_ev.get_controller() == MIDI_CC_MSB_MOD) {
                    float scaled_mod = ctrl_ev.get_value() * arg_mod_scale;
                    scaled_mod = std::roundf(scaled_mod);
                    ctrl_ev.set_value(static_cast<uint8_t>(clamp(scaled_mod, 0.0f, 127.0f)));
                }
            }
            else if (typeid(ev) == typeid(noteon_message_midi_event)) {
                noteon_message_midi_event& note_ev =
                    static_cast<noteon_message_midi_event&>(ev);
                note_ev.set_velocity(vel_scale(note_ev.get_velocity()));
            }
        }
    }
}

static void midi_apply_loop_and_state_reset() {
    using namespace cppmidi;

    for (midi_track& mtrk : mf.midi_tracks) {
        uint32_t tempo = 500000; // 120 bpm
        uint8_t voice = 0;
        uint8_t vol = 100;
        uint8_t pan = 0x40;
        int16_t bend = 0;
        uint8_t bendr = 2;
        uint8_t mod = 0;
        uint8_t modt = 0;
        uint8_t tune = 0x40;
        uint8_t prio = 0;
        // FIXME add memacc and pseudo echo for completeness
        // omitted for now because nobody would be using it

        uint32_t loop_start_tick = 0xFFFFFFFF;

        for (size_t itrk = 0; itrk < mtrk.midi_events.size(); itrk++) {
            midi_event& ev = *mtrk[itrk];
            if (typeid(ev) == typeid(tempo_meta_midi_event)) {
                tempo_meta_midi_event& tev = static_cast<tempo_meta_midi_event&>(ev);
                if (ev.ticks <= loop_start_tick)
                    tempo = tev.get_us_per_beat();
            } else if (typeid(ev) == typeid(program_message_midi_event)) {
                program_message_midi_event& pev = static_cast<program_message_midi_event&>(ev);
                if (ev.ticks <= loop_start_tick)
                    voice = pev.get_program();
            } else if (typeid(ev) == typeid(pitchbend_message_midi_event)) {
                pitchbend_message_midi_event& pev = static_cast<pitchbend_message_midi_event&>(ev);
                if (ev.ticks <= loop_start_tick)
                    bend = pev.get_pitch();
            } else if (typeid(ev) == typeid(controller_message_midi_event)) {
                controller_message_midi_event& cev = static_cast<controller_message_midi_event&>(ev);
                uint8_t ctrl = cev.get_controller();
                switch (ctrl) {
                case MIDI_CC_MSB_VOLUME:
                    if (ev.ticks <= loop_start_tick)
                        vol = cev.get_value();
                    break;
                case MIDI_CC_MSB_PAN:
                    if (ev.ticks <= loop_start_tick)
                        pan = cev.get_value();
                    break;
                case MIDI_CC_EX_BENDR:
                    if (ev.ticks <= loop_start_tick)
                        bendr = cev.get_value();
                    break;
                case MIDI_CC_MSB_MOD:
                    if (ev.ticks <= loop_start_tick)
                        mod = cev.get_value();
                    break;
                case MIDI_CC_EX_MODT:
                    if (ev.ticks <= loop_start_tick)
                        modt = cev.get_value();
                    break;
                case MIDI_CC_EX_TUNE:
                    if (ev.ticks <= loop_start_tick)
                        tune = cev.get_value();
                    break;
                case MIDI_CC_EX_PRIO:
                    if (ev.ticks <= loop_start_tick)
                        prio = cev.get_value();
                    break;
                case MIDI_CC_EX_LOOP:
                    if (cev.get_value() == EX_LOOP_START) {
                        // loop start
                        loop_start_tick = ev.ticks;
                    } else if (cev.get_value() == EX_LOOP_END &&
                            ev.ticks > loop_start_tick) {
                        // loop end insert events for start state
                        std::vector<std::unique_ptr<midi_event>> ptrs;
                        ptrs.emplace_back(new tempo_meta_midi_event(
                                    ev.ticks, tempo));
                        ptrs.emplace_back(new program_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    voice));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_MSB_VOLUME, vol));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_MSB_PAN, pan));
                        ptrs.emplace_back(new pitchbend_message_midi_event(
                                    ev.ticks, cev.channel(), bend));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_EX_BENDR, bendr));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_MSB_MOD, mod));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_EX_MODT, modt));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_EX_TUNE, tune));
                        ptrs.emplace_back(new controller_message_midi_event(
                                    ev.ticks, cev.channel(),
                                    MIDI_CC_EX_PRIO, prio));
                        mtrk.midi_events.insert(mtrk.midi_events.begin() +
                                static_cast<long>(itrk),
                                std::make_move_iterator(ptrs.begin()),
                                std::make_move_iterator(ptrs.end()));
                        itrk += ptrs.size();
                    }
                    break;
                default:
                    break;
                } // end switch
            } // end controller if
        } // end event loop
    } // end track loop
}

/*
 * Does what it says. Removes events that are not required to
 * reduces storage size
 */
static void midi_remove_redundant_events() {
    using namespace cppmidi;

    for (midi_track& mtrk : mf.midi_tracks) {
        uint8_t tempo = 150 / 2;
        uint8_t voice = 0;
        bool voice_init = false;
        uint8_t vol = 127;
        bool vol_init = false;
        uint8_t pan = 0x40;
        int8_t bend = 0;
        uint8_t bendr = 2;
        uint8_t mod = 0;
        uint8_t modt = 0;
        uint8_t tune = 0x40;
        uint8_t prio = 0;

        size_t dummy;

        for (size_t ievt = 0; ievt < mtrk.midi_events.size(); ievt++) {
            midi_event& ev = *mtrk.midi_events[ievt];
            if (typeid(ev) == typeid(tempo_meta_midi_event)) {
                tempo_meta_midi_event& tev = static_cast<tempo_meta_midi_event&>(ev);
                double halved_bpm = std::round(tev.get_bpm() * 0.5);
                halved_bpm = clamp(halved_bpm, 0.0, 255.0);
                uint8_t utempo = static_cast<uint8_t>(halved_bpm);
                if ((tempo == utempo) ||
                        find_next_event_at_tick_index<tempo_meta_midi_event>(
                            mtrk, ievt, dummy)) {
                    mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                } else {
                    tempo = utempo;
                }
            } else if (typeid(ev) == typeid(program_message_midi_event)) {
                program_message_midi_event& pev = static_cast<program_message_midi_event&>(ev);
                if ((voice_init && pev.get_program() == voice) ||
                        find_next_event_at_tick_index<program_message_midi_event>(
                            mtrk, ievt, dummy)) {
                    mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                } else {
                    voice_init = true;
                    voice = pev.get_program();
                }
            } else if (typeid(ev) == typeid(pitchbend_message_midi_event)) {
                pitchbend_message_midi_event& pev = static_cast<pitchbend_message_midi_event&>(ev);
                double dbend = pev.get_pitch() / 128.0;
                dbend = std::round(dbend);
                dbend = clamp(dbend, -64.0, +63.0);
                int8_t ubend = static_cast<int8_t>(dbend);
                if ((bend == ubend) ||
                        find_next_event_at_tick_index<pitchbend_message_midi_event>(
                            mtrk, ievt, dummy)) {
                    mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                } else {
                    bend = ubend;
                }
            } else if (typeid(ev) == typeid(controller_message_midi_event)) {
                controller_message_midi_event& cev = static_cast<controller_message_midi_event&>(ev);
                uint8_t ctrl = cev.get_controller();
                switch (ctrl) {
                case MIDI_CC_MSB_VOLUME:
                    if ((vol_init && vol == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_MSB_VOLUME>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        vol_init = true;
                        vol = cev.get_value();
                    }
                    break;
                case MIDI_CC_MSB_PAN:
                    if ((pan == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_MSB_PAN>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        pan = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_BENDR:
                    if ((bendr == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_BENDR>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        bendr = cev.get_value();
                    }
                    break;
                case MIDI_CC_MSB_MOD:
                    if ((mod == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_MSB_MOD>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        mod = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_MODT:
                    if ((modt == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_MODT>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        modt = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_TUNE:
                    if ((tune == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_TUNE>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        tune = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_LOOP:
                    if (cev.get_value() != EX_LOOP_START &&
                            cev.get_value() != EX_LOOP_END) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    }
                    break;
                case MIDI_CC_EX_PRIO:
                    if ((prio == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_PRIO>(mtrk, ievt, dummy)) {
                        mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    } else {
                        prio = cev.get_value();
                    }
                    break;
                default:
                    dbg("Removing MIDI event of type: %s\n", typeid(ev).name());
                    mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
                    break;
                } // end controller switch 
            } else if (typeid(ev) == typeid(timesignature_meta_midi_event)) {
                // ignore
            } else if (typeid(ev) == typeid(noteon_message_midi_event)) {
                // ignore
            } else if (typeid(ev) == typeid(noteoff_message_midi_event)) {
                // ignore
            } else {
                dbg("Removing MIDI event of type: %s\n", typeid(ev).name());
                mtrk[ievt] = std::make_unique<dummy_midi_event>(mtrk[ievt]->ticks);
            }
        } // end event loop
    } // end track for loop
}

struct bar {
    bar(uint32_t start_tick, uint32_t num_ticks)
        : start_tick(start_tick), num_ticks(num_ticks) {}
    uint32_t start_tick;
    uint32_t num_ticks;
};

static void midi_to_agb() {
    using namespace cppmidi;

    // create bar table
    uint32_t current_bar_len = 96;
    std::vector<bar> bar_table;

    bar_table.emplace_back(0, 0);
    if (mf.midi_tracks.size() == 0)
        return;
    const midi_track& mtrk = mf[0];

    uint32_t prev_tick = 0;

    for (size_t ievt = 0; ievt < mtrk.midi_events.size(); ievt++) {
        uint32_t diff_ticks = mtrk[ievt]->ticks - prev_tick;
        prev_tick = mtrk[ievt]->ticks;

        bar_table.back().num_ticks += diff_ticks;

        while (bar_table.back().num_ticks >= current_bar_len) {
            uint32_t new_num_ticks = bar_table.back().num_ticks - current_bar_len;
            bar_table.back().num_ticks = current_bar_len;
            bar_table.emplace_back(bar_table.back().start_tick +
                    bar_table.back().num_ticks, new_num_ticks);
        }

        if (typeid(*mtrk[ievt]) == typeid(timesignature_meta_midi_event)) {
            const timesignature_meta_midi_event& tev =
                static_cast<timesignature_meta_midi_event&>(*mtrk[ievt]);
            current_bar_len = tev.get_numerator() * 96 / (1 << tev.get_denominator());

            if (bar_table.back().num_ticks > 0) {
                dbg("warning, time signature not aligning with bars\n");
                bar_table.emplace_back(bar_table.back().num_ticks +
                        bar_table.back().num_ticks, 0);
            }
        }
    }

    // TODO verify this does what it should. This should make sure that the
    // last bar is always fully extended incase of missing events.
    bar_table.back().num_ticks = current_bar_len;

    // convert to agb events
    assert(as.tracks.size() == 0);

    auto add_wait_event = [](agb_bar& bar, uint32_t len) {
        bar.events.emplace_back(agb_ev::ty::WAIT);
        bar.events.back().wait = len;
    };

    auto get_note_length = [](const midi_track& mtrk, size_t noteon_index,
            uint32_t& len, size_t& noteoff_index) {
        const noteon_message_midi_event& noteon_ev =
            static_cast<const noteon_message_midi_event&>(*mtrk[noteon_index]);
        size_t i = noteon_index;
        uint8_t key = noteon_ev.get_key();
        while (1) {
            i += 1;
            if (i >= mtrk.midi_events.size())
                return false;
            if (typeid(*mtrk[i]) != typeid(noteoff_message_midi_event))
                continue;
            const noteoff_message_midi_event& noteoff_ev =
                static_cast<const noteoff_message_midi_event&>(*mtrk[i]);
            if (noteoff_ev.get_key() == key &&
                    noteoff_ev.get_velocity() == MIDI_NOTE_PARSE_INIT) {
                len = noteoff_ev.ticks - noteon_ev.ticks;
                noteoff_index = i;
                return true;
            }
        }
    };

    for (midi_track& mtrk : mf.midi_tracks) {
        as.tracks.emplace_back();
        agb_track& atrk = as.tracks.back();
        atrk.bars.emplace_back();

        uint32_t current_bar = 0;
        uint32_t tick_counter = 0;
        for (size_t ievt = 0; ievt < mtrk.midi_events.size(); ievt++) {
            const midi_event& ev = *mtrk[ievt];
            // skip all dummy events EXCEPT the very last one
            // so the song does not get truncated
            if (typeid(ev) == typeid(dummy_midi_event) &&
                    ievt + 1 != mtrk.midi_events.size()) {
                continue;
            }
            if (typeid(ev) == typeid(noteoff_message_midi_event)) {
                const noteoff_message_midi_event& noteoff_ev =
                    static_cast<const noteoff_message_midi_event&>(ev);
                if (noteoff_ev.get_velocity() == MIDI_NOTE_PARSE_SHORT)
                    continue;
            }

            uint32_t ticks_to_event = ev.ticks -
                (bar_table[current_bar].start_tick + tick_counter);

            while (ticks_to_event > 0) {
                // if next event isn't in this bar
                if (tick_counter + ticks_to_event >= bar_table[current_bar].num_ticks) {
                    // insert wait until the end of the bar
                    assert(current_bar < bar_table.size());
                    add_wait_event(atrk.bars.back(),
                            bar_table[current_bar].num_ticks - tick_counter);
                    atrk.bars.emplace_back();
                    tick_counter = 0;
                    current_bar += 1;
                    assert(current_bar < bar_table.size());
                    ticks_to_event = ev.ticks -
                        (bar_table[current_bar].start_tick + tick_counter);
                } else {
                    // event is still in this bar so we only have to
                    assert(current_bar < bar_table.size());
                    add_wait_event(atrk.bars.back(), ticks_to_event);
                    tick_counter += ticks_to_event;
                    ticks_to_event = 0;
                }
            }

            if (typeid(ev) == typeid(controller_message_midi_event)) {
                const controller_message_midi_event& cev =
                    static_cast<const controller_message_midi_event&>(ev);
                switch (cev.get_controller()) {
                case MIDI_CC_EX_LOOP:
                    if (cev.get_value() == EX_LOOP_START) {
                        atrk.bars.back().events.emplace_back(agb_ev::ty::LOOP_START);
                    } else if (cev.get_value() == EX_LOOP_END) {
                        atrk.bars.back().events.emplace_back(agb_ev::ty::LOOP_END);
                    }
                    break;
                case MIDI_CC_EX_PRIO:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::PRIO);
                    atrk.bars.back().events.back().prio = cev.get_value();
                    break;
                case MIDI_CC_MSB_VOLUME:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::VOL);
                    atrk.bars.back().events.back().vol = cev.get_value();
                    break;
                case MIDI_CC_MSB_PAN:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::PAN);
                    atrk.bars.back().events.back().pan =
                        static_cast<int8_t>(cev.get_value() - 64);
                    break;
                case MIDI_CC_EX_BENDR:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::BENDR);
                    atrk.bars.back().events.back().bendr = cev.get_value();
                    break;
                case MIDI_CC_EX_LFOS:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::LFOS);
                    atrk.bars.back().events.back().lfos = cev.get_value();
                    break;
                case MIDI_CC_EX_LFODL:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::LFODL);
                    atrk.bars.back().events.back().lfodl = cev.get_value();
                    break;
                case MIDI_CC_MSB_MOD:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::MOD);
                    atrk.bars.back().events.back().mod = cev.get_value();
                    break;
                case MIDI_CC_EX_MODT:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::MODT);
                    atrk.bars.back().events.back().modt = cev.get_value();
                    break;
                case MIDI_CC_EX_TUNE:
                    atrk.bars.back().events.emplace_back(agb_ev::ty::TUNE);
                    atrk.bars.back().events.back().tune =
                        static_cast<int8_t>(cev.get_value() - 64);
                    break;
                default: ;
                }
            } else if (typeid(ev) == typeid(tempo_meta_midi_event)) {
                const tempo_meta_midi_event& tev =
                    static_cast<const tempo_meta_midi_event&>(ev);
                double bpm = tev.get_bpm() / 2.0;
                bpm = clamp(bpm, 0.0, 255.0);
                bpm = std::round(bpm);
                atrk.bars.back().events.emplace_back(agb_ev::ty::TEMPO);
                atrk.bars.back().events.back().tempo = static_cast<uint8_t>(bpm);
            } else if (typeid(ev) == typeid(program_message_midi_event)) {
                const program_message_midi_event& pev =
                    static_cast<const program_message_midi_event&>(ev);
                atrk.bars.back().events.emplace_back(agb_ev::ty::VOICE);
                atrk.bars.back().events.back().voice = pev.get_program();
            } else if (typeid(ev) == typeid(pitchbend_message_midi_event)) {
                const pitchbend_message_midi_event& pev =
                    static_cast<const pitchbend_message_midi_event&>(ev);
                double pitch = pev.get_pitch() / 128.0;
                pitch = std::round(pitch);
                pitch = clamp(pitch, -64.0, +63.0);
                atrk.bars.back().events.emplace_back(agb_ev::ty::BEND);
                atrk.bars.back().events.back().bend = static_cast<int8_t>(pitch);
            } else if (typeid(ev) == typeid(noteon_message_midi_event)) {
                const noteon_message_midi_event& noteon_ev =
                    static_cast<const noteon_message_midi_event&>(ev);
                uint32_t note_len;
                size_t noteoff_index;
                if (get_note_length(mtrk, ievt, note_len, noteoff_index)) {
                    // note end could be found
                    if (note_len > 96) {
                        atrk.bars.back().events.emplace_back(agb_ev::ty::TIE);
                        atrk.bars.back().events.back().tie.key =
                            noteon_ev.get_key();
                        atrk.bars.back().events.back().tie.vel =
                            noteon_ev.get_velocity();

                        noteoff_message_midi_event& noteoff_ev =
                            static_cast<noteoff_message_midi_event&>(*mtrk[noteoff_index]);
                        noteoff_ev.set_velocity(MIDI_NOTE_PARSE_TIE);
                    } else {
                        atrk.bars.back().events.emplace_back(agb_ev::ty::NOTE);
                        atrk.bars.back().events.back().note.len =
                            static_cast<uint8_t>(std::max(note_len, 1u));
                        atrk.bars.back().events.back().note.key =
                            noteon_ev.get_key();
                        atrk.bars.back().events.back().note.vel =
                            noteon_ev.get_velocity();

                        noteoff_message_midi_event& noteoff_ev =
                            static_cast<noteoff_message_midi_event&>(*mtrk[noteoff_index]);
                        noteoff_ev.set_velocity(MIDI_NOTE_PARSE_SHORT);
                    }
                } else {
                    die("ERROR: Couldn't find Note OFF for Note ON\n");
                }
            } else if (typeid(ev) == typeid(noteoff_message_midi_event)) {
                const noteoff_message_midi_event& noteoff_ev =
                    static_cast<const noteoff_message_midi_event&>(ev);
                if (noteoff_ev.get_velocity() == MIDI_NOTE_PARSE_INIT)
                    die("ERROR: Note OFF without initial Note ON\n");
                if (noteoff_ev.get_velocity() == MIDI_NOTE_PARSE_TIE) {
                    atrk.bars.back().events.emplace_back(agb_ev::ty::EOT);
                    atrk.bars.back().events.back().eot.key =
                        noteoff_ev.get_key();
                }
            }
        }
    }
}

/*
 * Note Order:
 * Note's should always be turned off before turning the next ones
 * on. On PC MIDI software that usually doesn't matter but on GBA
 * the engine might allocate a new channel (which might fail) before
 * deallocating one on the same time spot. The GBA engine is stupid
 * and will process the events in that exact order and so we have to
 * do some prevention here. Otherwise unnecessary notes might get
 * dropped.
 */
static void agb_optimize() {
    for (agb_track& atrk : as.tracks) {
        for (agb_bar& abar : atrk.bars) {
            size_t first_ev_at_tick = 0;
            for (size_t ievt = 0; ievt < abar.events.size(); ievt++) {
                if (abar.events[ievt].type == agb_ev::ty::WAIT) {
                    first_ev_at_tick = ievt + 1;
                } else if (abar.events[ievt].type == agb_ev::ty::EOT) {
                    size_t events_to_shift = ievt - first_ev_at_tick;
                    if (events_to_shift == 0) {
                        first_ev_at_tick = ievt + 1;
                        continue;
                    }
                    agb_ev eot_event(std::move(abar.events[ievt]));
                    std::vector<agb_ev> events_removed(
                            std::make_move_iterator(abar.events.begin() +
                                first_ev_at_tick),
                            std::make_move_iterator(abar.events.begin() +
                                static_cast<long>(first_ev_at_tick + events_to_shift)));
                    abar.events[first_ev_at_tick] = std::move(eot_event);
                    std::move(events_removed.begin(), events_removed.end(),
                            abar.events.begin() + static_cast<long>(first_ev_at_tick + 1));

                    first_ev_at_tick += 1;
                }
            }
        }
    }
}

static void agb_comment_line(std::ofstream& ofs, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    if (len == -1)
        die("Please use a vsnprintf that conforms to C99, returned -1");
    va_end(args);

    const size_t width = 56;
    const size_t num_stars = width - std::min(static_cast<size_t>(len), width);
    const size_t left_stars = num_stars / 2;
    const size_t right_stars = num_stars - left_stars;

    char buf_final[512];
    char *buf_iter = buf_final;
    *buf_iter++ = '@';
    for (size_t i = 0; i < left_stars; i++)
        *buf_iter++ = '*';
    *buf_iter++ = ' ';
    memcpy(buf_iter, buf, static_cast<size_t>(len));
    buf_iter += len;
    *buf_iter++ = ' ';
    for (size_t i = 0; i < right_stars; i++)
        *buf_iter++ = '*';
    *buf_iter++ = '@';
    *buf_iter++ = '\0';
    ofs << buf_final << std::endl;
}

static void agb_out(std::ofstream& ofs, const char *msg, ...) {
    char buf[256];
    va_list args;
    va_start(args, msg);
    vsnprintf(buf, sizeof(buf), msg, args);
    va_end(args);
    ofs << buf;
}

struct agb_state {
    agb_state()
        : cmd_state(cmd::INVALID), note_key(0xFF), note_vel(0xFF), note_len(0),
        may_repeat(false) {}

    void reset() {
        cmd_state = cmd::INVALID;
        note_key = 0xFF;
        note_vel = 0xFF;
        note_len = 0;
        // TODO verify
        // reset() only get's called in places where a set to
        // may_repeat = false might not be needed
        may_repeat = false;
    }

    enum class cmd {
        VOICE, VOL, PAN, BEND, BENDR, LFOS, LFODL, MOD, MODT, TUNE,
        XCMD, EOT, TIE, NOTE, INVALID
    } cmd_state;
    uint8_t note_key, note_vel, note_len;
    bool may_repeat;
};

static void write_event(std::ofstream& ofs, agb_state& state, const agb_ev& ev, size_t itrk) {
    static uint8_t len_table[97] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 24, 24, 24, 28, 28, 30, 30,
        32, 32, 32, 32, 36, 36, 36, 36, 40, 40, 42, 42, 44, 44, 44,
        44, 48, 48, 48, 48, 52, 52, 54, 54, 56, 56, 56, 56, 60, 60,
        60, 60, 64, 64, 66, 66, 68, 68, 68, 68, 72, 72, 72, 72, 76,
        76, 78, 78, 80, 80, 80, 80, 84, 84, 84, 84, 88, 88, 90, 90,
        92, 92, 92, 92, 96
    };
    static const char *note_names[128] = {
        "CnM2", "CsM2", "DnM2", "DsM2", "EnM2", "FnM2", "FsM2", "GnM2", "GsM2", "AnM2", "AsM2", "BnM2",
        "CnM1", "CsM1", "DnM1", "DsM1", "EnM1", "FnM1", "FsM1", "GnM1", "GsM1", "AnM1", "AsM1", "BnM1",
        "Cn0", "Cs0", "Dn0", "Ds0", "En0", "Fn0", "Fs0", "Gn0", "Gs0", "An0", "As0", "Bn0",
        "Cn1", "Cs1", "Dn1", "Ds1", "En1", "Fn1", "Fs1", "Gn1", "Gs1", "An1", "As1", "Bn1",
        "Cn2", "Cs2", "Dn2", "Ds2", "En2", "Fn2", "Fs2", "Gn2", "Gs2", "An2", "As2", "Bn2",
        "Cn3", "Cs3", "Dn3", "Ds3", "En3", "Fn3", "Fs3", "Gn3", "Gs3", "An3", "As3", "Bn3",
        "Cn4", "Cs4", "Dn4", "Ds4", "En4", "Fn4", "Fs4", "Gn4", "Gs4", "An4", "As4", "Bn4",
        "Cn5", "Cs5", "Dn5", "Ds5", "En5", "Fn5", "Fs5", "Gn5", "Gs5", "An5", "As5", "Bn5",
        "Cn6", "Cs6", "Dn6", "Ds6", "En6", "Fn6", "Fs6", "Gn6", "Gs6", "An6", "As6", "Bn6",
        "Cn7", "Cs7", "Dn7", "Ds7", "En7", "Fn7", "Fs7", "Gn7", "Gs7", "An7", "As7", "Bn7",
        "Cn8", "Cs8", "Dn8", "Ds8", "En8", "Fn8", "Fs8", "Gn8"
    };
    static const char *gate_names[3] = {
        "gtp1", "gtp2", "gtp3"
    };
    switch (ev.type) {
    case agb_ev::ty::WAIT:
        {
            uint32_t len = ev.wait;
            assert(len > 0);
            while (len > 96) {
                agb_out(ofs, "        .byte   W96\n");
                len -= 96;
            }
            uint32_t wout = len_table[len];
            agb_out(ofs, "        .byte   W%02u\n", wout);
            len -= wout;
            if (len > 0)
                agb_out(ofs, "        .byte   W%02u\n", len);
            state.may_repeat = true;
        }
        break;
    case agb_ev::ty::LOOP_START:
        agb_out(ofs, "%s_%zu_LOOP:\n", arg_sym.c_str(), itrk);
        state.reset();
        break;
    case agb_ev::ty::LOOP_END:
        agb_out(ofs, "        .byte   GOTO\n         .word  %s_%zu_LOOP\n",
                arg_sym.c_str(), itrk);
        break;
    case agb_ev::ty::PRIO:
        agb_out(ofs, "        .byte   PRIO  , %d\n", ev.prio);
        break;
    case agb_ev::ty::TEMPO:
        agb_out(ofs, "        .byte   TEMPO , %d/2\n", ev.tempo * 2);
        break;
    case agb_ev::ty::KEYSH:
        agb_out(ofs, "        .byte   KEYSH , %s_key%+d\n",
                arg_sym.c_str(), ev.keysh);
        break;
    case agb_ev::ty::VOICE:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::VOICE) {
            agb_out(ofs, "        .byte                   %d\n", ev.voice);
        } else {
            agb_out(ofs, "        .byte           VOICE , %d\n", ev.voice);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::VOICE;
        }
        break;
    case agb_ev::ty::VOL:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::VOL) {
            agb_out(ofs, "        .byte                   %d\n", ev.vol);
        } else {
            agb_out(ofs, "        .byte           VOL   , %d\n", ev.vol);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::VOL;
        }
        break;
    case agb_ev::ty::PAN:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::PAN) {
            agb_out(ofs, "        .byte                   c_v%+d\n", ev.pan);
        } else {
            agb_out(ofs, "        .byte           PAN   , c_v%+d\n", ev.pan);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::PAN;
        }
        break;
    case agb_ev::ty::BEND:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::BEND) {
            agb_out(ofs, "        .byte                   c_v%+d\n", ev.bend);
        } else {
            agb_out(ofs, "        .byte           BEND  , c_v%+d\n", ev.bend);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::BEND;
        }
        break;
    case agb_ev::ty::BENDR:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::BENDR) {
            agb_out(ofs, "        .byte                   %d\n", ev.bendr);
        } else {
            agb_out(ofs, "        .byte           BENDR , %d\n", ev.bendr);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::BENDR;
        }
        break;
    case agb_ev::ty::LFOS:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::LFOS) {
            agb_out(ofs, "        .byte                   %d\n", ev.lfos);
        } else {
            agb_out(ofs, "        .byte           LFOS  , %d\n", ev.lfos);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::LFOS;
        }
        break;
    case agb_ev::ty::LFODL:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::LFODL) {
            agb_out(ofs, "        .byte                   %d\n", ev.lfodl);
        } else {
            agb_out(ofs, "        .byte           LFODL , %d\n", ev.lfodl);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::LFODL;
        }
        break;
    case agb_ev::ty::MOD:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::MOD) {
            agb_out(ofs, "        .byte                   %d\n", ev.mod);
        } else {
            agb_out(ofs, "        .byte           MOD   , %d\n", ev.mod);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::MOD;
        }
        break;
    case agb_ev::ty::MODT:
        {
            const char *modt;
            if (ev.modt == 1)
                modt = "mod_tre";
            else if (ev.modt == 2)
                modt = "mod_pan";
            else
                modt = "mod_vib";
            if (state.may_repeat && state.cmd_state == agb_state::cmd::MODT) {
                agb_out(ofs, "        .byte                   %s\n", modt);
            } else {
                agb_out(ofs, "        .byte           MODT  , %s\n", modt);
                state.may_repeat = true;
                state.cmd_state = agb_state::cmd::MODT;
            }
        }
        break;
    case agb_ev::ty::TUNE:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::TUNE) {
            agb_out(ofs, "        .byte                   c_v%+d\n", ev.tune);
        } else {
            agb_out(ofs, "        .byte           TUNE  , c_v%+d\n", ev.tune);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::TUNE;
        }
        break;
    case agb_ev::ty::XCMD:
        if (state.may_repeat && state.cmd_state == agb_state::cmd::XCMD) {
            agb_out(ofs, "        .byte                   0x%02X  , %d\n",
                    ev.xcmd.type, ev.xcmd.par);
        } else {
            agb_out(ofs, "        .byte           XCMD  , 0x%02X  , %d\n",
                    ev.xcmd.type, ev.xcmd.par);
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::XCMD;
        }
        break;
    case agb_ev::ty::EOT:
        assert(ev.eot.key < 128);
        if (state.may_repeat && state.cmd_state == agb_state::cmd::EOT) {
            agb_out(ofs, "        .byte                   %s\n",
                    note_names[ev.eot.key]);
            state.note_key = ev.eot.key;
        } else {
            if (state.note_key == ev.eot.key) {
                agb_out(ofs, "        .byte           EOT\n");
            } else {
                agb_out(ofs, "        .byte           EOT   , %s\n",
                        note_names[ev.eot.key]);
                state.note_key = ev.eot.key;
            }
            state.may_repeat = true;
            state.cmd_state = agb_state::cmd::EOT;
        }
        break;
    case agb_ev::ty::TIE:
        assert(ev.tie.key < 128);
        assert(ev.tie.vel < 128);
        if (state.may_repeat && state.cmd_state == agb_state::cmd::TIE) {
            if (state.note_vel == ev.tie.vel) {
                agb_out(ofs, "        .byte                   %s\n",
                        note_names[ev.tie.key]);
                state.note_key = ev.tie.key;
                state.may_repeat = false;
            } else {
                agb_out(ofs, "        .byte                   %s , v%03d\n",
                        note_names[ev.tie.key], ev.tie.vel);
                state.note_key = ev.tie.key;
                state.note_vel = ev.tie.vel;
            }
        } else {
            if (state.note_key == ev.tie.key &&
                    state.note_vel == ev.tie.vel) {
                agb_out(ofs, "        .byte           TIE\n");
                state.may_repeat = false;
            } else if (state.note_vel == ev.tie.vel) {
                agb_out(ofs, "        .byte           TIE   , %s\n",
                        note_names[ev.tie.key]);
                state.note_key = ev.tie.key;
                state.may_repeat = false;
            } else {
                agb_out(ofs, "        .byte           TIE   , %s , v%03d\n",
                        note_names[ev.tie.key], ev.tie.vel);
                state.note_key = ev.tie.key;
                state.note_vel = ev.tie.vel;
                state.may_repeat = false;
            }
            state.cmd_state = agb_state::cmd::TIE;
        }
        break;
    case agb_ev::ty::NOTE:
        assert(state.note_len == len_table[state.note_len]);
        assert(ev.note.len > 0 && ev.note.len <= 96);
        assert(ev.note.len - len_table[ev.note.len] <= 3);
        assert(ev.note.key < 128);
        assert(ev.note.vel < 128);
        if (state.may_repeat && state.cmd_state == agb_state::cmd::NOTE) {
            if (state.note_vel == ev.note.vel && state.note_len == ev.note.len) {
                agb_out(ofs, "        .byte                   %s\n",
                        note_names[ev.note.key]);
                state.note_key = ev.note.key;
                state.may_repeat = false;
            } else if (state.note_len == ev.note.len) {
                agb_out(ofs, "        .byte                   %s , v%03d\n",
                        note_names[ev.note.key], ev.note.vel);
                state.note_key = ev.note.key;
                state.note_vel = ev.note.vel;
                state.may_repeat = false;
            } else if (state.note_len == len_table[ev.note.len]) {
                int gi = ev.note.len - len_table[ev.note.len] - 1;
                assert(gi >= 0 && gi <= 2);
                agb_out(ofs, "        .byte                   %s , v%03d , %s\n",
                        note_names[ev.note.key], ev.note.vel, gate_names[gi]);
                state.note_key = ev.note.key;
                state.note_vel = ev.note.vel;
                state.note_len = len_table[ev.note.len];
            } else if (ev.note.len == len_table[ev.note.len] &&
                    state.note_key == ev.note.key &&
                    state.note_vel == ev.note.vel) {
                agb_out(ofs, "        .byte           N%02d\n", ev.note.len);
                state.note_len = ev.note.len;
                state.may_repeat = false;
            } else if (ev.note.len == len_table[ev.note.len] &&
                    state.note_vel == ev.note.vel) {
                agb_out(ofs, "        .byte           N%02d   , %s\n",
                        ev.note.len, note_names[ev.note.key]);
                state.note_len = ev.note.len;
                state.note_key = ev.note.key;
                state.may_repeat = false;
            } else if (ev.note.len == len_table[ev.note.len]) {
                agb_out(ofs, "        .byte           N%02d   , %s , v%03d\n",
                        len_table[ev.note.len], note_names[ev.note.key], ev.note.vel);
                state.note_key = ev.note.key;
                state.note_vel = ev.note.vel;
                state.note_len = ev.note.len;
                state.may_repeat = false;
            } else {
                int gi = ev.note.len - len_table[ev.note.len] - 1;
                assert(gi >= 0 && gi <= 2);
                agb_out(ofs, "        .byte           N%02d   , %s , v%03d , %s\n",
                        len_table[ev.note.len], note_names[ev.note.key],
                        ev.note.vel, gate_names[gi]);
                state.note_key = ev.note.key;
                state.note_vel = ev.note.vel;
                state.note_len = len_table[ev.note.len];
            }
        } else {
            int gate_time = ev.note.len - len_table[ev.note.len];
            if (gate_time == 0 && state.note_key == ev.note.key &&
                    state.note_vel == ev.note.vel) {
                agb_out(ofs, "        .byte           N%02d\n",
                        ev.note.len);
                state.note_len = ev.note.len;
                state.may_repeat = false;
            } else if (gate_time == 0 && state.note_vel == ev.note.vel) {
                agb_out(ofs, "        .byte           N%02d   , %s\n",
                        ev.note.len, note_names[ev.note.key]);
                state.note_len = ev.note.len;
                state.note_key = ev.note.key;
                state.may_repeat = false;
            } else if (gate_time == 0) {
                agb_out(ofs, "        .byte           N%02d   , %s , v%03d\n",
                        ev.note.len, note_names[ev.note.key], ev.note.vel);
                state.note_len = ev.note.len;
                state.note_key = ev.note.key;
                state.note_vel = ev.note.vel;
                state.may_repeat = false;
            } else {
                int gi = ev.note.len - len_table[ev.note.len] - 1;
                agb_out(ofs, "        .byte           N%02d   , %s , v%03d , %s\n",
                        len_table[ev.note.len], note_names[ev.note.key],
                        ev.note.vel, gate_names[gi]);
                state.note_len = len_table[ev.note.len];
                state.note_key = ev.note.key;
                state.note_vel = ev.note.vel;
                state.may_repeat = true;
            }
            state.cmd_state = agb_state::cmd::NOTE;
        }
        break;
    }
}

struct bar_dest {
    bar_dest(size_t track, size_t bar)
        : track(track), bar(bar) {}
    size_t track, bar;
};

static void write_agb() {
    using namespace cppmidi;

    std::ofstream fout(arg_output_file, std::ios::out);
    if (!fout.is_open())
        die("Unable to open output file: %s\n", strerror(errno));

    // pair: first = track, second bar index
    std::unordered_map<
        std::reference_wrapper<agb_bar>,
        bar_dest,
        agb_bar_ref_hasher,
        agb_bar_ref_hasher>
            compression_table;

    // fill compression table
    for (size_t itrk = 0; itrk < as.tracks.size(); itrk++) {
        agb_track& atrk = as.tracks[itrk];
        for (size_t ibar = 0; ibar < atrk.bars.size(); ibar++) {
            agb_bar& abar = atrk.bars[ibar];
            if (abar.events.size() == 0) {
                // this should only happen for events in the beginning
                // of the very last bar in the track
                assert(ibar + 1 == atrk.bars.size());
                continue;
            }
            if (abar.size() <= 5)
                continue;
            for (size_t ievt = 0; ievt < abar.events.size(); ievt++) {
                // if one event contains a loop end, don't make it callable
                // otherwise other tracks might call the loop end which will
                // make things go out of order
                if (abar.events[ievt].type == agb_ev::ty::LOOP_END)
                    goto outer_continue;
                if (abar.events[ievt].type == agb_ev::ty::LOOP_START)
                    goto outer_continue;
            }
            {
                auto result = compression_table.insert(
                        std::pair<std::reference_wrapper<agb_bar>, bar_dest>(
                            abar, bar_dest(itrk, ibar)));
                if (!result.second) {
                    // if bar already is inserted, trigger its reference count
                    as.tracks[result.first->second.track].bars[result.first->second.bar].is_referenced = true;
                    // mark reference origin
                    abar.does_reference = true;
                }
            }
outer_continue:
            ;
        }
    }

    // write header
    agb_out(fout, "        .include \"MPlayDef.s\"\n\n");
    agb_out(fout, "        .equ    %s_grp, %s\n", arg_sym.c_str(), arg_vgr.c_str());
    agb_out(fout, "        .equ    %s_pri, %d\n", arg_sym.c_str(), arg_pri);
    if (arg_rev > 0) {
        agb_out(fout, "        .equ    %s_rev, %d+reverb_set\n",
                arg_sym.c_str(), arg_rev);
    } else {
        agb_out(fout, "        .equ    %s_rev, 0\n",
                arg_sym.c_str());
    }
    agb_out(fout, "        .equ    %s_key, 0\n\n", arg_sym.c_str());
    agb_out(fout, "        .section .rodata\n");
    agb_out(fout, "        .global %s\n", arg_sym.c_str());
    agb_out(fout, "        .align  2\n\n");

    for (size_t itrk = 0; itrk < as.tracks.size(); itrk++) {
        assert(as.tracks.size() == mf.midi_tracks.size());
        const midi_track& mtrk = mf[itrk];
        agb_track& atrk = as.tracks[itrk];

        agb_state state;

        agb_comment_line(fout, "Track %zu (Midi-Chn.%d)", itrk,
                trk_get_channel_num(mtrk));

        agb_out(fout, "\n%s_%zu:\n", arg_sym.c_str(), itrk);
        agb_out(fout, "        .byte   KEYSH , %s_key+0\n", arg_sym.c_str());

        for (size_t ibar = 0; ibar < atrk.bars.size(); ibar++) {
            agb_bar& abar = atrk.bars[ibar];
            // assert the bar does does not reference and is referenced at
            // the same time
            assert(!abar.is_referenced || !abar.does_reference);
            agb_out(fout, "@ %03zu   ----------------------------------------\n", ibar);
            if (abar.is_referenced) {
                // TODO This sometimes adds unneccessary labels and PENDs below
                // In some cases the compressor will decide to not call this section
                // in the end due to smaller space usage without a call. Probably a bit
                // more complicated to fix.
                agb_out(fout, "%s_%zu_%zu:\n", arg_sym.c_str(), itrk, ibar);
                state.reset();
            }

            if (!abar.does_reference) {
                for (size_t ievt = 0; ievt < abar.events.size(); ievt++) {
                    write_event(fout, state, abar.events[ievt], itrk);
                }
            } else {
                std::reference_wrapper<agb_bar> key(abar);
                auto result = compression_table.find(key);
                // if this bar references another it must be found
                assert(result != compression_table.end());

                size_t track_refed = result->second.track;
                size_t bar_refed = result->second.bar;

                agb_out(fout, "        .byte   PATT\n");
                agb_out(fout, "         .word  %s_%zu_%zu\n", arg_sym.c_str(),
                        track_refed, bar_refed);
                state.reset();
            }

            if (abar.is_referenced)
                agb_out(fout, "        .byte   PEND\n");
        }
        agb_out(fout, "        .byte   FINE\n\n");
    }
    agb_out(fout, "\n");
    agb_comment_line(fout, "End of Song");
    agb_out(fout, "\n        .align  2\n");
    agb_out(fout, "%s:\n", arg_sym.c_str());
    agb_out(fout, "        .byte   %-23zu @ Num Tracks\n", as.tracks.size());
    agb_out(fout, "        .byte   %-23zu @ Unknown\n", 0);
    agb_out(fout, "        .byte   %-23s @ Priority\n",
            (arg_sym + "_pri").c_str());
    agb_out(fout, "        .byte   %-23s @ Reverb\n\n",
            (arg_sym + "_rev").c_str());
    agb_out(fout, "        .word   %-23s\n\n",
            (arg_sym + "_grp").c_str());

    for (size_t i = 0; i < as.tracks.size(); i++) {
        agb_out(fout, "        .word   %s_%zu\n", arg_sym.c_str(), i);
    }

    agb_out(fout, "\n        .end\n");

    if (fout.bad())
        die("std::ofstream::bad\n");
    if (fout.fail())
        die("std::ofstream::fail\n");
    fout.close();
}

static void dbg(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    if (arg_debug_output)
        vfprintf(stderr, msg, args);
    va_end(args);
}

static void die(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    exit(1);
}

static void err(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}
