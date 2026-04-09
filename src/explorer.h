#ifndef YOTTA_EXPLORER_H
#define YOTTA_EXPLORER_H

#include "types.h"

/* Initialize explorer at the given directory */
void explorer_init(const char *dir);

/* Refresh the flat file list from the directory tree */
void explorer_refresh(void);

/* Expand/collapse the selected node */
void explorer_toggle(void);

/* Move selection up/down */
void explorer_move_up(void);
void explorer_move_down(void);

/* Open the selected file in the editor */
void explorer_open_selected(void);

/* Handle an input event while explorer is focused */
void explorer_handle_event(InputEvent *ev);

#endif /* YOTTA_EXPLORER_H */
