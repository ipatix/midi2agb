#include <string>
#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cmath>
#include <cassert>

#include "cppmidi/cppmidi.h"

static void die(const char *msg, ...) {
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    exit(1);
}

static void usage() {
    fprintf(stderr, "Usage:\n$ midi2agb <input.mid> [<output.mid>] [options]\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "-s <sym> | symbol name for song header (default: file name)\n");
    fprintf(stderr, "-v <mvl> | master volume 0..128 (default: 128)\n");
    fprintf(stderr, "-g <vgr> | voicegroup symbol name (default: voicegroup000)\n");
    fprintf(stderr, "-p <pri> | song priority 0..127 (default: 0)\n");
    fprintf(stderr, "-r <rev> | song reverb 0..127 (default: 0)\n");
    fprintf(stderr, "-n       | apply natural volume scale\n");
    fprintf(stderr, "-e       | exact note gate time (increases size by a few bytes)\n");
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

static std::string arg_sym;
static uint8_t arg_mvl = 128;
static std::string arg_vgr;
static uint8_t arg_pri = 0;
static uint8_t arg_rev = 0;
static bool arg_natural = false;
static bool arg_exact = false;
static float arg_mod_scale = 1.0f;

// conditional global event options

static uint8_t arg_modt = 0;
static bool arg_modt_global = false;

static std::string arg_input_file;
static bool arg_input_file_read = false;
static std::string arg_output_file;
static bool arg_output_file_read = false;

static cppmidi::midi_file mf;

static void midi_read_infile_arguments();

static void midi_remove_empty_tracks();
static void midi_apply_filters();
static void midi_apply_loop_and_state_reset();
static void midi_remove_redundant_events();

static void write_agb();

int main(int argc, char *argv[]) {
    if (argc == 1)
        usage();
    try {
        // argument parsing
        for (int i = 1; i < argc; i++) {
            std::string st(argv[i]);
            if (!st.compare("-l")) {
                if (++i >= argc)
                    die("-l: missing parameter\n");
                arg_sym = argv[i];
                fix_str(arg_sym);
            } else if (!st.compare("-v")) {
                if (++i >= argc)
                    die("-v: missing parameter\n");
                int mvl = stoi(std::string(argv[i]));
                if (mvl < 0 || mvl > 127)
                    die("-v: parameter %d out of range\n", mvl);
                arg_mvl = static_cast<uint8_t>(mvl);
            } else if (!st.compare("-g")) {
                if (++i >= argc)
                    die("-l missing parameter\n");
                arg_vgr = argv[i];
                fix_str(arg_vgr);
            } else if (!st.compare("-p")) {
                if (++i >= argc)
                    die("-p: missing parameter\n");
                int prio = stoi(std::string(argv[i]));
                if (prio < 0 || prio > 127)
                    die("-p: parameter %d out of range\n", prio);
                arg_pri = static_cast<uint8_t>(prio);
            } else if (!st.compare("-r")) {
                if (++i >= argc)
                    die("-r: missing parameter\n");
                int rev = stoi(std::string(argv[i]));
                if (rev < 0 || rev > 127)
                    die("-r: parameter %d out of range\n", rev);
                arg_rev = static_cast<uint8_t>(rev);
            } else if (!st.compare("-n")) {
                arg_natural = true;
            } else if (!st.compare("-e")) {
                arg_exact = true;
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
            if (arg_input_file.size() >= 4) {
                std::string ext(arg_input_file.end() - 4, arg_input_file.end());

                for (char& c : ext)
                    c = static_cast<char>(std::tolower(c));
                if (!ext.compare(".mid")) {
                    arg_output_file = arg_input_file.substr(0, arg_input_file.size() - 4) + ".s";
                } else {
                    arg_output_file = arg_input_file + ".s";
                }
            } else {
                arg_output_file = arg_input_file + ".s";
            }
            arg_output_file_read = true;
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

        write_agb();
    } catch (const cppmidi::xcept& ex) {
        fprintf(stderr, "cppmidi lib error:\n%s\n", ex.what());
        return 1;
    } catch (const std::exception& ex) {
        fprintf(stderr, "std lib error:\n%s\n", ex.what());
        return 1;
    }
}

static const uint8_t MIDI_CC_EX_BENDR = 20;
static const uint8_t MIDI_CC_EX_MODT = 22;
static const uint8_t MIDI_CC_EX_TUNE = 23;
static const uint8_t MIDI_CC_EX_LOOP = 30;

static const uint8_t EX_LOOP_START = 100;
static const uint8_t EX_LOOP_END = 101;

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

static void midi_read_infile_arguments() {
    using namespace cppmidi;
    /*
     * Special Events (use Marker/Text/Cuepoint):
     * - "[": loop start for all tracks
     * - "]": loop end for all tracks
     * - "modt=%d": set's modulation type at position
     * - "tune=%d": set's tuning (+-1 key, range -64 to +63)
     * - "modscale_global=%f": scales modulation by factor %f
     * - "modt_global=%d": set's modulation type for whole song
     */
    bool found_start = false, found_end = false;
    uint32_t loop_start = 0, loop_end = 0;

    uint8_t lsb_rpn = 0, msb_rpn = 0;

    // parse meta events
    for (midi_track& mtrk : mf.midi_tracks) {
        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            const midi_event& ev = *mtrk[i];
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
                } else if (cev.get_controller() == MIDI_CC_MSB_DATA_ENTRY) {
                    // found a bend range command
                    mtrk[i] = std::make_unique<controller_message_midi_event>(
                            cev.ticks, cev.channel(),
                            MIDI_CC_EX_BENDR, cev.get_value());
                }
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
            } else if (!ev_text.substr(0, 5).compare("modt=")) {
                int modt = std::stoi(ev_text.substr(5, std::string::npos));
                modt = clamp(modt, 0, 2);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[i] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_MODT,
                            static_cast<uint8_t>(modt));
                }
            } else if (!ev_text.substr(0, 5).compare("tune=")) {
                int tune = std::stoi(ev_text.substr(5, std::string::npos));
                tune = clamp(tune, -64, 63);
                int channel = trk_get_channel_num(mtrk);
                if (channel > 0) {
                    mtrk[i] = std::make_unique<controller_message_midi_event>(
                            ev.ticks, channel, MIDI_CC_EX_TUNE,
                            static_cast<uint8_t>(tune));
                }
            } else if (!ev_text.substr(0, 16).compare("modscale_global=")) {
                arg_mod_scale = std::stof(ev_text.substr(16, std::string::npos));
                arg_mod_scale = clamp(arg_mod_scale, 0.0f, 16.0f);
                // the actual scale get's applied in a seperate filter
            } else if (!ev_text.substr(0, 12).compare("modt_global=")) {
                // the flag below will get read by an additional filter
                // which adds global events
                arg_modt_global = true;
                int modt = std::stoi(ev_text.substr(12, std::string::npos));
                modt = clamp(modt, 0, 2);
                arg_modt = static_cast<uint8_t>(modt);
            }
        }
    } // end meta event track loop

    // insert loop and global events
    for (midi_track& mtrk : mf.midi_tracks) {
        int chn = trk_get_channel_num(mtrk);
        // chn mustn't be negative because track with no message events
        // have been sorted out previously
        assert(chn >= 0);
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
                        loop_start, static_cast<uint8_t>(chn),
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
        // add more inital event insertions
    }
}

static void midi_remove_empty_tracks() {
    using namespace cppmidi;
    midi_track tempo_track;

    // seperate tempo events
    for (midi_track& mtrk : mf.midi_tracks) {
        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            if (typeid(*mtrk[i]) != typeid(tempo_meta_midi_event))
                continue;
            tempo_track.midi_events.emplace_back(std::move(mtrk[i]));
            mtrk.midi_events.erase(mtrk.midi_events.begin() +
                    static_cast<long>(i--));
        }
    }

    tempo_track.sort_events();

    // remove tracks without notes
    for (size_t trk = 0; trk < mf.midi_tracks.size(); trk++) {
        // set false if a note event was found
        bool del = true;
        for (const std::unique_ptr<midi_event>& ev : mf[trk].midi_events) {
            if (typeid(*ev) == typeid(noteon_message_midi_event)) {
                del = false;
                break;
            }
        }
        if (del) {
            mf.midi_tracks.erase(mf.midi_tracks.begin() +
                    static_cast<long>(trk--));
        }
    }

    if (mf.midi_tracks.size() == 0)
        return;

    // all empty tracks deleted, now reinsert tempo events to first track
    for (std::unique_ptr<midi_event>& tev : tempo_track.midi_events) {
        // locate position for insertion
        auto position = std::lower_bound(
                mf[0].midi_events.begin(),
                mf[0].midi_events.end(),
                tev, ev_tick_cmp);
        if (position == mf[0].midi_events.end()) {
            mf[0].midi_events.emplace_back(std::move(tev));
        } else {
            mf[0].midi_events.insert(position, std::move(tev));
        }
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
 * Note Order:
 * Note's should always be turned off before turning the next ones
 * on. On PC MIDI software that usually doesn't matter but on GBA
 * the engine might allocate a new channel (which might fail) before
 * deallocating one on the same time spot. The GBA engine is stupid
 * and will process the events in that exact order and so we have to
 * do some prevention here. Otherwise unnecessary notes might get
 * dropped.
 */
static void midi_apply_filters() {
    using namespace cppmidi;

    auto vol_scale = [&](uint8_t vol, uint8_t expr) {
        float x = vol * expr * arg_mvl;
        if (arg_natural) {
            x /= 127.0f * 127.0f * 128.0f;
            x = powf(x, 10.0f / 6.0f);
            x *= 127.0f;
            x = std::round(x);
        } else {
            x /= 127.0f;
            x = std::round(x);
        }
        return static_cast<uint8_t>(clamp(static_cast<int>(x), 0, 127));
    };

    auto vel_scale = [&](uint8_t vel) {
        float x = vel;
        if (arg_natural) {
            x /= 127.0f;
            x = powf(x, 10.0f / 6.0f);
            x *= 127.0f;
            x = std::round(x);
        }
        // clamp to lower 1 because midi velocity 0 is a note off
        return static_cast<uint8_t>(clamp(static_cast<int>(x), 0, 127));
    };

    for (midi_track& mtrk : mf.midi_tracks) {
        uint8_t volume = 100;
        uint8_t expression = 127;

        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            midi_event& ev = *mtrk[i];
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
                }
            } else if (typeid(ev) == typeid(noteon_message_midi_event)) {
                size_t target_event = 0;
                if (find_next_event_at_tick_index<noteoff_message_midi_event>(
                            mtrk, i, target_event)) {
                    std::swap(mtrk[i], mtrk[target_event]);
                } else {
                    noteon_message_midi_event& note_ev =
                        static_cast<noteon_message_midi_event&>(ev);
                    note_ev.set_velocity(vel_scale(note_ev.get_velocity()));
                }
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
        // FIXME add prio, and pseudo echo for completeness
        // omitted for now because nobody would be using it

        bool loop_start_passed = false;

        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            midi_event& ev = *mtrk[i];
            if (typeid(ev) == typeid(tempo_meta_midi_event)) {
                tempo_meta_midi_event& tev = static_cast<tempo_meta_midi_event&>(ev);
                if (!loop_start_passed)
                    tempo = tev.get_us_per_beat();
            } else if (typeid(ev) == typeid(program_message_midi_event)) {
                program_message_midi_event& pev = static_cast<program_message_midi_event&>(ev);
                if (!loop_start_passed)
                    voice = pev.get_program();
            } else if (typeid(ev) == typeid(pitchbend_message_midi_event)) {
                pitchbend_message_midi_event& pev = static_cast<pitchbend_message_midi_event&>(ev);
                if (!loop_start_passed)
                    bend = pev.get_pitch();
            } else if (typeid(ev) == typeid(controller_message_midi_event)) {
                controller_message_midi_event& cev = static_cast<controller_message_midi_event&>(ev);
                uint8_t ctrl = cev.get_controller();
                switch (ctrl) {
                case MIDI_CC_MSB_VOLUME:
                    if (!loop_start_passed)
                        vol = cev.get_value();
                    break;
                case MIDI_CC_MSB_PAN:
                    if (!loop_start_passed)
                        pan = cev.get_value();
                    break;
                case MIDI_CC_EX_BENDR:
                    if (!loop_start_passed)
                        bendr = cev.get_value();
                    break;
                case MIDI_CC_MSB_MOD:
                    if (!loop_start_passed)
                        mod = cev.get_value();
                    break;
                case MIDI_CC_EX_MODT:
                    if (!loop_start_passed)
                        modt = cev.get_value();
                    break;
                case MIDI_CC_EX_TUNE:
                    if (!loop_start_passed)
                        tune = cev.get_value();
                    break;
                case MIDI_CC_EX_LOOP:
                    if (cev.get_value() == EX_LOOP_START) {
                        // loop start
                        loop_start_passed = true;
                    } else if (cev.get_value() == EX_LOOP_END && loop_start_passed) {
                        // loop end
                        // insert events for start state
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
                        mtrk.midi_events.insert(mtrk.midi_events.begin() +
                                static_cast<long>(i),
                                std::make_move_iterator(ptrs.begin()),
                                std::make_move_iterator(ptrs.end()));
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
        uint8_t tempo = 120 / 2;
        bool tempo_init = false;
        uint8_t voice = 0;
        bool voice_init = false;
        uint8_t vol = 100;
        bool vol_init = false;
        uint8_t pan = 0x40;
        bool pan_init = false;
        uint8_t bend = 0x40;
        bool bend_init = false;
        uint8_t bendr = 2;
        bool bendr_init = false;
        uint8_t mod = 0;
        bool mod_init = false;
        uint8_t modt = 0;
        bool modt_init = false;
        uint8_t tune = 0x40;
        bool tune_init = false;

        size_t dummy;

        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            midi_event& ev = *mtrk.midi_events[i];
            if (typeid(ev) == typeid(tempo_meta_midi_event)) {
                tempo_meta_midi_event& tev = static_cast<tempo_meta_midi_event&>(ev);
                double halved_bpm = std::round(tev.get_bpm() * 0.5);
                halved_bpm = clamp(halved_bpm, 0.0, 255.0);
                uint8_t utempo = static_cast<uint8_t>(halved_bpm);
                if ((tempo_init && tempo == utempo) ||
                        find_next_event_at_tick_index<tempo_meta_midi_event>(
                            mtrk, i, dummy)) {
                    mtrk.midi_events.erase(mtrk.midi_events.begin() +
                            static_cast<long>(i--));
                } else {
                    tempo = utempo;
                }
            } else if (typeid(ev) == typeid(program_message_midi_event)) {
                program_message_midi_event& pev = static_cast<program_message_midi_event&>(ev);
                if ((voice_init && pev.get_program() == voice) ||
                        find_next_event_at_tick_index<program_message_midi_event>(
                            mtrk, i, dummy)) {
                    mtrk.midi_events.erase(mtrk.midi_events.begin() +
                            static_cast<long>(i--));
                } else {
                    voice = pev.get_program();
                }
            } else if (typeid(ev) == typeid(pitchbend_message_midi_event)) {
                pitchbend_message_midi_event& pev = static_cast<pitchbend_message_midi_event&>(ev);
                uint8_t ubend = static_cast<uint8_t>((pev.get_pitch() + 64) / 128);
                if ((bend_init && bend == ubend) ||
                        find_next_event_at_tick_index<pitchbend_message_midi_event>(
                            mtrk, i, dummy)) {
                    mtrk.midi_events.erase(mtrk.midi_events.begin() +
                            static_cast<long>(i--));
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
                            MIDI_CC_MSB_VOLUME>(mtrk, i, dummy)) {
                        mtrk.midi_events.erase(mtrk.midi_events.begin() +
                                static_cast<long>(i--));
                    } else {
                        vol = cev.get_value();
                    }
                    break;
                case MIDI_CC_MSB_PAN:
                    if ((pan_init && pan == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_MSB_PAN>(mtrk, i, dummy)) {
                        mtrk.midi_events.erase(mtrk.midi_events.begin() +
                                static_cast<long>(i--));
                    } else {
                        pan = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_BENDR:
                    if ((bendr_init && bendr == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_BENDR>(mtrk, i, dummy)) {
                        mtrk.midi_events.erase(mtrk.midi_events.begin() +
                                static_cast<long>(i--));
                    } else {
                        bendr = cev.get_value();
                    }
                    break;
                case MIDI_CC_MSB_MOD:
                    if ((mod_init && mod == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_MSB_MOD>(mtrk, i, dummy)) {
                        mtrk.midi_events.erase(mtrk.midi_events.begin() +
                                static_cast<long>(i--));
                    } else {
                        mod = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_MODT:
                    if ((modt_init && modt == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_MODT>(mtrk, i, dummy)) {
                        mtrk.midi_events.erase(mtrk.midi_events.begin() +
                                static_cast<long>(i--));
                    } else {
                        modt = cev.get_value();
                    }
                    break;
                case MIDI_CC_EX_TUNE:
                    if ((tune_init && tune == cev.get_value()) ||
                            find_next_event_at_tick_index<controller_message_midi_event,
                            MIDI_CC_EX_TUNE>(mtrk, i, dummy)) {
                        mtrk.midi_events.erase(mtrk.midi_events.begin() +
                                static_cast<long>(i--));
                    } else {
                        tune = cev.get_value();
                    }
                    break;
                default:
                    break;
                } // end controller switch 
            } // end if controller events
        } // end event loop
    } // end track for loop
}

static void write_agb() {
    // TODO
    mf.save_to_file("_test.mid");
}
