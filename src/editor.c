#include "editor.h"
#include "highlight.h"
#include "types.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

/* ── Buffer helpers ── */

void buf_line_ensure(Line *l, int needed) {
    if (needed <= l->cap) return;
    int nc = l->cap ? l->cap * 2 : 64;
    while (nc < needed) nc *= 2;
    l->data = realloc(l->data, nc);
    if (!l->data) { perror("realloc"); exit(1); }
    l->cap = nc;
}

Buffer *buf_new(void) {
    Buffer *b = calloc(1, sizeof(Buffer));
    b->cap_lines = 64;
    b->lines = calloc(b->cap_lines, sizeof(Line));
    /* Start with one empty line */
    b->num_lines = 1;
    buf_line_ensure(&b->lines[0], 1);
    b->lines[0].data[0] = '\0';
    b->lines[0].len = 0;
    return b;
}

void buf_free(Buffer *b) {
    if (!b) return;
    for (int i = 0; i < b->num_lines; i++)
        free(b->lines[i].data);
    free(b->lines);
    free(b);
}

static void buf_ensure_lines(Buffer *b, int needed) {
    if (needed <= b->cap_lines) return;
    int nc = b->cap_lines * 2;
    while (nc < needed) nc *= 2;
    b->lines = realloc(b->lines, nc * sizeof(Line));
    memset(b->lines + b->cap_lines, 0, (nc - b->cap_lines) * sizeof(Line));
    b->cap_lines = nc;
}

bool buf_load(Buffer *b, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    /* Free existing lines */
    for (int i = 0; i < b->num_lines; i++) free(b->lines[i].data);
    b->num_lines = 0;

    char raw[MAX_LINE_LEN];
    while (fgets(raw, sizeof(raw), f)) {
        int rlen = (int)strlen(raw);
        /* strip trailing \n or \r\n */
        while (rlen > 0 && (raw[rlen-1] == '\n' || raw[rlen-1] == '\r')) rlen--;
        raw[rlen] = '\0';

        buf_ensure_lines(b, b->num_lines + 1);
        Line *l = &b->lines[b->num_lines++];
        l->len = l->cap = 0;
        l->data = NULL;
        buf_line_ensure(l, rlen + 1);
        memcpy(l->data, raw, rlen);
        l->data[rlen] = '\0';
        l->len = rlen;
        l->dirty = true;
    }
    fclose(f);
    if (b->num_lines == 0) {
        buf_ensure_lines(b, 1);
        Line *l = &b->lines[0];
        l->len = l->cap = 0; l->data = NULL;
        buf_line_ensure(l, 1);
        l->data[0] = '\0'; l->len = 0;
        b->num_lines = 1;
    }
    strncpy(b->filepath, filepath, MAX_PATH - 1);
    b->modified = false;
    strncpy(b->lang, hl_detect_lang(filepath), sizeof(b->lang) - 1);
    return true;
}

bool buf_save(Buffer *b) {
    if (!b->filepath[0]) return false;
    FILE *f = fopen(b->filepath, "w");
    if (!f) return false;
    for (int i = 0; i < b->num_lines; i++) {
        fwrite(b->lines[i].data, 1, b->lines[i].len, f);
        fputc('\n', f);
    }
    fclose(f);
    b->modified = false;
    return true;
}

bool buf_save_as(Buffer *b, const char *filepath) {
    strncpy(b->filepath, filepath, MAX_PATH - 1);
    strncpy(b->lang, hl_detect_lang(filepath), sizeof(b->lang) - 1);
    return buf_save(b);
}

/* Insert a Unicode codepoint (encoded as UTF-8) at byte offset col in row */
void buf_insert_char(Buffer *b, int row, int col, uint32_t cp) {
    if (row < 0 || row >= b->num_lines) return;
    Line *l = &b->lines[row];

    /* Encode cp to UTF-8 */
    char utf[4]; int ulen;
    if (cp < 0x80)         { utf[0] = (char)cp; ulen = 1; }
    else if (cp < 0x800)   { utf[0] = (char)(0xC0|(cp>>6)); utf[1]=(char)(0x80|(cp&0x3F)); ulen=2; }
    else if (cp < 0x10000) { utf[0]=(char)(0xE0|(cp>>12)); utf[1]=(char)(0x80|((cp>>6)&0x3F)); utf[2]=(char)(0x80|(cp&0x3F)); ulen=3; }
    else { utf[0]=(char)(0xF0|(cp>>18)); utf[1]=(char)(0x80|((cp>>12)&0x3F)); utf[2]=(char)(0x80|((cp>>6)&0x3F)); utf[3]=(char)(0x80|(cp&0x3F)); ulen=4; }

    col = col < 0 ? 0 : col > l->len ? l->len : col;
    buf_line_ensure(l, l->len + ulen + 1);
    memmove(l->data + col + ulen, l->data + col, l->len - col);
    memcpy(l->data + col, utf, ulen);
    l->len += ulen;
    l->data[l->len] = '\0';
    l->dirty = true;
    b->modified = true;
}

void buf_delete_char(Buffer *b, int row, int col) {
    if (row < 0 || row >= b->num_lines) return;
    Line *l = &b->lines[row];
    if (col < 0 || col >= l->len) return;
    /* find UTF-8 byte length at col */
    unsigned char c = (unsigned char)l->data[col];
    int blen = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 :
               (c & 0xF0) == 0xE0 ? 3 : 4;
    memmove(l->data + col, l->data + col + blen, l->len - col - blen);
    l->len -= blen;
    l->data[l->len] = '\0';
    l->dirty = true;
    b->modified = true;
}

void buf_insert_newline(Buffer *b, int row, int col) {
    if (row < 0 || row >= b->num_lines) return;
    Line *old = &b->lines[row];
    col = col < 0 ? 0 : col > old->len ? old->len : col;

    /* Allocate new line with tail of old line */
    buf_ensure_lines(b, b->num_lines + 1);
    /* Shift lines down */
    memmove(b->lines + row + 2, b->lines + row + 1,
            (b->num_lines - row - 1) * sizeof(Line));
    b->num_lines++;

    Line *nl = &b->lines[row + 1];
    nl->len = nl->cap = 0; nl->data = NULL;
    int tail = old->len - col;
    buf_line_ensure(nl, tail + 1);
    memcpy(nl->data, old->data + col, tail);
    nl->data[tail] = '\0';
    nl->len = tail;
    nl->dirty = true;

    /* Truncate old line */
    old->len = col;
    old->data[col] = '\0';
    old->dirty = true;
    b->modified = true;
}

void buf_delete_line(Buffer *b, int row) {
    if (row < 0 || row >= b->num_lines) return;
    free(b->lines[row].data);
    memmove(b->lines + row, b->lines + row + 1,
            (b->num_lines - row - 1) * sizeof(Line));
    b->num_lines--;
    if (b->num_lines == 0) {
        buf_ensure_lines(b, 1);
        Line *l = &b->lines[0];
        l->len = l->cap = 0; l->data = NULL;
        buf_line_ensure(l, 1);
        l->data[0] = '\0'; l->len = 0;
        b->num_lines = 1;
    }
    b->modified = true;
}

void buf_join_line(Buffer *b, int row) {
    if (row < 0 || row + 1 >= b->num_lines) return;
    Line *a = &b->lines[row];
    Line *bline = &b->lines[row + 1];
    buf_line_ensure(a, a->len + bline->len + 1);
    memcpy(a->data + a->len, bline->data, bline->len);
    a->len += bline->len;
    a->data[a->len] = '\0';
    a->dirty = true;
    free(bline->data);
    memmove(b->lines + row + 1, b->lines + row + 2,
            (b->num_lines - row - 2) * sizeof(Line));
    b->num_lines--;
    b->modified = true;
}

/* ── Editor state ── */

void editor_init(void) {
    g.tab_count = 0;
    g.active_tab = -1;
    g.mode = MODE_NORMAL;
}

int editor_open_file(const char *filepath) {
    /* Check if already open */
    for (int i = 0; i < g.tab_count; i++) {
        if (strcmp(g.tabs[i].buf.filepath, filepath) == 0) {
            editor_switch_tab(i);
            return i;
        }
    }
    if (g.tab_count >= MAX_TABS) return -1;
    int idx = g.tab_count++;
    Tab *t = &g.tabs[idx];
    memset(t, 0, sizeof(Tab));

    /* Init buffer inline (no heap) */
    t->buf.cap_lines = 64;
    t->buf.lines = calloc(t->buf.cap_lines, sizeof(Line));
    t->buf.num_lines = 1;
    buf_line_ensure(&t->buf.lines[0], 1);
    t->buf.lines[0].data[0] = '\0';
    t->buf.lines[0].len = 0;

    buf_load(&t->buf, filepath);
    strncpy(t->title, filepath, sizeof(t->title) - 1);
    t->cursor_row = t->cursor_col = 0;
    t->scroll_row = t->scroll_col = 0;
    t->active = true;

    editor_switch_tab(idx);
    return idx;
}

bool editor_close_tab(int idx) {
    if (idx < 0 || idx >= g.tab_count) return false;
    Tab *t = &g.tabs[idx];
    for (int i = 0; i < t->buf.num_lines; i++) free(t->buf.lines[i].data);
    free(t->buf.lines);
    memmove(g.tabs + idx, g.tabs + idx + 1, (g.tab_count - idx - 1) * sizeof(Tab));
    g.tab_count--;
    if (g.active_tab >= g.tab_count) g.active_tab = g.tab_count - 1;
    g.needs_redraw = true;
    return true;
}

void editor_switch_tab(int idx) {
    if (idx < 0 || idx >= g.tab_count) return;
    g.active_tab = idx;
    g.needs_redraw = true;
}

void editor_clamp_cursor(Tab *t) {
    if (t->cursor_row < 0) t->cursor_row = 0;
    if (t->cursor_row >= t->buf.num_lines) t->cursor_row = t->buf.num_lines - 1;
    Line *l = &t->buf.lines[t->cursor_row];
    if (t->cursor_col < 0) t->cursor_col = 0;
    if (t->cursor_col > l->len) t->cursor_col = l->len;
}

/* ── Cursor movement ── */

void editor_move_left(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    if (t->cursor_col > 0) {
        /* move back one UTF-8 character */
        int col = t->cursor_col - 1;
        while (col > 0 && (t->buf.lines[t->cursor_row].data[col] & 0xC0) == 0x80) col--;
        t->cursor_col = col;
    } else if (t->cursor_row > 0) {
        t->cursor_row--;
        t->cursor_col = t->buf.lines[t->cursor_row].len;
    }
    g.needs_redraw = true;
}

void editor_move_right(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    Line *l = &t->buf.lines[t->cursor_row];
    if (t->cursor_col < l->len) {
        unsigned char c = (unsigned char)l->data[t->cursor_col];
        int blen = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 :
                   (c & 0xF0) == 0xE0 ? 3 : 4;
        t->cursor_col += blen;
    } else if (t->cursor_row + 1 < t->buf.num_lines) {
        t->cursor_row++;
        t->cursor_col = 0;
    }
    g.needs_redraw = true;
}

void editor_move_up(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    if (t->cursor_row > 0) {
        t->cursor_row--;
        editor_clamp_cursor(t);
    }
    g.needs_redraw = true;
}

void editor_move_down(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    if (t->cursor_row + 1 < t->buf.num_lines) {
        t->cursor_row++;
        editor_clamp_cursor(t);
    }
    g.needs_redraw = true;
}

void editor_move_home(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    /* Jump to first non-whitespace, or col 0 if already there */
    Line *l = &t->buf.lines[t->cursor_row];
    int ws = 0;
    while (ws < l->len && (l->data[ws] == ' ' || l->data[ws] == '\t')) ws++;
    t->cursor_col = (t->cursor_col == ws) ? 0 : ws;
    g.needs_redraw = true;
}

void editor_move_end(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    t->cursor_col = t->buf.lines[t->cursor_row].len;
    g.needs_redraw = true;
}

void editor_move_page_up(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    int ph = g.panes[PANE_EDITOR].h;
    t->cursor_row -= ph;
    if (t->cursor_row < 0) t->cursor_row = 0;
    editor_clamp_cursor(t);
    g.needs_redraw = true;
}

void editor_move_page_down(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    int ph = g.panes[PANE_EDITOR].h;
    t->cursor_row += ph;
    if (t->cursor_row >= t->buf.num_lines)
        t->cursor_row = t->buf.num_lines - 1;
    editor_clamp_cursor(t);
    g.needs_redraw = true;
}

void editor_move_word_forward(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    Line *l = &t->buf.lines[t->cursor_row];
    int col = t->cursor_col;
    /* skip current word */
    while (col < l->len && (isalnum((unsigned char)l->data[col]) || l->data[col] == '_')) col++;
    /* skip whitespace */
    while (col < l->len && !isalnum((unsigned char)l->data[col]) && l->data[col] != '_') col++;
    t->cursor_col = col;
    g.needs_redraw = true;
}

void editor_move_word_backward(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    Line *l = &t->buf.lines[t->cursor_row];
    int col = t->cursor_col;
    if (col > 0) col--;
    while (col > 0 && !isalnum((unsigned char)l->data[col]) && l->data[col] != '_') col--;
    while (col > 0 && (isalnum((unsigned char)l->data[col-1]) || l->data[col-1] == '_')) col--;
    t->cursor_col = col;
    g.needs_redraw = true;
}

/* ── Editing operations ── */

void editor_insert_char(uint32_t cp) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    buf_insert_char(&t->buf, t->cursor_row, t->cursor_col, cp);

    /* Advance cursor */
    int ulen;
    if (cp < 0x80) ulen = 1;
    else if (cp < 0x800) ulen = 2;
    else if (cp < 0x10000) ulen = 3;
    else ulen = 4;
    t->cursor_col += ulen;
    g.needs_redraw = true;
}

void editor_insert_newline(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    /* Auto-indent: copy leading whitespace from current line */
    Line *l = &t->buf.lines[t->cursor_row];
    int ws = 0;
    while (ws < l->len && (l->data[ws] == ' ' || l->data[ws] == '\t')) ws++;

    buf_insert_newline(&t->buf, t->cursor_row, t->cursor_col);
    t->cursor_row++;
    t->cursor_col = 0;

    /* Insert auto-indent */
    for (int i = 0; i < ws && i < t->cursor_col + ws; i++) {
        buf_insert_char(&t->buf, t->cursor_row, t->cursor_col,
                        (unsigned char)l->data[i]);
        t->cursor_col++;
    }
    g.needs_redraw = true;
}

void editor_delete_char_backward(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    if (t->cursor_col > 0) {
        /* find start of previous UTF-8 char */
        int col = t->cursor_col - 1;
        while (col > 0 && (t->buf.lines[t->cursor_row].data[col] & 0xC0) == 0x80) col--;
        buf_delete_char(&t->buf, t->cursor_row, col);
        t->cursor_col = col;
    } else if (t->cursor_row > 0) {
        int prev_len = t->buf.lines[t->cursor_row - 1].len;
        buf_join_line(&t->buf, t->cursor_row - 1);
        t->cursor_row--;
        t->cursor_col = prev_len;
    }
    g.needs_redraw = true;
}

void editor_delete_char_forward(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    Line *l = &t->buf.lines[t->cursor_row];
    if (t->cursor_col < l->len) {
        buf_delete_char(&t->buf, t->cursor_row, t->cursor_col);
    } else if (t->cursor_row + 1 < t->buf.num_lines) {
        buf_join_line(&t->buf, t->cursor_row);
    }
    g.needs_redraw = true;
}

void editor_save(void) {
    if (g.active_tab < 0) return;
    Tab *t = &g.tabs[g.active_tab];
    if (buf_save(&t->buf))
        snprintf(g.status_msg, sizeof(g.status_msg), "Saved: %s", t->buf.filepath);
    else
        snprintf(g.status_msg, sizeof(g.status_msg), "Error saving file!");
    g.needs_redraw = true;
}

void editor_search_start(void) {
    g.mode = MODE_SEARCH;
    g.cmd_len = 0;
    g.cmd_buf[0] = '\0';
}

void editor_search_next(void) {
    if (g.active_tab < 0 || !g.cmd_buf[0]) return;
    Tab *t = &g.tabs[g.active_tab];
    int start_row = t->cursor_row;
    int start_col = t->cursor_col + 1;
    for (int r = start_row; r < t->buf.num_lines; r++) {
        Line *l = &t->buf.lines[r];
        const char *found = strstr(l->data + (r == start_row ? start_col : 0), g.cmd_buf);
        if (found) {
            t->cursor_row = r;
            t->cursor_col = (int)(found - l->data);
            g.needs_redraw = true;
            return;
        }
    }
    /* Wrap */
    for (int r = 0; r <= start_row; r++) {
        Line *l = &t->buf.lines[r];
        const char *found = strstr(l->data, g.cmd_buf);
        if (found) {
            t->cursor_row = r;
            t->cursor_col = (int)(found - l->data);
            g.needs_redraw = true;
            return;
        }
    }
    snprintf(g.status_msg, sizeof(g.status_msg), "Pattern not found: %s", g.cmd_buf);
}

/* ── Handle input event (editor pane is active) ── */

void editor_handle_event(InputEvent *ev) {
    if (g.active_tab < 0) {
        /* No tab: only handle file-open shortcuts from outside */
        return;
    }

    /* Command mode */
    if (g.mode == MODE_COMMAND) {
        if (ev->key == KEY_ENTER) {
            /* TODO: execute command in cmd_buf */
            g.mode = MODE_NORMAL;
            g.needs_redraw = true;
        } else if (ev->key == KEY_ESCAPE) {
            g.mode = MODE_NORMAL;
            g.cmd_len = 0;
            g.cmd_buf[0] = '\0';
            g.needs_redraw = true;
        } else if (ev->key == KEY_BACKSPACE && g.cmd_len > 0) {
            g.cmd_buf[--g.cmd_len] = '\0';
            g.needs_redraw = true;
        } else if (ev->codepoint >= 32 && g.cmd_len < (int)sizeof(g.cmd_buf) - 1) {
            char tmp[8]; int n;
            uint32_t cp = ev->codepoint;
            if (cp < 0x80) { tmp[0]=(char)cp; n=1; }
            else if (cp < 0x800) { tmp[0]=(char)(0xC0|(cp>>6)); tmp[1]=(char)(0x80|(cp&0x3F)); n=2; }
            else { tmp[0]=(char)(0xE0|(cp>>12)); tmp[1]=(char)(0x80|((cp>>6)&0x3F)); tmp[2]=(char)(0x80|(cp&0x3F)); n=3; }
            memcpy(g.cmd_buf + g.cmd_len, tmp, n);
            g.cmd_len += n;
            g.cmd_buf[g.cmd_len] = '\0';
            g.needs_redraw = true;
        }
        return;
    }

    /* Search mode */
    if (g.mode == MODE_SEARCH) {
        if (ev->key == KEY_ENTER) {
            editor_search_next();
            g.mode = MODE_NORMAL;
        } else if (ev->key == KEY_ESCAPE) {
            g.mode = MODE_NORMAL;
            g.cmd_len = 0; g.cmd_buf[0] = '\0';
            g.needs_redraw = true;
        } else if (ev->key == KEY_BACKSPACE && g.cmd_len > 0) {
            g.cmd_buf[--g.cmd_len] = '\0';
            g.needs_redraw = true;
        } else if (ev->codepoint >= 32 && g.cmd_len < (int)sizeof(g.cmd_buf) - 1) {
            char tmp[8]; int n;
            uint32_t cp = ev->codepoint;
            if (cp < 0x80) { tmp[0]=(char)cp; n=1; }
            else if (cp < 0x800) { tmp[0]=(char)(0xC0|(cp>>6)); tmp[1]=(char)(0x80|(cp&0x3F)); n=2; }
            else { tmp[0]=(char)(0xE0|(cp>>12)); tmp[1]=(char)(0x80|((cp>>6)&0x3F)); tmp[2]=(char)(0x80|(cp&0x3F)); n=3; }
            memcpy(g.cmd_buf + g.cmd_len, tmp, n);
            g.cmd_len += n;
            g.cmd_buf[g.cmd_len] = '\0';
            editor_search_next();
            g.needs_redraw = true;
        }
        return;
    }

    /* Insert mode */
    if (g.mode == MODE_INSERT) {
        switch (ev->key) {
            case KEY_ESCAPE:
                g.mode = MODE_NORMAL;
                editor_move_left(); /* back one */
                break;
            case KEY_ENTER:      editor_insert_newline();       break;
            case KEY_BACKSPACE:  editor_delete_char_backward(); break;
            case KEY_DELETE:     editor_delete_char_forward();  break;
            case KEY_ARROW_UP:   editor_move_up();              break;
            case KEY_ARROW_DOWN: editor_move_down();            break;
            case KEY_ARROW_LEFT: editor_move_left();            break;
            case KEY_ARROW_RIGHT:editor_move_right();           break;
            case KEY_HOME:       editor_move_home();            break;
            case KEY_END:        editor_move_end();             break;
            case KEY_PAGE_UP:    editor_move_page_up();         break;
            case KEY_PAGE_DOWN:  editor_move_page_down();       break;
            case KEY_TAB:
                for (int i = 0; i < 4; i++) editor_insert_char(' ');
                break;
            default:
                if (ev->codepoint >= 32)
                    editor_insert_char(ev->codepoint);
                break;
        }
        g.needs_redraw = true;
        return;
    }

    /* Normal mode (Vim-like) */
    switch (ev->key) {
        case KEY_ARROW_UP:   editor_move_up();    break;
        case KEY_ARROW_DOWN: editor_move_down();   break;
        case KEY_ARROW_LEFT: editor_move_left();   break;
        case KEY_ARROW_RIGHT:editor_move_right();  break;
        case KEY_HOME:       editor_move_home();   break;
        case KEY_END:        editor_move_end();    break;
        case KEY_PAGE_UP:    editor_move_page_up();    break;
        case KEY_PAGE_DOWN:  editor_move_page_down();  break;
        default: break;
    }

    if (ev->codepoint) {
        uint32_t c = ev->codepoint;
        Tab *t = &g.tabs[g.active_tab];
        switch (c) {
            case 'i': g.mode = MODE_INSERT; break;
            case 'I': editor_move_home(); g.mode = MODE_INSERT; break;
            case 'a': editor_move_right(); g.mode = MODE_INSERT; break;
            case 'A': editor_move_end();   g.mode = MODE_INSERT; break;
            case 'o':
                editor_move_end();
                editor_insert_newline();
                g.mode = MODE_INSERT;
                break;
            case 'O':
                if (t->cursor_row > 0) t->cursor_row--;
                editor_move_end();
                editor_insert_newline();
                g.mode = MODE_INSERT;
                break;
            case 'h': editor_move_left();  break;
            case 'j': editor_move_down();  break;
            case 'k': editor_move_up();    break;
            case 'l': editor_move_right(); break;
            case 'w': editor_move_word_forward();  break;
            case 'b': editor_move_word_backward(); break;
            case '0': t->cursor_col = 0; g.needs_redraw = true; break;
            case '$': editor_move_end(); break;
            case 'g': t->cursor_row = 0; t->cursor_col = 0; g.needs_redraw = true; break;
            case 'G':
                t->cursor_row = t->buf.num_lines - 1;
                editor_clamp_cursor(t);
                g.needs_redraw = true;
                break;
            case 'x': editor_delete_char_forward();  break;
            case 'X': editor_delete_char_backward(); break;
            case 'd':
                /* dd = delete line (simplified: just delete current line) */
                buf_delete_line(&t->buf, t->cursor_row);
                editor_clamp_cursor(t);
                g.needs_redraw = true;
                break;
            case ':': g.mode = MODE_COMMAND; g.cmd_len = 0; g.cmd_buf[0] = '\0'; break;
            case '/': editor_search_start(); break;
            case 'n': editor_search_next(); break;
            default: break;
        }
    }
    g.needs_redraw = true;
}
