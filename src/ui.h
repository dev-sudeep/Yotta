#ifndef YOTTA_UI_H
#define YOTTA_UI_H

#include "types.h"

/* ── Screen cell ── */
typedef struct {
    uint32_t ch;      /* Unicode codepoint */
    char     fg[32];  /* ANSI fg sequence */
    char     bg[32];  /* ANSI bg sequence */
    bool     bold;
    bool     italic;
} Cell;

/* Initialize/destroy the screen buffer */
void ui_init(void);
void ui_free(void);

/* Resize screen buffers to new dimensions */
void ui_resize(int rows, int cols);

/* Write a cell into the back-buffer */
void ui_put_cell(int row, int col, uint32_t ch,
                 const char *fg, const char *bg,
                 bool bold, bool italic);

/* Write a string into the back-buffer starting at (row,col) */
int  ui_put_str(int row, int col, const char *s,
                const char *fg, const char *bg,
                bool bold, bool italic);

/* Fill a rectangle with a background color */
void ui_fill_rect(int row, int col, int w, int h,
                  const char *bg);

/* Draw a box using box-drawing chars */
void ui_draw_box(int row, int col, int w, int h,
                 const char *fg, const char *bg, bool active);

/* Draw a horizontal/vertical separator */
void ui_hline(int row, int col, int len, const char *fg, const char *bg);
void ui_vline(int row, int col, int len, const char *fg, const char *bg);

/* Flush back-buffer to front-buffer (differential update) */
void ui_flush(void);

/* Clear screen and reset */
void ui_clear_screen(void);

/* Show/hide cursor */
void ui_cursor_show(int row, int col);
void ui_cursor_hide(void);

/* Enable/disable terminal mouse tracking */
void ui_mouse_enable(void);
void ui_mouse_disable(void);

/* OSC 11 soft dark background hint */
void ui_osc_background(void);

/* Clamp helpers */
static inline int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Render the full UI */
void ui_render(void);

/* High-level pane renderers (called from ui_render) */
void ui_render_explorer(void);
void ui_render_editor(void);
void ui_render_chat(void);
void ui_render_terminal(void);
void ui_render_status_bar(void);
void ui_render_tab_bar(void);

#endif /* YOTTA_UI_H */
