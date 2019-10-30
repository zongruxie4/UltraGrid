/**
 * @file   keyboard_control.cpp
 * @author Martin Pulec     <pulec@cesnet.cz>
 *
 * With code taken from Olivier Mehani (set_tio()).
 */
/*
 * Copyright (c) 2015-2019 CESNET, z. s. p. o.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @todo
 * Consider using some abstraction over terminal control mainly because
 * differencies between Linux and Windows (ANSI mode or terminfo?)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#include "config_unix.h"
#include "config_win32.h"
#endif // HAVE_CONFIG_H

#include <climits>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "debug.h"
#include "host.h"
#include "keyboard_control.h"
#include "messaging.h"
#include "utils/color_out.h"
#include "utils/thread.h"
#include "video.h"

#ifdef HAVE_TERMIOS_H
#include <unistd.h>
#include <termios.h>
#else
#include <conio.h>
#endif

static bool set_tio();

#define CONFIG_FILE "ug-key-map.txt"
#define MOD_NAME "[key control] "

using namespace std;
using namespace std::chrono;

#ifdef HAVE_TERMIOS_H
static bool signal_catched = false;

static void catch_signal(int)
{
        signal_catched = true;
}
#endif

#ifdef HAVE_TERMIOS_H
static struct termios old_tio;
#endif

static void restore_old_tio(void)
{
#ifdef HAVE_TERMIOS_H
        struct sigaction sa, sa_old;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTTOU, &sa, &sa_old);
        /* set the new settings immediately */
        /* restore the former settings */
        tcsetattr(STDIN_FILENO,TCSANOW,&old_tio);
        sigaction(SIGTTOU, &sa_old, NULL);
#endif
}

keyboard_control::keyboard_control(struct module *parent) :
        m_root(nullptr),
#ifdef HAVE_TERMIOS_H
        m_event_pipe{0, 0},
#endif
        m_should_exit(false),
        m_started(false),
        m_locked_against_changes(true),
        guarded_keys { '*', '/', '9', '0', 'c', 'C', 'm', 'M', '+', '-', 'e', 's', 'q', 'v', 'V' }
{
        m_start_time = time(NULL);

        m_root = get_root_module(parent);
        module_init_default(&m_mod);
        m_mod.cls = MODULE_CLASS_KEYCONTROL;
        m_mod.priv_data = this;
        m_mod.new_message = keyboard_control::msg_received_func;
        module_register(&m_mod, parent);
}

keyboard_control::~keyboard_control() {
        module_done(&m_mod);
}

ADD_TO_PARAM(disable_keyboard_control, "disable-keyboard-control", "* disable-keyboard-control\n"
                "  disables keyboard control (usable mainly for non-interactive runs)\n");
void keyboard_control::start()
{
        set_thread_name("keyboard-control");
        if (get_commandline_param("disable-keyboard-control")) {
                return;
        }
#ifdef HAVE_TERMIOS_H
        if (pipe(m_event_pipe) != 0) {
                log_msg(LOG_LEVEL_ERROR, "[key control] Cannot create control pipe!\n");
                return;
        }

        if (!isatty(STDIN_FILENO)) {
                log_msg(LOG_LEVEL_WARNING, "[key control] Stdin is not a TTY - disabling keyboard control.\n");
                return;
        }

        if (!set_tio()) {
                log_msg(LOG_LEVEL_WARNING, "[key control] Background task - disabling keyboard control.\n");
                return;
        }
        atexit(restore_old_tio);
#endif

        load_config_map();

        m_keyboard_thread = thread(&keyboard_control::run, this);
        m_started = true;
}

void keyboard_control::signal()
{
#ifdef HAVE_TERMIOS_H
        char c = 0;
        if (write(m_event_pipe[1], &c, 1) != 1) {
                perror(MOD_NAME "m_event_pipe write");
        }
#else
        m_cv.notify_one();
#endif
}

void keyboard_control::stop()
{
        if (!m_started) {
                return;
        }
        unique_lock<mutex> lk(m_lock);
        m_should_exit = true;
        lk.unlock();
        signal();
        m_keyboard_thread.join();
        m_started = false;

#ifdef HAVE_TERMIOS_H
        close(m_event_pipe[0]);
        close(m_event_pipe[1]);
#endif
}

#ifdef HAVE_TERMIOS_H
#define GETCH getchar
#else
#define GETCH getch
#endif

/*
 * Input must be 0x80-0xff
 */
static int count_utf8_bytes(unsigned int i) {
        int count = 0;
        while ((i & 0x80) != 0u) {
                count++;
                i <<= 1;
        }
        return count;
}

/**
 * Tries to parse at least some small subset of ANSI control sequences not to
 * be ArrowUp interpreted as '\E', '[' and 'A' individually.
 *
 * @todo
 * * improve coverage and/or find some suitable implemnation (ideally one file,
 *   not ncurses)
 * * the function can be moved to separate util file
 */
static int64_t get_ansi_code() {
        int64_t c = GETCH();
        debug_msg(MOD_NAME "Pressed %d\n", c);
        if (c == '[') { // CSI
                c = '\E' << 8 | '[';
                while (true) {
                        if ((c >> 56u) != 0) {
                                LOG(LOG_LEVEL_WARNING) << MOD_NAME "Long control sequence detected!\n";
                                return -1;
                        }
                        c <<= 8;
                        int tmp = GETCH();
                        assert(tmp < 0x80); // ANSI esc seq should use only 7-bit encoding
                        debug_msg(MOD_NAME "Pressed %d\n", tmp);
                        if (tmp == EOF) {
                                LOG(LOG_LEVEL_WARNING) << MOD_NAME "EOF detected!\n";
                                return -1;
                        }
                        c |= tmp;
                        if (tmp >= 0x40 && tmp <= 0xee) { // final byte
                                break;
                        }
                }
        } else if (c == 'N' // is this even used?
                        || c == 'O') { // eg. \EOP - F1-F4 (the rest of Fn is CSI)
                int tmp = GETCH();
                debug_msg(MOD_NAME "Pressed %d\n", tmp);
                if (tmp == EOF) {
                        LOG(LOG_LEVEL_WARNING) << MOD_NAME "EOF detected!\n";
                        return -1;
                }
                c = '\E' << 16 | c << 8 | tmp;
        } else {
                LOG(LOG_LEVEL_WARNING) << MOD_NAME "Unknown control sequence!\n";
                return -1;
        }

        return c;
}

#ifndef WIN32
static int64_t get_utf8_code(int c) {
        if (c < 0xc0) {
                LOG(LOG_LEVEL_WARNING) << MOD_NAME "Wrong UTF sequence!\n";
                return -1;
        }
        int ones = count_utf8_bytes(c);
        for (int i = 1; i < ones; ++i) {
                c = (c & 0x7fffff) << 8;
                int tmp = GETCH();
                debug_msg(MOD_NAME "Pressed %d\n", tmp);
                if (tmp == EOF) {
                        LOG(LOG_LEVEL_WARNING) << MOD_NAME "EOF detected!\n";
                        return -1;
                }
                c |= tmp;
        }
        if (ones > 7) {
                LOG(LOG_LEVEL_WARNING) << MOD_NAME "Unsupported UTF sequence length!\n";
                return -1;
        }
        return c;
}
#endif

#ifdef WIN32
static int64_t convert_win_to_ansi_keycode(int c) {
        switch (c) {
        case 0xe048: return K_UP;
        case 0xe04b: return K_LEFT;
        case 0xe04d: return K_RIGHT;
        case 0xe050: return K_DOWN;
        default:
                   LOG(LOG_LEVEL_WARNING) << MOD_NAME "Cannot map Win keycode 0x"
                           << hex << setfill('0') << setw(2) << c << setw(0) << dec << " to ANSI.\n";
                   return -1;
        }
}
#endif

static bool is_utf8(int64_t ch) {
#ifdef WIN32
        return false;
#endif

        unsigned char first_byte;
        while (ch != 0) {
                first_byte = ch & 0xff;
                ch >>= 8;
        }
        return first_byte & 0x80; /// @todo UTF-8 validity check would be nice
}

static string get_utf8_representation(int64_t ch) {
        char str[9] = "";
        while (ch != 0) {
                memmove(str + 1, str, 8);
                str[0] = ch & 0xff;
                ch >>= 8;
        }
        return str;
}

static string get_keycode_representation(int64_t ch) {
        switch (ch) {
        case K_UP: return "KEY_UP";
        case K_DOWN: return "KEY_DOWN";
        case K_LEFT: return "KEY_LEFT";
        case K_RIGHT: return "KEY_RIGHT";
        }

        if (ch >= 1 && ch <= 'z' - 'a' + 1) {
                return string("Ctrl-") + string(1, 'a' + ch - 1);
        }

        if (ch <= UCHAR_MAX && isprint(ch)) {
                return string("'") + string(1, ch) + "'";
        }

        if (is_utf8(ch)) {
                return string("'") + get_utf8_representation(ch) + "'";
        }

        stringstream oss;
        oss << "0x" << hex << ch;
        return oss.str();
}

/**
 * @returns next key either from keyboard or received via control socket.
 *          If m_should_exit is set, returns 0.
 */
int64_t keyboard_control::get_next_key()
{
        int64_t c;
        while (1) {
                {
                        unique_lock<mutex> lk(m_lock);
                        if (m_should_exit) {
                                return 0;
                        }

                        if (!m_pressed_keys.empty()) {
                                c = m_pressed_keys.front();
                                m_pressed_keys.pop();
                                return c;
                        }
                }
#ifdef HAVE_TERMIOS_H
                fd_set set;
                FD_ZERO(&set);
                FD_SET(0, &set);
                FD_SET(m_event_pipe[0], &set);
                select(m_event_pipe[0] + 1, &set, NULL, NULL, NULL);

                if (FD_ISSET(m_event_pipe[0], &set)) {
                        char c;
                        read(m_event_pipe[0], &c, 1);
                        continue; // skip to event processing
                }

                if (FD_ISSET(0, &set)) {
#else
                if (kbhit()) {
#endif
                        int64_t c = GETCH();
                        debug_msg(MOD_NAME "Pressed %d\n", c);
                        if (c == '\E') {
                                if ((c = get_ansi_code()) == -1) {
                                        continue;
                                }
#ifdef WIN32
                        } else if (c == 0x0 || c == 0xe0) { // Win keycodes
                                int tmp = GETCH();
                                debug_msg(MOD_NAME "Pressed %d\n", tmp);
                                if (tmp == EOF) {
                                        LOG(LOG_LEVEL_WARNING) << MOD_NAME "EOF detected!\n";
                                        continue;
                                }
                                if ((c = convert_win_to_ansi_keycode(c << 8 | tmp)) == -1) {
                                        continue;
                                }
#else
                        } else if (c > 0x80) {
                                if ((c = get_utf8_code(c)) == -1) {
                                        continue;
                                }
#endif
                        }
                        return c;
                }
#if !defined HAVE_TERMIOS_H
                // kbhit is non-blockinkg - we don't want to busy-wait
                unique_lock<mutex> lk(m_lock);
                m_cv.wait_for(lk, milliseconds(200));
                lk.unlock();
#endif
        }
}

void keyboard_control::run()
{
        int saved_log_level = -1;
        int64_t c;

        while ((c = get_next_key())) {
                m_lock.lock();
                if (key_mapping.find(c) != key_mapping.end()) { // user defined mapping exists
                        string cmd = key_mapping.at(c).first;
                        m_lock.unlock();
                        exec_external_commands(cmd.c_str());
                        continue;
                }
                m_lock.unlock();

                if (c == K_CTRL('X')) {
                        m_locked_against_changes = !m_locked_against_changes; // ctrl-x pressed
                        cout << GREEN("Keyboard control: " << (m_locked_against_changes ? "" : "un") << "locked against changes\n");
                        continue;
                }
                if (m_locked_against_changes && guarded_keys.find(c) != guarded_keys.end()) {
                        cout << GREEN("Keyboard control: locked against changes, press 'Ctrl-x' to unlock or 'h' for help.\n");
                        continue;
                }

                switch (c) {
                case '*':
                case '/':
                case '9':
                case '0':
                case 'm':
                {
                        char path[] = "audio.receiver";
                        auto m = (struct msg_receiver *) new_message(sizeof(struct msg_receiver));
                        switch (c) {
                        case '0':
                        case '*': m->type = RECEIVER_MSG_INCREASE_VOLUME; break;
                        case '9':
                        case '/': m->type = RECEIVER_MSG_DECREASE_VOLUME; break;
                        case 'm': m->type = RECEIVER_MSG_MUTE; break;
                        }

                        auto resp = send_message(m_root, path, (struct message *) m);
                        free_response(resp);
                        break;
                }
                case 'M':
                {
                        char path[] = "audio.sender";
                        auto m = (struct msg_sender *) new_message(sizeof(struct msg_sender));
                        m->type = SENDER_MSG_MUTE;

                        auto resp = send_message(m_root, path, (struct message *) m);
                        free_response(resp);
                        break;
                }
                case '+':
                case '-':
                {
                        int audio_delay = get_audio_delay();
                        audio_delay += c == '+' ? 10 : -10;
                        log_msg(LOG_LEVEL_INFO, "New audio delay: %d ms.\n", audio_delay);
                        set_audio_delay(audio_delay);
                        break;
                }
                case 'e':
                {
                        char path[] = "exporter";
                        auto m = (struct message *) new_message(sizeof(struct msg_universal));
                        strcpy(((struct msg_universal *) m)->text, "toggle");
                        auto resp = send_message(m_root, path, (struct message *) m);
                        free_response(resp);
                        break;
                }
                case 'c':
                case 'C':
                        read_command(c == 'C');
                        break;
                case 'v':
                case 'V':
                {
                        if (islower(c)) {
                                log_level = (log_level + 1) % (LOG_LEVEL_MAX + 1);
                        } else {
                                log_level = (log_level - 1 + (LOG_LEVEL_MAX + 1)) % (LOG_LEVEL_MAX + 1);
                        }
                        cout << "Log level: " << log_level << "\n";
                        break;
                }
                case 's':
                        if (saved_log_level == -1) {
                                saved_log_level = log_level;
                                cout << GREEN("Output suspended, press 'q' to continue.\n");
                                log_level = LOG_LEVEL_QUIET;
                        }
                        break;
                case 'q':
                        if (saved_log_level != -1) {
                                log_level = saved_log_level;
                                saved_log_level = -1;
                                cout << GREEN( "Output resumed.\n");
                        }
                        break;
                case 'h':
                        usage();
                        break;
                case 'i':
                        cout << "\n";
                        info();
                        break;
                case '\n':
                case '\r':
                        cout << endl;
                        break;
                default:
                        LOG(LOG_LEVEL_WARNING) << MOD_NAME "Unsupported key " << get_keycode_representation(c) << " pressed. Press 'h' to help.\n";
                }
        }
}

void keyboard_control::info()
{
        cout << BOLD("Start time: ") << asctime(localtime(&m_start_time));
        cout << BOLD("Verbosity level: ") << log_level << (log_level == LOG_LEVEL_INFO ? " (default)" : "") << "\n";
        cout << BOLD("Locked against changes: ") << (m_locked_against_changes ? "true" : "false") << "\n";
        cout << BOLD("Audio playback delay: " << get_audio_delay()) << " ms\n";

        {
                char path[] = "audio.receiver";
                auto m = (struct msg_receiver *) new_message(sizeof(struct msg_receiver));
                m->type = RECEIVER_MSG_GET_VOLUME;
                auto resp = send_message_sync(m_root, path, (struct message *) m, 100, SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(resp) == 200) {
                        double vol = atof(response_get_text(resp));
                        double db = 20.0 * log10(vol);
                        std::streamsize p = cout.precision();
                        ios_base::fmtflags f = cout.flags();
                        cout << BOLD("Received audio volume: ") << fixed << setprecision(2) << vol * 100.0 << "% (" << (db >= 0.0 ? "+" : "") <<  db << " dB)\n";
                        cout.precision(p);
                        cout.flags(f);
                }
                free_response(resp);
        }

        {
                char path[] = "audio.sender";
                auto m = (struct msg_sender *) new_message(sizeof(struct msg_sender));
                m->type = SENDER_MSG_GET_STATUS;
                auto resp = send_message_sync(m_root, path, (struct message *) m, 100, SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(resp) == 200) {
                        int muted;
                        sscanf(response_get_text(resp), "%d", &muted);
                        cout << BOLD("Sended audio status - muted: ") << (muted ? "true" : "false") << "\n";
                }
                free_response(resp);
        }

	{
		struct video_desc desc{};
                struct msg_sender *m = (struct msg_sender *) new_message(sizeof(struct msg_sender));
                m->type = SENDER_MSG_QUERY_VIDEO_MODE;
                struct response *r = send_message_sync(m_root, "sender", (struct message *) m, 100,  SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(r) == RESPONSE_OK) {
                        const char *text = response_get_text(r);
                        istringstream iss(text);
                        iss >> desc;
                        cout << BOLD("Captured video format: ") << desc << "\n";
                }
                free_response(r);
	}

	{
		struct video_desc desc{};
                struct msg_universal *m = (struct msg_universal *) new_message(sizeof(struct msg_universal));
                strcpy(m->text, "get_format");
                struct response *r = send_message_sync(m_root, "receiver.decoder", (struct message *) m, 100,  SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(r) == RESPONSE_OK) {
                        const char *text = response_get_text(r);
                        istringstream iss(text);
                        iss >> desc;
                        cout << BOLD("Received video format: ") <<  desc << "\n";
                }
                free_response(r);
	}

	{
                struct msg_universal *m = (struct msg_universal *) new_message(sizeof(struct msg_universal));
                strcpy(m->text, "get_port");
                struct response *r = send_message_sync(m_root, "control", (struct message *) m, 100,  SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(r) == RESPONSE_OK) {
                        cout << BOLD("Control port: ") << response_get_text(r) << "\n";
                }
                free_response(r);
	}

	{
                struct msg_universal *m = (struct msg_universal *) new_message(sizeof(struct msg_universal));
                strcpy(m->text, "status");
                struct response *r = send_message_sync(m_root, "exporter", (struct message *) m, 100,  SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(r) == RESPONSE_OK) {
                        cout << BOLD("Exporting: ") << response_get_text(r) << "\n";
                }
                free_response(r);
	}

        cout << "\n";
}

#define G(key) (guarded_keys.find(key) != guarded_keys.end() ? " [g]" : "")

void keyboard_control::usage()
{
        cout << "\nAvailable keybindings:\n" <<
                BOLD("\t  * 0  ") << "- increase volume" << G('*') << "\n" <<
                BOLD("\t  / 9  ") << "- decrease volume" << G('/') << "\n" <<
                BOLD("\t   +   ") << "- increase audio delay by 10 ms" << G('+') << "\n" <<
                BOLD("\t   -   ") << "- decrease audio delay by 10 ms" << G('-') << "\n" <<
                BOLD("\t   m   ") << "- mute/unmute receiver" << G('m') << "\n" <<
                BOLD("\t   M   ") << "- mute/unmute sender" << G('M') << "\n" <<
                BOLD("\t   v   ") << "- increase verbosity level" << G('v') << "\n" <<
                BOLD("\t   V   ") << "- decrease verbosity level" << G('V') << "\n" <<
                BOLD("\t   e   ") << "- record captured content (toggle)" << G('e') << "\n" <<
                BOLD("\t   h   ") << "- show help" << G('h') << "\n" <<
                BOLD("\t   i   ") << "- show various information" << G('*') << "\n" <<
                BOLD("\t  s q  ") << "- suspend/resume output" << G('s') << G('q') << "\n" <<
                BOLD("\t  c C  ") << "- execute command through control socket (capital for multiple)" << G('c') << G('C') << "\n" <<
                BOLD("\tCtrl-x ") << "- unlock/lock against changes" << G(K_CTRL('X')) << "\n" <<
                BOLD("\tCtrl-c ") << "- exit " << G(K_CTRL('c')) << "\n" <<
                "\n";

        if (key_mapping.size() > 0) {
                cout << "Custom keybindings:\n";
                for (auto it : key_mapping) {
                        cout << BOLD("\t" << setw(10) << get_keycode_representation(it.first) << setw(0)) << " - " << (it.second.second.empty() ? it.second.first : it.second.second) << G(it.first) << "\n";
                }
                cout << "\n";
        }
        cout << BOLD("[g]") << " indicates that that option is guarded (Ctrl-x needs to be pressed first)\n";
}

void keyboard_control::load_config_map() {
        FILE *in = fopen(CONFIG_FILE, "r");
        if (!in) {
                return;
        }
        char buf[1024];
        while (fgets(buf, sizeof buf - 1, in)) {
                if (strlen(buf) > 0 && buf[strlen(buf) - 1] == '\n') { // trim NL
                        buf[strlen(buf) - 1] = '\0';
                }
                if (strlen(buf) == 0) {
                        continue;
                }
                if (!exec_local_command(buf)) {
                        LOG(LOG_LEVEL_ERROR) << "Wrong config: " << buf << "\n";
                        continue;
                }
        }
        fclose(in);
}

bool keyboard_control::exec_local_command(const char *command)
{
        if (strcmp(command, "localhelp") == 0) {
                printf("Local help:\n"
                                "\tmap <X> cmd1[;cmd2..][#name]\n"
                                "\t\tmaps key to command(s) for control socket (with an optional\n\t\tidentifier)\n"
                                "\tmap #<X> cmd1[;cmd2..][#name]\n"
                                "\t\tmaps key number (ASCII or multi-byte for non-ASCII) to\n\t\tcommand(s) for control socket (with an optional identifier)\n"
                                "\tunmap <X>\n"
                                "\tpress <num>\n"
                                "\t\tEmulate pressing key <num>\n\n"
                                "Mappings can be stored in " CONFIG_FILE "\n\n");
                return true;
        } else if (strstr(command, "map ") == command) {
                char *ccpy = strdup(command + strlen("map "));
                if (strlen(ccpy) > 3) {
                        int key = ccpy[0];
                        char *command = ccpy + 2;
                        if (key == '#' && isdigit(ccpy[1])) {
                                key = strtol(ccpy + 1, &command, 10);
                                command += 1; // skip ' '
                        }
                        const char *name = "";
                        if (strchr(command, '#')) {
                                name = strchr(command, '#') + 1;
                                *strchr(command, '#') = '\0';
                        }
                        key_mapping.insert({key, make_pair(command, name)});
                }
                free(ccpy);
                return true;
        } else if (strstr(command, "unmap ") == command) {
                command += strlen("unmap ");
                if (strlen(command) >= 1) {
                        key_mapping.erase(command[0]);
                }
                return true;
        } else if (strstr(command, "press ") == command) {
                const char *key = command + strlen("press ");
                int64_t k;
                sscanf(key, "%" SCNi64, &k);
                unique_lock<mutex> lk(m_lock);
                m_pressed_keys.push(k);
                lk.unlock();
                signal();
                return true;
        }
        return false;
}

void keyboard_control::exec_external_commands(const char *commands)
{
        char *cpy = strdup(commands);
        char *item, *save_ptr, *tmp = cpy;
        while ((item = strtok_r(tmp, ";", &save_ptr))) {
                struct msg_universal *m = (struct msg_universal *) new_message(sizeof(struct msg_universal));
                strcpy(m->text, "execute ");
                strncat(m->text, item, sizeof m->text - strlen(m->text) - 1);
                struct response *r = send_message_sync(m_root, "control", (struct message *) m, 100,  SEND_MESSAGE_FLAG_QUIET | SEND_MESSAGE_FLAG_NO_STORE);
                if (response_get_status(r) != RESPONSE_OK) {
                        LOG(LOG_LEVEL_ERROR) << "Could not send a message!\n";
                }
                free_response(r);

                tmp = nullptr;
        }
        free(cpy);
}

void keyboard_control::execute_command(const char *command) {
        if (!exec_local_command(command)) {
                exec_external_commands(command);
        }
}

void keyboard_control::read_command(bool multiple)
{
        int saved_log_level = log_level;
        log_level = LOG_LEVEL_QUIET;
        restore_old_tio();
        if (multiple) {
                printf("Enter commands for control (try \"help\" or \"localhelp\").\n");
                printf("Exit the mode with blank line.\n");
        } else {
                printf("Enter a command for control (try \"help\" or \"localhelp\"):\n");
        }
        while (1) {
                log_level = LOG_LEVEL_QUIET;
                printf("control> ");
                char buf[sizeof msg_universal::text];
                if (!fgets(buf, sizeof buf - 1, stdin)) {
                        log_level = saved_log_level;
                        break;
                }
                log_level = saved_log_level;

                if (strlen(buf) == 0 || (strlen(buf) == 1 && buf[0] == '\n')) { // empty input
                        break;
                }
                if (buf[strlen(buf) - 1] == '\n') { // strip newline
                        buf[strlen(buf) - 1] = '\0';
                }

                execute_command(buf);

                if (!multiple) {
                        break;
                }
        }

        set_tio();
}

/**
 * From http://shtrom.ssji.net/skb/getc.html
 */
static bool set_tio()
{
#ifdef HAVE_TERMIOS_H
        struct termios new_tio;
        /* get the terminal settings for stdin */
        tcgetattr(STDIN_FILENO,&old_tio);
        /* we want to keep the old setting to restore them a the end */
        new_tio=old_tio;
        /* disable canonical mode (buffered i/o) and local echo */
        new_tio.c_lflag &=(~ICANON & ~ECHO);
        // Wrap calling of tcsetattr() by handling SIGTTOU. SIGTTOU can be raised if task is
        // run in background and trying to call tcsetattr(). If so, we disable keyboard
        // control.
        struct sigaction sa, sa_old;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = catch_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTTOU, &sa, &sa_old);
        /* set the new settings immediately */
        tcsetattr(STDIN_FILENO,TCSANOW,&new_tio);
        sigaction(SIGTTOU, &sa_old, NULL);

        if (!signal_catched) {
                return true;
        }
#endif
        return false;
}

void keyboard_control::msg_received_func(struct module *m) {
        auto s = static_cast<keyboard_control *>(m->priv_data);

        s->msg_received();
}

void keyboard_control::msg_received() {
        struct message *msg;
        while ((msg = check_message(&m_mod))) {
                auto m = reinterpret_cast<struct msg_universal *>(msg);
                lock_guard<mutex> lk(m_lock);
                execute_command(m->text);
                struct response *r = new_response(RESPONSE_OK, nullptr);
                free_message((struct message *) m, r);
        }
}

