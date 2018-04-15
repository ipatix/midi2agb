#include <string>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>

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

static uint32_t loop_start = 0;
static uint32_t loop_end = 0;
static bool loop_present = false;

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
                arg_pri = static_cast<uint8_t>(arg_pri);
            } else if (!st.compare("-r")) {
                if (++i >= argc)
                    die("-r: missing parameter\n");
                int rev = stoi(std::string(argv[i]));
                if (rev < 0 || rev > 127)
                    die("-r: parameter %d out of range\n", rev);
                arg_pri = static_cast<uint8_t>(arg_pri);
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
        cppmidi::midi_file mf;
        mf.load_from_file(arg_input_file);

        // 24 clocks per quarter note is pretty much the standard for GBA
        mf.convert_time_division(24);

        // apply scales, loop fix
        // TODO, do this later
    } catch (const cppmidi::xcept& ex) {
        fprintf(stderr, "cppmidi lib error:\n%s\n", ex.what());
        return 1;
    } catch (const std::exception& ex) {
        fprintf(stderr, "std lib error:\n%s\n", ex.what());
        return 1;
    }
}
