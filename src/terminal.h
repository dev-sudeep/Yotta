#ifndef YOTTA_TERMINAL_H
#define YOTTA_TERMINAL_H

#include "types.h"

/* Spawn a PTY shell (bash/zsh/dash). Returns true on success. */
bool pty_spawn(void);

/* Kill the PTY process */
void pty_kill(void);

/* Write user input to the PTY master */
void pty_write(const char *buf, int len);

/* Read available output from PTY into g.pty.buf */
void pty_read_pending(void);

/* Handle an input event while terminal pane is active */
void terminal_handle_event(InputEvent *ev);

/* Resize the PTY to new dimensions */
void pty_resize(int rows, int cols);

#endif /* YOTTA_TERMINAL_H */
