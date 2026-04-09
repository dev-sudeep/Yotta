#include "terminal.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#ifdef __linux__
#  include <pty.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <util.h>
#endif

bool pty_spawn(void) {
    if (g.pty.running) return true;

    /* Determine terminal size for PTY */
    int rows = g.panes[PANE_TERMINAL].h > 1 ? g.panes[PANE_TERMINAL].h - 1 : 24;
    int cols = g.panes[PANE_TERMINAL].w > 0  ? g.panes[PANE_TERMINAL].w    : 80;
    g.pty.rows = rows;
    g.pty.cols = cols;

    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
        .ws_xpixel = 0,
        .ws_ypixel = 0
    };

    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (pid < 0) return false;

    if (pid == 0) {
        /* Child: exec shell */
        const char *shell = getenv("SHELL");
        if (!shell || !shell[0]) shell = "/bin/bash";
        /* Try in order */
        const char *shells[] = { shell, "/bin/bash", "/bin/sh", NULL };
        for (int i = 0; shells[i]; i++) {
            execlp(shells[i], shells[i], NULL);
        }
        _exit(127);
    }

    /* Parent */
    g.pty.pid       = pid;
    g.pty.master_fd = master_fd;
    g.pty.running   = true;
    g.pty.buf_len   = 0;

    /* Set master fd non-blocking */
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    return true;
}

void pty_kill(void) {
    if (!g.pty.running) return;
    kill(g.pty.pid, SIGHUP);
    int status;
    waitpid(g.pty.pid, &status, WNOHANG);
    close(g.pty.master_fd);
    g.pty.running = false;
    g.pty.pid = 0;
    g.pty.master_fd = -1;
}

void pty_write(const char *buf, int len) {
    if (!g.pty.running || len <= 0) return;
    (void)write(g.pty.master_fd, buf, len);
}

void pty_read_pending(void) {
    if (!g.pty.running) return;
    char tmp[4096];
    ssize_t n;
    while ((n = read(g.pty.master_fd, tmp, sizeof(tmp))) > 0) {
        /* Append to pty buffer, dropping oldest if full */
        int avail = (int)sizeof(g.pty.buf) - g.pty.buf_len;
        if (avail < (int)n) {
            /* Shift out old data */
            int shift = (int)n - avail;
            memmove(g.pty.buf, g.pty.buf + shift, g.pty.buf_len - shift);
            g.pty.buf_len -= shift;
        }
        memcpy(g.pty.buf + g.pty.buf_len, tmp, n);
        g.pty.buf_len += (int)n;
        g.needs_redraw = true;
    }
    /* Check if child died */
    if (n == 0 || (n < 0 && errno == EIO)) {
        int status;
        waitpid(g.pty.pid, &status, WNOHANG);
        g.pty.running = false;
    }
}

void pty_resize(int rows, int cols) {
    if (!g.pty.running) return;
    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
        .ws_xpixel = 0, .ws_ypixel = 0
    };
    ioctl(g.pty.master_fd, TIOCSWINSZ, &ws);
    kill(g.pty.pid, SIGWINCH);
    g.pty.rows = rows;
    g.pty.cols = cols;
}

void terminal_handle_event(InputEvent *ev) {
    if (!g.pty.running) {
        /* Try to re-spawn on any key */
        pty_spawn();
        return;
    }

    char buf[16];
    int n = 0;

    if (ev->is_mouse) return;

    switch (ev->key) {
        case KEY_ENTER:       buf[0] = '\r'; n = 1; break;
        case KEY_BACKSPACE:   buf[0] = 127;  n = 1; break;
        case KEY_TAB:         buf[0] = '\t'; n = 1; break;
        case KEY_ESCAPE:      buf[0] = 0x1b; n = 1; break;
        case KEY_ARROW_UP:    memcpy(buf,"\x1b[A",3); n=3; break;
        case KEY_ARROW_DOWN:  memcpy(buf,"\x1b[B",3); n=3; break;
        case KEY_ARROW_RIGHT: memcpy(buf,"\x1b[C",3); n=3; break;
        case KEY_ARROW_LEFT:  memcpy(buf,"\x1b[D",3); n=3; break;
        case KEY_HOME:        memcpy(buf,"\x1b[H",3); n=3; break;
        case KEY_END:         memcpy(buf,"\x1b[F",3); n=3; break;
        case KEY_PAGE_UP:     memcpy(buf,"\x1b[5~",4); n=4; break;
        case KEY_PAGE_DOWN:   memcpy(buf,"\x1b[6~",4); n=4; break;
        case KEY_DELETE:      memcpy(buf,"\x1b[3~",4); n=4; break;
        case KEY_F1:          memcpy(buf,"\x1bOP",3); n=3; break;
        case KEY_F2:          memcpy(buf,"\x1bOQ",3); n=3; break;
        case KEY_F3:          memcpy(buf,"\x1bOR",3); n=3; break;
        case KEY_F4:          memcpy(buf,"\x1bOS",3); n=3; break;
        default:
            if (ev->key > 0 && ev->key < 32) {
                buf[0] = (char)ev->key; n = 1;
            } else if (ev->codepoint) {
                uint32_t cp = ev->codepoint;
                if (cp < 0x80) { buf[0]=(char)cp; n=1; }
                else if (cp < 0x800) {
                    buf[0]=(char)(0xC0|(cp>>6));
                    buf[1]=(char)(0x80|(cp&0x3F)); n=2;
                } else {
                    buf[0]=(char)(0xE0|(cp>>12));
                    buf[1]=(char)(0x80|((cp>>6)&0x3F));
                    buf[2]=(char)(0x80|(cp&0x3F)); n=3;
                }
            }
            break;
    }
    if (n > 0) pty_write(buf, n);
}
