#include "input.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>

static struct termios orig_termios;
static bool raw_active = false;

void input_raw_mode(void) {
    if (raw_active) return;
    if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) return;
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(unsigned)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_cflag |=  (unsigned)(CS8);
    raw.c_lflag &= ~(unsigned)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_active = true;
}

void input_restore_mode(void) {
    if (!raw_active) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_active = false;
}

uint32_t input_utf8_codepoint(const unsigned char *buf, int *bytes_consumed) {
    unsigned char c = buf[0];
    if (c < 0x80) {
        *bytes_consumed = 1;
        return c;
    } else if ((c & 0xE0) == 0xC0) {
        *bytes_consumed = 2;
        return ((c & 0x1F) << 6) | (buf[1] & 0x3F);
    } else if ((c & 0xF0) == 0xE0) {
        *bytes_consumed = 3;
        return ((c & 0x0F) << 12) | ((buf[1] & 0x3F) << 6) | (buf[2] & 0x3F);
    } else {
        *bytes_consumed = 4;
        return ((c & 0x07) << 18) | ((buf[1] & 0x3F) << 12) |
               ((buf[2] & 0x3F) << 6) | (buf[3] & 0x3F);
    }
}

/* Wait up to timeout_ms for data on stdin; return bytes read (0 = timeout) */
static int stdin_wait_read(unsigned char *buf, int maxlen, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv = {
        .tv_sec  = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };
    int r = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return 0;
    ssize_t n = read(STDIN_FILENO, buf, maxlen);
    return n > 0 ? (int)n : 0;
}

bool input_read_event(InputEvent *ev, int timeout_ms) {
    memset(ev, 0, sizeof(*ev));

    unsigned char buf[64];
    int n = stdin_wait_read(buf, sizeof(buf), timeout_ms);
    if (n <= 0) return false;

    /* ESC sequence */
    if (buf[0] == 0x1b) {
        if (n == 1) {
            /* lone ESC */
            ev->key = KEY_ESCAPE;
            return true;
        }
        /* Try to read more bytes if only partial so far */
        if (n < 6) {
            int extra = stdin_wait_read(buf + n, sizeof(buf) - n, 20);
            n += extra;
        }

        /* CSI: \x1b[ */
        if (n >= 2 && buf[1] == '[') {
            /* Mouse: CSI M (X10 3-byte), CSI < ... M/m (SGR extended) */
            if (n >= 3 && buf[2] == 'M') {
                /* X10 mode: 3 parameter bytes */
                if (n >= 6) {
                    ev->is_mouse = true;
                    ev->mouse.button   = buf[3] - 32;
                    ev->mouse.x        = buf[4] - 32;
                    ev->mouse.y        = buf[5] - 32;
                    ev->mouse.released = false;
                    ev->key = KEY_MOUSE;
                    return true;
                }
            }
            /* SGR mouse: \x1b[<Pb;Px;PyM or m */
            if (n >= 4 && buf[2] == '<') {
                /* parse Pb;Px;Py + trailing M/m */
                int pb = 0, px = 0, py = 0;
                int i = 3;
                while (i < n && buf[i] >= '0' && buf[i] <= '9')
                    pb = pb * 10 + (buf[i++] - '0');
                if (i < n && buf[i] == ';') i++;
                while (i < n && buf[i] >= '0' && buf[i] <= '9')
                    px = px * 10 + (buf[i++] - '0');
                if (i < n && buf[i] == ';') i++;
                while (i < n && buf[i] >= '0' && buf[i] <= '9')
                    py = py * 10 + (buf[i++] - '0');
                bool released = (i < n && buf[i] == 'm');
                ev->is_mouse      = true;
                ev->mouse.button  = pb;
                ev->mouse.x       = px;
                ev->mouse.y       = py;
                ev->mouse.released = released;
                ev->key = KEY_MOUSE;
                return true;
            }
            /* Arrow keys and others */
            if (n >= 3) {
                /* look at the final byte */
                unsigned char final = buf[n - 1];
                /* Check modifier byte if present: CSI [modifier] final */
                switch (final) {
                    case 'A': ev->key = KEY_ARROW_UP;    return true;
                    case 'B': ev->key = KEY_ARROW_DOWN;  return true;
                    case 'C': ev->key = KEY_ARROW_RIGHT; return true;
                    case 'D': ev->key = KEY_ARROW_LEFT;  return true;
                    case 'H': ev->key = KEY_HOME;        return true;
                    case 'F': ev->key = KEY_END;         return true;
                    case 'Z': ev->key = KEY_SHIFT_TAB;   return true;
                    case '~':
                        if (n >= 4) {
                            int code = buf[2] - '0';
                            if (n >= 5 && buf[3] >= '0' && buf[3] <= '9')
                                code = code * 10 + (buf[3] - '0');
                            switch (code) {
                                case 1:  ev->key = KEY_HOME;      return true;
                                case 2:  ev->key = KEY_INSERT;    return true;
                                case 3:  ev->key = KEY_DELETE;    return true;
                                case 4:  ev->key = KEY_END;       return true;
                                case 5:  ev->key = KEY_PAGE_UP;   return true;
                                case 6:  ev->key = KEY_PAGE_DOWN; return true;
                                case 11: ev->key = KEY_F1;        return true;
                                case 12: ev->key = KEY_F2;        return true;
                                case 13: ev->key = KEY_F3;        return true;
                                case 14: ev->key = KEY_F4;        return true;
                                case 15: ev->key = KEY_F5;        return true;
                                case 17: ev->key = KEY_F6;        return true;
                                case 18: ev->key = KEY_F7;        return true;
                                case 19: ev->key = KEY_F8;        return true;
                                case 20: ev->key = KEY_F9;        return true;
                                case 21: ev->key = KEY_F10;       return true;
                                case 23: ev->key = KEY_F11;       return true;
                                case 24: ev->key = KEY_F12;       return true;
                                default: break;
                            }
                        }
                        break;
                    /* Ctrl+Tab: CSI I (often) */
                    case 'I': ev->key = KEY_CTRL_TAB; return true;
                    default: break;
                }
            }
            /* Ctrl+Shift+C: some terminals send \x1b[67;5u or similar */
        }
        /* OSC / SS3 */
        if (n >= 2 && buf[1] == 'O') {
            if (n >= 3) {
                switch (buf[2]) {
                    case 'A': ev->key = KEY_ARROW_UP;    return true;
                    case 'B': ev->key = KEY_ARROW_DOWN;  return true;
                    case 'C': ev->key = KEY_ARROW_RIGHT; return true;
                    case 'D': ev->key = KEY_ARROW_LEFT;  return true;
                    case 'H': ev->key = KEY_HOME;        return true;
                    case 'F': ev->key = KEY_END;         return true;
                    case 'P': ev->key = KEY_F1;          return true;
                    case 'Q': ev->key = KEY_F2;          return true;
                    case 'R': ev->key = KEY_F3;          return true;
                    case 'S': ev->key = KEY_F4;          return true;
                    default:  break;
                }
            }
        }
        /* Just ESC + something unknown */
        ev->key = KEY_ESCAPE;
        return true;
    }

    /* Control characters */
    if (buf[0] < 32) {
        if (buf[0] == '\r' || buf[0] == '\n') {
            ev->key = KEY_ENTER;
        } else if (buf[0] == '\t') {
            ev->key = KEY_TAB;
        } else if (buf[0] == '\b') {
            /* Ctrl+H = 8 = backspace in some terminals */
            ev->key = KEY_BACKSPACE;
        } else {
            ev->key = (KeyCode)buf[0];
        }
        return true;
    }

    /* DEL / Backspace */
    if (buf[0] == 127) {
        ev->key = KEY_BACKSPACE;
        return true;
    }

    /* Printable UTF-8 */
    int bc = 1;
    uint32_t cp = input_utf8_codepoint(buf, &bc);
    ev->key       = KEY_NONE;
    ev->codepoint = cp;
    return true;
}
