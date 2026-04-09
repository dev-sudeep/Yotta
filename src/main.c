/*
 * Yotta — A modern terminal-based code editor
 * Main entry point and event loop
 */

#include "types.h"
#include "ui.h"
#include "input.h"
#include "editor.h"
#include "explorer.h"
#include "git.h"
#include "terminal.h"
#include "lsp.h"
#include "chat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <errno.h>

/* ── Global state ── */
AppState g;

/* ── Signal handlers ── */
static volatile int sigwinch_pending = 0;

static void handle_sigwinch(int sig) {
    (void)sig;
    sigwinch_pending = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;
    g.quit = true;
}

static void handle_sigchld(int sig) {
    (void)sig;
    /* reap children to avoid zombies */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {}
}

/* ── Terminal size ── */
static void query_term_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        g.term_rows = ws.ws_row > 0 ? ws.ws_row : 24;
        g.term_cols = ws.ws_col > 0 ? ws.ws_col : 80;
    } else {
        g.term_rows = 24;
        g.term_cols = 80;
    }
}

/* ── Mouse click: hit-test panes ── */
static void handle_mouse_click(int x, int y) {
    /* Tab bar (row 1) */
    if (y == 1) {
        /* Switch tabs by clicking */
        int col = 7; /* after "Yotta " */
        for (int i = 0; i < g.tab_count; i++) {
            Tab *t = &g.tabs[i];
            const char *name = t->title[0] ? t->title : "untitled";
            const char *slash = strrchr(name, '/');
            if (slash) name = slash + 1;
            char label[64];
            snprintf(label, sizeof(label), " %s%s ",
                     name, t->buf.modified ? " ●" : "");
            int llen = (int)strlen(label) + 1; /* +1 for separator */
            if (x >= col && x < col + llen - 1) {
                editor_switch_tab(i);
                return;
            }
            col += llen;
        }
        return;
    }

    /* Hit-test each pane */
    for (int p = 0; p < PANE_COUNT; p++) {
        PaneGeom *pg = &g.panes[p];
        if (!pg->visible) continue;
        if (x >= pg->x && x < pg->x + pg->w &&
            y >= pg->y && y < pg->y + pg->h) {
            g.active_pane = (PaneId)p;
            if (p == PANE_EXPLORER) {
                /* Click on a file node */
                int rel_y = y - pg->y;
                int new_sel = g.explorer_scroll + rel_y;
                if (new_sel >= 0 && new_sel < g.explorer_count) {
                    g.explorer_sel = new_sel;
                    explorer_open_selected();
                }
            }
            g.needs_redraw = true;
            return;
        }
    }
}

/* ── Global keyboard shortcuts ── */
static bool handle_global_shortcut(InputEvent *ev) {
    /* Ctrl+Q: quit */
    if (ev->key == KEY_CTRL_Q) {
        g.quit = true;
        return true;
    }
    /* Ctrl+S: save */
    if (ev->key == KEY_CTRL_S) {
        editor_save();
        return true;
    }
    /* Ctrl+B: toggle explorer */
    if (ev->key == KEY_CTRL_B) {
        g.explorer_visible = !g.explorer_visible;
        g.needs_redraw = true;
        return true;
    }
    /* Ctrl+T: toggle terminal */
    if (ev->key == KEY_CTRL_T) {
        g.terminal_visible = !g.terminal_visible;
        if (g.terminal_visible && !g.pty.running) pty_spawn();
        if (g.terminal_visible) g.active_pane = PANE_TERMINAL;
        g.needs_redraw = true;
        return true;
    }
    /* Ctrl+L: focus editor */
    if (ev->key == KEY_CTRL_L) {
        g.active_pane = PANE_EDITOR;
        g.needs_redraw = true;
        return true;
    }
    /* Ctrl+P: open file search (stub: just show status) */
    if (ev->key == KEY_CTRL_P) {
        snprintf(g.status_msg, sizeof(g.status_msg),
                 "Ctrl+P: file search — type in explorer then Enter");
        g.active_pane = PANE_EXPLORER;
        g.needs_redraw = true;
        return true;
    }
    /* Ctrl+Tab: next tab */
    if (ev->key == KEY_CTRL_TAB) {
        if (g.tab_count > 1)
            editor_switch_tab((g.active_tab + 1) % g.tab_count);
        return true;
    }
    /* Shift+Ctrl+Tab (Shift+Tab in some terminals): previous tab */
    if (ev->key == KEY_SHIFT_TAB) {
        if (g.tab_count > 1)
            editor_switch_tab((g.active_tab - 1 + g.tab_count) % g.tab_count);
        return true;
    }
    /* Ctrl+W: close tab */
    if (ev->key == KEY_CTRL_W) {
        editor_close_tab(g.active_tab);
        return true;
    }
    /* Ctrl+Shift+C: toggle chat */
    if (ev->key == KEY_CTRL_SHIFT_C) {
        g.chat_visible = !g.chat_visible;
        if (g.chat_visible) g.active_pane = PANE_CHAT;
        g.needs_redraw = true;
        return true;
    }
    /* Tab key when not in insert mode / terminal: cycle pane focus */
    if (ev->key == KEY_CTRL_N) {
        int next = ((int)g.active_pane + 1) % PANE_COUNT;
        while (!g.panes[next].visible && next != (int)g.active_pane)
            next = (next + 1) % PANE_COUNT;
        g.active_pane = (PaneId)next;
        g.needs_redraw = true;
        return true;
    }
    return false;
}

/* ── Dispatch event to active pane ── */
static void dispatch_event(InputEvent *ev) {
    /* Mouse events */
    if (ev->is_mouse) {
        if (!ev->mouse.released) {
            /* Scroll */
            if (ev->mouse.button == 64) { /* scroll up */
                switch (g.active_pane) {
                    case PANE_EXPLORER: explorer_move_up(); explorer_move_up(); break;
                    case PANE_EDITOR:   editor_move_up(); editor_move_up(); break;
                    case PANE_CHAT:     if (g.chat_scroll < g.chat_count) g.chat_scroll++; g.needs_redraw = true; break;
                    default: break;
                }
            } else if (ev->mouse.button == 65) { /* scroll down */
                switch (g.active_pane) {
                    case PANE_EXPLORER: explorer_move_down(); explorer_move_down(); break;
                    case PANE_EDITOR:   editor_move_down(); editor_move_down(); break;
                    case PANE_CHAT:     if (g.chat_scroll > 0) g.chat_scroll--; g.needs_redraw = true; break;
                    default: break;
                }
            } else if (ev->mouse.button == 0) {
                handle_mouse_click(ev->mouse.x, ev->mouse.y);
            }
        }
        return;
    }

    /* Check global shortcuts first */
    if (handle_global_shortcut(ev)) return;

    /* Route to active pane */
    switch (g.active_pane) {
        case PANE_EDITOR:
            editor_handle_event(ev);
            break;
        case PANE_EXPLORER:
            explorer_handle_event(ev);
            break;
        case PANE_TERMINAL:
            terminal_handle_event(ev);
            break;
        case PANE_CHAT:
            chat_handle_event(ev);
            break;
        default:
            break;
    }
}

/* ── Cleanup on exit ── */
static void cleanup(void) {
    pty_kill();
    lsp_shutdown();
    ui_mouse_disable();
    input_restore_mode();
    ui_clear_screen();
    printf("\x1b[?25h"); /* show cursor */
    fflush(stdout);
    ui_free();
}

/* ── Print usage ── */
static void usage(const char *prog) {
    fprintf(stderr,
        "Yotta %s — A modern terminal code editor\n\n"
        "Usage: %s [file ...]\n\n"
        "Keyboard shortcuts:\n"
        "  Ctrl+Q        Quit\n"
        "  Ctrl+S        Save file\n"
        "  Ctrl+B        Toggle file explorer\n"
        "  Ctrl+T        Toggle terminal\n"
        "  Ctrl+L        Focus editor\n"
        "  Ctrl+P        Open file search\n"
        "  Ctrl+Tab      Next tab\n"
        "  Ctrl+W        Close tab\n"
        "  Ctrl+Shift+C  Toggle Copilot chat\n"
        "  Ctrl+N        Cycle pane focus\n\n"
        "Editor (Normal mode, Vim-like):\n"
        "  i/a/o/O  Enter insert mode\n"
        "  h/j/k/l  Movement\n"
        "  w/b      Word forward/backward\n"
        "  0/$      Line start/end\n"
        "  g/G      File start/end\n"
        "  x/d      Delete char/line\n"
        "  /        Search\n"
        "  :        Command mode\n"
        "  Esc      Return to normal mode\n",
        YOTTA_VERSION, prog);
}

/* ── Main ── */
int main(int argc, char *argv[]) {
    /* Handle --help */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("yotta %s\n", YOTTA_VERSION);
            return 0;
        }
    }

    /* ── Initialize global state ── */
    memset(&g, 0, sizeof(g));
    g.explorer_visible = true;
    g.terminal_visible = false;
    g.chat_visible     = true;
    g.active_pane      = PANE_EDITOR;
    g.active_tab       = -1;
    g.quit             = false;
    g.needs_redraw     = true;

    /* Get working directory */
    if (!getcwd(g.cwd, sizeof(g.cwd)))
        strncpy(g.cwd, ".", sizeof(g.cwd) - 1);

    /* ── Signal handlers ── */
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGTERM,  handle_sigterm);
    signal(SIGINT,   handle_sigterm);
    signal(SIGCHLD,  handle_sigchld);
    signal(SIGPIPE,  SIG_IGN);

    /* ── Query terminal size ── */
    query_term_size();

    /* ── Setup raw mode + mouse ── */
    input_raw_mode();
    ui_osc_background();
    ui_mouse_enable();
    ui_cursor_hide();
    ui_clear_screen();

    /* ── Init subsystems ── */
    ui_init();
    ui_resize(g.term_rows, g.term_cols);
    editor_init();
    explorer_init(g.cwd);
    git_detect(g.cwd);
    git_refresh_status();
    chat_init();

    /* ── Open files from command line ── */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            editor_open_file(argv[i]);
    }

    /* ── Main event loop ── */
    while (!g.quit) {
        /* Handle SIGWINCH */
        if (sigwinch_pending) {
            sigwinch_pending = 0;
            query_term_size();
            ui_resize(g.term_rows, g.term_cols);
            if (g.pty.running) {
                int th = g.panes[PANE_TERMINAL].h > 1 ? g.panes[PANE_TERMINAL].h - 1 : 24;
                int tw = g.panes[PANE_TERMINAL].w  > 0 ? g.panes[PANE_TERMINAL].w    : 80;
                pty_resize(th, tw);
            }
            g.needs_redraw = true;
        }

        /* Render if needed */
        if (g.needs_redraw)
            ui_render();

        /* Wait for input or PTY output (up to ~16ms for ~60fps) */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;

        if (g.pty.running && g.pty.master_fd >= 0) {
            FD_SET(g.pty.master_fd, &rfds);
            if (g.pty.master_fd > maxfd) maxfd = g.pty.master_fd;
        }
        if (g.lsp.pid > 0 && g.lsp.stdout_fd >= 0) {
            FD_SET(g.lsp.stdout_fd, &rfds);
            if (g.lsp.stdout_fd > maxfd) maxfd = g.lsp.stdout_fd;
        }

        struct timeval tv = { .tv_sec = 0, .tv_usec = 16000 }; /* ~16ms */
        int sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* PTY output */
        if (g.pty.running && g.pty.master_fd >= 0 &&
            FD_ISSET(g.pty.master_fd, &rfds)) {
            pty_read_pending();
        }

        /* LSP output */
        if (g.lsp.pid > 0 && g.lsp.stdout_fd >= 0 &&
            FD_ISSET(g.lsp.stdout_fd, &rfds)) {
            lsp_poll();
        }

        /* Keyboard / mouse input */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            InputEvent ev;
            while (input_read_event(&ev, 0)) {
                dispatch_event(&ev);
                if (g.quit) break;
                /* Clear status message on any key */
                g.status_msg[0] = '\0';
            }
        }
    }

    cleanup();
    return 0;
}
