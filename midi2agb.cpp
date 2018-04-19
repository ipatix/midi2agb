#include <string>
#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>
#include <cmath>

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

static std::string arg_input_file;
static bool arg_input_file_read = false;
static std::string arg_output_file;
static bool arg_output_file_read = false;

static cppmidi::midi_file mf;

static void midi_read_special_events();
static void midi_remove_empty_tracks();
static void midi_filter_volume();
static void midi_loop_and_state_reset();
static void midi_remove_redundant_events();

int main(int argc, char *argv[]) {
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

        // apply scales, loop fix
        midi_read_special_events();
        midi_remove_empty_tracks();
        midi_filter_volume();
        midi_loop_and_state_reset();
        midi_remove_redundant_events();
        // TODO
    } catch (const cppmidi::xcept& ex) {
        fprintf(stderr, "cppmidi lib error:\n%s\n", ex.what());
        return 1;
    } catch (const std::exception& ex) {
        fprintf(stderr, "std lib error:\n%s\n", ex.what());
        return 1;
    }
}

static void midi_read_special_events() {
    /*
     * Special Events:
     * - Meta Marker/Text/Cuepoint "[": loop start for all tracks
     * - Meta Marker/Text/Cuepoint "]": loop end for all tracks
     * - Meta Text "modscale=%f": scales modulation by factor %f
     * - Meta Text "modt_global=%d": set's modulation type for whole song
     * - Meta Marker/Text/Cuepoint "modt=%d": set's modulation type at position
     */
    // TODO
}

static void midi_remove_empty_tracks() {
    using namespace cppmidi;
    midi_track tempo_track;

    // seperate tempo events
    for (midi_track& trk : mf.midi_tracks) {
        for (size_t i = 0; i < trk.midi_events.size(); i++) {
            ev_type type = trk.midi_events[i]->event_type();
            if (type != ev_type::MetaTempo)
                continue;
            tempo_track.midi_events.emplace_back(std::move(trk.midi_events[i]));
            trk.midi_events.erase(trk.begin() + i--);
        }
    }

    tempo_track.sort_events();

    // remove tracks without notes
    for (size_t trk = 0; trk < mf.midi_tracks.size(); trk++) {
        // set false if a note event was found
        bool del = true;
        for (const midi_event& ev : mf.midi_tracks[trk].midi_events) {
            if (ev.event_type() == ev_type::MsgNoteOn) {
                del = false;
                break;
            }
        }
        mf.midi_tracks.erase(mf.midi_tracks.begin() + trk--);
    }

    if (mf.midi_tracks.size() == 0)
        throw std::runtime_error("The MIDI file has no notes!");

    // all empty tracks deleted, now reinsert tempo events to first track
    for (midi_event& tev : tempo_track.midi_events) {
        // locate position for insertion
        auto cmp = [](const unique_ptr<midi_event>& a,
                const unique_ptr<midi_event>& b) {
            return a->ticks < b->ticks;
        };
        auto position = std::lower_bound(
                mf.midi_tracks[0].midi_events.begin(),
                mf.midi_tracks[0].midi_events.end(),
                tev, cmp);
        if (position == mf.midi_tracks[0].midi_events.end()) {
            mf.midi_tracks[0].midi_events.emplace_back(std::move(tev));
        } else {
            mf.midi_tracks[0].midi_events.insert(position, std::move(tev));
        }
    }
    // done
}

static void midi_filter_volume() {
    using namespace cppmidi;

    auto vol_scale = [](uint8_t vol, uint8_t expr, bool nat) {
        float x = vol * expr;
        if (nat) {
            x /= 127.0f * 127.0f;
            x = powf(x, 10.0f / 6.0f);
            x *= 127.0f;
            x = std:round(x);
        } else {
            x /= 127.0f;
            x = std::round(x);
        }
        return static_cast<uint8_t>(std::max(0, std::min(127, static_cast<int>(x))));
    };

    auto vel_scale = [](uint8_t vel, bool nat) {
        float x = vel;
        if (nat) {
            x /= 127.0f;
            x = powf(x, 10.0f / 6.0f);
            x *= 127.0f;
            x = std:round(x);
        }
        // clamp to lower 1 because midi velocity 0 is a note off
        return static_cast<uint8_t>(std::max(1, std::min(127, static_cast<int>(x))));
    };

    for (midi_track& mtrk : mf.midi_tracks) {
        uint8_t volume = 100;
        uint8_t expression = 127;

        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            midi_event& ev = *mtrk.midi_events[i];
            if (ev.event_type() == ev_type::MsgController) {
                controller_message_midi_event& ctrl_ev = 
                    static_cast<controller_message_midi_event&>(ev);
                if (ctrl_ev.get_controller() == MIDI_CC_MSB_VOLUME) {
                    volume = ctrl_ev.get_value();
                    ctrl_ev.set_value(vol_scale(volume, expression, arg_natural));
                } else if (ctrl_ev.get_controller() == MIDI_CC_MSB_EXPRESSION) {
                    expression = ctrl_ev.get_value();
                    ctrl_ev.set_controller(MIDI_CC_MSB_VOLUME);
                    ctrl_ev.set_value(vol_scale(volume, expression, arg_natural));
                }
            } else if (ev.event_type() == ev_type::MsgNoteOn) {
                noteon_message_midi_event& note_ev =
                    static_cast<noteon_message_midi_event&>(ev);
                note_ev.set_velocity(vel_scale(note_ev.get_velocity()));
            }
        }
    }
}

static void midi_loop_and_state_reset() {
    for (midi_track& mtrk : mf.midi_tracks) {
        uint8_t state_vol = 100;
        uint8_t state_voice = 0xFF;
        uint8_t state_pan = 0x40;
        uint8_t state_tempo = 120 / 2;
        int16_t state_bend = 0;
        uint8_t state_bendr = 2;
        uint8_t state_mod = 0;
        uint8_t state_modt = 0;
        uint8_t state_tune = 0;
        uint8_t state_iecv = 0;
        uint8_t state_iecl = 0;

        for (size_t i = 0; i < mtrk.midi_events.size(); i++) {
            // TODO
        }
    }
}
