#include "rip.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>

int rip_enable_raw_mode(rip_state_t *state) {
    if (state->raw_mode_enabled) return 0;

    // If stdin is a terminal, use it. Otherwise (piped input), open /dev/tty
    if (isatty(STDIN_FILENO)) {
        state->tty_fd = STDIN_FILENO;
        state->is_stdin = 0;
    } else {
        state->tty_fd = open("/dev/tty", O_RDONLY);
        if (state->tty_fd == -1) {
            perror("open /dev/tty");
            return -1;
        }
        state->is_stdin = 1;
    }

    if (tcgetattr(state->tty_fd, &state->orig_termios) == -1) {
        perror("tcgetattr");
        if (state->is_stdin) close(state->tty_fd);
        return -1;
    }

    struct termios raw = state->orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;  // wait for at least 1 byte
    raw.c_cc[VTIME] = 0; // block indefinitely

    if (tcsetattr(state->tty_fd, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        if (state->is_stdin) close(state->tty_fd);
        return -1;
    }

    state->raw_mode_enabled = 1;
    // Switch to alternate screen buffer
    printf("\033[?1049h");
    fflush(stdout);
    return 0;
}

void rip_disable_raw_mode(rip_state_t *state) {
    if (!state->raw_mode_enabled) return;

    // Restore original screen buffer
    printf("\033[?1049l");
    fflush(stdout);

    tcsetattr(state->tty_fd, TCSAFLUSH, &state->orig_termios);
    if (state->is_stdin && state->tty_fd != STDIN_FILENO) {
        close(state->tty_fd);
    }
    state->raw_mode_enabled = 0;
}

void rip_get_terminal_size(rip_state_t *state) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Fallback values
        state->term_rows = 24;
        state->term_cols = 80;
    } else {
        state->term_rows = ws.ws_row;
        state->term_cols = ws.ws_col;
    }
}

int rip_read_key(rip_state_t *state) {
    char c;
    ssize_t nread;
    while ((nread = read(state->tty_fd, &c, 1)) != 1) {
        if (nread == -1) {
            if (errno == EINTR) return KEY_RESIZE;
            return -1;
        }
    }

    if (c == '\033') {
        char seq[3];
        // Set non-blocking read to see if there are more chars following '\033'
        int flags = fcntl(state->tty_fd, F_GETFL);
        fcntl(state->tty_fd, F_SETFL, flags | O_NONBLOCK);

        ssize_t r1 = read(state->tty_fd, &seq[0], 1);
        ssize_t r2 = (r1 == 1) ? read(state->tty_fd, &seq[1], 1) : 0;

        fcntl(state->tty_fd, F_SETFL, flags); // restore blocking mode

        if (r1 != 1 || r2 != 1) return '\033'; // Just escape key

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                char seq2[1];
                flags = fcntl(state->tty_fd, F_GETFL);
                fcntl(state->tty_fd, F_SETFL, flags | O_NONBLOCK);
                ssize_t r3 = read(state->tty_fd, &seq2[0], 1);
                fcntl(state->tty_fd, F_SETFL, flags);

                if (r3 == 1 && seq2[0] == '~') {
                    switch (seq[1]) {
                        case '1': return KEY_HOME;
                        case '4': return KEY_END;
                        case '5': return KEY_PAGE_UP;
                        case '6': return KEY_PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return KEY_ARROW_UP;
                    case 'B': return KEY_ARROW_DOWN;
                    case 'C': return KEY_ARROW_RIGHT;
                    case 'D': return KEY_ARROW_LEFT;
                    case 'H': return KEY_HOME;
                    case 'F': return KEY_END;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'A': return KEY_ARROW_UP;
                case 'B': return KEY_ARROW_DOWN;
                case 'C': return KEY_ARROW_RIGHT;
                case 'D': return KEY_ARROW_LEFT;
                case 'H': return KEY_HOME;
                case 'F': return KEY_END;
            }
        }
        return '\033';
    }

    return c;
}

int rip_decode_utf8(const char *str, size_t len, unsigned int *ch) {
    if (len == 0) {
        *ch = 0;
        return 0;
    }
    unsigned char c = (unsigned char)str[0];
    if (c < 0x80) {
        *ch = c;
        return 1;
    }
    if ((c & 0xe0) == 0xc0) {
        if (len < 2) goto invalid;
        *ch = ((c & 0x1f) << 6) | (str[1] & 0x3f);
        return 2;
    }
    if ((c & 0xe0) == 0xe0) {
        if (len < 3) goto invalid;
        *ch = ((c & 0x0f) << 12) | ((str[1] & 0x3f) << 6) | (str[2] & 0x3f);
        return 3;
    }
    if ((c & 0xf0) == 0xf0) {
        if (len < 4) goto invalid;
        *ch = ((c & 0x07) << 18) | ((str[1] & 0x3f) << 12) | ((str[2] & 0x3f) << 6) | (str[3] & 0x3f);
        return 4;
    }
invalid:
    *ch = c;
    return 1;
}

int rip_char_width(unsigned int ch, int visual_col) {
    if (ch == '\t') {
        return 4 - (visual_col % 4);
    }
    if (ch < 32 || ch == 127) {
        return 2; /* e.g. ^A or ^? */
    }
    /* Basic CJK ranges */
    if ((ch >= 0x3000 && ch <= 0x9FFF) ||
        (ch >= 0xF900 && ch <= 0xFAFF) ||
        (ch >= 0xFF00 && ch <= 0xFFEF) ||
        (ch >= 0x1F300 && ch <= 0x1F9FF)) {
        return 2;
    }
    return 1;
}
