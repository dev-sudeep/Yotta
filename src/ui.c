#include "ui.h"
#include "types.h"
#include "editor.h"
#include "explorer.h"
#include "highlight.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ── Double buffer ── */
static Cell *front_buf = NULL;
static Cell *back_buf  = NULL;
static int   buf_rows  = 0;
static int   buf_cols  = 0;

/* Output write buffer for batched writes */
#define OUT_BUF_SIZE (1 << 20)  /* 1 MB */
static char   out_buf[OUT_BUF_SIZE];
static size_t out_pos = 0;

static void out_flush_raw(void) {
    if (out_pos > 0) {
        fwrite(out_buf, 1, out_pos, stdout);
        out_pos = 0;
    }
}

static void out_write(const char *s, size_t n) {
    if (out_pos + n >= OUT_BUF_SIZE) out_flush_raw();
    memcpy(out_buf + out_pos, s, n);
    out_pos += n;
}

static void out_puts(const char *s) {
    out_write(s, strlen(s));
}

/* out_putc reserved for future use */
#if 0
static void out_putc(char c) {
    if (out_pos + 1 >= OUT_BUF_SIZE) out_flush_raw();
    out_buf[out_pos++] = c;
}
#endif

/* encode a unicode codepoint to UTF-8 into dst, return byte count */
static int encode_utf8(char *dst, uint32_t cp) {
    if (cp < 0x80) {
        dst[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        dst[0] = (char)(0xF0 | (cp >> 18));
        dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

/* ── Init / Free ── */
void ui_init(void) {
    out_pos = 0;
}

void ui_free(void) {
    free(front_buf);
    free(back_buf);
    front_buf = back_buf = NULL;
}

void ui_resize(int rows, int cols) {
    free(front_buf);
    free(back_buf);
    buf_rows = rows;
    buf_cols = cols;
    size_t n = (size_t)rows * cols;
    front_buf = calloc(n, sizeof(Cell));
    back_buf  = calloc(n, sizeof(Cell));
    /* mark front as dirty so full redraw happens */
    for (size_t i = 0; i < n; i++) {
        front_buf[i].ch = 1; /* force mismatch */
    }
}

/* ── Cell write ── */
void ui_put_cell(int row, int col, uint32_t ch,
                 const char *fg, const char *bg,
                 bool bold, bool italic) {
    if (row < 0 || row >= buf_rows || col < 0 || col >= buf_cols) return;
    Cell *c = &back_buf[row * buf_cols + col];
    c->ch   = ch;
    c->bold   = bold;
    c->italic = italic;
    strncpy(c->fg, fg ? fg : COL_FG,  sizeof(c->fg) - 1);
    c->fg[sizeof(c->fg) - 1] = '\0';
    strncpy(c->bg, bg ? bg : COL_BG,  sizeof(c->bg) - 1);
    c->bg[sizeof(c->bg) - 1] = '\0';
}

int ui_put_str(int row, int col, const char *s,
               const char *fg, const char *bg,
               bool bold, bool italic) {
    int start = col;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && col < buf_cols) {
        uint32_t cp;
        int len;
        if (*p < 0x80) {
            cp = *p; len = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = (*p & 0x1F); len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = (*p & 0x0F); len = 3;
        } else {
            cp = (*p & 0x07); len = 4;
        }
        for (int i = 1; i < len && p[i]; i++)
            cp = (cp << 6) | (p[i] & 0x3F);
        p += len;
        ui_put_cell(row, col, cp, fg, bg, bold, italic);
        col++;
    }
    return col - start;
}

void ui_fill_rect(int row, int col, int w, int h, const char *bg) {
    for (int r = row; r < row + h && r < buf_rows; r++)
        for (int c2 = col; c2 < col + w && c2 < buf_cols; c2++)
            ui_put_cell(r, c2, ' ', COL_FG, bg, false, false);
}

/* ── Box drawing ── */
/* Box chars: single line */
#define BOX_TL  0x250C  /* ┌ */
#define BOX_TR  0x2510  /* ┐ */
#define BOX_BL  0x2514  /* └ */
#define BOX_BR  0x2518  /* ┘ */
#define BOX_H   0x2500  /* ─ */
#define BOX_V   0x2502  /* │ */
/* Bold / active box chars */
#define BOX_TL_B 0x2554 /* ╔ */
#define BOX_TR_B 0x2557 /* ╗ */
#define BOX_BL_B 0x255A /* ╚ */
#define BOX_BR_B 0x255D /* ╝ */
#define BOX_H_B  0x2550 /* ═ */
#define BOX_V_B  0x2551 /* ║ */

void ui_draw_box(int row, int col, int w, int h,
                 const char *fg, const char *bg, bool active) {
    uint32_t tl = active ? BOX_TL_B : BOX_TL;
    uint32_t tr = active ? BOX_TR_B : BOX_TR;
    uint32_t bl = active ? BOX_BL_B : BOX_BL;
    uint32_t br = active ? BOX_BR_B : BOX_BR;
    uint32_t hh = active ? BOX_H_B  : BOX_H;
    uint32_t vv = active ? BOX_V_B  : BOX_V;

    ui_put_cell(row, col, tl, fg, bg, false, false);
    ui_put_cell(row, col+w-1, tr, fg, bg, false, false);
    ui_put_cell(row+h-1, col, bl, fg, bg, false, false);
    ui_put_cell(row+h-1, col+w-1, br, fg, bg, false, false);
    for (int c2 = col+1; c2 < col+w-1; c2++) {
        ui_put_cell(row, c2, hh, fg, bg, false, false);
        ui_put_cell(row+h-1, c2, hh, fg, bg, false, false);
    }
    for (int r = row+1; r < row+h-1; r++) {
        ui_put_cell(r, col, vv, fg, bg, false, false);
        ui_put_cell(r, col+w-1, vv, fg, bg, false, false);
    }
}

void ui_hline(int row, int col, int len, const char *fg, const char *bg) {
    for (int i = 0; i < len; i++)
        ui_put_cell(row, col + i, BOX_H, fg, bg, false, false);
}

void ui_vline(int row, int col, int len, const char *fg, const char *bg) {
    for (int i = 0; i < len; i++)
        ui_put_cell(row + i, col, BOX_V, fg, bg, false, false);
}

/* ── Flush ── */
void ui_flush(void) {
    /* track current terminal attributes to minimize output */
    char cur_fg[32] = {0};
    char cur_bg[32] = {0};
    bool cur_bold   = false;
    bool cur_italic = false;
    int  cur_row    = -1;
    int  cur_col    = -1;

    char tmp[16];
    int  tlen;

    for (int r = 0; r < buf_rows; r++) {
        for (int c = 0; c < buf_cols; c++) {
            Cell *bc = &back_buf[r * buf_cols + c];
            Cell *fc = &front_buf[r * buf_cols + c];

            /* skip if identical */
            if (bc->ch == fc->ch &&
                strcmp(bc->fg, fc->fg) == 0 &&
                strcmp(bc->bg, fc->bg) == 0 &&
                bc->bold   == fc->bold &&
                bc->italic == fc->italic) continue;

            /* copy to front */
            *fc = *bc;

            /* reposition cursor if needed */
            if (r != cur_row || c != cur_col) {
                tlen = snprintf(tmp, sizeof(tmp), "\x1b[%d;%dH", r + 1, c + 1);
                out_write(tmp, tlen);
                cur_row = r; cur_col = c;
            }

            /* emit attribute changes */
            bool attr_changed = (bc->bold != cur_bold || bc->italic != cur_italic);
            bool fg_changed   = strcmp(bc->fg, cur_fg) != 0;
            bool bg_changed   = strcmp(bc->bg, cur_bg) != 0;

            if (attr_changed) {
                out_puts("\x1b[0m"); /* reset */
                cur_bold = cur_italic = false;
                fg_changed = bg_changed = true; /* force re-emit */
            }
            if (bg_changed) {
                out_puts(bc->bg);
                strncpy(cur_bg, bc->bg, sizeof(cur_bg) - 1);
                cur_bg[sizeof(cur_bg) - 1] = '\0';
            }
            if (fg_changed) {
                out_puts(bc->fg);
                strncpy(cur_fg, bc->fg, sizeof(cur_fg) - 1);
                cur_fg[sizeof(cur_fg) - 1] = '\0';
            }
            if (bc->bold   && !cur_bold)   { out_puts("\x1b[1m"); cur_bold   = true; }
            if (bc->italic && !cur_italic) { out_puts("\x1b[3m"); cur_italic = true; }

            /* emit character */
            tlen = encode_utf8(tmp, bc->ch ? bc->ch : ' ');
            out_write(tmp, tlen);
            cur_col++;
        }
    }

    out_puts("\x1b[0m"); /* final reset */
    out_flush_raw();
    fflush(stdout);
}

void ui_clear_screen(void) {
    out_puts("\x1b[2J\x1b[H");
    out_flush_raw();
}

void ui_cursor_show(int row, int col) {
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH\x1b[?25h", row, col);
    out_puts(buf);
    out_flush_raw();
}

void ui_cursor_hide(void) {
    out_puts("\x1b[?25l");
    out_flush_raw();
}

void ui_mouse_enable(void) {
    /* Enable mouse tracking: X10, button motion, and SGR extended coordinates */
    out_puts("\x1b[?1000h\x1b[?1002h\x1b[?1006h");
    out_flush_raw();
}

void ui_mouse_disable(void) {
    out_puts("\x1b[?1000l\x1b[?1002l\x1b[?1006l");
    out_flush_raw();
}

void ui_osc_background(void) {
    /* OSC 11: set background color hint (soft dark) */
    out_puts("\x1b]11;rgb:12/12/1c\x1b\\");
    out_flush_raw();
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Layout computation                                                         */
/* ─────────────────────────────────────────────────────────────────────────── */

static void compute_layout(void) {
    int rows = g.term_rows;
    int cols = g.term_cols;

    /* Reserve 1 row for tab bar, 1 for status bar */
    int top    = 2;          /* tab-bar row = 1, content starts at row 2 */
    int bottom = rows - 1;   /* status bar at row rows */
    int avail_rows = bottom - top;   /* rows available for panes */

    /* Terminal pane height */
    int term_h = g.terminal_visible ? (avail_rows / 3) : 0;
    int main_h = avail_rows - term_h;

    /* Horizontal split */
    int explorer_w = g.explorer_visible ? 26 : 0;
    int chat_w     = g.chat_visible     ? 32 : 0;
    int editor_w   = cols - explorer_w - chat_w;

    /* Explorer */
    g.panes[PANE_EXPLORER].x = 1;
    g.panes[PANE_EXPLORER].y = top;
    g.panes[PANE_EXPLORER].w = explorer_w;
    g.panes[PANE_EXPLORER].h = main_h;
    g.panes[PANE_EXPLORER].visible = g.explorer_visible && explorer_w > 0;

    /* Editor */
    g.panes[PANE_EDITOR].x = 1 + explorer_w;
    g.panes[PANE_EDITOR].y = top;
    g.panes[PANE_EDITOR].w = editor_w;
    g.panes[PANE_EDITOR].h = main_h;
    g.panes[PANE_EDITOR].visible = (editor_w > 0);

    /* Chat */
    g.panes[PANE_CHAT].x = 1 + explorer_w + editor_w;
    g.panes[PANE_CHAT].y = top;
    g.panes[PANE_CHAT].w = chat_w;
    g.panes[PANE_CHAT].h = main_h;
    g.panes[PANE_CHAT].visible = g.chat_visible && chat_w > 0;

    /* Terminal */
    g.panes[PANE_TERMINAL].x = 1;
    g.panes[PANE_TERMINAL].y = top + main_h;
    g.panes[PANE_TERMINAL].w = cols;
    g.panes[PANE_TERMINAL].h = term_h;
    g.panes[PANE_TERMINAL].visible = g.terminal_visible && term_h > 0;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Helper: truncate string to fit width, writing into back buffer             */
/* ─────────────────────────────────────────────────────────────────────────── */
static int put_truncated(int row, int col, int max_w,
                         const char *s,
                         const char *fg, const char *bg,
                         bool bold, bool italic) {
    int written = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && written < max_w) {
        uint32_t cp;
        int len;
        if (*p < 0x80) { cp = *p; len = 1; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; len = 2; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; len = 3; }
        else { cp = *p & 0x07; len = 4; }
        for (int i = 1; i < len && p[i]; i++) cp = (cp << 6) | (p[i] & 0x3F);
        p += len;
        ui_put_cell(row, col + written, cp, fg, bg, bold, italic);
        written++;
    }
    /* pad */
    while (written < max_w) {
        ui_put_cell(row, col + written, ' ', fg, bg, false, false);
        written++;
    }
    return written;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Tab bar                                                                    */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render_tab_bar(void) {
    int row = 1; /* 1-based row 1 */
    int cols = g.term_cols;

    /* Fill tab bar background */
    ui_fill_rect(row - 1, 0, cols, 1, COL_BG_TAB);

    /* App name */
    ui_put_str(row - 1, 0, " Yotta ", COL_FG_ACCENT, COL_BG_TAB, true, false);
    int col = 7;

    /* Draw tabs */
    for (int i = 0; i < g.tab_count && col < cols - 2; i++) {
        Tab *t = &g.tabs[i];
        bool active = (i == g.active_tab);
        const char *tbg = active ? COL_BG_TAB_ACT : COL_BG_TAB;
        const char *tfg = active ? COL_FG_BRIGHT : COL_FG_DIM;

        char label[64];
        const char *name = t->title[0] ? t->title : "untitled";
        /* get basename */
        const char *slash = strrchr(name, '/');
        if (slash) name = slash + 1;
        snprintf(label, sizeof(label), " %s%s ", name, t->buf.modified ? " ●" : "");

        int llen = (int)strlen(label);
        if (col + llen > cols - 2) llen = cols - 2 - col;
        if (llen <= 0) break;

        for (int j = 0; j < llen; j++) {
            ui_put_cell(row - 1, col + j, (unsigned char)label[j],
                        tfg, tbg, active, false);
        }
        col += llen;
        /* separator */
        if (col < cols - 2)
            ui_put_cell(row - 1, col++, BOX_V, COL_FG_BORDER, COL_BG_TAB, false, false);
    }
    /* fill rest */
    while (col < cols)
        ui_put_cell(row - 1, col++, ' ', COL_FG, COL_BG_TAB, false, false);
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Status bar                                                                 */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render_status_bar(void) {
    int row = g.term_rows; /* last row (1-based) */
    int cols = g.term_cols;

    ui_fill_rect(row - 1, 0, cols, 1, COL_BG_STATUS);

    /* Mode badge */
    const char *mode_str = "NORMAL";
    const char *mode_fg  = COL_FG_ACCENT;
    switch (g.mode) {
        case MODE_INSERT:  mode_str = "INSERT";  mode_fg = COL_FG_GREEN;   break;
        case MODE_VISUAL:  mode_str = "VISUAL";  mode_fg = COL_FG_MAGENTA; break;
        case MODE_COMMAND: mode_str = "COMMAND"; mode_fg = COL_FG_YELLOW;  break;
        case MODE_SEARCH:  mode_str = "SEARCH";  mode_fg = COL_FG_CYAN;    break;
        default: break;
    }
    char left[256];
    snprintf(left, sizeof(left), " %s ", mode_str);
    int lw = ui_put_str(row - 1, 0, left, mode_fg, COL_BG_STATUS, true, false);

    /* Git branch */
    if (g.in_git_repo && g.git_branch[0]) {
        char branch[64];
        snprintf(branch, sizeof(branch), "  %s ", g.git_branch);
        lw += ui_put_str(row - 1, lw, branch, COL_FG_GREEN, COL_BG_STATUS, false, false);
    }

    /* Status message or command buffer */
    if (g.mode == MODE_COMMAND || g.mode == MODE_SEARCH) {
        char prompt[1024];
        snprintf(prompt, sizeof(prompt), "%s%s",
                 g.mode == MODE_COMMAND ? ":" : "/", g.cmd_buf);
        ui_put_str(row - 1, lw, prompt, COL_FG, COL_BG_STATUS, false, false);
    } else if (g.status_msg[0]) {
        ui_put_str(row - 1, lw, g.status_msg, COL_FG_DIM, COL_BG_STATUS, false, false);
    }

    /* Right side: file + cursor */
    if (g.active_tab >= 0 && g.active_tab < g.tab_count) {
        Tab *t = &g.tabs[g.active_tab];
        const char *fname = t->title[0] ? t->title : "untitled";
        const char *slash = strrchr(fname, '/');
        if (slash) fname = slash + 1;
        char right[128];
        snprintf(right, sizeof(right), " %s  %d:%d  %s ",
                 fname,
                 t->cursor_row + 1, t->cursor_col + 1,
                 t->buf.lang[0] ? t->buf.lang : "plain");
        int rlen = (int)strlen(right);
        int rstart = cols - rlen;
        if (rstart > lw)
            ui_put_str(row - 1, rstart, right, COL_FG_DIM, COL_BG_STATUS, false, false);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Explorer pane                                                              */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render_explorer(void) {
    PaneGeom *p = &g.panes[PANE_EXPLORER];
    if (!p->visible) return;

    bool active = (g.active_pane == PANE_EXPLORER);
    const char *border_fg = active ? COL_FG_ACCENT : COL_FG_BORDER;

    /* Fill background */
    ui_fill_rect(p->y - 1, p->x - 1, p->w, p->h, COL_BG_PANEL);

    /* Title bar */
    char title[64];
    snprintf(title, sizeof(title), " EXPLORER ");
    put_truncated(p->y - 1, p->x - 1, p->w, title,
                  COL_FG_ACCENT, COL_BG_PANEL, true, false);

    /* Right border (vertical line) */
    for (int r = p->y - 1; r < p->y - 1 + p->h; r++)
        ui_put_cell(r, p->x - 1 + p->w - 1, BOX_V, border_fg, COL_BG_PANEL, false, false);

    /* File nodes */
    int inner_w = p->w - 2; /* leave 1 for border */
    int visible_h = p->h - 1; /* below title */
    int start = g.explorer_scroll;

    for (int i = 0; i < visible_h && (start + i) < g.explorer_count; i++) {
        int ni = start + i;
        FileNode *n = &g.explorer_nodes[ni];
        int row = p->y + i; /* 1-based */

        bool sel = (ni == g.explorer_sel);
        const char *bg = sel ? COL_BG_SEL : COL_BG_PANEL;

        /* Indent */
        int indent = n->depth * 2;
        /* fill leading spaces */
        for (int x = 0; x < indent && x < inner_w; x++)
            ui_put_cell(row - 1, p->x - 1 + x, ' ', COL_FG, bg, false, false);

        /* Icon */
        const char *icon;
        const char *icon_fg;
        if (n->is_dir) {
            icon    = n->expanded ? "▾ " : "▸ ";
            icon_fg = COL_FG_ACCENT;
        } else {
            icon    = "  ";
            icon_fg = COL_FG;
        }

        /* Git status indicator */
        const char *git_fg = COL_FG;
        char git_ch = ' ';
        switch (n->git_status) {
            case GIT_STATUS_MODIFIED:  git_fg = COL_FG_MODIFIED; git_ch = 'M'; break;
            case GIT_STATUS_UNTRACKED: git_fg = COL_FG_UNTRACK;  git_ch = '?'; break;
            case GIT_STATUS_STAGED:    git_fg = COL_FG_STAGED;   git_ch = 'S'; break;
            case GIT_STATUS_DELETED:   git_fg = COL_FG_RED;      git_ch = 'D'; break;
            case GIT_STATUS_CONFLICT:  git_fg = COL_FG_RED;      git_ch = '!'; break;
            default: break;
        }

        int col = p->x - 1 + indent;
        /* icon */
        if (col < p->x - 1 + inner_w - 1) {
            ui_put_cell(row - 1, col++, (unsigned char)icon[0], icon_fg, bg, false, false);
            if (icon[1] && col < p->x - 1 + inner_w - 1)
                ui_put_cell(row - 1, col++, (unsigned char)icon[1], icon_fg, bg, false, false);
        }

        /* name */
        const char *fg_name = n->is_dir ? COL_FG_BRIGHT : COL_FG;
        int max_name = p->x - 1 + inner_w - 2 - col;
        if (max_name > 0) {
            put_truncated(row - 1, col, max_name, n->name, fg_name, bg, false, false);
        }
        /* git indicator at right edge */
        if (git_ch != ' ') {
            int gc = p->x - 1 + inner_w - 2;
            if (gc >= col)
                ui_put_cell(row - 1, gc, git_ch, git_fg, bg, true, false);
        }
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Editor pane                                                                */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render_editor(void) {
    PaneGeom *p = &g.panes[PANE_EDITOR];
    if (!p->visible) return;

    bool active = (g.active_pane == PANE_EDITOR);
    const char *border_fg = active ? COL_FG_ACCENT : COL_FG_BORDER;

    /* Fill background */
    ui_fill_rect(p->y - 1, p->x - 1, p->w, p->h, COL_BG);

    if (g.tab_count == 0) {
        /* Empty editor: show welcome */
        int mid = p->y - 1 + p->h / 2;
        int cx  = p->x - 1 + (p->w - 32) / 2;
        if (cx < p->x - 1) cx = p->x - 1;
        ui_put_str(mid, cx, "  Welcome to Yotta  ",
                   COL_FG_ACCENT, COL_BG, true, false);
        ui_put_str(mid + 1, cx, "  Ctrl+P: Open file ",
                   COL_FG_DIM, COL_BG, false, false);
        return;
    }

    if (g.active_tab < 0 || g.active_tab >= g.tab_count) return;
    Tab *tab = &g.tabs[g.active_tab];
    Buffer *buf = &tab->buf;

    /* Line number gutter width */
    int lnum_w = 0;
    {
        int n = buf->num_lines;
        while (n > 0) { lnum_w++; n /= 10; }
        if (lnum_w < 3) lnum_w = 3;
        lnum_w += 1; /* space after number */
    }

    int text_x  = p->x - 1 + lnum_w;  /* 0-based col for text start */
    int text_w  = p->w - lnum_w;
    int text_h  = p->h;

    /* Ensure scroll is valid */
    if (tab->cursor_row < tab->scroll_row)
        tab->scroll_row = tab->cursor_row;
    if (tab->cursor_row >= tab->scroll_row + text_h)
        tab->scroll_row = tab->cursor_row - text_h + 1;
    if (tab->cursor_col < tab->scroll_col)
        tab->scroll_col = tab->cursor_col;
    if (tab->cursor_col >= tab->scroll_col + text_w)
        tab->scroll_col = tab->cursor_col - text_w + 1;

    /* Build highlighted tokens for visible lines */
    for (int li = 0; li < text_h; li++) {
        int row_idx = tab->scroll_row + li;
        int screen_row = p->y - 1 + li;

        /* Line number */
        if (row_idx < buf->num_lines) {
            char lnum[16];
            snprintf(lnum, sizeof(lnum), "%*d ", lnum_w - 1, row_idx + 1);
            for (int i = 0; (size_t)i < strlen(lnum); i++) {
                bool is_cur = (row_idx == tab->cursor_row);
                ui_put_cell(screen_row, p->x - 1 + i, (unsigned char)lnum[i],
                            is_cur ? COL_FG : COL_FG_LNUM, COL_BG, false, false);
            }
        } else {
            /* Tilde for lines past end */
            ui_put_cell(screen_row, p->x - 1, '~', COL_FG_LNUM, COL_BG, false, false);
            for (int i = 1; i < lnum_w; i++)
                ui_put_cell(screen_row, p->x - 1 + i, ' ', COL_FG, COL_BG, false, false);
        }

        /* Text content */
        if (row_idx >= buf->num_lines) {
            /* fill rest of line */
            for (int c2 = 0; c2 < text_w; c2++)
                ui_put_cell(screen_row, text_x + c2, ' ', COL_FG, COL_BG, false, false);
            continue;
        }

        Line *line = &buf->lines[row_idx];

        /* Get syntax tokens */
        HlToken tokens[MAX_LINE_LEN];
        int ntok = highlight_line(buf->lang, line->data, line->len, tokens, MAX_LINE_LEN);

        /* Render tokens */
        int byte_off = tab->scroll_col;
        int screen_col = 0;

        /* Find which token covers byte_off */
        for (int ti = 0; ti < ntok && screen_col < text_w; ti++) {
            HlToken *tok = &tokens[ti];
            /* skip tokens before scroll */
            if (tok->start + tok->len <= byte_off) continue;

            const char *col_fg = hl_token_color(tok->type);
            bool bold   = hl_token_bold(tok->type);
            bool italic = hl_token_italic(tok->type);

            /* bytes in this token from byte_off */
            int ts = tok->start > byte_off ? tok->start : byte_off;
            const char *text_ptr = line->data + ts;
            int text_bytes = tok->len - (ts - tok->start);

            const unsigned char *p2 = (const unsigned char *)text_ptr;
            int bytes_left = text_bytes;
            while (bytes_left > 0 && screen_col < text_w) {
                uint32_t cp;
                int blen;
                if (*p2 < 0x80) { cp = *p2; blen = 1; }
                else if ((*p2 & 0xE0) == 0xC0) { cp = *p2 & 0x1F; blen = 2; }
                else if ((*p2 & 0xF0) == 0xE0) { cp = *p2 & 0x0F; blen = 3; }
                else { cp = *p2 & 0x07; blen = 4; }
                for (int i = 1; i < blen && bytes_left > i; i++)
                    cp = (cp << 6) | (p2[i] & 0x3F);
                p2 += blen; bytes_left -= blen;

                /* cursor highlight */
                bool is_cursor = (row_idx == tab->cursor_row &&
                                  (int)(text_ptr - line->data) + (int)(p2 - (const unsigned char *)text_ptr) - blen == tab->cursor_col);
                const char *cell_bg = is_cursor ? COL_BG_CURSOR : COL_BG;
                const char *cell_fg = is_cursor ? COL_FG_BRIGHT : col_fg;

                if (cp == '\t') {
                    /* expand tab to 4 spaces */
                    for (int s = 0; s < 4 && screen_col < text_w; s++)
                        ui_put_cell(screen_row, text_x + screen_col++, ' ', cell_fg, cell_bg, bold, italic);
                } else {
                    ui_put_cell(screen_row, text_x + screen_col++, cp, cell_fg, cell_bg, bold, italic);
                }
                text_ptr = (const char *)p2; /* advance for cursor check */
            }
        }
        /* Fill rest of line */
        while (screen_col < text_w) {
            bool is_cursor = (row_idx == tab->cursor_row &&
                              screen_col + tab->scroll_col >= line->len);
            const char *cell_bg = (is_cursor && screen_col + tab->scroll_col == line->len)
                                  ? COL_BG_CURSOR : COL_BG;
            ui_put_cell(screen_row, text_x + screen_col++, ' ', COL_FG, cell_bg, false, false);
        }
    }

    /* Right border */
    if (g.chat_visible) {
        for (int r = p->y - 1; r < p->y - 1 + p->h; r++)
            ui_put_cell(r, p->x - 1 + p->w - 1, BOX_V, border_fg, COL_BG, false, false);
    }
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Chat pane                                                                  */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render_chat(void) {
    PaneGeom *p = &g.panes[PANE_CHAT];
    if (!p->visible) return;

    bool active = (g.active_pane == PANE_CHAT);
    const char *border_fg = active ? COL_FG_ACCENT : COL_FG_BORDER;

    ui_fill_rect(p->y - 1, p->x - 1, p->w, p->h, COL_BG_PANEL);

    /* Title */
    put_truncated(p->y - 1, p->x - 1, p->w, " ✦ Copilot Chat ",
                  COL_FG_ACCENT, COL_BG_PANEL, true, false);
    ui_put_cell(p->y - 1, p->x - 1, BOX_V, border_fg, COL_BG_PANEL, false, false);

    /* Left border */
    for (int r = p->y - 1; r < p->y - 1 + p->h; r++)
        ui_put_cell(r, p->x - 1, BOX_V, border_fg, COL_BG_PANEL, false, false);

    int inner_x = p->x;      /* 0-based */
    int inner_w = p->w - 1;

    /* Input box at bottom (2 rows) */
    int input_row = p->y - 1 + p->h - 2;
    ui_hline(input_row, inner_x - 1, inner_w + 1, border_fg, COL_BG_PANEL);
    put_truncated(input_row + 1, inner_x - 1, inner_w + 1, g.chat_input,
                  COL_FG, COL_BG_PANEL, false, false);

    /* Messages */
    int msg_h = p->h - 3;
    int avail = msg_h;

    /* Collect lines for visible messages (simple word-wrap) */
    /* We render bottom-up */
    int mi = g.chat_count - 1 - g.chat_scroll;
    int r  = p->y - 1 + msg_h;

    while (r > p->y - 1 && mi >= 0) {
        ChatMessage *msg = &g.chat_msgs[mi--];
        const char *role_fg = msg->is_user ? COL_FG_ACCENT : COL_FG_GREEN;
        const char *role    = msg->is_user ? " You: " : " AI:  ";

        /* Simple line wrap */
        const char *txt = msg->text ? msg->text : "";
        int tlen = (int)strlen(txt);
        int lw = inner_w - 1;
        if (lw < 4) lw = 4;
        int nlines = (tlen + lw - 1) / lw;
        if (nlines < 1) nlines = 1;

        for (int l = nlines - 1; l >= 0 && r > p->y - 1; l--) {
            r--;
            int off = l * lw;
            int chunk = tlen - off;
            if (chunk > lw) chunk = lw;
            char line_buf[MAX_LINE_LEN];
            memcpy(line_buf, txt + off, chunk);
            line_buf[chunk] = '\0';
            put_truncated(r, inner_x - 1, inner_w, line_buf,
                          COL_FG, COL_BG_PANEL, false, false);
        }
        /* Role label */
        if (r > p->y - 1) {
            r--;
            put_truncated(r, inner_x - 1, inner_w, role, role_fg, COL_BG_PANEL, true, false);
        }
    }
    (void)avail;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Terminal pane                                                              */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render_terminal(void) {
    PaneGeom *p = &g.panes[PANE_TERMINAL];
    if (!p->visible) return;

    bool active = (g.active_pane == PANE_TERMINAL);
    const char *border_fg = active ? COL_FG_ACCENT : COL_FG_BORDER;

    ui_fill_rect(p->y - 1, p->x - 1, p->w, p->h, COL_BG);

    /* Top border */
    for (int c2 = p->x - 1; c2 < p->x - 1 + p->w; c2++)
        ui_put_cell(p->y - 1, c2, BOX_H, border_fg, COL_BG, false, false);
    put_truncated(p->y - 1, p->x - 1 + 2, 12, " TERMINAL ",
                  border_fg, COL_BG, true, false);

    /* Render PTY output: g.pty.buf contains raw terminal output */
    /* Simple rendering: just show last (h-1) lines of output, split on \n */
    int inner_h = p->h - 1;
    int inner_w = p->w;
    const char *out = g.pty.buf;
    int outlen = g.pty.buf_len;

    /* Find line starts from end */
    const char *lines[256];
    int lcount = 0;
    int li2 = outlen - 1;
    while (li2 >= 0 && lcount < inner_h * 2) {
        if (out[li2] == '\n') lcount++;
        li2--;
    }
    /* Re-scan from that position */
    const char *cur = out + li2 + 1;
    const char *end = out + outlen;
    lcount = 0;
    const char *lstart = cur;
    while (cur <= end && lcount < inner_h) {
        if (cur == end || *cur == '\n') {
            lines[lcount++] = lstart;
            lstart = cur + 1;
        }
        cur++;
    }

    for (int l = 0; l < lcount; l++) {
        int row2 = p->y + l; /* 1-based */
        const char *ls = lines[l];
        const char *le = (l + 1 < lcount) ? lines[l + 1] - 1 : end;
        int col2 = 0;
        while (ls < le && col2 < inner_w) {
            unsigned char c2 = (unsigned char)*ls++;
            if (c2 == '\r') continue;
            if (c2 < 0x80)
                ui_put_cell(row2 - 1, p->x - 1 + col2++, c2, COL_FG, COL_BG, false, false);
            else {
                /* multi-byte */
                uint32_t cp = c2;
                int blen = (c2 & 0xE0) == 0xC0 ? 2 :
                           (c2 & 0xF0) == 0xE0 ? 3 :
                           (c2 & 0xF8) == 0xF0 ? 4 : 1;
                for (int bi = 1; bi < blen && ls < le; bi++)
                    cp = (cp << 6) | ((unsigned char)*ls++ & 0x3F);
                ui_put_cell(row2 - 1, p->x - 1 + col2++, cp, COL_FG, COL_BG, false, false);
            }
        }
        while (col2 < inner_w)
            ui_put_cell(row2 - 1, p->x - 1 + col2++, ' ', COL_FG, COL_BG, false, false);
    }
    (void)border_fg;
}

/* ─────────────────────────────────────────────────────────────────────────── */
/*  Master render                                                              */
/* ─────────────────────────────────────────────────────────────────────────── */
void ui_render(void) {
    compute_layout();

    /* Clear back buffer */
    size_t n = (size_t)buf_rows * buf_cols;
    for (size_t i = 0; i < n; i++) {
        back_buf[i].ch = ' ';
        strncpy(back_buf[i].fg, COL_FG, sizeof(back_buf[i].fg) - 1);
        back_buf[i].fg[sizeof(back_buf[i].fg) - 1] = '\0';
        strncpy(back_buf[i].bg, COL_BG, sizeof(back_buf[i].bg) - 1);
        back_buf[i].bg[sizeof(back_buf[i].bg) - 1] = '\0';
        back_buf[i].bold = back_buf[i].italic = false;
    }

    ui_render_tab_bar();
    ui_render_explorer();
    ui_render_editor();
    ui_render_chat();
    ui_render_terminal();
    ui_render_status_bar();

    ui_flush();
    ui_cursor_hide();
    g.needs_redraw = false;
}
