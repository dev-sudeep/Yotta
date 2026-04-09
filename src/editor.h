#ifndef YOTTA_EDITOR_H
#define YOTTA_EDITOR_H

#include "types.h"

/* ── Buffer operations ── */
Buffer *buf_new(void);
void    buf_free(Buffer *b);
bool    buf_load(Buffer *b, const char *filepath);
bool    buf_save(Buffer *b);
bool    buf_save_as(Buffer *b, const char *filepath);

/* Ensure line capacity */
void    buf_line_ensure(Line *l, int needed);

/* Insert/delete within the buffer */
void    buf_insert_char(Buffer *b, int row, int col, uint32_t cp);
void    buf_delete_char(Buffer *b, int row, int col);  /* delete char AT col */
void    buf_insert_newline(Buffer *b, int row, int col);
void    buf_delete_line(Buffer *b, int row);
void    buf_join_line(Buffer *b, int row); /* join row with row+1 */

/* ── Tab / editor operations ── */
void editor_init(void);
int  editor_open_file(const char *filepath);   /* returns tab index */
bool editor_close_tab(int idx);
void editor_switch_tab(int idx);

/* Cursor movement */
void editor_move_left(void);
void editor_move_right(void);
void editor_move_up(void);
void editor_move_down(void);
void editor_move_home(void);
void editor_move_end(void);
void editor_move_page_up(void);
void editor_move_page_down(void);
void editor_move_word_forward(void);
void editor_move_word_backward(void);

/* Editing */
void editor_insert_char(uint32_t cp);
void editor_insert_newline(void);
void editor_delete_char_backward(void); /* backspace */
void editor_delete_char_forward(void);  /* del */

/* Save / search */
void editor_save(void);
void editor_search_start(void);
void editor_search_next(void);

/* Handle input event while editor pane is active */
void editor_handle_event(InputEvent *ev);

/* Clamp the cursor to valid position in current tab */
void editor_clamp_cursor(Tab *t);

#endif /* YOTTA_EDITOR_H */
